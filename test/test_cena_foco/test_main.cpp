// Modo Foco, cenas QG (tempo/CenaFocoEstado): nos novos/antes, g de cada no, fundo por estado, revelacao dos dois modos,
// cursor do terminal e a janela da vitoria. Logica pura (a arte fica no renderizador).
#include <unity.h>

#include <initializer_list>

#include "tempo/CenaFocoEstado.h"

using namespace tempo::cenafoco;

void setUp() {}
void tearDown() {}

namespace {
    // mesma ordem irregular da arte (CenasFoco.generated.cpp)
    constexpr uint8_t kOrdem[kNos] = {5, 2, 8, 0, 3, 6, 9, 1, 7, 4, 10, 11};

    Entrada focando(double p, int planejado = 25, int antes = 0) {
        Entrada e;
        e.p = p;
        e.minutosPlanejados = planejado;
        e.minutosFocoAntes = antes;
        return e;
    }

    int contar(const Quadro& q, TipoNo tipo) {
        int n = 0;
        for (const auto& no : q.nos) n += no.tipo == tipo ? 1 : 0;
        return n;
    }
} // namespace

// ---------------------------------------------------------------- quantos nos
void test_nos_novos_um_a_cada_cinco_minutos() {
    TEST_ASSERT_EQUAL_INT(1, nosNovos(0));
    TEST_ASSERT_EQUAL_INT(1, nosNovos(5));
    TEST_ASSERT_EQUAL_INT(1, nosNovos(7));    // 1,4 -> 1
    TEST_ASSERT_EQUAL_INT(2, nosNovos(8));    // 1,6 -> 2
    TEST_ASSERT_EQUAL_INT(5, nosNovos(25));
    TEST_ASSERT_EQUAL_INT(12, nosNovos(60));
    TEST_ASSERT_EQUAL_INT(12, nosNovos(90));  // teto: 12 nos no mapa
    TEST_ASSERT_EQUAL_INT(1, nosNovos(-3));
}

void test_nos_antes_do_dia_cabem_no_que_sobra() {
    TEST_ASSERT_EQUAL_INT(0, nosAntes(0, 5));
    TEST_ASSERT_EQUAL_INT(0, nosAntes(4, 5));     // menos de 5 min nao acende no
    TEST_ASSERT_EQUAL_INT(5, nosAntes(25, 5));
    TEST_ASSERT_EQUAL_INT(7, nosAntes(300, 5));   // 12 - 5
    TEST_ASSERT_EQUAL_INT(0, nosAntes(300, 12));
    TEST_ASSERT_EQUAL_INT(0, nosAntes(-10, 5));
}

// ---------------------------------------------------------------- g de cada no
void test_inicio_todos_os_nos_desta_sessao_apagados() {
    const auto q = calcular(focando(0.0, 25, 20), kOrdem);
    TEST_ASSERT_EQUAL(static_cast<int>(Fundo::Noite), static_cast<int>(q.fundo));
    TEST_ASSERT_EQUAL_INT(5, q.novos);
    TEST_ASSERT_EQUAL_INT(4, q.antes);
    TEST_ASSERT_EQUAL_INT(0, q.tomados);
    TEST_ASSERT_TRUE(q.revelar && q.mapa);
    TEST_ASSERT_EQUAL_INT(4, contar(q, TipoNo::Antes));
    TEST_ASSERT_EQUAL_INT(5, contar(q, TipoNo::Novo));
    TEST_ASSERT_EQUAL_INT(3, contar(q, TipoNo::Apagado));
    // os "antes" sao os primeiros da ordem; os novos vem logo depois
    for (int i = 0; i < 4; ++i) TEST_ASSERT_EQUAL(static_cast<int>(TipoNo::Antes), static_cast<int>(q.nos[kOrdem[i]].tipo));
    for (int i = 4; i < 9; ++i) {
        TEST_ASSERT_EQUAL(static_cast<int>(TipoNo::Novo), static_cast<int>(q.nos[kOrdem[i]].tipo));
        TEST_ASSERT_EQUAL_FLOAT(0.0f, q.nos[kOrdem[i]].g);
    }
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, kCheioMinimo, q.cheio);
}

void test_meio_g_por_no_e_tomados() {
    // 25 min (5 nos), p = 0,62 -> p*5 = 3,1: 3 completos, o 4o com g = 0,1, o 5o apagado
    const auto q = calcular(focando(0.62, 25), kOrdem);
    TEST_ASSERT_EQUAL_INT(3, q.tomados);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, q.nos[kOrdem[0]].g);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, q.nos[kOrdem[2]].g);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.1f, q.nos[kOrdem[3]].g);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, q.nos[kOrdem[4]].g);
    TEST_ASSERT_FLOAT_WITHIN(1e-4f, 0.12f + 0.88f * 0.62f, q.cheio);
}

void test_p_quase_um_e_fora_da_faixa() {
    auto q = calcular(focando(0.9999, 60), kOrdem);
    TEST_ASSERT_EQUAL_INT(12, q.novos);
    TEST_ASSERT_EQUAL_INT(11, q.tomados);
    TEST_ASSERT_TRUE(q.nos[kOrdem[11]].g > 0.99f && q.nos[kOrdem[11]].g < 1.0f);
    q = calcular(focando(1.7, 25), kOrdem);   // p > 1 (arredondamento): tudo aceso, nada alem
    TEST_ASSERT_EQUAL_INT(5, q.tomados);
    q = calcular(focando(-0.5, 25), kOrdem);
    TEST_ASSERT_EQUAL_INT(0, q.tomados);
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, kCheioMinimo, q.cheio);
}

// ---------------------------------------------------------------- fundo por estado
void test_vitoria_e_hora_extra() {
    auto e = focando(0.3, 25, 10);
    e.horaExtra = true;
    e.vitoria = true;
    auto q = calcular(e, kOrdem);
    TEST_ASSERT_EQUAL(static_cast<int>(Fundo::Vitoria), static_cast<int>(q.fundo));
    TEST_ASSERT_FALSE(q.revelar);
    TEST_ASSERT_FALSE(q.mapa);   // arte pronta
    e.vitoria = false;
    q = calcular(e, kOrdem);
    TEST_ASSERT_EQUAL(static_cast<int>(Fundo::Amanhecer), static_cast<int>(q.fundo));
    TEST_ASSERT_FALSE(q.revelar);
    TEST_ASSERT_TRUE(q.mapa);
    TEST_ASSERT_EQUAL_INT(5, q.tomados);   // hora extra = p 1: todos os nos desta sessao acesos
    for (int i = 2; i < 7; ++i) TEST_ASSERT_EQUAL_FLOAT(1.0f, q.nos[kOrdem[i]].g);
}

// ---------------------------------------------------------------- revelacao
void test_revelacao_neon_por_fracao() {
    TEST_ASSERT_EQUAL_INT(7, linhasModo0(60, 0.12f));      // 7,2
    TEST_ASSERT_EQUAL_INT(60, linhasModo0(60, 1.0f));
    TEST_ASSERT_EQUAL_INT(39, linhasModo0(60, 0.12f + 0.88f * 0.6f));   // 38,9
    TEST_ASSERT_EQUAL_INT(0, linhasModo0(60, -1.0f));
    TEST_ASSERT_EQUAL_INT(60, linhasModo0(60, 3.0f));
}

void test_revelacao_cidade_por_linha_de_terminal() {
    // QG Cidade: topo 3,0 px, passo 4,8 px, 4 linhas de cabecalho, maximo 7 nos; retangulo de 64 px
    TEST_ASSERT_EQUAL_INT(22, linhasModo1(64, 30, 48, 4, 7, 0));   // 3 + 4*4,8 = 22,2
    TEST_ASSERT_EQUAL_INT(27, linhasModo1(64, 30, 48, 4, 7, 1));   // 27,0
    TEST_ASSERT_EQUAL_INT(37, linhasModo1(64, 30, 48, 4, 7, 3));   // 36,6
    TEST_ASSERT_EQUAL_INT(56, linhasModo1(64, 30, 48, 4, 7, 7));   // 55,8
    TEST_ASSERT_EQUAL_INT(56, linhasModo1(64, 30, 48, 4, 7, 12));  // nao passa de 7 linhas de nos
    TEST_ASSERT_EQUAL_INT(10, linhasModo1(10, 30, 48, 4, 7, 7));   // nunca passa do retangulo
}

void test_cursor_do_terminal_na_linha_seguinte() {
    TEST_ASSERT_EQUAL_INT(30 + 4 * 48 + 4, cursorModo1Topox10(30, 48, 4, 7, 0));   // k = 0: logo depois do cabecalho
    TEST_ASSERT_EQUAL_INT(30 + 4 * 48 + 4, cursorModo1Topox10(30, 48, 4, 7, -3));
    TEST_ASSERT_EQUAL_INT(30 + 5 * 48 + 4, cursorModo1Topox10(30, 48, 4, 7, 1));
    TEST_ASSERT_EQUAL_INT(30 + 11 * 48 + 4, cursorModo1Topox10(30, 48, 4, 7, 9));
    // o cursor fica logo abaixo do que foi revelado
    for (int k = 1; k <= 7; ++k)
        TEST_ASSERT_TRUE(cursorModo1Topox10(30, 48, 4, 7, k) >= linhasModo1(64, 30, 48, 4, 7, k) * 10 - 5);
}

// ---------------------------------------------------------------- roteiro da vitoria
void test_vitoria_so_se_a_cena_estava_na_tela() {
    Roteiro r;
    r.atualizar(true, false, true, 1000);     // focando com a cena
    r.atualizar(false, true, true, 1010);     // inimigo zerou: vitoria
    TEST_ASSERT_TRUE(r.vitoria(1010));
    r.atualizar(false, true, true, 1010 + kVitoriaMs - 10);
    TEST_ASSERT_TRUE(r.vitoria(1010 + kVitoriaMs - 10));
    r.atualizar(false, true, true, 1010 + kVitoriaMs);
    TEST_ASSERT_FALSE(r.vitoria(1010 + kVitoriaMs));   // depois: amanhecer
    r.atualizar(false, true, true, 1010 + 3 * kVitoriaMs);
    TEST_ASSERT_FALSE(r.vitoria(1010 + 3 * kVitoriaMs));

    Roteiro semCena;
    semCena.atualizar(true, false, false, 1000);   // batalha na tela no momento dos 100 %
    semCena.atualizar(false, true, false, 1010);
    semCena.atualizar(false, true, true, 12000);   // a cena so volta depois (10 s sem toque): direto o amanhecer
    TEST_ASSERT_FALSE(semCena.vitoria(12000));
}

void test_toque_na_vitoria_encerra() {
    Roteiro r;
    r.atualizar(true, false, true, 1000);
    r.atualizar(false, true, true, 1010);
    r.atualizar(false, true, false, 2000);   // acordou: a batalha esta na tela
    TEST_ASSERT_FALSE(r.vitoria(2000));
    r.atualizar(false, true, true, 13000);   // a cena voltou: amanhecer
    TEST_ASSERT_FALSE(r.vitoria(13000));
}

void test_vitoria_com_millis_dando_a_volta() {
    Roteiro r;
    const uint32_t base = 0xFFFFFFFFu - 2000u;
    r.atualizar(true, false, true, base);
    r.atualizar(false, true, true, base + 10);
    r.atualizar(false, true, true, base + 5000);   // passou de 0xFFFFFFFF
    TEST_ASSERT_TRUE(r.vitoria(base + 5000));
    r.atualizar(false, true, true, base + 10 + kVitoriaMs);
    TEST_ASSERT_FALSE(r.vitoria(base + 10 + kVitoriaMs));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_nos_novos_um_a_cada_cinco_minutos);
    RUN_TEST(test_nos_antes_do_dia_cabem_no_que_sobra);
    RUN_TEST(test_inicio_todos_os_nos_desta_sessao_apagados);
    RUN_TEST(test_meio_g_por_no_e_tomados);
    RUN_TEST(test_p_quase_um_e_fora_da_faixa);
    RUN_TEST(test_vitoria_e_hora_extra);
    RUN_TEST(test_revelacao_neon_por_fracao);
    RUN_TEST(test_revelacao_cidade_por_linha_de_terminal);
    RUN_TEST(test_cursor_do_terminal_na_linha_seguinte);
    RUN_TEST(test_vitoria_so_se_a_cena_estava_na_tela);
    RUN_TEST(test_toque_na_vitoria_encerra);
    RUN_TEST(test_vitoria_com_millis_dando_a_volta);
    return UNITY_END();
}
