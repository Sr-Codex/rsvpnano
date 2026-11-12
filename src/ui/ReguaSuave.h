#pragma once

// Primitivas de borda suavizada (cobertura por pixel) usadas pelo Inicio v4 do Tempo: retangulo arredondado com borda,
// marca vertical da regua, agulha (triangulo + traco + brilho). Logica pura: quem chama grava o pixel
// (`pixel(x, y, rgb565)`) e preenche retas com `rect(x, y, w, h, rgb565)`. Cores chapadas saem na grade do RGB565;
// valores intermediarios usam o pontilhado ordenado do AnelSuave (nada de fatias de degrade).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ui/AnelSuave.h"

namespace ui::regua {

    using Rgb = ui::anel::Rgb;

    inline Rgb deRgb565(uint16_t v) {
        return {float(((v >> 11) & 31) * 255 / 31), float(((v >> 5) & 63) * 255 / 63), float((v & 31) * 255 / 31)};
    }
    inline Rgb mistura(const Rgb& a, const Rgb& b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    }

    // Cobertura (0..1) do pixel de centro (px, py) por um retangulo arredondado [x, x+w) x [y, y+h) de raio r.
    inline float coberturaRetanguloArredondado(float px, float py, float x, float y, float w, float h, float r) {
        const float hw = w * 0.5f, hh = h * 0.5f;
        const float qx = std::fabs(px - (x + hw)) - (hw - r), qy = std::fabs(py - (y + hh)) - (hh - r);
        const float d = std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
        return std::clamp(0.5f - d, 0.0f, 1.0f);
    }

    // Retangulo arredondado cheio (cor `fill`) com borda opcional de `borda` px (cor `corBorda`), sobre `fundo` chapado.
    // Retas por `rect`, so os 4 cantos (r x r) sao pixel a pixel.
    template <typename Pixel, typename RectFn>
    void retanguloSuave(Pixel&& pixel, RectFn&& rect, int x, int y, int w, int h, int r, const Rgb& fill, const Rgb& fundo,
                        int borda = 0, const Rgb& corBorda = {}) {
        r = std::min(r, std::min(w, h) / 2);
        const uint16_t cFill = ui::anel::Anel::para565(ui::anel::Anel::grade(fill));
        const uint16_t cBorda = ui::anel::Anel::para565(ui::anel::Anel::grade(corBorda));
        if (borda > 0) {
            rect(x + r, y, w - 2 * r, h, cBorda);
            rect(x, y + r, r, h - 2 * r, cBorda);
            rect(x + w - r, y + r, r, h - 2 * r, cBorda);
            rect(x + r, y + borda, w - 2 * r, h - 2 * borda, cFill);
            rect(x + borda, y + r, r - borda, h - 2 * r, cFill);
            rect(x + w - r, y + r, r - borda, h - 2 * r, cFill);
        } else {
            rect(x + r, y, w - 2 * r, h, cFill);
            rect(x, y + r, r, h - 2 * r, cFill);
            rect(x + w - r, y + r, r, h - 2 * r, cFill);
        }
        const int xs[2] = {x, x + w - r}, ys[2] = {y, y + h - r};
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
                for (int cy = ys[j]; cy < ys[j] + r; ++cy)
                    for (int cx = xs[i]; cx < xs[i] + r; ++cx) {
                        const float px = cx + 0.5f, py = cy + 0.5f;
                        const float fora = coberturaRetanguloArredondado(px, py, float(x), float(y), float(w), float(h), float(r));
                        Rgb c = fundo;
                        if (borda > 0) {
                            c = mistura(c, corBorda, fora);
                            const float dentro = coberturaRetanguloArredondado(px, py, float(x + borda), float(y + borda),
                                                                               float(w - 2 * borda), float(h - 2 * borda),
                                                                               float(std::max(0, r - borda)));
                            c = mistura(c, fill, dentro);
                        } else {
                            c = mistura(c, fill, fora);
                        }
                        pixel(cx, cy, ui::anel::Anel::para565(c, cx, cy));
                    }
    }

    // Cobertura horizontal do pixel p (cobre [p, p+1)) por um traco [a0, a1].
    inline float coberturaFaixa(float a0, float a1, int p) {
        return std::clamp(std::min(a1, float(p + 1)) - std::max(a0, float(p)), 0.0f, 1.0f);
    }

    // Cobertura do pixel (x, y) por um triangulo (supersampling 4x4).
    inline float coberturaTriangulo(int x, int y, float ax, float ay, float bx, float by, float cx, float cy) {
        const auto lado = [](float px, float py, float x0, float y0, float x1, float y1) {
            return (px - x1) * (y0 - y1) - (x0 - x1) * (py - y1);
        };
        int dentro = 0;
        for (int j = 0; j < 4; ++j)
            for (int i = 0; i < 4; ++i) {
                const float px = x + (i + 0.5f) / 4, py = y + (j + 0.5f) / 4;
                const float d1 = lado(px, py, ax, ay, bx, by), d2 = lado(px, py, bx, by, cx, cy), d3 = lado(px, py, cx, cy, ax, ay);
                const bool neg = d1 < 0 || d2 < 0 || d3 < 0, pos = d1 > 0 || d2 > 0 || d3 > 0;
                if (!(neg && pos)) ++dentro;
            }
        return dentro / 16.0f;
    }

    // Integral da gaussiana (sigma) entre a e b: cobertura do brilho de uma faixa [a, b] desfocada.
    inline float gaussFaixa(float a, float b, float sigma) {
        const auto fi = [sigma](float z) { return 0.5f * (1.0f + std::erf(z / (sigma * 1.41421356f))); };
        return fi(b) - fi(a);
    }

    // Simbolo ∞ (laco duplo deitado) tracado com borda suavizada: curva parametrica x = a cos t, y = b sin 2t / ... amostrada em
    // N segmentos; a cobertura de cada pixel vem da distancia ao tracado (traco de `esp` px). Nao depende de glifo de fonte
    // nem de arquivo gerado. `larg` = largura total do simbolo (px), `alt` = altura total.
    struct Infinito {
        static constexpr int N = 56;
        float x[N + 1], y[N + 1];
        float meioEsp = 1.2f;
        float cx0 = 0, cy0 = 0, raioBusca = 0;

        Infinito(float cx, float cy, float larg, float alt, float esp) : meioEsp(esp * 0.5f), cx0(cx), cy0(cy) {
            const float a = (larg - esp) * 0.5f, b = (alt - esp) * 0.5f;
            for (int i = 0; i <= N; ++i) {
                const float t = 6.28318531f * float(i) / float(N);
                const float d = 1.0f + 0.55f * std::sin(t) * std::sin(t);   // laços redondos (lemniscata achatada no cruzamento)
                x[i] = cx + a * std::cos(t) / d * 1.0f;
                y[i] = cy + b * std::sin(t) * std::cos(t) * 2.0f / d;
            }
            raioBusca = std::max(a, b) + esp + 2.0f;
        }
        // Cobertura (0..1) do pixel de centro (px, py).
        float cobertura(float px, float py) const {
            float melhor = 1e9f;
            for (int i = 0; i < N; ++i) {
                const float vx = x[i + 1] - x[i], vy = y[i + 1] - y[i];
                const float wx = px - x[i], wy = py - y[i];
                const float len2 = vx * vx + vy * vy;
                const float u = len2 > 0 ? std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f) : 0.0f;
                const float dx = wx - u * vx, dy = wy - u * vy;
                melhor = std::min(melhor, dx * dx + dy * dy);
            }
            return std::clamp(meioEsp + 0.5f - std::sqrt(melhor), 0.0f, 1.0f);
        }
    };

} // namespace ui::regua
