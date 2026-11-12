#pragma once

// Formas com borda suavizada (cobertura por pixel), logica pura e comum a todo o firmware: retangulo arredondado
// (cheio, com borda, so contorno), disco, anel, triangulo e traco que percorre o perimetro (contorno de progresso).
// Quem chama grava o pixel (`pixel(x, y, rgb565)`) e preenche retas/miolos com `rect(x, y, w, h, rgb565)`; o
// adaptador para Arduino_GFX fica em ui/FormaSuaveGfx.h. Custo: so a faixa dos cantos/bordas e calculada pixel a
// pixel; o miolo continua `rect` rapido. Cores chapadas saem na grade do RGB565; tons intermediarios usam o
// pontilhado ordenado do AnelSuave (nada de fatias).
//
// Convencao: o pixel (x, y) cobre [x, x+1) x [y, y+1); o centro e (x + 0,5; y + 0,5). `fundo*` = cor por tras (a borda
// suave mistura com ela, entao ela tem de ser conhecida/chapada).

#include <algorithm>
#include <cmath>
#include <cstdint>

#include "ui/AnelSuave.h"

namespace ui::forma {

    using Rgb = ui::anel::Rgb;

    inline Rgb deRgb565(uint16_t v) {
        return {float(((v >> 11) & 31) * 255 / 31), float(((v >> 5) & 63) * 255 / 63), float((v & 31) * 255 / 31)};
    }
    inline Rgb mistura(const Rgb& a, const Rgb& b, float t) {
        t = std::clamp(t, 0.0f, 1.0f);
        return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
    }
    inline uint16_t para565(const Rgb& c, int x, int y) { return ui::anel::Anel::para565(c, x, y); }
    inline uint16_t chapada565(const Rgb& c) { return ui::anel::Anel::para565(ui::anel::Anel::grade(c)); }

    // Mistura a->b em t; nos extremos devolve a cor chapada na grade do RGB565 (sem pontilhado: um miolo de cor fora da grade
    // ficaria com manchas de pontilhado diferentes do resto do preenchimento), no meio usa o pontilhado ordenado.
    inline uint16_t mistura565(const Rgb& a, const Rgb& b, float t, int x, int y) {
        if (t <= 0.001f) return chapada565(a);
        if (t >= 0.999f) return chapada565(b);
        return para565(mistura(a, b, t), x, y);
    }

    // Distancia com sinal (negativa dentro) do ponto ate um retangulo arredondado [x, x+w) x [y, y+h) de raio r.
    inline float distanciaRetanguloArredondado(float px, float py, float x, float y, float w, float h, float r) {
        const float hw = w * 0.5f, hh = h * 0.5f;
        const float qx = std::fabs(px - (x + hw)) - (hw - r), qy = std::fabs(py - (y + hh)) - (hh - r);
        return std::hypot(std::max(qx, 0.0f), std::max(qy, 0.0f)) + std::min(std::max(qx, qy), 0.0f) - r;
    }

    // Cobertura (0..1) do pixel de centro (px, py) por um retangulo arredondado [x, x+w) x [y, y+h) de raio r.
    inline float coberturaRetanguloArredondado(float px, float py, float x, float y, float w, float h, float r) {
        return std::clamp(0.5f - distanciaRetanguloArredondado(px, py, x, y, w, h, r), 0.0f, 1.0f);
    }

    // Retangulo arredondado cheio (cor `fill`) com borda opcional de `borda` px (cor `corBorda`), sobre `fundo` chapado.
    // Retas por `rect`, so os 4 cantos (r x r) sao pixel a pixel.
    template <typename Pixel, typename RectFn>
    void retanguloSuave(Pixel&& pixel, RectFn&& rect, int x, int y, int w, int h, int r, const Rgb& fill, const Rgb& fundo,
                        int borda = 0, const Rgb& corBorda = {}) {
        r = std::min(r, std::min(w, h) / 2);
        const uint16_t cFill = chapada565(fill);
        const uint16_t cBorda = chapada565(corBorda);
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
                        if (borda > 0) {
                            const float dentro = coberturaRetanguloArredondado(px, py, float(x + borda), float(y + borda),
                                                                               float(w - 2 * borda), float(h - 2 * borda),
                                                                               float(std::max(0, r - borda)));
                            if (dentro >= 0.999f) pixel(cx, cy, cFill);
                            else if (fora <= 0.001f) pixel(cx, cy, chapada565(fundo));
                            else if (fora >= 0.999f && dentro <= 0.001f) pixel(cx, cy, cBorda);
                            else pixel(cx, cy, para565(mistura(mistura(fundo, corBorda, fora), fill, dentro), cx, cy));
                        } else {
                            pixel(cx, cy, mistura565(fundo, fill, fora, cx, cy));
                        }
                    }
    }

    // Somente o contorno (faixa de `esp` px por dentro da borda do retangulo arredondado), o resto do retangulo fica como
    // esta (miolo nao e tocado). `fundoFora` = cor por tras da borda externa; `fundoDentro` = cor logo por dentro da faixa
    // (usada so para suavizar a borda interna). Substitui drawRoundRect.
    template <typename Pixel, typename RectFn>
    void contornoSuave(Pixel&& pixel, RectFn&& rect, int x, int y, int w, int h, int r, int esp, const Rgb& cor,
                       const Rgb& fundoFora, const Rgb& fundoDentro) {
        r = std::min(r, std::min(w, h) / 2);
        esp = std::max(1, std::min(esp, std::min(w, h) / 2));
        const uint16_t cCor = chapada565(cor);
        rect(x + r, y, w - 2 * r, esp, cCor);
        rect(x + r, y + h - esp, w - 2 * r, esp, cCor);
        rect(x, y + r, esp, h - 2 * r, cCor);
        rect(x + w - esp, y + r, esp, h - 2 * r, cCor);
        const int xs[2] = {x, x + w - r}, ys[2] = {y, y + h - r};
        const int ri = std::max(0, r - esp);
        for (int j = 0; j < 2; ++j)
            for (int i = 0; i < 2; ++i)
                for (int cy = ys[j]; cy < ys[j] + r; ++cy)
                    for (int cx = xs[i]; cx < xs[i] + r; ++cx) {
                        const float px = cx + 0.5f, py = cy + 0.5f;
                        const float fora = coberturaRetanguloArredondado(px, py, float(x), float(y), float(w), float(h), float(r));
                        const float dentro = coberturaRetanguloArredondado(px, py, float(x + esp), float(y + esp),
                                                                           float(w - 2 * esp), float(h - 2 * esp), float(ri));
                        if (fora <= 0.0f) continue;      // fora do canto: nao toca
                        if (dentro >= 1.0f) continue;    // miolo: nao toca
                        if (fora >= 0.999f && dentro <= 0.001f) { pixel(cx, cy, cCor); continue; }
                        pixel(cx, cy, para565(mistura(mistura(fundoFora, cor, fora), fundoDentro, dentro), cx, cy));
                    }
    }

    // Disco cheio de raio `r` centrado no pixel (cx, cy) (mesmo tamanho de fillCircle: diametro 2r+1), borda suavizada.
    // O miolo sai em fiadas (`hline(x, y, w, rgb565)`), so a borda e pixel a pixel.
    template <typename Pixel, typename HLine>
    void discoSuave(Pixel&& pixel, HLine&& hline, int cx, int cy, int r, const Rgb& cor, const Rgb& fundo) {
        const float R = float(r) + 0.5f;
        const float cheio = std::max(R - 0.5f, 0.0f);
        const uint16_t cCor = chapada565(cor);
        const int ext = r + 1;
        for (int j = -ext; j <= ext; ++j) {
            const float miolo2 = cheio * cheio - float(j * j);
            const int imax = miolo2 >= 0 ? int(std::floor(std::sqrt(miolo2))) : -1;
            if (imax >= 0) hline(cx - imax, cy + j, 2 * imax + 1, cCor);
            for (int i = -ext; i <= ext; ++i) {
                if (std::abs(i) <= imax) continue;
                const float cob = std::clamp(R + 0.5f - std::hypot(float(i), float(j)), 0.0f, 1.0f);
                if (cob <= 0.0f) continue;
                pixel(cx + i, cy + j, mistura565(fundo, cor, cob, cx + i, cy + j));
            }
        }
    }

    // Anel (contorno de circulo) de `esp` px centrado em (cx, cy): equivale a drawCircle de raio r com esp = 1.
    // `fundoFora` por tras da borda externa, `fundoDentro` por dentro; o miolo nao e tocado.
    template <typename Pixel>
    void aneloSuave(Pixel&& pixel, int cx, int cy, int r, int esp, const Rgb& cor, const Rgb& fundoFora, const Rgb& fundoDentro) {
        const float Ro = float(r) + 0.5f, Ri = Ro - float(std::max(1, esp));
        const int ext = r + 1;
        for (int j = -ext; j <= ext; ++j)
            for (int i = -ext; i <= ext; ++i) {
                const float d = std::hypot(float(i), float(j));
                const float fora = std::clamp(Ro + 0.5f - d, 0.0f, 1.0f), dentro = std::clamp(Ri + 0.5f - d, 0.0f, 1.0f);
                if (fora <= 0.0f || dentro >= 1.0f) continue;
                if (fora >= 0.999f && dentro <= 0.001f) { pixel(cx + i, cy + j, chapada565(cor)); continue; }
                pixel(cx + i, cy + j, para565(mistura(mistura(fundoFora, cor, fora), fundoDentro, dentro), cx + i, cy + j));
            }
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

    // Triangulo cheio (vertices em coordenadas de pixel inteiras, como fillTriangle) com borda suavizada.
    template <typename Pixel>
    void trianguloSuave(Pixel&& pixel, int ax, int ay, int bx, int by, int cx, int cy, const Rgb& cor, const Rgb& fundo) {
        // vertices no centro do pixel, como o fillTriangle (que inclui os pixels dos vertices)
        const float fax = ax + 0.5f, fay = ay + 0.5f, fbx = bx + 0.5f, fby = by + 0.5f, fcx = cx + 0.5f, fcy = cy + 0.5f;
        const int x0 = std::min({ax, bx, cx}), x1 = std::max({ax, bx, cx}), y0 = std::min({ay, by, cy}), y1 = std::max({ay, by, cy});
        for (int y = y0 - 1; y <= y1 + 1; ++y)
            for (int x = x0 - 1; x <= x1 + 1; ++x) {
                const float cob = coberturaTriangulo(x, y, fax, fay, fbx, fby, fcx, fcy);
                if (cob <= 0.0f) continue;
                pixel(x, y, cob >= 1.0f ? chapada565(cor) : para565(mistura(fundo, cor, cob), x, y));
            }
    }

    // Traco de `esp` px ao longo de uma polilinha (n pontos em coordenadas de pixel, centro = +0,5), pontas redondas, borda
    // suavizada (cobertura pela distancia ao traco). Serve para bocas, arcos e curvas finas amostradas.
    template <typename Pixel>
    void polilinhaSuave(Pixel&& pixel, const float* xs, const float* ys, int n, float esp, const Rgb& cor, const Rgb& fundo) {
        if (n < 1) return;
        const float m = esp * 0.5f + 1.0f;
        float x0 = xs[0], x1 = xs[0], y0 = ys[0], y1 = ys[0];
        for (int i = 1; i < n; ++i) {
            x0 = std::min(x0, xs[i]); x1 = std::max(x1, xs[i]);
            y0 = std::min(y0, ys[i]); y1 = std::max(y1, ys[i]);
        }
        for (int y = int(std::floor(y0 - m)); y <= int(std::ceil(y1 + m)); ++y)
            for (int x = int(std::floor(x0 - m)); x <= int(std::ceil(x1 + m)); ++x) {
                const float px = x + 0.5f, py = y + 0.5f;
                float melhor = 1e9f;
                for (int i = 0; i < std::max(1, n - 1); ++i) {
                    const float ax = xs[i], ay = ys[i], bx = xs[std::min(i + 1, n - 1)], by = ys[std::min(i + 1, n - 1)];
                    const float vx = bx - ax, vy = by - ay, wx = px - ax, wy = py - ay, len2 = vx * vx + vy * vy;
                    const float u = len2 > 0 ? std::clamp((wx * vx + wy * vy) / len2, 0.0f, 1.0f) : 0.0f;
                    const float dx = wx - u * vx, dy = wy - u * vy;
                    melhor = std::min(melhor, dx * dx + dy * dy);
                }
                const float cob = std::clamp(esp * 0.5f + 0.5f - std::sqrt(melhor), 0.0f, 1.0f);
                if (cob <= 0.0f) continue;
                pixel(x, y, cob >= 1.0f ? chapada565(cor) : para565(mistura(fundo, cor, cob), x, y));
            }
    }

    // ---- traco que percorre o perimetro de um retangulo arredondado (contorno de progresso) -------------------------
    // Caminho = linha central de uma faixa de `esp` px por dentro da borda; comeca no meio do topo e vai no sentido
    // horario. Comprimento = frac * perimetro, pontas redondas.
    struct Perimetro {
        float cx, cy;        // centro do retangulo
        float a, b, rp;      // meia-reta horizontal / vertical da linha central e raio do canto dela
        float ca, total;     // comprimento de um canto, perimetro total
        float meiaEsp;

        Perimetro(int x, int y, int w, int h, int r, int esp) {
            r = std::min(r, std::min(w, h) / 2);
            meiaEsp = float(esp) * 0.5f;
            cx = x + w * 0.5f;
            cy = y + h * 0.5f;
            rp = std::max(float(r) - meiaEsp, 0.0f);
            a = std::max(w * 0.5f - meiaEsp - rp, 0.0f);
            b = std::max(h * 0.5f - meiaEsp - rp, 0.0f);
            ca = 1.57079633f * rp;
            total = 4 * a + 4 * b + 4 * ca;
        }

        // Ponto da linha central a distancia s do inicio.
        void ponto(float s, float& px, float& py) const {
            s = std::clamp(s, 0.0f, total);
            const float k = 1.57079633f;
            const auto arco = [&](float t, float ox, float oy, float a0) {   // a0 = angulo inicial (0 = topo, horario)
                const float ang = rp > 0 ? a0 + t / rp : a0;
                px = cx + ox + rp * std::sin(ang);
                py = cy + oy - rp * std::cos(ang);
            };
            if (s <= a) { px = cx + s; py = cy - (b + rp); return; }
            s -= a;
            if (s <= ca) { arco(s, a, -b, 0.0f); return; }
            s -= ca;
            if (s <= 2 * b) { px = cx + a + rp; py = cy - b + s; return; }
            s -= 2 * b;
            if (s <= ca) { arco(s, a, b, k); return; }
            s -= ca;
            if (s <= 2 * a) { px = cx + a - s; py = cy + b + rp; return; }
            s -= 2 * a;
            if (s <= ca) { arco(s, -a, b, 2 * k); return; }
            s -= ca;
            if (s <= 2 * b) { px = cx - a - rp; py = cy + b - s; return; }
            s -= 2 * b;
            if (s <= ca) { arco(s, -a, -b, 3 * k); return; }
            s -= ca;
            px = cx - a + s; py = cy - (b + rp);
        }

        // Distancia perpendicular ate a linha central e posicao `s` da projecao do ponto (px, py).
        void projeta(float px, float py, float& dperp, float& s) const {
            const float dx = px - cx, dy = py - cy;
            const float u = std::fabs(dx) - a, v = std::fabs(dy) - b;
            const float k = ca;
            dperp = std::fabs(std::hypot(std::max(u, 0.0f), std::max(v, 0.0f)) + std::min(std::max(u, v), 0.0f) - rp);
            if (u > 0 && v > 0) {   // canto
                const float uu = u, vv = v;
                if (dx > 0 && dy < 0) s = a + rp * std::atan2(uu, vv);
                else if (dx > 0) s = a + k + 2 * b + rp * std::atan2(vv, uu);
                else if (dy > 0) s = a + 2 * k + 2 * b + 2 * a + rp * std::atan2(uu, vv);
                else s = a + 3 * k + 4 * b + 2 * a + rp * std::atan2(vv, uu);
            } else if (u > v) {     // lado direito / esquerdo
                if (dx > 0) s = a + k + (std::clamp(dy, -b, b) + b);
                else s = a + 3 * k + 2 * b + 2 * a + (b - std::clamp(dy, -b, b));
            } else {                // topo / base
                if (dy < 0) s = dx >= 0 ? std::clamp(dx, 0.0f, a) : total + std::clamp(dx, -a, 0.0f);
                else s = a + 2 * k + 2 * b + (a - std::clamp(dx, -a, a));
            }
        }
    };

    // Cobertura (0..1) do pixel de centro (px, py) pelo traco de progresso `frac` (0..1) do perimetro. frac >= 1: volta inteira.
    inline float coberturaTracoPerimetro(const Perimetro& per, float frac, float px, float py) {
        if (frac <= 0.0f) return 0.0f;
        float dperp, s;
        per.projeta(px, py, dperp, s);
        float dist = dperp;
        if (frac < 1.0f) {
            const float S = per.total * frac;
            if (s < 0.0f || s > S) {
                float ex, ey, sx, sy;
                per.ponto(S, ex, ey);
                per.ponto(0.0f, sx, sy);
                dist = std::min(std::hypot(px - ex, py - ey), std::hypot(px - sx, py - sy));
            }
        }
        return std::clamp(per.meiaEsp + 0.5f - dist, 0.0f, 1.0f);
    }

    // Pinta o traco de progresso sobre o retangulo arredondado (x, y, w, h, r): faixa de `esp` px por dentro da borda.
    // Por baixo dele esta o retangulo (cor `fundoDentro`, ou o trilho da mesma largura) sobre `fundoFora`.
    template <typename Pixel>
    void tracoPerimetro(Pixel&& pixel, int x, int y, int w, int h, int r, int esp, double frac, const Rgb& cor,
                        const Rgb& fundoFora, const Rgb& fundoDentro) {
        if (frac <= 0.0) return;
        r = std::min(r, std::min(w, h) / 2);
        const Perimetro per(x, y, w, h, r, esp);
        const float f = float(std::min(frac, 1.0));
        const int faixa = esp + 1;
        const auto um = [&](int cx, int cy) {
            const float px = cx + 0.5f, py = cy + 0.5f;
            const float cob = coberturaTracoPerimetro(per, f, px, py);
            if (cob <= 0.0f) return;
            const float baixo = coberturaRetanguloArredondado(px, py, float(x), float(y), float(w), float(h), float(r));
            const Rgb sob = mistura(fundoFora, fundoDentro, baixo);
            pixel(cx, cy, cob >= 0.999f ? chapada565(cor) : para565(mistura(sob, cor, cob), cx, cy));
        };
        for (int cy = y; cy < y + h; ++cy) {
            if (cy < y + faixa || cy >= y + h - faixa) {
                for (int cx = x; cx < x + w; ++cx) um(cx, cy);
            } else {
                for (int cx = x; cx < x + faixa; ++cx) um(cx, cy);
                for (int cx = x + w - faixa; cx < x + w; ++cx) um(cx, cy);
            }
        }
    }

} // namespace ui::forma
