// Anel suavizado (ui/AnelSuave): cobertura das bordas, pontas redondas, trilho, miolo e 2a volta. Logica pura.
#include <unity.h>

#include <cmath>

#include "ui/AnelSuave.h"
#include "ui/ReguaSuave.h"

using ui::anel::Anel;
using ui::anel::Params;
using ui::anel::Rgb;

void setUp() {}
void tearDown() {}

namespace {
    // cores ja na grade do RGB565 (multiplos de 8/4/8): o anel alinha as cores chapadas a essa grade
    const Rgb kFundo{16, 20, 16}, kC0{248, 120, 48}, kC1{248, 196, 80};

    Params base(double frac) {
        Params p;
        p.cx = 100;
        p.cy = 100;
        p.raio = 20;
        p.larg = 10;   // ro = 25, ri = 15
        p.frac = frac;
        p.c0 = kC0;
        p.c1 = kC1;
        p.fundo = kFundo;
        p.baseTrilho = kC0;
        return p;
    }

    Rgb mix(Rgb a, Rgb b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
    Rgb trilho() { return Anel::grade(mix(kFundo, kC0, 0.22f)); }

    void igual(Rgb esperado, Rgb obtido, float tol = 0.6f) {
        TEST_ASSERT_FLOAT_WITHIN(tol, esperado.r, obtido.r);
        TEST_ASSERT_FLOAT_WITHIN(tol, esperado.g, obtido.g);
        TEST_ASSERT_FLOAT_WITHIN(tol, esperado.b, obtido.b);
    }
} // namespace

// No meio da faixa, dentro do arco: cor cheia do degrade (t = 0,25 a direita -> 25 % entre c0 e c1).
void test_meio_da_faixa_cor_cheia() {
    const Anel a(base(0.5));
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(120, 100, c));
    igual(mix(kC0, kC1, 0.25f), c);
}

// Pixel exatamente na borda de fora: meio coberto -> cor intermediaria (e isso que tira a "escadinha").
void test_borda_de_fora_e_intermediaria() {
    const Anel a(base(0.5));
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(125, 100, c));
    igual(mix(mix(kFundo, trilho(), 0.5f), mix(kC0, kC1, 0.25f), 0.5f), c);
    TEST_ASSERT_TRUE(c.g > kFundo.g + 20 && c.g < 150);
}

// Longe do anel: nao toca o pixel (fica o que ja estava na tela).
void test_fora_nao_toca() {
    const Anel a(base(0.5));
    Rgb c;
    TEST_ASSERT_FALSE(a.cor(128, 100, c));
    TEST_ASSERT_FALSE(a.cor(100, 60, c));
}

// Miolo: cor de fundo (o anel apaga o que havia dentro, como o desenho anterior).
void test_miolo_e_fundo() {
    const Anel a(base(0.5));
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(100, 100, c));
    igual(kFundo, c, 0.01f);
    TEST_ASSERT_TRUE(a.raioMiolo() > 0 && a.raioMiolo() < 15);
}

// Parte nao percorrida: trilho (22 % da cor base sobre o fundo).
void test_trilho_fora_do_arco() {
    const Anel a(base(0.25));
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(80, 100, c));   // lado esquerdo, t = 0,75
    igual(trilho(), c);
}

// Ponta final redonda: um pouco depois do fim do arco, ainda dentro do semicirculo da ponta, cor do fim.
void test_ponta_final_redonda() {
    const Anel a(base(0.25));   // fim a direita (120, 100)
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(120, 103, c));
    igual(mix(kC0, kC1, 0.25f), c);
    TEST_ASSERT_TRUE(a.cor(120, 110, c));   // alem da ponta: trilho
    igual(trilho(), c, 1.0f);
}

// Ponta inicial redonda: logo a esquerda do topo (t perto de 1) ainda e c0.
void test_ponta_inicial_redonda() {
    const Anel a(base(0.25));
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(97, 80, c));
    igual(kC0, c);
}

// frac = 0: so trilho, nenhum pixel com a cor do arco.
void test_sem_progresso_so_trilho() {
    const Anel a(base(0.0));
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(100, 80, c));
    igual(trilho(), c);
}

// Degrade continuo: pixels vizinhos ao longo da faixa nunca saltam mais que um pouco (sem fatias de 4 graus).
void test_degrade_continuo() {
    Params p = base(1.0);
    p.raio = 70;
    p.larg = 15;
    const Anel a(p);
    Rgb ant;
    bool tem = false;
    for (int i = 15; i < 345; ++i) {   // fora das pontas (no topo a ponta final fica por cima)
        const float ang = (-90.0f + float(i)) * 3.14159265f / 180.0f;
        const int x = 100 + int(std::lround(70 * std::cos(ang))), y = 100 + int(std::lround(70 * std::sin(ang)));
        Rgb c;
        TEST_ASSERT_TRUE(a.cor(x, y, c));
        if (tem) TEST_ASSERT_TRUE(std::fabs(c.g - ant.g) < 2.0f);
        ant = c;
        tem = true;
    }
}

// 2a volta: a 1a fica inteira nas cores de base, a 2a vai por cima e ha sombra escura adiante da ponta.
void test_segunda_volta_com_sombra() {
    Params p = base(1.25);
    const Rgb b0{88, 88, 88}, b1{88, 88, 88};
    p.volta1a0 = &b0;
    p.volta1a1 = &b1;
    const Anel a(p);
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(80, 100, c));   // t = 0,75: so a 1a volta (cinza)
    igual(b0, c);
    TEST_ASSERT_TRUE(a.cor(120, 100, c));  // t = 0,25 = ponta da 2a volta (cor final c1)
    igual(kC1, c, 1.0f);
    const float ang = (-90.0f + 90.0f + 20.0f) * 3.14159265f / 180.0f;   // depois da ponta redonda: sombra escura
    TEST_ASSERT_TRUE(a.cor(100 + int(std::lround(20 * std::cos(ang))), 100 + int(std::lround(20 * std::sin(ang))), c));
    igual(Anel::grade(mix(kC1, Rgb{0, 0, 0}, 0.6f)), c, 1.5f);   // cor final escurecida 60 %
}

// Anel completo (frac = 1): a ponta final (cor c1) fica por cima do inicio, logo a direita do topo.
void test_anel_cheio_ponta_final_por_cima() {
    const Anel a(base(1.0));
    Rgb c;
    TEST_ASSERT_TRUE(a.cor(102, 80, c));   // t ~ 0,016: sem a regra seria ~c0; com a ponta por cima e c1
    igual(kC1, c, 1.0f);
}

// Pontilhado: cor na grade sai exata em qualquer posicao; cor entre dois niveis alterna entre os dois vizinhos.
void test_pontilhado() {
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) TEST_ASSERT_EQUAL_HEX16(Anel::para565(kC0), Anel::para565(kC0, x, y));
    int altos = 0;
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            const uint16_t v = Anel::para565(Rgb{100, 0, 0}, x, y);   // 100 = 12,5 niveis de 8
            TEST_ASSERT_TRUE(v == (12 << 11) || v == (13 << 11));
            if (v == (13 << 11)) ++altos;
        }
    TEST_ASSERT_EQUAL_INT(8, altos);   // metade alta, metade baixa
}

void test_para565() {
    TEST_ASSERT_EQUAL_HEX16(0xFFFF, Anel::para565({255, 255, 255}));
    TEST_ASSERT_EQUAL_HEX16(0x0000, Anel::para565({0, 0, 0}));
    TEST_ASSERT_EQUAL_HEX16(0xF800, Anel::para565({255, 0, 0}));
}


// ---- ui/ReguaSuave: retangulo arredondado, marca, agulha ------------------------------------------------------
void test_regua_retangulo_cobertura() {
    namespace rg = ui::regua;
    // interior = 1, fora = 0, canto cortado = parcial e simetrico
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, rg::coberturaRetanguloArredondado(50.5f, 50.5f, 10, 10, 100, 60, 16));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rg::coberturaRetanguloArredondado(5.5f, 5.5f, 10, 10, 100, 60, 16));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rg::coberturaRetanguloArredondado(10.5f, 10.5f, 10, 10, 100, 60, 16));   // ponta do canto
    const float a = rg::coberturaRetanguloArredondado(14.5f, 14.5f, 10, 10, 100, 60, 16);
    const float b = rg::coberturaRetanguloArredondado(105.5f, 65.5f, 10, 10, 100, 60, 16);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, a, b);
    TEST_ASSERT_TRUE(a > 0.0f && a < 1.0f);
}

void test_regua_retangulo_pinta_cantos_suaves() {
    namespace rg = ui::regua;
    const Rgb fundo{16, 20, 16}, fill{40, 36, 32}, borda{248, 120, 48};
    int parciais = 0, cheios = 0, cantoOrigem = -1;
    uint16_t retas = 0;
    rg::retanguloSuave([&](int x, int y, uint16_t c) {
                           if (x == 0 && y == 0) cantoOrigem = c;
                           if (c != ui::anel::Anel::para565(ui::anel::Anel::grade(fundo)) && c != ui::anel::Anel::para565(ui::anel::Anel::grade(fill))) ++parciais; else ++cheios;
                       },
                       [&](int, int, int w, int h, uint16_t c) { retas = c; (void)w; (void)h; },
                       0, 0, 60, 40, 16, fill, fundo);
    TEST_ASSERT_TRUE(parciais > 8);     // bordas do canto suavizadas (tons intermediarios)
    TEST_ASSERT_EQUAL_INT(ui::anel::Anel::para565(ui::anel::Anel::grade(fundo)), cantoOrigem);   // ponta do canto = fundo
    TEST_ASSERT_EQUAL_UINT16(ui::anel::Anel::para565(ui::anel::Anel::grade(fill)), retas);
    (void)cheios; (void)borda;
}

void test_regua_marca_e_agulha_cobertura() {
    namespace rg = ui::regua;
    // traco de 3 px centrado em 100: pixels 99 e 101 com 1/2... (a faixa [98.5, 101.5])
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, rg::coberturaFaixa(98.5f, 101.5f, 98));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, rg::coberturaFaixa(98.5f, 101.5f, 99));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, rg::coberturaFaixa(98.5f, 101.5f, 100));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, rg::coberturaFaixa(98.5f, 101.5f, 101));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rg::coberturaFaixa(98.5f, 101.5f, 102));
    // triangulo da agulha: meio = 1, bordas inclinadas = parcial, fora = 0
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, rg::coberturaTriangulo(100, 44, 93, 44, 107, 44, 100, 52));
    const float lado = rg::coberturaTriangulo(94, 45, 93, 44, 107, 44, 100, 52);
    TEST_ASSERT_TRUE(lado > 0.0f && lado < 1.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, rg::coberturaTriangulo(80, 44, 93, 44, 107, 44, 100, 52));
    // brilho: soma da gaussiana na faixa central e maior que nas pontas
    TEST_ASSERT_TRUE(rg::gaussFaixa(-4.5f, 4.5f, 4.0f) > rg::gaussFaixa(8.0f, 17.0f, 4.0f));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.0f, rg::gaussFaixa(-100.0f, 100.0f, 4.0f));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_regua_retangulo_cobertura);
    RUN_TEST(test_regua_retangulo_pinta_cantos_suaves);
    RUN_TEST(test_regua_marca_e_agulha_cobertura);
    RUN_TEST(test_meio_da_faixa_cor_cheia);
    RUN_TEST(test_borda_de_fora_e_intermediaria);
    RUN_TEST(test_fora_nao_toca);
    RUN_TEST(test_miolo_e_fundo);
    RUN_TEST(test_trilho_fora_do_arco);
    RUN_TEST(test_ponta_final_redonda);
    RUN_TEST(test_ponta_inicial_redonda);
    RUN_TEST(test_sem_progresso_so_trilho);
    RUN_TEST(test_degrade_continuo);
    RUN_TEST(test_segunda_volta_com_sombra);
    RUN_TEST(test_anel_cheio_ponta_final_por_cima);
    RUN_TEST(test_pontilhado);
    RUN_TEST(test_para565);
    return UNITY_END();
}
