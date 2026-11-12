#pragma once

// Primitivas de borda suavizada (cobertura por pixel) usadas pelo Inicio v4 do Tempo: retangulo arredondado com borda,
// marca vertical da regua, agulha (triangulo + traco + brilho). Logica pura: quem chama grava o pixel
// (`pixel(x, y, rgb565)`) e preenche retas com `rect(x, y, w, h, rgb565)`. Cores chapadas saem na grade do RGB565;
// valores intermediarios usam o pontilhado ordenado do AnelSuave (nada de fatias de degrade).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ui/AnelSuave.h"
#include "ui/FormaSuave.h"

namespace ui::regua {

    // Retangulo arredondado, mistura e conversao RGB565 moram em ui/FormaSuave.h (comum a todo o firmware).
    using ui::forma::Rgb;
    using ui::forma::deRgb565;
    using ui::forma::mistura;
    using ui::forma::coberturaRetanguloArredondado;
    using ui::forma::retanguloSuave;

    // Cobertura horizontal do pixel p (cobre [p, p+1)) por um traco [a0, a1].
    inline float coberturaFaixa(float a0, float a1, int p) {
        return std::clamp(std::min(a1, float(p + 1)) - std::max(a0, float(p)), 0.0f, 1.0f);
    }

    using ui::forma::coberturaTriangulo;

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
