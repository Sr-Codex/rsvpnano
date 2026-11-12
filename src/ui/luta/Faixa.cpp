#include "ui/luta/Faixa.h"

#include <algorithm>
#include <cmath>
#include <cstring>

#include "text/Utf8Text.h"
#include "ui/AnelSuave.h"
#include "ui/SansFont.h"

namespace ui::luta {

    namespace {
        using tempo::luta::Efeitos;
        using tempo::luta::Modo;
        using tempo::luta::Rng;

        // Paleta do mockup (tools/mockup_tempo_luta.py)
        constexpr Cor kLaranja{255, 120, 50}, kAmbar{255, 196, 80};
        constexpr Cor kCarmim{240, 70, 100}, kVinho{150, 20, 60}, kCarmimClaro{255, 150, 170};
        constexpr Cor kQuente{255, 236, 200}, kBranco{255, 255, 245};
        constexpr Cor kAzul{80, 160, 255}, kAzul2{150, 205, 255};
        constexpr Cor kAgua{50, 200, 188}, kAgua2{140, 232, 222};
        constexpr Cor kTrilho{22, 14, 14}, kTrilhoAgua{12, 26, 26};
        constexpr float kPi = 3.14159265f;
        constexpr float kForcaGrao = 1.5f;   // intensidade de cada grao de po (4 espalhamentos de peso 1)

        inline float lim01(float v) { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
        inline Cor esc(const Cor& c, float k) { return {c.r * k, c.g * k, c.b * k}; }
        inline float phi(float z) { return 0.5f * std::erfc(-z * 0.70710678f); }   // CDF normal

        // Tela de pintura (RGB 8 bits, regiao kLarg x kAlt)
        struct Tela {
            uint8_t* p;
            inline uint8_t* at(int x, int y) const { return p + (size_t(y) * kLarg + size_t(x)) * 3; }
            inline bool dentro(int x, int y) const { return x >= 0 && y >= 0 && x < kLarg && y < kAlt; }
            static inline uint8_t u8(float v) { return uint8_t(v < 0.0f ? 0.0f : (v > 255.0f ? 255.0f : v) + 0.5f); }
            // mistura: px = px*(1-a) + c*a
            void mistura(int x, int y, const Cor& c, float a) const {
                if (!dentro(x, y) || a <= 0.0f) return;
                a = std::min(1.0f, a);
                uint8_t* q = at(x, y);
                q[0] = u8(q[0] + (c.r - q[0]) * a);
                q[1] = u8(q[1] + (c.g - q[1]) * a);
                q[2] = u8(q[2] + (c.b - q[2]) * a);
            }
            // soma luz (aditivo, satura em 255)
            void soma(int x, int y, const Cor& c, float a) const {
                if (!dentro(x, y) || a <= 0.0f) return;
                uint8_t* q = at(x, y);
                q[0] = u8(q[0] + c.r * a);
                q[1] = u8(q[1] + c.g * a);
                q[2] = u8(q[2] + c.b * a);
            }
            // lighten: px = max(px, c)
            void maximo(int x, int y, uint8_t r, uint8_t g, uint8_t b) const {
                if (!dentro(x, y)) return;
                uint8_t* q = at(x, y);
                q[0] = std::max(q[0], r);
                q[1] = std::max(q[1], g);
                q[2] = std::max(q[2], b);
            }
        };

        // Segmento com borda suavizada (cobertura pela distancia ao segmento) e luz aditiva.
        void segmento(const Tela& t, float x0, float y0, float x1, float y1, float w, const Cor& c, float alfa) {
            const float m = w * 0.5f + 1.0f;
            const int xa = int(std::floor(std::min(x0, x1) - m)), xb = int(std::ceil(std::max(x0, x1) + m));
            const int ya = int(std::floor(std::min(y0, y1) - m)), yb = int(std::ceil(std::max(y0, y1) + m));
            const float vx = x1 - x0, vy = y1 - y0, l2 = vx * vx + vy * vy;
            for (int y = std::max(0, ya); y <= std::min(kAlt - 1, yb); ++y)
                for (int x = std::max(0, xa); x <= std::min(kLarg - 1, xb); ++x) {
                    const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                    const float u = l2 > 0.0f ? lim01(((px - x0) * vx + (py - y0) * vy) / l2) : 0.0f;
                    const float dx = px - (x0 + u * vx), dy = py - (y0 + u * vy);
                    const float cob = lim01(w * 0.5f + 0.5f - std::sqrt(dx * dx + dy * dy));
                    if (cob > 0.0f) t.soma(x, y, c, cob * alfa);
                }
        }

        struct Ponto2 {
            float x, y;
        };

        // Polilinha em zigue-zague de a ate b (como o `raio` do mockup: deslocamento perpendicular menor nas pontas).
        int raio(Rng& rng, Ponto2 a, Ponto2 b, int n, float amp, Ponto2* pts) {
            const float dx = b.x - a.x, dy = b.y - a.y;
            const float ln = std::max(0.001f, std::sqrt(dx * dx + dy * dy));
            const float px = -dy / ln, py = dx / ln;
            int k = 0;
            pts[k++] = a;
            for (int i = 1; i < n; ++i) {
                const float u = float(i) / float(n);
                const float d = std::sin(kPi * u) * amp * rng.entre(-1.0f, 1.0f);
                pts[k++] = {a.x + dx * u + px * d, a.y + dy * u + py * d};
            }
            pts[k++] = b;
            return k;
        }

        void faiscas(const Tela& t, Rng& rng, float x, float y, int n, const Cor& cor, float ang0, float ang1, float l0, float l1) {
            for (int i = 0; i < n; ++i) {
                const float a = rng.entre(ang0, ang1) * kPi / 180.0f;
                const float r0 = rng.entre(1.0f, 4.0f), ll = rng.entre(l0, l1), w = rng.entre(0.9f, 1.6f);
                const float ca = std::cos(a), sa = std::sin(a);
                segmento(t, x + r0 * ca, y + r0 * sa, x + (r0 + ll) * ca, y + (r0 + ll) * sa, w, cor, 0.85f);
            }
        }

        // Halo gaussiano pre-calculado (uma vez): valor de um disco de raio R borrado com sigma, em funcao da distancia.
        struct HaloRadial {
            static constexpr int kN = 48;
            float v[kN + 1];
            HaloRadial(float R, float sigma) {
                for (int i = 0; i <= kN; ++i) v[i] = phi((R - float(i)) / sigma);
            }
            float em(float d) const {
                if (d >= float(kN)) return 0.0f;
                const int i = int(d);
                const float f = d - float(i);
                return v[i] + (v[std::min(kN, i + 1)] - v[i]) * f;
            }
        };

        // Pill (trilho) -> cobertura 0..255
        void montarPilula(uint8_t* m) {
            const float cx = float(kBarraLarg) * 0.5f, cy = float(kBarraAlt) * 0.5f, r = float(kBarraAlt) * 0.5f;
            const float a = cx - r;
            for (int y = 0; y < kBarraAlt; ++y)
                for (int x = 0; x < kBarraLarg; ++x) {
                    const float px = float(x) + 0.5f, py = float(y) + 0.5f;
                    const float dx = std::max(std::fabs(px - cx) - a, 0.0f), dy = std::fabs(py - cy);
                    const float d = std::sqrt(dx * dx + dy * dy) - r;
                    m[y * kBarraLarg + x] = uint8_t(lim01(0.5f - d) * 255.0f + 0.5f);
                }
        }

        // texto / icone dentro do buffer
        int baseDoTexto(const ui::sans::Font& f, int yc) { return yc + (int(f.ascent) - (int(f.height) - int(f.ascent)) + 1) / 2; }

        void desenharTexto(const Tela& t, const std::string& s, int tam, int x, int yc, const Cor& cor) {
            const auto& f = ui::sans::font(uint8_t(tam));
            const int base = baseDoTexto(f, yc);
            uint32_t cp;
            std::string_view sv(s);
            while (Utf8Text::next(sv, cp)) {
                const ui::sans::Glyph* g = ui::sans::glyph(f, cp);
                if (!g) g = ui::sans::glyph(f, '?');
                for (int row = 0; row < g->height; ++row)
                    for (int col = 0; col < g->width; ++col) {
                        const int idx = row * g->width + col;
                        const uint8_t pk = f.pixels[g->offset + idx / 2];
                        const unsigned a = (idx & 1) ? (pk & 15U) : (pk >> 4);
                        if (a) t.mistura(x + g->left + col, base + g->top + row, cor, float(a) / 15.0f);
                    }
                x += g->advance;
            }
        }

        void desenharIcone(const Tela& t, icons::phosphor::Id id, int cx, int cy, int lado, const Cor& cor) {
            const icons::phosphor::Mascara* melhor = nullptr;
            for (const auto& m : icons::phosphor::kMascaras) {
                if (m.id != id) continue;
                if (!melhor || std::abs(int(m.lado) - lado) < std::abs(int(melhor->lado) - lado)) melhor = &m;
            }
            if (!melhor) return;
            const int x0 = cx - melhor->lado / 2, y0 = cy - melhor->lado / 2;
            const uint8_t* p = melhor->alfa;
            for (int y = 0; y < melhor->lado; ++y)
                for (int x = 0; x < melhor->lado; x += 2) {
                    const uint8_t b = *p++;
                    for (int k = 0; k < 2; ++k) {
                        const unsigned a = k == 0 ? (b >> 4) : (b & 15U);
                        if (a) t.mistura(x0 + x + k, y0 + y, cor, float(a) / 15.0f);
                    }
                }
        }

        inline float gradiente(float x, float xa, float xb) { return xb > xa ? lim01((x - xa) / (xb - xa)) : 0.0f; }
    } // namespace

    float frenteX(float frente) { return float(kBarraX0) + float(kBarraLarg) * lim01(frente); }
    Cor misturarCor(const Cor& a, const Cor& b, float t) { return {a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t}; }
    int larguraTexto(const std::string& texto, int tamanho) { return ui::sans::width(texto, ui::sans::font(uint8_t(tamanho))); }

    Faixa::Faixa(const Memoria& m) : mem_(m) {
        if (mem_.pilula) montarPilula(mem_.pilula);
    }

    void Faixa::limpar(const Cor& fundo) {
        const uint8_t r = Tela::u8(fundo.r), g = Tela::u8(fundo.g), b = Tela::u8(fundo.b);
        uint8_t* p = mem_.rgb;
        for (int i = 0; i < kLarg * kAlt; ++i) {
            *p++ = r;
            *p++ = g;
            *p++ = b;
        }
    }

    void Faixa::rotulo(const Rotulo& r, bool aDireita, float apagado, const Cor& fundo) {
        if (r.texto.empty() && !r.temIcone) return;
        const Tela t{mem_.rgb};
        const Cor cor = misturarCor(r.cor, fundo, apagado);
        constexpr int kYc = 8;   // centro da linha (tela: y 106)
        if (!aDireita) {
            if (r.temIcone) desenharIcone(t, r.icone, 17, kYc, 16, cor);
            desenharTexto(t, r.texto, 15, r.temIcone ? 30 : 8, kYc, cor);
        } else {
            const int direita = r.temIcone ? 444 : 460;
            desenharTexto(t, r.texto, 15, direita - larguraTexto(r.texto, 15), kYc, cor);
            if (r.temIcone) desenharIcone(t, r.icone, 453, kYc, 16, cor);
        }
    }

    // ------------------------------------------------------------------------------------------------ barra
    void Faixa::barra(const Quadro& q) {
        const Tela t{mem_.rgb};
        const Efeitos fx = tempo::luta::efeitos(q.modo);
        const float X0 = float(kBarraX0), X1 = float(kBarraX1);
        const Cor fundo = q.fundo;

        // x da divisa entre as duas forcas (ou do fim do preenchimento)
        float xd = frenteX(q.frente);
        if (q.modo == Modo::HoraExtra) xd = frenteX(q.extra);
        else if (q.modo == Modo::PausaCiclo) xd = frenteX(q.pausa);
        else if (q.modo == Modo::GolpeFinal) {
            const float xs = X1 - float(kPedacoPx);
            xd = xs + (X1 - xs) * tempo::luta::frenteDoGolpe(q.golpe);
        }
        const float apL = fx.apagado, apR = q.modo == Modo::Distraido ? 0.0f : fx.apagado;

        for (int x = kBarraX0; x < kBarraX1; ++x) {
            const float px = float(x) + 0.5f;
            const float wl = lim01(xd - float(x));   // fracao da coluna do lado esquerdo (corte reto na frente)
            Cor esq, dir;
            switch (q.modo) {
            case Modo::HoraExtra: {
                // base laranja inteira; o azul extra entra pela esquerda (ate xd)
                const Cor base = misturarCor(kLaranja, kAmbar, gradiente(px, X0, X1));
                const Cor az = misturarCor(kAzul, kAzul2, gradiente(px, X0, xd));
                esq = misturarCor(az, fundo, q.tregua ? 0.5f : 0.0f);
                dir = misturarCor(base, fundo, q.tregua ? 0.5f : 0.0f);
                break;
            }
            case Modo::PausaCiclo: {
                esq = misturarCor(misturarCor(kAgua, kAgua2, gradiente(px, X0, xd)), fundo, 0.12f);
                dir = kTrilhoAgua;
                break;
            }
            case Modo::GolpeFinal: {
                esq = misturarCor(kLaranja, kAmbar, gradiente(px, X0, X1));
                dir = fundo;   // o resto e a camada do inimigo (ou nada)
                break;
            }
            default:
                esq = misturarCor(misturarCor(kLaranja, kAmbar, gradiente(px, X0, xd)), fundo, apL);
                dir = misturarCor(misturarCor(kCarmim, kVinho, gradiente(px, xd, X1)), fundo, apR);
                break;
            }
            const Cor c = misturarCor(dir, esq, wl);
            for (int y = kBarraY0; y < kBarraY1; ++y) {
                const float cov = float(mem_.pilula[(y - kBarraY0) * kBarraLarg + (x - kBarraX0)]) / 255.0f;
                if (cov <= 0.0f) continue;
                if (q.modo == Modo::GolpeFinal && wl <= 0.0f) continue;   // alem da frente: so a camada do inimigo
                if (q.modo == Modo::GolpeFinal) t.mistura(x, y, c, cov * wl);
                else t.mistura(x, y, c, cov);
            }
        }
    }

    // ------------------------------------------------------------------------------------------------ luzes
    void Faixa::luzes(const Quadro& q) {
        const Tela t{mem_.rgb};
        const Efeitos fx = tempo::luta::efeitos(q.modo);
        const float cy = float(kBarraY0 + kBarraY1) * 0.5f;
        const float y0 = float(kBarraY0) - 3.0f, y1 = float(kBarraY1) + 3.0f;
        const float p = q.pulso;
        const bool animar = q.animar;

        float xl = frenteX(q.frente);
        if (q.modo == Modo::HoraExtra) xl = frenteX(q.extra);
        else if (q.modo == Modo::PausaCiclo) xl = frenteX(q.pausa);
        else if (q.modo == Modo::GolpeFinal) {
            const float xs = float(kBarraX1 - kPedacoPx);
            xl = xs + (float(kBarraX1) - xs) * tempo::luta::frenteDoGolpe(q.golpe);
        }

        // ---- parametros da frente por modo
        Cor linha = kQuente, halo = esc(kAmbar, 0.85f * p);
        float larguraLinha = 2.4f, larguraHalo = 12.0f, raioDisco = 4.5f * p + 1.5f, raioHalo = 17.0f;
        bool disco = true;
        switch (q.modo) {
        case Modo::Focando:
        case Modo::GolpeFinal: break;
        case Modo::FimSegurando:
            linha = esc(kQuente, 0.75f);
            halo = esc(kAmbar, 0.40f);
            larguraLinha = 2.2f;
            larguraHalo = 9.0f;
            disco = false;
            break;
        case Modo::Distraido:
            linha = Cor{255, 200, 215};
            halo = esc(kCarmim, 0.85f * p);
            break;
        case Modo::Pausado:
        case Modo::PausaCiclo:
            linha = kAgua2;
            halo = esc(kAgua, 0.55f);
            larguraLinha = 2.2f;
            larguraHalo = 9.0f;
            disco = false;
            break;
        case Modo::HoraExtra:
            if (q.tregua) {   // pausada na hora extra: divisoria fria, sem clarao
                linha = kAgua2;
                halo = esc(kAgua, 0.55f);
                larguraLinha = 2.2f;
                larguraHalo = 9.0f;
                disco = false;
                break;
            }
            linha = Cor{235, 245, 255};
            halo = esc(kAzul2, 0.80f * p);
            larguraLinha = 2.2f;
            larguraHalo = 11.0f;
            raioDisco = 4.5f * p + 0.5f;
            raioHalo = 18.0f;
            break;
        }
        if (!fx.choque && !fx.divisoria) return;
        Cor branco = kBranco;
        if (q.modo == Modo::GolpeFinal) {   // no fim do golpe a frente se apaga (a hora extra acende a dela, em azul)
            const float u = lim01((q.golpe - 0.95f) / 0.25f);
            const float fade = 1.0f - u * u * (3.0f - 2.0f * u);
            linha = esc(linha, fade);
            halo = esc(halo, fade);
            branco = esc(branco, fade);
            if (fade <= 0.001f) return;
        }

        // ---- halo da linha: gaussiana separavel (perfil de uma faixa de largura `larguraHalo` borrada com sigma 5)
        constexpr float kSig = 5.0f;
        static const HaloRadial discoLut(17.0f, kSig);
        {
            const int xa = std::max(0, int(xl) - 26), xb = std::min(kLarg - 1, int(xl) + 26);
            float hy[kAlt];
            for (int y = 0; y < kAlt; ++y) {
                const float py = float(y) + 0.5f;
                float v = phi((py - y0) / kSig) - phi((py - y1) / kSig);
                // o halo nao pode ser cortado na borda de baixo da faixa (o rodape comeca logo abaixo): some suave
                const float taper = lim01((float(kAlt) - py) / 9.0f);
                hy[y] = v * taper * taper * (3.0f - 2.0f * taper);
            }
            for (int x = xa; x <= xb; ++x) {
                const float dx = float(x) + 0.5f - xl;
                const float hx = phi((dx + larguraHalo * 0.5f) / kSig) - phi((dx - larguraHalo * 0.5f) / kSig);
                if (hx <= 0.002f) continue;
                for (int y = 0; y < kAlt; ++y) t.soma(x, y, halo, hx * hy[y]);
            }
            if (disco) {
                const float RR = raioHalo / 17.0f;
                for (int y = 0; y < kAlt; ++y) {
                    const float py = float(y) + 0.5f;
                    const float taper = lim01((float(kAlt) - py) / 9.0f);
                    const float tp = taper * taper * (3.0f - 2.0f * taper);
                    for (int x = std::max(0, int(xl) - 40); x <= std::min(kLarg - 1, int(xl) + 40); ++x) {
                        const float dx = float(x) + 0.5f - xl, dy = py - cy;
                        const float d = std::sqrt(dx * dx + dy * dy) / RR;
                        const float v = discoLut.em(d);
                        if (v > 0.002f) t.soma(x, y, halo, v * 0.85f * tp);
                    }
                }
            }
        }
        // ---- nucleo da linha (AA)
        for (int x = std::max(0, int(xl) - 3); x <= std::min(kLarg - 1, int(xl) + 3); ++x) {
            const float cx = lim01(larguraLinha * 0.5f + 0.5f - std::fabs(float(x) + 0.5f - xl));
            if (cx <= 0.0f) continue;
            for (int y = 0; y < kAlt; ++y) {
                const float py = float(y) + 0.5f;
                const float cv = lim01(std::min(py - y0, y1 - py) + 0.5f);
                if (cv > 0.0f) t.soma(x, y, linha, cx * cv);
            }
        }
        if (disco) {
            for (int y = int(cy - raioDisco) - 2; y <= int(cy + raioDisco) + 2; ++y)
                for (int x = int(xl - raioDisco) - 2; x <= int(xl + raioDisco) + 2; ++x) {
                    const float dx = float(x) + 0.5f - xl, dy = float(y) + 0.5f - cy;
                    const float cv = lim01(raioDisco + 0.5f - std::sqrt(dx * dx + dy * dy));
                    if (cv > 0.0f) t.soma(x, y, branco, cv);
                }
        }

        // ---- faiscas, raios e setas (so animando; o quadro de repouso fica limpo)
        if (!animar) return;
        Rng rng(q.quadro * 2654435761u + uint32_t(q.modo) * 97u + 1u);
        switch (q.modo) {
        case Modo::Focando:
            faiscas(t, rng, xl + 2.0f, cy, 13, Cor{255, 215, 160}, -75.0f, 75.0f, 4.0f, 15.0f);   // golpes no inimigo
            faiscas(t, rng, xl - 2.0f, cy, 4, Cor{255, 180, 150}, 150.0f, 210.0f, 3.0f, 7.0f);
            for (int k = 0; k < 3; ++k) {   // pequenos raios rachando a parte carmim
                Ponto2 pts[8];
                const Ponto2 a{xl + 3.0f, cy + rng.entre(-6.0f, 6.0f)};
                const Ponto2 b{xl + rng.entre(16.0f, 30.0f), cy + rng.entre(-9.0f, 9.0f)};
                const int n = raio(rng, a, b, 4, 3.0f, pts);
                for (int i = 0; i + 1 < n; ++i) segmento(t, pts[i].x, pts[i].y, pts[i + 1].x, pts[i + 1].y, 1.0f, Cor{255, 200, 150}, 1.0f);
            }
            break;
        case Modo::Distraido:
            faiscas(t, rng, xl - 2.0f, cy, 13, Cor{255, 160, 185}, 105.0f, 255.0f, 4.0f, 15.0f);   // o inimigo empurra
            for (int k = 0; k < 3; ++k) {   // setas ‹‹‹ do inimigo avancando sobre o laranja
                const float x = xl - 22.0f - float(k) * 16.0f;
                const Cor c = esc(Cor{255, 140, 165}, 0.9f - 0.25f * float(k));
                segmento(t, x + 5.0f, cy - 6.0f, x, cy, 1.8f, c, 1.0f);
                segmento(t, x, cy, x + 5.0f, cy + 6.0f, 1.8f, c, 1.0f);
            }
            break;
        case Modo::HoraExtra:
            faiscas(t, rng, xl + 2.0f, cy, 8, Cor{200, 228, 255}, -70.0f, 70.0f, 4.0f, 12.0f);
            break;
        default: break;
        }
    }

    // ------------------------------------------------------------------------------------------------ golpe final
    void Faixa::poeira(const Quadro& q) {
        if (nParticulas_ == 0 || !camadaPronta_ || mem_.camada == nullptr) return;
        const Tela t{mem_.rgb};
        const float prog = q.golpe;
        for (uint32_t i = 0; i < nParticulas_; ++i) {
            const tempo::luta::Particula& p = mem_.particulas[i];
            const auto pt = tempo::luta::estadoParticula(p, prog);
            if (!pt.solta) {   // ainda nao soltou: o bloco 2x2 do pedaco segue la (so clareia)
                const int bx = mem_.blocos[i * 2], by = mem_.blocos[i * 2 + 1];
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const uint8_t* c = mem_.camada + (size_t(by + dy) * kCamadaLarg + size_t(bx + dx - kCamadaX0)) * 3;
                        t.maximo(bx + dx, by + dy, c[0], c[1], c[2]);
                    }
                continue;
            }
            if (pt.a <= 0.0f) continue;
            // grao de po: 1 particula representa o bloco 2x2 de origem -> 4 espalhamentos bilineares (borda suave, grao de ~2 px)
            const Cor c{float(pt.r), float(pt.g), float(pt.b)};
            // fade nas bordas de cima da faixa (o po sobe para fora dela): evita corte seco
            const float topo = lim01(pt.y / 12.0f);
            const float k = pt.a * kForcaGrao * topo;
            for (int sy = 0; sy < 2; ++sy)
                for (int sx = 0; sx < 2; ++sx) {
                    const float fx = pt.x + (sx ? 0.5f : -0.5f) - 0.5f, fy = pt.y + (sy ? 0.5f : -0.5f) - 0.5f;
                    const int ix = int(std::floor(fx)), iy = int(std::floor(fy));
                    const float ax = fx - float(ix), ay = fy - float(iy);
                    t.soma(ix, iy, c, (1 - ax) * (1 - ay) * k);
                    t.soma(ix + 1, iy, c, ax * (1 - ay) * k);
                    t.soma(ix, iy + 1, c, (1 - ax) * ay * k);
                    t.soma(ix + 1, iy + 1, c, ax * ay * k);
                }
        }
    }

    bool Faixa::prepararPoeira(const Rotulo& inimigo, uint32_t semente, const Cor& fundo) {
        nParticulas_ = 0;
        camadaPronta_ = false;
        if (!mem_.camada || !mem_.particulas || !mem_.blocos || !mem_.rgb || !mem_.pilula) return false;
        // 1) renderiza o inimigo (pedaco carmim + rotulo) no buffer principal
        limpar(fundo);
        const Tela t{mem_.rgb};
        const float xs = float(kBarraX1 - kPedacoPx);
        for (int x = int(xs); x < kBarraX1; ++x) {
            const Cor c = misturarCor(kCarmimClaro, kCarmim, gradiente(float(x) + 0.5f, xs, float(kBarraX1)));
            for (int y = kBarraY0; y < kBarraY1; ++y) {
                const float cov = float(mem_.pilula[(y - kBarraY0) * kBarraLarg + (x - kBarraX0)]) / 255.0f;
                t.mistura(x, y, c, cov);
            }
        }
        rotulo(inimigo, true, 0.0f, fundo);
        // 2) copia a regiao da camada
        for (int y = 0; y < kAlt; ++y)
            std::memcpy(mem_.camada + size_t(y) * kCamadaLarg * 3, mem_.rgb + (size_t(y) * kLarg + size_t(kCamadaX0)) * 3, size_t(kCamadaLarg) * 3);
        // 3) amostra 1 particula por bloco de 2x2 px com conteudo
        Rng rng(semente);
        uint32_t n = 0;
        for (int by = 0; by + 1 < kAlt && n < kMaxParticulas; by += 2)
            for (int bx = kCamadaX0; bx + 1 < kLarg && n < kMaxParticulas; bx += 2) {
                int melhorSoma = 0;
                uint8_t cr = 0, cg = 0, cb = 0;
                for (int dy = 0; dy < 2; ++dy)
                    for (int dx = 0; dx < 2; ++dx) {
                        const uint8_t* c = mem_.camada + (size_t(by + dy) * kCamadaLarg + size_t(bx + dx - kCamadaX0)) * 3;
                        const int soma = int(c[0]) + int(c[1]) + int(c[2]);
                        if (soma > melhorSoma) {
                            melhorSoma = soma;
                            cr = c[0];
                            cg = c[1];
                            cb = c[2];
                        }
                    }
                if (melhorSoma < 60) continue;
                tempo::luta::Semente s;
                s.x = float(bx) + 1.0f;
                s.y = float(by) + 1.0f;
                s.f = (s.x - xs) / float(kPedacoPx);
                s.r = cr;
                s.g = cg;
                s.b = cb;
                mem_.particulas[n] = tempo::luta::criarParticula(s, rng);
                mem_.blocos[n * 2] = uint16_t(bx);
                mem_.blocos[n * 2 + 1] = uint16_t(by);
                ++n;
            }
        nParticulas_ = n;
        camadaPronta_ = n > 0;
        return camadaPronta_;
    }

    // ------------------------------------------------------------------------------------------------ saida
    void Faixa::converter() {
        const uint8_t* p = mem_.rgb;
        uint16_t* o = mem_.saida;
        for (int y = 0; y < kAlt; ++y)
            for (int x = 0; x < kLarg; ++x, p += 3)
                *o++ = ui::anel::Anel::para565(ui::anel::Rgb{float(p[0]), float(p[1]), float(p[2])}, x + kTelaX, y + kTelaY);
    }

    void Faixa::compor(const Quadro& q) {
        if (!mem_.rgb || !mem_.saida || !mem_.pilula) return;
        const Efeitos fx = tempo::luta::efeitos(q.modo);
        limpar(q.fundo);
        const float apRot = (q.modo == Modo::FimSegurando || q.modo == Modo::Pausado) ? fx.apagado
                            : (q.modo == Modo::HoraExtra && q.tregua) ? 0.5f : 0.0f;
        rotulo(q.esq, false, apRot, q.fundo);
        if (q.modo != Modo::GolpeFinal || !camadaPronta_) rotulo(q.dir, true, apRot, q.fundo);
        barra(q);
        if (q.modo == Modo::GolpeFinal && fx.poeira) poeira(q);
        luzes(q);
        converter();
    }

} // namespace ui::luta
