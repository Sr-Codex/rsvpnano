#include <unity.h>

#include <cstring>

#include "tempo/Cartao.h"

using namespace tempo::cartao;

void setUp() {}
void tearDown() {}

void test_estado_ou_backup_valido_e_ok() {
    TEST_ASSERT_TRUE(classificar(Arquivo::Valido, Arquivo::Ausente, false) == Leitura::Ok);
    TEST_ASSERT_TRUE(classificar(Arquivo::Invalido, Arquivo::Valido, true) == Leitura::Ok);    // gravação interrompida: o .bak salva
    TEST_ASSERT_TRUE(classificar(Arquivo::Ausente, Arquivo::Valido, true) == Leitura::Ok);
}

void test_cartao_novo_e_ausente() {
    TEST_ASSERT_TRUE(classificar(Arquivo::Ausente, Arquivo::Ausente, false) == Leitura::Ausente);
}

void test_estado_sumido_com_outros_dados_e_ilegivel() {
    // o resumo/logs existem mas o estado sumiu: não é cartão novo, não pode gravar zero por cima
    TEST_ASSERT_TRUE(classificar(Arquivo::Ausente, Arquivo::Ausente, true) == Leitura::Ilegivel);
}

void test_arquivo_estragado_e_ilegivel() {
    TEST_ASSERT_TRUE(classificar(Arquivo::Invalido, Arquivo::Ausente, false) == Leitura::Ilegivel);
    TEST_ASSERT_TRUE(classificar(Arquivo::Invalido, Arquivo::Invalido, false) == Leitura::Ilegivel);
    TEST_ASSERT_TRUE(classificar(Arquivo::Ausente, Arquivo::Invalido, false) == Leitura::Ilegivel);
}

void test_situacao_e_gravacao() {
    TEST_ASSERT_TRUE(aposCarregar(false, Leitura::Ok) == Situacao::SemCartao);
    TEST_ASSERT_TRUE(aposCarregar(true, Leitura::Ilegivel) == Situacao::Ilegivel);
    TEST_ASSERT_TRUE(aposCarregar(true, Leitura::Ausente) == Situacao::Ok);
    TEST_ASSERT_FALSE(podeGravar(Situacao::Ilegivel));
    TEST_ASSERT_FALSE(podeGravar(Situacao::SemCartao));
    TEST_ASSERT_TRUE(podeGravar(Situacao::Ok));
    TEST_ASSERT_TRUE(podeGravar(Situacao::FalhaGravar));   // tenta de novo na próxima vez
    TEST_ASSERT_TRUE(aposGravar(Situacao::Ok, false) == Situacao::FalhaGravar);
    TEST_ASSERT_TRUE(aposGravar(Situacao::FalhaGravar, true) == Situacao::Ok);
    TEST_ASSERT_TRUE(aposGravar(Situacao::Ilegivel, true) == Situacao::Ilegivel);   // nunca "destrava" sozinho
}

void test_avisos() {
    TEST_ASSERT_NULL(aviso(Situacao::Ok));
    TEST_ASSERT_NOT_NULL(aviso(Situacao::SemCartao));
    TEST_ASSERT_NOT_NULL(aviso(Situacao::Ilegivel));
    TEST_ASSERT_NOT_NULL(aviso(Situacao::FalhaGravar));
    TEST_ASSERT_TRUE(std::strstr(aviso(Situacao::Ilegivel), "cartão") != nullptr);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_estado_ou_backup_valido_e_ok);
    RUN_TEST(test_cartao_novo_e_ausente);
    RUN_TEST(test_estado_sumido_com_outros_dados_e_ilegivel);
    RUN_TEST(test_arquivo_estragado_e_ilegivel);
    RUN_TEST(test_situacao_e_gravacao);
    RUN_TEST(test_avisos);
    return UNITY_END();
}
