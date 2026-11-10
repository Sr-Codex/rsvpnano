// Fase 2 do app Habitos: Pular, cartao/lista (HabitosVista), lembretes e deteccao de pegada (Lembretes). Logica pura.
#include <unity.h>

#include <cmath>
#include <string>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/Habitos.h"
#include "tempo/HabitosVista.h"
#include "tempo/Lembretes.h"
#include "tempo/Motor.h"

using namespace tempo;

void setUp() {}
void tearDown() {}

namespace {

    const Data kTerca = {2026, 11, 10};
    constexpr const char* kDiario = "RRULE:FREQ=DAILY;INTERVAL=1";

    DefHabito def(const char* id, const char* nome, std::vector<std::string> lembretes = {}) {
        DefHabito d;
        d.id = id;
        d.name = nome;
        d.repeatRule = kDiario;
        d.reminders = std::move(lembretes);
        d.targetStartDate = 20260101;
        return d;
    }

    ListaHabitos lista(std::vector<DefHabito> habitos) {
        ListaHabitos l;
        l.fonte = "ticktick";
        l.habitos = std::move(habitos);
        return l;
    }

    Estado estadoCom(const ListaHabitos& l, int minuto) {
        Estado e;
        abrirDia(e, kTerca);
        avancarRelogio(e, minuto);
        reconciliarDia(e, l);
        return e;
    }

    // rotina do mockup: Academia 06:30, Suplemento 08:00, Medicacao 08:10 e 20:00, Almoco 12:30, Dentes 13:00 e 22:30,
    // Descanso 14:00, Banho 19:00, + 1 sem horario
    ListaHabitos rotina() {
        return lista({def("ac", "Academia", {"06:30"}), def("su", "Suplemento", {"08:00"}),
                      def("md", "Medica\xC3\xA7\xC3\xA3o", {"08:10", "20:00"}), def("al", "Almo\xC3\xA7o", {"12:30"}),
                      def("de", "Escovar dentes", {"13:00", "22:30"}), def("ds", "Descanso", {"14:00"}),
                      def("ba", "Banho", {"19:00"}), def("lv", "Ler um pouco")});
    }

    size_t achar(const Estado& e, const char* id, int indice = 0) {
        for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i)
            if (e.dia.ocorrencias[i].habitoId == id && e.dia.ocorrencias[i].indice == indice) return i;
        TEST_FAIL_MESSAGE("ocorrencia nao encontrada");
        return 0;
    }

    // ================================================================ Pular
    void test_pular_nao_paga_nao_enfileira_nao_vira_esquecida() {
        Estado e = estadoCom(rotina(), 12 * 60 + 40);
        const size_t i = achar(e, "al");
        const double saldo = e.saldo, xp = e.xpTotal;
        const auto r = pularOcorrencia(e, i);
        TEST_ASSERT_TRUE(r.registrado);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Pulado, (int)r.status);
        TEST_ASSERT_TRUE(r.cr == 0.0 && r.xp == 0.0 && r.coracoesMeios == 0);
        TEST_ASSERT_TRUE(saldo == e.saldo && xp == e.xpTotal && e.dia.habitosCr == 0.0);
        TEST_ASSERT_FALSE(e.dia.ocorrencias[i].feita);
        TEST_ASSERT_EQUAL_INT(0, ocorrenciasFeitas(e, "al"));
        TEST_ASSERT_EQUAL_INT((int)FaseOcorrencia::Pulada, (int)faseDaOcorrencia(e.dia.ocorrencias[i], 12 * 60 + 40));
        // idempotente e nao pula o que ja foi feito
        TEST_ASSERT_FALSE(pularOcorrencia(e, i).registrado);
        std::vector<ItemFila> fila;
        const size_t j = achar(e, "ba");
        TEST_ASSERT_TRUE(marcarFeita(e, j, fila).registrado);
        TEST_ASSERT_FALSE(pularOcorrencia(e, j).registrado);
        TEST_ASSERT_FALSE(pularOcorrencia(e, 999).registrado);
        // evento de log do pulo
        ResultadoOcorrencia rp;
        rp.registrado = true;
        rp.indice = i;
        rp.status = StatusHabito::Pulado;
        const EventoLog ev = eventoHabito(e, rp, "");
        TEST_ASSERT_EQUAL_STRING("pulado", ev.resultado.c_str());
        TEST_ASSERT_TRUE(ev.cr == 0.0 && ev.xp == 0.0);
    }

    void test_pulada_sai_da_proxima_e_ida_e_volta_no_estado() {
        Estado e = estadoCom(rotina(), 12 * 60 + 10);   // Almoco 12:30: na janela (11:45-13:15), antes do horario
        const auto antes = ocorrenciaAgora(e, 12 * 60 + 10);
        TEST_ASSERT_TRUE(antes.has_value());
        TEST_ASSERT_EQUAL_INT((int)achar(e, "al"), (int)antes->indice);
        pularOcorrencia(e, antes->indice);
        const auto depois = ocorrenciaAgora(e, 12 * 60 + 10);
        TEST_ASSERT_TRUE(depois.has_value());
        TEST_ASSERT_TRUE(depois->indice != antes->indice);
        Estado lido;
        TEST_ASSERT_TRUE(lerEstado(serializarEstado(e), lido));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Pulado, (int)lido.dia.ocorrencias[achar(lido, "al")].status);
        // reconciliar de novo nao descongela a pulada (mesmo que a definicao mude de horario)
        ListaHabitos l = rotina();
        for (auto& h : l.habitos)
            if (h.id == "al") h.reminders = {"15:00"};
        reconciliarDia(e, l);
        const auto& oc = e.dia.ocorrencias[achar(e, "al")];
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Pulado, (int)oc.status);
        TEST_ASSERT_EQUAL_INT(12 * 60 + 30, oc.minuto);
    }

    void test_fechar_dia_pulada_fora_do_denominador() {
        // 2 ocorrencias: uma no horario + uma pulada = fracao 1,0 (+1/2 coracao); uma no horario + uma pendente = 0,5.
        const auto l = lista({def("a", "Academia", {"08:00"}), def("b", "Banho", {"09:00"})});
        Estado a = estadoCom(l, 8 * 60);
        a.coracoesMeios = 6;
        std::vector<ItemFila> fila;
        marcarFeita(a, achar(a, "a"), fila);
        pularOcorrencia(a, achar(a, "b"));
        const int antesA = a.coracoesMeios;
        fecharDia(a, false);
        Estado b = estadoCom(l, 8 * 60);
        b.coracoesMeios = 6;
        marcarFeita(b, achar(b, "a"), fila);
        fecharDia(b, false);
        TEST_ASSERT_EQUAL_INT(antesA + 2, a.coracoesMeios);   // habitos bons (+1 meio) + dia limpo (+1 meio)
        TEST_ASSERT_EQUAL_INT(6 + 1, b.coracoesMeios);        // so o dia limpo: a pendente virou esquecida (0,5 < 0,8)
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Esquecido, (int)b.dia.ocorrencias[achar(b, "b")].status);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Pulado, (int)a.dia.ocorrencias[achar(a, "b")].status);   // pulada nao vira esquecida
        // medicacao pulada nao tira coracao (esquecida tiraria 2 meios)
        const auto lm = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", {"08:00"})});
        Estado m = estadoCom(lm, 8 * 60);
        m.coracoesMeios = 6;
        pularOcorrencia(m, 0);
        fecharDia(m, false);
        TEST_ASSERT_TRUE(m.coracoesMeios >= 6);
        // so puladas: criterio de habitos ignorado (nao bloqueia nem rende coracao de habitos bons)
        Estado so = estadoCom(lm, 8 * 60);
        pularOcorrencia(so, 0);
        const auto resumo = fecharDia(so, false);
        TEST_ASSERT_EQUAL_INT(0, resumo.habitosNoHorario);
    }

    // ================================================================ cartao
    void test_cartao_automatico_proximo_agora_atrasado_completo() {
        Estado e = estadoCom(rotina(), 12 * 60 + 10);
        // todas as anteriores resolvidas: 06:30, 08:00, 08:10 pulados; "sem horario" fica para o fim
        for (const char* id : {"ac", "su"}) pularOcorrencia(e, achar(e, id));
        pularOcorrencia(e, achar(e, "md", 0));
        Cartao c = montarCartao(e, 12 * 60 + 10, std::nullopt);
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Proximo, (int)c.modo);
        TEST_ASSERT_EQUAL_INT((int)achar(e, "al"), (int)c.indice);
        TEST_ASSERT_EQUAL_INT(20, c.minutosAte);
        TEST_ASSERT_TRUE(c.podeFeito && c.podePular && !c.confirmar2Toques);
        // 12:40: Almoco no horario (na janela)
        c = montarCartao(e, 12 * 60 + 40, std::nullopt);
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Agora, (int)c.modo);
        TEST_ASSERT_EQUAL_INT(10, c.atrasoMin);
        // 14:20: Almoco e Dentes 13:00 vencidos; Descanso 14:00 (janela 5 min) atrasado 20 min. A mais cedo na janela/atrasada ganha.
        Estado f = estadoCom(rotina(), 14 * 60 + 20);
        for (const char* id : {"ac", "su", "al"}) pularOcorrencia(f, achar(f, id));
        pularOcorrencia(f, achar(f, "md", 0));
        pularOcorrencia(f, achar(f, "de", 0));
        c = montarCartao(f, 14 * 60 + 20, std::nullopt);
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Atrasado, (int)c.modo);
        TEST_ASSERT_EQUAL_INT((int)achar(f, "ds"), (int)c.indice);
        TEST_ASSERT_EQUAL_INT(20, c.atrasoMin);
        // medicacao as 20:07: 2 toques, sem "Pular"
        Estado m = estadoCom(rotina(), 20 * 60 + 7);
        for (size_t i = 0; i < m.dia.ocorrencias.size(); ++i)
            if (m.dia.ocorrencias[i].habitoId != "md" || m.dia.ocorrencias[i].indice == 0) pularOcorrencia(m, i);
        c = montarCartao(m, 20 * 60 + 7, std::nullopt);
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Agora, (int)c.modo);
        TEST_ASSERT_TRUE(c.confirmar2Toques && c.podeFeito);
        TEST_ASSERT_EQUAL_INT((int)achar(m, "md", 1), (int)c.indice);
        // tudo resolvido = dia completo
        for (size_t i = 0; i < m.dia.ocorrencias.size(); ++i) pularOcorrencia(m, i);
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::DiaCompleto, (int)montarCartao(m, 22 * 60, std::nullopt).modo);
        const auto t = totaisDoDia(m);
        TEST_ASSERT_EQUAL_INT((int)m.dia.ocorrencias.size(), t.puladas);
        TEST_ASSERT_EQUAL_INT(0, t.pendentes);
        // sem habitos
        Estado vazio;
        abrirDia(vazio, kTerca);
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::SemHabitos, (int)montarCartao(vazio, 600, std::nullopt).modo);
    }

    void test_cartao_futuro_distante_e_perdido_e_selecao() {
        Estado e = estadoCom(rotina(), 10 * 60);
        const size_t banho = achar(e, "ba");   // 19:00: janela 18:00-20:00 -> ainda futuro as 10:00
        Cartao c = montarCartao(e, 10 * 60, banho);
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Proximo, (int)c.modo);
        TEST_ASSERT_FALSE(c.podeFeito);   // "Feito agora" fora da janela cairia como esquecido
        TEST_ASSERT_TRUE(c.podePular);
        TEST_ASSERT_EQUAL_INT(9 * 60, c.minutosAte);
        // selecionar a medicacao futura: sem Pular
        const size_t med = achar(e, "md", 1);
        c = montarCartao(e, 10 * 60, med);
        TEST_ASSERT_TRUE(c.confirmar2Toques && !c.podeFeito && !c.podePular);
        // Perdida: janela + 2 h passadas. Academia 06:30 (janela 60) as 10:00
        const size_t ac = achar(e, "ac");
        c = montarCartao(e, 10 * 60, ac);
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Perdido, (int)c.modo);
        TEST_ASSERT_FALSE(c.podeFeito);
        TEST_ASSERT_TRUE(c.podePular);
        // medicacao perdida ainda deixa "Sim, tomei agora" (registra a hora real)
        c = montarCartao(e, 12 * 60, achar(e, "md", 0));
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Perdido, (int)c.modo);
        TEST_ASSERT_TRUE(c.podeFeito);
        // feita: informativo
        std::vector<ItemFila> fila;
        marcarFeita(e, achar(e, "su"), fila);
        c = montarCartao(e, 10 * 60, achar(e, "su"));
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Feito, (int)c.modo);
        TEST_ASSERT_FALSE(c.podeFeito || c.podePular);
        // indice invalido = automatico
        c = montarCartao(e, 10 * 60, 999);
        TEST_ASSERT_TRUE(c.modo != ModoCartao::SemHabitos);
    }

    void test_selecao_e_rolagem_da_lista() {
        auto s = alternarSelecao(std::nullopt, 3);
        TEST_ASSERT_TRUE(s && *s == 3);
        s = alternarSelecao(s, 4);
        TEST_ASSERT_TRUE(s && *s == 4);
        s = alternarSelecao(s, 4);   // tocar de novo volta ao automatico
        TEST_ASSERT_FALSE(s.has_value());
        // foco na 3a linha, clampado (9 itens, 5 visiveis => rolagem 0..4)
        TEST_ASSERT_EQUAL_INT(0, rolagemPara(9, 0));
        TEST_ASSERT_EQUAL_INT(1, rolagemPara(9, 3));
        TEST_ASSERT_EQUAL_INT(3, rolagemPara(9, 5));
        TEST_ASSERT_EQUAL_INT(4, rolagemPara(9, 8));
        TEST_ASSERT_EQUAL_INT(0, rolagemPara(4, 3));   // cabe tudo
        TEST_ASSERT_EQUAL_INT(0, rolar(0, -1, 9));
        TEST_ASSERT_EQUAL_INT(2, rolar(1, 1, 9));
        TEST_ASSERT_EQUAL_INT(4, rolar(4, 3, 9));
        TEST_ASSERT_EQUAL_INT(0, rolar(0, 5, 3));
    }

    void test_totais_e_minuto_proximo() {
        Estado e = estadoCom(rotina(), 12 * 60 + 10);
        std::vector<ItemFila> fila;
        marcarFeita(e, achar(e, "al"), fila);   // 12:10, janela do almoco 11:45-13:15: no horario, paga
        pularOcorrencia(e, achar(e, "ac"));
        const auto t = totaisDoDia(e);
        TEST_ASSERT_EQUAL_INT(10, t.total);
        TEST_ASSERT_EQUAL_INT(1, t.feitas);
        TEST_ASSERT_EQUAL_INT(1, t.puladas);
        TEST_ASSERT_EQUAL_INT(8, t.pendentes);
        TEST_ASSERT_TRUE(t.cr > 0.0);
        const auto p = minutoProximoHabito(e, 12 * 60 + 10);
        TEST_ASSERT_TRUE(p.has_value());
        TEST_ASSERT_EQUAL_INT(13 * 60, *p);   // almoco feito: o proximo e escovar os dentes 13:00
        TEST_ASSERT_FALSE(minutoProximoHabito(e, 23 * 60).has_value());
    }

    // ================================================================ pegada (IMU)
    void test_detector_pegada_dispara_uma_vez_e_rearma_so_quieto() {
        DetectorPegada d;
        uint32_t t = 1000;
        // repouso: nunca dispara
        for (int i = 0; i < 40; ++i, t += 50) TEST_ASSERT_FALSE(d.alimentar(t, 0.01f));
        // 1 solavanco isolado (colocar na mesa) nao basta
        TEST_ASSERT_FALSE(d.alimentar(t, 0.6f));
        t += 50;
        for (int i = 0; i < 40; ++i, t += 50) TEST_ASSERT_FALSE(d.alimentar(t, 0.01f));
        // pegada: varios picos em < 1,5 s
        int disparos = 0;
        for (int i = 0; i < 20; ++i, t += 50) disparos += d.alimentar(t, 0.5f) ? 1 : 0;
        TEST_ASSERT_EQUAL_INT(1, disparos);   // segurar a placa tremendo nao dispara de novo
        // continua mexendo: nada
        for (int i = 0; i < 60; ++i, t += 50) TEST_ASSERT_FALSE(d.alimentar(t, 0.4f));
        // quieta por >= 1,5 s: rearma
        for (int i = 0; i < 40; ++i, t += 50) d.alimentar(t, 0.02f);
        disparos = 0;
        for (int i = 0; i < 10; ++i, t += 50) disparos += d.alimentar(t, 0.5f) ? 1 : 0;
        TEST_ASSERT_EQUAL_INT(1, disparos);
        // picos espalhados (> janela) nao somam
        DetectorPegada e;
        t = 0;
        int d2 = 0;
        for (int i = 0; i < 10; ++i, t += 2000) d2 += e.alimentar(t, 0.5f) ? 1 : 0;
        TEST_ASSERT_EQUAL_INT(0, d2);
    }

    // ================================================================ lembretes
    constexpr uint32_t kMin = 60000;

    void test_lembrete_na_hora_uma_vez_e_sem_qualquer_hora() {
        const auto l = lista({def("al", "Almo\xC3\xA7o", {"12:30"}), def("lv", "Ler um pouco")});
        Estado e = estadoCom(l, 12 * 60 + 29);
        AgendaLembretes ag;
        uint32_t t = 100000;
        TEST_ASSERT_FALSE(ag.avaliar(e, 12 * 60 + 29, t, false).has_value());   // ainda nao e a hora
        auto a = ag.avaliar(e, 12 * 60 + 30, t += 1000, false);
        TEST_ASSERT_TRUE(a.has_value());
        TEST_ASSERT_EQUAL_STRING("Almo\xC3\xA7o", a->nome.c_str());
        TEST_ASSERT_FALSE(a->repeticao);
        // nao repete sem movimento, nem 1 hora depois (sem loop de bipe)
        for (int m = 31; m < 150; ++m) TEST_ASSERT_FALSE(ag.avaliar(e, 12 * 60 + m, t += kMin, false).has_value());
        // "qualquer hora" nunca avisa
        Estado so = estadoCom(lista({def("lv", "Ler um pouco")}), 10 * 60);
        AgendaLembretes b;
        for (int m = 0; m < 600; m += 5) TEST_ASSERT_FALSE(b.avaliar(so, m, 1000 + m * 1000U, true).has_value());
    }

    void test_reaviso_por_movimento_limites() {
        const auto l = lista({def("ds", "Descanso", {"14:00"})});
        Estado e = estadoCom(l, 14 * 60);
        AgendaLembretes ag;
        uint32_t t = 500000;
        TEST_ASSERT_TRUE(ag.avaliar(e, 14 * 60, t, false).has_value());            // aviso inicial
        // pegou 30 s depois: cedo demais (< 2 min)
        TEST_ASSERT_FALSE(ag.avaliar(e, 14 * 60 + 1, t + 30000, true).has_value());
        // pegou 2 min depois: re-aviso
        auto r = ag.avaliar(e, 14 * 60 + 2, t + 120000, true);
        TEST_ASSERT_TRUE(r.has_value());
        TEST_ASSERT_TRUE(r->repeticao);
        // sem movimento: nada, mesmo depois de muito tempo
        TEST_ASSERT_FALSE(ag.avaliar(e, 14 * 60 + 40, t + 40 * kMin, false).has_value());
        // movimento logo em seguida: respeita os 2 min
        TEST_ASSERT_FALSE(ag.avaliar(e, 14 * 60 + 3, t + 150000, true).has_value());
        // limite de re-avisos por ocorrencia
        int maisReavisos = 0;   // alem do 1o re-aviso, ja dado acima
        uint32_t agora = t + 120000;
        int minuto = 14 * 60 + 2;
        for (int i = 0; i < 20; ++i) {
            agora += 130000;
            minuto = 14 * 60 + 2;   // dentro da janela de atraso (ate 14:00 + 5 + 120)
            if (ag.avaliar(e, minuto, agora, true)) ++maisReavisos;
        }
        TEST_ASSERT_EQUAL_INT(kMaxReavisos - 1, maisReavisos);   // 1 ja gasto acima: no total kMaxReavisos, nunca em loop
        // atendido (feita): nunca mais
        std::vector<ItemFila> fila;
        marcarFeita(e, 0, fila);
        TEST_ASSERT_FALSE(ag.avaliar(e, 14 * 60 + 5, agora + 500000, true).has_value());
    }

    void test_nao_avisa_fora_da_janela_e_ligou_tarde_so_por_movimento() {
        const auto l = lista({def("ds", "Descanso", {"14:00"})});
        Estado e = estadoCom(l, 17 * 60);
        AgendaLembretes ag;
        // 17:00: janela + 2 h do Descanso (14:00 + 5 + 120) ja passou: perdida, sem aviso nem por movimento
        TEST_ASSERT_FALSE(ag.avaliar(e, 17 * 60, 100000, true).has_value());
        // ligou as 14:30 com o Descanso pendente: nao apita sozinho, mas ao pegar a placa avisa
        Estado f = estadoCom(l, 14 * 60 + 30);
        AgendaLembretes b;
        TEST_ASSERT_FALSE(b.avaliar(f, 14 * 60 + 30, 100000, false).has_value());
        TEST_ASSERT_FALSE(b.avaliar(f, 14 * 60 + 31, 160000, false).has_value());
        auto r = b.avaliar(f, 14 * 60 + 32, 170000, true);
        TEST_ASSERT_TRUE(r.has_value());
        TEST_ASSERT_TRUE(r->repeticao);
    }

    void test_medicacao_insiste_15min_adia_e_para() {
        const auto l = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", {"20:00"})});
        Estado e = estadoCom(l, 20 * 60);
        AgendaLembretes ag;
        uint32_t t = 1000000;
        TEST_ASSERT_TRUE(ag.avaliar(e, 20 * 60, t, false).has_value());
        TEST_ASSERT_FALSE(ag.avaliar(e, 20 * 60 + 14, t + 14 * kMin, false).has_value());
        auto r = ag.avaliar(e, 20 * 60 + 15, t + 15 * kMin, false);   // insiste sozinha, sem mexer na placa
        TEST_ASSERT_TRUE(r.has_value());
        TEST_ASSERT_TRUE(r->repeticao);
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Medicacao, (int)r->tipo);
        TEST_ASSERT_FALSE(ag.avaliar(e, 20 * 60 + 16, t + 16 * kMin, false).has_value());
        // "Ainda nao": adia 15 min a partir de agora (20:20): nem movimento avisa antes
        const uint32_t agora = t + 20 * kMin;
        ag.adiar(e, 0, agora);
        TEST_ASSERT_FALSE(ag.avaliar(e, 20 * 60 + 21, agora + 1 * kMin, true).has_value());
        TEST_ASSERT_FALSE(ag.avaliar(e, 20 * 60 + 34, agora + 14 * kMin, true).has_value());
        auto d = ag.avaliar(e, 20 * 60 + 35, agora + 15 * kMin, false);
        TEST_ASSERT_TRUE(d.has_value());
        TEST_ASSERT_TRUE(d->repeticao);
        // limite de insistencias automaticas
        int avisos = 0;
        uint32_t x = agora + 15 * kMin;
        for (int i = 0; i < 12; ++i) {
            x += 15 * kMin;
            if (ag.avaliar(e, 20 * 60 + 40, x, false)) ++avisos;   // minuto fixo dentro da janela atrasada
        }
        TEST_ASSERT_EQUAL_INT(kMaxInsistenciasMedicacao - 1, avisos);   // 1 insistencia ja consumida acima
    }

    void test_varias_vencendo_juntas_e_virada_do_dia() {
        const auto l = lista({def("a", "Academia", {"09:00"}), def("b", "Banho", {"09:00"})});
        Estado e = estadoCom(l, 9 * 60);
        AgendaLembretes ag;
        auto r = ag.avaliar(e, 9 * 60, 5000, false);
        TEST_ASSERT_TRUE(r.has_value());
        TEST_ASSERT_EQUAL_INT(1, r->outros);   // 1 aviso so, citando o outro
        TEST_ASSERT_FALSE(ag.avaliar(e, 9 * 60, 6000, false).has_value());
        // dia seguinte: estado zerado, volta a avisar
        Estado f;
        abrirDia(f, Data{2026, 11, 11});
        avancarRelogio(f, 9 * 60);
        reconciliarDia(f, l);
        TEST_ASSERT_TRUE(ag.avaliar(f, 9 * 60, 90000000, false).has_value());
    }

    void test_adiar_e_resolvida_ignoram_indice_invalido() {
        Estado e = estadoCom(rotina(), 8 * 60);
        AgendaLembretes ag;
        ag.adiar(e, 999, 0);
        ag.resolvida(e, 999);
        ag.zerar();
        TEST_PASS();
    }

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_pular_nao_paga_nao_enfileira_nao_vira_esquecida);
    RUN_TEST(test_pulada_sai_da_proxima_e_ida_e_volta_no_estado);
    RUN_TEST(test_fechar_dia_pulada_fora_do_denominador);
    RUN_TEST(test_cartao_automatico_proximo_agora_atrasado_completo);
    RUN_TEST(test_cartao_futuro_distante_e_perdido_e_selecao);
    RUN_TEST(test_selecao_e_rolagem_da_lista);
    RUN_TEST(test_totais_e_minuto_proximo);
    RUN_TEST(test_detector_pegada_dispara_uma_vez_e_rearma_so_quieto);
    RUN_TEST(test_lembrete_na_hora_uma_vez_e_sem_qualquer_hora);
    RUN_TEST(test_reaviso_por_movimento_limites);
    RUN_TEST(test_nao_avisa_fora_da_janela_e_ligou_tarde_so_por_movimento);
    RUN_TEST(test_medicacao_insiste_15min_adia_e_para);
    RUN_TEST(test_varias_vencendo_juntas_e_virada_do_dia);
    RUN_TEST(test_adiar_e_resolvida_ignoram_indice_invalido);
    return UNITY_END();
}
