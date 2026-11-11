#include <unity.h>

#include "update/OtaConfirmacao.h"

using namespace ota;

void setUp() {}
void tearDown() {}

namespace {
    Confirmador saudavelAte(uint32_t quadroMs, bool sd = true, bool toque = true) {
        Confirmador c;
        c.setupConcluido();
        c.toqueOk(toque);
        c.sdMontado(sd);
        c.primeiroQuadro(quadroMs);
        return c;
    }
}

void test_sem_quadro_nao_confirma() {
    Confirmador c;
    c.setupConcluido();
    c.toqueOk(true);
    c.sdMontado(true);
    c.tick(50000);
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Pendente, 50000));
}

void test_confirma_apos_10s_de_laco() {
    auto c = saudavelAte(1000);
    for (uint32_t t = 1000; t <= 10000; t += 1000) c.tick(t);
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Pendente, 10999));
    c.tick(11000);
    TEST_ASSERT_TRUE(c.deveConfirmar(EstadoImagem::Pendente, 11000));
}

void test_so_confirma_se_pendente() {
    auto c = saudavelAte(0);
    for (uint32_t t = 0; t <= 20000; t += 1000) c.tick(t);
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::SemEstado, 20000));
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Valida, 20000));
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Outro, 20000));
    TEST_ASSERT_TRUE(c.deveConfirmar(EstadoImagem::Pendente, 20000));
}

void test_exige_setup_e_toque() {
    Confirmador c;
    c.toqueOk(true);
    c.sdMontado(true);
    c.primeiroQuadro(0);
    c.tick(20000);
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Pendente, 20000));   // sem setup concluido
    auto d = saudavelAte(0, true, false);
    d.tick(20000);
    TEST_ASSERT_FALSE(d.deveConfirmar(EstadoImagem::Pendente, 20000));   // toque falhou
}

void test_lacuna_grande_reinicia_a_contagem() {
    auto c = saudavelAte(0);
    c.tick(4000);
    c.tick(12000);   // lacuna de 8 s: travou
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Pendente, 12000));
    c.tick(17000);
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Pendente, 21999));
    c.tick(21000);
    c.tick(22000);
    TEST_ASSERT_TRUE(c.deveConfirmar(EstadoImagem::Pendente, 22000));
}

void test_sem_sd_exige_60s() {
    auto c = saudavelAte(0, false);
    for (uint32_t t = 0; t <= 30000; t += 1000) c.tick(t);
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Pendente, 30000));
    for (uint32_t t = 31000; t <= 60000; t += 1000) c.tick(t);
    TEST_ASSERT_TRUE(c.deveConfirmar(EstadoImagem::Pendente, 60000));
}

void test_antes_de_dormir_basta_setup_e_quadro() {
    Confirmador c;
    TEST_ASSERT_FALSE(c.deveConfirmarAntesDeReiniciar(EstadoImagem::Pendente));   // nada pronto
    c.setupConcluido();
    TEST_ASSERT_FALSE(c.deveConfirmarAntesDeReiniciar(EstadoImagem::Pendente));   // sem quadro
    c.primeiroQuadro(100);
    TEST_ASSERT_TRUE(c.deveConfirmarAntesDeReiniciar(EstadoImagem::Pendente));    // 0 s de laco: ainda assim confirma
    TEST_ASSERT_FALSE(c.deveConfirmarAntesDeReiniciar(EstadoImagem::Valida));
    TEST_ASSERT_FALSE(c.deveConfirmarAntesDeReiniciar(EstadoImagem::SemEstado));
}

void test_confirmada_nao_confirma_de_novo() {
    auto c = saudavelAte(0);
    for (uint32_t t = 1000; t <= 11000; t += 1000) c.tick(t);
    TEST_ASSERT_TRUE(c.deveConfirmar(EstadoImagem::Pendente, 11000));
    c.confirmada();
    TEST_ASSERT_FALSE(c.deveConfirmar(EstadoImagem::Pendente, 12000));
    TEST_ASSERT_FALSE(c.deveConfirmarAntesDeReiniciar(EstadoImagem::Pendente));
}

void test_relogio_com_overflow() {
    const uint32_t base = 0xFFFFFF00u;   // 11 s depois o uint32 ja deu a volta
    auto c = saudavelAte(base);
    uint32_t t = base;
    for (int k = 0; k <= 11; ++k) {
        t = base + static_cast<uint32_t>(k) * 1000u;
        c.tick(t);
    }
    TEST_ASSERT_TRUE(t < base);   // confirma que houve a virada
    TEST_ASSERT_TRUE(c.deveConfirmar(EstadoImagem::Pendente, t));
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_sem_quadro_nao_confirma);
    RUN_TEST(test_confirma_apos_10s_de_laco);
    RUN_TEST(test_so_confirma_se_pendente);
    RUN_TEST(test_exige_setup_e_toque);
    RUN_TEST(test_lacuna_grande_reinicia_a_contagem);
    RUN_TEST(test_sem_sd_exige_60s);
    RUN_TEST(test_antes_de_dormir_basta_setup_e_quadro);
    RUN_TEST(test_confirmada_nao_confirma_de_novo);
    RUN_TEST(test_relogio_com_overflow);
    return UNITY_END();
}
