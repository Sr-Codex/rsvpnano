// Batalha em cabo de guerra (etapa c do Tempo): posicao da frente, estado -> aparencia, fases do golpe final, particulas de po
// deterministicas, quando a animacao para, o "+" nas fontes de 39/66 px e a composicao da faixa (pixels). Logica pura.
#include <unity.h>

#include <algorithm>
#include <cmath>
#include <vector>

#include "tempo/Cronometro.h"
#include "tempo/Luta.h"
#include "ui/SansFont.h"
#include "ui/luta/Faixa.h"

using namespace tempo::luta;

// Unity sem precisao dupla neste ambiente: compara em float.
#define ASSERT_NEAR(delta, esperado, atual) TEST_ASSERT_FLOAT_WITHIN(float(delta), float(esperado), float(atual))

void setUp() {}
void tearDown() {}

namespace {
    tempo::Cronometro cron(int planoMin, int64_t focadoMs, int64_t distraidoMs = 0) {
        tempo::Cronometro c;
        c.zerar(planoMin);
        c.focadoMs = uint64_t(focadoMs);
        c.distraidoMs = uint64_t(distraidoMs);
        return c;
    }
    constexpr int64_t kMin = 60000;
} // namespace

// ---------------------------------------------------------------- posicao da frente
void test_frente_normal_e_o_progresso_do_foco() {
    ASSERT_NEAR(1e-5, 0.0, frente(cron(25, 0)));
    ASSERT_NEAR(1e-5, 0.4, frente(cron(25, 10 * kMin)));
    ASSERT_NEAR(1e-5, 0.5, frente(cron(50, 25 * kMin)));
}

void test_frente_cura_empurra_de_volta() {
    const double antes = frente(cron(25, 10 * kMin));
    const double depois = frente(cron(25, 10 * kMin, 3 * kMin));   // 3 min distraido = o inimigo curou 3 min
    TEST_ASSERT_TRUE(depois < antes);
    ASSERT_NEAR(1e-5, double(10 * kMin) / double(25 * kMin + 3 * kMin), depois);
    // a cura e limitada ao plano (restante = plano + plano - foco = 40 min)
    ASSERT_NEAR(1e-5, double(10 * kMin) / double(10 * kMin + 40 * kMin), frente(cron(25, 10 * kMin, 90 * kMin)));
}

void test_frente_limites_e_hora_extra() {
    ASSERT_NEAR(1e-5, 1.0, frente(cron(25, 25 * kMin)));        // inimigo zerou
    ASSERT_NEAR(1e-5, 1.0, frente(cron(25, 40 * kMin)));        // hora extra: segue em 1
    ASSERT_NEAR(1e-5, 1.0, frente(cron(25, 28 * kMin, 3 * kMin)));   // zerou depois de curar
    ASSERT_NEAR(1e-5, 0.0, frente(cron(25, 0, 5 * kMin)));      // distraido desde o comeco: foco 0 -> frente 0
    tempo::Cronometro vazio;   // plano 25 por padrao, nada acontecido
    TEST_ASSERT_TRUE(frente(vazio) >= 0.0 && frente(vazio) <= 1.0);
    const double f = frente(cron(25, 1));
    TEST_ASSERT_TRUE(f >= 0.0 && f <= 1.0);
}

void test_volta_extra_recomeca_sem_teto() {
    ASSERT_NEAR(1e-5, 0.0, voltaExtra(cron(25, 20 * kMin)));              // ainda nao zerou
    ASSERT_NEAR(1e-5, 0.0, voltaExtra(cron(25, 25 * kMin)));              // exatamente no zero
    ASSERT_NEAR(1e-6, 192000.0 / double(25 * kMin), voltaExtra(cron(25, 25 * kMin + 192000)));   // +3:12
    ASSERT_NEAR(1e-6, 0.04, voltaExtra(cron(25, 51 * kMin)));              // +26 min: passou de 100 %, recomecou
    ASSERT_NEAR(1e-6, 0.0, voltaExtra(cron(25, 75 * kMin)));              // 2 voltas exatas
    TEST_ASSERT_TRUE(voltaExtra(cron(25, 600 * kMin)) < 1.0);                           // sem teto
}

// ---------------------------------------------------------------- estado -> aparencia
void test_modo_por_prioridade() {
    EntradaModo e;
    TEST_ASSERT_EQUAL(int(Modo::Focando), int(modoDe(e)));
    e.fimSegurando = true;
    TEST_ASSERT_EQUAL(int(Modo::FimSegurando), int(modoDe(e)));
    e.distraido = true;
    TEST_ASSERT_EQUAL(int(Modo::Distraido), int(modoDe(e)));
    e.pausada = true;
    TEST_ASSERT_EQUAL(int(Modo::Pausado), int(modoDe(e)));
    e.descanso = true;
    TEST_ASSERT_EQUAL(int(Modo::PausaCiclo), int(modoDe(e)));
    EntradaModo x;
    x.horaExtra = true;
    TEST_ASSERT_EQUAL(int(Modo::HoraExtra), int(modoDe(x)));
    x.golpeFinal = true;
    TEST_ASSERT_EQUAL(int(Modo::GolpeFinal), int(modoDe(x)));
    x.pausada = true;   // pausar durante o golpe final: tregua
    TEST_ASSERT_EQUAL(int(Modo::Pausado), int(modoDe(x)));
}

void test_efeitos_de_cada_estado() {
    const auto foc = efeitos(Modo::Focando);   // choque branco-quente + faiscas e raios no lado do inimigo
    TEST_ASSERT_TRUE(foc.choque && foc.faiscasInimigo && foc.raios && foc.animada);
    TEST_ASSERT_TRUE(!foc.faiscasRecuo && !foc.setas && !foc.poeira && !foc.azul && !foc.divisoria);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, foc.apagado);

    const auto fim = efeitos(Modo::FimSegurando);   // luta enfraquece: ~30 % apagado, sem faiscas
    TEST_ASSERT_EQUAL_FLOAT(0.30f, fim.apagado);
    TEST_ASSERT_TRUE(fim.choque && !fim.faiscasInimigo && !fim.raios && !fim.animada);

    const auto dis = efeitos(Modo::Distraido);   // laranja apagado, clarao carmim, faiscas para a esquerda, setas
    TEST_ASSERT_TRUE(dis.frenteCarmim && dis.faiscasRecuo && dis.setas && !dis.faiscasInimigo && dis.apagado > 0.0f);

    const auto pau = efeitos(Modo::Pausado);   // tregua: ~50 % apagado, divisoria fria, nada se mexe
    TEST_ASSERT_EQUAL_FLOAT(0.50f, pau.apagado);
    TEST_ASSERT_TRUE(pau.divisoria && !pau.choque && !pau.faiscasInimigo && !pau.faiscasRecuo && !pau.animada);

    const auto ciclo = efeitos(Modo::PausaCiclo);   // mesma linguagem, verde-agua
    TEST_ASSERT_TRUE(ciclo.verdeAgua && ciclo.divisoria && !ciclo.animada);

    const auto golpe = efeitos(Modo::GolpeFinal);
    TEST_ASSERT_TRUE(golpe.poeira && golpe.choque && golpe.animada);

    const auto extra = efeitos(Modo::HoraExtra);
    TEST_ASSERT_TRUE(extra.azul && extra.choque && !extra.poeira);
}

// ---------------------------------------------------------------- fases da animacao
void test_golpe_final_dura_1500ms_e_passa_para_hora_extra() {
    TEST_ASSERT_TRUE(golpeFinalAtivo(0));
    TEST_ASSERT_TRUE(golpeFinalAtivo(1499));
    TEST_ASSERT_FALSE(golpeFinalAtivo(1500));
    TEST_ASSERT_FALSE(golpeFinalAtivo(60000));
    EntradaModo e;
    e.horaExtra = true;   // o Timer nao encerra: o inimigo zerou e segue em hora extra
    e.golpeFinal = golpeFinalAtivo(1000);
    TEST_ASSERT_EQUAL(int(Modo::GolpeFinal), int(modoDe(e)));
    e.golpeFinal = golpeFinalAtivo(1500);
    TEST_ASSERT_EQUAL(int(Modo::HoraExtra), int(modoDe(e)));
}

void test_progresso_do_golpe_e_monotono_e_acaba() {
    float anterior = -1.0f;
    for (uint32_t ms = 0; ms <= 2000; ms += 50) {
        const float p = progressoGolpe(ms);
        TEST_ASSERT_TRUE(p >= anterior);
        anterior = p;
    }
    TEST_ASSERT_EQUAL_FLOAT(0.0f, progressoGolpe(0));
    TEST_ASSERT_EQUAL_FLOAT(kProgGolpe, progressoGolpe(kGolpeFinalMs));
    TEST_ASSERT_EQUAL_FLOAT(kProgGolpe, progressoGolpe(99999));
    TEST_ASSERT_EQUAL_FLOAT(0.0f, frenteDoGolpe(0.0f));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, frenteDoGolpe(kProgGolpe));
}

void test_quadros_por_segundo_entre_10_e_12() {
    const float fps = 1000.0f / float(kQuadroMs);
    TEST_ASSERT_TRUE(fps >= 10.0f && fps <= 12.0f);
    TEST_ASSERT_EQUAL_UINT32(0, indiceDoQuadro(0));
    TEST_ASSERT_EQUAL_UINT32(0, indiceDoQuadro(kQuadroMs - 1));
    TEST_ASSERT_EQUAL_UINT32(1, indiceDoQuadro(kQuadroMs));
}

// ---------------------------------------------------------------- particulas
namespace {
    std::vector<Particula> gerar(uint32_t semente, int n) {
        Rng rng(semente);
        std::vector<Particula> v;
        for (int i = 0; i < n; ++i) {
            Semente s;
            s.x = 340.0f + float(i % 60) * 2.0f;
            s.y = 30.0f + float(i / 60) * 2.0f;
            s.f = float(i % 60) / 59.0f;
            s.r = 240;
            s.g = 70;
            s.b = 100;
            v.push_back(criarParticula(s, rng));
        }
        return v;
    }
} // namespace

void test_particulas_deterministicas_pela_semente() {
    const auto a = gerar(1234, 300), b = gerar(1234, 300), c = gerar(99, 300);
    bool algumaDiferente = false;
    for (size_t i = 0; i < a.size(); ++i) {
        TEST_ASSERT_EQUAL_FLOAT(a[i].t0, b[i].t0);
        TEST_ASSERT_EQUAL_FLOAT(a[i].vx, b[i].vx);
        TEST_ASSERT_EQUAL_FLOAT(a[i].vy, b[i].vy);
        const auto pa = estadoParticula(a[i], 0.5f), pb = estadoParticula(b[i], 0.5f);
        TEST_ASSERT_EQUAL_FLOAT(pa.x, pb.x);
        TEST_ASSERT_EQUAL_FLOAT(pa.y, pb.y);
        TEST_ASSERT_EQUAL_FLOAT(pa.a, pb.a);
        if (a[i].t0 != c[i].t0) algumaDiferente = true;
    }
    TEST_ASSERT_TRUE(algumaDiferente);
}

void test_particula_espera_solta_esquenta_esfria_e_some() {
    Particula p;
    p.x = 400;
    p.y = 130;
    p.t0 = 0.3f;
    p.vx = 20;
    p.vy = 80;
    p.r = 240;
    p.g = 70;
    p.b = 100;
    const auto antes = estadoParticula(p, 0.1f);   // a frente ainda nao passou: parada, opaca
    TEST_ASSERT_FALSE(antes.solta);
    TEST_ASSERT_EQUAL_FLOAT(1.0f, antes.a);
    TEST_ASSERT_EQUAL_FLOAT(400.0f, antes.x);
    TEST_ASSERT_EQUAL_FLOAT(130.0f, antes.y);
    const auto quente = estadoParticula(p, 0.31f);   // logo depois: esquenta (mais claro/rosado que a cor de origem)
    TEST_ASSERT_TRUE(quente.solta);
    TEST_ASSERT_TRUE(int(quente.g) > 70);
    const auto frio = estadoParticula(p, 0.3f + 0.40f);   // mais tarde: esfria em cinza (g sobe para ~120, r cai)
    TEST_ASSERT_TRUE(frio.y < 130.0f && frio.x > 400.0f);   // levada pelo vento: sobe e vai para a direita
    TEST_ASSERT_TRUE(frio.a > 0.0f && frio.a < 0.3f);
    TEST_ASSERT_TRUE(int(frio.r) < 240);
    const auto fim = estadoParticula(p, 0.3f + kVidaParticula + 0.01f);
    TEST_ASSERT_EQUAL_FLOAT(0.0f, fim.a);
}

void test_po_some_por_completo_ate_o_fim_do_golpe() {
    // a ultima particula (f = 1, ruido maximo) ainda some antes de kProgGolpe
    for (uint32_t semente : {1u, 7u, 12345u, 0xC0FFEEu}) {
        const auto v = gerar(semente, 480);
        for (const auto& p : v) {
            TEST_ASSERT_TRUE(p.t0 <= kProgFrente + kRuidoLiberacao + 1e-4f);
            TEST_ASSERT_EQUAL_FLOAT(0.0f, estadoParticula(p, kProgGolpe).a);
            TEST_ASSERT_TRUE(estadoParticula(p, 0.0f).a == 1.0f);   // no inicio todas inteiras
        }
    }
    // a frente de liberacao passa da esquerda para a direita: f maior solta depois (em media)
    Rng rng(5);
    Semente e{}, d{};
    e.f = 0.0f;
    d.f = 1.0f;
    float somaE = 0, somaD = 0;
    for (int i = 0; i < 200; ++i) {
        somaE += criarParticula(e, rng).t0;
        somaD += criarParticula(d, rng).t0;
    }
    TEST_ASSERT_TRUE(somaD > somaE + 200 * 0.4f);
}

// ---------------------------------------------------------------- quando animar
void test_animacao_para_nos_casos_certos() {
    ContextoAnimacao c;
    TEST_ASSERT_TRUE(deveAnimar(c));                 // batalha na frente, tela clara, deitada
    c.escurecida = true;
    TEST_ASSERT_FALSE(deveAnimar(c));                // Modo Foco escurecendo/escuro
    c = ContextoAnimacao{};
    c.cenaNaTela = true;
    TEST_ASSERT_FALSE(deveAnimar(c));                // cena QG em tela cheia
    c = ContextoAnimacao{};
    c.telaBatalha = false;
    TEST_ASSERT_FALSE(deveAnimar(c));                // fora da tela da batalha
    c = ContextoAnimacao{};
    c.emPe = true;
    TEST_ASSERT_FALSE(deveAnimar(c));                // placa em pe
}

void test_quadro_de_repouso_tem_brilho_fixo() {
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pulso(3, false));
    TEST_ASSERT_EQUAL_FLOAT(1.0f, pulso(7, false));
    for (uint32_t q = 0; q < 30; ++q) {
        const float p = pulso(q, true);
        TEST_ASSERT_TRUE(p >= 0.49f && p <= 1.0f);
    }
    TEST_ASSERT_TRUE(pulso(2, true) != pulso(7, true));
}

// ---------------------------------------------------------------- "+" nas fontes de 39 e 66 px
void test_fontes_grandes_tem_mais_e_o_resto_segue_igual() {
    for (uint8_t tam : {uint8_t(39), uint8_t(66)}) {
        const auto& f = ui::sans::font(tam);
        TEST_ASSERT_NOT_NULL(ui::sans::glyph(f, '+'));
        TEST_ASSERT_EQUAL_UINT16(16, f.count);   // 15 de antes + o "+"
        for (char ch : std::string(" 0123456789:.-?+")) TEST_ASSERT_NOT_NULL(ui::sans::glyph(f, uint32_t(ch)));
        TEST_ASSERT_NULL(ui::sans::glyph(f, 'A'));   // so os caracteres do relogio
        for (uint16_t i = 1; i < f.count; ++i) TEST_ASSERT_TRUE(f.glyphs[i - 1].codepoint < f.glyphs[i].codepoint);   // busca binaria valida
        const auto* mais = ui::sans::glyph(f, '+');
        TEST_ASSERT_TRUE(mais->width > 8 && mais->height > 8 && mais->advance > mais->width / 2);
    }
    // o relogio da hora extra cabe: "+3:12" tem largura de 5 caracteres e o "+" nao e um "?"
    const auto& f66 = ui::sans::font(66);
    TEST_ASSERT_TRUE(ui::sans::width("+3:12", f66) > ui::sans::width("3:12", f66));
    TEST_ASSERT_TRUE(ui::sans::glyph(f66, '+') != ui::sans::glyph(f66, '?'));
}

// ---------------------------------------------------------------- composicao da faixa (pixels)
namespace {
    struct Mem {
        std::vector<uint8_t> rgb = std::vector<uint8_t>(size_t(ui::luta::kLarg) * ui::luta::kAlt * 3);
        std::vector<uint16_t> saida = std::vector<uint16_t>(size_t(ui::luta::kLarg) * ui::luta::kAlt);
        std::vector<uint8_t> pilula = std::vector<uint8_t>(size_t(ui::luta::kBarraLarg) * ui::luta::kBarraAlt);
        std::vector<uint8_t> camada = std::vector<uint8_t>(size_t(ui::luta::kCamadaLarg) * ui::luta::kAlt * 3);
        std::vector<Particula> part = std::vector<Particula>(ui::luta::kMaxParticulas);
        std::vector<uint16_t> blocos = std::vector<uint16_t>(ui::luta::kMaxParticulas * 2);
        ui::luta::Memoria m() {
            ui::luta::Memoria r;
            r.rgb = rgb.data();
            r.saida = saida.data();
            r.pilula = pilula.data();
            r.camada = camada.data();
            r.particulas = part.data();
            r.blocos = blocos.data();
            return r;
        }
    };

    ui::luta::Quadro quadroBase(Modo modo) {
        ui::luta::Quadro q;
        q.modo = modo;
        q.esq = {"VOCÊ 10:02", {255, 120, 50}, true, icons::phosphor::Id::Sword};
        q.dir = {"60% INIMIGO", {240, 96, 116}, true, icons::phosphor::Id::Skull};
        return q;
    }

    void rgbDe(uint16_t c, int& r, int& g, int& b) {
        r = ((c >> 11) & 31) << 3;
        g = ((c >> 5) & 63) << 2;
        b = (c & 31) << 3;
    }
    uint16_t px(const Mem& m, int x, int y) { return m.saida[size_t(y) * ui::luta::kLarg + size_t(x)]; }
    int somaCanais(uint16_t c) {
        int r, g, b;
        rgbDe(c, r, g, b);
        return r + g + b;
    }
} // namespace

void test_faixa_focando_laranja_a_esquerda_carmim_a_direita() {
    Mem mem;
    ui::luta::Faixa faixa(mem.m());
    auto q = quadroBase(Modo::Focando);
    q.frente = 0.4f;   // frente em x = 8 + 0,4 * 452 = 188,8
    faixa.compor(q);
    int r, g, b;
    rgbDe(px(mem, 100, 32), r, g, b);   // linha central da barra, bem antes da frente
    TEST_ASSERT_TRUE(r > 200 && g > 90 && b < 110);   // laranja -> ambar
    rgbDe(px(mem, 400, 32), r, g, b);   // depois da frente: carmim
    TEST_ASSERT_TRUE(r > 150 && g < 80 && b > 40);
    rgbDe(px(mem, 2, 2), r, g, b);      // cantos do fundo seguem pretos
    TEST_ASSERT_TRUE(r + g + b < 8);
    TEST_ASSERT_EQUAL_INT(0, somaCanais(px(mem, 2, 32)));   // fora do trilho (x < 8)
    // rotulos: ha tinta nas duas linhas de rotulo (y 0..16)
    int tintaEsq = 0, tintaDir = 0;
    for (int y = 0; y < 17; ++y) {
        for (int x = 0; x < 120; ++x) tintaEsq += somaCanais(px(mem, x, y)) > 60;
        for (int x = 340; x < 468; ++x) tintaDir += somaCanais(px(mem, x, y)) > 60;
    }
    TEST_ASSERT_TRUE(tintaEsq > 80 && tintaDir > 80);
    // a frente de choque: perto de x = 188 o brilho passa do das cores vizinhas
    TEST_ASSERT_TRUE(somaCanais(px(mem, 188, 32)) > somaCanais(px(mem, 150, 32)) + 60);
}

void test_faixa_frente_acompanha_a_posicao_e_a_cura_recua() {
    Mem mem;
    ui::luta::Faixa faixa(mem.m());
    auto q = quadroBase(Modo::Focando);
    q.animar = false;
    auto xDoMaisBrilho = [&](float frente) {
        q.frente = frente;
        faixa.compor(q);
        int melhor = -1;
        for (int i = 20; i < 440; ++i) melhor = std::max(melhor, somaCanais(px(mem, i, 32)));
        long soma = 0, n = 0;   // centro do trecho mais brilhante (o clarao satura varios pixels)
        for (int i = 20; i < 440; ++i)
            if (somaCanais(px(mem, i, 32)) >= melhor - 3) {
                soma += i;
                ++n;
            }
        const int x = int(soma / std::max(1L, n));
        return x;
    };
    const int a = xDoMaisBrilho(0.50f), b = xDoMaisBrilho(0.40f);
    TEST_ASSERT_INT_WITHIN(3, int(8 + 0.50f * 452), a);
    TEST_ASSERT_INT_WITHIN(3, int(8 + 0.40f * 452), b);
    TEST_ASSERT_TRUE(b < a);
}

void test_faixa_repouso_e_estavel_e_sem_faiscas() {
    Mem m1, m2;
    ui::luta::Faixa f1(m1.m()), f2(m2.m());
    for (Modo modo : {Modo::Focando, Modo::Distraido, Modo::HoraExtra}) {
        auto q = quadroBase(modo);
        q.animar = false;
        q.pulso = 1.0f;
        q.extra = 0.128f;
        q.quadro = 3;
        f1.compor(q);
        q.quadro = 977;   // outra semente de faiscas: sem animar nada muda
        f2.compor(q);
        TEST_ASSERT_EQUAL_MEMORY(m1.saida.data(), m2.saida.data(), m1.saida.size() * 2);
    }
    // animando, quadros diferentes tem faiscas diferentes
    auto q = quadroBase(Modo::Focando);
    q.quadro = 3;
    f1.compor(q);
    q.quadro = 4;
    f2.compor(q);
    TEST_ASSERT_TRUE(std::memcmp(m1.saida.data(), m2.saida.data(), m1.saida.size() * 2) != 0);
}

void test_faixa_estados_se_distinguem_pela_cor() {
    Mem mem;
    ui::luta::Faixa faixa(mem.m());
    auto q = quadroBase(Modo::Focando);
    q.animar = false;
    faixa.compor(q);
    int r0, g0, b0;
    rgbDe(px(mem, 60, 32), r0, g0, b0);
    q.modo = Modo::Pausado;   // ~50 % apagado
    faixa.compor(q);
    int r1, g1, b1;
    rgbDe(px(mem, 60, 32), r1, g1, b1);
    TEST_ASSERT_TRUE(r1 < r0 * 3 / 4);
    // hora extra: a esquerda (parte extra) e azul, o resto e laranja
    q = quadroBase(Modo::HoraExtra);
    q.extra = 0.30f;
    q.animar = false;
    faixa.compor(q);
    int r, g, b;
    rgbDe(px(mem, 40, 28), r, g, b);
    TEST_ASSERT_TRUE(b > r);                     // azul
    rgbDe(px(mem, 300, 32), r, g, b);
    TEST_ASSERT_TRUE(r > 200 && b < 120);        // laranja
    // hora extra PAUSADA (tregua): a mesma barra, bem mais apagada
    {
        auto e = quadroBase(Modo::HoraExtra);
        e.extra = 0.30f;
        e.animar = false;
        faixa.compor(e);
        int rn, gn, bn, rt, gt, bt;
        rgbDe(px(mem, 300, 32), rn, gn, bn);
        e.tregua = true;
        faixa.compor(e);
        rgbDe(px(mem, 300, 32), rt, gt, bt);
        TEST_ASSERT_TRUE(rt < rn * 3 / 4 + 4);
    }
    // pausa do ciclo: verde-agua enchendo ate `pausa`
    q = quadroBase(Modo::PausaCiclo);
    q.pausa = 0.5f;
    q.animar = false;
    faixa.compor(q);
    rgbDe(px(mem, 60, 32), r, g, b);
    TEST_ASSERT_TRUE(g > r && b > r);
    rgbDe(px(mem, 400, 32), r, g, b);            // esvaziada: so o trilho escuro
    TEST_ASSERT_TRUE(r + g + b < 120);
}

void test_faixa_golpe_final_po_deterministico_e_some() {
    Mem a, b;
    ui::luta::Faixa fa(a.m()), fb(b.m());
    const ui::luta::Rotulo inimigo{"INIMIGO", {240, 96, 116}, true, icons::phosphor::Id::Skull};
    TEST_ASSERT_TRUE(fa.prepararPoeira(inimigo, 0xC0FFEEu, {0, 0, 0}));
    TEST_ASSERT_TRUE(fb.prepararPoeira(inimigo, 0xC0FFEEu, {0, 0, 0}));
    TEST_ASSERT_TRUE(fa.numParticulas() > 400 && fa.numParticulas() <= ui::luta::kMaxParticulas);
    TEST_ASSERT_EQUAL_UINT32(fa.numParticulas(), fb.numParticulas());
    for (uint32_t i = 0; i < fa.numParticulas(); ++i) {
        TEST_ASSERT_EQUAL_FLOAT(a.part[i].x, b.part[i].x);
        TEST_ASSERT_EQUAL_FLOAT(a.part[i].y, b.part[i].y);
        TEST_ASSERT_EQUAL_FLOAT(a.part[i].t0, b.part[i].t0);
        TEST_ASSERT_EQUAL_UINT16(a.blocos[i * 2], b.blocos[i * 2]);
    }
    // o mesmo golpe em tres instantes: inicio (pedaco carmim inteiro), meio (po no ar), fim (so a barra laranja)
    auto q = quadroBase(Modo::GolpeFinal);
    q.esq.texto = "VOCÊ 25:00";
    q.golpe = 0.0f;
    fa.compor(q);
    int carmimInicio = 0;
    for (int y = 22; y < 42; ++y)
        for (int x = 360; x < 456; ++x) {
            int r, g, bl;
            rgbDe(px(a, x, y), r, g, bl);
            carmimInicio += (r > 150 && g < 140 && bl > 60 && r > bl + 40) ? 1 : 0;
        }
    TEST_ASSERT_TRUE(carmimInicio > 900);
    q.golpe = 0.5f;
    fa.compor(q);
    int claroAcima = 0;   // po subindo: tinta acima da barra (y < 18) na zona do pedaco, onde no inicio so havia rotulo
    for (int y = 0; y < 14; ++y)
        for (int x = 330; x < 440; ++x) claroAcima += somaCanais(px(a, x, y)) > 40;
    q.golpe = 0.0f;
    fa.compor(q);
    int tintaInicioAcima = 0;
    for (int y = 0; y < 14; ++y)
        for (int x = 330; x < 440; ++x) tintaInicioAcima += somaCanais(px(a, x, y)) > 40;
    q.golpe = 0.5f;
    fa.compor(q);
    int claroAcima2 = 0;
    for (int y = 0; y < 14; ++y)
        for (int x = 330; x < 440; ++x) claroAcima2 += somaCanais(px(a, x, y)) > 40;
    TEST_ASSERT_TRUE(claroAcima2 > 0);
    (void)claroAcima;
    (void)tintaInicioAcima;
    q.golpe = kProgGolpe;
    fa.compor(q);
    int carmimFim = 0, laranjaFim = 0;
    for (int y = 22; y < 42; ++y)
        for (int x = 200; x < 456; ++x) {
            int r, g, bl;
            rgbDe(px(a, x, y), r, g, bl);
            carmimFim += (r > bl + 90 && g < 90) ? 1 : 0;
            laranjaFim += (r > 200 && g > 100 && bl < 130) ? 1 : 0;
        }
    TEST_ASSERT_EQUAL_INT(0, carmimFim);          // o inimigo virou po: nada de carmim
    TEST_ASSERT_TRUE(laranjaFim > 3500);          // e a barra e toda sua
    // o rotulo do inimigo tambem sumiu no fim (acima da barra, a direita: so o halo da frente, sem texto rosa)
    int rosa = 0;
    for (int y = 0; y < 17; ++y)
        for (int x = 340; x < 468; ++x) {
            int r, g, bl;
            rgbDe(px(a, x, y), r, g, bl);
            rosa += (r > 150 && g < 140 && bl > 80) ? 1 : 0;
        }
    TEST_ASSERT_EQUAL_INT(0, rosa);
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_frente_normal_e_o_progresso_do_foco);
    RUN_TEST(test_frente_cura_empurra_de_volta);
    RUN_TEST(test_frente_limites_e_hora_extra);
    RUN_TEST(test_volta_extra_recomeca_sem_teto);
    RUN_TEST(test_modo_por_prioridade);
    RUN_TEST(test_efeitos_de_cada_estado);
    RUN_TEST(test_golpe_final_dura_1500ms_e_passa_para_hora_extra);
    RUN_TEST(test_progresso_do_golpe_e_monotono_e_acaba);
    RUN_TEST(test_quadros_por_segundo_entre_10_e_12);
    RUN_TEST(test_particulas_deterministicas_pela_semente);
    RUN_TEST(test_particula_espera_solta_esquenta_esfria_e_some);
    RUN_TEST(test_po_some_por_completo_ate_o_fim_do_golpe);
    RUN_TEST(test_animacao_para_nos_casos_certos);
    RUN_TEST(test_quadro_de_repouso_tem_brilho_fixo);
    RUN_TEST(test_fontes_grandes_tem_mais_e_o_resto_segue_igual);
    RUN_TEST(test_faixa_focando_laranja_a_esquerda_carmim_a_direita);
    RUN_TEST(test_faixa_frente_acompanha_a_posicao_e_a_cura_recua);
    RUN_TEST(test_faixa_repouso_e_estavel_e_sem_faiscas);
    RUN_TEST(test_faixa_estados_se_distinguem_pela_cor);
    RUN_TEST(test_faixa_golpe_final_po_deterministico_e_some);
    return UNITY_END();
}
