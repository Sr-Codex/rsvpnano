#include "ui/AnelSuave.h"

#include <algorithm>
#include <cmath>

namespace ui::anel {

    namespace {
        constexpr float kPi = 3.14159265358979f;

        float clamp01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }

        Rgb mistura(const Rgb& a, const Rgb& b, float t) {
            t = clamp01(t);
            return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
        }

        // Cobertura de um disco de raio r (centro em ox, oy) para o pixel em (x, y). Longe do disco: 0 sem raiz quadrada.
        float coberturaDisco(float x, float y, float ox, float oy, float r) {
            const float ddx = x - ox, ddy = y - oy, d2 = ddx * ddx + ddy * ddy, lim = r + 0.5f;
            if (d2 >= lim * lim) return 0.0f;
            return clamp01(r - std::sqrt(d2) + 0.5f);
        }
    } // namespace

    uint16_t Anel::para565(const Rgb& c) {
        const auto canal = [](float v) { return static_cast<uint8_t>(std::lround(std::clamp(v, 0.0f, 255.0f))); };
        const uint8_t r = canal(c.r), g = canal(c.g), b = canal(c.b);
        return static_cast<uint16_t>(((r & 0xF8U) << 8) | ((g & 0xFCU) << 3) | (b >> 3));
    }

    // Pontilhado ordenado (Bayer 4x4): um valor entre dois niveis do RGB565 vira uma mistura fina dos dois vizinhos,
    // na proporcao certa. Valores ja na grade (multiplos de 8/4/8, ver `grade`) saem exatos, sem ruido.
    uint16_t Anel::para565(const Rgb& c, int x, int y) {
        static constexpr uint8_t kBayer[4][4] = {{0, 8, 2, 10}, {12, 4, 14, 6}, {3, 11, 1, 9}, {15, 7, 13, 5}};
        const float limiar = (float(kBayer[y & 3][x & 3]) + 0.5f) / 16.0f;
        const auto nivel = [limiar](float v, float passo, int maximo) {
            const int n = int(std::floor((std::clamp(v, 0.0f, 255.0f) + limiar * passo) / passo));
            return n > maximo ? maximo : n;
        };
        const int r = nivel(c.r, 8.0f, 31), g = nivel(c.g, 4.0f, 63), b = nivel(c.b, 8.0f, 31);
        return static_cast<uint16_t>((r << 11) | (g << 5) | b);
    }

    // Cor "na grade" do RGB565 (truncada como `ui::themes::rgb565`): cores chapadas (fundo, trilho, pontas) saem
    // identicas ao resto da tela mesmo com o pontilhado.
    Rgb Anel::grade(const Rgb& c) {
        const auto q = [](float v, int mascara) { return float(int(std::clamp(v, 0.0f, 255.0f)) & mascara); };
        return {q(c.r, 0xF8), q(c.g, 0xFC), q(c.b, 0xF8)};
    }

    void Anel::montarArco(Arco& a, float tFim, Rgb a0, Rgb a1, float kMul, bool pontas) const {
        a.tFim = tFim;
        a.a0 = a0;
        a.a1 = a1;
        a.kMul = kMul;
        a.pontas = pontas;
        a.a1Fim = mistura(a0, a1, std::min(tFim, 1.0f) * kMul);
        a.sx = 0.0f;
        a.sy = -raio_;
        const float ang = (-90.0f + 360.0f * std::min(tFim, 1.0f)) * kPi / 180.0f;
        a.ex = raio_ * std::cos(ang);
        a.ey = raio_ * std::sin(ang);
    }

    Anel::Anel(const Params& p) : cx_(p.cx), cy_(p.cy) {
        raio_ = p.raio;
        hw_ = p.larg / 2.0f;
        ro_ = raio_ + hw_;
        ri_ = raio_ - hw_;
        fundo_ = grade(p.fundo);
        trilho_ = p.trilhoFixo != nullptr ? grade(*p.trilhoFixo) : grade(mistura(fundo_, grade(p.baseTrilho), 0.22f));

        const float frac = float(p.frac);
        if (frac > 0.0005f) {
            if (frac <= 1.0f) {
                montarArco(arcos_[nArcos_++], frac, grade(p.c0), grade(p.c1), 1.0f, true);
            } else {
                const bool base = p.volta1a0 != nullptr && p.volta1a1 != nullptr;
                montarArco(arcos_[nArcos_++], 1.0f, grade(base ? *p.volta1a0 : p.c0), grade(base ? *p.volta1a1 : p.c1), 1.0f, false);
                const float f2 = std::min(frac - 1.0f, 0.999f);
                // sombra escura sob/adiante da ponta da 2a volta (7 graus a frente, raio meia-largura + 1)
                const float angS = (-90.0f + 360.0f * f2 + 7.0f) * kPi / 180.0f;
                temSombra_ = true;
                sombraX_ = raio_ * std::cos(angS);
                sombraY_ = raio_ * std::sin(angS);
                sombraR_ = hw_ + 1.0f;
                sombraCor_ = grade(mistura(grade(p.c1), Rgb{0, 0, 0}, 0.6f));
                sombraAntesDoArco_ = 1;
                montarArco(arcos_[nArcos_++], f2, grade(p.c0), grade(p.c1), f2 > 0.0f ? 1.0f / f2 : 1.0f, true);
            }
        }

        const float alcance = ro_ + 2.0f;   // sombra vai ate ro + 1 (+ meio pixel de suavizacao)
        x0_ = cx_ - int(std::ceil(alcance));
        x1_ = cx_ + int(std::ceil(alcance));
        y0_ = cy_ - int(std::ceil(alcance));
        y1_ = cy_ + int(std::ceil(alcance));
        raioMiolo_ = std::max(0, int(std::floor(ri_)) - 2);
    }

    bool Anel::cor(int x, int y, Rgb& out) const {
        const float dx = float(x - cx_), dy = float(y - cy_);
        const float d = std::sqrt(dx * dx + dy * dy);
        if (d > ro_ + 2.0f) return false;
        if (d < ri_ - 1.0f) {   // miolo: so fundo
            out = fundo_;
            return true;
        }

        const float covR = clamp01(ro_ - d + 0.5f) * clamp01(d - ri_ + 0.5f);
        Rgb c = mistura(fundo_, trilho_, covR);

        // angulo a partir do topo, sentido horario, em [0, 1)
        float t = std::atan2(dy, dx) / (2.0f * kPi) + 0.25f;
        if (t < 0.0f) t += 1.0f;
        if (t >= 1.0f) t -= 1.0f;

        const auto aplicarSombra = [&]() {
            const float cov = coberturaDisco(dx, dy, sombraX_, sombraY_, sombraR_);
            if (cov <= 0.0f) return false;
            c = mistura(c, sombraCor_, cov);
            return true;
        };

        bool tocou = covR > 0.0f || d < ri_;   // a borda do miolo tambem e repintada (com o fundo)
        for (int i = 0; i < nArcos_; ++i) {
            if (temSombra_ && i == sombraAntesDoArco_ && aplicarSombra()) tocou = true;
            const Arco& a = arcos_[i];
            const bool dentro = a.tFim >= 1.0f || t <= a.tFim;
            const Rgb corFim = a.a1Fim;
            float covS = 0.0f, covE = 0.0f;
            if (a.pontas) {
                covS = coberturaDisco(dx, dy, a.sx, a.sy, hw_);
                covE = coberturaDisco(dx, dy, a.ex, a.ey, hw_);
            }
            // Perto do fim (ate meia volta antes), a ponta final e a faixa sao a mesma forma: uniao pela maior
            // cobertura (sem "calombo" na borda). Longe do fim -- quando o arco quase fecha e a ponta final alcanca o
            // inicio -- a ponta final vai POR CIMA, como no Apple Watch.
            const bool fimPorCima = !dentro || (a.tFim - t) > 0.5f;
            if (dentro) {
                const float cov = std::max(covR, std::max(covS, fimPorCima ? 0.0f : covE));
                if (cov > 0.0f) {
                    c = mistura(c, mistura(a.a0, a.a1, t * a.kMul), cov);
                    tocou = true;
                }
            } else if (covS > 0.0f) {
                c = mistura(c, a.a0, covS);
                tocou = true;
            }
            if (fimPorCima && covE > 0.0f) {
                c = mistura(c, corFim, covE);
                tocou = true;
            }
        }
        if (!tocou) return false;
        out = c;
        return true;
    }

} // namespace ui::anel
