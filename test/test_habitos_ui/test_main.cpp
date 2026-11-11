// Fase 2 do app Habitos: Pular, cartao/lista (HabitosVista), lembretes e deteccao de pegada (Lembretes). Logica pura.
#include <unity.h>

#include <cmath>
#include <memory>
#include <string>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/Habitos.h"
#include "tempo/HabitosVista.h"
#include "tempo/Lembretes.h"
#include "tempo/Motor.h"
#include "tempo/Parametros.h"
#include "tempo/Recarga.h"

using namespace tempo;

// Penalidades LIGADAS explicitamente: estes testes cobrem as regras de punicao (o padrao do produto e desligado; ver
// param::kPenalidadesAtivas e os testes "penalidades_desligadas_*").
void setUp() { tempo::definirPenalidadesParaTeste(true); }
void tearDown() { tempo::definirPenalidadesParaTeste(tempo::param::kPenalidadesAtivas); }

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

    // ---- despertar por timer atrasado (achado 4): o lembrete que motivou o despertar toca mesmo passada a tolerancia
    void test_lembrete_atrasado_so_toca_com_aviso_forcado_do_despertar() {
        const auto l = lista({def("al", "Almo\xC3\xA7o", {"12:30"})});
        // acordou 20 min depois do alvo (oscilador RC): sem forcar, o aviso inicial e calado (tolerancia de 5 min)
        Estado e = estadoCom(l, 12 * 60 + 50);
        AgendaLembretes calado;
        TEST_ASSERT_FALSE(calado.avaliar(e, 12 * 60 + 50, 100000, false).has_value());
        // boot de lembrete: forca SO a ocorrencia do alvo (12:30) -> toca, como aviso inicial, uma vez
        AgendaLembretes ag;
        ag.forcarAvisoInicial(e, 12 * 60 + 30);
        auto a = ag.avaliar(e, 12 * 60 + 50, 100000, false);
        TEST_ASSERT_TRUE(a.has_value());
        TEST_ASSERT_FALSE(a->repeticao);
        TEST_ASSERT_EQUAL_STRING("Almo\xC3\xA7o", a->nome.c_str());
        TEST_ASSERT_FALSE(ag.avaliar(e, 12 * 60 + 51, 101000, false).has_value());   // so uma vez
    }

    void test_forcar_aviso_so_vale_para_o_minuto_do_alvo_e_pendentes() {
        const auto l = lista({def("al", "Almo\xC3\xA7o", {"12:30"}), def("de", "Dentes", {"12:00"})});
        Estado e = estadoCom(l, 12 * 60 + 40);
        AgendaLembretes ag;
        ag.forcarAvisoInicial(e, 12 * 60 + 30);   // so o 12:30; o 12:00 (10 min atrasado) continua calado
        auto a = ag.avaliar(e, 12 * 60 + 40, 100000, false);
        TEST_ASSERT_TRUE(a.has_value());
        TEST_ASSERT_EQUAL_STRING("Almo\xC3\xA7o", a->nome.c_str());
        TEST_ASSERT_EQUAL_INT(0, a->outros);
        // ja feita: nao toca
        Estado f = estadoCom(l, 12 * 60 + 40);
        const size_t idx = achar(f, "al");
        std::vector<ItemFila> fila;
        marcarFeita(f, idx, fila);
        AgendaLembretes b;
        b.forcarAvisoInicial(f, 12 * 60 + 30);
        TEST_ASSERT_FALSE(b.avaliar(f, 12 * 60 + 40, 100000, false).has_value());
    }

    // ---- releitura do SD so quando precisa (achado 3)
    void test_recarga_so_com_sync_terminada_virada_ou_tela() {
        EntradaRecarga e;
        e.versaoSync = 3;
        e.versaoVista = 3;
        TEST_ASSERT_FALSE(deveReler(e));            // nada mudou: nao relê (antes relia a cada 60 s)
        e.versaoSync = 4;
        TEST_ASSERT_TRUE(deveReler(e));             // a sync terminou
        e.versaoSync = 3;
        e.viradaDoDia = true;
        TEST_ASSERT_TRUE(deveReler(e));             // dia virou
        e.viradaDoDia = false;
        e.pedidoTela = true;
        TEST_ASSERT_TRUE(deveReler(e));             // tela de Habitos abriu
    }

    void test_recarga_nunca_com_a_sync_rodando() {
        EntradaRecarga e;
        e.syncRodando = true;
        e.versaoSync = 9;
        e.viradaDoDia = true;
        e.pedidoTela = true;
        TEST_ASSERT_FALSE(deveReler(e));            // o job grava os arquivos: espera o sinal de "terminou"
        e.syncRodando = false;
        TEST_ASSERT_TRUE(deveReler(e));             // terminou: relê
    }

    void test_status_por_versao_nao_copia_a_cada_segundo() {
        uint32_t vista = 0;
        int copias = 0;
        for (int i = 0; i < 60; ++i)
            if (consumirVersao(vista, 0)) ++copias;   // 60 s sem sync: nenhuma copia de string
        TEST_ASSERT_EQUAL_INT(0, copias);
        for (int i = 0; i < 60; ++i)
            if (consumirVersao(vista, 1)) ++copias;   // uma sync terminou: uma copia
        TEST_ASSERT_EQUAL_INT(1, copias);
        TEST_ASSERT_TRUE(consumirVersao(vista, 2));
        TEST_ASSERT_FALSE(consumirVersao(vista, 2));
    }

    // ---- penalidades desligadas nos habitos (achado 6): medicacao esquecida no fechamento nao tira coracao
    void test_penalidades_desligadas_medicacao_esquecida_no_fechamento() {
        const auto l = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", {"08:10"})});
        definirPenalidadesParaTeste(false);
        Estado e = estadoCom(l, 23 * 60 + 59);
        const int antes = e.coracoesMeios;
        fecharDia(e, false);
        TEST_ASSERT_TRUE(e.coracoesMeios >= antes);   // nenhum coracao perdido (so poderia ganhar)
        definirPenalidadesParaTeste(true);
        Estado f = estadoCom(l, 23 * 60 + 59);
        const int antesF = f.coracoesMeios;
        fecharDia(f, false);
        TEST_ASSERT_TRUE(f.coracoesMeios < antesF + 1);   // com punicao: medicacao esquecida custa -1 coracao (-2 meios)
        TEST_ASSERT_TRUE(f.coracoesMeios < e.coracoesMeios);
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


    // ============================================================ Habitos v2: desfazer
    // Marca como a tela faz: captura antes, marca (motor + fila), fecha a captura.
    InstanteHabito marcarComCaptura(Estado& e, size_t i, std::vector<ItemFila>& fila, ResultadoOcorrencia* saida = nullptr) {
        InstanteHabito s = capturarHabito(e, i);
        const auto r = marcarFeita(e, i, fila);
        fecharCapturaHabito(s, e);
        if (saida) *saida = r;
        return s;
    }

    void test_desfazer_feito_no_horario_restaura_tudo() {
        Estado e = estadoCom(rotina(), 12 * 60 + 30);
        const Estado antes = e;
        std::vector<ItemFila> fila;
        ResultadoOcorrencia r;
        const InstanteHabito s = marcarComCaptura(e, achar(e, "al"), fila, &r);
        TEST_ASSERT_TRUE(r.registrado);
        TEST_ASSERT_TRUE(e.saldo > antes.saldo);
        TEST_ASSERT_TRUE(podeDesfazerHabito(e, s));
        TEST_ASSERT_TRUE(desfazerOcorrencia(e, s));
        TEST_ASSERT_EQUAL_FLOAT(antes.saldo, e.saldo);
        TEST_ASSERT_EQUAL_FLOAT(antes.xpTotal, e.xpTotal);
        TEST_ASSERT_EQUAL_FLOAT(antes.dia.habitosCr, e.dia.habitosCr);
        TEST_ASSERT_EQUAL_FLOAT(antes.dia.crGanho, e.dia.crGanho);
        TEST_ASSERT_EQUAL_FLOAT(antes.dia.xpGanho, e.dia.xpGanho);
        for (size_t a = 0; a < kNumAtributos; ++a) TEST_ASSERT_EQUAL_FLOAT(antes.pontosAtributo[a], e.pontosAtributo[a]);
        TEST_ASSERT_EQUAL_UINT((unsigned)antes.dia.eventos.size(), (unsigned)e.dia.eventos.size());
        const auto& oc = e.dia.ocorrencias[achar(e, "al")];
        TEST_ASSERT_FALSE(oc.feita);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Pendente, (int)oc.status);
        TEST_ASSERT_EQUAL_INT(-1, oc.minutoReal);
        TEST_ASSERT_EQUAL_INT(antes.streak, e.streak);
        // pendente de novo: volta a ser o cartao automatico
        TEST_ASSERT_EQUAL_INT((int)achar(e, "al"), (int)montarCartao(e, 12 * 60 + 30, std::nullopt).indice);
        // nao desfaz duas vezes
        TEST_ASSERT_FALSE(desfazerOcorrencia(e, s));
    }

    void test_desfazer_pular_e_medicacao_atrasada_coracoes() {
        Estado e = estadoCom(rotina(), 8 * 60 + 5);
        const size_t su = achar(e, "su");
        InstanteHabito s = capturarHabito(e, su);
        TEST_ASSERT_TRUE(pularOcorrencia(e, su).registrado);
        fecharCapturaHabito(s, e);
        TEST_ASSERT_TRUE(desfazerOcorrencia(e, s));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Pendente, (int)e.dia.ocorrencias[su].status);
        // medicacao feita muito depois da janela: -coracoes; desfazer devolve
        Estado m = estadoCom(rotina(), 10 * 60);
        const int coracoes = m.coracoesMeios;
        std::vector<ItemFila> fila;
        ResultadoOcorrencia r;
        const InstanteHabito sm = marcarComCaptura(m, achar(m, "md", 0), fila, &r);
        TEST_ASSERT_TRUE(m.coracoesMeios < coracoes);
        TEST_ASSERT_TRUE(desfazerOcorrencia(m, sm));
        TEST_ASSERT_EQUAL_INT(coracoes, m.coracoesMeios);
        TEST_ASSERT_EQUAL_INT(0, m.dia.coracoesDeltaMeios);
    }

    void test_desfazer_respeita_teto_de_cr_e_recusa_se_algo_mudou() {
        Estado e = estadoCom(rotina(), 6 * 60 + 30);
        std::vector<ItemFila> fila;
        const InstanteHabito s = marcarComCaptura(e, achar(e, "ac"), fila);   // academia 15 cr
        TEST_ASSERT_EQUAL_FLOAT(15.0, e.dia.habitosCr);
        // outra marcacao depois (eventos mudaram): a captura antiga nao pode mais desfazer
        avancarRelogio(e, 8 * 60);
        marcarFeita(e, achar(e, "su"), fila);
        TEST_ASSERT_FALSE(podeDesfazerHabito(e, s));
        TEST_ASSERT_FALSE(desfazerOcorrencia(e, s));
        TEST_ASSERT_TRUE(e.dia.ocorrencias[achar(e, "ac")].feita);
        // captura invalida / dia diferente
        TEST_ASSERT_FALSE(podeDesfazerHabito(e, InstanteHabito{}));
        Estado f = estadoCom(rotina(), 8 * 60);
        std::vector<ItemFila> fila2;
        const InstanteHabito sf = marcarComCaptura(f, achar(f, "su"), fila2);
        f.dia.data = {2026, 11, 11};
        TEST_ASSERT_FALSE(podeDesfazerHabito(f, sf));
        // dia fechado: nao desfaz
        Estado g = estadoCom(rotina(), 8 * 60);
        const InstanteHabito sg = marcarComCaptura(g, achar(g, "su"), fila2);
        g.dia.fechado = true;
        TEST_ASSERT_FALSE(desfazerOcorrencia(g, sg));
    }

    void test_desfazer_fila_remove_substitui_ou_enfileira_correcao() {
        Estado e = estadoCom(rotina(), 8 * 60 + 10);
        std::vector<ItemFila> fila;
        // 1a dose da medicacao: valor 1 / meta 2 na fila
        const InstanteHabito s0 = marcarComCaptura(e, achar(e, "md", 0), fila);
        TEST_ASSERT_EQUAL_UINT(1, (unsigned)fila.size());
        // desfazer ainda nao enviado, sobrou 0 feitas: sai da fila
        TEST_ASSERT_TRUE(desfazerOcorrencia(e, s0));
        TEST_ASSERT_EQUAL_INT((int)DestinoDesfazer::RemovidoDaFila, (int)desfazerNaFila(fila, itemFilaDoHabito(e, "md")));
        TEST_ASSERT_TRUE(fila.empty());
        // 2 doses: desfazer a 2a deixa valor 1 na fila (substitui, aceita valor menor)
        std::vector<ItemFila> f2;
        marcarFeita(e, achar(e, "md", 0), f2);
        avancarRelogio(e, 20 * 60);
        const InstanteHabito s1 = marcarComCaptura(e, achar(e, "md", 1), f2);
        TEST_ASSERT_EQUAL_FLOAT(2.0, f2[0].valor);
        f2[0].tentativas = 3;
        TEST_ASSERT_TRUE(desfazerOcorrencia(e, s1));
        TEST_ASSERT_EQUAL_INT((int)DestinoDesfazer::SubstituidoNaFila, (int)desfazerNaFila(f2, itemFilaDoHabito(e, "md")));
        TEST_ASSERT_EQUAL_UINT(1, (unsigned)f2.size());
        TEST_ASSERT_EQUAL_FLOAT(1.0, f2[0].valor);
        TEST_ASSERT_EQUAL_INT(0, f2[0].tentativas);
        // ja enviado (fila vazia): enfileira a correcao com o valor menor (inclusive 0)
        std::vector<ItemFila> f3;
        TEST_ASSERT_EQUAL_INT((int)DestinoDesfazer::CorrecaoEnfileirada, (int)desfazerNaFila(f3, itemFilaDoHabito(e, "md")));
        TEST_ASSERT_EQUAL_UINT(1, (unsigned)f3.size());
        TEST_ASSERT_EQUAL_FLOAT(1.0, f3[0].valor);
    }

    void test_log_desfeito_anula_o_evento_anterior() {
        Estado e = estadoCom(rotina(), 12 * 60 + 30);
        std::vector<ItemFila> fila;
        ResultadoOcorrencia r;
        marcarComCaptura(e, achar(e, "al"), fila, &r);
        std::vector<EventoLog> log;
        log.push_back(eventoHabito(e, r, ""));
        const EventoLog outro = [&] { EventoLog x; x.tipo = "batalha"; x.projeto = "Livro"; return x; }();
        log.push_back(outro);
        log.push_back(eventoHabitoDesfeito(e, r.indice));
        TEST_ASSERT_EQUAL_STRING("desfeito", log.back().resultado.c_str());
        removerDesfeitos(log);
        TEST_ASSERT_EQUAL_UINT(1, (unsigned)log.size());
        TEST_ASSERT_EQUAL_STRING("batalha", log[0].tipo.c_str());
        // feito, desfeito, feito de novo: sobra o ultimo
        std::vector<EventoLog> l2{eventoHabito(e, r, ""), eventoHabitoDesfeito(e, r.indice), eventoHabito(e, r, "")};
        removerDesfeitos(l2);
        TEST_ASSERT_EQUAL_UINT(1, (unsigned)l2.size());
        TEST_ASSERT_EQUAL_STRING("no_horario", l2[0].resultado.c_str());
    }

    void test_lembrete_ja_avisada_nao_toca_de_novo_na_hora() {
        Estado e = estadoCom(rotina(), 12 * 60 + 30);
        AgendaLembretes ag;
        const size_t al = achar(e, "al");
        ag.jaAvisada(e, al, 1000);
        const auto a = ag.avaliar(e, 12 * 60 + 30, 2000, false);
        TEST_ASSERT_FALSE(a.has_value());   // sem aviso inicial depois do desfazer
        const auto b = ag.avaliar(e, 12 * 60 + 33, 1000 + kIntervaloReavisoMs + 10, true);   // mas o re-aviso por movimento vale
        TEST_ASSERT_TRUE(b.has_value());
        TEST_ASSERT_TRUE(b->repeticao);
    }

    // ============================================================ Habitos v2: vista
    void test_icones_por_tipo_e_nome() {
        const auto I = [](TipoHabito t, const char* n) { return (int)iconeDoHabito(t, n); };
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Haltere, I(TipoHabito::Academia, "Academia"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Frasco, I(TipoHabito::Suplemento, "Suplemento"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Capsula, I(TipoHabito::Medicacao, "Medica\xC3\xA7\xC3\xA3o"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Talher, I(TipoHabito::Almoco, "Almo\xC3\xA7o"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Dente, I(TipoHabito::Dentes, "Escovar dentes"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Xicara, I(TipoHabito::Descanso, "Descanso"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Gota, I(TipoHabito::Banho, "Banho"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Lotus, I(TipoHabito::Meditacao, "Medita\xC3\xA7\xC3\xA3o"));
        // Geral: pelo nome
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Livro, I(TipoHabito::Geral, "Leitura"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Livro, I(TipoHabito::Geral, "Ler um pouco"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Talher, I(TipoHabito::Geral, "Jantar"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Talher, I(TipoHabito::Geral, "Caf\xC3\xA9 da manh\xC3\xA3"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Generico, I(TipoHabito::Geral, "Regar as plantas"));
    }

    void test_fracao_da_janela_e_do_dia() {
        Estado e = estadoCom(rotina(), 12 * 60 + 10);
        const auto& al = e.dia.ocorrencias[achar(e, "al")];   // 12:30, janela +-45
        TEST_ASSERT_FLOAT_WITHIN(0.01, 25.0 / 90.0, fracaoJanela(al, 12 * 60 + 10));
        TEST_ASSERT_FLOAT_WITHIN(0.01, 0.5, fracaoJanela(al, 12 * 60 + 30));
        TEST_ASSERT_FLOAT_WITHIN(0.01, 1.0, fracaoJanela(al, 13 * 60 + 15));
        // atrasado: andamento dos 120 min de atraso
        TEST_ASSERT_FLOAT_WITHIN(0.01, 30.0 / 120.0, fracaoJanela(al, 13 * 60 + 45));
        TEST_ASSERT_FLOAT_WITHIN(0.001, 1.0, fracaoJanela(al, 23 * 60));
        TEST_ASSERT_EQUAL_FLOAT(0.0, fracaoJanela(e.dia.ocorrencias[achar(e, "lv")], 12 * 60));   // sem horario
        OcorrenciaDia feita = al;
        feita.feita = true;
        feita.status = StatusHabito::NoHorario;
        TEST_ASSERT_EQUAL_FLOAT(1.0, fracaoJanela(feita, 0));
        TotaisDia t;
        TEST_ASSERT_EQUAL_FLOAT(0.0, fracaoDoDia(t));
        t.total = 10;
        t.feitas = 3;
        TEST_ASSERT_FLOAT_WITHIN(0.001, 0.3, fracaoDoDia(t));
        t.puladas = 5;
        TEST_ASSERT_FLOAT_WITHIN(0.001, 0.6, fracaoDoDia(t));
    }

    void test_grupos_da_linha_do_tempo() {
        // a rotina do mockup: 06:30 | 08:00 + 08:10 | 12:30 (Almoco) 12:30? aqui so Almoco 12:30 + Dentes 13:00 | 14:00 | 19:00 | 20:00 | 22:30
        Estado e = estadoCom(rotina(), 12 * 60 + 10);
        pularOcorrencia(e, achar(e, "ac"));
        marcarFeita(e, achar(e, "su"), *std::make_unique<std::vector<ItemFila>>());
        auto vistas = ocorrenciasDoDia(e, 12 * 60 + 10);
        const auto foco = ocorrenciaAgora(e, 12 * 60 + 10);
        const auto grupos = agruparLinhaDoTempo(vistas, foco->indice);
        // 06:30 | 08:00,08:10 | 12:30,13:00 (30 min) | 14:00 | 19:00 | 20:00 | 22:30  (a "sem horario" fica de fora)
        TEST_ASSERT_EQUAL_UINT(7, (unsigned)grupos.size());
        TEST_ASSERT_EQUAL_UINT(1, (unsigned)grupos[0].indices.size());
        TEST_ASSERT_TRUE(grupos[0].todasResolvidas && !grupos[0].todasFeitas);   // pulado
        TEST_ASSERT_EQUAL_UINT(2, (unsigned)grupos[1].indices.size());
        TEST_ASSERT_EQUAL_INT(8 * 60, grupos[1].minutoIni);
        TEST_ASSERT_EQUAL_INT(8 * 60 + 10, grupos[1].minutoFim);
        TEST_ASSERT_FALSE(grupos[1].todasResolvidas);                            // a medicacao 08:10 segue pendente
        TEST_ASSERT_EQUAL_UINT((unsigned)achar(e, "md", 0), (unsigned)grupos[1].representante);
        TEST_ASSERT_EQUAL_UINT(2, (unsigned)grupos[2].indices.size());           // Almoco 12:30 + Dentes 13:00
        TEST_ASSERT_TRUE(grupos[2].temFoco);
        TEST_ASSERT_EQUAL_UINT((unsigned)achar(e, "al"), (unsigned)grupos[2].representante);
        TEST_ASSERT_FALSE(grupos[3].temFoco);
        size_t total = 0;
        for (const auto& g : grupos) total += g.indices.size();
        TEST_ASSERT_EQUAL_UINT(vistas.size() - 1, (unsigned)total);              // todas menos a sem horario
        // limite: exatamente 30 min agrupa, 31 nao
        ListaHabitos l = lista({def("a", "Alfa", {"10:00"}), def("b", "Beta", {"10:30"}), def("c", "Gama", {"11:01"})});
        Estado f = estadoCom(l, 9 * 60);
        const auto g2 = agruparLinhaDoTempo(ocorrenciasDoDia(f, 9 * 60), std::nullopt);
        TEST_ASSERT_EQUAL_UINT(2, (unsigned)g2.size());
        TEST_ASSERT_EQUAL_UINT(2, (unsigned)g2[0].indices.size());
        // tudo feito no grupo
        std::vector<ItemFila> fila;
        avancarRelogio(f, 10 * 60 + 30);
        marcarFeita(f, 0, fila);
        marcarFeita(f, 1, fila);
        const auto g3 = agruparLinhaDoTempo(ocorrenciasDoDia(f, 10 * 60 + 30), std::nullopt);
        TEST_ASSERT_TRUE(g3[0].todasFeitas && g3[0].todasResolvidas);
        // atrasado marca o grupo
        const auto g4 = agruparLinhaDoTempo(ocorrenciasDoDia(f, 14 * 60), std::nullopt);
        TEST_ASSERT_TRUE(g4[1].atrasado);
    }

    void test_linha_agora_nao_cruza_o_texto_dos_grupos() {
        const std::vector<int> pos{112, 160, 400}, meia{22, 22, 22};
        // agora em cima do texto do grupo 2 (ainda nao chegou: acima=1) -> sobe para o vao antes dele
        TEST_ASSERT_EQUAL_INT(138, posicaoLinhaAgora(165, pos, meia, 1, 100, 556));
        // idem, mas o grupo 2 ja comecou (acima=2) -> desce para o vao depois dele
        TEST_ASSERT_EQUAL_INT(182, posicaoLinhaAgora(165, pos, meia, 2, 100, 556));
        // ja num vao livre: fica onde esta
        TEST_ASSERT_EQUAL_INT(300, posicaoLinhaAgora(300, pos, meia, 2, 100, 556));
        // antes de todos / depois de todos: limites da area
        TEST_ASSERT_EQUAL_INT(60, posicaoLinhaAgora(40, pos, meia, 0, 60, 556));
        TEST_ASSERT_EQUAL_INT(556, posicaoLinhaAgora(600, pos, meia, 3, 100, 556));
        // blocos encostados (sem vao): ponto medio entre eles
        const std::vector<int> p2{100, 144};
        TEST_ASSERT_EQUAL_INT(122, posicaoLinhaAgora(120, p2, {22, 22}, 1, 60, 500));
        // nunca cruza: para qualquer yAgora o resultado fica fora de todos os blocos
        for (int y = 90; y <= 560; y += 3)
            for (size_t k = 0; k <= 3; ++k) {
                const int r = posicaoLinhaAgora(y, pos, meia, k, 60, 556);
                for (size_t i = 0; i < pos.size(); ++i) TEST_ASSERT_FALSE(r > pos[i] - 22 && r < pos[i] + 22);
            }
    }

    void test_posicoes_no_eixo_espacamento_e_limite() {
        const auto y = posicoesNoEixo({100, 110, 300}, 48, 556);
        TEST_ASSERT_EQUAL_INT(100, y[0]);
        TEST_ASSERT_EQUAL_INT(148, y[1]);   // empurrado para manter 48 px
        TEST_ASSERT_EQUAL_INT(300, y[2]);
        // estourou o limite: o ultimo fica em yMax e os anteriores sobem respeitando o espacamento
        const auto z = posicoesNoEixo({500, 520, 540, 560}, 48, 556);
        TEST_ASSERT_EQUAL_INT(556, z[3]);
        TEST_ASSERT_EQUAL_INT(508, z[2]);
        TEST_ASSERT_EQUAL_INT(460, z[1]);
        TEST_ASSERT_EQUAL_INT(412, z[0]);
        TEST_ASSERT_TRUE(posicoesNoEixo({}, 48, 556).empty());
    }

    void test_primeiro_habito_de_amanha() {
        const auto a = primeiroHabitoDeAmanha(rotina(), kTerca);
        TEST_ASSERT_TRUE(a.has_value());
        TEST_ASSERT_EQUAL_STRING("Academia", a->nome.c_str());
        TEST_ASSERT_EQUAL_INT(6 * 60 + 30, a->minuto);
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Academia, (int)a->tipo);
        TEST_ASSERT_FALSE(primeiroHabitoDeAmanha(lista({def("lv", "Ler")}), kTerca).has_value());   // so "qualquer hora"
        TEST_ASSERT_FALSE(primeiroHabitoDeAmanha(lista({}), kTerca).has_value());
    }

    void test_animacao_da_celebracao_e_janela_do_desfazer() {
        const auto zero = animacaoCelebracao(0);
        TEST_ASSERT_EQUAL_FLOAT(0.0, zero.anel);
        TEST_ASSERT_EQUAL_FLOAT(0.0, zero.disco);
        TEST_ASSERT_EQUAL_FLOAT(0.0, zero.faiscas);
        const auto meio = animacaoCelebracao(300);
        TEST_ASSERT_TRUE(meio.anel > 0.5 && meio.anel < 1.0);
        TEST_ASSERT_TRUE(meio.faiscas > 0.0 && meio.faiscas < 1.0);
        const auto fim = animacaoCelebracao(kAnimacaoCelebracaoMs + 500);
        TEST_ASSERT_EQUAL_FLOAT(1.0, fim.anel);
        TEST_ASSERT_EQUAL_FLOAT(1.0, fim.disco);
        TEST_ASSERT_EQUAL_FLOAT(1.0, fim.faiscas);
        TEST_ASSERT_TRUE(desfazerDisponivel(10000, 10000));
        TEST_ASSERT_TRUE(desfazerDisponivel(17999, 10000));
        TEST_ASSERT_FALSE(desfazerDisponivel(18000, 10000));
    }

    void test_lista_do_dia_rolagem_em_pixels_e_toque_na_linha() {
        constexpr int kH = kAlturaLinhaLista, kVis = 140;   // tela deitada: ~2,5 linhas visiveis
        TEST_ASSERT_EQUAL_INT(0, rolarPx(0, -30, 10, kH, kVis));
        TEST_ASSERT_EQUAL_INT(30, rolarPx(0, 30, 10, kH, kVis));
        TEST_ASSERT_EQUAL_INT(10 * kH - kVis, rolarPx(400, 9999, 10, kH, kVis));
        TEST_ASSERT_EQUAL_INT(0, rolarPx(0, 50, 2, kH, kVis));   // cabe tudo
        TEST_ASSERT_EQUAL_INT(0, (int)*linhaNoToque(0, 10, 10, kH));
        TEST_ASSERT_EQUAL_INT(1, (int)*linhaNoToque(0, 60, 10, kH));
        TEST_ASSERT_EQUAL_INT(3, (int)*linhaNoToque(56, 130, 10, kH));   // (130 + 56) / 56
        TEST_ASSERT_FALSE(linhaNoToque(0, -1, 10, kH).has_value());
        TEST_ASSERT_FALSE(linhaNoToque(0, 600, 3, kH).has_value());
        // foco centralizado e clampado
        TEST_ASSERT_EQUAL_INT(0, rolagemPxPara(10, 0, kH, kVis));
        TEST_ASSERT_EQUAL_INT(5 * kH - (kVis - kH) / 2, rolagemPxPara(10, 5, kH, kVis));
        TEST_ASSERT_EQUAL_INT(10 * kH - kVis, rolagemPxPara(10, 9, kH, kVis));
        // selecao: tocar numa linha seleciona; tocar na selecionada volta ao automatico (alternarSelecao)
        std::optional<size_t> sel;
        sel = alternarSelecao(sel, *linhaNoToque(56, 30, 10, kH));
        TEST_ASSERT_EQUAL_INT(1, (int)*sel);
        sel = alternarSelecao(sel, 1);
        TEST_ASSERT_FALSE(sel.has_value());
    }

    void test_estado_do_cartao_perdido_sem_feito_e_medicacao_2_toques() {
        Estado e = estadoCom(rotina(), 23 * 60);
        // 23:00: o Banho (19:00, janela +-60, atraso +2 h ate 22:00) esta perdido: so ver/pular, sem "Feito"
        const Cartao banho = montarCartao(e, 23 * 60, achar(e, "ba"));
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Perdido, (int)banho.modo);
        TEST_ASSERT_FALSE(banho.podeFeito);
        // medicacao perdida ainda aceita "Sim, tomei agora" (2 toques) e nao tem "Pular" como acao normal
        const Cartao med = montarCartao(e, 23 * 60, achar(e, "md", 1));
        TEST_ASSERT_TRUE(med.confirmar2Toques && med.podeFeito);
        // depois de marcar (celebracao na tela), o cartao automatico vai para o proximo pendente
        Estado f = estadoCom(rotina(), 12 * 60 + 31);
        for (const char* id : {"ac", "su"}) pularOcorrencia(f, achar(f, id));
        pularOcorrencia(f, achar(f, "md", 0));
        std::vector<ItemFila> fila;
        marcarFeita(f, achar(f, "al"), fila);
        const Cartao prox = montarCartao(f, 12 * 60 + 31, std::nullopt);
        TEST_ASSERT_EQUAL_INT((int)achar(f, "de", 0), (int)prox.indice);   // Escovar 13:00
        TEST_ASSERT_EQUAL_INT((int)ModoCartao::Proximo, (int)prox.modo);
    }

    void test_icone_do_ticktick_e_cor() {
        const auto I = [](const char* nome, const char* res, TipoHabito t = TipoHabito::Geral) { return (int)iconeDoHabito(t, nome, res); };
        // (a) habit_* por palavra
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Livro, I("x", "habit_reading"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Lotus, I("x", "habit_meditating"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Haltere, I("x", "habit_workout"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Gota, I("x", "habit_drink_water"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Frasco, I("x", "habit_vitamins"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Capsula, I("x", "habit_medicine"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Talher, I("x", "habit_eat_breakfast"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Dente, I("x", "habit_brush_teeth"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Xicara, I("x", "habit_coffee"));
        // habit_* da conta real (antes Generico) -> Phosphor: cerebro / calendar-check / prohibit / check-circle
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Cerebro, I("Regar plantas", "habit_stay_positive"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Plano, I("Regar plantas", "habit_plan"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Proibido, I("Regar plantas", "habit_quit_snacks"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::CheckDiario, I("Regar plantas", "habit_daily_check_in"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Sol, I("x", "habit_morning"));
        // habit_* sem mapa -> nome/tipo -> Generico (star)
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Generico, I("Regar plantas", "habit_zzz"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Dente, I("Escovar dentes", "habit_zzz", TipoHabito::Dentes));
        // nome: manha -> sol (cafe da manha continua talher)
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Sol, I("Rotina da manh\xC3\xA3", ""));
        // (b) emoji
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Xicara, I("x", "txt_\xE2\x98\x95"));     // cafe
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Livro, I("x", "txt_\xF0\x9F\x93\x96"));  // livro
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Capsula, I("x", "txt_\xF0\x9F\x92\x8A"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Proibido, I("x", "txt_\xF0\x9F\x9A\xAB"));  // proibido
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Cerebro, I("x", "txt_\xF0\x9F\xA7\xA0"));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Generico, I("x", "txt_\xF0\x9F\x90\xB6"));  // sem mapa -> generico
        // iconRes vence o tipo; vazio cai no nome; o tipo/valores nao mudam
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Livro, I("Medica\xC3\xA7\xC3\xA3o", "habit_reading", TipoHabito::Medicacao));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Capsula, I("Medica\xC3\xA7\xC3\xA3o", "", TipoHabito::Medicacao));
        // (c) palavras do nome
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Livro, I("Estudo de ingles", ""));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Haltere, I("Corrida", ""));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Gota, I("Hidratar", ""));
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Talher, I("Caf\xC3\xA9 da manh\xC3\xA3", ""));
        // cor
        TEST_ASSERT_EQUAL_INT(-1, corDoHabito(""));
        TEST_ASSERT_EQUAL_INT(-1, corDoHabito("A0EFED"));
        TEST_ASSERT_EQUAL_INT(-1, corDoHabito("#GG0000"));
        TEST_ASSERT_EQUAL_INT(0xA0EFED, corDoHabito("#A0EFED"));   // ja claro: intacta
        const int32_t escura = corDoHabito("#101030");
        TEST_ASSERT_TRUE(escura > 0x101030);
        const double lum = (0.2126 * ((escura >> 16) & 255) + 0.7152 * ((escura >> 8) & 255) + 0.0722 * (escura & 255)) / 255.0;
        TEST_ASSERT_TRUE(lum >= 0.42);
        TEST_ASSERT_TRUE(corDoHabito("#000000") > 0);
    }

    void test_icone_e_cor_preservados_do_json_ao_dia_e_json_antigo() {
        ListaHabitos l;
        const char* novo = R"({"fonte":"ticktick","gerado_em":"x","habitos":[{"id":"a","name":"Ler","repeatRule":"RRULE:FREQ=DAILY;INTERVAL=1","reminders":["09:00"],"goal":1,"targetStartDate":20260101,"iconRes":"habit_reading","color":"#A0EFED"}]})";
        TEST_ASSERT_TRUE(lerHabitos(novo, l));
        TEST_ASSERT_EQUAL_STRING("habit_reading", l.habitos[0].iconRes.c_str());
        TEST_ASSERT_EQUAL_STRING("#A0EFED", l.habitos[0].color.c_str());
        Estado e = estadoCom(l, 9 * 60);
        TEST_ASSERT_EQUAL_STRING("habit_reading", e.dia.ocorrencias[0].iconRes.c_str());
        const auto v = ocorrenciasDoDia(e, 9 * 60);
        TEST_ASSERT_EQUAL_INT((int)IconeHabito::Livro, (int)v[0].icone);
        TEST_ASSERT_EQUAL_INT(0xA0EFED, v[0].cor);
        // muda no TickTick: pendente acompanha
        l.habitos[0].iconRes = "habit_meditating";
        reconciliarDia(e, l);
        TEST_ASSERT_EQUAL_STRING("habit_meditating", e.dia.ocorrencias[0].iconRes.c_str());
        // JSON antigo (sem os campos) continua valido; estado serializado vai e volta com os campos
        const char* antigo = R"({"fonte":"ticktick","gerado_em":"x","habitos":[{"id":"a","name":"Ler","repeatRule":"","reminders":["09:00"],"goal":1,"targetStartDate":0}]})";
        ListaHabitos o;
        TEST_ASSERT_TRUE(lerHabitos(antigo, o));
        TEST_ASSERT_EQUAL_STRING("", o.habitos[0].iconRes.c_str());
        Estado f = estadoCom(o, 9 * 60);
        TEST_ASSERT_EQUAL_INT(-1, ocorrenciasDoDia(f, 9 * 60)[0].cor);
        const std::string js = serializarEstado(e);
        Estado volta;
        TEST_ASSERT_TRUE(lerEstado(js, volta));
        TEST_ASSERT_EQUAL_STRING("habit_meditating", volta.dia.ocorrencias[0].iconRes.c_str());
        TEST_ASSERT_EQUAL_STRING("#A0EFED", volta.dia.ocorrencias[0].color.c_str());
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
    RUN_TEST(test_lembrete_atrasado_so_toca_com_aviso_forcado_do_despertar);
    RUN_TEST(test_forcar_aviso_so_vale_para_o_minuto_do_alvo_e_pendentes);
    RUN_TEST(test_recarga_so_com_sync_terminada_virada_ou_tela);
    RUN_TEST(test_recarga_nunca_com_a_sync_rodando);
    RUN_TEST(test_status_por_versao_nao_copia_a_cada_segundo);
    RUN_TEST(test_penalidades_desligadas_medicacao_esquecida_no_fechamento);
    RUN_TEST(test_reaviso_por_movimento_limites);
    RUN_TEST(test_nao_avisa_fora_da_janela_e_ligou_tarde_so_por_movimento);
    RUN_TEST(test_medicacao_insiste_15min_adia_e_para);
    RUN_TEST(test_varias_vencendo_juntas_e_virada_do_dia);
    RUN_TEST(test_adiar_e_resolvida_ignoram_indice_invalido);
    RUN_TEST(test_desfazer_feito_no_horario_restaura_tudo);
    RUN_TEST(test_desfazer_pular_e_medicacao_atrasada_coracoes);
    RUN_TEST(test_desfazer_respeita_teto_de_cr_e_recusa_se_algo_mudou);
    RUN_TEST(test_desfazer_fila_remove_substitui_ou_enfileira_correcao);
    RUN_TEST(test_log_desfeito_anula_o_evento_anterior);
    RUN_TEST(test_lembrete_ja_avisada_nao_toca_de_novo_na_hora);
    RUN_TEST(test_icones_por_tipo_e_nome);
    RUN_TEST(test_linha_agora_nao_cruza_o_texto_dos_grupos);
    RUN_TEST(test_fracao_da_janela_e_do_dia);
    RUN_TEST(test_grupos_da_linha_do_tempo);
    RUN_TEST(test_posicoes_no_eixo_espacamento_e_limite);
    RUN_TEST(test_primeiro_habito_de_amanha);
    RUN_TEST(test_animacao_da_celebracao_e_janela_do_desfazer);
    RUN_TEST(test_lista_do_dia_rolagem_em_pixels_e_toque_na_linha);
    RUN_TEST(test_estado_do_cartao_perdido_sem_feito_e_medicacao_2_toques);
    RUN_TEST(test_icone_do_ticktick_e_cor);
    RUN_TEST(test_icone_e_cor_preservados_do_json_ao_dia_e_json_antigo);
    return UNITY_END();
}
