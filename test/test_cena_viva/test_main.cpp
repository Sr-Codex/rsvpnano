// Modo Foco v2 (tempo/CenaViva): relogio da cena (texto, estado e barra em foco, vitoria, hora extra e livre), mapa dos
// digitos de 7 segmentos e as animacoes da cena viva (deterministicas por quadro; piscadas e falhas do neon nos quadros
// esperados; drone e carros dentro da janela). Logica pura (o desenho fica no renderizador).
#include <unity.h>

#include <cmath>
#include <cstring>

#include "tempo/CenaViva.h"

using namespace tempo::cenaviva;

void setUp() {}
void tearDown() {}

namespace {
    EntradaRelogio foco(int64_t restanteMs, uint64_t focadoMs, int64_t planoMs) {
        EntradaRelogio e;
        e.restanteMs = restanteMs;
        e.focadoMs = focadoMs;
        e.planoMs = planoMs;
        return e;
    }
} // namespace

// ---------------------------------------------------------------- relogio
void test_relogio_foco_mostra_o_restante_arredondado_para_cima() {
    const auto r = relogio(foco(1420500, 79500, 1500000));   // 23:40,5 restantes de 25 min
    TEST_ASSERT_EQUAL_STRING("23:41", r.texto.data());
    TEST_ASSERT_EQUAL(static_cast<int>(EstadoRelogio::Foco), static_cast<int>(r.estado));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 79500.0f / 1500000.0f, r.fracao);
    TEST_ASSERT_EQUAL_STRING("00:01", relogio(foco(1, 1499999, 1500000)).texto.data());
    TEST_ASSERT_EQUAL_STRING("05:00", relogio(foco(300000, 0, 300000)).texto.data());   // 2 digitos nos minutos
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, relogio(foco(300000, 0, 300000)).fracao);
}

void test_relogio_foco_de_uma_hora_ou_mais() {
    const auto r = relogio(foco(3725000, 0, 5 * 3600000LL));
    TEST_ASSERT_EQUAL_STRING("1:02:05", r.texto.data());
    TEST_ASSERT_EQUAL_STRING("5:00:00", relogio(foco(5 * 3600000LL, 0, 5 * 3600000LL)).texto.data());
}

void test_relogio_vitoria_e_zero_com_barra_cheia() {
    auto e = foco(0, 1500000, 1500000);
    e.horaExtra = true;
    e.vitoria = true;
    e.overtimeMs = 4000;
    const auto r = relogio(e);
    TEST_ASSERT_EQUAL_STRING("00:00", r.texto.data());
    TEST_ASSERT_EQUAL(static_cast<int>(EstadoRelogio::Vitoria), static_cast<int>(r.estado));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, r.fracao);
}

void test_relogio_hora_extra_conta_para_cima_com_mais() {
    auto e = foco(0, 1692000, 1500000);
    e.horaExtra = true;
    e.overtimeMs = 192000;
    const auto r = relogio(e);
    TEST_ASSERT_EQUAL_STRING("+3:12", r.texto.data());
    TEST_ASSERT_EQUAL(static_cast<int>(EstadoRelogio::HoraExtra), static_cast<int>(r.estado));
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, r.fracao);
    e.overtimeMs = 3725999;
    TEST_ASSERT_EQUAL_STRING("+1:02:05", relogio(e).texto.data());
}

void test_relogio_livre_mostra_o_decorrido_e_a_hora_atual() {
    EntradaRelogio e;
    e.livre = true;
    e.focadoMs = 785400;   // 13:05
    e.restanteMs = 999999;  // ignorado no livre
    auto r = relogio(e);
    TEST_ASSERT_EQUAL_STRING("13:05", r.texto.data());
    TEST_ASSERT_EQUAL(static_cast<int>(EstadoRelogio::Livre), static_cast<int>(r.estado));
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 785400.0f / 3600000.0f, r.fracao);
    e.focadoMs = 3900000;   // 1:05:00 -> 5 min dentro da 2a hora
    r = relogio(e);
    TEST_ASSERT_EQUAL_STRING("1:05:00", r.texto.data());
    TEST_ASSERT_FLOAT_WITHIN(1e-5f, 300000.0f / 3600000.0f, r.fracao);
}

void test_relogio_valores_fora_da_faixa() {
    TEST_ASSERT_EQUAL_STRING("00:00", relogio(foco(-5000, 0, 1500000)).texto.data());
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 1.0f, relogio(foco(0, 9000000, 1500000)).fracao);   // barra nunca passa de cheia
    TEST_ASSERT_FLOAT_WITHIN(1e-6f, 0.0f, relogio(foco(0, 1000, 0)).fracao);           // sem plano: barra vazia
    std::array<char, 12> s{};
    formatar(100LL * 3600, true, s);   // horas limitadas a 2 digitos: sempre cabe
    TEST_ASSERT_EQUAL_STRING("+99:00:00", s.data());
}

// ---------------------------------------------------------------- 7 segmentos
void test_mapa_de_segmentos_0_a_9() {
    // bits: a=1 b=2 c=4 d=8 e=16 f=32 g=64
    const uint8_t esperado[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
    for (int d = 0; d < 10; ++d) TEST_ASSERT_EQUAL_HEX8(esperado[d], segmentos(static_cast<char>('0' + d)));
    TEST_ASSERT_EQUAL_HEX8(0, segmentos(':'));
    TEST_ASSERT_EQUAL_HEX8(0, segmentos('+'));
    TEST_ASSERT_EQUAL_HEX8(0, segmentos(' '));
    // contagem de segmentos acesos de cada digito (o "8" acende todos, o "1" so dois)
    const int acesos[10] = {6, 2, 5, 5, 4, 5, 6, 3, 7, 6};
    for (int d = 0; d < 10; ++d) {
        int n = 0;
        for (int k = 0; k < 7; ++k) n += (segmentos(static_cast<char>('0' + d)) >> k) & 1;
        TEST_ASSERT_EQUAL_INT(acesos[d], n);
    }
}

// ---------------------------------------------------------------- animacoes
void test_quadro_e_sprite_a_8_por_segundo() {
    TEST_ASSERT_EQUAL_UINT32(0, quadro(124));
    TEST_ASSERT_EQUAL_UINT32(1, quadro(125));
    TEST_ASSERT_EQUAL_UINT32(8, quadro(1000));
    TEST_ASSERT_EQUAL_INT(0, quadroSprite(3000));   // 24 quadros = 1 fase = 3 s
    TEST_ASSERT_EQUAL_INT(23, quadroSprite(2999));
    TEST_ASSERT_EQUAL_INT(5, quadroSprite(3000 + 5 * 125));
}

void test_gotas_deterministicas_e_dentro_da_janela() {
    const float alt = 88.0f;
    for (uint32_t ms = 0; ms < 200000; ms += 997) {
        const float y = gotaY(40.0f, 40.0f, ms, alt);
        TEST_ASSERT_TRUE(y >= 0.0f && y < alt);
        TEST_ASSERT_EQUAL_FLOAT(y, gotaY(40.0f, 40.0f, ms, alt));   // mesmo quadro, mesma posicao
    }
    // 1 fase depois a gota desceu `vel` px (mod altura), como nos mockups
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, std::fmod(40.0f + 40.0f, alt), gotaY(40.0f, 40.0f, 3000, alt));
    TEST_ASSERT_FLOAT_WITHIN(1e-3f, 40.0f, gotaY(40.0f, 40.0f, 0, alt));
    // continua mesmo com millis grande (sem salto entre quadros vizinhos, fora a volta ao topo)
    const uint32_t grande = 4000000000u;
    const float a = gotaY(10.0f, 60.0f, grande, alt), b = gotaY(10.0f, 60.0f, grande + 125, alt);
    const float passo = std::fmod(b - a + alt, alt);
    TEST_ASSERT_FLOAT_WITHIN(0.05f, 60.0f * 125.0f / 3000.0f, passo);
}

void test_neon_treme_de_vez_em_quando_nos_quadros_esperados() {
    int total = 0, blocosCom = 0;
    for (uint32_t bloco = 0; bloco < 400; ++bloco) {
        int noBloco = 0, primeiro = -1;
        for (uint32_t d = 0; d < 32; ++d) {
            const uint32_t q = bloco * 32 + d;
            TEST_ASSERT_EQUAL(neonTreme(q), neonTreme(q));   // deterministico
            if (!neonTreme(q)) continue;
            ++noBloco;
            TEST_ASSERT_TRUE(d >= 3 && d <= 30);                          // nunca na emenda dos blocos
            if (primeiro < 0) primeiro = static_cast<int>(d);
            else TEST_ASSERT_EQUAL_INT(primeiro + 2, static_cast<int>(d));   // falha dupla: 2 quadros depois
        }
        TEST_ASSERT_TRUE(noBloco <= 2);
        total += noBloco;
        blocosCom += noBloco > 0 ? 1 : 0;
    }
    // ~3 em cada 4 blocos de 4 s tem falha: "de vez em quando", nunca o tempo todo
    TEST_ASSERT_TRUE(blocosCom > 400 * 6 / 10 && blocosCom < 400 * 9 / 10);
    TEST_ASSERT_TRUE(total < 400 * 32 / 10);   // menos de 10 % dos quadros
}

void test_gato_pisca_dois_quadros_por_vez() {
    for (uint32_t bloco = 0; bloco < 300; ++bloco) {
        int fechados = 0;
        for (uint32_t d = 0; d < 40; ++d) {
            const uint32_t q = bloco * 40 + d;
            if (!gatoPisca(q)) continue;
            ++fechados;
            // cada piscada tem 2 quadros seguidos (250 ms)
            TEST_ASSERT_TRUE(gatoPisca(q + 1) || gatoPisca(q - 1));
        }
        TEST_ASSERT_TRUE(fechados == 2 || fechados == 4);   // 1 piscada por bloco de 5 s, as vezes 2
    }
}

void test_roteador_um_apagado_girando() {
    for (uint32_t ms = 0; ms < 30000; ms += 125) {
        int acesos = 0;
        for (int i = 0; i < 4; ++i) acesos += roteadorAceso(i, ms) ? 1 : 0;
        TEST_ASSERT_EQUAL_INT(3, acesos);
    }
    // o apagado anda 1 LED a cada 1/8 de fase (375 ms)
    int apagado0 = -1, apagado1 = -1;
    for (int i = 0; i < 4; ++i) {
        if (!roteadorAceso(i, 0)) apagado0 = i;
        if (!roteadorAceso(i, 375)) apagado1 = i;
    }
    TEST_ASSERT_EQUAL_INT((apagado0 + 3) % 4, apagado1);
    TEST_ASSERT_EQUAL(roteadorAceso(2, 374), roteadorAceso(2, 0));
}

void test_rack_sorteia_a_cada_meio_segundo() {
    int acesos = 0, total = 0;
    for (uint32_t passo = 0; passo < 200; ++passo)
        for (int fila = 0; fila < 9; ++fila)
            for (int led = 0; led < 4; ++led) {
                const uint32_t ms = passo * 500;
                const bool a = rackAceso(fila, led, ms, 0.7f);
                TEST_ASSERT_EQUAL(a, rackAceso(fila, led, ms + 499, 0.7f));   // estavel dentro dos 500 ms
                acesos += a ? 1 : 0;
                ++total;
            }
    const float f = static_cast<float>(acesos) / static_cast<float>(total);
    TEST_ASSERT_FLOAT_WITHIN(0.04f, 0.7f, f);
}

void test_teclado_e_faixa_em_onda_com_periodo_de_uma_fase() {
    for (int i = 0; i < 40; ++i)
        for (uint32_t ms = 0; ms < 3000; ms += 125) {
            const float w = teclaMistura(i, ms);
            TEST_ASSERT_TRUE(w >= 0.0f && w <= 1.0f);
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, w, teclaMistura(i, ms + 3000));
            TEST_ASSERT_FLOAT_WITHIN(1e-5f, faixaMistura(i % 12, ms), faixaMistura(i % 12, ms + 6000));
        }
    // tecla vizinha defasada (a onda anda pelo teclado)
    TEST_ASSERT_TRUE(std::fabs(teclaMistura(0, 0) - teclaMistura(5, 0)) > 0.1f);
}

void test_janelas_da_cidade_acendem_e_apagam() {
    int acesas = 0, total = 0, trocas = 0;
    for (int i = 0; i < 40; ++i) {
        bool antes = janelaAcesa(i, 0);
        for (uint32_t ms = 0; ms < 120000; ms += 250) {
            const bool a = janelaAcesa(i, ms);
            TEST_ASSERT_EQUAL(a, janelaAcesa(i, ms));
            acesas += a ? 1 : 0;
            ++total;
            trocas += a != antes ? 1 : 0;
            antes = a;
        }
    }
    const float f = static_cast<float>(acesas) / static_cast<float>(total);
    TEST_ASSERT_TRUE(f > 0.12f && f < 0.38f);   // ~1 em 4
    TEST_ASSERT_TRUE(trocas > 40 * 4);          // mudam com o tempo (nao ficam paradas)
}

void test_drone_so_aparece_dentro_da_janela() {
    const float x0 = 22, x1 = 150, yBase = 36, amp = 3;
    bool viu = false;
    float xAntes = -1e9f;
    for (uint32_t ms = 0; ms < 3 * kDronePeriodoMs; ms += 25) {
        const auto d = drone(ms, x0, x1, yBase, amp, 20, 10);
        if (!d.visivel) {
            xAntes = -1e9f;
            continue;
        }
        viu = true;
        TEST_ASSERT_TRUE(d.x > x0 && d.x < x1);
        TEST_ASSERT_TRUE(d.y >= yBase - amp - 1e-3f && d.y <= yBase + amp + 1e-3f);
        TEST_ASSERT_TRUE(ms % kDronePeriodoMs < 3000);   // passa em 1 fase e some ate o proximo periodo
        TEST_ASSERT_TRUE(d.x > xAntes);   // da esquerda para a direita
        xAntes = d.x;
    }
    TEST_ASSERT_TRUE(viu);
    // a luz vermelha pisca (int(fase * 24) % 2, como no mockup)
    TEST_ASSERT_TRUE(drone(600, x0, x1, yBase, amp, 20, 10).vermelhoAceso != drone(725, x0, x1, yBase, amp, 20, 10).vermelhoAceso);
}

void test_carros_dentro_da_janela_e_no_sentido_certo() {
    const float x0 = 6, x1 = 300;
    for (uint32_t ms = 0; ms < 20000; ms += 50) {
        for (const float f0 : {0.0f, 0.37f}) {
            const float a = carroVivaX(f0, +1, ms, x0, x1), b = carroVivaX(f0, -1, ms, x0, x1);
            TEST_ASSERT_TRUE(a >= x0 && a <= x1);
            TEST_ASSERT_TRUE(b >= x0 && b <= x1);
            const float a2 = carroVivaX(f0, +1, ms + 50, x0, x1), b2 = carroVivaX(f0, -1, ms + 50, x0, x1);
            if (std::fabs(a2 - a) < 100) TEST_ASSERT_TRUE(a2 > a);   // fora a volta: anda para a direita
            if (std::fabs(b2 - b) < 100) TEST_ASSERT_TRUE(b2 < b);
        }
        const float l = carroLongeX(194.0f, -132.0f, ms, 294.0f);
        TEST_ASSERT_TRUE(l >= -20.0f && l < 294.0f + 20.0f);
    }
    // 1 fase = a janela inteira
    TEST_ASSERT_FLOAT_WITHIN(0.01f, carroVivaX(0.0f, +1, 1000, x0, x1), carroVivaX(0.0f, +1, 4000, x0, x1));
}

void test_antenas_alternam_por_camada() {
    for (uint32_t ms = 0; ms < 9000; ms += 125) {
        TEST_ASSERT_TRUE(antenaAcesa(0, ms) != antenaAcesa(1, ms));
        TEST_ASSERT_EQUAL(antenaAcesa(0, ms), antenaAcesa(2, ms));
    }
    TEST_ASSERT_TRUE(antenaAcesa(0, 0) != antenaAcesa(0, 750));   // troca a cada 1/4 de fase
    TEST_ASSERT_EQUAL(antenaAcesa(0, 0), antenaAcesa(0, 749));
}

void test_letreiros_piscam_de_leve() {
    int falhas = 0;
    for (int i = 0; i < 11; ++i)
        for (uint32_t ms = 0; ms < 60000; ms += 125) {
            const float f = letreiroForca(i, ms);
            TEST_ASSERT_TRUE(f >= 0.6f && f <= 1.0f);
            TEST_ASSERT_EQUAL_FLOAT(f, letreiroForca(i, ms));
            falhas += f < 0.85f ? 1 : 0;
        }
    TEST_ASSERT_TRUE(falhas > 0);                      // as vezes falha
    TEST_ASSERT_TRUE(falhas < 11 * 480 / 20);          // mas raramente (< 5 % dos quadros)
}

void test_dois_pontos_piscam_uma_vez_por_segundo() {
    TEST_ASSERT_TRUE(doisPontosAcesos(0));
    TEST_ASSERT_TRUE(doisPontosAcesos(499));
    TEST_ASSERT_FALSE(doisPontosAcesos(500));
    TEST_ASSERT_TRUE(doisPontosAcesos(1000));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_relogio_foco_mostra_o_restante_arredondado_para_cima);
    RUN_TEST(test_relogio_foco_de_uma_hora_ou_mais);
    RUN_TEST(test_relogio_vitoria_e_zero_com_barra_cheia);
    RUN_TEST(test_relogio_hora_extra_conta_para_cima_com_mais);
    RUN_TEST(test_relogio_livre_mostra_o_decorrido_e_a_hora_atual);
    RUN_TEST(test_relogio_valores_fora_da_faixa);
    RUN_TEST(test_mapa_de_segmentos_0_a_9);
    RUN_TEST(test_quadro_e_sprite_a_8_por_segundo);
    RUN_TEST(test_gotas_deterministicas_e_dentro_da_janela);
    RUN_TEST(test_neon_treme_de_vez_em_quando_nos_quadros_esperados);
    RUN_TEST(test_gato_pisca_dois_quadros_por_vez);
    RUN_TEST(test_roteador_um_apagado_girando);
    RUN_TEST(test_rack_sorteia_a_cada_meio_segundo);
    RUN_TEST(test_teclado_e_faixa_em_onda_com_periodo_de_uma_fase);
    RUN_TEST(test_janelas_da_cidade_acendem_e_apagam);
    RUN_TEST(test_drone_so_aparece_dentro_da_janela);
    RUN_TEST(test_carros_dentro_da_janela_e_no_sentido_certo);
    RUN_TEST(test_antenas_alternam_por_camada);
    RUN_TEST(test_letreiros_piscam_de_leve);
    RUN_TEST(test_dois_pontos_piscam_uma_vez_por_segundo);
    return UNITY_END();
}
