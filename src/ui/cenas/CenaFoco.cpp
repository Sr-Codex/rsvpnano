#include "ui/cenas/CenaFoco.h"

#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <string_view>
#include <utility>

#include "ui/SansFont.h"
#include "ui/Ui.h"

namespace ui::cenas {

    namespace {
        using tempo::cenafoco::Fundo;
        using tempo::cenafoco::Quadro;
        using tempo::cenafoco::TipoNo;

        constexpr const char* kTag = "cena";

        void* psram(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }

        template<typename T>
        void soltar(T*& p) {
            if (p != nullptr) heap_caps_free(p);
            p = nullptr;
        }

        // Pontilhado de ruido azul (o mesmo dos mockups, tools/mockup_horizonte5.py:para_tela): por canal,
        // nivel = floor(v / passo + limiar), limiar = kRuidoAzul64 / 256, passo 8 (5 bits) ou 4 (6 bits).
        inline uint16_t pontilhar(int r, int g, int b, int x, int y) {
            const int lim = kRuidoAzul64[((y & 63) << 6) | (x & 63)];
            const int R = std::min(31, (r * 32 + lim) >> 8);
            const int G = std::min(63, (g * 64 + lim) >> 8);
            const int B = std::min(31, (b * 32 + lim) >> 8);
            return static_cast<uint16_t>((R << 11) | (G << 5) | B);
        }

        struct Rgbf {
            float r = 0, g = 0, b = 0;
        };

        inline Rgbf de565(uint16_t c) {
            return {static_cast<float>((c >> 11) & 31) / 31.0f, static_cast<float>((c >> 5) & 63) / 63.0f,
                    static_cast<float>(c & 31) / 31.0f};
        }

        inline Rgbf mistura(const Rgbf& a, const Rgbf& b, float t) {
            return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
        }

        inline int canal8(float v) { return std::clamp(static_cast<int>(v * 255.0f + 0.5f), 0, 255); }

        inline uint16_t pontilhar(const Rgbf& c, int x, int y) { return pontilhar(canal8(c.r), canal8(c.g), canal8(c.b), x, y); }

        int aoBloco(JPEGDRAW* d) {
            auto* destino = static_cast<uint16_t*>(d->pUser);
            const auto* px = reinterpret_cast<const uint32_t*>(d->pPixels);   // RGB8888: R no byte baixo, depois G, B
            for (int linha = 0; linha < d->iHeight; ++linha) {
                const int y = d->y + linha;
                if (y < 0 || y >= CenaFoco::kAlt) continue;
                uint16_t* saida = destino + y * CenaFoco::kLarg;
                const uint32_t* entrada = px + linha * d->iWidth;
                for (int col = 0; col < d->iWidthUsed; ++col) {
                    const int x = d->x + col;
                    if (x < 0 || x >= CenaFoco::kLarg) continue;
                    const uint32_t p = entrada[col];
                    saida[x] = pontilhar(int(p & 0xFF), int((p >> 8) & 0xFF), int((p >> 16) & 0xFF), x, y);
                }
            }
            return 1;
        }

        // ---- desenho suavizado em um buffer RGB float (regiao do mapa)
        struct Tela {
            float* rgb;
            uint8_t* tocado;
            int w, h;

            void sobre(int x, int y, const Rgbf& c, float a) {   // "por cima" com cobertura a
                if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) return;
                a = std::min(a, 1.0f);
                float* p = rgb + (y * w + x) * 3;
                p[0] += (c.r - p[0]) * a;
                p[1] += (c.g - p[1]) * a;
                p[2] += (c.b - p[2]) * a;
                tocado[y * w + x] = 1;
            }
            void luz(int x, int y, const Rgbf& c, float a) {     // brilho somado em "screen" (como o bloom dos mockups)
                if (x < 0 || y < 0 || x >= w || y >= h || a <= 0.0f) return;
                float* p = rgb + (y * w + x) * 3;
                p[0] = 1.0f - (1.0f - p[0]) * (1.0f - std::min(1.0f, c.r * a));
                p[1] = 1.0f - (1.0f - p[1]) * (1.0f - std::min(1.0f, c.g * a));
                p[2] = 1.0f - (1.0f - p[2]) * (1.0f - std::min(1.0f, c.b * a));
                tocado[y * w + x] = 1;
            }
        };

        // Coordenadas "de borda": o pixel x cobre [x, x+1) (como o PIL a 4x dos mockups); centro em x + 0,5.
        // Cobertura de disco/anel por 4x4 amostras por pixel (exata o bastante para raios de 1,6..4,8 px).
        float coberturaAnel(int x, int y, float cx, float cy, float r0, float r1) {
            const float r02 = r0 * r0, r12 = r1 * r1;
            int dentro = 0;
            for (int j = 0; j < 4; ++j) {
                const float dy = static_cast<float>(y) + (static_cast<float>(j) + 0.5f) * 0.25f - cy;
                for (int i = 0; i < 4; ++i) {
                    const float dx = static_cast<float>(x) + (static_cast<float>(i) + 0.5f) * 0.25f - cx;
                    const float d2 = dx * dx + dy * dy;
                    dentro += (d2 <= r12 && d2 >= r02) ? 1 : 0;
                }
            }
            return static_cast<float>(dentro) / 16.0f;
        }

        // Linha de Wu (anti-aliasing): coordenadas com o centro do pixel em inteiro.
        template<typename Plot>
        void linhaWu(float x0, float y0, float x1, float y1, Plot&& plot) {
            const auto ipart = [](float v) { return std::floor(v); };
            const auto fpart = [](float v) { return v - std::floor(v); };
            const auto rfpart = [&](float v) { return 1.0f - fpart(v); };
            const bool ingreme = std::fabs(y1 - y0) > std::fabs(x1 - x0);
            if (ingreme) { std::swap(x0, y0); std::swap(x1, y1); }
            if (x0 > x1) { std::swap(x0, x1); std::swap(y0, y1); }
            const float dx = x1 - x0, dy = y1 - y0;
            const float grad = dx < 1e-6f ? 1.0f : dy / dx;
            const auto put = [&](float a, float b, float c) {
                if (ingreme) plot(static_cast<int>(b), static_cast<int>(a), c);
                else plot(static_cast<int>(a), static_cast<int>(b), c);
            };
            // ponta 1
            float xend = std::round(x0);
            float yend = y0 + grad * (xend - x0);
            float xgap = rfpart(x0 + 0.5f);
            const float xp1 = xend, yp1 = ipart(yend);
            put(xp1, yp1, rfpart(yend) * xgap);
            put(xp1, yp1 + 1, fpart(yend) * xgap);
            float intery = yend + grad;
            // ponta 2
            xend = std::round(x1);
            yend = y1 + grad * (xend - x1);
            xgap = fpart(x1 + 0.5f);
            const float xp2 = xend, yp2 = ipart(yend);
            put(xp2, yp2, rfpart(yend) * xgap);
            put(xp2, yp2 + 1, fpart(yend) * xgap);
            for (float x = xp1 + 1; x < xp2; x += 1.0f) {
                put(x, ipart(intery), rfpart(intery));
                put(x, ipart(intery) + 1, fpart(intery));
                intery += grad;
            }
        }

        // Brilho em volta de um no aceso: o "bloom" curto dos mockups (desfoque gaussiano sigma 2,2 do disco, ganho
        // 0,85), aproximado por uma gaussiana de mesma massa: pico r^2 / (2 s2), s2 = sigma^2 + r^2/4.
        constexpr float kBrilhoSigma = 2.2f, kBrilhoGanho = 0.85f, kBrilhoAlcance = 3.5f;
        // Anel de "conectando" (no desta sessao entre apagado e aceso): raio 4,5 px, 0,5 px, cor do no novo; forcas ajustadas
        // comparando com o mockup em zoom 4x.
        constexpr float kAnelRaio = 4.5f, kAnelMeiaLarg = 0.25f, kAnelForca = 0.5f;
        constexpr float kAnelBrilho = 0.16f, kAnelBrilhoSigma = 0.9f;

        // ================================================================ v2: cena viva (compositor por camadas)
        constexpr size_t kCapTile = size_t(CenaFoco::kLarg) * 32;   // faixa de composicao: ate 32 linhas da tela
        // "Por cima" na janela (sobreLuz): o que substitui o ceu nos mockups (gota, farol, luz de antena) aparece SOMADO ao
        // pixel final, porque quase todo o brilho do ceu vem do bloom (largo 1,25 + curto 0,85), nao do proprio emissivo.
        // Medido no mockup (render com e sem as gotas): (A - B) / (1 - B) = cor * cobertura -> alvo = screen(cor, pixel).
        constexpr float kAlvoFundo = 1.0f;
        // Traco de 0,5 px: no mockup o pico efetivo e ~0,67 (o realce do LANCZOS + o brilho curto da propria gota).
        constexpr float kRealceTraco = 1.35f;
        constexpr float kFantasmaSegmento = 0.025f;   // segmento apagado do LED ainda aparece de leve (painel de verdade)

        struct Rgb {
            float r = 0, g = 0, b = 0;
        };

        inline Rgb operator*(const Rgb& c, float k) { return {c.r * k, c.g * k, c.b * k}; }
        inline Rgb deCor(const Cor& c) { return {float(c.r), float(c.g), float(c.b)}; }
        inline Rgb misturaRgb(const Rgb& a, const Rgb& b, float t) {
            return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t};
        }
        inline float px8(int16_t v) { return static_cast<float>(v) / 8.0f; }

        // Fotografia do QG Cidade (tools/mockup_qg2.py:grade): saturacao e vinheta aplicadas a luz que a placa desenha.
        struct Grade {
            bool ativa = false;
            float sat = 1.0f;
            float vinheta(float x, float y) const {
                if (!ativa) return 1.0f;
                const float vy = (y - CenaFoco::kAlt * 0.48f) / (CenaFoco::kAlt * 0.75f);
                const float vx = (x - CenaFoco::kLarg * 0.6f) / (CenaFoco::kLarg * 0.7f);
                return 1.0f - 0.30f * std::pow(std::min(1.0f, vx * vx + vy * vy), 1.4f);
            }
            Rgb tratar(Rgb c, float x, float y) const {
                if (!ativa) return c;
                const float l = 0.299f * c.r + 0.587f * c.g + 0.114f * c.b;
                const float v = vinheta(x, y);
                return {(l + (c.r - l) * sat) * v, (l + (c.g - l) * sat) * v, (l + (c.b - l) * sat) * v};
            }
        };

        // Visibilidade da janela (moldura/persiana na frente da cidade). Fora do retangulo da janela = 1.
        struct Vis {
            const uint8_t* mapa = nullptr;
            Retangulo r{};
            float em(int x, int y) const {
                if (mapa == nullptr || x < r.x0 || y < r.y0 || x >= r.x1 || y >= r.y1) return 1.0f;
                return static_cast<float>(mapa[(y - r.y0) * (r.x1 - r.x0) + (x - r.x0)]) / 255.0f;
            }
            float emPonto(float x, float y) const { return em(static_cast<int>(std::floor(x)), static_cast<int>(std::floor(y))); }
        };

        // Caixa de pixels inteiros [x0, x1) x [y0, y1) que cobre [a0, a1) x [b0, b1), recortada a um limite.
        struct Faixa {
            int x0, y0, x1, y1;
            bool vazia() const { return x0 >= x1 || y0 >= y1; }
        };
        inline Faixa cobrir(float a0, float b0, float a1, float b1, const Faixa& limite) {
            Faixa f{static_cast<int>(std::floor(a0)), static_cast<int>(std::floor(b0)), static_cast<int>(std::ceil(a1)),
                    static_cast<int>(std::ceil(b1))};
            f.x0 = std::max(f.x0, limite.x0), f.y0 = std::max(f.y0, limite.y0);
            f.x1 = std::min(f.x1, limite.x1), f.y1 = std::min(f.y1, limite.y1);
            return f;
        }

        inline float sobreposicao(float a0, float a1, float b0, float b1) {
            return std::max(0.0f, std::min(a1, b1) - std::max(a0, b0));
        }

        // Cobertura (4x4 amostras) de uma elipse de centro (cx, cy) e raios rx, ry no pixel (x, y).
        float coberturaElipse(int x, int y, float cx, float cy, float rx, float ry) {
            if (rx <= 0 || ry <= 0) return 0.0f;
            int dentro = 0;
            for (int j = 0; j < 4; ++j) {
                const float dy = (static_cast<float>(y) + (static_cast<float>(j) + 0.5f) * 0.25f - cy) / ry;
                for (int i = 0; i < 4; ++i) {
                    const float dx = (static_cast<float>(x) + (static_cast<float>(i) + 0.5f) * 0.25f - cx) / rx;
                    dentro += dx * dx + dy * dy <= 1.0f ? 1 : 0;
                }
            }
            return static_cast<float>(dentro) / 16.0f;
        }

        // Desfoque gaussiano aproximado por 3 caixas (como o GaussianBlur do PIL dos mockups), com zeros fora da regiao.
        void caixasParaGauss(float sigma, int (&raios)[3]) {
            const float ideal = std::sqrt(12.0f * sigma * sigma / 3.0f + 1.0f);
            int wl = static_cast<int>(std::floor(ideal));
            if (wl % 2 == 0) --wl;
            const int wu = wl + 2;
            const float mIdeal = (12.0f * sigma * sigma - 3.0f * wl * wl - 4.0f * 3.0f * wl - 3.0f * 3.0f) / (-4.0f * wl - 4.0f);
            const int m = static_cast<int>(std::lround(mIdeal));
            for (int i = 0; i < 3; ++i) raios[i] = ((i < m ? wl : wu) - 1) / 2;
        }

        void caixaH(const float* src, float* dst, int w, int h, int r) {
            const float k = 1.0f / static_cast<float>(2 * r + 1);
            for (int y = 0; y < h; ++y) {
                const float* s = src + y * w;
                float* d = dst + y * w;
                float soma = 0;
                for (int x = 0; x <= r && x < w; ++x) soma += s[x];
                for (int x = 0; x < w; ++x) {
                    d[x] = soma * k;
                    if (x + r + 1 < w) soma += s[x + r + 1];
                    if (x - r >= 0) soma -= s[x - r];
                }
            }
        }

        void caixaV(const float* src, float* dst, int w, int h, int r) {
            const float k = 1.0f / static_cast<float>(2 * r + 1);
            for (int x = 0; x < w; ++x) {
                float soma = 0;
                for (int y = 0; y <= r && y < h; ++y) soma += src[y * w + x];
                for (int y = 0; y < h; ++y) {
                    dst[y * w + x] = soma * k;
                    if (y + r + 1 < h) soma += src[(y + r + 1) * w + x];
                    if (y - r >= 0) soma -= src[(y - r) * w + x];
                }
            }
        }

        // dst = desfoque(src) (src fica intacto; tmp do mesmo tamanho)
        void desfocar(const float* src, float* dst, float* tmp, int w, int h, float sigma) {
            int raios[3];
            caixasParaGauss(sigma, raios);
            caixaH(src, tmp, w, h, raios[0]);
            caixaV(tmp, dst, w, h, raios[0]);
            for (int i = 1; i < 3; ++i) {
                caixaH(dst, tmp, w, h, raios[i]);
                caixaV(tmp, dst, w, h, raios[i]);
            }
        }

        // Segmento de LED (hexagono: pontas em bico). Horizontal: linha central y = c, de a a b; vertical: x = c.
        struct Segmento {
            bool horizontal;
            float c, a, b, meia;
            bool dentro(float x, float y) const {
                const float d = std::fabs(horizontal ? y - c : x - c);
                const float t = horizontal ? x : y;
                return d <= meia && t >= a + d && t <= b - d;
            }
        };
    } // namespace

    // Faixa de composicao: pixels da base_ convertidos sob demanda para float 0..255 (565 exato: R5*8, G6*4, B5*8, que o
    // pontilhado devolve identico), luz somada por "screen" e de volta a 565 com o mesmo pontilhado.
    struct CenaFoco::Tile {
        float* rgb;
        uint8_t* tocado;
        const uint16_t* base;
        Faixa f;   // em coordenadas da tela
        int w() const { return f.x1 - f.x0; }
        bool dentro(int x, int y) const { return x >= f.x0 && y >= f.y0 && x < f.x1 && y < f.y1; }
        float* px(int x, int y) {
            const int i = (y - f.y0) * w() + (x - f.x0);
            float* p = rgb + i * 3;
            if (!tocado[i]) {
                const uint16_t c = base[y * kLarg + x];
                p[0] = static_cast<float>((c >> 11) & 31) * 8.0f;
                p[1] = static_cast<float>((c >> 5) & 63) * 4.0f;
                p[2] = static_cast<float>(c & 31) * 8.0f;
                tocado[i] = 1;
            }
            return p;
        }
        // soma "screen" (como os brilhos dos mockups): p = 255 - (255 - p)(255 - l)/255
        void luz(int x, int y, const Rgb& l) {
            if (!dentro(x, y) || l.r + l.g + l.b < 0.06f) return;
            float* p = px(x, y);
            p[0] = 255.0f - (255.0f - p[0]) * (255.0f - std::min(255.0f, l.r)) / 255.0f;
            p[1] = 255.0f - (255.0f - p[1]) * (255.0f - std::min(255.0f, l.g)) / 255.0f;
            p[2] = 255.0f - (255.0f - p[2]) * (255.0f - std::min(255.0f, l.b)) / 255.0f;
        }
        // Algo que SUBSTITUI o emissivo da janela nos mockups (gota, farol, luz de antena): alvo = screen(cor, kAlvoFundo *
        // pixel), misturado pela cobertura (com kAlvoFundo = 1 e o mesmo que somar cor * cobertura por "screen").
        void sobreLuz(int x, int y, const Rgb& c, float a) {
            if (!dentro(x, y) || a <= 0.002f) return;
            a = std::min(a, 1.0f);
            float* p = px(x, y);
            const float cor[3] = {std::min(255.0f, c.r), std::min(255.0f, c.g), std::min(255.0f, c.b)};
            for (int k = 0; k < 3; ++k) {
                const float alvo = cor[k] + kAlvoFundo * p[k] * (1.0f - cor[k] / 255.0f);
                p[k] += (alvo - p[k]) * a;
            }
        }
        void multiplicar(int x, int y, float fator) {
            if (!dentro(x, y) || fator >= 0.999f) return;
            float* p = px(x, y);
            p[0] *= fator, p[1] *= fator, p[2] *= fator;
        }
    };

    namespace {
        // Pincel das luzes pequenas: nucleo com borda suavizada + halo gaussiano curto de mesma massa (o "bloom" curto dos
        // mockups: desfoque sigma s do nucleo, ganho g). Variancia do halo = s^2 + variancia da forma (disco r: r^2/4 por
        // eixo; caixa w: w^2/12). Modo Luz = soma "screen" (LED/olho/tecla: o mockup nao tinha nada por baixo); modo Sobre =
        // substitui o ceu da janela (sobreLuz) e o halo soma por cima.
        struct Pincel {
            float sigma, ganho;
            Grade grade;
            Vis vis;
            Faixa limite;   // recorte extra (ex.: a imagem da cidade so existe dentro da janela)
            enum Modo : uint8_t { Luz, Sobre };

            void forma(CenaFoco::Tile& t, bool elipse, float x0, float y0, float x1, float y1, Rgb cor, Modo modo,
                       float visivel = 1.0f, bool halo = true) const {
                if (visivel <= 0.0f || x1 <= x0 || y1 <= y0) return;
                const float cx = (x0 + x1) / 2, cy = (y0 + y1) / 2, w = x1 - x0, h = y1 - y0;
                cor = grade.tratar(cor, cx, cy);
                const float vCentro = vis.emPonto(cx, cy) * visivel;
                if (vCentro <= 0.0f && modo == Luz) return;
                const float s2x = sigma * sigma + (elipse ? w * w / 16.0f : w * w / 12.0f);
                const float s2y = sigma * sigma + (elipse ? h * h / 16.0f : h * h / 12.0f);
                const float area = elipse ? 3.14159265f * w * h / 4.0f : w * h;
                const float pico = halo ? ganho * area / (6.2831853f * std::sqrt(s2x * s2y)) * vCentro : 0.0f;
                const float ax = halo ? 3.0f * std::sqrt(s2x) : 0.0f, ay = halo ? 3.0f * std::sqrt(s2y) : 0.0f;
                Faixa lim = t.f;
                lim.x0 = std::max(lim.x0, limite.x0), lim.y0 = std::max(lim.y0, limite.y0);
                lim.x1 = std::min(lim.x1, limite.x1), lim.y1 = std::min(lim.y1, limite.y1);
                const Faixa f = cobrir(cx - w / 2 - ax, cy - h / 2 - ay, cx + w / 2 + ax, cy + h / 2 + ay, lim);
                for (int y = f.y0; y < f.y1; ++y)
                    for (int x = f.x0; x < f.x1; ++x) {
                        const float dx = static_cast<float>(x) + 0.5f - cx, dy = static_cast<float>(y) + 0.5f - cy;
                        if (pico > 0.0f) {
                            const float e = dx * dx / (2 * s2x) + dy * dy / (2 * s2y);
                            if (e < 4.5f) t.luz(x, y, cor * (pico * std::exp(-e)));
                        }
                        float cob = 0.0f;
                        if (static_cast<float>(x) + 1 > x0 && static_cast<float>(x) < x1 && static_cast<float>(y) + 1 > y0
                            && static_cast<float>(y) < y1)
                            cob = elipse ? coberturaElipse(x, y, cx, cy, w / 2, h / 2)
                                         : sobreposicao(float(x), float(x + 1), x0, x1) * sobreposicao(float(y), float(y + 1), y0, y1);
                        if (cob <= 0.0f) continue;
                        const float v = vis.em(x, y) * visivel;
                        if (modo == Luz) t.luz(x, y, cor * (cob * v));
                        else t.sobreLuz(x, y, cor, cob * v);
                    }
            }

            // Traco fino de (ax, ay) a (bx, by), largura `larg` (< 1 px), "por cima" com a visibilidade da janela.
            void traco(CenaFoco::Tile& t, float ax, float ay, float bx, float by, float larg, Rgb cor) const {
                cor = grade.tratar(cor, (ax + bx) / 2, (ay + by) / 2);
                Faixa lim = t.f;
                lim.x0 = std::max(lim.x0, limite.x0), lim.y0 = std::max(lim.y0, limite.y0);
                lim.x1 = std::min(lim.x1, limite.x1), lim.y1 = std::min(lim.y1, limite.y1);
                const Faixa f = cobrir(std::min(ax, bx) - 1, std::min(ay, by) - 1, std::max(ax, bx) + 1, std::max(ay, by) + 1, lim);
                const float vx = bx - ax, vy = by - ay, l2 = std::max(1e-6f, vx * vx + vy * vy);
                const float teto = std::min(1.0f, larg);
                for (int y = f.y0; y < f.y1; ++y)
                    for (int x = f.x0; x < f.x1; ++x) {
                        const float px = static_cast<float>(x) + 0.5f - ax, py = static_cast<float>(y) + 0.5f - ay;
                        const float u = std::clamp((px * vx + py * vy) / l2, 0.0f, 1.0f);
                        const float dx = px - u * vx, dy = py - u * vy;
                        const float cob = std::min(1.0f, kRealceTraco * std::clamp(0.5f + larg / 2 - std::sqrt(dx * dx + dy * dy), 0.0f, teto));
                        if (cob > 0.0f) t.sobreLuz(x, y, cor, cob * vis.em(x, y));
                    }
            }
        };
    } // namespace

    CenaFoco::~CenaFoco() { liberar(); }

    void CenaFoco::liberar() {
        soltar(fundo_);
        soltar(base_);
        soltar(patchMapa_);
        soltar(trabalho_);
        soltar(rgb_);
        soltar(tocado_);
        soltar(tileRgb_);
        soltar(tileTocado_);
        soltar(tileSaida_);
        soltar(janelaVis_);
        soltar(relMascara_);
        soltar(relCurto_);
        soltar(relLargo_);
        soltar(relTemp_);
        capTrabalho_ = capMapa_ = capJanela_ = capRelogio_ = 0;
        cena_ = patchDe_ = janelaDe_ = nullptr;
        valido_ = false;
        basePronta_ = false;
        sigRelogioPronto_ = 0;
    }

    bool CenaFoco::alocar(const Cena& cena) {
        const auto area = [](const Retangulo& r) {
            return r.x1 > r.x0 && r.y1 > r.y0 ? static_cast<size_t>(r.x1 - r.x0) * static_cast<size_t>(r.y1 - r.y0) : 0;
        };
        const size_t mapa = area(cena.mapa), trabalho = std::max(area(cena.revela), mapa);
        const size_t tela = size_t(kLarg) * kAlt;
        if (fundo_ == nullptr) fundo_ = static_cast<uint16_t*>(psram(tela * sizeof(uint16_t)));
        if (base_ == nullptr) base_ = static_cast<uint16_t*>(psram(tela * sizeof(uint16_t)));
        if (capTrabalho_ < trabalho) {
            soltar(trabalho_);
            trabalho_ = static_cast<uint16_t*>(psram(trabalho * sizeof(uint16_t)));
            capTrabalho_ = trabalho_ ? trabalho : 0;
        }
        if (capMapa_ < mapa) {
            soltar(patchMapa_);
            soltar(rgb_);
            soltar(tocado_);
            patchDe_ = nullptr;
            patchMapa_ = static_cast<uint16_t*>(psram(mapa * sizeof(uint16_t)));
            rgb_ = static_cast<float*>(psram(mapa * 3 * sizeof(float)));
            tocado_ = static_cast<uint8_t*>(psram(mapa));
            capMapa_ = (patchMapa_ && rgb_ && tocado_) ? mapa : 0;
        }
        if (tileRgb_ == nullptr) tileRgb_ = static_cast<float*>(psram(kCapTile * 3 * sizeof(float)));
        if (tileTocado_ == nullptr) tileTocado_ = static_cast<uint8_t*>(psram(kCapTile));
        if (tileSaida_ == nullptr) tileSaida_ = static_cast<uint16_t*>(psram(kCapTile * sizeof(uint16_t)));
        bool vivaOk = true;
        if (cena.viva != nullptr) {
            const size_t janela = area(cena.viva->janela), relogio = area(cena.viva->relogio.brilho);
            if (capJanela_ < janela) {
                soltar(janelaVis_);
                janelaDe_ = nullptr;
                janelaVis_ = static_cast<uint8_t*>(psram(janela));
                capJanela_ = janelaVis_ ? janela : 0;
            }
            if (capRelogio_ < relogio) {
                soltar(relMascara_);
                soltar(relCurto_);
                soltar(relLargo_);
                soltar(relTemp_);
                relMascara_ = static_cast<float*>(psram(relogio * sizeof(float)));
                relCurto_ = static_cast<float*>(psram(relogio * sizeof(float)));
                relLargo_ = static_cast<float*>(psram(relogio * sizeof(float)));
                relTemp_ = static_cast<float*>(psram(relogio * sizeof(float)));
                capRelogio_ = (relMascara_ && relCurto_ && relLargo_ && relTemp_) ? relogio : 0;
                sigRelogioPronto_ = 0;
            }
            vivaOk = capJanela_ >= janela && capRelogio_ >= relogio;
        }
        if (fundo_ == nullptr || base_ == nullptr || capTrabalho_ == 0 || capMapa_ == 0 || tileRgb_ == nullptr
            || tileTocado_ == nullptr || tileSaida_ == nullptr || !vivaOk) {
            ESP_LOGW(kTag, "sem PSRAM para a cena (fundo=%p base=%p)", static_cast<void*>(fundo_), static_cast<void*>(base_));
            liberar();
            return false;
        }
        return true;
    }

    bool CenaFoco::decodificar(const uint8_t* jpeg, size_t tamanho) {
        // O estado do JPEGDEC tem ~18 KB: na PSRAM tambem (nada de RAM interna durante uma sync TLS).
        void* memoria = psram(sizeof(JPEGDEC));
        if (memoria == nullptr) return false;
        auto* dec = new (memoria) JPEGDEC();
        bool ok = dec->openRAM(const_cast<uint8_t*>(jpeg), static_cast<int>(tamanho), aoBloco) != 0;
        if (ok && (dec->getWidth() != kLarg || dec->getHeight() != kAlt)) {
            ESP_LOGW(kTag, "fundo com tamanho inesperado %dx%d", dec->getWidth(), dec->getHeight());
            ok = false;
        }
        if (ok) {
            dec->setPixelType(RGB8888);
            dec->setUserPointer(fundo_);
            ok = dec->decode(0, 0, 0) != 0;
            if (!ok) ESP_LOGW(kTag, "falha ao decodificar o fundo (erro %d)", dec->getLastError());
        }
        dec->close();
        dec->~JPEGDEC();
        heap_caps_free(memoria);
        return ok;
    }

    bool CenaFoco::preparar(const Cena& cena, Fundo fundo) {
        if (pronta(cena, fundo)) return true;
        if (!alocar(cena)) return false;
        const int64_t t0 = esp_timer_get_time();
        valido_ = false;
        basePronta_ = false;
        const Retangulo& m = cena.mapa;
        const int mw = m.x1 - m.x0, mh = m.y1 - m.y0;
        const auto guardarPatch = [&] {
            for (int y = 0; y < mh; ++y)
                std::memcpy(patchMapa_ + y * mw, fundo_ + (m.y0 + y) * kLarg + m.x0, size_t(mw) * sizeof(uint16_t));
            patchDe_ = &cena;
        };
        // amanhecer: o mapa e redesenhado sobre o retangulo do mapa da NOITE (nos apagados); sem ele ainda, decodifica antes
        if (fundo != Fundo::Noite && patchDe_ != &cena) {
            if (!decodificar(cena.noite, cena.noiteTam)) return false;
            guardarPatch();
        }
        const uint8_t* jpeg = fundo == Fundo::Noite ? cena.noite : fundo == Fundo::Vitoria ? cena.vitoria : cena.amanhecer;
        const size_t tam = fundo == Fundo::Noite ? cena.noiteTam : fundo == Fundo::Vitoria ? cena.vitoriaTam : cena.amanhecerTam;
        if (!decodificar(jpeg, tam)) return false;
        if (fundo == Fundo::Noite) guardarPatch();
        cena_ = &cena;
        fundoAtual_ = fundo;
        valido_ = true;
        ++geracao_;
        sigRelogioPronto_ = 0;
        prepararJanela();
        ESP_LOGD(kTag, "fundo %d decodificado em %lld us", static_cast<int>(fundo),
                 static_cast<long long>(esp_timer_get_time() - t0));
        return true;
    }

    void CenaFoco::comporRevelacao(const Quadro& q, int w, int h) {
        const Cena& c = *cena_;
        const Retangulo& r = c.revela;
        int linhas = 0, cursor = -1;
        if (q.revelar) {
            if (c.revelaModo == 0) {
                linhas = tempo::cenafoco::linhasModo0(h, q.cheio);
            } else {
                linhas = tempo::cenafoco::linhasModo1(h, c.linhaY0x10, c.linhaPassox10, c.linhasCabecalho, c.linhasMax, q.tomados);
                cursor = tempo::cenafoco::cursorModo1Topox10(c.linhaY0x10, c.linhaPassox10, c.linhasCabecalho, c.linhasMax,
                                                             q.tomados);
            }
        }
        // O sprite vem TRUNCADO para RGB565 (sem pontilhado): cada nivel e o piso do valor real, ~meio nivel mais escuro
        // que o fundo pontilhado (degrau visivel na emenda). Recentra no meio do degrau e pontilha com o mesmo ruido azul.
        // QG Neon: o corte cai em qualquer linha de pixel e o brilho do codigo do sprite acabaria num degrau reto; uma
        // rampa de kRampaNeon linhas centrada no corte imita o brilho que some aos poucos nos mockups.
        // Abaixo do corte, o brilho ambiente do codigo (o "bloom" largo dos mockups) continua e some aos poucos: a diferenca
        // sprite - fundo dos pixels SEM texto logo acima do corte (percentil 30 por canal) decai com kBrilhoNeonLinhas.
        constexpr float kRampaNeon = 3.0f, kBrilhoNeonLinhas = 5.0f;
        const auto sprite8 = [](uint16_t s, float& r8, float& g8, float& b8) {
            r8 = static_cast<float>(((s >> 11) & 31) * 8 + 4), g8 = static_cast<float>(((s >> 5) & 63) * 4 + 2),
            b8 = static_cast<float>((s & 31) * 8 + 4);
        };
        const auto fundo8 = [](uint16_t f, float& r8, float& g8, float& b8) {
            r8 = static_cast<float>(((f >> 11) & 31) * 255 / 31), g8 = static_cast<float>(((f >> 5) & 63) * 255 / 63),
            b8 = static_cast<float>((f & 31) * 255 / 31);
        };
        const bool rampa = c.revelaModo == 0 && linhas > 0 && linhas < h;
        float brilho[3] = {0, 0, 0};
        if (rampa) {
            constexpr int kAmostraLinhas = 4;
            const int kMaxAmostras = static_cast<int>(capMapa_);   // rascunho na PSRAM (o rgb_ do mapa), nada na pilha
            float* dif[3] = {rgb_, rgb_ + kMaxAmostras, rgb_ + 2 * kMaxAmostras};
            int n = 0;
            for (int y = std::max(0, linhas - kAmostraLinhas); y < linhas; ++y)
                for (int x = 0; x < w && n < kMaxAmostras; ++x, ++n) {
                    float sr, sg, sb, fr, fg, fb;
                    sprite8(c.revelaPx[y * w + x], sr, sg, sb);
                    fundo8(fundo_[(r.y0 + y) * kLarg + r.x0 + x], fr, fg, fb);
                    dif[0][n] = sr - fr, dif[1][n] = sg - fg, dif[2][n] = sb - fb;
                }
            for (int k = 0; k < 3 && n > 0; ++k) {
                std::nth_element(dif[k], dif[k] + n * 3 / 10, dif[k] + n);
                brilho[k] = std::max(0.0f, dif[k][n * 3 / 10]);
            }
        }
        for (int y = 0; y < h; ++y) {
            float a = y < linhas ? 1.0f : 0.0f;
            if (rampa)
                a = std::clamp(0.5f + (static_cast<float>(linhas) - (static_cast<float>(y) + 0.5f)) / kRampaNeon, 0.0f, 1.0f);
            const float resto = rampa && y >= linhas - 1
                ? (1.0f - a) * std::exp(-(static_cast<float>(y) + 0.5f - static_cast<float>(linhas)) / kBrilhoNeonLinhas) : 0.0f;
            const uint16_t* fundo = fundo_ + (r.y0 + y) * kLarg + r.x0;
            uint16_t* saida = trabalho_ + y * w;
            if (a <= 0.0f && resto * std::max({brilho[0], brilho[1], brilho[2]}) < 0.5f) {
                std::memcpy(saida, fundo, size_t(w) * sizeof(uint16_t));
                continue;
            }
            const uint16_t* sprite = c.revelaPx + y * w;
            for (int x = 0; x < w; ++x) {
                float sr, sg, sb, fr, fg, fb;
                sprite8(sprite[x], sr, sg, sb);
                if (a < 1.0f) {
                    fundo8(fundo[x], fr, fg, fb);
                    sr = fr + (sr - fr) * a + brilho[0] * resto;
                    sg = fg + (sg - fg) * a + brilho[1] * resto;
                    sb = fb + (sb - fb) * a + brilho[2] * resto;
                }
                saida[x] = pontilhar(std::clamp(static_cast<int>(sr + 0.5f), 0, 255), std::clamp(static_cast<int>(sg + 0.5f), 0, 255),
                                     std::clamp(static_cast<int>(sb + 0.5f), 0, 255), r.x0 + x, r.y0 + y);
            }
        }
        if (cursor < 0) return;
        // cursor do terminal: bloco de 2 x 3,2 px (como na arte), borda de cima/baixo suavizada pela cobertura da linha
        const float topo = static_cast<float>(cursor) / 10.0f, base = topo + 3.2f;
        const Rgbf cor = de565(c.corCursor);
        for (int y = static_cast<int>(std::floor(topo)); y < static_cast<int>(std::ceil(base)) && y < h; ++y) {
            if (y < 0) continue;
            const float a = std::min(base, float(y + 1)) - std::max(topo, float(y));
            if (a <= 0.0f) continue;
            for (int x = 3; x < 5 && x < w; ++x) {
                uint16_t& px = trabalho_[y * w + x];
                px = pontilhar(mistura(de565(px), cor, a), r.x0 + x, r.y0 + y);
            }
        }
    }

    void CenaFoco::comporMapa(const Quadro& q, int w, int h) {
        const Cena& c = *cena_;
        const Retangulo& m = c.mapa;
        // base: o proprio fundo (noite/vitoria) ou, no amanhecer, o retangulo do mapa da noite (nos apagados)
        const bool doPatch = q.mapa && fundoAtual_ != Fundo::Noite;
        const auto base = [&](int x, int y) { return doPatch ? patchMapa_[y * w + x] : fundo_[(m.y0 + y) * kLarg + m.x0 + x]; };
        if (!q.mapa) {   // vitoria: a arte ja tem o mapa
            for (int y = 0; y < h; ++y)
                for (int x = 0; x < w; ++x) trabalho_[y * w + x] = base(x, y);
            return;
        }
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const Rgbf b = de565(base(x, y));
                float* p = rgb_ + (y * w + x) * 3;
                p[0] = b.r, p[1] = b.g, p[2] = b.b;
            }
        std::memset(tocado_, 0, size_t(w) * h);
        Tela tela{rgb_, tocado_, w, h};

        const Rgbf apagado = de565(c.corNoApagado), antes = de565(c.corNoAntes), novo = de565(c.corNoNovo);
        const Rgbf eloApagado = de565(c.corEloApagado), eloAntes = de565(c.corEloAntes), eloNovo = de565(c.corEloNovo);
        float gx[tempo::cenafoco::kNos], cx[tempo::cenafoco::kNos], cy[tempo::cenafoco::kNos];
        for (int i = 0; i < tempo::cenafoco::kNos; ++i) {
            const auto& no = q.nos[i];
            gx[i] = no.tipo == TipoNo::Antes ? 1.0f : no.tipo == TipoNo::Novo ? std::clamp(no.g, 0.0f, 1.0f) : 0.0f;
            cx[i] = static_cast<float>(c.noX4[i]) / 4.0f - static_cast<float>(m.x0);   // borda (pixel x = [x, x+1))
            cy[i] = static_cast<float>(c.noY4[i]) / 4.0f - static_cast<float>(m.y0);
        }
        // 1) elos acesos (os apagados ja estao na arte): cor conforme os dois nos
        for (const auto& elo : c.elos) {
            const int a = elo[0] % tempo::cenafoco::kNos, b = elo[1] % tempo::cenafoco::kNos;
            const float t = std::min(gx[a], gx[b]);
            if (t <= 0.0f) continue;
            const bool doDia = q.nos[a].tipo == TipoNo::Antes && q.nos[b].tipo == TipoNo::Antes;
            const Rgbf cor = mistura(eloApagado, doDia ? eloAntes : eloNovo, t);
            linhaWu(cx[a] - 0.5f, cy[a] - 0.5f, cx[b] - 0.5f, cy[b] - 0.5f,
                    [&](int x, int y, float cob) { tela.sobre(x, y, cor, cob); });
        }
        // 2) brilho, anel de "conectando" e disco de cada no aceso
        for (int i = 0; i < tempo::cenafoco::kNos; ++i) {
            const float g = gx[i];
            if (g <= 0.0f) continue;
            const bool doDia = q.nos[i].tipo == TipoNo::Antes;
            // a cor anda mais depressa que o raio (como nos mockups, onde o no meio aceso ja ganha o brilho da cor nova):
            // f(g) = 1 - (1 - g)^1,5, ajustada amostrando o mockup em g = 0,2..0,8
            const Rgbf cor = doDia ? antes : mistura(apagado, novo, 1.0f - std::pow(1.0f - g, 1.5f));
            const float r = 1.6f + 1.2f * g;
            const float s2 = kBrilhoSigma * kBrilhoSigma + r * r / 4.0f;
            const float pico = kBrilhoGanho * r * r / (2.0f * s2) * g;
            const bool conectando = !doDia && g < 1.0f;
            const float alcance = std::max(r + kBrilhoAlcance, conectando ? kAnelRaio + 2.5f : 0.0f);
            const int x0 = std::max(0, static_cast<int>(std::floor(cx[i] - alcance)));
            const int x1 = std::min(w - 1, static_cast<int>(std::ceil(cx[i] + alcance)));
            const int y0 = std::max(0, static_cast<int>(std::floor(cy[i] - alcance)));
            const int y1 = std::min(h - 1, static_cast<int>(std::ceil(cy[i] + alcance)));
            for (int y = y0; y <= y1; ++y)
                for (int x = x0; x <= x1; ++x) {
                    const float dx = static_cast<float>(x) + 0.5f - cx[i], dy = static_cast<float>(y) + 0.5f - cy[i];
                    const float d2 = dx * dx + dy * dy;
                    if (d2 < (r + kBrilhoAlcance) * (r + kBrilhoAlcance)) tela.luz(x, y, cor, pico * std::exp(-d2 / (2.0f * s2)));
                    if (conectando) {
                        const float cob = coberturaAnel(x, y, cx[i], cy[i], kAnelRaio - kAnelMeiaLarg, kAnelRaio + kAnelMeiaLarg);
                        const float dr = std::sqrt(d2) - kAnelRaio;   // + o brilho do anel (bloom curto dos mockups)
                        const float brilhoAnel = kAnelBrilho * std::exp(-dr * dr / (2.0f * kAnelBrilhoSigma * kAnelBrilhoSigma));
                        if (cob > 0.0f || brilhoAnel > 0.01f) tela.luz(x, y, novo, cob * kAnelForca + brilhoAnel);
                    }
                    const float cob = coberturaAnel(x, y, cx[i], cy[i], 0.0f, r);
                    if (cob > 0.0f) tela.sobre(x, y, cor, cob);
                }
        }
        // 3) de volta a RGB565: so os pixels tocados (pontilhados como o fundo); o resto fica identico a arte
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const int i = y * w + x;
                if (!tocado_[i]) {
                    trabalho_[i] = base(x, y);
                    continue;
                }
                const float* p = rgb_ + i * 3;
                trabalho_[i] = pontilhar(Rgbf{p[0], p[1], p[2]}, m.x0 + x, m.y0 + y);
            }
    }

    // ================================================================ v2: cena viva
    struct CenaFoco::Instante {
        uint32_t ms = 0, quadro = 0;
        int fundo = 0;              // 0 = noite, 1 = vitoria, 2 = amanhecer (indice das tabelas por fundo)
        tempo::cenaviva::Relogio relogio;
        bool treme = false;         // QG Neon: o tubo do relogio falha neste quadro
        bool doisPontos = true;     // QG Cidade: ":" aceso
        Grade grade;
    };

    void CenaFoco::copiarParaBase(const Retangulo& r) {
        const int w = r.x1 - r.x0, h = r.y1 - r.y0;
        for (int y = 0; y < h; ++y)
            std::memcpy(base_ + (r.y0 + y) * kLarg + r.x0, trabalho_ + y * w, size_t(w) * sizeof(uint16_t));
    }

    void CenaFoco::prepararJanela() {
        if (cena_ == nullptr || cena_->viva == nullptr || janelaDe_ == cena_ || janelaVis_ == nullptr) return;
        const CenaViva& v = *cena_->viva;
        const Retangulo& r = v.janela;
        const int w = r.x1 - r.x0, h = r.y1 - r.y0;
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x) {
                const float px = static_cast<float>(r.x0 + x), py = static_cast<float>(r.y0 + y);
                float tapado = 0.0f;   // moldura, montantes e laminas nao se sobrepoem (salvo nos cantos): soma, limitada a 1
                for (int i = 0; i < v.nOclusores; ++i) {
                    const Caixa8& o = v.oclusores[i];
                    tapado += sobreposicao(px, px + 1, px8(o.x0), px8(o.x1)) * sobreposicao(py, py + 1, px8(o.y0), px8(o.y1));
                }
                janelaVis_[y * w + x] = static_cast<uint8_t>(std::lround(255.0f * (1.0f - std::min(1.0f, tapado))));
            }
        janelaDe_ = cena_;
    }

    // Mascara do relogio (luz direta 0..1 ja com a forca do tubo) + brilho curto e largo (desfoque da propria mascara,
    // os mesmos sigma/ganho dos mockups). So quando o texto, a forca, os ":" ou a barra mudam (~1x/s).
    void CenaFoco::prepararRelogio(const Instante& in) {
        const RelogioCena& rc = cena_->viva->relogio;
        const Retangulo& R = rc.brilho;
        const int w = R.x1 - R.x0, h = R.y1 - R.y0;
        std::fill_n(relMascara_, size_t(w) * h, 0.0f);
        const auto pinta = [&](int x, int y, float v) {
            if (x < R.x0 || y < R.y0 || x >= R.x1 || y >= R.y1 || v <= 0.0f) return;
            float& m = relMascara_[(y - R.y0) * w + (x - R.x0)];
            m = std::max(m, std::min(1.0f, v));
        };
        using tempo::cenaviva::EstadoRelogio;
        float forca = static_cast<float>(in.relogio.estado == EstadoRelogio::Vitoria ? rc.forcaVitoria : rc.forcaFoco) / 255.0f;
        if (in.treme) forca *= static_cast<float>(rc.forcaTreme) / 255.0f;
        const std::string_view texto(in.relogio.texto.data());
        if (rc.estilo == 0) {   // neon: a fonte do firmware (Atkinson), centrada pela tinta como o sans::draw
            const auto& fonte = ui::sans::font(rc.fonte);
            int avanco = 0, esquerda = 0;
            for (const char ch : texto) {
                const auto* g = ui::sans::glyph(fonte, static_cast<uint8_t>(ch));
                if (g == nullptr) g = ui::sans::glyph(fonte, '?');
                esquerda = std::min(esquerda, avanco + g->left);
                avanco += g->advance;
            }
            int x = static_cast<int>(std::lround(px8(rc.centroX8) - ui::sans::width(texto, fonte) / 2.0f)) - esquerda;
            const int base = static_cast<int>(std::lround(px8(rc.centroY8)));
            for (const char ch : texto) {
                const auto* g = ui::sans::glyph(fonte, static_cast<uint8_t>(ch));
                if (g == nullptr) g = ui::sans::glyph(fonte, '?');
                for (int lin = 0; lin < g->height; ++lin)
                    for (int col = 0; col < g->width; ++col) {
                        const int i = lin * g->width + col;
                        const uint8_t par = fonte.pixels[g->offset + i / 2];
                        const int alfa = (i & 1) ? (par & 15) : (par >> 4);
                        if (alfa) pinta(x + g->left + col, base + g->top + lin, static_cast<float>(alfa) / 15.0f * forca);
                    }
                x += g->advance;
            }
        } else {   // LED de 7 segmentos, suavizados por cobertura (4x4)
            const float esp = px8(rc.segEsp8), alt = px8(rc.digAlt8), larg = px8(rc.digLarg8), avanco = px8(rc.digAvanco8);
            const float cy = px8(rc.centroY8), meia = esp / 2, folga = esp * 0.15f;
            const float x0 = px8(rc.centroX8) - static_cast<float>(texto.size()) * avanco / 2;
            const auto segmento = [&](const Segmento& s, float v) {
                if (v <= 0.0f) return;
                const float ax0 = s.horizontal ? s.a : s.c - s.meia, ax1 = s.horizontal ? s.b : s.c + s.meia;
                const float ay0 = s.horizontal ? s.c - s.meia : s.a, ay1 = s.horizontal ? s.c + s.meia : s.b;
                for (int y = static_cast<int>(std::floor(ay0)); y < static_cast<int>(std::ceil(ay1)); ++y)
                    for (int x = static_cast<int>(std::floor(ax0)); x < static_cast<int>(std::ceil(ax1)); ++x) {
                        int dentro = 0;
                        for (int j = 0; j < 4; ++j)
                            for (int i = 0; i < 4; ++i)
                                dentro += s.dentro(x + (i + 0.5f) * 0.25f, y + (j + 0.5f) * 0.25f) ? 1 : 0;
                        if (dentro) pinta(x, y, v * static_cast<float>(dentro) / 16.0f);
                    }
            };
            for (size_t i = 0; i < texto.size(); ++i) {
                const char ch = texto[i];
                const float cx = x0 + (static_cast<float>(i) + 0.5f) * avanco;
                const float L = cx - larg / 2 + meia, Rr = cx + larg / 2 - meia, T = cy - alt / 2 + meia, B = cy + alt / 2 - meia;
                if (ch >= '0' && ch <= '9') {
                    const Segmento segs[7] = {{true, T, L + folga, Rr - folga, meia},  {false, Rr, T + folga, cy - folga, meia},
                                              {false, Rr, cy + folga, B - folga, meia}, {true, B, L + folga, Rr - folga, meia},
                                              {false, L, cy + folga, B - folga, meia},  {false, L, T + folga, cy - folga, meia},
                                              {true, cy, L + folga, Rr - folga, meia}};
                    const uint8_t bits = tempo::cenaviva::segmentos(ch);
                    for (int k = 0; k < 7; ++k) segmento(segs[k], (bits >> k) & 1 ? forca : forca * kFantasmaSegmento);
                } else if (ch == ':') {
                    const float v = in.doisPontos ? forca : forca * kFantasmaSegmento;
                    for (const float dy : {-alt * 0.22f, alt * 0.22f}) {   // pontos redondos (disco com borda suavizada)
                        const float py = cy + dy, rp = meia * 1.05f;
                        for (int y = static_cast<int>(std::floor(py - rp)); y < static_cast<int>(std::ceil(py + rp)); ++y)
                            for (int x = static_cast<int>(std::floor(cx - rp)); x < static_cast<int>(std::ceil(cx + rp)); ++x)
                                pinta(x, y, v * coberturaElipse(x, y, cx, py, rp, rp));
                    }
                } else if (ch == '+') {
                    segmento({true, cy, cx - larg * 0.42f, cx + larg * 0.42f, meia}, forca);
                    segmento({false, cx, cy - alt * 0.3f, cy + alt * 0.3f, meia}, forca);
                }
            }
        }
        {   // barra: tubo apagado inteiro + trecho aceso ate a fracao (pontas e bordas por cobertura)
            const float by = px8(rc.barraY8), mb = px8(rc.barraEsp8) / 2, bx0 = px8(rc.barraX0x8), bx1 = px8(rc.barraX1x8);
            const float aceso = bx0 + (bx1 - bx0) * std::max(0.02f, std::clamp(in.relogio.fracao, 0.0f, 1.0f));
            const float fb = rc.forcaBarra ? static_cast<float>(rc.forcaBarra) / 255.0f : forca;
            const float tubo = static_cast<float>(rc.tuboApagado) / 255.0f;
            for (int y = static_cast<int>(std::floor(by - mb)); y < static_cast<int>(std::ceil(by + mb)); ++y) {
                const float linha = sobreposicao(float(y), float(y + 1), by - mb, by + mb);
                for (int x = static_cast<int>(std::floor(bx0)); x < static_cast<int>(std::ceil(bx1)); ++x) {
                    const float col = sobreposicao(float(x), float(x + 1), bx0, bx1);
                    const float acc = sobreposicao(float(x), float(x + 1), bx0, aceso);
                    pinta(x, y, linha * (acc * fb + (col - acc) * tubo));
                }
            }
        }
        const float sigma = static_cast<float>(cena_->viva->curtoSigmax100) / 100.0f;
        const float ganhoCurto = static_cast<float>(cena_->viva->curtoGanhox100) / 100.0f;
        const float ganhoLargo = static_cast<float>(rc.largoGanhox100[std::clamp(in.fundo, 0, 2)]) / 100.0f;
        desfocar(relMascara_, relCurto_, relTemp_, w, h, sigma);
        desfocar(relMascara_, relLargo_, relTemp_, w, h, 9.0f);
        for (size_t i = 0; i < size_t(w) * h; ++i) {
            relCurto_[i] *= ganhoCurto;
            relLargo_[i] *= ganhoLargo;
        }
    }

    Retangulo CenaFoco::caixa(int camada) const {
        Retangulo vazio{0, 0, 0, 0};
        if (cena_ == nullptr || cena_->viva == nullptr) return vazio;
        const CenaViva& v = *cena_->viva;
        const float m = 3.0f * static_cast<float>(v.curtoSigmax100) / 100.0f + 2.0f;   // alcance do halo curto
        float x0 = 1e9f, y0 = 1e9f, x1 = -1e9f, y1 = -1e9f;
        const auto junta = [&](float a0, float b0, float a1, float b1) {
            x0 = std::min(x0, a0 - m), y0 = std::min(y0, b0 - m), x1 = std::max(x1, a1 + m), y1 = std::max(y1, b1 + m);
        };
        const auto juntaCaixa = [&](const Caixa8& c) { junta(px8(c.x0), px8(c.y0), px8(c.x1), px8(c.y1)); };
        switch (camada) {
        case Janela: junta(v.janela.x0, v.janela.y0, v.janela.x1, v.janela.y1); break;
        case Relogio: return v.relogio.brilho;
        case Roteador: for (int i = 0; i < v.nRoteador; ++i) juntaCaixa(v.roteador[i].caixa); break;
        case Rack: for (int i = 0; i < v.nRack; ++i) juntaCaixa(v.rack[i].caixa); break;
        case Olhos: for (int i = 0; i < v.nOlhos; ++i) juntaCaixa(v.olhos[i].caixa); break;
        case Teclado: for (int i = 0; i < v.nTeclas; ++i) juntaCaixa(v.teclas[i]); break;
        case FaixaGabinete: for (int i = 0; i < v.nFaixa; ++i) juntaCaixa(v.faixa[i]); break;
        case Sprite: return v.sprite.quadros ? v.sprite.r : vazio;
        default: return vazio;
        }
        if (x1 <= x0) return vazio;
        return {static_cast<int16_t>(std::max(0.0f, std::floor(x0))), static_cast<int16_t>(std::max(0.0f, std::floor(y0))),
                static_cast<int16_t>(std::min<float>(kLarg, std::ceil(x1))), static_cast<int16_t>(std::min<float>(kAlt, std::ceil(y1)))};
    }

    uint32_t CenaFoco::assinatura(int camada, const Instante& in) const {
        namespace cv = tempo::cenaviva;
        if (cena_ == nullptr || cena_->viva == nullptr) return 0;
        const CenaViva& v = *cena_->viva;
        uint32_t s = ui::Context::combine(0x56495600u + static_cast<uint32_t>(camada), static_cast<uint32_t>(in.fundo));
        switch (camada) {
        case Janela:
            if (v.tipo == 1 || v.forcaGota[in.fundo] > 0) return ui::Context::combine(s, in.quadro);   // gotas/carros: todo quadro
            for (int i = 0; i < v.nJanelas; ++i) s = ui::Context::combine(s, cv::janelaAcesa(i, in.ms) ? 1u : 0u);
            if (v.droneR8 > 0) {
                const auto d = cv::drone(in.ms, v.janela.x0, v.janela.x1, px8(v.droneY8), px8(v.droneAmp8), px8(v.droneVao8),
                                         px8(v.droneEntra8));
                if (d.visivel)
                    s = ui::Context::combine(ui::Context::combine(s, static_cast<uint32_t>(std::lround(d.x * 8))),
                                             static_cast<uint32_t>(std::lround(d.y * 8)) * 2 + (d.vermelhoAceso ? 1 : 0));
            }
            return s;
        case Relogio: {
            for (const char ch : std::string_view(in.relogio.texto.data())) s = ui::Context::combine(s, static_cast<uint8_t>(ch));
            const RelogioCena& rc = v.relogio;
            const float fr = std::max(0.02f, std::clamp(in.relogio.fracao, 0.0f, 1.0f));
            s = ui::Context::combine(s, static_cast<uint32_t>(std::lround(fr * px8(rc.barraX1x8 - rc.barraX0x8) * 8)));
            s = ui::Context::combine(s, static_cast<uint32_t>(in.relogio.estado) * 4 + (in.treme ? 1 : 0)
                                            + (rc.estilo == 1 && in.doisPontos ? 2 : 0));
            return s;
        }
        case Roteador:
            for (int i = 0; i < v.nRoteador; ++i) s = ui::Context::combine(s, cv::roteadorAceso(i, in.ms) ? 1u : 0u);
            return s;
        case Rack:
            for (int i = 0; i < v.nRack; ++i)
                s = ui::Context::combine(s, cv::rackAceso(i / 4, i % 4, in.ms, static_cast<float>(v.rackAceso) / 255.0f) ? 1u : 0u);
            return s;
        case Olhos: return ui::Context::combine(s, in.fundo != 2 && !cv::gatoPisca(in.quadro) ? 1u : 0u);
        case Teclado: return ui::Context::combine(s, in.fundo == 1 ? 0u : in.quadro);
        case FaixaGabinete: return ui::Context::combine(s, in.quadro);
        case Sprite: return ui::Context::combine(s, static_cast<uint32_t>(cv::quadroSprite(in.ms)));
        default: return 0;
        }
    }

    void CenaFoco::aplicar(int camada, Tile& t, const Instante& in) const {
        namespace cv = tempo::cenaviva;
        const CenaViva& v = *cena_->viva;
        Pincel p{static_cast<float>(v.curtoSigmax100) / 100.0f, static_cast<float>(v.curtoGanhox100) / 100.0f, in.grade, Vis{},
                 Faixa{0, 0, kLarg, kAlt}};
        const Faixa janela{v.janela.x0, v.janela.y0, v.janela.x1, v.janela.y1};
        const float jx0 = v.janela.x0, jy0 = v.janela.y0, jx1 = v.janela.x1, jy1 = v.janela.y1;
        const auto caixaF = [](const Caixa8& c, float& a0, float& b0, float& a1, float& b1) {
            a0 = px8(c.x0), b0 = px8(c.y0), a1 = px8(c.x1), b1 = px8(c.y1);
        };
        float a0, b0, a1, b1;
        switch (camada) {
        case Janela: {
            p.vis = Vis{janelaVis_, v.janela};
            p.limite = janela;   // a imagem da cidade so existe dentro da janela
            if (v.tipo == 1) {
                for (int i = 0; i < v.nLetreiros; ++i) {   // letreiros piscando de leve (multiplica a propria arte)
                    const float f = cv::letreiroForca(i, in.ms);
                    if (f >= 0.999f) continue;
                    caixaF(v.letreiros[i], a0, b0, a1, b1);
                    Faixa lim = t.f;
                    lim.x0 = std::max(lim.x0, janela.x0), lim.y0 = std::max(lim.y0, janela.y0);
                    lim.x1 = std::min(lim.x1, janela.x1), lim.y1 = std::min(lim.y1, janela.y1);
                    const Faixa fx = cobrir(a0, b0, a1, b1, lim);
                    for (int y = fx.y0; y < fx.y1; ++y)
                        for (int x = fx.x0; x < fx.x1; ++x) {
                            const float cob = sobreposicao(float(x), float(x + 1), a0, a1) * sobreposicao(float(y), float(y + 1), b0, b1);
                            t.multiplicar(x, y, 1.0f - (1.0f - f) * cob * p.vis.em(x, y));
                        }
                }
                for (int i = 0; i < v.nAntenas; ++i) {
                    if (!cv::antenaAcesa(v.antenas[i].camada, in.ms)) continue;
                    caixaF(v.antenas[i].caixa, a0, b0, a1, b1);
                    p.forma(t, true, a0, b0, a1, b1, deCor(v.corAntena), Pincel::Sobre);
                }
                const float luzes = in.fundo == 2 ? 0.35f : 1.0f;   // 1 - 0,65 * amanhecer (mockup_qg2.megacidade)
                for (int i = 0; i < v.nCarrosLonge; ++i) {
                    const CarroLonge& c = v.carrosLonge[i];
                    const float cx = jx0 + cv::carroLongeX(px8(c.fase08), px8(c.vel8), in.ms, jx1 - jx0), cy = px8(c.y8);
                    const float s = c.vel8 > 0 ? 1.0f : -1.0f;
                    for (int j = 0; j < 10; ++j) {   // rastro: 10 trechos de 1,6 px, cada vez mais apagados
                        const float xa = cx - s * j * 1.6f, xb = cx - s * (j + 1) * 1.6f;
                        p.forma(t, false, std::min(xa, xb), cy - 0.5f, std::max(xa, xb), cy + 0.5f,
                                deCor(c.cor) * (0.7f * (1.0f - j / 10.0f) * luzes + 0.05f), Pincel::Sobre, 1.0f, false);
                    }
                    p.forma(t, true, cx - 1.2f, cy - 1.2f, cx + 1.2f, cy + 1.2f, deCor(c.cor), Pincel::Sobre);
                }
            } else {
                for (int i = 0; i < v.nJanelas; ++i) {
                    if (!cv::janelaAcesa(i, in.ms)) continue;
                    caixaF(v.janelas[i].caixa, a0, b0, a1, b1);
                    p.forma(t, false, a0, b0, a1, b1, deCor(v.corJanela) * (v.janelas[i].forca / 255.0f), Pincel::Sobre);
                }
                if (v.droneR8 > 0) {
                    const auto d = cv::drone(in.ms, jx0, jx1, px8(v.droneY8), px8(v.droneAmp8), px8(v.droneVao8), px8(v.droneEntra8));
                    if (d.visivel) {
                        const float r = px8(v.droneR8), rb = px8(v.droneBrancoR8), bx = d.x + px8(v.droneBrancoDx8);
                        p.forma(t, true, d.x - r, d.y - r, d.x + r, d.y + r, deCor(d.vermelhoAceso ? v.droneCor : v.droneApagado),
                                Pincel::Sobre);
                        p.forma(t, true, bx - rb, d.y - rb, bx + rb, d.y + rb, deCor(v.droneBranco), Pincel::Sobre);
                    }
                }
            }
            const float fg = static_cast<float>(v.forcaGota[in.fundo]) / 255.0f;
            if (fg > 0.0f) {   // chuva no vidro (mesmas gotas e velocidades dos mockups)
                const Rgb cor = deCor(v.corGota) * fg;
                const Rgb ponta = deCor(v.corPonta) * (static_cast<float>(v.forcaPonta[in.fundo]) / 255.0f);
                const float larg = px8(v.largGota8), rp = px8(v.raioPonta8);
                for (int i = 0; i < v.nGotas; ++i) {
                    const Gota& g = v.gotas[i];
                    const float x = px8(g.x8), y = jy0 + cv::gotaY(px8(g.u8), px8(g.vel8), in.ms, jy1 - jy0);
                    const float ye = std::min(jy1, y + px8(g.comp8));
                    if (static_cast<int>(std::floor(std::min(x, x + px8(g.dx8)))) - 2 >= t.f.x1
                        || static_cast<int>(std::ceil(std::max(x, x + px8(g.dx8)))) + 2 < t.f.x0
                        || static_cast<int>(std::ceil(ye)) + 2 < t.f.y0 || static_cast<int>(std::floor(y)) - 2 >= t.f.y1)
                        continue;
                    p.traco(t, x, y, x + px8(g.dx8), ye, larg, cor);
                    if (rp > 0.0f && y + px8(g.comp8) < jy1 + rp)
                        p.forma(t, true, x - rp, y + px8(g.comp8) - rp, x + rp, y + px8(g.comp8) + rp, ponta, Pincel::Sobre);
                }
            }
            if (v.tipo == 1) {   // carros voadores: desenhados por cima de tudo na janela (mockup_qg2, VIVA), sem persiana
                p.vis = Vis{};
                for (int k = 0; k < v.nCarrosViva; ++k) {
                    const CarroViva& c = v.carrosViva[k];
                    const float x = cv::carroVivaX(c.fase0x100 / 100.0f, c.sentido, in.ms, jx0, jx1), y = px8(c.y8);
                    const float s = c.sentido > 0 ? 1.0f : -1.0f;
                    p.traco(t, x - 9.0f * s, y, x - 1.5f * s, y, 0.5f, Rgb{141, 18, 24});   // rastro: vermelho a 60 %
                    p.forma(t, true, x - 1.1f, y - 0.7f, x + 1.1f, y + 0.7f, deCor(c.farol), Pincel::Sobre);
                }
            }
            break;
        }
        case Relogio: {
            using tempo::cenaviva::EstadoRelogio;
            const RelogioCena& rc = v.relogio;
            const Retangulo& R = rc.brilho;
            const int w = R.x1 - R.x0;
            const Cor& c = in.relogio.estado == EstadoRelogio::Vitoria ? rc.corVitoria
                         : in.relogio.estado == EstadoRelogio::HoraExtra ? rc.corHoraExtra : rc.corFoco;
            const Faixa f = cobrir(R.x0, R.y0, R.x1, R.y1, t.f);
            for (int y = f.y0; y < f.y1; ++y)
                for (int x = f.x0; x < f.x1; ++x) {
                    const int i = (y - R.y0) * w + (x - R.x0);
                    const float m = relMascara_[i], cu = relCurto_[i], la = relLargo_[i];
                    if (m + cu + la < 0.0004f) continue;
                    const Rgb cor = in.grade.tratar(deCor(c), static_cast<float>(x), static_cast<float>(y));
                    t.luz(x, y, cor * la);   // como os mockups: largo, curto e o proprio tubo, cada um somado por "screen"
                    t.luz(x, y, cor * cu);
                    t.luz(x, y, cor * m);
                }
            break;
        }
        case Roteador:
            for (int i = 0; i < v.nRoteador; ++i) {
                caixaF(v.roteador[i].caixa, a0, b0, a1, b1);
                const float k = cv::roteadorAceso(i, in.ms) ? 1.0f : static_cast<float>(v.roteadorApagado) / 255.0f;
                p.forma(t, true, a0, b0, a1, b1, deCor(v.roteador[i].cor) * k, Pincel::Luz);
            }
            break;
        case Rack:
            for (int i = 0; i < v.nRack; ++i) {
                caixaF(v.rack[i].caixa, a0, b0, a1, b1);
                const bool aceso = cv::rackAceso(i / 4, i % 4, in.ms, static_cast<float>(v.rackAceso) / 255.0f);
                const Rgb cor = deCor(in.fundo == 1 ? v.rackVitoria : v.rack[i].cor);
                p.forma(t, true, a0, b0, a1, b1, cor * (aceso ? 1.0f : static_cast<float>(v.rackApagado) / 255.0f), Pincel::Luz);
            }
            break;
        case Olhos:
            if (in.fundo == 2 || cv::gatoPisca(in.quadro)) break;   // de manha os olhos nao brilham (mockup: amanhecer < 0,5)
            for (int i = 0; i < v.nOlhos; ++i) {
                caixaF(v.olhos[i].caixa, a0, b0, a1, b1);
                p.forma(t, true, a0, b0, a1, b1, deCor(v.olhos[i].cor), Pincel::Luz);
            }
            break;
        case Teclado: {
            const float forca = static_cast<float>(v.teclaForca) / 255.0f;
            for (int i = 0; i < v.nTeclas; ++i) {
                const float vis = static_cast<float>(v.teclasVis[in.fundo * v.nTeclas + i]) / 255.0f;
                if (vis <= 0.0f) continue;   // atras do personagem / da cadeira
                caixaF(v.teclas[i], a0, b0, a1, b1);
                const Rgb cor = in.fundo == 1 ? deCor(v.teclaVitoria)
                                              : misturaRgb(deCor(v.teclaA), deCor(v.teclaB), cv::teclaMistura(i, in.ms));
                p.forma(t, false, a0, b0, a1, b1, cor * forca, Pincel::Luz, vis);
            }
            break;
        }
        case FaixaGabinete:
            for (int i = 0; i < v.nFaixa; ++i) {
                caixaF(v.faixa[i], a0, b0, a1, b1);
                p.forma(t, false, a0, b0, a1, b1, misturaRgb(deCor(v.teclaA), deCor(v.teclaB), cv::faixaMistura(i, in.ms)),
                        Pincel::Luz);
            }
            break;
        case Sprite: {
            const SpriteLuz& s = v.sprite;
            const int w = s.r.x1 - s.r.x0, h = s.r.y1 - s.r.y0;
            const uint8_t* px = s.px + static_cast<size_t>(cv::quadroSprite(in.ms) % s.quadros) * w * h;
            const float maior = std::max({1.0f, float(s.tinta.r), float(s.tinta.g), float(s.tinta.b)});
            const Rgb tinta = deCor(s.tinta) * (1.0f / maior);
            const Faixa f = cobrir(s.r.x0, s.r.y0, s.r.x1, s.r.y1, t.f);
            for (int y = f.y0; y < f.y1; ++y)
                for (int x = f.x0; x < f.x1; ++x) {
                    const uint8_t v8 = px[(y - s.r.y0) * w + (x - s.r.x0)];
                    if (v8) t.luz(x, y, tinta * static_cast<float>(v8));   // a vinheta ja vem no sprite
                }
            break;
        }
        default: break;
        }
    }

    void CenaFoco::comporEPintar(ui::Context& ui, Retangulo r, const Instante& in) {
        r.x0 = std::max<int16_t>(r.x0, 0), r.y0 = std::max<int16_t>(r.y0, 0);
        r.x1 = std::min<int16_t>(r.x1, kLarg), r.y1 = std::min<int16_t>(r.y1, kAlt);
        const int w = r.x1 - r.x0;
        if (w <= 0 || r.y1 <= r.y0) return;
        const int linhas = std::max(1, static_cast<int>(kCapTile / static_cast<size_t>(w)));
        for (int y = r.y0; y < r.y1; y += linhas) {
            const Faixa f{r.x0, y, r.x1, std::min<int>(r.y1, y + linhas)};
            const int h = f.y1 - f.y0;
            std::memset(tileTocado_, 0, size_t(w) * h);
            Tile t{tileRgb_, tileTocado_, base_, f};
            for (int c = 0; c < kCamadas; ++c) {
                const Retangulo cx = caixa(c);
                if (cx.x1 > f.x0 && cx.x0 < f.x1 && cx.y1 > f.y0 && cx.y0 < f.y1) aplicar(c, t, in);
            }
            for (int yy = 0; yy < h; ++yy) {
                const uint16_t* b = base_ + (f.y0 + yy) * kLarg + f.x0;
                const uint8_t* tc = tileTocado_ + yy * w;
                const float* p = tileRgb_ + size_t(yy) * w * 3;
                uint16_t* o = tileSaida_ + yy * w;
                for (int x = 0; x < w; ++x)
                    o[x] = tc[x] ? pontilhar(std::clamp(static_cast<int>(p[x * 3] + 0.5f), 0, 255),
                                             std::clamp(static_cast<int>(p[x * 3 + 1] + 0.5f), 0, 255),
                                             std::clamp(static_cast<int>(p[x * 3 + 2] + 0.5f), 0, 255), f.x0 + x, f.y0 + yy)
                                 : b[x];
            }
            const ui::Rect rr{static_cast<int16_t>(f.x0), static_cast<int16_t>(f.y0), static_cast<int16_t>(w), static_cast<int16_t>(h)};
            ui.paint(rr, [&](Arduino_GFX& g, ui::Rect q) { g.draw16bitRGBBitmap(q.x, q.y, tileSaida_, w, h); });
        }
    }

    void CenaFoco::desenhar(ui::Context& ui, const Quadro& q, const tempo::cenaviva::Relogio& relogio, uint32_t ms) {
        if (fundo_ == nullptr || base_ == nullptr || cena_ == nullptr || !valido_) return;
        const int64_t t0 = esp_timer_get_time();
        const Cena& c = *cena_;
        const CenaViva* v = c.viva;
        Instante in;
        in.ms = ms;
        in.quadro = tempo::cenaviva::quadro(ms);
        in.fundo = std::clamp(static_cast<int>(fundoAtual_), 0, 2);
        in.relogio = relogio;
        in.treme = v != nullptr && v->tipo == 0 && tempo::cenaviva::neonTreme(in.quadro);
        in.doisPontos = tempo::cenaviva::doisPontosAcesos(ms);
        in.grade.ativa = v != nullptr && v->vinheta != 0;
        in.grade.sat = v != nullptr ? static_cast<float>(v->satx100[in.fundo == 1 ? 1 : 0]) / 100.0f : 1.0f;

        const ui::Rect tela{0, 0, kLarg, kAlt};
        const bool tudo = ui.redraw(tela, ui::Context::combine(0x43454E41u, geracao_), true);
        const bool baseNova = !basePronta_;
        if (baseNova) {
            std::memcpy(base_, fundo_, size_t(kLarg) * kAlt * sizeof(uint16_t));
            basePronta_ = true;
        }
        Retangulo sujos[kCamadas + 2];
        int nSujos = 0;
        {   // revelacao (no maximo ~1x/s: o App recalcula o quadro 1x/s)
            const Retangulo& r = c.revela;
            const int w = r.x1 - r.x0, h = r.y1 - r.y0;
            uint32_t sig = ui::Context::combine(0x52455600u, geracao_);
            if (q.revelar)
                sig = ui::Context::combine(sig, c.revelaModo == 0 ? static_cast<uint32_t>(tempo::cenafoco::linhasModo0(h, q.cheio))
                                                                  : static_cast<uint32_t>(q.tomados) + 1000U);
            if (baseNova || sig != sigRevela_) {
                comporRevelacao(q, w, h);
                copiarParaBase(r);
                sigRevela_ = sig;
                if (!tudo) sujos[nSujos++] = r;
            }
        }
        {   // mapa
            const Retangulo& m = c.mapa;
            const int w = m.x1 - m.x0, h = m.y1 - m.y0;
            uint32_t sig = ui::Context::combine(0x4D415000u, geracao_);
            sig = ui::Context::combine(sig, q.mapa ? 1U : 0U);
            if (q.mapa)
                for (const auto& no : q.nos)
                    sig = ui::Context::combine(sig, (static_cast<uint32_t>(no.tipo) << 8)
                                                        | static_cast<uint32_t>(std::lround(std::clamp(no.g, 0.0f, 1.0f) * 64.0f)));
            if (baseNova || sig != sigMapa_) {
                Quadro qq = q;   // g quantizado em 1/64 (o mesmo da assinatura: o desenho nao muda entre repinturas)
                for (auto& no : qq.nos) no.g = static_cast<float>(std::lround(std::clamp(no.g, 0.0f, 1.0f) * 64.0f)) / 64.0f;
                comporMapa(qq, w, h);
                copiarParaBase(m);
                sigMapa_ = sig;
                if (!tudo) sujos[nSujos++] = m;
            }
        }
        int camadasMudaram = 0;
        if (v != nullptr) {
            const uint32_t sigRel = ui::Context::combine(assinatura(Relogio, in), geracao_);
            if (sigRel != sigRelogioPronto_) {
                prepararRelogio(in);
                sigRelogioPronto_ = sigRel;
            }
            for (int k = 0; k < kCamadas; ++k) {
                const uint32_t s = assinatura(k, in);
                const Retangulo cx = caixa(k);
                if ((s != sigCamada_[k] || tudo) && cx.x1 > cx.x0 && cx.y1 > cx.y0) {
                    sujos[nSujos++] = cx;
                    ++camadasMudaram;
                }
                sigCamada_[k] = s;
            }
        }
        if (tudo) ui.paint(tela, [&](Arduino_GFX& g, ui::Rect r) { g.draw16bitRGBBitmap(r.x, r.y, base_, kLarg, kAlt); });
        if (!tudo && nSujos == 0) return;
        for (int i = 0; i < nSujos; ++i) comporEPintar(ui, sujos[i], in);
        ultimoQuadroUs_ = static_cast<uint32_t>(esp_timer_get_time() - t0);
        ESP_LOGD(kTag, "quadro composto em %lu us (fundo %d, %d regioes, %d camadas, tudo=%d)",
                 static_cast<unsigned long>(ultimoQuadroUs_), in.fundo, nSujos, camadasMudaram, tudo ? 1 : 0);
    }

} // namespace ui::cenas
