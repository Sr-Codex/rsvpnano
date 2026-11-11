#include <unity.h>

#include <cmath>
#include <cstdio>
#include <string>

#include "tempo/Codec.h"
#include "tempo/Habitos.h"
#include "tempo/Motor.h"
#include "tempo/Parametros.h"
#include "tempo/TickTick.h"

using namespace tempo;

// Penalidades LIGADAS explicitamente: estes testes cobrem as regras de punicao (o padrao do produto e desligado; ver
// param::kPenalidadesAtivas e os testes "penalidades_desligadas_*").
void setUp() { tempo::definirPenalidadesParaTeste(true); }
void tearDown() { tempo::definirPenalidadesParaTeste(tempo::param::kPenalidadesAtivas); }

namespace {

    void nearImpl(double esperado, double atual, int linha) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "linha %d: esperado %.6f, atual %.6f", linha, esperado, atual);
        TEST_ASSERT_TRUE_MESSAGE(std::fabs(esperado - atual) <= 1e-6, msg);
    }
#define NEAR(esperado, atual) nearImpl((esperado), (atual), __LINE__)

    Data D(int ano, int mes, int dia) {
        Data d;
        d.ano = ano;
        d.mes = mes;
        d.dia = dia;
        return d;
    }

    const Data kTerca = {2026, 11, 10};   // terca-feira

    DefHabito def(const char* id, const char* nome, const char* regra, std::vector<std::string> lembretes = {},
                  double meta = 1.0, int inicio = 20260101) {
        DefHabito d;
        d.id = id;
        d.name = nome;
        d.repeatRule = regra;
        d.reminders = std::move(lembretes);
        d.goal = meta;
        d.targetStartDate = inicio;
        return d;
    }

    constexpr const char* kDiario = "RRULE:FREQ=DAILY;INTERVAL=1";

    Estado estadoCom(const ListaHabitos& lista, int minuto = 0) {
        Estado e;
        abrirDia(e, kTerca);
        avancarRelogio(e, minuto);
        reconciliarDia(e, lista);
        return e;
    }

    ListaHabitos lista(std::vector<DefHabito> habitos) {
        ListaHabitos l;
        l.fonte = "ticktick";
        l.habitos = std::move(habitos);
        return l;
    }

    size_t achar(const Estado& e, const char* id, int indice = 0) {
        for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i)
            if (e.dia.ocorrencias[i].habitoId == id && e.dia.ocorrencias[i].indice == indice) return i;
        TEST_FAIL_MESSAGE("ocorrencia nao encontrada");
        return 0;
    }

    // ---------------------------------------------------------------- RRULE
    void test_rrule_diaria_semanal_e_intervalos() {
        const auto diaria = lerRegra("RRULE:FREQ=DAILY;INTERVAL=1");
        TEST_ASSERT_TRUE(valeNoDia(diaria, kTerca, 20260101));
        // INTERVAL=2 a partir de 2026-11-08: vale 08, 10, 12...
        const auto dia2 = lerRegra("RRULE:FREQ=DAILY;INTERVAL=2");
        TEST_ASSERT_TRUE(valeNoDia(dia2, D(2026, 11, 10), 20261108));
        TEST_ASSERT_FALSE(valeNoDia(dia2, D(2026, 11, 11), 20261108));
        // semanal com BYDAY (terca = TU)
        const auto util = lerRegra("RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR");
        TEST_ASSERT_TRUE(valeNoDia(util, kTerca, 0));
        TEST_ASSERT_TRUE(valeNoDia(util, D(2026, 11, 13), 0));    // sexta
        TEST_ASSERT_FALSE(valeNoDia(util, D(2026, 11, 14), 0));   // sabado
        TEST_ASSERT_FALSE(valeNoDia(util, D(2026, 11, 15), 0));   // domingo
        // semana inteira (regra real da conta)
        const auto todos = lerRegra("RRULE:FREQ=WEEKLY;INTERVAL=1;BYDAY=SU,MO,TU,WE,TH,FR,SA");
        TEST_ASSERT_TRUE(valeNoDia(todos, D(2026, 11, 15), 0));
        // semanal quinzenal: inicio na semana de 2026-11-02; vale nela e 2 semanas depois, nao na do meio
        const auto quinzenal = lerRegra("RRULE:FREQ=WEEKLY;INTERVAL=2;BYDAY=TU");
        TEST_ASSERT_TRUE(valeNoDia(quinzenal, D(2026, 11, 3), 20261102));
        TEST_ASSERT_FALSE(valeNoDia(quinzenal, D(2026, 11, 10), 20261102));
        TEST_ASSERT_TRUE(valeNoDia(quinzenal, D(2026, 11, 17), 20261102));
        // semanal sem BYDAY: dia da semana do inicio (2026-11-03 = terca)
        const auto semDia = lerRegra("RRULE:FREQ=WEEKLY");
        TEST_ASSERT_TRUE(valeNoDia(semDia, kTerca, 20261103));
        TEST_ASSERT_FALSE(valeNoDia(semDia, D(2026, 11, 11), 20261103));
    }

    void test_rrule_vazia_until_inicio_e_nao_suportada() {
        TEST_ASSERT_TRUE(valeNoDia(lerRegra(""), kTerca, 0));   // vazia = todo dia
        const auto ate = lerRegra("RRULE:FREQ=DAILY;UNTIL=20261109T030000Z");
        TEST_ASSERT_FALSE(valeNoDia(ate, kTerca, 0));
        TEST_ASSERT_TRUE(valeNoDia(ate, D(2026, 11, 9), 0));
        TEST_ASSERT_FALSE(valeNoDia(lerRegra(kDiario), kTerca, 20261111));   // antes do inicio
        // COUNT e ignorado
        TEST_ASSERT_TRUE(valeNoDia(lerRegra("RRULE:FREQ=DAILY;COUNT=3"), kTerca, 20260101));
        // nao suportadas: nao geram ocorrencias
        TEST_ASSERT_FALSE(valeNoDia(lerRegra("RRULE:FREQ=HOURLY;INTERVAL=2"), kTerca, 0));
        TEST_ASSERT_FALSE(valeNoDia(lerRegra("RRULE:FREQ=MONTHLY;BYDAY=1MO"), kTerca, 0));
        TEST_ASSERT_FALSE(valeNoDia(lerRegra("RRULE:FREQ=MONTHLY;BYSETPOS=1;BYDAY=MO"), kTerca, 0));
        TEST_ASSERT_FALSE(valeNoDia(lerRegra("lixo"), kTerca, 0));
        // mensal (dia 10) e anual
        const auto mensal = lerRegra("RRULE:FREQ=MONTHLY;BYMONTHDAY=10");
        TEST_ASSERT_TRUE(valeNoDia(mensal, kTerca, 0));
        TEST_ASSERT_FALSE(valeNoDia(mensal, D(2026, 11, 11), 0));
        TEST_ASSERT_TRUE(valeNoDia(lerRegra("RRULE:FREQ=MONTHLY;BYMONTHDAY=31"), D(2026, 11, 30), 0));   // ultimo dia
        TEST_ASSERT_TRUE(valeNoDia(lerRegra("RRULE:FREQ=MONTHLY"), kTerca, 20260310));   // dia do inicio
        TEST_ASSERT_TRUE(valeNoDia(lerRegra("RRULE:FREQ=YEARLY"), kTerca, 20241110));
        TEST_ASSERT_FALSE(valeNoDia(lerRegra("RRULE:FREQ=YEARLY"), kTerca, 20241111));
    }

    // ---------------------------------------------------------------- nomes e tipos
    void test_nome_para_tela_remove_emoji_e_usa_fallback() {
        TEST_ASSERT_EQUAL_STRING("Medita\xC3\xA7\xC3\xA3o", nomeParaTela("\xF0\x9F\xA7\x98\xF0\x9F\x8F\xBC\xE2\x80\x8D\xE2\x99\x82\xEF\xB8\x8F").c_str());  // 🧘🏼‍♂️
        TEST_ASSERT_EQUAL_STRING("Leitura", nomeParaTela("\xF0\x9F\x93\x96").c_str());   // 📖
        TEST_ASSERT_EQUAL_STRING("Foco", nomeParaTela("\xF0\x9F\xA7\xA0").c_str());      // 🧠
        TEST_ASSERT_EQUAL_STRING("H\xC3\xA1" "bito", nomeParaTela("\xE2\x8F\xB3").c_str());   // ⏳ sozinho
        TEST_ASSERT_EQUAL_STRING("H\xC3\xA1" "bito", nomeParaTela("").c_str());
        TEST_ASSERT_EQUAL_STRING("Loss Time", nomeParaTela("\xE2\x8F\xB3Loss Time").c_str());
        TEST_ASSERT_EQUAL_STRING("Medica\xC3\xA7\xC3\xA3o", nomeParaTela("Medica\xC3\xA7\xC3\xA3o \xF0\x9F\x92\x8A").c_str());
        // nome com texto + emoji de meditacao mantem o texto
        TEST_ASSERT_EQUAL_STRING("Respirar", nomeParaTela("Respirar \xF0\x9F\xA7\x98").c_str());
    }

    void test_detectar_tipo_por_nome_e_emoji() {
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Academia, (int)detectarTipo("ACADEMIA \xF0\x9F\x92\xAA"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Academia, (int)detectarTipo("Treino de perna"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Medicacao, (int)detectarTipo("Medica\xC3\xA7\xC3\xA3o"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Medicacao, (int)detectarTipo("Rem\xC3\xA9" "dio da manh\xC3\xA3"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Meditacao, (int)detectarTipo("\xF0\x9F\xA7\x98\xF0\x9F\x8F\xBC\xE2\x80\x8D\xE2\x99\x82\xEF\xB8\x8F"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Meditacao, (int)detectarTipo("Meditar 10 min"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Meditacao, (int)detectarTipo("Medita\xC3\xA7\xC3\xA3o"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Suplemento, (int)detectarTipo("Suplemento"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Almoco, (int)detectarTipo("Almo\xC3\xA7o"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Banho, (int)detectarTipo("Banho gelado"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Dentes, (int)detectarTipo("Escovar os dentes"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Dentes, (int)detectarTipo("Dentes"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Descanso, (int)detectarTipo("Descanso"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Geral, (int)detectarTipo("sem dopamina at 12am"));
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Geral, (int)detectarTipo("\xF0\x9F\xA7\xA0"));
        TEST_ASSERT_TRUE(exigeConfirmacao(TipoHabito::Medicacao));
        TEST_ASSERT_FALSE(exigeConfirmacao(TipoHabito::Academia));
        TEST_ASSERT_EQUAL_STRING("medicacao remedio", normalizarNome("MEDICA\xC3\x87\xC3\x83O \xF0\x9F\x92\x8A Rem\xC3\xA9" "dio").c_str());
    }

    void test_habito_arquivado_e_sentinela_2001() {
        TEST_ASSERT_FALSE(habitoArquivado(""));
        TEST_ASSERT_FALSE(habitoArquivado("2001-01-01T00:00:00.000+0000"));   // sentinela dos clientes Apple
        TEST_ASSERT_TRUE(habitoArquivado("2024-02-13T17:21:23.000+0000"));
    }

    // ---------------------------------------------------------------- ocorrencias
    void test_ocorrencias_um_por_horario_e_completa_com_qualquer_hora() {
        const auto l = lista({def("med", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"20:00", "08:10"}),
                              def("agua", "Beber \xC3\xA1gua", kDiario, {"10:00"}, 3.0),
                              def("lib", "Livre", kDiario, {}),
                              def("dupla", "Dupla", kDiario, {"09:00", "15:00"}, 1.0)});
        const auto oc = gerarOcorrencias(l, kTerca);
        // med 2 + agua 3 (1 horario + 2 qualquer hora) + lib 1 + dupla 2 = 8
        TEST_ASSERT_EQUAL_INT(8, (int)oc.size());
        TEST_ASSERT_EQUAL_INT(8 * 60 + 10, oc[0].minuto);   // ordenado por horario
        TEST_ASSERT_EQUAL_STRING("med", oc[0].habitoId.c_str());
        TEST_ASSERT_EQUAL_INT(2, oc[0].total);
        TEST_ASSERT_EQUAL_INT(9 * 60, oc[1].minuto);
        TEST_ASSERT_EQUAL_INT(10 * 60, oc[2].minuto);
        TEST_ASSERT_EQUAL_INT(3, oc[2].total);
        TEST_ASSERT_EQUAL_INT(15 * 60, oc[3].minuto);
        TEST_ASSERT_EQUAL_INT(20 * 60, oc[4].minuto);
        for (size_t i = 5; i < 8; ++i) TEST_ASSERT_EQUAL_INT(-1, oc[i].minuto);   // sem horario por ultimo
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Medicacao, (int)oc[0].tipo);
        TEST_ASSERT_EQUAL_INT(0, oc[0].indice);
        TEST_ASSERT_EQUAL_INT(1, oc[4].indice);
    }

    void test_ocorrencias_respeitam_repeticao_e_ignoram_nao_suportado() {
        const auto l = lista({def("u", "Dia util", "RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR"),
                              def("fds", "Fim de semana", "RRULE:FREQ=WEEKLY;BYDAY=SA,SU"),
                              def("raro", "Raro", "RRULE:FREQ=MONTHLY;BYDAY=2TU"),
                              def("", "Sem id", kDiario)});
        const auto oc = gerarOcorrencias(l, kTerca);
        TEST_ASSERT_EQUAL_INT(1, (int)oc.size());
        TEST_ASSERT_EQUAL_STRING("u", oc[0].habitoId.c_str());
        TEST_ASSERT_EQUAL_INT(1, (int)gerarOcorrencias(l, D(2026, 11, 14)).size());   // sabado: so o de fim de semana
    }

    void test_reconciliar_preserva_feitas_e_remove_pendentes_que_sumiram() {
        auto l = lista({def("a", "A", kDiario, {"08:00"}), def("b", "B", kDiario, {"09:00"})});
        Estado e = estadoCom(l, 8 * 60);
        TEST_ASSERT_EQUAL_INT(2, (int)e.dia.ocorrencias.size());
        std::vector<ItemFila> fila;
        marcarFeita(e, achar(e, "a"), fila);
        // "a" e "b" somem da lista: a feita fica, a pendente sai
        TEST_ASSERT_TRUE(reconciliarDia(e, lista({def("c", "C", kDiario, {"07:00"})})));
        TEST_ASSERT_EQUAL_INT(2, (int)e.dia.ocorrencias.size());
        TEST_ASSERT_EQUAL_STRING("c", e.dia.ocorrencias[0].habitoId.c_str());
        TEST_ASSERT_EQUAL_STRING("a", e.dia.ocorrencias[1].habitoId.c_str());
        TEST_ASSERT_TRUE(e.dia.ocorrencias[1].feita);
        TEST_ASSERT_FALSE(reconciliarDia(e, lista({def("c", "C", kDiario, {"07:00"})})));   // idempotente
        // horario mudou: a pendente acompanha
        TEST_ASSERT_TRUE(reconciliarDia(e, lista({def("c", "C", kDiario, {"07:30"})})));
        TEST_ASSERT_EQUAL_INT(7 * 60 + 30, e.dia.ocorrencias[0].minuto);
    }

    // ---------------------------------------------------------------- valores e classificacao
    void test_valor_geral_e_janela_60() {
        const auto l = lista({def("g", "Beber agua", kDiario, {"10:00"})});
        Estado e = estadoCom(l);
        auto r = registrarOcorrencia(e, 0, 10 * 60 + 60);   // limite da janela
        TEST_ASSERT_TRUE(r.registrado);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)r.status);
        NEAR(2.0, r.cr);
        NEAR(5.0, r.xp);
        NEAR(2.0, e.saldo);
        TEST_ASSERT_FALSE(registrarOcorrencia(e, 0, 10 * 60).registrado);   // nao duplica
        NEAR(2.0, e.saldo);
        // atrasado: 0 cr, 50 % do XP
        Estado f = estadoCom(l);
        r = registrarOcorrencia(f, 0, 10 * 60 + 61);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Atrasado, (int)r.status);
        NEAR(0.0, r.cr);
        NEAR(2.5, r.xp);
        // esquecido: alem de janela + 2 h
        Estado g = estadoCom(l);
        r = registrarOcorrencia(g, 0, 10 * 60 + 60 + 121);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Esquecido, (int)r.status);
        NEAR(0.0, r.cr);
        NEAR(0.0, r.xp);
        TEST_ASSERT_TRUE(g.dia.ocorrencias[0].feita);   // foi marcada, so nao rendeu
    }

    void test_valores_especiais_academia_medicacao_e_descanso() {
        const auto l = lista({def("ac", "Academia", kDiario, {"18:00"}), def("md", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"08:10", "20:00"}),
                              def("ds", "Descanso", kDiario, {"15:00"}), def("sp", "Suplemento", kDiario, {"07:00"}),
                              def("al", "Almo\xC3\xA7o", kDiario, {"12:00"}), def("bn", "Banho", kDiario, {"21:00"}),
                              def("dt", "Dentes", kDiario, {"22:00"})});
        Estado e = estadoCom(l);
        auto r = registrarOcorrencia(e, achar(e, "ac"), 18 * 60 + 60);
        NEAR(15.0, r.cr);
        NEAR(40.0, r.xp);
        r = registrarOcorrencia(e, achar(e, "md", 0), 8 * 60 + 10 + 30);
        NEAR(3.0, r.cr);
        NEAR(10.0, r.xp);
        r = registrarOcorrencia(e, achar(e, "md", 1), 20 * 60 + 31);   // atrasada: -1/2 coracao
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Atrasado, (int)r.status);
        NEAR(0.0, r.cr);
        NEAR(5.0, r.xp);
        TEST_ASSERT_EQUAL_INT(-1, r.coracoesMeios);
        TEST_ASSERT_EQUAL_INT(9, e.coracoesMeios);
        // descanso: so atrasa para depois (+5 min)
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)classificarOcorrencia(TipoHabito::Descanso, 900, 905));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Atrasado, (int)classificarOcorrencia(TipoHabito::Descanso, 900, 906));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)classificarOcorrencia(TipoHabito::Descanso, 900, 800));
        // janelas da tabela
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)classificarOcorrencia(TipoHabito::Suplemento, 420, 450));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Atrasado, (int)classificarOcorrencia(TipoHabito::Suplemento, 420, 451));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)classificarOcorrencia(TipoHabito::Almoco, 720, 765));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Atrasado, (int)classificarOcorrencia(TipoHabito::Almoco, 720, 766));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)classificarOcorrencia(TipoHabito::Banho, 1260, 1200));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)classificarOcorrencia(TipoHabito::Dentes, 1320, 1290));
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Atrasado, (int)classificarOcorrencia(TipoHabito::Dentes, 1320, 1289));
        // medicacao esquecida (feita tarde demais) = -1 coracao
        Estado f = estadoCom(l);
        r = registrarOcorrencia(f, achar(f, "md", 0), 8 * 60 + 10 + 30 + 121);
        TEST_ASSERT_EQUAL_INT(-2, r.coracoesMeios);
        TEST_ASSERT_EQUAL_INT(8, f.coracoesMeios);
    }

    void test_qualquer_hora_nunca_atrasa() {
        const auto l = lista({def("g", "Livre", kDiario)});
        Estado e = estadoCom(l, 23 * 60 + 50);
        const auto r = registrarOcorrencia(e, 0, 23 * 60 + 50);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)r.status);
        NEAR(2.0, r.cr);
        Estado f = estadoCom(l);
        TEST_ASSERT_EQUAL_INT((int)FaseOcorrencia::Agora, (int)faseDaOcorrencia(f.dia.ocorrencias[0], 23 * 60 + 50));
    }

    void test_teto_28_cr_por_dia() {
        // 2 academias (15 + 15) + gerais: o teto corta em 28 cr; XP nao tem teto
        const auto l = lista({def("a1", "Academia", kDiario, {"07:00"}), def("a2", "Treino", kDiario, {"07:00"}),
                              def("g", "Geral", kDiario, {"07:00"})});
        Estado e = estadoCom(l);
        auto r1 = registrarOcorrencia(e, achar(e, "a1"), 7 * 60);
        auto r2 = registrarOcorrencia(e, achar(e, "a2"), 7 * 60);
        auto r3 = registrarOcorrencia(e, achar(e, "g"), 7 * 60);
        NEAR(15.0, r1.cr);
        NEAR(13.0, r2.cr);   // 15 -> so 13 cabem
        TEST_ASSERT_TRUE(r2.tetoAplicado);
        NEAR(0.0, r3.cr);
        TEST_ASSERT_TRUE(r3.tetoAplicado);
        NEAR(28.0, e.saldo);
        NEAR(28.0, e.dia.habitosCr);
        NEAR(40.0 + 40.0 + 5.0, e.dia.xpGanho);
    }

    // ---------------------------------------------------------------- dia valido, coracoes
    void foco50(Estado& e) { e.dia.fochoValidoMin = 50; }

    void test_dia_valido_por_fracao_4_de_7() {
        std::vector<DefHabito> hs;
        for (int i = 0; i < 7; ++i) hs.push_back(def(("h" + std::to_string(i)).c_str(), "H", kDiario, {"10:00"}));
        // 4 de 7 no horario (57,1 %) = dia valido
        Estado e = estadoCom(lista(hs));
        for (int i = 0; i < 4; ++i) registrarOcorrencia(e, (size_t)i, 600);
        foco50(e);
        ResumoDia r = fecharDia(e, false);
        TEST_ASSERT_EQUAL_INT(4, r.habitosNoHorario);
        TEST_ASSERT_TRUE(r.valido);
        // 3 de 7 (42,9 %) = invalido
        Estado f = estadoCom(lista(hs));
        for (int i = 0; i < 3; ++i) registrarOcorrencia(f, (size_t)i, 600);
        foco50(f);
        TEST_ASSERT_FALSE(fecharDia(f, false).valido);
        // com 3 habitos no total, 2 de 3 (66,7 %) basta; 1 de 3 nao
        const auto tres = lista({def("a", "A", kDiario, {"10:00"}), def("b", "B", kDiario, {"10:00"}), def("c", "C", kDiario, {"10:00"})});
        Estado g = estadoCom(tres);
        registrarOcorrencia(g, 0, 600);
        registrarOcorrencia(g, 1, 600);
        foco50(g);
        TEST_ASSERT_TRUE(fecharDia(g, false).valido);
        Estado h = estadoCom(tres);
        registrarOcorrencia(h, 0, 600);
        foco50(h);
        TEST_ASSERT_FALSE(fecharDia(h, false).valido);
    }

    void test_coracao_por_80_por_cento_das_ocorrencias() {
        std::vector<DefHabito> hs;
        for (int i = 0; i < 5; ++i) hs.push_back(def(("h" + std::to_string(i)).c_str(), "H", kDiario, {"10:00"}));
        // 4 de 5 = 80 %: +1/2 coracao (habitos) +1/2 (dia limpo) = +1 coracao = 2 meios, mas o teto de ganho e +1/dia
        Estado e = estadoCom(lista(hs));
        e.coracoesMeios = 6;
        for (int i = 0; i < 4; ++i) registrarOcorrencia(e, (size_t)i, 600);
        fecharDia(e, false);
        TEST_ASSERT_EQUAL_INT(8, e.coracoesMeios);
        // 3 de 5 = 60 %: so o dia limpo (+1 meio)
        Estado f = estadoCom(lista(hs));
        f.coracoesMeios = 6;
        for (int i = 0; i < 3; ++i) registrarOcorrencia(f, (size_t)i, 600);
        fecharDia(f, false);
        TEST_ASSERT_EQUAL_INT(7, f.coracoesMeios);
    }

    void test_sem_habitos_o_criterio_e_ignorado() {
        Estado e;
        abrirDia(e, kTerca);
        foco50(e);
        const ResumoDia r = fecharDia(e, false);
        TEST_ASSERT_TRUE(r.valido);                 // foco >= 50 min basta
        TEST_ASSERT_EQUAL_INT(0, r.habitosNoHorario);
        TEST_ASSERT_EQUAL_INT(10, e.coracoesMeios);   // so o dia limpo (+1 meio, ja no teto): nada de habitos
        Estado f;
        abrirDia(f, kTerca);
        fecharDia(f, false);
        TEST_ASSERT_EQUAL_INT(10, f.coracoesMeios);   // nem medicacao esquecida nem bonus
    }

    void test_pendentes_viram_esquecidas_e_medicacao_custa_coracao() {
        const auto l = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"08:10", "20:00"}), def("g", "Geral", kDiario, {"09:00"})});
        Estado e = estadoCom(l);
        registrarOcorrencia(e, achar(e, "g"), 9 * 60);
        fecharDia(e, false);
        for (const auto& oc : e.dia.ocorrencias)
            if (oc.habitoId == "md") TEST_ASSERT_EQUAL_INT((int)StatusHabito::Esquecido, (int)oc.status);
        // 2 doses esquecidas = -2 x 1 coracao (-4 meios); dia limpo devolve +1 meio => 10 - 4 + 1
        TEST_ASSERT_EQUAL_INT(7, e.coracoesMeios);
    }

    // ---------------------------------------------------------------- fila
    void test_fila_dedupe_substitui_por_maior_valor_e_limita() {
        std::vector<ItemFila> fila;
        ItemFila a;
        a.id = "h1";
        a.stamp = 20261110;
        a.valor = 1;
        a.meta = 2;
        a.time = "2026-11-10T11:10:00+0000";
        enfileirar(fila, a);
        enfileirar(fila, a);   // igual: nao duplica
        TEST_ASSERT_EQUAL_INT(1, (int)fila.size());
        fila[0].tentativas = 3;
        a.valor = 2;
        enfileirar(fila, a);   // maior: substitui e zera erros
        TEST_ASSERT_EQUAL_INT(1, (int)fila.size());
        NEAR(2.0, fila[0].valor);
        TEST_ASSERT_EQUAL_INT(0, fila[0].tentativas);
        a.valor = 1;
        enfileirar(fila, a);   // menor: ignora
        NEAR(2.0, fila[0].valor);
        a.stamp = 20261111;    // outro dia = outro item
        enfileirar(fila, a);
        TEST_ASSERT_EQUAL_INT(2, (int)fila.size());
        for (int i = 0; i < 100; ++i) {
            ItemFila x = a;
            x.id = "x" + std::to_string(i);
            enfileirar(fila, x);
        }
        TEST_ASSERT_EQUAL_INT((int)kMaxFila, (int)fila.size());
        TEST_ASSERT_EQUAL_STRING("x99", fila.back().id.c_str());
    }

    void test_fila_envio_sucesso_erro_e_rede() {
        std::vector<ItemFila> fila;
        ItemFila a;
        a.id = "h1";
        a.stamp = 20261110;
        a.valor = 1;
        a.meta = 1;
        enfileirar(fila, a);
        aplicarEnvio(fila, a, 0);   // sem rede: fica, sem contar erro
        TEST_ASSERT_EQUAL_INT(1, (int)fila.size());
        TEST_ASSERT_EQUAL_INT(0, fila[0].tentativas);
        aplicarEnvio(fila, a, 500);   // erro HTTP: conta
        aplicarEnvio(fila, a, 404);
        TEST_ASSERT_EQUAL_INT(2, fila[0].tentativas);
        TEST_ASSERT_EQUAL_INT(404, fila[0].ultimoHttp);
        // entrou valor maior durante o envio: o 200 do valor antigo nao remove
        ItemFila b = a;
        b.valor = 2;
        enfileirar(fila, b);
        aplicarEnvio(fila, a, 200);
        TEST_ASSERT_EQUAL_INT(1, (int)fila.size());
        aplicarEnvio(fila, b, 201);
        TEST_ASSERT_EQUAL_INT(0, (int)fila.size());
        // limite de tentativas
        ItemFila c = a;
        c.tentativas = kMaxTentativasFila;
        TEST_ASSERT_FALSE(devePreEnviar(c));
        TEST_ASSERT_TRUE(devePreEnviar(a));
    }

    void test_fila_serializa_e_ignora_linhas_ruins() {
        std::vector<ItemFila> fila;
        ItemFila a;
        a.id = "abc123";
        a.stamp = 20261110;
        a.valor = 2;
        a.meta = 3;
        a.time = "2026-11-10T14:10:00+0000";
        a.tentativas = 2;
        a.ultimoHttp = 500;
        fila.push_back(a);
        std::string texto = serializarFila(fila) + "lixo\n{\"id\":\"\",\"stamp\":1}\n\n";
        const auto lida = lerFila(texto);
        TEST_ASSERT_EQUAL_INT(1, (int)lida.size());
        TEST_ASSERT_EQUAL_STRING("abc123", lida[0].id.c_str());
        TEST_ASSERT_EQUAL_INT(20261110, lida[0].stamp);
        NEAR(2.0, lida[0].valor);
        NEAR(3.0, lida[0].meta);
        TEST_ASSERT_EQUAL_INT(2, lida[0].tentativas);
        TEST_ASSERT_EQUAL_INT(500, lida[0].ultimoHttp);
        TEST_ASSERT_EQUAL_INT(0, (int)lerFila("").size());
    }

    void test_corpo_e_caminho_do_post_de_checkin() {
        ItemFila a;
        a.id = "6789ad00b9a2d1030a5920c8";
        a.stamp = 20261110;
        a.valor = 1;
        a.meta = 2;
        a.time = "2026-11-10T11:10:00+0000";
        TEST_ASSERT_EQUAL_STRING("{\"stamp\":20261110,\"time\":\"2026-11-10T11:10:00+0000\",\"value\":1,\"goal\":2}",
                                 ticktick::corpoCheckin(a).c_str());
        TEST_ASSERT_EQUAL_STRING("/habit/6789ad00b9a2d1030a5920c8/checkin", ticktick::caminhoPostCheckin(a).c_str());
    }

    void test_marcar_feita_enfileira_valor_e_meta_do_dia() {
        const auto l = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"08:10", "20:00"})});
        Estado e = estadoCom(l, 8 * 60 + 15);
        std::vector<ItemFila> fila;
        auto r = marcarFeita(e, 0, fila);
        TEST_ASSERT_TRUE(r.registrado);
        TEST_ASSERT_EQUAL_INT(1, (int)fila.size());
        NEAR(1.0, fila[0].valor);
        NEAR(2.0, fila[0].meta);   // goal = ocorrencias do dia
        TEST_ASSERT_EQUAL_INT(20261110, fila[0].stamp);
        TEST_ASSERT_EQUAL_STRING("2026-11-10T11:15:00+0000", fila[0].time.c_str());   // 08:15 em UTC-3
        TEST_ASSERT_FALSE(marcarFeita(e, 0, fila).registrado);   // de novo: nada
        avancarRelogio(e, 20 * 60);
        marcarFeita(e, 1, fila);
        TEST_ASSERT_EQUAL_INT(1, (int)fila.size());   // mesmo habito e dia: 1 item
        NEAR(2.0, fila[0].valor);
        TEST_ASSERT_EQUAL_STRING("2026-11-10T23:00:00+0000", fila[0].time.c_str());
    }

    // ---------------------------------------------------------------- meditacao pelo timer
    void test_meditacao_pelo_timer_marca_proxima_no_horario() {
        const auto l = lista({def("med", "\xF0\x9F\xA7\x98", kDiario, {"07:00", "19:00"}),
                              def("g", "Geral", kDiario, {"06:00"})});
        Estado e = estadoCom(l, 23 * 60);   // hora real longe dos dois horarios
        std::vector<ItemFila> fila;
        auto r = marcarMeditacaoPorTimer(e, fila);
        TEST_ASSERT_TRUE(r.registrado);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)r.status);   // independente da hora
        NEAR(2.0, r.cr);
        NEAR(5.0, r.xp);
        TEST_ASSERT_EQUAL_INT(7 * 60, e.dia.ocorrencias[r.indice].minuto);    // a primeira pendente
        TEST_ASSERT_EQUAL_INT(1, (int)fila.size());
        NEAR(1.0, fila[0].valor);
        NEAR(2.0, fila[0].meta);
        r = marcarMeditacaoPorTimer(e, fila);   // 2a sessao: a proxima
        TEST_ASSERT_TRUE(r.registrado);
        TEST_ASSERT_EQUAL_INT(19 * 60, e.dia.ocorrencias[r.indice].minuto);
        NEAR(2.0, fila[0].valor);
        TEST_ASSERT_FALSE(marcarMeditacaoPorTimer(e, fila).registrado);   // 3a: nao ha pendente
        TEST_ASSERT_EQUAL_INT(1, (int)fila.size());
        // outros habitos nao sao tocados
        TEST_ASSERT_FALSE(e.dia.ocorrencias[achar(e, "g")].feita);
        // sem habito de meditacao: nada acontece
        Estado f = estadoCom(lista({def("g", "Geral", kDiario, {"06:00"})}));
        std::vector<ItemFila> fila2;
        TEST_ASSERT_FALSE(marcarMeditacaoPorTimer(f, fila2).registrado);
        TEST_ASSERT_EQUAL_INT(0, (int)fila2.size());
    }

    // ---------------------------------------------------------------- check-ins remotos
    void test_interpretar_tempo_e_tempo_checkin() {
        auto m = interpretarTempo("2026-10-05T18:22:40.000+0000");
        TEST_ASSERT_TRUE(m.has_value());
        TEST_ASSERT_EQUAL_INT(5, m->data.dia);
        TEST_ASSERT_EQUAL_INT(15 * 60 + 22, m->minuto);   // UTC-3
        m = interpretarTempo("2026-11-11T01:30:00Z");   // vira o dia anterior no fuso local
        TEST_ASSERT_EQUAL_INT(10, m->data.dia);
        TEST_ASSERT_EQUAL_INT(22 * 60 + 30, m->minuto);
        m = interpretarTempo("2026-11-10T08:00:00-03:00");   // ja em hora local
        TEST_ASSERT_EQUAL_INT(8 * 60, m->minuto);
        TEST_ASSERT_FALSE(interpretarTempo("ontem").has_value());
        TEST_ASSERT_FALSE(interpretarTempo("").has_value());
        TEST_ASSERT_EQUAL_STRING("2026-11-10T11:10:00+0000", tempoCheckinUtc(kTerca, 8 * 60 + 10).c_str());
        TEST_ASSERT_EQUAL_STRING("2026-11-11T02:30:00+0000", tempoCheckinUtc(kTerca, 23 * 60 + 30).c_str());
        // ida e volta
        m = interpretarTempo(tempoCheckinUtc(kTerca, 23 * 60 + 30));
        TEST_ASSERT_EQUAL_INT(10, m->data.dia);
        TEST_ASSERT_EQUAL_INT(23 * 60 + 30, m->minuto);
    }

    CheckinRemoto remoto(const char* id, double valor, const char* time, int stamp = 20261110) {
        CheckinRemoto c;
        c.habitId = id;
        c.stamp = stamp;
        c.value = valor;
        c.goal = valor;
        c.time = time;
        return c;
    }

    void test_checkin_remoto_marca_pela_hora_sem_gerar_fila() {
        const auto l = lista({def("g", "Geral", kDiario, {"09:00"}), def("md", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"08:10", "20:00"})});
        Estado e = estadoCom(l, 21 * 60);
        // geral feito 09:30 local (12:30Z): no horario. Medicacao: 1 check-in as 20:10 (23:10Z) -> a ocorrencia das 20:00
        auto rs = aplicarCheckinsRemotos(e, {remoto("g", 1, "2026-11-10T12:30:00.000+0000"),
                                            remoto("md", 1, "2026-11-10T23:10:00.000+0000"),
                                            remoto("md", 1, "2026-11-09T23:10:00.000+0000", 20261109)});   // ontem: ignorado
        TEST_ASSERT_EQUAL_INT(2, (int)rs.size());
        const auto& g = e.dia.ocorrencias[achar(e, "g")];
        TEST_ASSERT_TRUE(g.feita);
        TEST_ASSERT_TRUE(g.remota);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)g.status);
        const auto& m1 = e.dia.ocorrencias[achar(e, "md", 1)];
        TEST_ASSERT_TRUE(m1.feita);
        TEST_ASSERT_EQUAL_INT(20 * 60 + 10, m1.minutoReal);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)m1.status);
        TEST_ASSERT_FALSE(e.dia.ocorrencias[achar(e, "md", 0)].feita);
        NEAR(2.0 + 3.0, e.saldo);
        // idempotente: aplicar de novo nao muda nada (e nao ha fila nesse caminho)
        TEST_ASSERT_EQUAL_INT(0, (int)aplicarCheckinsRemotos(e, {remoto("g", 1, "2026-11-10T12:30:00.000+0000"),
                                                                 remoto("md", 1, "2026-11-10T23:10:00.000+0000")}).size());
        NEAR(5.0, e.saldo);
    }

    void test_checkin_remoto_tarde_vale_atrasado_e_valor_maior_marca_varias() {
        const auto l = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"08:10", "20:00"}), def("g", "Geral", kDiario, {"07:00"})});
        Estado e = estadoCom(l, 23 * 60);
        // check-in do geral as 09:30 locais (07:00 + 150 min = atrasado)
        aplicarCheckinsRemotos(e, {remoto("g", 1, "2026-11-10T12:30:00.000+0000")});
        const auto& g = e.dia.ocorrencias[achar(e, "g")];
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::Atrasado, (int)g.status);
        // medicacao value 2 de 2, sem hora util: as duas viram feitas no horario planejado
        Estado f = estadoCom(l, 23 * 60);
        const auto rs = aplicarCheckinsRemotos(f, {remoto("md", 2, "")});
        TEST_ASSERT_EQUAL_INT(2, (int)rs.size());
        TEST_ASSERT_TRUE(f.dia.ocorrencias[achar(f, "md", 0)].feita);
        TEST_ASSERT_TRUE(f.dia.ocorrencias[achar(f, "md", 1)].feita);
        // value maior que o total e limitado ao total; 0 e ignorado; habito desconhecido e ignorado
        Estado h = estadoCom(l);
        TEST_ASSERT_EQUAL_INT(2, (int)aplicarCheckinsRemotos(h, {remoto("md", 9, ""), remoto("g", 0, ""), remoto("x", 1, "")}).size());
    }

    void test_visao_agora_proximo_lembrete_e_fases() {
        const auto l = lista({def("a", "A", kDiario, {"08:00"}), def("b", "B", kDiario, {"12:00"}),
                              def("c", "C", kDiario, {"20:00"}), def("d", "D", kDiario)});
        Estado e = estadoCom(l);
        // 07:00: 08:00 esta na janela (+-60 => 07:00..09:00) -> AGORA
        auto p = ocorrenciaAgora(e, 7 * 60);
        TEST_ASSERT_TRUE(p.has_value());
        TEST_ASSERT_EQUAL_STRING("a", e.dia.ocorrencias[p->indice].habitoId.c_str());
        TEST_ASSERT_EQUAL_INT((int)FaseOcorrencia::Agora, (int)p->fase);
        // 10:30: a esta atrasada (09:00 < 10:30 <= 11:00) e b (12:00) ainda e futura (antes de 11:00) -> atrasada primeiro
        p = ocorrenciaAgora(e, 10 * 60 + 30);
        TEST_ASSERT_EQUAL_STRING("a", e.dia.ocorrencias[p->indice].habitoId.c_str());
        TEST_ASSERT_EQUAL_INT((int)FaseOcorrencia::Atrasada, (int)p->fase);
        // 12:30: a perdida (>11:00), b na janela
        p = ocorrenciaAgora(e, 12 * 60 + 30);
        TEST_ASSERT_EQUAL_STRING("b", e.dia.ocorrencias[p->indice].habitoId.c_str());
        // 16:00: a e b perdidas (> janela + 2 h); c futura -> proxima
        p = ocorrenciaAgora(e, 16 * 60);
        TEST_ASSERT_EQUAL_STRING("c", e.dia.ocorrencias[p->indice].habitoId.c_str());
        TEST_ASSERT_EQUAL_INT((int)FaseOcorrencia::Futura, (int)p->fase);
        // tudo com horario feito: sobra o "qualquer hora"
        std::vector<ItemFila> fila;
        for (const char* id : {"a", "b", "c"}) marcarFeita(e, achar(e, id), fila);
        p = ocorrenciaAgora(e, 16 * 60);
        TEST_ASSERT_EQUAL_STRING("d", e.dia.ocorrencias[p->indice].habitoId.c_str());
        marcarFeita(e, achar(e, "d"), fila);
        TEST_ASSERT_FALSE(ocorrenciaAgora(e, 16 * 60).has_value());

        // proximo lembrete pendente
        Estado f = estadoCom(l);
        TEST_ASSERT_EQUAL_INT(8 * 60, *proximoLembrete(f, 6 * 60));
        TEST_ASSERT_EQUAL_INT(8 * 60, *proximoLembrete(f, 8 * 60));
        TEST_ASSERT_EQUAL_INT(12 * 60, *proximoLembrete(f, 8 * 60 + 1));
        TEST_ASSERT_FALSE(proximoLembrete(f, 21 * 60).has_value());
        marcarFeita(f, achar(f, "b"), fila);
        TEST_ASSERT_EQUAL_INT(20 * 60, *proximoLembrete(f, 8 * 60 + 1));

        // lista com fase e flag de 2 toques
        const auto med = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"08:10"})});
        Estado m = estadoCom(med);
        const auto v = ocorrenciasDoDia(m, 8 * 60);
        TEST_ASSERT_EQUAL_INT(1, (int)v.size());
        TEST_ASSERT_TRUE(v[0].confirmar2Toques);
        NEAR(3.0, v[0].cr);
        TEST_ASSERT_EQUAL_INT((int)FaseOcorrencia::Agora, (int)v[0].fase);
    }

    // ---------------------------------------------------------------- persistencia / compat
    void test_estado_antigo_sem_ocorrencias_continua_legivel() {
        // estado.json de antes dos habitos: Dia sem "ocorrencias" nem "habitosCr"
        const char* antigo = R"({"saldo":12.5,"coracoesMeios":9,"dia":{"data":{"ano":2026,"mes":11,"dia":9},"aberto":true,
            "habitos":[0,0,0,0,0,0,0],"crGanho":3.0}})";
        Estado e;
        TEST_ASSERT_TRUE(lerEstado(antigo, e));
        NEAR(12.5, e.saldo);
        TEST_ASSERT_EQUAL_INT(9, e.coracoesMeios);
        TEST_ASSERT_TRUE(e.dia.ocorrencias.empty());
        NEAR(0.0, e.dia.habitosCr);
        // e continua funcionando: reconciliar cria as ocorrencias do dia
        reconciliarDia(e, lista({def("g", "Geral", kDiario, {"10:00"})}));
        TEST_ASSERT_EQUAL_INT(1, (int)e.dia.ocorrencias.size());
    }

    void test_estado_com_ocorrencias_ida_e_volta() {
        const auto l = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"08:10", "20:00"})});
        Estado e = estadoCom(l, 8 * 60 + 10);
        std::vector<ItemFila> fila;
        marcarFeita(e, 0, fila);
        const std::string json = serializarEstado(e);
        TEST_ASSERT_TRUE(json.size() > 0);
        Estado lido;
        TEST_ASSERT_TRUE(lerEstado(json, lido));
        TEST_ASSERT_EQUAL_INT(2, (int)lido.dia.ocorrencias.size());
        const auto& oc = lido.dia.ocorrencias[0];
        TEST_ASSERT_EQUAL_STRING("md", oc.habitoId.c_str());
        TEST_ASSERT_EQUAL_STRING("Medica\xC3\xA7\xC3\xA3o", oc.nome.c_str());
        TEST_ASSERT_EQUAL_INT((int)TipoHabito::Medicacao, (int)oc.tipo);
        TEST_ASSERT_TRUE(oc.feita);
        TEST_ASSERT_EQUAL_INT((int)StatusHabito::NoHorario, (int)oc.status);
        TEST_ASSERT_EQUAL_INT(8 * 60 + 10, oc.minutoReal);
        TEST_ASSERT_FALSE(lido.dia.ocorrencias[1].feita);
        NEAR(3.0, lido.dia.habitosCr);
    }

    void test_evento_de_log_do_habito_ida_e_volta() {
        const auto l = lista({def("md", "Medica\xC3\xA7\xC3\xA3o", kDiario, {"08:10"})});
        Estado e = estadoCom(l, 8 * 60 + 12);
        std::vector<ItemFila> fila;
        const auto r = marcarFeita(e, 0, fila);
        const EventoLog ev = eventoHabito(e, r, "timer");
        EventoLog lido;
        TEST_ASSERT_TRUE(lerLinhaLog(linhaLog(ev), lido));
        TEST_ASSERT_EQUAL_STRING("habito", lido.tipo.c_str());
        TEST_ASSERT_EQUAL_STRING("2026-11-10", lido.data.c_str());
        TEST_ASSERT_EQUAL_STRING("08:12:00", lido.hora.c_str());
        TEST_ASSERT_EQUAL_STRING("no_horario", lido.resultado.c_str());
        TEST_ASSERT_EQUAL_STRING("md", lido.projetoId.c_str());
        TEST_ASSERT_EQUAL_STRING("timer", lido.origem.c_str());
        NEAR(3.0, lido.cr);
        NEAR(10.0, lido.xp);
    }

    // ---------------------------------------------------------------- TickTick (API)
    // Formato real (so os campos usados) de GET /open/v1/habit: arquivado de verdade, sentinela 2001, ativo, status != 0.
    const char* kHabitosApi = R"([
      {"id":"h1","name":"📖","repeatRule":"RRULE:FREQ=WEEKLY;INTERVAL=1;BYDAY=SU,MO,TU,WE,TH,FR,SA",
       "reminders":[],"goal":1.0,"type":"Boolean","status":0,"archivedTime":"2024-02-13T17:21:23.000+0000","targetStartDate":20221110},
      {"id":"h2","name":"Dia Produtivo","repeatRule":"RRULE:FREQ=DAILY;INTERVAL=1","reminders":[],"goal":1.0,"status":0,
       "archivedTime":"2001-01-01T00:00:00.000+0000","targetStartDate":20250116},
      {"id":"h3","name":"sem dopamina","repeatRule":"RRULE:FREQ=WEEKLY;BYDAY=MO,TU,WE,TH,FR","reminders":["06:30"],"goal":1.0,
       "status":0,"archivedTime":null,"targetStartDate":20260929,"exDates":null},
      {"id":"h4","name":"Faltam campos"},
      {"id":"h5","name":"Pausado","status":1,"repeatRule":"RRULE:FREQ=DAILY"}
    ])";

    void test_montar_habitos_so_ativos() {
        const auto json = ticktick::montarHabitos(kHabitosApi, "2026-11-10T12:00:00Z");
        TEST_ASSERT_TRUE(json.has_value());
        ListaHabitos l;
        TEST_ASSERT_TRUE(lerHabitos(*json, l));
        TEST_ASSERT_EQUAL_INT(3, (int)l.habitos.size());   // h2 (sentinela), h3, h4; h1 arquivado e h5 status 1 ficam fora
        TEST_ASSERT_EQUAL_STRING("h2", l.habitos[0].id.c_str());
        TEST_ASSERT_EQUAL_STRING("h3", l.habitos[1].id.c_str());
        TEST_ASSERT_EQUAL_STRING("06:30", l.habitos[1].reminders[0].c_str());
        TEST_ASSERT_EQUAL_INT(20260929, l.habitos[1].targetStartDate);
        TEST_ASSERT_EQUAL_STRING("h4", l.habitos[2].id.c_str());
        NEAR(1.0, l.habitos[2].goal);
        TEST_ASSERT_EQUAL_STRING("", l.habitos[2].repeatRule.c_str());
        TEST_ASSERT_EQUAL_STRING("ticktick", l.fonte.c_str());
        TEST_ASSERT_FALSE(ticktick::montarHabitos("{\"errorCode\":\"x\"}", "").has_value());
        // caminho do GET de check-ins
        TEST_ASSERT_EQUAL_STRING("/habit/checkins?habitIds=h2,h3,h4&from=20261110&to=20261110",
                                 ticktick::caminhoCheckins(l, 20261110).c_str());
        TEST_ASSERT_EQUAL_STRING("", ticktick::caminhoCheckins(ListaHabitos{}, 20261110).c_str());
    }

    void test_montar_checkins_do_dia() {
        // resposta real: lista de agregados por habito/ano, cada um com os check-ins do periodo
        const char* resposta = R"([
          {"id":"a","habitId":"h3","year":0,"checkins":[
            {"id":"c1","stamp":20261110,"time":"2026-11-10T12:52:04.000+0000","opTime":"x","value":1.0,"goal":1.0,"status":2},
            {"id":"c2","stamp":20261109,"time":"2026-11-09T12:52:04.000+0000","value":1.0,"goal":1.0,"status":2}]},
          {"id":"b","habitId":"h4","year":0,"checkins":[{"stamp":20261110,"value":2.0,"goal":3.0}]},
          {"id":"c","habitId":"h9","checkins":null}
        ])";
        const auto json = ticktick::montarCheckins(resposta, 20261110, "2026-11-10T12:00:00Z");
        TEST_ASSERT_TRUE(json.has_value());
        ArquivoCheckins a;
        TEST_ASSERT_TRUE(lerCheckins(*json, a));
        TEST_ASSERT_EQUAL_INT(2, (int)a.checkins.size());
        TEST_ASSERT_EQUAL_STRING("h3", a.checkins[0].habitId.c_str());
        TEST_ASSERT_EQUAL_STRING("2026-11-10T12:52:04.000+0000", a.checkins[0].time.c_str());
        NEAR(2.0, a.checkins[1].value);
        NEAR(3.0, a.checkins[1].goal);
        TEST_ASSERT_TRUE(ticktick::montarCheckins("[]", 20261110, "").has_value());
        TEST_ASSERT_FALSE(ticktick::montarCheckins("nao e json", 20261110, "").has_value());
    }

    void test_lista_de_habitos_ida_e_volta_e_invalido() {
        const auto l = lista({def("a", "A \xF0\x9F\x99\x82", "RRULE:FREQ=DAILY", {"08:00", "20:00"}, 2.0, 20260101)});
        std::string json;
        // serializa via montar -> le: usa o caminho oficial (API -> json -> lerHabitos)
        const auto m = ticktick::montarHabitos(
            R"([{"id":"a","name":"A 🙂","repeatRule":"RRULE:FREQ=DAILY","reminders":["08:00","20:00"],"goal":2.0,"status":0,"targetStartDate":20260101}])", "g");
        TEST_ASSERT_TRUE(m.has_value());
        ListaHabitos lida;
        TEST_ASSERT_TRUE(lerHabitos(*m, lida));
        TEST_ASSERT_EQUAL_INT(1, (int)lida.habitos.size());
        TEST_ASSERT_EQUAL_STRING("A \xF0\x9F\x99\x82", lida.habitos[0].name.c_str());
        TEST_ASSERT_EQUAL_INT(2, (int)lida.habitos[0].reminders.size());
        TEST_ASSERT_EQUAL_INT(2, (int)gerarOcorrencias(lida, kTerca).size());
        TEST_ASSERT_EQUAL_INT(2, (int)gerarOcorrencias(l, kTerca).size());
        TEST_ASSERT_FALSE(lerHabitos("", lida));
        TEST_ASSERT_FALSE(lerHabitos("[1,2", lida));
    }

    void test_montar_habitos_preserva_icone_e_cor() {
        const auto m = ticktick::montarHabitos(
            R"([{"id":"a","name":"Ler","reminders":["09:00"],"status":0,"iconRes":"habit_reading","color":"#A0EFED"},{"id":"b","name":"Cafe","status":0,"iconRes":"txt_\u2615","color":""},{"id":"c","name":"Sem","status":0}])", "g");
        TEST_ASSERT_TRUE(m.has_value());
        ListaHabitos l;
        TEST_ASSERT_TRUE(lerHabitos(*m, l));
        TEST_ASSERT_EQUAL_STRING("habit_reading", l.habitos[0].iconRes.c_str());
        TEST_ASSERT_EQUAL_STRING("#A0EFED", l.habitos[0].color.c_str());
        TEST_ASSERT_EQUAL_STRING("txt_\xE2\x98\x95", l.habitos[1].iconRes.c_str());
        TEST_ASSERT_EQUAL_STRING("", l.habitos[1].color.c_str());
        TEST_ASSERT_EQUAL_STRING("", l.habitos[2].iconRes.c_str());
    }

} // namespace

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_rrule_diaria_semanal_e_intervalos);
    RUN_TEST(test_rrule_vazia_until_inicio_e_nao_suportada);
    RUN_TEST(test_nome_para_tela_remove_emoji_e_usa_fallback);
    RUN_TEST(test_detectar_tipo_por_nome_e_emoji);
    RUN_TEST(test_habito_arquivado_e_sentinela_2001);
    RUN_TEST(test_ocorrencias_um_por_horario_e_completa_com_qualquer_hora);
    RUN_TEST(test_ocorrencias_respeitam_repeticao_e_ignoram_nao_suportado);
    RUN_TEST(test_reconciliar_preserva_feitas_e_remove_pendentes_que_sumiram);
    RUN_TEST(test_valor_geral_e_janela_60);
    RUN_TEST(test_valores_especiais_academia_medicacao_e_descanso);
    RUN_TEST(test_qualquer_hora_nunca_atrasa);
    RUN_TEST(test_teto_28_cr_por_dia);
    RUN_TEST(test_dia_valido_por_fracao_4_de_7);
    RUN_TEST(test_coracao_por_80_por_cento_das_ocorrencias);
    RUN_TEST(test_sem_habitos_o_criterio_e_ignorado);
    RUN_TEST(test_pendentes_viram_esquecidas_e_medicacao_custa_coracao);
    RUN_TEST(test_fila_dedupe_substitui_por_maior_valor_e_limita);
    RUN_TEST(test_fila_envio_sucesso_erro_e_rede);
    RUN_TEST(test_fila_serializa_e_ignora_linhas_ruins);
    RUN_TEST(test_corpo_e_caminho_do_post_de_checkin);
    RUN_TEST(test_marcar_feita_enfileira_valor_e_meta_do_dia);
    RUN_TEST(test_meditacao_pelo_timer_marca_proxima_no_horario);
    RUN_TEST(test_interpretar_tempo_e_tempo_checkin);
    RUN_TEST(test_checkin_remoto_marca_pela_hora_sem_gerar_fila);
    RUN_TEST(test_checkin_remoto_tarde_vale_atrasado_e_valor_maior_marca_varias);
    RUN_TEST(test_visao_agora_proximo_lembrete_e_fases);
    RUN_TEST(test_estado_antigo_sem_ocorrencias_continua_legivel);
    RUN_TEST(test_estado_com_ocorrencias_ida_e_volta);
    RUN_TEST(test_evento_de_log_do_habito_ida_e_volta);
    RUN_TEST(test_montar_habitos_so_ativos);
    RUN_TEST(test_montar_checkins_do_dia);
    RUN_TEST(test_lista_de_habitos_ida_e_volta_e_invalido);
    RUN_TEST(test_montar_habitos_preserva_icone_e_cor);
    return UNITY_END();
}
