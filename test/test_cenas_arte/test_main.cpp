// Arte gerada das cenas do Modo Foco (src/ui/cenas/CenasFoco.generated.*, tools/gerar_cenas_foco.py): confere que as
// tabelas das animacoes por diferenca e do rolo sao coerentes. O compositor (CenaFoco) le essas tabelas sem conferir
// limites; uma tabela errada viraria leitura fora da memoria na placa.

#include <unity.h>

#include <cstdint>

#include "tempo/CenaViva.h"
#include "ui/cenas/CenasFoco.generated.h"

using namespace ui::cenas;

namespace {

    constexpr int kLarg = 640, kAlt = 172;

    // Devolve quantos pixels a animacao tem (0 = nao existe neste fundo).
    uint32_t conferir(const Animacao& a) {
        if (a.nPoses == 0) return 0;
        const int w = a.r.x1 - a.r.x0, h = a.r.y1 - a.r.y0;
        TEST_ASSERT_TRUE(w > 0 && h > 0);
        TEST_ASSERT_TRUE(a.r.x0 >= 0 && a.r.y0 >= 0 && a.r.x1 <= kLarg && a.r.y1 <= kAlt);
        TEST_ASSERT_TRUE(a.periodo > 0);
        TEST_ASSERT_NOT_NULL(a.mapa);
        TEST_ASSERT_NOT_NULL(a.inicio);
        TEST_ASSERT_NOT_NULL(a.inicioPx);
        bool usada[255] = {};
        for (int q = 0; q < a.periodo; ++q) {
            TEST_ASSERT_TRUE(a.mapa[q] == tempo::cenaviva::kSemPose || a.mapa[q] < a.nPoses);
            if (a.mapa[q] < a.nPoses) usada[a.mapa[q]] = true;
        }
        TEST_ASSERT_EQUAL_UINT32(0, a.inicio[0]);
        TEST_ASSERT_EQUAL_UINT32(0, a.inicioPx[0]);
        uint32_t pixels = 0;
        for (int p = 0; p < a.nPoses; ++p) {
            TEST_ASSERT_TRUE(usada[p]);                              // nenhuma pose sobrando
            TEST_ASSERT_TRUE(a.inicio[p] < a.inicio[p + 1]);         // nenhuma pose vazia
            TEST_ASSERT_EQUAL_UINT32(pixels, a.inicioPx[p]);
            for (uint32_t k = a.inicio[p]; k < a.inicio[p + 1]; ++k) {
                const int x = a.pos[k] % w, y = a.pos[k] / w;
                TEST_ASSERT_TRUE(a.n[k] > 0);
                TEST_ASSERT_TRUE(y < h && x + a.n[k] <= w);          // a corrida fica numa linha so, dentro da caixa
                for (int i = 0; i < a.n[k] * 3; ++i) {               // todo pixel listado muda alguma coisa de verdade
                    const int d = a.delta[(pixels) * 3 + i];
                    TEST_ASSERT_TRUE(d >= -127 && d <= 127);
                }
                pixels += a.n[k];
            }
        }
        TEST_ASSERT_EQUAL_UINT32(pixels, a.inicioPx[a.nPoses]);
        return pixels;
    }

    void conferirCena(const Cena& c) {
        TEST_ASSERT_NOT_NULL(c.viva);
        for (int f = 0; f < 3; ++f) {
            conferir(c.viva->respira[f]);
            conferir(c.viva->tubo[f]);
            conferir(c.viva->ondas[f]);
            conferir(c.viva->cursorRolo[f]);
        }
        const Rolo& r = c.viva->rolo;
        if (r.quadrosPorPasso == 0) return;
        const int h = r.r.y1 - r.r.y0;
        TEST_ASSERT_TRUE(r.passo > 0 && h > 0 && h % r.passo == 0);  // faixa ciclica: numero inteiro de linhas de codigo
        TEST_ASSERT_TRUE(r.r.x0 >= 0 && r.r.y0 >= 0 && r.r.x1 <= kLarg && r.r.y1 <= kAlt && r.r.x1 > r.r.x0);
        for (int f = 0; f < 3; ++f) TEST_ASSERT_NOT_NULL(r.px[f]);
    }

} // namespace

void setUp() {}
void tearDown() {}

void test_tabelas_do_qg_neon() { conferirCena(kQgNeon); }
void test_tabelas_do_qg_cidade() { conferirCena(kQgCidade); }

void test_qg_neon_respira_e_as_telas_mexem() {
    const CenaViva& v = *kQgNeon.viva;
    TEST_ASSERT_TRUE(v.respira[0].nPoses > 1);        // noite
    TEST_ASSERT_EQUAL_UINT8(0, v.respira[1].nPoses);  // na vitoria o personagem esta recostado: nao respira
    TEST_ASSERT_TRUE(v.respira[2].nPoses > 1);        // hora extra
    for (int f = 0; f < 3; ++f) {
        TEST_ASSERT_EQUAL_UINT8(24, v.respira[f].nPoses ? v.respira[f].periodo : 24);
        TEST_ASSERT_EQUAL_UINT8(3, v.tubo[f].nPoses);     // 4 telas de hex; a do fundo nao precisa de diferenca
        TEST_ASSERT_EQUAL_UINT8(24, v.tubo[f].periodo);
        TEST_ASSERT_EQUAL_UINT8(24, v.ondas[f].nPoses);   // a onda muda em todo quadro da fase
        TEST_ASSERT_EQUAL_UINT8(24, v.ondas[f].periodo);
        int semPose = 0;
        for (int q = 0; q < 24; ++q) semPose += v.tubo[f].mapa[q] == tempo::cenaviva::kSemPose ? 1 : 0;
        TEST_ASSERT_EQUAL_INT(6, semPose);                // cada tela de hex fica 6 quadros (0,75 s)
        TEST_ASSERT_EQUAL_UINT8(0, v.cursorRolo[f].nPoses);
    }
    TEST_ASSERT_EQUAL_UINT8(0, v.rolo.quadrosPorPasso);   // o QG Neon nao tem tela que rola
    TEST_ASSERT_EQUAL_INT16(-1, v.cursorHoraExtraTopox10);
}

void test_qg_cidade_respira_rola_e_pisca() {
    const CenaViva& v = *kQgCidade.viva;
    TEST_ASSERT_TRUE(v.respira[0].nPoses > 1);
    TEST_ASSERT_EQUAL_UINT8(0, v.respira[1].nPoses);
    TEST_ASSERT_TRUE(v.respira[2].nPoses > 1);
    TEST_ASSERT_EQUAL_UINT8(3, v.rolo.quadrosPorPasso);                       // 1 linha de codigo a cada 375 ms
    TEST_ASSERT_EQUAL_UINT8(3, v.rolo.passo);
    TEST_ASSERT_EQUAL_INT(23, (v.rolo.r.y1 - v.rolo.r.y0) / v.rolo.passo);    // 23 linhas de codigo no ciclo
    for (int f = 0; f < 3; ++f) {
        TEST_ASSERT_EQUAL_UINT8(0, v.tubo[f].nPoses);
        TEST_ASSERT_EQUAL_UINT8(0, v.ondas[f].nPoses);
        const Animacao& c = v.cursorRolo[f];                                   // cursor do monitor em pe: 1 pose, pisca 1x/s
        TEST_ASSERT_EQUAL_UINT8(1, c.nPoses);
        TEST_ASSERT_EQUAL_UINT8(8, c.periodo);
        for (int q = 0; q < 8; ++q) TEST_ASSERT_EQUAL_UINT8(q < 4 ? 0 : tempo::cenaviva::kSemPose, c.mapa[q]);
        // o cursor fica dentro da faixa que rola (ou encostado nela): e desenhado por cima do rolo
        TEST_ASSERT_TRUE(c.r.x0 >= v.rolo.r.x0 - 8 && c.r.x1 <= v.rolo.r.x1 + 8);
    }
    // cursor do terminal na hora extra: dentro do retangulo da revelacao
    const int alt = kQgCidade.revela.y1 - kQgCidade.revela.y0;
    TEST_ASSERT_TRUE(v.cursorHoraExtraTopox10 > 0 && v.cursorHoraExtraTopox10 + 32 <= alt * 10);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_tabelas_do_qg_neon);
    RUN_TEST(test_tabelas_do_qg_cidade);
    RUN_TEST(test_qg_neon_respira_e_as_telas_mexem);
    RUN_TEST(test_qg_cidade_respira_rola_e_pisca);
    return UNITY_END();
}
