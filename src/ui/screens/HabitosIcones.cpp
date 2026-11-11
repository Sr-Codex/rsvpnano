#include "ui/screens/HabitosIcones.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ui/Rgb565.h"

namespace screens::icones {
    using ui::themes::rgb565;
    namespace {
        constexpr double kPi = 3.14159265358979323846;
        int gDx = 0, gDy = 0;   // deslocamento logico -> buffer de pintura (ver Bloco::desenhar: extra(g, dx, dy))
        void o_fillCircle(Arduino_GFX& g, int x, int y, int r, uint16_t c) { g.fillCircle(x + gDx, y + gDy, r, c); }
        void o_fillTriangle(Arduino_GFX& g, int x0, int y0, int x1, int y1, int x2, int y2, uint16_t c) {
            g.fillTriangle(x0 + gDx, y0 + gDy, x1 + gDx, y1 + gDy, x2 + gDx, y2 + gDy, c);
        }
        void o_drawLine(Arduino_GFX& g, int x0, int y0, int x1, int y1, uint16_t c) { g.drawLine(x0 + gDx, y0 + gDy, x1 + gDx, y1 + gDy, c); }
        void o_drawCircle(Arduino_GFX& g, int x, int y, int r, uint16_t c) { g.drawCircle(x + gDx, y + gDy, r, c); }
        void o_drawRoundRect(Arduino_GFX& g, int x, int y, int w, int h, int r, uint16_t c) { g.drawRoundRect(x + gDx, y + gDy, w, h, r, c); }
        void o_drawFastHLine(Arduino_GFX& g, int x, int y, int w, uint16_t c) { g.drawFastHLine(x + gDx, y + gDy, w, c); }

        inline int arred(double v) { return int(std::lround(v)); }

        void quad(Arduino_GFX& g, double x0, double y0, double x1, double y1, double x2, double y2, double x3, double y3, uint16_t cor) {
            o_fillTriangle(g, arred(x0), arred(y0), arred(x1), arred(y1), arred(x2), arred(y2), cor);
            o_fillTriangle(g, arred(x0), arred(y0), arred(x2), arred(y2), arred(x3), arred(y3), cor);
        }

        // Capsula (stadium) de eixo u: centros das pontas em c +- u*l, raio r.
        void capsula(Arduino_GFX& g, double cx, double cy, double ux, double uy, double l, double r, uint16_t cor, bool soMetadeNegativa = false,
                     double recuo = 0.0) {
            const double px = -uy, py = ux;
            const double ax = cx - ux * l, ay = cy - uy * l;   // ponta negativa
            o_fillCircle(g, arred(ax), arred(ay), std::max(1, arred(r)), cor);
            if (soMetadeNegativa) {
                const double fx = cx - ux * recuo, fy = cy - uy * recuo;
                quad(g, ax + px * r, ay + py * r, fx + px * r, fy + py * r, fx - px * r, fy - py * r, ax - px * r, ay - py * r, cor);
                return;
            }
            const double bx = cx + ux * l, by = cy + uy * l;
            o_fillCircle(g, arred(bx), arred(by), std::max(1, arred(r)), cor);
            quad(g, ax + px * r, ay + py * r, bx + px * r, by + py * r, bx - px * r, by - py * r, ax - px * r, ay - py * r, cor);
        }
    } // namespace

    Origem::Origem(int dx, int dy) : antesDx_(gDx), antesDy_(gDy) { gDx = dx; gDy = dy; }
    Origem::~Origem() { gDx = antesDx_; gDy = antesDy_; }

    int deslocX() { return gDx; }
    int deslocY() { return gDy; }
    void preencherRR(Arduino_GFX& g, int x, int y, int w, int h, int raio, uint16_t cor) { g.fillRoundRect(x + gDx, y + gDy, w, h, raio, cor); }
    void preencherRetangulo(Arduino_GFX& g, int x, int y, int w, int h, uint16_t cor) { g.fillRect(x + gDx, y + gDy, w, h, cor); }
    void disco(Arduino_GFX& g, int cx, int cy, int raio, uint16_t cor) { o_fillCircle(g, cx, cy, raio, cor); }

    uint16_t verde() { return rgb565(40, 190, 120); }
    uint16_t verdeClaro() { return rgb565(130, 235, 175); }
    uint16_t laranja() { return rgb565(255, 120, 50); }
    uint16_t ambar() { return rgb565(255, 196, 80); }
    uint16_t carmim() { return rgb565(240, 70, 100); }
    uint16_t carmimClaro() { return rgb565(255, 150, 170); }
    uint16_t tinta() { return rgb565(12, 13, 14); }
    uint16_t apagado() { return rgb565(110, 112, 106); }
    uint16_t linhaSutil() { return rgb565(52, 54, 52); }
    uint16_t painel() { return rgb565(34, 36, 35); }
    uint16_t fundoLaranja() { return rgb565(52, 30, 18); }
    uint16_t fundoCarmim() { return rgb565(56, 22, 30); }

    void linha(Arduino_GFX& g, double x0, double y0, double x1, double y1, double w, uint16_t cor) {
        if (w <= 1.3) {
            o_drawLine(g, arred(x0), arred(y0), arred(x1), arred(y1), cor);
            return;
        }
        const double dx = x1 - x0, dy = y1 - y0, len = std::hypot(dx, dy);
        if (len < 0.01) {
            o_fillCircle(g, arred(x0), arred(y0), std::max(1, arred(w / 2)), cor);
            return;
        }
        const double nx = -dy / len * w / 2, ny = dx / len * w / 2;
        quad(g, x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, cor);
        if (w >= 2.5) {   // pontas arredondadas
            const int r = std::max(1, arred(w / 2 - 0.2));
            o_fillCircle(g, arred(x0), arred(y0), r, cor);
            o_fillCircle(g, arred(x1), arred(y1), r, cor);
        }
    }

    void polilinha(Arduino_GFX& g, const double* p, int n, double w, uint16_t cor, bool fechada) {
        for (int i = 0; i + 1 < n; ++i) linha(g, p[2 * i], p[2 * i + 1], p[2 * i + 2], p[2 * i + 3], w, cor);
        if (fechada && n > 2) linha(g, p[2 * (n - 1)], p[2 * (n - 1) + 1], p[0], p[1], w, cor);
    }

    void contorno(Arduino_GFX& g, int x, int y, int w, int h, int raio, int espessura, uint16_t cor) {
        for (int i = 0; i < espessura && w - 2 * i > 2 && h - 2 * i > 2; ++i)
            o_drawRoundRect(g, x + i, y + i, w - 2 * i, h - 2 * i, std::max(0, raio - i), cor);
    }

    void aro(Arduino_GFX& g, int cx, int cy, int r, int espessura, uint16_t cor) {
        for (int i = 0; i < std::max(1, espessura) && r - i > 0; ++i) o_drawCircle(g, cx, cy, r - i, cor);
    }

    void visto(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor, double w) {
        const double k = s / 12.0;
        if (w <= 0) w = std::max(2.0, s / 5.0);
        const double p[] = {cx - 6 * k, cy + 0.5 * k, cx - 2 * k, cy + 5 * k, cx + 7 * k, cy - 5 * k};
        polilinha(g, p, 3, w, cor);
    }

    void pular(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor) {
        const double k = s / 12.0, w = std::max(2.0, s / 6.0);
        const double a[] = {cx - 7 * k, cy + 4 * k, cx - 7 * k, double(cy), cx + 5 * k, double(cy)};
        polilinha(g, a, 3, w, cor);
        const double b[] = {cx + 1 * k, cy - 4 * k, cx + 5 * k, double(cy), cx + 1 * k, cy + 4 * k};
        polilinha(g, b, 3, w, cor);
    }

    void chama(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor) {
        const double k = s / 10.0, w = std::max(2.0, s / 5.0);
        const double p[] = {cx + 0.0, cy - 10 * k, cx + 6 * k, cy - 1 * k, cx + 5 * k, cy + 6 * k, cx + 0.0, cy + 9 * k,
                            cx - 5 * k, cy + 6 * k, cx - 6 * k, cy + 0.0, cx - 3 * k, cy - 4 * k, cx - 1 * k, cy - 1 * k};
        polilinha(g, p, 8, w, cor, true);
    }

    void desfazer(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor) {
        // seta "voltar": ponta em "<" a esquerda, corpo horizontal, curva para a direita e volta por baixo
        const double k = s / 10.0, w = std::max(2.0, s / 5.0);
        const double ponta[] = {cx - 3 * k, cy - 5 * k, cx - 7 * k, cy - 1 * k, cx - 3 * k, cy + 3 * k};
        polilinha(g, ponta, 3, w, cor);
        std::vector<double> corpo{cx - 7 * k, cy - 1 * k, cx + 3 * k, cy - 1 * k};
        for (int a = -90; a <= 90; a += 20) {
            corpo.push_back(cx + 3 * k + 4.5 * k * std::cos(a * kPi / 180.0));
            corpo.push_back(cy + 3.5 * k + 4.5 * k * std::sin(a * kPi / 180.0));
        }
        corpo.push_back(cx - 1 * k);
        corpo.push_back(cy + 8 * k);
        polilinha(g, corpo.data(), int(corpo.size() / 2), w, cor);
    }

    void habito(Arduino_GFX& g, tempo::IconeHabito ic, int cx, int cy, int s, uint16_t cor, uint16_t fundo) {
        using I = tempo::IconeHabito;
        const double k = s / 12.0;
        const double w = std::max(2.0, s / 6.0);
        const int wi = std::max(2, arred(w));
        const auto X = [&](double dx) { return cx + dx * k; };
        const auto Y = [&](double dy) { return cy + dy * k; };
        switch (ic) {
        case I::Haltere: {
            linha(g, X(-7), Y(0), X(7), Y(0), w, cor);
            for (int sx : {-1, 1}) {
                contorno(g, arred(X(sx * 7 - 2)), arred(Y(-7)), std::max(4, arred(4 * k)), arred(14 * k), arred(1.5 * k), wi, cor);
                linha(g, X(sx * 11), Y(-3.5), X(sx * 11), Y(3.5), w, cor);
            }
            break;
        }
        case I::Frasco: {
            contorno(g, arred(X(-6)), arred(Y(-5)), arred(12 * k), arred(15 * k), arred(2.5 * k), wi, cor);
            contorno(g, arred(X(-4)), arred(Y(-10)), arred(8 * k), arred(4.5 * k), arred(1 * k), wi, cor);
            linha(g, X(-6), Y(1), X(6), Y(1), w * 0.85, cor);
            break;
        }
        case I::Capsula: {   // diagonal, metade cheia
            const double a = -40.0 * kPi / 180.0, ux = std::cos(a), uy = std::sin(a);
            const double r = 4.8 * k, l = 6.2 * k;
            capsula(g, cx, cy, ux, uy, l, r, cor);                                     // forma cheia
            capsula(g, cx, cy, ux, uy, l, std::max(0.6, r - w), fundo, true, w * 0.5);   // metade vazia (a outra segue cheia)
            break;
        }
        case I::Talher: {
            linha(g, X(-5), Y(-10), X(-5), Y(10), w, cor);
            for (double dx : {-8.0, -2.0}) linha(g, X(dx), Y(-10), X(dx), Y(-4), w * 0.85, cor);
            linha(g, X(-8), Y(-4), X(-2), Y(-4), w * 0.85, cor);
            linha(g, X(5), Y(10), X(5), Y(-10), w, cor);
            const double lamina[] = {X(5), Y(-10), X(8), Y(-8.5), X(9), Y(-4), X(8), Y(-0.5), X(5), Y(0)};
            polilinha(g, lamina, 5, w * 0.85, cor);
            break;
        }
        case I::Dente: {
            const double p[] = {X(-7), Y(-6), X(-4), Y(-9), X(0), Y(-7), X(4), Y(-9), X(7), Y(-6), X(6), Y(2), X(4), Y(9),
                                X(2), Y(9),  X(1),  Y(3),  X(-1), Y(3), X(-2), Y(9), X(-4), Y(9), X(-6), Y(2)};
            polilinha(g, p, 13, w, cor, true);
            break;
        }
        case I::Xicara: {
            contorno(g, arred(X(-8)), arred(Y(-4)), arred(12 * k), arred(12 * k), arred(3 * k), wi, cor);
            const double asa[] = {X(4), Y(-1), X(8), Y(-1), X(9.5), Y(2), X(8), Y(5), X(4), Y(5)};
            polilinha(g, asa, 5, w, cor);
            for (double dx : {-5.0, -1.0}) {
                const double vapor[] = {X(dx), Y(-5.5), X(dx - 1.6), Y(-8), X(dx), Y(-10.5)};
                polilinha(g, vapor, 3, w * 0.8, cor);
            }
            break;
        }
        case I::Gota: {
            const double topo[] = {X(-6.4), Y(-0.5), X(0), Y(-11), X(6.4), Y(-0.5)};
            polilinha(g, topo, 3, w, cor);
            std::vector<double> arco;
            for (int a = -20; a <= 200; a += 12) {
                arco.push_back(X(7 * std::cos(a * kPi / 180.0)));
                arco.push_back(Y(3 + 7 * std::sin(a * kPi / 180.0)));
            }
            polilinha(g, arco.data(), int(arco.size() / 2), w, cor);
            break;
        }
        case I::Lotus: {
            for (int ang : {-50, 0, 50}) {
                const double a = (ang - 90) * kPi / 180.0;
                const double tx = 9 * std::cos(a), ty = 9 * std::sin(a);
                const double my = (6 + ty) / 2;
                const double p[] = {X(0), Y(6), X(tx - 3), Y(my), X(tx), Y(ty), X(tx + 3), Y(my), X(0), Y(6)};
                polilinha(g, p, 5, w * 0.9, cor);
            }
            linha(g, X(-10), Y(8.5), X(10), Y(8.5), w * 0.9, cor);
            break;
        }
        case I::Livro: {
            const double esq[] = {X(0), Y(-5), X(-10), Y(-8), X(-10), Y(7), X(0), Y(10)};
            const double dir[] = {X(0), Y(-5), X(10), Y(-8), X(10), Y(7), X(0), Y(10)};
            polilinha(g, esq, 4, w, cor);
            polilinha(g, dir, 4, w, cor);
            linha(g, X(0), Y(-5), X(0), Y(10), w, cor);
            break;
        }
        case I::Generico:
        default:
            aro(g, cx, cy, std::max(3, arred(8 * k)), wi, cor);
            o_fillCircle(g, cx, cy, std::max(1, arred(2.6 * k)), cor);
            break;
        }
    }

    uint16_t de888(uint32_t rgb) { return rgb565(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)); }

    void marcador(Arduino_GFX& g, tempo::IconeHabito ic, int cx, int cy, int r, Marca m, uint16_t fundo, int32_t tom) {
        switch (m) {
        case Marca::Feito:
            o_fillCircle(g, cx, cy, r, verde());
            visto(g, cx, cy, std::max(5, arred(r * 0.62)), tinta(), std::max(2.0, r / 4.5));
            break;
        case Marca::Atual:
            o_fillCircle(g, cx, cy, r, fundoLaranja());
            aro(g, cx, cy, r, std::max(2, r / 6), laranja());
            habito(g, ic, cx, cy, std::max(6, arred(r * 0.58)), laranja(), fundoLaranja());
            break;
        case Marca::Atrasado:
            o_fillCircle(g, cx, cy, r, fundoCarmim());
            aro(g, cx, cy, r, std::max(2, r / 6), carmim());
            habito(g, ic, cx, cy, std::max(6, arred(r * 0.58)), carmim(), fundoCarmim());
            break;
        case Marca::Perdido: {
            aro(g, cx, cy, r, std::max(2, r / 8), carmim());
            const double d = r * 0.38;
            linha(g, cx - d, cy - d, cx + d, cy + d, std::max(2.0, r / 6.0), carmim());
            linha(g, cx - d, cy + d, cx + d, cy - d, std::max(2.0, r / 6.0), carmim());
            break;
        }
        case Marca::Pulado: {
            for (int a = 0; a < 360; a += 30)   // contorno tracejado
                if ((a / 30) % 2 == 0)
                    linha(g, cx + r * std::cos(a * kPi / 180.0), cy + r * std::sin(a * kPi / 180.0),
                          cx + r * std::cos((a + 22) * kPi / 180.0), cy + r * std::sin((a + 22) * kPi / 180.0), 1.6, apagado());
            linha(g, cx - r * 0.4, cy, cx + r * 0.4, cy, std::max(2.0, r / 7.0), apagado());
            break;
        }
        case Marca::Pendente:
        default:
            aro(g, cx, cy, r, std::max(1, r / 8), tom >= 0 ? de888(uint32_t(tom)) : linhaSutil());
            habito(g, ic, cx, cy, std::max(6, arred(r * 0.55)), tom >= 0 ? de888(uint32_t(tom)) : apagado(), fundo);
            break;
        }
    }

    void ponto(Arduino_GFX& g, int cx, int cy, int r, Marca m, uint16_t fundo) {
        (void)fundo;
        switch (m) {
        case Marca::Feito: o_fillCircle(g, cx, cy, r, verde()); break;
        case Marca::Atual:
        case Marca::Atrasado: {
            const uint16_t c = m == Marca::Atual ? laranja() : carmim();
            aro(g, cx, cy, r + 2, 3, c);
            o_fillCircle(g, cx, cy, std::max(1, r - 3), c);
            break;
        }
        case Marca::Perdido: {
            aro(g, cx, cy, r, 2, carmim());
            linha(g, cx - r * 0.45, cy - r * 0.45, cx + r * 0.45, cy + r * 0.45, 1.6, carmim());
            linha(g, cx - r * 0.45, cy + r * 0.45, cx + r * 0.45, cy - r * 0.45, 1.6, carmim());
            break;
        }
        case Marca::Pulado:
            aro(g, cx, cy, r, 1, apagado());
            o_drawFastHLine(g, cx - r / 2, cy, r, apagado());
            break;
        case Marca::Pendente:
        default: aro(g, cx, cy, r, 2, rgb565(88, 90, 86)); break;
        }
    }

    void faiscas(Arduino_GFX& g, int cx, int cy, int raioAnel, double t) {
        t = std::clamp(t, 0.0, 1.0);
        if (t <= 0.01) return;
        for (int i = 0; i < 10; ++i) {
            const double a = (i * 36 + 8) * kPi / 180.0;
            const double r0 = raioAnel + 6, r1 = r0 + 8 * t;
            linha(g, cx + r0 * std::cos(a), cy + r0 * std::sin(a), cx + r1 * std::cos(a), cy + r1 * std::sin(a), 2.2,
                  i % 2 ? ambar() : verdeClaro());
        }
    }

} // namespace screens::icones
