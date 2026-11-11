// Modo Foco etapa 0 (tempo/ModoFoco): escurecer apos 10 s, estados que bloqueiam, 1o toque consumido + armar 300 ms,
// rampas, dia/noite/piso, toda saida restaura o brilho e ajuste Desligado. Etapa 1 (ajuste Cena): quando a cena aparece,
// brilho proprio da cena (dia/noite), vitoria no brilho do usuario, sem saltos, e toda saida restaura. Logica pura.
#include <unity.h>

#include <initializer_list>

#include "tempo/ModoFoco.h"

using namespace tempo::modofoco;

void setUp() {}
void tearDown() {}

namespace {
    constexpr uint8_t kUsuario = 70;
    constexpr int kTarde = 14 * 60 + 20;
    constexpr uint8_t kAlvoDia = 28;     // 40 % de 70
    constexpr uint8_t kAlvoNoite = 14;   // 20 % de 70

    Contexto focando(int minuto = kTarde) {
        Contexto c;
        c.ajuste = Ajuste::Escurecer;
        c.telaBatalha = true;
        c.fase = Fase::Focando;
        c.brilhoUsuario = kUsuario;
        c.minutoDoDia = minuto;
        return c;
    }

    // Roda o laco de `de` ate `ate` (inclusive) em passos de 10 ms; devolve o ultimo brilho.
    uint8_t rodar(Controlador& m, const Contexto& c, uint32_t de, uint32_t ate) {
        uint8_t b = 0;
        for (uint32_t t = de; t <= ate; t += 10) b = m.atualizar(c, t);
        return b;
    }

    // Leva o controlador ao estado Escuro (comecando em t=1000) e devolve o instante final.
    uint32_t escurecer(Controlador& m, const Contexto& c) {
        rodar(m, c, 1000, 1000 + kOciosoMs + kDescerMs + 20);
        TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escuro), static_cast<int>(m.estado()));
        return 1000 + kOciosoMs + kDescerMs + 20;
    }
} // namespace

// ---------------------------------------------------------------- entrada
void test_dez_segundos_sem_toque_escurece_com_rampa() {
    Controlador m;
    const auto c = focando();
    TEST_ASSERT_EQUAL_UINT8(kUsuario, rodar(m, c, 1000, 1000 + kOciosoMs - 10));
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()));
    TEST_ASSERT_EQUAL_UINT8(kUsuario, m.atualizar(c, 1000 + kOciosoMs));   // comeca a rampa (nivel 0)
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escurecendo), static_cast<int>(m.estado()));
    // meio da rampa: entre o normal e o alvo, descendo
    const uint8_t meio = m.atualizar(c, 1000 + kOciosoMs + kDescerMs / 2);
    TEST_ASSERT_TRUE(meio < kUsuario && meio > kAlvoDia);
    TEST_ASSERT_UINT8_WITHIN(1, (kUsuario + kAlvoDia) / 2, meio);
    TEST_ASSERT_EQUAL_UINT8(kAlvoDia, m.atualizar(c, 1000 + kOciosoMs + kDescerMs));
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escuro), static_cast<int>(m.estado()));
}

void test_hora_extra_e_pausa_do_ciclo_tambem_escurecem() {
    for (Fase f : {Fase::HoraExtra, Fase::PausaCiclo}) {
        Controlador m;
        auto c = focando();
        c.fase = f;
        escurecer(m, c);
        TEST_ASSERT_EQUAL_UINT8(kAlvoDia, m.atualizar(c, 20000));
    }
}

void test_estados_que_bloqueiam_nunca_escurecem() {
    struct Caso { const char* nome; Contexto c; };
    Caso casos[] = {
        {"pausado", focando()}, {"distraido", focando()}, {"fechamento", focando()}, {"sem batalha", focando()},
        {"fora da tela da batalha", focando()}, {"fim segurando", focando()}, {"dedo na tela", focando()},
        {"aviso de habito", focando()}, {"em pe", focando()}, {"desligado", focando()},
    };
    casos[0].c.fase = Fase::Pausado;
    casos[1].c.fase = Fase::Distraido;
    casos[2].c.fase = Fase::Fechamento;
    casos[3].c.fase = Fase::Nenhuma;
    casos[4].c.telaBatalha = false;
    casos[5].c.segurandoFim = true;
    casos[6].c.dedoNaTela = true;
    casos[7].c.avisoHabito = true;
    casos[8].c.emPe = true;
    casos[9].c.ajuste = Ajuste::Desligado;
    for (const auto& caso : casos) {
        TEST_ASSERT_FALSE_MESSAGE(podeEscurecer(caso.c), caso.nome);
        Controlador m;
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(kUsuario, rodar(m, caso.c, 1000, 1000 + 3 * kOciosoMs), caso.nome);
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()), caso.nome);
    }
    TEST_ASSERT_TRUE(podeEscurecer(focando()));
}

// ---------------------------------------------------------------- ajuste Cena (etapa 1)
namespace {
    constexpr uint8_t kCenaDia = 53;     // 75 % de 70 (52,5 -> 53)
    constexpr uint8_t kCenaNoite = 32;   // 45 % de 70 (31,5 -> 32)

    Contexto comCena(int minuto = kTarde) {
        auto c = focando(minuto);
        c.ajuste = Ajuste::Cena;
        return c;
    }
} // namespace

void test_cena_tem_brilho_proprio_de_dia_e_de_noite() {
    TEST_ASSERT_EQUAL_UINT8(kCenaDia, brilhoCena(kUsuario, kTarde));
    TEST_ASSERT_EQUAL_UINT8(kCenaNoite, brilhoCena(kUsuario, 23 * 60));
    TEST_ASSERT_EQUAL_UINT8(kPisoPct, brilhoCena(5, kTarde));      // piso: nunca abaixo do minimo do usuario
    TEST_ASSERT_EQUAL_UINT8(3, brilhoCena(3, kTarde));             // nunca acima do usuario
    for (int minuto : {kTarde, 23 * 60}) {
        Controlador m;
        const auto c = comCena(minuto);
        escurecer(m, c);
        TEST_ASSERT_TRUE(m.mostrarCena(c));
        TEST_ASSERT_EQUAL_UINT8(minuto == kTarde ? kCenaDia : kCenaNoite, m.atualizar(c, 20000));
    }
}

void test_cena_aparece_na_metade_da_rampa_com_mergulho() {
    Controlador m;
    const auto c = comCena();
    rodar(m, c, 1000, 1000 + kOciosoMs);
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escurecendo), static_cast<int>(m.estado()));
    // 1a metade: ainda a batalha, descendo do usuario ate o escurecido (28)
    const uint8_t quarto = m.atualizar(c, 1000 + kOciosoMs + kDescerMs / 4);
    TEST_ASSERT_FALSE(m.mostrarCena(c));
    TEST_ASSERT_UINT8_WITHIN(1, (kUsuario + kAlvoDia) / 2, quarto);
    // metade: troca para a cena no ponto mais escuro
    TEST_ASSERT_UINT8_WITHIN(1, kAlvoDia, m.atualizar(c, 1000 + kOciosoMs + kDescerMs / 2));
    TEST_ASSERT_TRUE(m.mostrarCena(c));
    // 2a metade: a cena sobe ate o brilho dela
    const uint8_t tresQuartos = m.atualizar(c, 1000 + kOciosoMs + 3 * kDescerMs / 4);
    TEST_ASSERT_UINT8_WITHIN(1, (kAlvoDia + kCenaDia) / 2, tresQuartos);
    TEST_ASSERT_EQUAL_UINT8(kCenaDia, m.atualizar(c, 1000 + kOciosoMs + kDescerMs));
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escuro), static_cast<int>(m.estado()));
    // o toque com a cena na tela e consumido (so acorda)
    TEST_ASSERT_TRUE(m.toque(1000 + kOciosoMs + kDescerMs + 10));
}

void test_cena_nao_aparece_na_pausa_do_ciclo() {
    Controlador m;
    auto c = comCena();
    c.fase = Fase::PausaCiclo;
    escurecer(m, c);
    TEST_ASSERT_FALSE(m.mostrarCena(c));   // a pausa do ciclo so escurece (etapa 0)
    TEST_ASSERT_EQUAL_UINT8(kAlvoDia, m.atualizar(c, 20000));
    // a pausa acabou e a rodada seguinte comecou sem toque: a cena entra, o brilho anda sem saltar
    c.fase = Fase::Focando;
    const uint8_t logo = m.atualizar(c, 20010);
    TEST_ASSERT_TRUE(m.mostrarCena(c));
    TEST_ASSERT_TRUE(logo <= kAlvoDia + 2);
    TEST_ASSERT_EQUAL_UINT8(kCenaDia, rodar(m, c, 20020, 20020 + kTrocaBrilhoCenaMs));
}

void test_vitoria_no_brilho_do_usuario_e_volta_suave_ao_da_cena() {
    Controlador m;
    auto c = comCena();
    uint32_t t = escurecer(m, c);
    TEST_ASSERT_EQUAL_UINT8(kCenaDia, m.atualizar(c, t += 10));
    c.fase = Fase::HoraExtra;
    c.cenaVitoria = true;
    uint8_t anterior = kCenaDia;
    for (uint32_t fim = t + kTrocaBrilhoCenaMs; t < fim;) {   // sobe sem saltos
        const uint8_t b = m.atualizar(c, t += 10);
        TEST_ASSERT_TRUE(b >= anterior && b - anterior <= 2);
        anterior = b;
    }
    TEST_ASSERT_EQUAL_UINT8(kUsuario, m.atualizar(c, t += 10));
    TEST_ASSERT_TRUE(m.mostrarCena(c));        // continua a cena (vitoria), so o brilho mudou
    c.cenaVitoria = false;                     // amanhecer
    TEST_ASSERT_TRUE(m.atualizar(c, t += 10) > kCenaDia);
    TEST_ASSERT_EQUAL_UINT8(kCenaDia, rodar(m, c, t + 10, t + 10 + kTrocaBrilhoCenaMs));
}

void test_acordar_da_cena_sobe_do_brilho_da_cena_sem_mergulhar() {
    Controlador m;
    const auto c = comCena();
    uint32_t t = escurecer(m, c);
    TEST_ASSERT_EQUAL_UINT8(kCenaDia, m.atualizar(c, t += 10));
    TEST_ASSERT_TRUE(m.toque(t += 10));
    TEST_ASSERT_FALSE(m.mostrarCena(c));
    uint8_t anterior = kCenaDia;
    for (uint32_t fim = t + kSubirMs; t < fim;) {
        const uint8_t b = m.atualizar(c, t += 10);
        TEST_ASSERT_FALSE(m.mostrarCena(c));
        TEST_ASSERT_TRUE(b >= anterior);       // nunca desce (nao passa pelo escurecido)
        anterior = b;
    }
    TEST_ASSERT_EQUAL_UINT8(kUsuario, m.atualizar(c, t += 10));
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()));
}

void test_cena_cada_saida_restaura_o_brilho() {
    struct Saida { const char* nome; void (*aplicar)(Contexto&); };
    const Saida saidas[] = {
        {"pausar", [](Contexto& c) { c.fase = Fase::Pausado; }},
        {"distrai", [](Contexto& c) { c.fase = Fase::Distraido; }},
        {"teto do overtime / saque / Reflexao", [](Contexto& c) { c.fase = Fase::Fechamento; }},
        {"batalha cancelada", [](Contexto& c) { c.fase = Fase::Nenhuma; }},
        {"trocar de app / standby / OTA", [](Contexto& c) { c.telaBatalha = false; }},
        {"aviso de habito", [](Contexto& c) { c.avisoHabito = true; }},
        {"em pe", [](Contexto& c) { c.emPe = true; }},
        {"ajuste Desligado", [](Contexto& c) { c.ajuste = Ajuste::Desligado; }},
    };
    for (const auto& s : saidas) {
        for (bool vitoria : {false, true}) {
            Controlador m;
            auto c = comCena();
            c.cenaVitoria = vitoria;
            const uint32_t t = escurecer(m, c);
            rodar(m, c, t, t + kTrocaBrilhoCenaMs);
            s.aplicar(c);
            c.cenaVitoria = false;
            m.atualizar(c, t + kTrocaBrilhoCenaMs + 10);
            TEST_ASSERT_FALSE_MESSAGE(m.mostrarCena(c), s.nome);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(kUsuario, rodar(m, c, t + kTrocaBrilhoCenaMs + 20,
                                                           t + kTrocaBrilhoCenaMs + 20 + kSubirMs), s.nome);
            TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()), s.nome);
            TEST_ASSERT_EQUAL_UINT8_MESSAGE(kUsuario, rodar(m, c, t + 4000, t + 3 * kOciosoMs), s.nome);
        }
    }
}

void test_dez_segundos_so_contam_depois_de_voltar_ao_foco() {
    // "Voltei"/"Continuar": distraido/pausado por muito tempo nao "adianta" o relogio dos 10 s
    Controlador m;
    auto c = focando();
    c.fase = Fase::Distraido;
    rodar(m, c, 1000, 60000);
    c.fase = Fase::Focando;   // Voltei em t=60010
    TEST_ASSERT_EQUAL_UINT8(kUsuario, rodar(m, c, 60010, 60010 + kOciosoMs - 20));
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()));
    m.atualizar(c, 60010 + kOciosoMs);
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escurecendo), static_cast<int>(m.estado()));
}

void test_toque_zera_os_dez_segundos() {
    Controlador m;
    const auto c = focando();
    rodar(m, c, 1000, 9000);
    TEST_ASSERT_FALSE(m.toque(9000));   // tela clara: o toque vai para o botao
    TEST_ASSERT_EQUAL_UINT8(kUsuario, rodar(m, c, 9010, 9000 + kOciosoMs - 10));
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()));
}

// ---------------------------------------------------------------- rampa
void test_toque_no_comeco_da_rampa_cancela_sem_consumir() {
    Controlador m;
    const auto c = focando();
    rodar(m, c, 1000, 1000 + kOciosoMs + kDescerMs / 5);   // 20 % da rampa
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escurecendo), static_cast<int>(m.estado()));
    const uint32_t t = 1000 + kOciosoMs + kDescerMs / 5 + 10;
    TEST_ASSERT_FALSE(m.toque(t));   // ainda claro: cancela e o botao funciona
    TEST_ASSERT_EQUAL_UINT8(kUsuario, rodar(m, c, t, t + kSubirMs));
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()));
    // e os 10 s recomecam do toque
    TEST_ASSERT_EQUAL_UINT8(kUsuario, rodar(m, c, t + kSubirMs + 10, t + kOciosoMs - 10));
    m.atualizar(c, t + kOciosoMs);
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escurecendo), static_cast<int>(m.estado()));
}

void test_toque_no_fim_da_rampa_e_consumido() {
    Controlador m;
    const auto c = focando();
    rodar(m, c, 1000, 1000 + kOciosoMs + kDescerMs * 3 / 4);
    TEST_ASSERT_TRUE(m.toque(1000 + kOciosoMs + kDescerMs * 3 / 4 + 5));
}

// ---------------------------------------------------------------- acordar
void test_primeiro_toque_so_acorda_e_e_consumido() {
    Controlador m;
    const auto c = focando();
    const uint32_t t = escurecer(m, c);
    TEST_ASSERT_TRUE(m.toque(t + 10));   // consumido: nao aciona o botao
    // sobe em ~300 ms
    const uint8_t meio = m.atualizar(c, t + 10 + kSubirMs / 2);
    TEST_ASSERT_TRUE(meio > kAlvoDia && meio < kUsuario);
    TEST_ASSERT_EQUAL_UINT8(kUsuario, m.atualizar(c, t + 10 + kSubirMs));
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()));
}

void test_botoes_so_armam_300ms_depois_de_acordar() {
    Controlador m;
    const auto c = focando();
    const uint32_t t = escurecer(m, c);
    TEST_ASSERT_TRUE(m.toque(t));                       // acorda
    m.atualizar(c, t + 100);
    TEST_ASSERT_TRUE(m.toque(t + 150));                 // toque duplo rapido (risco 8): ainda consumido
    m.atualizar(c, t + 200);
    TEST_ASSERT_TRUE(m.toque(t + kArmarMs - 1));        // ainda dentro da janela
    m.atualizar(c, t + kArmarMs + 50);
    TEST_ASSERT_FALSE(m.toque(t + kArmarMs + 60));      // armado: vai para o botao
    TEST_ASSERT_FALSE(m.toque(t + kArmarMs + 200));
}

void test_depois_de_acordar_escurece_de_novo_apos_dez_segundos() {
    Controlador m;
    const auto c = focando();
    const uint32_t t = escurecer(m, c);
    m.toque(t);
    TEST_ASSERT_EQUAL_UINT8(kUsuario, rodar(m, c, t, t + kOciosoMs - 10));
    m.atualizar(c, t + kOciosoMs);
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escurecendo), static_cast<int>(m.estado()));
    TEST_ASSERT_EQUAL_UINT8(kAlvoDia, rodar(m, c, t + kOciosoMs, t + kOciosoMs + kDescerMs + 10));
}

void test_botao_acorda_sem_ser_consumido() {
    Controlador m;
    const auto c = focando();
    const uint32_t t = escurecer(m, c);
    m.botao(t);   // BOOT: mantem a funcao (o App executa normalmente) e acorda
    m.atualizar(c, t + 50);
    TEST_ASSERT_TRUE(m.toque(t + 100));   // toque junto com o BOOT: nao aciona nada
    TEST_ASSERT_EQUAL_UINT8(kUsuario, rodar(m, c, t + 110, t + 110 + kSubirMs));
    TEST_ASSERT_FALSE(m.toque(t + 110 + kSubirMs + 10));
}

// ---------------------------------------------------------------- dia / noite / piso
void test_alvo_de_dia_de_noite_e_piso() {
    TEST_ASSERT_EQUAL_UINT8(kAlvoDia, brilhoEscurecido(70, kTarde));
    TEST_ASSERT_EQUAL_UINT8(kAlvoNoite, brilhoEscurecido(70, 23 * 60));
    TEST_ASSERT_EQUAL_UINT8(kAlvoNoite, brilhoEscurecido(70, 2 * 60));
    TEST_ASSERT_EQUAL_UINT8(kAlvoNoite, brilhoEscurecido(70, 22 * 60));       // 22:00 ja e noite
    TEST_ASSERT_EQUAL_UINT8(kAlvoDia, brilhoEscurecido(70, 6 * 60));          // 06:00 ja e dia
    TEST_ASSERT_EQUAL_UINT8(kAlvoNoite, brilhoEscurecido(70, 6 * 60 - 1));
    TEST_ASSERT_EQUAL_UINT8(kAlvoDia, brilhoEscurecido(70, -1));             // hora desconhecida = dia
    TEST_ASSERT_EQUAL_UINT8(40, brilhoEscurecido(100, kTarde));
    TEST_ASSERT_EQUAL_UINT8(20, brilhoEscurecido(100, 23 * 60));
    // piso: nunca abaixo de kPisoPct, nunca acima do proprio usuario
    TEST_ASSERT_EQUAL_UINT8(kPisoPct, brilhoEscurecido(10, 23 * 60));        // 2 % -> 5 %
    TEST_ASSERT_EQUAL_UINT8(kPisoPct, brilhoEscurecido(5, kTarde));
    TEST_ASSERT_EQUAL_UINT8(3, brilhoEscurecido(3, kTarde));                  // abaixo do piso: nao escurece (nem clareia)
    for (int u = 1; u <= 100; ++u)
        for (int min : {-1, 0, 300, 360, 720, 1319, 1320, 1439}) {
            const uint8_t a = brilhoEscurecido(uint8_t(u), min);
            TEST_ASSERT_TRUE(a <= u);
            TEST_ASSERT_TRUE(a >= (u < kPisoPct ? u : kPisoPct));
        }
}

void test_noite_escurece_mais() {
    Controlador m;
    const auto c = focando(23 * 60);
    escurecer(m, c);
    TEST_ASSERT_EQUAL_UINT8(kAlvoNoite, m.atualizar(c, 20000));
}

void test_brilho_do_usuario_mudando_segue_o_novo_valor() {
    // o valor salvo e so lido: mudou em Ajustes/Companion -> o alvo acompanha (e o normal tambem)
    Controlador m;
    auto c = focando();
    escurecer(m, c);
    c.brilhoUsuario = 100;
    TEST_ASSERT_EQUAL_UINT8(40, m.atualizar(c, 20000));
    c.ajuste = Ajuste::Desligado;
    TEST_ASSERT_EQUAL_UINT8(100, rodar(m, c, 20010, 20010 + kSubirMs));
}

// ---------------------------------------------------------------- toda saida restaura
void test_cada_saida_restaura_o_brilho() {
    struct Saida { const char* nome; void (*aplicar)(Contexto&); };
    const Saida saidas[] = {
        {"pausar", [](Contexto& c) { c.fase = Fase::Pausado; }},
        {"distrai", [](Contexto& c) { c.fase = Fase::Distraido; }},
        {"fim da batalha / teto do overtime / saque / Reflexao", [](Contexto& c) { c.fase = Fase::Fechamento; }},
        {"batalha cancelada", [](Contexto& c) { c.fase = Fase::Nenhuma; }},
        {"trocar de app / standby / OTA / despertador / deep sleep", [](Contexto& c) { c.telaBatalha = false; }},
        {"aviso de habito", [](Contexto& c) { c.avisoHabito = true; }},
        {"girar a placa (em pe)", [](Contexto& c) { c.emPe = true; }},
        {"Fim segurando", [](Contexto& c) { c.segurandoFim = true; }},
        {"ajuste Desligado", [](Contexto& c) { c.ajuste = Ajuste::Desligado; }},
    };
    for (const auto& s : saidas) {
        Controlador m;
        auto c = focando();
        const uint32_t t = escurecer(m, c);
        s.aplicar(c);
        const uint8_t inicio = m.atualizar(c, t + 10);
        TEST_ASSERT_TRUE_MESSAGE(inicio < kUsuario, s.nome);   // sobe em rampa curta (nao pisca)
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(kUsuario, rodar(m, c, t + 20, t + 10 + kSubirMs), s.nome);
        TEST_ASSERT_EQUAL_MESSAGE(static_cast<int>(Estado::Normal), static_cast<int>(m.estado()), s.nome);
        // e fica normal enquanto durar
        TEST_ASSERT_EQUAL_UINT8_MESSAGE(kUsuario, rodar(m, c, t + 20 + kSubirMs, t + 3 * kOciosoMs), s.nome);
    }
}

void test_saida_no_meio_da_rampa_tambem_restaura() {
    Controlador m;
    auto c = focando();
    rodar(m, c, 1000, 1000 + kOciosoMs + kDescerMs / 2);
    c.telaBatalha = false;
    TEST_ASSERT_EQUAL_UINT8(kUsuario, rodar(m, c, 1000 + kOciosoMs + kDescerMs / 2 + 10, 1000 + kOciosoMs + kDescerMs + kSubirMs));
}

void test_ajuste_desligado_nunca_muda_o_brilho() {
    Controlador m;
    auto c = focando();
    c.ajuste = Ajuste::Desligado;
    for (uint32_t t = 0; t < 5 * kOciosoMs; t += 10) TEST_ASSERT_EQUAL_UINT8(kUsuario, m.atualizar(c, t));
    TEST_ASSERT_FALSE(m.toque(5 * kOciosoMs));
}

void test_relogio_de_millis_dando_a_volta() {
    Controlador m;
    const auto c = focando();
    const uint32_t base = 0xFFFFFFFFu - 5000u;
    rodar(m, c, base, base + 4990);
    for (uint32_t t = 0; t <= kOciosoMs + kDescerMs; t += 10) m.atualizar(c, base + 5000u + t);
    TEST_ASSERT_EQUAL(static_cast<int>(Estado::Escuro), static_cast<int>(m.estado()));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_dez_segundos_sem_toque_escurece_com_rampa);
    RUN_TEST(test_hora_extra_e_pausa_do_ciclo_tambem_escurecem);
    RUN_TEST(test_estados_que_bloqueiam_nunca_escurecem);
    RUN_TEST(test_cena_tem_brilho_proprio_de_dia_e_de_noite);
    RUN_TEST(test_cena_aparece_na_metade_da_rampa_com_mergulho);
    RUN_TEST(test_cena_nao_aparece_na_pausa_do_ciclo);
    RUN_TEST(test_vitoria_no_brilho_do_usuario_e_volta_suave_ao_da_cena);
    RUN_TEST(test_acordar_da_cena_sobe_do_brilho_da_cena_sem_mergulhar);
    RUN_TEST(test_cena_cada_saida_restaura_o_brilho);
    RUN_TEST(test_dez_segundos_so_contam_depois_de_voltar_ao_foco);
    RUN_TEST(test_toque_zera_os_dez_segundos);
    RUN_TEST(test_toque_no_comeco_da_rampa_cancela_sem_consumir);
    RUN_TEST(test_toque_no_fim_da_rampa_e_consumido);
    RUN_TEST(test_primeiro_toque_so_acorda_e_e_consumido);
    RUN_TEST(test_botoes_so_armam_300ms_depois_de_acordar);
    RUN_TEST(test_depois_de_acordar_escurece_de_novo_apos_dez_segundos);
    RUN_TEST(test_botao_acorda_sem_ser_consumido);
    RUN_TEST(test_alvo_de_dia_de_noite_e_piso);
    RUN_TEST(test_noite_escurece_mais);
    RUN_TEST(test_brilho_do_usuario_mudando_segue_o_novo_valor);
    RUN_TEST(test_cada_saida_restaura_o_brilho);
    RUN_TEST(test_saida_no_meio_da_rampa_tambem_restaura);
    RUN_TEST(test_ajuste_desligado_nunca_muda_o_brilho);
    RUN_TEST(test_relogio_de_millis_dando_a_volta);
    return UNITY_END();
}
