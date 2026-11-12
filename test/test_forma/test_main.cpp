// ui/FormaSuave: retangulo arredondado (cheio, borda, contorno), disco, anel, triangulo e traco de progresso no perimetro.
// Logica pura (sem tela): cada teste coleta os pixels gravados.
#include <unity.h>

#include <cmath>
#include <map>
#include <utility>

#include "ui/FimEnche.h"
#include "ui/FormaSuave.h"

namespace f = ui::forma;
using f::Rgb;

void setUp() {}
void tearDown() {}

namespace {
    const Rgb kFundo{16, 20, 16}, kFill{40, 36, 32}, kBorda{248, 120, 48}, kTraco{248, 120, 48};

    struct Tela {
        std::map<std::pair<int, int>, uint16_t> px;
        void pixel(int x, int y, uint16_t c) { px[{x, y}] = c; }
        void rect(int x, int y, int w, int h, uint16_t c) {
            for (int j = 0; j < h; ++j)
                for (int i = 0; i < w; ++i) px[{x + i, y + j}] = c;
        }
        bool tem(int x, int y) const { return px.count({x, y}) != 0; }
        uint16_t at(int x, int y) const { return px.at({x, y}); }
    };
    uint16_t c565(const Rgb& c) { return f::chapada565(c); }
    // pixel "intermediario" = nem fundo nem cor cheia
    bool parcial(uint16_t c, uint16_t a, uint16_t b) { return c != a && c != b; }
}

void test_cobertura_canto_fora_dentro_borda() {
    // retangulo (10,10) 100x60 r16: ponta do canto 0 %, interior 100 %, e a borda do arco ~50 %
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.0f, f::coberturaRetanguloArredondado(10.5f, 10.5f, 10, 10, 100, 60, 16));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, f::coberturaRetanguloArredondado(60.5f, 40.5f, 10, 10, 100, 60, 16));
    // ponto exatamente sobre o arco (a 45 graus do centro do canto, distancia r): cobertura 0,5
    const float cx = 10 + 16, cy = 10 + 16, d = 16.0f / std::sqrt(2.0f);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, f::coberturaRetanguloArredondado(cx - d, cy - d, 10, 10, 100, 60, 16));
    // reta: pixel de centro na aresta = 0,5 ; meio pixel pra dentro = 1
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.5f, f::coberturaRetanguloArredondado(60.0f, 10.0f, 10, 10, 100, 60, 16));
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, f::coberturaRetanguloArredondado(60.0f, 10.5f, 10, 10, 100, 60, 16));
}

void test_cheio_cantos_suaves_e_miolo_rapido() {
    Tela t;
    int rects = 0;
    f::retanguloSuave([&](int x, int y, uint16_t c) { t.pixel(x, y, c); },
                      [&](int x, int y, int w, int h, uint16_t c) { ++rects; t.rect(x, y, w, h, c); }, 0, 0, 60, 40, 12, kFill, kFundo);
    TEST_ASSERT_EQUAL_UINT16(c565(kFundo), t.at(0, 0));        // ponta do canto = fundo
    TEST_ASSERT_EQUAL_UINT16(c565(kFill), t.at(30, 20));       // miolo cheio
    TEST_ASSERT_EQUAL_UINT16(c565(kFill), t.at(30, 0));        // reta cheia
    int parciais = 0;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x) parciais += parcial(t.at(x, y), c565(kFundo), c565(kFill));
    TEST_ASSERT_TRUE(parciais >= 8);   // degrau substituido por tons intermediarios
    TEST_ASSERT_TRUE(rects <= 6);      // o miolo e fillRect, nao pixel a pixel
    // simetria dos 4 cantos
    TEST_ASSERT_EQUAL_UINT16(t.at(1, 1), t.at(58, 1));
    TEST_ASSERT_EQUAL_UINT16(t.at(1, 1), t.at(1, 38));
}

void test_miolo_fora_da_grade_sem_pontilhado() {
    // cor fora da grade do RGB565: todo pixel 100 % dentro (inclusive nos cantos) sai igual, sem manchas de pontilhado
    const Rgb fora{230, 230, 222};
    Tela t;
    f::retanguloSuave([&](int x, int y, uint16_t c) { t.pixel(x, y, c); },
                      [&](int x, int y, int w, int h, uint16_t c) { t.rect(x, y, w, h, c); }, 0, 0, 60, 40, 12, fora, kFundo);
    for (int y = 6; y < 12; ++y)
        for (int x = 6; x < 12; ++x) TEST_ASSERT_EQUAL_UINT16(c565(fora), t.at(x, y));
}

void test_contorno_so_toca_a_faixa() {
    Tela t;
    f::contornoSuave([&](int x, int y, uint16_t c) { t.pixel(x, y, c); },
                     [&](int x, int y, int w, int h, uint16_t c) { t.rect(x, y, w, h, c); }, 0, 0, 60, 40, 12, 2, kBorda, kFundo, kFill);
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), t.at(30, 0));
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), t.at(30, 1));
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), t.at(0, 20));
    TEST_ASSERT_FALSE(t.tem(30, 2));    // miolo intocado
    TEST_ASSERT_FALSE(t.tem(30, 20));
    TEST_ASSERT_FALSE(t.tem(0, 0));     // canto fora do arco: nao toca
    int parciais = 0;
    for (int y = 0; y < 12; ++y)
        for (int x = 0; x < 12; ++x)
            if (t.tem(x, y) && parcial(t.at(x, y), c565(kBorda), c565(kFundo))) ++parciais;
    TEST_ASSERT_TRUE(parciais >= 8);
}

void test_disco_e_anel() {
    Tela t;
    f::discoSuave([&](int x, int y, uint16_t c) { t.pixel(x, y, c); },
                  [&](int x, int y, int w, uint16_t c) { t.rect(x, y, w, 1, c); }, 50, 50, 10, kBorda, kFundo);
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), t.at(50, 50));
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), t.at(60, 50));    // diametro 2r+1, como fillCircle
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), t.at(40, 50));
    TEST_ASSERT_TRUE(!t.tem(50, 39) || t.at(50, 39) != c565(kBorda));
    int parciais = 0;
    for (const auto& [k, v] : t.px) parciais += parcial(v, c565(kFundo), c565(kBorda));
    TEST_ASSERT_TRUE(parciais >= 12);
    // anel: o miolo nao e tocado e a faixa esta em r
    Tela a;
    f::aneloSuave([&](int x, int y, uint16_t c) { a.pixel(x, y, c); }, 50, 50, 10, 1, kBorda, kFundo, kFill);
    TEST_ASSERT_FALSE(a.tem(50, 50));
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), a.at(60, 50));
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), a.at(50, 40));
}

void test_triangulo() {
    Tela t;
    f::trianguloSuave([&](int x, int y, uint16_t c) { t.pixel(x, y, c); }, 10, 10, 30, 10, 20, 30, kBorda, kFundo);
    TEST_ASSERT_EQUAL_UINT16(c565(kBorda), t.at(20, 14));
    int parciais = 0;
    for (const auto& [k, v] : t.px) parciais += parcial(v, c565(kFundo), c565(kBorda));
    TEST_ASSERT_TRUE(parciais >= 10);
}

void test_perimetro_geometria() {
    // 100x40, r 10, faixa 3: perimetro = 4a + 4b + 2 pi rp; rp = 8,5 ; a = 50-1,5-8,5 = 40 ; b = 20-1,5-8,5 = 10
    const f::Perimetro p(0, 0, 100, 40, 10, 3);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 160.0f + 40.0f + 2 * 3.14159265f * 8.5f, p.total);
    float x, y;
    p.ponto(0, x, y);      // meio do topo, centro da faixa (1,5 px por dentro)
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, y);
    p.ponto(p.total * 0.5f, x, y);   // metade: meio da base
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 38.5f, y);
    p.ponto(p.total, x, y);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 50.0f, x);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1.5f, y);
    // projecao: ponto da linha central recupera o proprio s
    for (float s : {5.0f, 45.0f, 60.0f, 90.0f, 130.0f, 200.0f, 240.0f}) {
        p.ponto(s, x, y);
        float dp, sp;
        p.projeta(x, y, dp, sp);
        TEST_ASSERT_FLOAT_WITHIN(0.05f, 0.0f, dp);
        TEST_ASSERT_FLOAT_WITHIN(0.1f, s, sp);
    }
}

void test_progresso_perimetro_0_meio_1() {
    const auto pinta = [&](double frac) {
        Tela t;
        f::tracoPerimetro([&](int x, int y, uint16_t c) { t.pixel(x, y, c); }, 0, 0, 100, 40, 10, 3, frac, kTraco, kFundo, kFill);
        return t;
    };
    const uint16_t laranja = c565(kTraco);
    auto cheios = [&](const Tela& t) { int n = 0; for (const auto& [k, v] : t.px) n += v == laranja; return n; };
    // 0: nada
    TEST_ASSERT_EQUAL_INT(0, (int)pinta(0.0).px.size());
    // 0,5: do meio do topo ate o meio da base, sentido horario: lado direito pintado, esquerdo nao
    const Tela m = pinta(0.5);
    TEST_ASSERT_TRUE(m.tem(75, 1) && m.at(75, 1) == laranja);      // topo, lado direito
    TEST_ASSERT_TRUE(m.tem(98, 20) && m.at(98, 20) == laranja);    // lado direito
    TEST_ASSERT_TRUE(m.tem(75, 38) && m.at(75, 38) == laranja);    // base, lado direito
    TEST_ASSERT_FALSE(m.tem(25, 1) && m.at(25, 1) == laranja);     // topo esquerdo vazio
    TEST_ASSERT_FALSE(m.tem(1, 20) && m.at(1, 20) == laranja);     // lado esquerdo vazio
    // 1: anel inteiro
    const Tela t1 = pinta(1.0);
    TEST_ASSERT_TRUE(t1.tem(25, 1) && t1.at(25, 1) == laranja);
    TEST_ASSERT_TRUE(t1.tem(1, 20) && t1.at(1, 20) == laranja);
    TEST_ASSERT_TRUE(t1.tem(50, 38) && t1.at(50, 38) == laranja);
    TEST_ASSERT_TRUE(cheios(t1) > cheios(m) && cheios(m) > 0);
    // fundo dos cantos nao e tocado, e a borda do canto tem tons intermediarios (sem degrau)
    int parciais = 0;
    for (const auto& [k, v] : t1.px) parciais += parcial(v, laranja, c565(kFundo)) && parcial(v, laranja, c565(kFill));
    TEST_ASSERT_TRUE(parciais >= 16);
    TEST_ASSERT_FALSE(t1.tem(0, 0) && t1.at(0, 0) == laranja);
    // pontas redondas: um pouco de traco passa do inicio (meio do topo, lado esquerdo = cap inicial)
    const Tela curto = pinta(0.2);
    TEST_ASSERT_TRUE(curto.tem(50, 1) && curto.at(50, 1) == laranja);
    TEST_ASSERT_TRUE(curto.tem(49, 1) && curto.at(49, 1) != c565(kFundo));   // cap redondo atras do inicio
}

void test_progresso_monotono() {
    int anterior = -1;
    for (double fr : {0.1, 0.25, 0.4, 0.6, 0.75, 0.9, 1.0}) {
        Tela t;
        f::tracoPerimetro([&](int x, int y, uint16_t c) { t.pixel(x, y, c); }, 0, 0, 90, 36, 12, 3, fr, kTraco, kFundo, kFill);
        int n = 0;
        for (const auto& [k, v] : t.px) n += v == c565(kTraco);
        TEST_ASSERT_TRUE(n > anterior);
        anterior = n;
    }
}

void test_fim_enche_nivel_e_cor_do_texto() {
    namespace fim = ui::fim;
    // botao y=100, h=56: 0 % = nada laranja; 50 % = topo na linha 128; 100 % = encerrando
    const auto n0 = fim::nivel(100, 56, 0.0);
    TEST_ASSERT_EQUAL_INT(156, n0.linha);
    TEST_ASSERT_FALSE(n0.encerrando);
    for (int y = 100; y < 156; ++y) TEST_ASSERT_FALSE(fim::textoEscuro(y, n0));   // tudo claro: nada de laranja
    const auto n50 = fim::nivel(100, 56, 0.5);
    TEST_ASSERT_EQUAL_INT(128, n50.linha);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, n50.cobertura);
    TEST_ASSERT_FALSE(fim::textoEscuro(127, n50));   // acima do nivel: base, texto claro
    TEST_ASSERT_TRUE(fim::textoEscuro(128, n50));    // linha do nivel (100 % laranja): escuro
    TEST_ASSERT_TRUE(fim::textoEscuro(155, n50));
    // nivel no meio de uma linha: cobertura de sub-pixel e troca de cor a partir de 50 %
    const auto n30 = fim::nivel(100, 56, 0.3);   // topo em 156 - 16,8 = 139,2
    TEST_ASSERT_EQUAL_INT(139, n30.linha);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.8f, n30.cobertura);
    TEST_ASSERT_EQUAL(int(fim::Camada::Transicao), int(fim::camadaDaLinha(139, n30)));
    TEST_ASSERT_TRUE(fim::textoEscuro(139, n30));
    const auto n34 = fim::nivel(100, 56, 0.34);  // topo em 136,96 -> 4 % da linha 136 fica de fora
    TEST_ASSERT_EQUAL_INT(136, n34.linha);
    const auto n60 = fim::nivel(100, 56, 0.6);   // topo em 122,4: so 60 % da linha 122 e laranja... 
    TEST_ASSERT_EQUAL_INT(122, n60.linha);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, n60.cobertura);
    // crescimento monotono do laranja
    int anterior = 1000;
    for (double p : {0.0, 0.2, 0.4, 0.6, 0.8, 1.0}) {
        const auto n = fim::nivel(100, 56, p);
        TEST_ASSERT_TRUE(n.linha <= anterior);
        anterior = n.linha;
    }
    // 100 %: encerrando, todas as linhas escuras (botao todo laranja)
    const auto n1 = fim::nivel(100, 56, 1.0);
    TEST_ASSERT_TRUE(n1.encerrando);
    for (int y = 100; y < 156; ++y) TEST_ASSERT_TRUE(fim::textoEscuro(y, n1));
}

void test_fim_encerrando_dura_450ms_e_se_descarta_se_esquecido() {
    namespace fim = ui::fim;
    unsigned quando = 0;
    TEST_ASSERT_FALSE(fim::agendado(quando));
    TEST_ASSERT_FALSE(fim::hora(1000, quando));            // nada agendado: nunca encerra
    quando = fim::agendar(1000);
    TEST_ASSERT_TRUE(fim::agendado(quando));
    TEST_ASSERT_EQUAL_UINT(1450, quando);
    TEST_ASSERT_FALSE(fim::hora(1000, quando));
    TEST_ASSERT_FALSE(fim::hora(1449, quando));            // ainda mostrando "encerrando"
    TEST_ASSERT_TRUE(fim::hora(1450, quando));             // 450 ms depois: encerra
    TEST_ASSERT_TRUE(fim::hora(1600, quando));
    TEST_ASSERT_FALSE(fim::vencido(1600, quando));
    TEST_ASSERT_TRUE(fim::vencido(1450 + fim::kVencidoMs + 1, quando));   // esquecido (saiu da batalha): descarta
    const unsigned perto = 0xFFFFFFFFu - 100;               // virada do contador de ms
    const unsigned q2 = fim::agendar(perto);
    TEST_ASSERT_FALSE(fim::hora(perto + 400, q2));
    TEST_ASSERT_TRUE(fim::hora(perto + 450, q2));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_cobertura_canto_fora_dentro_borda);
    RUN_TEST(test_cheio_cantos_suaves_e_miolo_rapido);
    RUN_TEST(test_miolo_fora_da_grade_sem_pontilhado);
    RUN_TEST(test_contorno_so_toca_a_faixa);
    RUN_TEST(test_disco_e_anel);
    RUN_TEST(test_triangulo);
    RUN_TEST(test_perimetro_geometria);
    RUN_TEST(test_progresso_perimetro_0_meio_1);
    RUN_TEST(test_progresso_monotono);
    RUN_TEST(test_fim_enche_nivel_e_cor_do_texto);
    RUN_TEST(test_fim_encerrando_dura_450ms_e_se_descarta_se_esquecido);
    return UNITY_END();
}
