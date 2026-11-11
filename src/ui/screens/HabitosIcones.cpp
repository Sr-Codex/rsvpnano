#include "ui/screens/HabitosIcones.h"

#include <algorithm>
#include <cmath>
#include <vector>

#include "ui/Rgb565.h"
#include "ui/icons/PhosphorIcons.generated.h"

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

    namespace {
        using icons::phosphor::Id;

        Id idDoIcone(tempo::IconeHabito ic) {
            using I = tempo::IconeHabito;
            switch (ic) {
            case I::Haltere: return Id::Barbell;
            case I::Frasco: return Id::JarLabel;
            case I::Capsula: return Id::Pill;
            case I::Talher: return Id::ForkKnife;
            case I::Dente: return Id::Tooth;
            case I::Xicara: return Id::Coffee;
            case I::Gota: return Id::Drop;
            case I::Lotus: return Id::FlowerLotus;
            case I::Livro: return Id::BookOpen;
            case I::Cerebro: return Id::Brain;
            case I::Plano: return Id::CalendarCheck;
            case I::Proibido: return Id::Prohibit;
            case I::Sol: return Id::Sun;
            case I::CheckDiario: return Id::CheckCircle;
            case I::Generico:
            default: return Id::Star;
            }
        }

        // Mascara do icone no lado mais proximo de `lado` (empate -> o maior); nullptr se o icone nao existe.
        const icons::phosphor::Mascara* achar(Id id, int lado) {
            const icons::phosphor::Mascara* melhor = nullptr;
            for (const auto& m : icons::phosphor::kMascaras) {
                if (m.id != id) continue;
                if (!melhor || std::abs(int(m.lado) - lado) < std::abs(int(melhor->lado) - lado) ||
                    (std::abs(int(m.lado) - lado) == std::abs(int(melhor->lado) - lado) && m.lado > melhor->lado))
                    melhor = &m;
            }
            return melhor;
        }

        // Mistura alfa (0..15) de `cor` sobre `fundo`, canal a canal em RGB565.
        inline uint16_t misturar(uint16_t cor, uint16_t fundo, unsigned a) {
            const unsigned ia = 15 - a;
            const unsigned r = (((cor >> 11) & 31) * a + ((fundo >> 11) & 31) * ia + 7) / 15;
            const unsigned gg = (((cor >> 5) & 63) * a + ((fundo >> 5) & 63) * ia + 7) / 15;
            const unsigned b = ((cor & 31) * a + (fundo & 31) * ia + 7) / 15;
            return uint16_t((r << 11) | (gg << 5) | b);
        }
    } // namespace

    uint16_t misturarAlfa(uint16_t cor, uint16_t fundo, unsigned alfa4) { return misturar(cor, fundo, std::min(15u, alfa4)); }

    void phosphor(Arduino_GFX& g, icons::phosphor::Id id, int cx, int cy, int lado, uint16_t cor, uint16_t fundo) {
        const auto* m = achar(id, lado);
        if (!m) return;
        const int x0 = cx - m->lado / 2 + gDx, y0 = cy - m->lado / 2 + gDy;
        const uint8_t* p = m->alfa;
        for (int y = 0; y < m->lado; ++y) {
            for (int x = 0; x < m->lado; x += 2) {
                const uint8_t b = *p++;
                for (int k = 0; k < 2; ++k) {
                    const unsigned a = k == 0 ? (b >> 4) : (b & 15);
                    if (a) g.drawPixel(x0 + x + k, y0 + y, a == 15 ? cor : misturar(cor, fundo, a));
                }
            }
        }
    }

    void visto(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor, uint16_t fundo) {
        phosphor(g, Id::Check, cx, cy, 2 * s, cor, fundo);
    }
    void vistoDisco(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor, uint16_t fundo) {
        phosphor(g, Id::CheckCircle, cx, cy, 2 * s, cor, fundo);
    }
    void pular(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor, uint16_t fundo) {
        phosphor(g, Id::SkipForward, cx, cy, 2 * s, cor, fundo);
    }
    void chama(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor, uint16_t fundo) {
        phosphor(g, Id::Fire, cx, cy, 2 * s, cor, fundo);
    }
    void desfazer(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor, uint16_t fundo) {
        phosphor(g, Id::ArrowCounterClockwise, cx, cy, 2 * s, cor, fundo);
    }

    void habito(Arduino_GFX& g, tempo::IconeHabito ic, int cx, int cy, int s, uint16_t cor, uint16_t fundo) {
        phosphor(g, idDoIcone(ic), cx, cy, 2 * s, cor, fundo);
    }

    uint16_t de888(uint32_t rgb) { return rgb565(uint8_t(rgb >> 16), uint8_t(rgb >> 8), uint8_t(rgb)); }

    void marcador(Arduino_GFX& g, tempo::IconeHabito ic, int cx, int cy, int r, Marca m, uint16_t fundo, int32_t tom) {
        switch (m) {
        case Marca::Feito:
            o_fillCircle(g, cx, cy, r, verde());
            vistoDisco(g, cx, cy, std::max(5, arred(r * 0.7)), tinta(), verde());
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
