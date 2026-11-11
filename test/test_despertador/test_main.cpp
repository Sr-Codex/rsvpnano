#include <unity.h>

#include "tempo/Despertador.h"
#include "tempo/Habitos.h"

using namespace tempo;

void setUp() {}
void tearDown() {}

namespace {

    const Data kHoje = {2026, 11, 11};
    constexpr int64_t kEpoch = 1794000000;   // ancora arbitraria (UTC); so diferencas importam

    OcorrenciaDia oc(int minuto, TipoHabito tipo = TipoHabito::Geral, bool feita = false) {
        OcorrenciaDia o;
        o.habitoId = "h" + std::to_string(minuto);
        o.nome = "x";
        o.tipo = tipo;
        o.minuto = minuto;
        o.feita = feita;
        o.status = feita ? StatusHabito::NoHorario : StatusHabito::Pendente;
        return o;
    }

    Estado estadoCom(std::initializer_list<OcorrenciaDia> lista) {
        Estado e;
        e.dia.data = kHoje;
        e.dia.aberto = true;
        for (const auto& o : lista) e.dia.ocorrencias.push_back(o);
        return e;
    }

    // 15:00 locais, bateria 80 %, automatico
    EntradaDespertador base(const Estado* e) {
        EntradaDespertador in;
        in.motivo = MotivoDesligar::Automatico;
        in.relogioValido = true;
        in.epochAgora = kEpoch;
        in.hoje = kHoje;
        in.segundoDoDia = 15 * 3600;
        in.estado = e;
        in.bateriaPercent = 80;
        return in;
    }

} // namespace

void test_dorme_ate_proximo_lembrete_5s_antes() {
    Estado e = estadoCom({oc(18 * 60), oc(21 * 60)});
    const auto d = decidirDespertador(base(&e));
    TEST_ASSERT_TRUE(d.dormir);
    TEST_ASSERT_EQUAL_INT64(kEpoch + 3 * 3600, d.alvoEpoch);
    TEST_ASSERT_EQUAL_INT64(kEpoch + 3 * 3600 - 5, d.acordarEpoch);
    TEST_ASSERT_EQUAL(static_cast<int>(OrigemDespertar::Lembrete), static_cast<int>(d.origem));
}

void test_manual_sempre_desliga() {
    Estado e = estadoCom({oc(16 * 60)});
    auto in = base(&e);
    in.motivo = MotivoDesligar::Manual;
    const auto d = decidirDespertador(in);
    TEST_ASSERT_FALSE(d.dormir);
    TEST_ASSERT_EQUAL(static_cast<int>(PorqueDesliga::Manual), static_cast<int>(d.porque));
}

void test_relogio_invalido_desliga() {
    Estado e = estadoCom({oc(16 * 60)});
    auto in = base(&e);
    in.relogioValido = false;
    TEST_ASSERT_FALSE(decidirDespertador(in).dormir);
    in.relogioValido = true;
    in.epochAgora = 0;
    TEST_ASSERT_EQUAL(static_cast<int>(PorqueDesliga::RelogioInvalido), static_cast<int>(decidirDespertador(in).porque));
}

void test_sem_lembrete_pendente_desliga() {
    Estado vazio = estadoCom({});
    TEST_ASSERT_EQUAL(static_cast<int>(PorqueDesliga::SemLembrete), static_cast<int>(decidirDespertador(base(&vazio)).porque));
    // tudo feito, ou "qualquer hora" (sem horario), ou horario que ja passou: nada a agendar
    Estado e = estadoCom({oc(16 * 60, TipoHabito::Geral, true), oc(-1), oc(10 * 60)});
    TEST_ASSERT_FALSE(decidirDespertador(base(&e)).dormir);
    auto semEstado = base(nullptr);
    TEST_ASSERT_FALSE(decidirDespertador(semEstado).dormir);
}

void test_estado_de_outro_dia_e_ignorado() {
    Estado e = estadoCom({oc(16 * 60)});
    e.dia.data = {2026, 11, 10};
    TEST_ASSERT_FALSE(decidirDespertador(base(&e)).dormir);
}

void test_horizonte_14h() {
    Estado e = estadoCom({oc(23 * 60 + 30)});   // 8,5 h a frente
    TEST_ASSERT_TRUE(decidirDespertador(base(&e)).dormir);
    auto in = base(&e);
    in.segundoDoDia = 9 * 3600;                 // 14,5 h a frente
    const auto d = decidirDespertador(in);
    TEST_ASSERT_FALSE(d.dormir);
    TEST_ASSERT_EQUAL(static_cast<int>(PorqueDesliga::ForaDoHorizonte), static_cast<int>(d.porque));
    in.segundoDoDia = 9 * 3600 + 30 * 60;       // exatamente 14 h: ainda dorme
    TEST_ASSERT_TRUE(decidirDespertador(in).dormir);
}

void test_amanha_de_manha_quando_hoje_acabou() {
    Estado e = estadoCom({oc(9 * 60)});   // hoje ja passou
    auto in = base(&e);
    in.segundoDoDia = 22 * 3600;
    in.primeiroLembreteAmanhaMin = 7 * 60;   // 9 h a frente
    const auto d = decidirDespertador(in);
    TEST_ASSERT_TRUE(d.dormir);
    TEST_ASSERT_EQUAL_INT64(kEpoch + 9 * 3600, d.alvoEpoch);
    in.segundoDoDia = 15 * 3600;             // 16 h a frente: fora do horizonte
    TEST_ASSERT_FALSE(decidirDespertador(in).dormir);
}

void test_bateria_baixa_desliga_mas_carregando_dorme() {
    Estado e = estadoCom({oc(16 * 60)});
    auto in = base(&e);
    in.bateriaPercent = 14;
    auto d = decidirDespertador(in);
    TEST_ASSERT_FALSE(d.dormir);
    TEST_ASSERT_EQUAL(static_cast<int>(PorqueDesliga::BateriaBaixa), static_cast<int>(d.porque));
    in.bateriaPercent = 15;
    TEST_ASSERT_TRUE(decidirDespertador(in).dormir);
    in.bateriaPercent = 5;
    in.carregando = true;
    TEST_ASSERT_TRUE(decidirDespertador(in).dormir);
    in.carregando = false;
    in.bateriaPercent = -1;                  // desconhecida e sem carregar
    TEST_ASSERT_FALSE(decidirDespertador(in).dormir);
}

void test_medicacao_insiste_15min_depois_do_ultimo_aviso() {
    Estado e = estadoCom({oc(14 * 60 + 50, TipoHabito::Medicacao)});   // 14:50, agora 15:00 (na janela)
    auto in = base(&e);
    in.ultimoAvisoMedicacaoEpoch = kEpoch - 6 * 60;   // avisou ha 6 min -> insiste em 9 min
    auto d = decidirDespertador(in);
    TEST_ASSERT_TRUE(d.dormir);
    TEST_ASSERT_EQUAL(static_cast<int>(OrigemDespertar::InsistenciaMedicacao), static_cast<int>(d.origem));
    TEST_ASSERT_EQUAL_INT64(kEpoch + 9 * 60, d.alvoEpoch);
    TEST_ASSERT_EQUAL_INT64(kEpoch + 9 * 60 - 5, d.acordarEpoch);
    // desconhecido: 15 min a partir de agora
    in.ultimoAvisoMedicacaoEpoch.reset();
    TEST_ASSERT_EQUAL_INT64(kEpoch + 15 * 60, decidirDespertador(in).alvoEpoch);
}

void test_medicacao_para_apos_max_insistencias_e_fora_da_janela() {
    Estado e = estadoCom({oc(14 * 60 + 50, TipoHabito::Medicacao)});
    auto in = base(&e);
    in.insistenciasMedicacao = kMaxInsistenciasMedicacao;
    TEST_ASSERT_FALSE(decidirDespertador(in).dormir);   // sem outro lembrete: desliga
    // ocorrencia perdida (alem da janela + atraso): nao insiste
    Estado velha = estadoCom({oc(8 * 60, TipoHabito::Medicacao)});
    TEST_ASSERT_FALSE(decidirDespertador(base(&velha)).dormir);
}

void test_proximo_horario_vence_insistencia_quando_antes() {
    Estado e = estadoCom({oc(14 * 60 + 50, TipoHabito::Medicacao), oc(15 * 60 + 5)});   // outro habito em 5 min
    const auto d = decidirDespertador(base(&e));
    TEST_ASSERT_TRUE(d.dormir);
    TEST_ASSERT_EQUAL_INT64(kEpoch + 5 * 60, d.alvoEpoch);
    TEST_ASSERT_EQUAL(static_cast<int>(OrigemDespertar::Lembrete), static_cast<int>(d.origem));
}

void test_limite_de_ciclos_so_ate_o_fim_do_dia() {
    Estado e = estadoCom({oc(18 * 60)});
    auto in = base(&e);
    in.ciclosSemInteracao = kMaxCiclosSemInteracao;
    TEST_ASSERT_TRUE(decidirDespertador(in).dormir);    // ainda ha lembrete de hoje
    Estado acabou = estadoCom({oc(9 * 60)});
    in.estado = &acabou;
    in.segundoDoDia = 22 * 3600;
    in.primeiroLembreteAmanhaMin = 7 * 60;
    auto d = decidirDespertador(in);
    TEST_ASSERT_FALSE(d.dormir);                         // amanha nao: poupa bateria
    TEST_ASSERT_EQUAL(static_cast<int>(PorqueDesliga::LimiteDeCiclos), static_cast<int>(d.porque));
    in.ciclosSemInteracao = kMaxCiclosSemInteracao - 1;
    TEST_ASSERT_TRUE(decidirDespertador(in).dormir);
}

void test_alvo_iminente_nao_agenda_timer_no_passado() {
    Estado e = estadoCom({oc(15 * 60)});
    auto in = base(&e);
    in.segundoDoDia = 15 * 3600 - 2;   // lembrete em 2 s
    const auto d = decidirDespertador(in);
    TEST_ASSERT_TRUE(d.dormir);
    TEST_ASSERT_EQUAL_INT64(kEpoch + kMinimoSonoS, d.acordarEpoch);
}

void test_primeiro_lembrete_do_dia_da_lista() {
    ListaHabitos lista;
    DefHabito a;
    a.id = "a";
    a.name = "Agua";
    a.reminders = {"21:00", "08:30"};
    lista.habitos.push_back(a);
    DefHabito b;
    b.id = "b";
    b.name = "Livre";
    lista.habitos.push_back(b);
    const auto m = primeiroLembreteDoDia(lista, kHoje);
    TEST_ASSERT_TRUE(m.has_value());
    TEST_ASSERT_EQUAL_INT(8 * 60 + 30, *m);
    TEST_ASSERT_FALSE(primeiroLembreteDoDia(ListaHabitos{}, kHoje).has_value());
}

void test_avaliar_despertar_na_hora_nao_dorme_de_novo() {
    const auto a = avaliarDespertar(kEpoch + 10, kEpoch + 5, 0);   // acordou 5 s DEPOIS do alvo
    TEST_ASSERT_FALSE(a.adiantado);
    TEST_ASSERT_TRUE(a.atrasado);
    const auto b = avaliarDespertar(kEpoch, kEpoch + 5, 0);   // acordou 5 s antes (antecedencia normal)
    TEST_ASSERT_FALSE(b.adiantado);
    TEST_ASSERT_FALSE(b.atrasado);
    const auto c = avaliarDespertar(kEpoch, kEpoch + kAdiantadoMinS, 0);   // exatamente no limite: ainda na hora
    TEST_ASSERT_FALSE(c.adiantado);
}

void test_avaliar_despertar_adiantado_dorme_ate_o_alvo() {
    const auto a = avaliarDespertar(kEpoch, kEpoch + 300, 0);   // faltam 5 min
    TEST_ASSERT_TRUE(a.adiantado);
    TEST_ASSERT_FALSE(a.atrasado);
    TEST_ASSERT_EQUAL_INT64(300, a.faltamS);
    TEST_ASSERT_EQUAL_UINT32(static_cast<uint32_t>(300 - kAntecedenciaS), a.dormirS);   // acorda de novo com a mesma antecedencia
    const auto b = avaliarDespertar(kEpoch, kEpoch + 21, 0);   // 21 s: logo acima do limite, nunca menos que o minimo
    TEST_ASSERT_TRUE(b.adiantado);
    TEST_ASSERT_TRUE(b.dormirS >= kMinimoSonoS);
}

void test_avaliar_despertar_limita_re_dormidas_e_ignora_sem_relogio() {
    TEST_ASSERT_FALSE(avaliarDespertar(kEpoch, kEpoch + 300, kMaxReDormidas).adiantado);   // protecao contra laco
    TEST_ASSERT_TRUE(avaliarDespertar(kEpoch, kEpoch + 300, kMaxReDormidas - 1).adiantado);
    TEST_ASSERT_FALSE(avaliarDespertar(0, kEpoch + 300, 0).adiantado);   // sem hora: trata como na hora
    TEST_ASSERT_FALSE(avaliarDespertar(kEpoch, 0, 0).adiantado);         // sem alvo gravado
    TEST_ASSERT_FALSE(avaliarDespertar(kEpoch, kEpoch + kHorizonteMaxS + 3600, 0).adiantado);   // alvo absurdo (RTC errado)
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_dorme_ate_proximo_lembrete_5s_antes);
    RUN_TEST(test_manual_sempre_desliga);
    RUN_TEST(test_relogio_invalido_desliga);
    RUN_TEST(test_sem_lembrete_pendente_desliga);
    RUN_TEST(test_estado_de_outro_dia_e_ignorado);
    RUN_TEST(test_horizonte_14h);
    RUN_TEST(test_amanha_de_manha_quando_hoje_acabou);
    RUN_TEST(test_bateria_baixa_desliga_mas_carregando_dorme);
    RUN_TEST(test_medicacao_insiste_15min_depois_do_ultimo_aviso);
    RUN_TEST(test_medicacao_para_apos_max_insistencias_e_fora_da_janela);
    RUN_TEST(test_proximo_horario_vence_insistencia_quando_antes);
    RUN_TEST(test_limite_de_ciclos_so_ate_o_fim_do_dia);
    RUN_TEST(test_alvo_iminente_nao_agenda_timer_no_passado);
    RUN_TEST(test_primeiro_lembrete_do_dia_da_lista);
    RUN_TEST(test_avaliar_despertar_na_hora_nao_dorme_de_novo);
    RUN_TEST(test_avaliar_despertar_adiantado_dorme_ate_o_alvo);
    RUN_TEST(test_avaliar_despertar_limita_re_dormidas_e_ignora_sem_relogio);
    return UNITY_END();
}
