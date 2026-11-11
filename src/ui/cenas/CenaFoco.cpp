#include "ui/cenas/CenaFoco.h"

#include <JPEGDEC.h>
#include <esp_heap_caps.h>
#include <esp_log.h>
#include <esp_timer.h>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <new>
#include <utility>

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
    } // namespace

    CenaFoco::~CenaFoco() { liberar(); }

    void CenaFoco::liberar() {
        soltar(fundo_);
        soltar(patchMapa_);
        soltar(trabalho_);
        soltar(rgb_);
        soltar(tocado_);
        capTrabalho_ = capMapa_ = 0;
        cena_ = patchDe_ = nullptr;
        valido_ = false;
    }

    bool CenaFoco::alocar(const Cena& cena) {
        const auto area = [](const Retangulo& r) { return static_cast<size_t>(r.x1 - r.x0) * static_cast<size_t>(r.y1 - r.y0); };
        const size_t mapa = area(cena.mapa), trabalho = std::max(area(cena.revela), mapa);
        if (fundo_ == nullptr) fundo_ = static_cast<uint16_t*>(psram(size_t(kLarg) * kAlt * sizeof(uint16_t)));
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
        if (fundo_ == nullptr || capTrabalho_ == 0 || capMapa_ == 0) {
            ESP_LOGW(kTag, "sem PSRAM para a cena (fundo=%p)", static_cast<void*>(fundo_));
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

    void CenaFoco::desenhar(ui::Context& ui, const Quadro& q) {
        if (fundo_ == nullptr || cena_ == nullptr || !valido_) return;
        const int64_t t0 = esp_timer_get_time();
        bool pintou = false;
        const Cena& c = *cena_;

        const ui::Rect tela{0, 0, kLarg, kAlt};
        if (ui.redraw(tela, ui::Context::combine(0x43454E41u, geracao_), true)) {
            ui.paint(tela, [&](Arduino_GFX& g, ui::Rect r) { g.draw16bitRGBBitmap(r.x, r.y, fundo_, kLarg, kAlt); });
            pintou = true;
        }

        {   // revelacao (sempre a mesma regiao, para o ui::Context nao limpar nada fora de hora)
            const Retangulo& r = c.revela;
            const int w = r.x1 - r.x0, h = r.y1 - r.y0;
            uint32_t sig = ui::Context::combine(0x52455600u, geracao_);
            if (q.revelar) {
                sig = ui::Context::combine(sig, c.revelaModo == 0 ? static_cast<uint32_t>(tempo::cenafoco::linhasModo0(h, q.cheio))
                                                                  : static_cast<uint32_t>(q.tomados) + 1000U);
            }
            const ui::Rect rect{r.x0, r.y0, static_cast<int16_t>(w), static_cast<int16_t>(h)};
            if (ui.redraw(rect, sig, true)) {
                comporRevelacao(q, w, h);
                ui.paint(rect, [&](Arduino_GFX& g, ui::Rect rr) { g.draw16bitRGBBitmap(rr.x, rr.y, trabalho_, w, h); });
                pintou = true;
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
            const ui::Rect rect{m.x0, m.y0, static_cast<int16_t>(w), static_cast<int16_t>(h)};
            if (ui.redraw(rect, sig, true)) {
                Quadro qq = q;   // g quantizado em 1/64 (o mesmo da assinatura: o desenho nao muda entre repinturas)
                for (auto& no : qq.nos) no.g = static_cast<float>(std::lround(std::clamp(no.g, 0.0f, 1.0f) * 64.0f)) / 64.0f;
                comporMapa(qq, w, h);
                ui.paint(rect, [&](Arduino_GFX& g, ui::Rect rr) { g.draw16bitRGBBitmap(rr.x, rr.y, trabalho_, w, h); });
                pintou = true;
            }
        }
        if (pintou)
            ESP_LOGD(kTag, "quadro composto em %lld us (fundo %d)", static_cast<long long>(esp_timer_get_time() - t0),
                     static_cast<int>(fundoAtual_));
    }

} // namespace ui::cenas
