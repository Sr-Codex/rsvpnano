#pragma once

// Adaptador de ui/FormaSuave.h para Arduino_GFX: substitutos suavizados (com borda por cobertura de pixel) de
// fillRoundRect / drawRoundRect / fillCircle / drawCircle / fillTriangle. Todos recebem a cor de fundo (RGB565) por tras
// da forma, que e a cor com que a borda suave se mistura.

#include <Arduino_GFX_Library.h>

#include "ui/FormaSuave.h"

namespace ui::forma {

    namespace detalhe {
        template <typename F>
        void com(Arduino_GFX& g, F&& f) {
            g.startWrite();
            f();
            g.endWrite();
        }
        inline auto pixel(Arduino_GFX& g) {
            return [&g](int px, int py, uint16_t c) { g.writePixel(int16_t(px), int16_t(py), c); };
        }
        inline auto rect(Arduino_GFX& g) {
            return [&g](int rx, int ry, int rw, int rh, uint16_t c) {
                if (rw > 0 && rh > 0) g.writeFillRect(int16_t(rx), int16_t(ry), int16_t(rw), int16_t(rh), c);
            };
        }
        inline auto hlinha(Arduino_GFX& g) {
            return [&g](int hx, int hy, int hw, uint16_t c) {
                if (hw > 0) g.writeFastHLine(int16_t(hx), int16_t(hy), int16_t(hw), c);
            };
        }
    } // namespace detalhe

    // Retangulo arredondado cheio, com borda opcional de `borda` px (cor `corBorda`), sobre `fundo`.
    inline void retanguloArredondado(Arduino_GFX& g, int x, int y, int w, int h, int r, uint16_t cor, uint16_t fundo, int borda = 0,
                                     uint16_t corBorda = 0) {
        if (w <= 0 || h <= 0) return;
        if (r <= 0) {
            g.fillRect(x, y, w, h, borda > 0 ? corBorda : cor);
            if (borda > 0) g.fillRect(x + borda, y + borda, w - 2 * borda, h - 2 * borda, cor);
            return;
        }
        detalhe::com(g, [&] {
            retanguloSuave(detalhe::pixel(g), detalhe::rect(g), x, y, w, h, r, deRgb565(cor), deRgb565(fundo), borda, deRgb565(corBorda));
        });
    }

    // Contorno de `esp` px por dentro da borda; o miolo nao e tocado (substitui drawRoundRect).
    inline void contornoArredondado(Arduino_GFX& g, int x, int y, int w, int h, int r, uint16_t cor, uint16_t fundoFora,
                                    uint16_t fundoDentro, int esp = 1) {
        if (w <= 0 || h <= 0) return;
        detalhe::com(g, [&] {
            contornoSuave(detalhe::pixel(g), detalhe::rect(g), x, y, w, h, r, esp, deRgb565(cor), deRgb565(fundoFora), deRgb565(fundoDentro));
        });
    }

    // Disco cheio de raio r centrado em (cx, cy) (substitui fillCircle).
    inline void disco(Arduino_GFX& g, int cx, int cy, int r, uint16_t cor, uint16_t fundo) {
        if (r < 0) return;
        detalhe::com(g, [&] { discoSuave(detalhe::pixel(g), detalhe::hlinha(g), cx, cy, r, deRgb565(cor), deRgb565(fundo)); });
    }

    // Contorno de circulo de raio r e espessura `esp` (substitui drawCircle).
    inline void circulo(Arduino_GFX& g, int cx, int cy, int r, uint16_t cor, uint16_t fundoFora, uint16_t fundoDentro, int esp = 1) {
        if (r <= 0) return;
        detalhe::com(g, [&] { aneloSuave(detalhe::pixel(g), cx, cy, r, esp, deRgb565(cor), deRgb565(fundoFora), deRgb565(fundoDentro)); });
    }

    // Triangulo cheio (substitui fillTriangle).
    inline void triangulo(Arduino_GFX& g, int ax, int ay, int bx, int by, int cx, int cy, uint16_t cor, uint16_t fundo) {
        detalhe::com(g, [&] { trianguloSuave(detalhe::pixel(g), ax, ay, bx, by, cx, cy, deRgb565(cor), deRgb565(fundo)); });
    }

    // Traco de `esp` px ao longo de uma polilinha (xs[i], ys[i] em coordenadas de pixel: o centro do pixel inteiro (x, y) e (x + 0,5; y + 0,5)).
    inline void polilinha(Arduino_GFX& g, const float* xs, const float* ys, int n, float esp, uint16_t cor, uint16_t fundo) {
        detalhe::com(g, [&] { polilinhaSuave(detalhe::pixel(g), xs, ys, n, esp, deRgb565(cor), deRgb565(fundo)); });
    }

    // Traco de progresso (0..1) percorrendo o perimetro do retangulo arredondado, faixa de `esp` px por dentro da borda.
    inline void tracoDoPerimetro(Arduino_GFX& g, int x, int y, int w, int h, int r, int esp, double frac, uint16_t cor,
                                 uint16_t fundoFora, uint16_t fundoDentro) {
        detalhe::com(g, [&] {
            tracoPerimetro(detalhe::pixel(g), x, y, w, h, r, esp, frac, deRgb565(cor), deRgb565(fundoFora), deRgb565(fundoDentro));
        });
    }

} // namespace ui::forma
