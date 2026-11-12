#include "ui/screens/TempoScreen.h"
#include "tempo/HabitosVista.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>

#include "storage/tempo/TempoStorage.h"
#include "som/Regras.h"
#include "tempo/Parametros.h"
#include "tempo/Regua.h"
#include "tempo/Sessao.h"
#include "ui/ReguaSuave.h"
#include "ui/AnelSuave.h"
#include "ui/SansFont.h"
#include "ui/screens/HabitosIcones.h"
#include "tempo/Progressao.h"
#include "tempo/Relogio.h"

namespace screens {
    namespace {
        using ui::Rect;
        using ui::TextAlign;
        using ui::modern::Style;
        using ui::modern::button;
        using namespace ui::themes;

        
        // ------------------------------------------------------------ formatacao
        std::string fmtRelogio(int64_t totalSeg) { return tempo::formatarRelogio(totalSeg); }   // h:mm:ss a partir de 1 h

        std::string fmtHoraMin(int minutos) {
            char texto[24];
            if (minutos >= 60) std::snprintf(texto, sizeof texto, "%dh%02d", minutos / 60, minutos % 60);
            else std::snprintf(texto, sizeof texto, "%dmin", minutos);
            return texto;
        }

        std::string fmtNum(double valor) {
            char texto[24];
            const double arred = std::round(valor);
            if (std::fabs(valor - arred) < 0.05) std::snprintf(texto, sizeof texto, "%.0f", arred);
            else std::snprintf(texto, sizeof texto, "%.1f", valor);
            for (char* c = texto; *c; ++c)
                if (*c == '.') *c = ',';  // pt-BR: vírgula decimal
            return texto;
        }

        std::string fmtSinal(double valor) {
            return (valor >= 0 ? "+" : "") + fmtNum(valor);
        }

        std::string dataCabecalho(const tempo::Agora& agora) {
            if (!agora.valida) return "data desconhecida";
            static constexpr const char* kSemana[] = {"SEG", "TER", "QUA", "QUI", "SEX", "SÁB", "DOM"};
            static constexpr const char* kMes[] = {"JAN", "FEV", "MAR", "ABR", "MAI", "JUN",
                                                   "JUL", "AGO", "SET", "OUT", "NOV", "DEZ"};
            char texto[40];
            std::snprintf(texto, sizeof texto, "%s, %d %s", kSemana[tempo::diaDaSemana(agora.data)], agora.data.dia,
                          kMes[std::clamp(agora.data.mes, 1, 12) - 1]);
            return texto;
        }

        std::string horaTexto(const tempo::Agora& agora) {
            if (!agora.valida) return {};
            char texto[16];
            std::snprintf(texto, sizeof texto, "%02d:%02d:%02d", agora.segundoDoDia / 3600, agora.segundoDoDia / 60 % 60,
                          agora.segundoDoDia % 60);
            return texto;
        }

        const char* nomeAtributo(tempo::Atributo atributo) {
            switch (atributo) {
            case tempo::Atributo::Oficio: return "Ofício";
            case tempo::Atributo::Mente: return "Mente";
            case tempo::Atributo::Corpo: return "Corpo";
            default: return "Energia";
            }
        }

        // ------------------------------------------------------------- desenho
        using Desenho = std::function<void(Arduino_GFX&, int, int)>;  // (gfx, dx, dy) somados as coords absolutas

        // Regiao opaca desenhada so quando a assinatura muda.
        void regiao(ui::Context& ui, Rect area, uint32_t assinatura, const Desenho& desenho) {
            if (!ui.redraw(area, assinatura, true)) return;
            ui.paint(area, [&](Arduino_GFX& g, Rect alvo) {
                g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, ui.color(Background));
                desenho(g, alvo.x - area.x, alvo.y - area.y);
            });
        }

        // Mistura RGB565: t = 0 -> a, t = 1 -> b.
        uint16_t misturar565(uint16_t a, uint16_t b, double t) {
            const auto canal = [&](int sh, int mx) {
                const int ca = (a >> sh) & mx, cb = (b >> sh) & mx;
                return int(std::lround(ca + (cb - ca) * t));
            };
            return uint16_t((canal(11, 31) << 11) | (canal(5, 63) << 5) | canal(0, 31));
        }

        // Retangulo arredondado com borda suavizada (cantos pixel a pixel; retas por fillRect). `fundo` = cor por tras.
        void retanguloSuave565(Arduino_GFX& g, int x, int y, int w, int h, int r, uint16_t cor, uint16_t fundo, int borda = 0,
                               uint16_t corBorda = 0) {
            g.startWrite();
            ui::regua::retanguloSuave(
                [&](int px, int py, uint16_t c) { g.writePixel(int16_t(px), int16_t(py), c); },
                [&](int rx, int ry, int rw, int rh, uint16_t c) { if (rw > 0 && rh > 0) g.writeFillRect(int16_t(rx), int16_t(ry), int16_t(rw), int16_t(rh), c); },
                x, y, w, h, r, ui::regua::deRgb565(cor), ui::regua::deRgb565(fundo), borda, ui::regua::deRgb565(corBorda));
            g.endWrite();
        }

        // Bloco (cartao ou linha) com texto proprio sobre fundo uniforme.
        struct Bloco {
            ui::Context& ui;
            Rect area;
            ColorRole fundo;
            int raio;
            uint32_t sig;
            struct Item {
                ui::TextLayout layout;
                uint16_t cor;
            };
            std::vector<Item> itens;
            int32_t corFixa = -1;    // >= 0: fundo RGB565 proprio (ex.: linha selecionada) no lugar do papel
            bool apertado = false;   // dedo em cima (estado "apertado"): fundo mais claro + contorno
            bool suave = false;      // cantos e contorno com borda suavizada (Inicio v4)
            int borda = 0;           // suave: contorno fixo (px) na cor `corBorda`
            uint16_t corBorda = 0;

            Bloco(ui::Context& u, Rect a, ColorRole f, int r, uint32_t semente = 0)
                : ui(u), area(a), fundo(f), raio(r), sig(ui::Context::combine(semente, static_cast<uint32_t>(f))) {}

            void texto(Rect r, std::string_view s, uint8_t tamanho, ColorRole papel, TextAlign alinhamento = TextAlign::Start,
                       uint8_t linhas = 1) {
                sig = ui::Context::signature(s, ui::Context::combine(sig, tamanho * 32U + papel));
                itens.push_back({ui.prepareText(r, s, tamanho, alinhamento, linhas), ui.color(papel)});
            }
            void textoCor(Rect r, std::string_view s, uint8_t tamanho, uint16_t cor, TextAlign alinhamento = TextAlign::Start) {
                sig = ui::Context::signature(s, ui::Context::combine(sig, tamanho * 65536U + cor));
                itens.push_back({ui.prepareText(r, s, tamanho, alinhamento, 1), cor});
            }
            void marca(uint32_t valor) { sig = ui::Context::combine(sig, valor); }
            // Texto de 1 linha centrado verticalmente em `yc` (tamanho = altura de fonte da familia sans).
            void linha(int x, int yc, int w, std::string_view s, uint8_t tamanho, uint16_t cor, TextAlign alinhamento = TextAlign::Start) {
                const int h = ui::sans::font(tamanho).height;
                textoCor(Rect{int16_t(x), int16_t(yc - h / 2), int16_t(w), int16_t(h)}, s, tamanho, cor, alinhamento);
            }

            void desenhar(const Desenho& extra = {}) {
                if (apertado) sig = ui::Context::combine(sig, 0xA9E5ADU);
                sig = ui::Context::combine(sig, uint32_t(corFixa + 1));
                if (suave) sig = ui::Context::combine(sig, uint32_t(borda) * 65537U + corBorda + 0x5A0000U);
                if (!ui.redraw(area, sig, true)) return;
                const uint16_t base = corFixa >= 0 ? uint16_t(corFixa) : ui.color(fundo);
                const uint16_t cor = apertado ? misturar565(base, 0xFFFF, 0.16) : base;
                ui.paint(area, [&](Arduino_GFX& g, Rect alvo) {
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, ui.color(Background));
                    if (suave && raio > 0) {
                        retanguloSuave565(g, alvo.x, alvo.y, alvo.w, alvo.h, raio, cor, ui.color(Background),
                                          apertado ? 2 : borda, apertado ? uint16_t(0xFFFF) : corBorda);
                    } else if (raio > 0) g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, raio, cor);
                    else g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, cor);
                    if (apertado && raio > 0 && !suave) {
                        g.drawRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, raio, 0xFFFF);
                        g.drawRoundRect(alvo.x + 1, alvo.y + 1, alvo.w - 2, alvo.h - 2, raio - 1, 0xFFFF);
                    }
                    if (extra) {
                        icones::Origem origem(alvo.x - area.x, alvo.y - area.y);
                        extra(g, alvo.x - area.x, alvo.y - area.y);
                    }
                    for (const auto& item : itens)
                        ui.drawText(g, item.layout, item.cor, alvo.x - area.x, alvo.y - area.y, cor);
                });
            }
        };


        // ------------------------------------------------------- cores e aneis (C3)
        struct Cor {
            int r, g, b;
        };
        constexpr Cor kLaranja{255, 120, 50}, kAmbar{255, 196, 80};     // voce
        constexpr Cor kCarmim{235, 50, 90}, kVinho{150, 20, 60};        // inimigo
        constexpr Cor kAzul{90, 200, 255}, kAzul2{60, 120, 255};        // overtime / distraido
        constexpr Cor kAgua{60, 200, 170}, kAgua2{150, 235, 215};       // pausa
        constexpr Cor kVerde{25, 158, 112};
        constexpr Cor kPreto{0, 0, 0};
        constexpr Cor kCorCat[tempo::kNumChips] = {{255, 140, 60}, {150, 120, 235}, {60, 190, 170}, {90, 200, 255}, {255, 196, 80}};
        constexpr int kCx = 80, kCy = 88;      // centro dos aneis da batalha
        constexpr int kRaioFora = 70, kRaioDentro = 50, kEspessura = 15;
        constexpr Cor kTonal{34, 36, 35}, kSelFundo{44, 28, 16}, kClaro{228, 228, 220};
        constexpr Cor kCinzaA{120, 122, 116}, kCinzaB{165, 166, 158}, kVinhoApagado{120, 56, 72}, kVinhoApagado2{150, 70, 92};

        uint16_t c565(Cor c) { return rgb565(uint8_t(c.r), uint8_t(c.g), uint8_t(c.b)); }
        Cor mistura(Cor a, Cor b, double t) {
            t = std::clamp(t, 0.0, 1.0);
            return {int(a.r + (b.r - a.r) * t), int(a.g + (b.g - a.g) * t), int(a.b + (b.b - a.b) * t)};
        }
        Cor deRole(ui::Context& ui, ColorRole papel) {
            const uint16_t v = ui.color(papel);  // RGB565 -> RGB888
            return {((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31};
        }

        // Anel estilo Apple: trilho (cor do anel 22 % sobre o fundo), arco em degrade continuo ao longo do angulo,
        // comecando no topo no sentido horario, pontas redondas; frac > 1 = 2a volta por cima com sombra sob a ponta.
        // Borda suavizada pixel a pixel (ui/AnelSuave). (ox, oy) = origem da regiao no alvo de pintura.
        ui::anel::Rgb rgbF(Cor c) { return {float(c.r), float(c.g), float(c.b)}; }

        void pintarAnel(Arduino_GFX& g, const ui::anel::Params& p) {
            const ui::anel::Anel a(p);
            if (a.raioMiolo() > 0) g.fillCircle(p.cx, p.cy, a.raioMiolo(), ui::anel::Anel::para565(ui::anel::Anel::grade(p.fundo)));
            g.startWrite();
            a.pintar([&](int x, int y, uint16_t cor) { g.writePixel(int16_t(x), int16_t(y), cor); }, true);
            g.endWrite();
        }

        void anel(Arduino_GFX& g, int ox, int oy, int cx, int cy, int raio, int larg, double frac, Cor c0, Cor c1,
                  Cor fundo, Cor baseTrilho, const Cor* volta1a0 = nullptr, const Cor* volta1a1 = nullptr) {
            ui::anel::Params p;
            p.cx = ox + cx;
            p.cy = oy + cy;
            p.raio = raio;
            p.larg = larg;
            p.frac = frac;
            p.c0 = rgbF(c0);
            p.c1 = rgbF(c1);
            p.fundo = rgbF(fundo);
            p.baseTrilho = rgbF(baseTrilho);
            ui::anel::Rgb v0, v1;
            if (volta1a0 != nullptr && volta1a1 != nullptr) {
                v0 = rgbF(*volta1a0);
                v1 = rgbF(*volta1a1);
                p.volta1a0 = &v0;
                p.volta1a1 = &v1;
            }
            pintarAnel(g, p);
        }

        ui::anel::Rgb rgbDe565(uint16_t v) {
            return {float(((v >> 11) & 31) * 255 / 31), float(((v >> 5) & 63) * 255 / 63), float((v & 31) * 255 / 31)};
        }

        // Botao colorido (cores proprias, nao as do tema).
        bool botao(ui::Context& ui, Rect r, std::string_view texto, uint16_t fundo, uint16_t tinta, uint8_t tamanho = 13,
                   bool habilitado = true, uint16_t contorno = 0, bool temContorno = false) {
            const uint32_t sig = ui::Context::signature(
                texto, ui::Context::combine(ui::Context::combine(fundo, tinta), habilitado * 4U + temContorno * 2U + contorno * 8U));
            if (ui.redraw(r, sig, true)) {
                const auto layout = ui.prepareText(r, texto, tamanho, TextAlign::Center, 1);
                ui.paint(r, [&](Arduino_GFX& g, Rect alvo) {
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, ui.color(Background));
                    g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 10, fundo);
                    if (temContorno) {
                        g.drawRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 10, contorno);
                        g.drawRoundRect(alvo.x + 1, alvo.y + 1, alvo.w - 2, alvo.h - 2, 9, contorno);
                    }
                    ui.drawText(g, layout, tinta, alvo.x - r.x, alvo.y - r.y, fundo);
                });
            }
            return ui.tap(r, habilitado);
        }

        // ---- botao v3: icone Phosphor + texto (+ subtexto), estado "apertado", contorno de selecao e contorno que enche
        // (Fim · segure). Toque FIRME: soltar dentro do alvo conta mesmo depois de 600 ms (Context::firmTap).
        using IconeId = icons::phosphor::Id;
        struct Btn {
            std::string texto;
            std::string sub;              // 2a linha (15 px) abaixo do texto
            bool comIcone = false;
            IconeId icone = IconeId::Star;
            uint16_t fundo = 0, tinta = 0;
            uint8_t tam = 17;
            bool habilitado = true;
            bool selecionado = false;     // contorno de 2 px na cor da tinta
            int progresso64 = -1;         // >= 0: contorno laranja enchendo (0..64)
            bool empilhado = false;       // icone em cima, texto embaixo (blocos pequenos)
            std::string sub2;             // empilhado: linha extra (13 px, apagada)
            uint16_t tintaSub = 0;        // 0 = mesma da tinta
            uint16_t corContorno = 0;     // 0 = mesma da tinta
            uint16_t tintaIcone = 0;      // 0 = mesma da tinta
            bool check = false;           // selecionado: check-circle laranja no canto
            bool suave = false;           // cantos e contorno com borda suavizada (Inicio v4)
            int raio = 16;                // raio do canto
        };

        // Contorno de retangulo arredondado enchendo no sentido horario a partir do meio do topo (frac 0..1).
        void contornoQueEnche(Arduino_GFX& g, int x, int y, int w, int h, int r, double frac, uint16_t cor) {
            struct Pt { double x, y; };
            std::vector<Pt> pts;
            constexpr double kRad = 3.14159265358979 / 180.0;
            pts.push_back({x + w / 2.0, double(y)});
            pts.push_back({x + w - r + 0.0, double(y)});
            const double cantos[4][3] = {{double(x + w - r), double(y + r), -90.0}, {double(x + w - r), double(y + h - r), 0.0},
                                         {double(x + r), double(y + h - r), 90.0}, {double(x + r), double(y + r), 180.0}};
            for (const auto& c : cantos)
                for (int k = 0; k <= 8; ++k) {
                    const double a = (c[2] + 90.0 * k / 8) * kRad;
                    pts.push_back({c[0] + r * std::cos(a), c[1] + r * std::sin(a)});
                }
            pts.push_back({x + w / 2.0, double(y)});
            double total = 0;
            for (size_t i = 0; i + 1 < pts.size(); ++i) total += std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
            double alvo = total * std::clamp(frac, 0.0, 1.0), acc = 0;
            for (size_t i = 0; i + 1 < pts.size() && acc < alvo; ++i) {
                const double seg = std::hypot(pts[i + 1].x - pts[i].x, pts[i + 1].y - pts[i].y);
                const int passos = std::max(1, int(std::ceil(seg / 1.5)));
                for (int k = 0; k <= passos; ++k) {
                    const double d = seg * k / passos;
                    if (acc + d > alvo) break;
                    const double t = seg > 0 ? d / seg : 0;
                    g.fillCircle(int(std::lround(pts[i].x + (pts[i + 1].x - pts[i].x) * t)),
                                 int(std::lround(pts[i].y + (pts[i + 1].y - pts[i].y) * t)), 1, cor);
                }
                acc += seg;
            }
        }

        bool botao3(ui::Context& ui, Rect r, const Btn& b) {
            const bool apertado = b.habilitado && ui.pressed(r);
            const uint32_t sig = ui::Context::signature(
                b.texto + "|" + b.sub + "|" + b.sub2,
                ui::Context::combine(ui::Context::combine(b.fundo, b.tinta),
                                     (b.habilitado ? 1U : 0U) + (apertado ? 2U : 0U) + (b.selecionado ? 4U : 0U)
                                         + (b.comIcone ? 8U : 0U) + (b.empilhado ? 16U : 0U) + (b.check ? 8388608U : 0U) + uint32_t(b.corContorno) * 3U + uint32_t(b.tintaIcone) * 5U + uint32_t(b.tam) * 32U
                                         + uint32_t(int(b.icone)) * 8192U + uint32_t(b.progresso64 + 1) * 262144U + (b.suave ? 0x40000000U : 0U)));
            if (ui.redraw(r, sig, true)) {
                const uint16_t fundo = apertado ? misturar565(b.fundo, 0xFFFF, 0.22) : b.fundo;
                const uint16_t tinta = b.tinta;
                const auto fonte = ui::sans::font(b.tam);
                const int larguraTexto = ui::sans::width(b.texto, fonte);
                const int lado = b.empilhado ? 22 : b.tam + 3;
                Rect rt{r.x, r.y, r.w, r.h};
                ui::TextLayout t1, t2, t3;
                int cx = r.x + r.w / 2, cyTexto = r.y + r.h / 2, xIcone = 0, yIcone = 0;
                if (b.empilhado) {
                    const int y0 = b.sub2.empty() ? r.y + 22 : r.y + 20;
                    yIcone = y0 - 2;
                    xIcone = cx;
                    const int yTexto = b.sub2.empty() ? r.y + r.h - 20 : r.y + 45;
                    t1 = ui.prepareText({r.x, int16_t(yTexto - 9), r.w, 18}, b.texto, b.tam, TextAlign::Center, 1);
                    if (!b.sub2.empty()) t2 = ui.prepareText({r.x, int16_t(r.y + 54), r.w, 16}, b.sub2, 13, TextAlign::Center, 1);
                } else {
                    const int folga = b.texto.empty() ? 0 : 8;
                    const int bloco = larguraTexto + (b.comIcone ? lado + folga : 0);
                    int xIni = cx - bloco / 2;
                    if (!b.sub.empty()) cyTexto = r.y + r.h / 2 - 9;
                    if (b.comIcone) { xIcone = xIni + lado / 2; yIcone = cyTexto; xIni += lado + folga; }
                    t1 = ui.prepareText({int16_t(xIni), int16_t(cyTexto - fonte.height / 2), int16_t(larguraTexto + 4), int16_t(fonte.height)},
                                        b.texto, b.tam, TextAlign::Start, 1);
                    if (!b.sub.empty())
                        t2 = ui.prepareText({r.x, int16_t(r.y + r.h - 28), r.w, 18}, b.sub, 15, TextAlign::Center, 1);
                }
                ui.paint(r, [&](Arduino_GFX& g, Rect alvo) {
                    const int dx = alvo.x - r.x, dy = alvo.y - r.y;
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, ui.color(Background));
                    const bool contorno = b.selecionado || apertado;
                    const uint16_t cc = b.selecionado ? (b.corContorno != 0 ? b.corContorno : tinta) : uint16_t(0xFFFF);   // apertado = contorno branco
                    if (b.suave) {
                        retanguloSuave565(g, alvo.x, alvo.y, alvo.w, alvo.h, b.raio, fundo, ui.color(Background), contorno ? 2 : 0, cc);
                    } else {
                        g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 16, fundo);
                        if (contorno) {
                            g.drawRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 16, cc);
                            g.drawRoundRect(alvo.x + 1, alvo.y + 1, alvo.w - 2, alvo.h - 2, 15, cc);
                        }
                    }
                    if (b.progresso64 >= 0)
                        contornoQueEnche(g, alvo.x, alvo.y, alvo.w, alvo.h, 16, b.progresso64 / 64.0, c565(kLaranja));
                    if (b.comIcone) {
                        icones::Origem o(dx, dy);
                        icones::phosphor(g, b.icone, xIcone, yIcone, lado, b.tintaIcone != 0 ? b.tintaIcone : tinta, fundo);
                    }
                    if (b.check) {
                        icones::Origem o(dx, dy);
                        icones::phosphor(g, IconeId::CheckCircle, r.x + r.w - 16, r.y + 16, 16, c565(kLaranja), fundo);
                    }
                    ui.drawText(g, t1, tinta, dx, dy, fundo);
                    ui.drawText(g, t2, b.tintaSub != 0 ? b.tintaSub : tinta, dx, dy, fundo);
                });
            }
            return ui.firmTap(r, b.habilitado);
        }

        // Quebra em ate `maxLinhas` linhas de largura `larg`; tenta o tamanho A e depois o B; no ultimo recurso corta com "...".
        struct Quebra {
            std::vector<std::string> linhas;
            uint8_t tam = 17;
            bool coube = true;
        };
        Quebra quebrar(const std::string& texto, int larg, uint8_t tamA, uint8_t tamB, int maxLinhas = 2) {
            Quebra q;
            for (uint8_t tam : {tamA, tamB}) {
                const auto& fonte = ui::sans::font(tam);
                std::vector<std::string> linhas;
                std::string atual;
                bool ok = true;
                size_t i = 0;
                while (i < texto.size()) {
                    size_t j = texto.find(' ', i);
                    if (j == std::string::npos) j = texto.size();
                    const std::string palavra = texto.substr(i, j - i);
                    i = j + 1;
                    if (palavra.empty()) continue;
                    const std::string tent = atual.empty() ? palavra : atual + " " + palavra;
                    if (ui::sans::width(tent, fonte) <= larg) { atual = tent; continue; }
                    if (atual.empty()) { ok = false; break; }   // palavra maior que a linha
                    linhas.push_back(atual);
                    atual = palavra;
                    if (ui::sans::width(atual, fonte) > larg) { ok = false; break; }
                }
                if (!atual.empty()) linhas.push_back(atual);
                if (ok && int(linhas.size()) <= maxLinhas) {
                    q.linhas = linhas;
                    q.tam = tam;
                    q.coube = true;
                    return q;
                }
            }
            // sem caber: tamanho B, ultima linha cortada com reticencias
            const auto& fonte = ui::sans::font(tamB);
            q.tam = tamB;
            q.coube = false;
            std::string resto = texto;
            for (int n = 0; n < maxLinhas && !resto.empty(); ++n) {
                std::string linha;
                size_t pos = 0;
                while (pos < resto.size()) {
                    size_t fim = resto.find(' ', pos);
                    if (fim == std::string::npos) fim = resto.size();
                    const std::string tent = resto.substr(0, fim);
                    if (ui::sans::width(tent, fonte) > larg) break;
                    linha = tent;
                    pos = fim + 1;
                }
                if (linha.empty()) linha = resto.substr(0, 1);
                const bool ultima = n + 1 == maxLinhas;
                resto = resto.size() > linha.size() ? resto.substr(std::min(resto.size(), linha.size() + 1)) : std::string{};
                if (ultima && !resto.empty()) {
                    while (!linha.empty() && ui::sans::width(linha + "...", fonte) > larg) linha.pop_back();
                    linha += "...";
                }
                q.linhas.push_back(linha);
            }
            return q;
        }

        IconeId iconeDaCategoria(tempo::Categoria c) {
            switch (c) {
            case tempo::Categoria::Trabalho: return IconeId::Briefcase;
            case tempo::Categoria::Estudo: return IconeId::GraduationCap;
            case tempo::Categoria::Leitura: return IconeId::BookOpen;
            case tempo::Categoria::Meditacao: return IconeId::FlowerLotus;
            default: return IconeId::Kanban;
            }
        }

        std::string fmtHoraRelogio(int minutoDoDia) {
            char texto[8];
            minutoDoDia = ((minutoDoDia % 1440) + 1440) % 1440;
            std::snprintf(texto, sizeof texto, "%02d:%02d", minutoDoDia / 60, minutoDoDia % 60);
            return texto;
        }

        // ------------------------------------------------------------------ regua do Inicio (v4)
        // Painel 460 x 98 (mockup tempo-regua4@2x): "‹ −5" / "+5 ›" nos cantos, valor grande + "min" (+ "× 4"), fita de
        // minutos sob a agulha fixa (marcas de 5 em 5, maiores e numeradas nas dezenas, somem nas pontas), agulha com brilho.
        // Tudo com cobertura por pixel (ui/ReguaSuave): marcas, triangulo da agulha, brilho gaussiano, cantos e contorno.
        struct VistaRegua {
            double centro = 4;       // indice de parada (fracionario) sob a agulha
            int valor = 25;          // minutos mostrados (encaixado)
            bool arrastando = false; // dedo levando a fita: contorno laranja, rotulos apagados, brilho forte
            bool ativo = true;       // false (batalha em curso): rotulos apagados
            bool x4 = false;         // "× 4" laranja ao lado do valor
            // valor == 0 = parada ∞ ("livre"): ∞ + "livre" no lugar do valor, sem rotulo a esquerda; o ∞ da fita some
            // da fita e fica FIXO num botaozinho na ponta esquerda quando sai da area visivel
        };

        void textoEm(Arduino_GFX& g, const std::string& txt, uint8_t tam, int x, int baseline, uint16_t cor, uint16_t fundo,
                     const Rect& clip, int alinhamento /*0 esq, 1 centro, 2 dir*/) {
            const auto& fonte = ui::sans::font(tam);
            const int w = ui::sans::width(txt, fonte);
            const int xi = alinhamento == 1 ? x - w / 2 : alinhamento == 2 ? x - w : x;
            ui::sans::draw(g, txt, fonte, xi, baseline, cor, fundo, clip.x, clip.y, clip.w, clip.h);
        }
        // Baseline que deixa o texto de `tam` centrado verticalmente em yc.
        int baselineCentrada(uint8_t tam, int yc) {
            const auto& f = ui::sans::font(tam);
            return yc - f.height / 2 + f.ascent;
        }

        void desenharRegua(ui::Context& ui, Rect area, const VistaRegua& v) {
            const uint16_t cPainel = ui.color(SurfaceMuted), cTexto = ui.color(Foreground), cMudo = ui.color(Muted);
            const uint16_t cLaranja = c565(kLaranja), cBg = ui.color(Background);
            const int px6 = int(std::lround(v.centro * tempo::regua::kPxPorParada));
            uint32_t sig = ui::Context::combine(0x9E6A0000U, uint32_t(px6 + 100000));
            sig = ui::Context::combine(sig, uint32_t(v.valor) * 8U + v.arrastando * 1U + v.ativo * 2U + v.x4 * 4U);
            sig = ui::Context::combine(sig, uint32_t(cPainel) * 65537U + cMudo);
            if (!ui.redraw(area, sig, true)) return;
            ui.paint(area, [&](Arduino_GFX& g, Rect alvo) {
                namespace rg = ui::regua;
                using ui::anel::Anel;
                const int ox = alvo.x, oy = alvo.y, W = area.w, H = area.h;
                constexpr float kCx = 230.0f;   // agulha (mockup: X0..X1 = 8..468 -> centro 238, em coordenadas do painel)
                const rg::Rgb painel = rg::deRgb565(cPainel), lar = rg::deRgb565(cLaranja), fg = rg::deRgb565(cTexto),
                              mudo = rg::deRgb565(cMudo);
                g.fillRect(ox, oy, W, H, cBg);
                retanguloSuave565(g, ox, oy, W, H, 16, cPainel, cBg, v.arrastando ? 2 : 0, cLaranja);
                const bool livre = tempo::regua::ehLivre(v.valor);
                const bool infFixo = tempo::regua::infFixo(v.centro);
                const rg::Rgb fundoInf = rg::mistura(painel, lar, 0.12f);
                if (infFixo) {   // botaozinho do ∞ na ponta esquerda (toque = ir para o livre): 44 x 48, y 46..94
                    const auto c565 = [](const rg::Rgb& c) { return Anel::para565(Anel::grade(c)); };
                    retanguloSuave565(g, ox + tempo::regua::kInfAreaX0, oy + 46, tempo::regua::kInfAreaX1 - tempo::regua::kInfAreaX0, 48, 10,
                                      c565(fundoInf), cPainel, 1, c565(rg::mistura(painel, lar, 0.45f)));
                }

                // ---- brilho da agulha: caixa 9 x 26 px (x = kCx, y 52..78) desfocada (sigma 4); aditivo sobre o painel
                constexpr float kSigma = 4.0f;
                constexpr int gx0 = int(kCx) - 14, gx1 = int(kCx) + 14, gy0 = 36, gy1 = 92;
                const float forca = v.arrastando ? 0.55f : 0.32f;
                float colK[gx1 - gx0 + 1], linK[gy1 - gy0 + 1];
                for (int x = gx0; x <= gx1; ++x) colK[x - gx0] = rg::gaussFaixa(kCx - 4.5f - (x + 0.5f), kCx + 4.5f - (x + 0.5f), kSigma);
                for (int y = gy0; y <= gy1; ++y) linK[y - gy0] = rg::gaussFaixa(52.0f - (y + 0.5f), 78.0f - (y + 0.5f), kSigma);
                const auto base = [&](int lx, int ly) {   // painel + brilho em (lx, ly)
                    if (lx < gx0 || lx > gx1 || ly < gy0 || ly > gy1) return painel;
                    const float k = forca * colK[lx - gx0] * linK[ly - gy0];
                    return rg::Rgb{std::min(255.0f, painel.r + lar.r * k), std::min(255.0f, painel.g + lar.g * k),
                                   std::min(255.0f, painel.b + lar.b * k)};
                };
                g.startWrite();
                const auto put = [&](int lx, int ly, const rg::Rgb& c) {
                    g.writePixel(int16_t(ox + lx), int16_t(oy + ly), Anel::para565(c, lx, ly));
                };
                for (int y = gy0; y <= gy1; ++y)
                    for (int x = gx0; x <= gx1; ++x)
                        if (colK[x - gx0] * linK[y - gy0] > 0.004f) put(x, y, base(x, y));

                // ---- marcas: so de 5 em 5; maior (e com numero) nas dezenas; opacidade cai nas pontas
                const float xa = infFixo ? 60.0f : 12.0f, xb = float(W) - 12.0f;
                struct Rotulo { int x; std::string txt; uint16_t cor; };
                std::vector<Rotulo> rotulos;
                for (int i = 0; i < tempo::regua::numParadas(); ++i) {
                    const int m = tempo::regua::minutoDaParada(i);
                    const float x = kCx + float(tempo::regua::xDoIndice(i, v.centro, 0.0));
                    if (x < xa || x > xb) continue;
                    const float k = tempo::regua::opacidadePonta(x, xa, xb);
                    if (k <= 0.01f) continue;
                    const bool maior = tempo::regua::paradaMaior(m);
                    const float larg = maior ? 2.0f : 1.5f, alfa = k * (maior ? 0.8f : 0.55f);
                    const int y0 = maior ? 58 : 63, y1 = 74;
                    const rg::Rgb& tinta = maior ? fg : mudo;
                    for (int lx = int(std::floor(x - larg / 2)); lx <= int(std::ceil(x + larg / 2)); ++lx) {
                        const float cob = rg::coberturaFaixa(x - larg / 2, x + larg / 2, lx);
                        if (cob <= 0) continue;
                        for (int ly = y0; ly < y1; ++ly) put(lx, ly, rg::mistura(base(lx, ly), tinta, alfa * cob));
                    }
                    if (maior) rotulos.push_back({int(std::lround(x)), tempo::regua::rotuloDaParada(m),
                                                  Anel::para565(Anel::grade(rg::mistura(painel, tempo::regua::paradaHoraCheia(m) ? fg : mudo, k)))});
                }

                // ---- ∞ da fita (traco suavizado por cobertura); some perto da agulha (la o ∞ grande ja esta no valor)
                if (infFixo) {
                    const rg::Infinito inf(float(tempo::regua::kInfAreaX0 + tempo::regua::kInfAreaX1) / 2.0f, 70.0f, 25.0f, 13.0f, 2.6f);
                    for (int ly = 58; ly <= 82; ++ly)
                        for (int lx = 17; lx <= 40; ++lx) {
                            const float cob = inf.cobertura(lx + 0.5f, ly + 0.5f);
                            if (cob > 0) put(lx, ly, rg::mistura(fundoInf, lar, cob));
                        }
                } else {
                    const float xi = float(tempo::regua::xDoInf(v.centro));
                    const float k = tempo::regua::opacidadePonta(xi, xa, xb) * std::clamp((std::fabs(xi - kCx) - 8.0f) / 22.0f, 0.0f, 1.0f);
                    if (k > 0.01f) {
                        const rg::Infinito inf(xi, 68.0f, 25.0f, 13.0f, 2.6f);
                        for (int ly = 58; ly <= 79; ++ly)
                            for (int lx = int(xi) - 16; lx <= int(xi) + 16; ++lx) {
                                if (lx < 0 || lx >= W) continue;
                                const float cob = inf.cobertura(lx + 0.5f, ly + 0.5f) * k;
                                if (cob > 0) put(lx, ly, rg::mistura(base(lx, ly), lar, cob));
                            }
                    }
                }
                // ---- valor livre: ∞ grande no miolo (o texto "livre" vem depois)
                const int wLivre = ui::sans::width("livre", ui::sans::font(28));
                const int xLivre0 = int(kCx) - (36 + 10 + wLivre) / 2;
                if (livre) {
                    const rg::Infinito grande(float(xLivre0) + 18.0f, 24.0f, 35.0f, 19.0f, 3.3f);
                    for (int ly = 10; ly <= 40; ++ly)
                        for (int lx = xLivre0 - 2; lx <= xLivre0 + 38; ++lx) {
                            const float cob = grande.cobertura(lx + 0.5f, ly + 0.5f);
                            if (cob > 0) put(lx, ly, rg::mistura(base(lx, ly), lar, cob));
                        }
                }

                // ---- agulha: triangulo (apice y=52) + traco de 3 px (y 50..79), cobertura por pixel sobre o brilho
                for (int ly = 43; ly < 80; ++ly)
                    for (int lx = int(kCx) - 9; lx <= int(kCx) + 9; ++lx) {
                        const float tri = ly < 53 ? rg::coberturaTriangulo(lx, ly, kCx - 7, 44, kCx + 7, 44, kCx, 52) : 0.0f;
                        const float traco = ly >= 50 ? rg::coberturaFaixa(kCx - 1.5f, kCx + 1.5f, lx) : 0.0f;
                        const float cob = std::max(tri, traco);
                        if (cob > 0) put(lx, ly, rg::mistura(base(lx, ly), lar, cob));
                    }
                g.endWrite();

                // ---- textos (fonte sans com alfa de 4 bits: borda suavizada)
                const Rect clip{alvo.x, alvo.y, int16_t(W), int16_t(H)};
                for (const auto& r : rotulos)
                    textoEm(g, r.txt, 15, ox + r.x, oy + baselineCentrada(15, 88), r.cor, cPainel, clip, 1);
                const bool apagado = v.arrastando || !v.ativo;   // arrastando: sem rotulos de toque
                const int passoE = tempo::regua::passoDoLado(v.valor, false), passoD = tempo::regua::passoDoLado(v.valor, true);
                const uint16_t cEsq = apagado || passoE == 0 ? cMudo : cLaranja;   // no limite (5 / 300) apagado e sem acao
                const uint16_t cDir = apagado || passoD == 0 ? cMudo : cLaranja;
                if (!livre)
                    textoEm(g, "‹ −" + std::to_string(passoE ? passoE : 5), 17, ox + 16, oy + baselineCentrada(17, 22), cEsq, cPainel, clip, 0);
                textoEm(g, "+" + std::to_string(passoD ? passoD : 15) + " ›", 17, ox + W - 16, oy + baselineCentrada(17, 22), cDir,
                        cPainel, clip, 2);
                // valor: algarismos em 39 px, "min"/"h" em 17 px, mesma linha de base
                if (livre) {
                    textoEm(g, "livre", 28, ox + xLivre0 + 36 + 10, oy + 34, cTexto, cPainel, clip, 0);
                    return;
                }
                const auto partes = tempo::regua::partesDoValor(v.valor);
                const auto larg = [&](const tempo::regua::ParteValor& p) { return ui::sans::width(p.texto, ui::sans::font(p.grande ? 39 : 17)); };
                int wTot = 6 * (int(partes.size()) - 1);
                for (const auto& p : partes) wTot += larg(p);
                int xv = int(kCx) - wTot / 2;
                for (const auto& p : partes) {
                    textoEm(g, p.texto, p.grande ? 39 : 17, ox + xv, oy + 36, p.grande ? cTexto : cMudo, cPainel, clip, 0);
                    xv += larg(p) + 6;
                }
                if (v.x4) textoEm(g, "× 4", 21, ox + int(kCx) + wTot / 2 + 10, oy + 36, cLaranja, cPainel, clip, 0);
            });
        }
    } // namespace

    void desenharAneisMini(ui::Context& ui, Arduino_GFX& g, int cx, int cy, int raio, double voce, double inimigo,
                           bool bonus) {
        const Cor fundo = deRole(ui, SurfaceMuted);
        const int larg = std::max(5, int(raio * 0.27));
        anel(g, 0, 0, cx, cy, raio, larg, voce, bonus ? kAzul : kLaranja, bonus ? kAzul2 : kAmbar, fundo,
             bonus ? kAzul : kLaranja);
        anel(g, 0, 0, cx, cy, int(raio * 0.62), larg, inimigo, kCarmim, kVinho, fundo, kCarmim);
    }

    void desenharAnelApple(Arduino_GFX& g, int cx, int cy, int raio, int larg, double frac, uint16_t cor0, uint16_t cor1,
                           uint16_t fundo) {
        const auto de565 = [](uint16_t v) { return Cor{((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31}; };
        anel(g, 0, 0, cx, cy, raio, larg, frac, de565(cor0), de565(cor1), de565(fundo), de565(cor0));
    }

    void desenharAnelSuave(Arduino_GFX& g, int cx, int cy, float raio, float larg, double frac, uint16_t cor0,
                           uint16_t cor1, uint16_t fundo, uint16_t trilho) {
        ui::anel::Params p;
        p.cx = cx;
        p.cy = cy;
        p.raio = raio;
        p.larg = larg;
        p.frac = frac;
        p.c0 = rgbDe565(cor0);
        p.c1 = rgbDe565(cor1);
        p.fundo = rgbDe565(fundo);
        const ui::anel::Rgb tr = rgbDe565(trilho);
        p.trilhoFixo = &tr;
        pintarAnel(g, p);
    }

    void desenharAnelXp(ui::Context& ui, Arduino_GFX& g, int cx, int cy, int raio, int larg, double frac,
                        ui::themes::ColorRole fundo) {
        anel(g, 0, 0, cx, cy, raio, larg, frac, kLaranja, kAmbar, deRole(ui, fundo), kLaranja);
    }

    // ====================================================================== launcher
    ResumoTempo TempoScreen::resumo(fs::FS* filesystem, uint32_t nowMs) {
        if (!carregado_) carregar(filesystem, true, nowMs);
        else if (nowMs - ultimoDesenhoMs_ > 1500 && !battleActive()) carregar(filesystem, false, nowMs);
        ultimoDesenhoMs_ = nowMs;
        ResumoTempo r;
        r.saldo = std::to_string(static_cast<long>(std::lround(estado_.saldo)));   // inteiro: cabe no cartao do launcher
        r.temProjetos = !titulos_.empty();
        r.planejadoMin = planejadoMin_;
        r.livre = escolha_ < 0;
        r.nivel = tempo::nivelDeXp(estado_.xpTotal);
        {
            const double base = double(tempo::xpAcumulado(r.nivel));
            r.xpFrac = std::clamp((estado_.xpTotal - base) / std::max(1.0, double(tempo::xpProximo(r.nivel))), 0.0, 1.0);
        }
        r.streak = estado_.streak;
        r.focoHojeMin = estado_.dia.fochoPagoMin;
        if (emFechamento()) {
            r.estado = ResumoTempo::Estado::Reflexao;
        } else if (battleActive()) {
            r.estado = ResumoTempo::Estado::Batalha;
            r.pausada = fase_ == Fase::Pausada;
            r.descanso = fase_ == Fase::Descanso;
            r.bonus = overtime_ && !r.descanso;
            r.projeto = nomeBatalha_;
            const int64_t plano = std::max<int64_t>(1, int64_t(planejadoMin_) * 60000);
            r.inimigoPct = r.descanso ? 0 : pontosDeVida();
            r.vocePct = r.descanso ? 100 : int(std::min<int64_t>(100, int64_t(cron_.focadoMs) * 100 / plano));
            if (livre_) {   // livre: relogio para cima, sem inimigo
                r.inimigoPct = 0;
                r.vocePct = 100;
                r.bonus = false;
                r.centro = fmtRelogio(int64_t(cron_.focadoMs / 1000));
            } else if (r.descanso) r.centro = "pausa";
            else r.centro = r.bonus ? "+" + fmtRelogio(overtimeMs() / 1000) : fmtRelogio((restanteMs() + 999) / 1000);
        } else {
            r.projeto = escolha_ >= 0 && size_t(escolha_) < titulos_.size() ? titulos_[escolha_]
                                                                           : tempo::nomeFocoLivre(categoria_);
            r.centro = tempo::regua::ehLivre(duracaoMin_) ? std::string{"livre"} : fmtRelogio(int64_t(duracaoMin_) * 60);
            r.planejadoMin = tempo::regua::ehLivre(duracaoMin_) ? duracaoTimerMin_ : duracaoMin_;
        }
        // Habitos (launcher): usa o minuto do relogio de parede (o estado so avanca na manutencao ~1x/s)
        if (const auto agoraH = tempo::agora(nowMs); agoraH.valida && !estado_.dia.ocorrencias.empty()) {
            const int minuto = agoraH.minutoDoDia;
            const auto totais = tempo::totaisDoDia(estado_);
            r.habTotal = totais.total;
            r.habFeitas = totais.feitas + totais.puladas;
            if (const auto prox = tempo::minutoProximoHabito(estado_, minuto)) r.habProximoMin = *prox;
            for (const auto& oc : estado_.dia.ocorrencias)
                if (!oc.feita && oc.status == tempo::StatusHabito::Pendente
                    && tempo::faseDaOcorrencia(oc, minuto) == tempo::FaseOcorrencia::Atrasada) r.habAtrasado = true;
            if (const auto ag = tempo::ocorrenciaAgora(estado_, minuto)) {
                const auto& oc = estado_.dia.ocorrencias[ag->indice];
                if (oc.minuto >= 0 && oc.minuto <= minuto) {   // na hora ou atrasada (as sem horario e as futuras ficam fora)
                    r.habAgora = true;
                    r.habNome = oc.nome;
                    char t[48];
                    if (minuto > oc.minuto) std::snprintf(t, sizeof t, "%02d:%02d · há %d min", oc.minuto / 60, oc.minuto % 60, minuto - oc.minuto);
                    else std::snprintf(t, sizeof t, "%02d:%02d · agora", oc.minuto / 60, oc.minuto % 60);
                    r.habLinha = t;
                }
            }
        }
        r.assinatura = ui::Context::signature(r.projeto + "|" + r.centro + "|" + r.saldo + "|" + r.habNome + "|" + r.habLinha,
                                              uint32_t(r.estado) * 1000003U + r.pausada * 7U + r.descanso * 11U + r.bonus * 13U
                                                  + uint32_t(r.vocePct) * 131U + uint32_t(r.inimigoPct) * 17U + r.temProjetos
                                                  + uint32_t(r.nivel) * 977U + uint32_t(r.xpFrac * 100) * 31U + uint32_t(r.streak) * 53U
                                                  + uint32_t(r.focoHojeMin) * 7U + uint32_t(r.habFeitas) * 41U + uint32_t(r.habTotal) * 43U
                                                  + uint32_t(r.habProximoMin + 1) * 47U + r.habAtrasado * 59U + r.habAgora * 61U);
        return r;
    }

    void TempoScreen::abrirBatalha(Screen& screen) {
        screen = emFechamento() || saqueAtivo_ ? Screen::TempoVitoria : Screen::TempoBatalha;
    }

    // Botao do cartao AGORA (sem batalha): abre o Inicio com o proximo projeto (o 1o da lista) ja escolhido.
    bool TempoScreen::comecar(Screen& screen) {
        if (battleActive() || emFechamento()) return false;
        seletor_ = false;
        if (!lista_.projetos.empty()) escolher(0);
        screen = Screen::TempoHoje;
        return true;
    }

    // BOOT curto no launcher: Foco livre imediato com a ultima categoria/duracao (ignora o ciclo pomodoro).
    void TempoScreen::focoRapido(uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        if (!carregado_) carregar(filesystem, true, nowMs);
        if (battleActive() || emFechamento()) {
            abrirBatalha(screen);
            return;
        }
        seletor_ = false;
        escolha_ = -1;
        ciclo_ = tempo::Ciclo{};
        planejadoMin_ = duracaoTimerMin_;   // BOOT no launcher SEMPRE comeca um Timer (nunca o livre), com a ultima duracao de Timer
        categoriaBatalha_ = categoria_;
        projeto_ = -1;
        nomeBatalha_ = tempo::nomeFocoLivre(categoriaBatalha_);
        salvarPreferencias();
        ligarBatalha(nowMs, false);
        screen = Screen::TempoBatalha;
    }

    void TempoScreen::escolher(int indice) {
        // Com projeto a categoria e UNICA (Projetos, vem de tempo::categoriaDoFoco); categoria_ guarda so a do Foco livre.
        escolha_ = indice >= 0 && size_t(indice) < lista_.projetos.size() ? indice : -1;
    }

    void TempoScreen::salvarPreferencias() {
        estado_.ultimaCategoria = static_cast<int>(tempo::categoriaLivreValida(static_cast<int>(categoria_)));
        estado_.ultimaDuracaoMin = duracaoMin_;   // 0 = livre (ultima escolha da regua)
        if (!tempo::regua::ehLivre(duracaoMin_)) duracaoTimerMin_ = duracaoMin_;
        estado_.ultimaDuracaoTimerMin = duracaoTimerMin_;
        estado_.ciclopomodoro = pomodoro_;
        salvar(fs_);
    }

    // ====================================================================== estado
    void TempoScreen::garantirDia(uint32_t nowMs) {
        const auto agora = tempo::agora(nowMs);
        if (!agora.valida) return;
        if (!estado_.dia.aberto || !(estado_.dia.data == agora.data)) tempo::abrirDia(estado_, agora.data);
        tempo::avancarRelogio(estado_, agora.minutoDoDia);
        if (!habitos_.habitos.empty() || !estado_.dia.ocorrencias.empty()) tempo::reconciliarDia(estado_, habitos_);
    }

    void TempoScreen::gravarEventoHabito(fs::FS* filesystem, const tempo::ResultadoOcorrencia& r, std::string_view origem) {
        if (filesystem == nullptr || !r.registrado || estado_.dia.data.ano < tempo::kAnoMinimoValido) return;
        TempoStorage::appendLog(*filesystem, estado_.dia.data, tempo::eventoHabito(estado_, r, origem));
    }

    void TempoScreen::registrarHabitoMeditacao(fs::FS* filesystem) {
        if (filesystem == nullptr) filesystem = fs_;
        if (filesystem == nullptr) return;
        std::vector<tempo::ItemFila> fila;
        const auto r = tempo::marcarMeditacaoPorTimer(estado_, fila);
        if (!r.registrado) return;
        // Etapa 2: nada vai para a fila nem para o log antes de confirmarSaque (o Desfazer da batalha tem que poder apagar tudo).
        filaMed_ = std::move(fila);
        if (estado_.dia.data.ano >= tempo::kAnoMinimoValido) eventoMedPend_ = tempo::eventoHabito(estado_, r, "timer");
    }

    void TempoScreen::atualizarHabitos(fs::FS* filesystem, uint32_t nowMs) {
        if (filesystem == nullptr || fase_ == Fase::Lutando || fase_ == Fase::Pausada || fase_ == Fase::Descanso || emFechamento()) return;
        if (!carregado_) carregar(filesystem, true, nowMs);   // ja le habitos.json
        else if (syncAtiva_) recargaPedida_ = true;           // o job esta gravando habitos.json: le depois do sinal de "terminou"
        else {
            habitos_ = tempo::ListaHabitos{};
            TempoStorage::loadHabits(*filesystem, habitos_);
        }
        garantirDia(nowMs);
        if (syncAtiva_) return;                               // idem para os check-ins do celular
        bool mudou = false;
        tempo::ArquivoCheckins remotos;
        if (TempoStorage::loadHabitCheckins(*filesystem, remotos)) {
            const auto resultados = tempo::aplicarCheckinsRemotos(estado_, remotos.checkins);
            for (const auto& r : resultados) gravarEventoHabito(filesystem, r, "ticktick");
            mudou = !resultados.empty();
        }
        if (mudou) salvar(filesystem);
    }

    bool TempoScreen::marcarHabito(uint32_t nowMs, fs::FS* filesystem, size_t indice, tempo::ResultadoOcorrencia* resultado) {
        if (filesystem == nullptr) return false;
        if (!carregado_) carregar(filesystem, true, nowMs);
        garantirDia(nowMs);
        std::vector<tempo::ItemFila> fila;
        const int nivelAntes = tempo::nivelDeXp(estado_.xpTotal);
        tempo::InstanteHabito instante = tempo::capturarHabito(estado_, indice);
        const auto r = tempo::marcarFeita(estado_, indice, fila);
        if (!r.registrado) return false;
        tempo::fecharCapturaHabito(instante, estado_);
        desfazer_ = std::move(instante);
        if (resultado != nullptr) *resultado = r;
        for (const auto& item : fila) TempoStorage::enqueueCheckin(*filesystem, item);
        if (!fila.empty()) {
            checkinNovo_ = true;   // "Feito" envia logo, mas so depois da janela do "Desfazer"
            checkinLiberaEmMs_ = nowMs + tempo::kDesfazerMs + 500;
        }
        lembretes_.resolvida(estado_, indice);
        gravarEventoHabito(filesystem, r, "");
        salvar(filesystem);
        // Um som so: dia completo (todos resolvidos) e nivel valem mais que o "feito" simples.
        const auto totais = tempo::totaisDoDia(estado_);
        const bool diaCompleto = totais.feitas > 0 && totais.pendentes == 0 && totais.esquecidas == 0;
        const bool subiu = tempo::nivelDeXp(estado_.xpTotal) > nivelAntes;
        if (const auto id = som::escolher({som::SomId::SobeTres, diaCompleto ? som::SomId::FaseCompleta : som::SomId::Bip,
                                           subiu ? som::SomId::PowerUp : som::SomId::Bip}))
            pedirSom(*id);
        return true;
    }

    bool TempoScreen::pularHabito(uint32_t nowMs, fs::FS* filesystem, size_t indice) {
        if (filesystem == nullptr) return false;
        if (!carregado_) carregar(filesystem, true, nowMs);
        garantirDia(nowMs);
        tempo::InstanteHabito instante = tempo::capturarHabito(estado_, indice);
        const auto r = tempo::pularOcorrencia(estado_, indice);
        if (!r.registrado) return false;
        tempo::fecharCapturaHabito(instante, estado_);
        desfazer_ = std::move(instante);
        lembretes_.resolvida(estado_, indice);
        gravarEventoHabito(filesystem, r, "");
        salvar(filesystem);
        return true;
    }

    bool TempoScreen::desfazerHabito(uint32_t nowMs, fs::FS* filesystem) {
        if (filesystem == nullptr || !podeDesfazerHabito()) return false;
        const tempo::InstanteHabito instante = desfazer_;
        const bool eraFeito = estado_.dia.ocorrencias[instante.indice].feita;
        const std::string habitoId = estado_.dia.ocorrencias[instante.indice].habitoId;
        if (!tempo::desfazerOcorrencia(estado_, instante)) return false;
        desfazer_ = tempo::InstanteHabito{};
        if (eraFeito) {
            bool ok = false;
            const auto destino = TempoStorage::undoCheckin(*filesystem, tempo::itemFilaDoHabito(estado_, habitoId), ok);
            if (destino == tempo::DestinoDesfazer::CorrecaoEnfileirada) {   // o check-in antigo ja tinha ido: manda o valor menor
                checkinNovo_ = true;
                checkinLiberaEmMs_ = nowMs;
            }
        }
        TempoStorage::appendLog(*filesystem, estado_.dia.data, tempo::eventoHabitoDesfeito(estado_, instante.indice));
        lembretes_.jaAvisada(estado_, instante.indice, nowMs);
        salvar(filesystem);
        return true;
    }

    void TempoScreen::adiarHabito(uint32_t nowMs, size_t indice) {
        lembretes_.adiar(estado_, indice, nowMs, uint32_t(tempo::kAdiarMedicacaoMin) * 60000U);
    }

    void TempoScreen::sincronizacaoTerminou(fs::FS* filesystem, uint32_t nowMs, uint32_t versao) {
        syncAtiva_ = false;
        if (!carregado_ || fase_ != Fase::Nenhuma || emFechamento()) return;   // em batalha/reflexao: a manutencao relê depois (versaoVista_ fica velha de proposito)
        versaoVista_ = versao;
        recargaPedida_ = false;
        recarregarProjetos(filesystem);
        atualizarHabitos(filesystem, nowMs);
    }

    void TempoScreen::manutencaoHabitos(uint32_t nowMs, fs::FS* filesystem, uint32_t versaoSync) {
        if (!carregado_ || filesystem == nullptr || fase_ != Fase::Nenhuma || emFechamento()) return;   // nada durante batalha/reflexao/saque
        const auto agora = tempo::agora(nowMs);
        if (!agora.valida) return;
        const bool viradaDoDia = !estado_.dia.aberto || !(estado_.dia.data == agora.data);
        const bool reler = tempo::deveReler({viradaDoDia, versaoSync, versaoVista_, recargaPedida_, syncAtiva_});
        if (reler) {
            versaoVista_ = versaoSync;
            recargaPedida_ = false;
            const tempo::Data antes = estado_.dia.data;
            atualizarHabitos(filesystem, nowMs);   // garantirDia (abre/fecha o dia) + reconcilia + check-ins do celular
            recarregarProjetos(filesystem);
            if (viradaDoDia && !(estado_.dia.data == antes)) salvar(filesystem);
        } else if (viradaDoDia) {
            // So acontece com a sync ativa (deveReler = false): abre/fecha o dia em RAM; os arquivos sao relidos quando ela terminar.
            const tempo::Data antes = estado_.dia.data;
            atualizarHabitos(filesystem, nowMs);
            if (!(estado_.dia.data == antes)) salvar(filesystem);
        } else {
            tempo::avancarRelogio(estado_, agora.minutoDoDia);
        }
    }

    std::optional<tempo::Aviso> TempoScreen::lembretesTick(uint32_t nowMs, bool pegouAgora) {
        if (!carregado_) return std::nullopt;
        const auto agora = tempo::agora(nowMs);
        if (!agora.valida || !(estado_.dia.data == agora.data)) return std::nullopt;
        return lembretes_.avaliar(estado_, agora.minutoDoDia, nowMs, pegouAgora);
    }

    void TempoScreen::semearInsistenciaMedicacao(uint32_t nowMs, int insistenciasFeitas) {
        if (!carregado_) return;
        const auto agora = tempo::agora(nowMs);
        if (!agora.valida || !(estado_.dia.data == agora.data)) return;
        lembretes_.semearInsistenciaMedicacao(estado_, agora.minutoDoDia, nowMs, insistenciasFeitas);
    }

    void TempoScreen::salvar(fs::FS* filesystem) {
        if (filesystem != nullptr) TempoStorage::saveState(*filesystem, estado_);
    }

    void TempoScreen::pedirSom(som::SomId id) {
        if (nSons_ < sons_.size()) sons_[nSons_++] = id;
    }

    bool TempoScreen::pegarSom(som::SomId& id) {
        if (nSons_ == 0) return false;
        id = sons_[0];
        for (size_t i = 1; i < nSons_; ++i) sons_[i - 1] = sons_[i];
        --nSons_;
        return true;
    }

    bool TempoScreen::conquistaNova(fs::FS* filesystem, const tempo::EventoLog& evento, int streakAntes) const {
        tempo::Resumo antes;
        if (filesystem != nullptr) TempoStorage::loadSummary(*filesystem, antes);   // sem resumo = vazio (1a batalha)
        tempo::Resumo depois = antes;
        tempo::registrarEvento(depois, evento, estado_.streak);
        return tempo::conquistasGanhas(tempo::avaliarConquistas(depois, estado_.streak))
             > tempo::conquistasGanhas(tempo::avaliarConquistas(antes, streakAntes));
    }

    void TempoScreen::carregar(fs::FS* filesystem, bool recarregarEstado, uint32_t nowMs) {
        fs_ = filesystem;
        if (recarregarEstado) {
            estado_ = tempo::Estado{};
            if (filesystem != nullptr) TempoStorage::loadState(*filesystem, estado_);
            if (syncAtiva_) {
                recargaPedida_ = true;   // o job grava habitos.json agora: le depois do sinal de "terminou"
            } else {
                habitos_ = tempo::ListaHabitos{};
                if (filesystem != nullptr) TempoStorage::loadHabits(*filesystem, habitos_);
            }
            garantirDia(nowMs);
            carregado_ = true;
            categoria_ = tempo::categoriaLivreValida(estado_.ultimaCategoria);   // salvo "Projetos" (versoes antigas) vira Trabalho
            duracaoMin_ = tempo::regua::limitarEncaixado(estado_.ultimaDuracaoMin);   // 5..300, uma parada da regua; 0 = livre
            duracaoTimerMin_ = tempo::regua::duracaoDoTimer(duracaoMin_, estado_.ultimaDuracaoTimerMin);
            pomodoro_ = estado_.ciclopomodoro && tempo::ciclo4xPermitido(duracaoMin_);
            salvar(filesystem);
        }
        if (syncAtiva_) {
            recargaPedida_ = true;       // projetos.json esta sendo gravado: mantem a lista atual ate a sync terminar
        } else {
            lista_ = tempo::ListaProjetos{};
            titulos_.clear();
            if (filesystem != nullptr) TempoStorage::loadProjects(*filesystem, lista_);
            for (const auto& projeto : lista_.projetos) titulos_.push_back(tempo::limparTitulo(projeto.titulo));
        }
        pagina_ = 0;
        if (escolha_ >= int(titulos_.size())) escolha_ = -1;
    }

    void TempoScreen::recarregarProjetos(fs::FS* filesystem) {
        if (!carregado_ || fase_ != Fase::Nenhuma || filesystem == nullptr) return;
        carregar(filesystem, false, 0);
    }

    void TempoScreen::acumular(uint32_t nowMs) {
        const uint64_t agora = tempo::ms(nowMs);
        const uint64_t delta = agora > ultimoMs_ ? agora - ultimoMs_ : 0;
        ultimoMs_ = agora;
        cron_.avancar(delta, fase_ == Fase::Lutando);   // pausado/descanso: nada muda (o inimigo nao se cura)
        // Aviso de 90 min sem toque: so conta o tempo RODANDO (pausado e distraido tem regras proprias)
        const auto ev = ocio_.avancar(delta, fase_ == Fase::Lutando && !cron_.distraido);
        if (ev == tempo::livre::AvisoOcioso::Evento::Avisar) {
            focadoNoAvisoMs_ = cron_.focadoMs;
            evOcio_ = 1;
        } else if (ev == tempo::livre::AvisoOcioso::Evento::Concluir) {
            evOcio_ = 2;
        }
    }

    void TempoScreen::atividade(uint32_t nowMs, bool toque, int x, int y) {
        const bool naTela = avisoDesenhadoMs_ != 0 && nowMs - avisoDesenhadoMs_ < 600;
        if (!ocio_.ativo()) { ocio_.tocou(); return; }
        if (!naTela) { ocio_.continuar(); return; }   // aviso aberto fora da tela do Tempo: qualquer toque/botao continua
        if (toque) { toqueNovo_ = true; toqueX_ = x; toqueY_ = y; }   // na tela: o desenho decide (o dedo pode estar no Concluir)
    }

    void TempoScreen::continuarOcio(uint32_t nowMs) {
        ocio_.continuar();
        toqueNovo_ = false;
        avisoDesenhadoMs_ = 0;
        ultimoMs_ = tempo::ms(nowMs);
    }

    // 5 min sem resposta ao "ainda focando?": conclui e paga SO ate a hora do aviso (o foco depois do aviso nao conta).
    void TempoScreen::concluirPorOcio(uint32_t nowMs, fs::FS* filesystem) {
        cron_.focadoMs = tempo::livre::pagoAteOAviso(cron_.focadoMs, focadoNoAvisoMs_);
        ocio_.zerar();
        ultimoMs_ = tempo::ms(nowMs);   // finalizar/finalizarLivre nao somam mais tempo
        avisoDesenhadoMs_ = 0;
        if (livre_) {
            finalizarLivre(nowMs, filesystem, true);
        } else {
            ciclo_ = tempo::Ciclo{};
            finalizar(nowMs, !overtime_, filesystem);
        }
    }

    int64_t TempoScreen::restanteMs() const { return cron_.restanteMs(); }
    int64_t TempoScreen::overtimeMs() const { return cron_.overtimeMs(); }
    int TempoScreen::pontosDeVida() const { return cron_.pontosDeVida(); }

    // Estimativa do motor: foco = "ao terminar" (zera o inimigo no plano + cura); overtime = "ate agora".
    tempo::LiquidacaoBatalha TempoScreen::estimar() const {
        return tempo::liquidarBatalha(estado_, cron_.entrada(categoriaBatalha_, overtime_));
    }

    // Comeca a batalha (ou o ciclo pomodoro) com a escolha do Inicio.
    void TempoScreen::comecarSessao(uint32_t nowMs) {
        if (battleActive() || emFechamento()) return;
        const bool comProjeto = escolha_ >= 0 && size_t(escolha_) < titulos_.size();
        categoriaBatalha_ = tempo::categoriaDoFoco(comProjeto, categoria_);   // categoria unica do projeto
        if (comProjeto) {
            projeto_ = escolha_;
            nomeBatalha_ = titulos_[escolha_].empty() ? std::string{"Projeto"} : titulos_[escolha_];
            tempo::registrarProjetoRecente(estado_.projetosRecentes, lista_.projetos[escolha_].id);
        } else {
            projeto_ = -1;
            nomeBatalha_ = tempo::nomeFocoLivre(categoriaBatalha_);
        }
        const bool livre = tempo::regua::ehLivre(duracaoMin_);
        ciclo_ = !livre && pomodoro_ && tempo::ciclo4xPermitido(duracaoMin_) ? tempo::iniciarCiclo(duracaoMin_) : tempo::Ciclo{};   // 4x repete a rodada escolhida na regua
        emCiclo_ = ciclo_.ativo;
        cicloRodadas_ = 0;
        cicloFocadoMin_ = 0;
        planejadoMin_ = livre ? 0 : duracaoMin_;
        naoContaAteMs_ = 0;
        salvarPreferencias();
        ligarBatalha(nowMs, livre);
    }

    void TempoScreen::ligarBatalha(uint32_t nowMs, bool livre) {
        livre_ = livre;
        saqueLivre_ = false;
        {
            const auto ag = tempo::agora(nowMs);
            inicioMinutoDia_ = ag.valida ? ag.minutoDoDia : -1;
        }
        ocio_.zerar();
        evOcio_ = 0;
        toqueNovo_ = false;
        avisoDesenhadoMs_ = 0;
        cron_.zerar(livre ? 0 : planejadoMin_);
        overtime_ = false;
        arrastando_ = false;
        fimApertado_ = false;
        fimDicaAteMs_ = 0;
        ultimoMs_ = tempo::ms(nowMs);
        saqueAtivo_ = false;
        fase_ = Fase::Lutando;
        pedirSom(som::SomId::SobeQuatro);   // comecar batalha/foco (inclui a proxima rodada do ciclo)
    }

    // Pausar (decisao 19): interrupcao legitima. O relogio para e o inimigo NAO se cura (acumular so conta em Lutando).
    void TempoScreen::pausarOuRetomar(uint32_t nowMs) {
        acumular(nowMs);
        if (fase_ == Fase::Lutando) {
            fase_ = Fase::Pausada;
            pausaInicioMs_ = tempo::ms(nowMs);
            pedirSom(som::SomId::DesceAlerta);
        } else if (fase_ == Fase::Pausada) { fase_ = Fase::Lutando; ultimoMs_ = tempo::ms(nowMs); }
    }

    // Distrai (desvio): o relogio segue como distracao e o inimigo se cura enquanto durar.
    void TempoScreen::alternarDistracao(uint32_t nowMs) {
        if (fase_ != Fase::Lutando || overtime_) return;
        acumular(nowMs);
        if (!cron_.distraido) {
            cron_.distrair();
            pedirSom(som::SomId::DesceAlerta);
        } else {
            cron_.voltar();
        }
    }

    void TempoScreen::iniciarDescanso(uint32_t nowMs, bool voltaParaBatalha) {
        if (voltaParaBatalha) acumular(nowMs);   // congela o relogio da batalha
        descansoVolta_ = voltaParaBatalha;
        if (!ciclo_.ativo) descansoMs_ = int64_t(tempo::kCicloPausaMin) * 60000;
        descansoInicioMs_ = tempo::ms(nowMs);
        fase_ = Fase::Descanso;
    }

    void TempoScreen::encerrarDescanso(uint32_t nowMs) {
        if (fase_ != Fase::Descanso) return;
        if (ciclo_.ativo) {   // ciclo pomodoro: a pausa emenda na proxima rodada (ou acaba apos a pausa longa)
            ciclo_ = tempo::avancarCiclo(ciclo_);
            descansoVolta_ = false;
            if (ciclo_.ativo) {
                planejadoMin_ = tempo::duracaoCicloMin(ciclo_);
                ligarBatalha(nowMs);
            } else {
                fase_ = Fase::Nenhuma;
            }
            return;
        }
        fase_ = descansoVolta_ ? Fase::Pausada : Fase::Nenhuma;
        descansoVolta_ = false;
    }

    // Fim da batalha: guarda a entrada, liquida (a economia ja vale e o estado ja e salvo, como antes) e abre a Reflexao. Nada vai
    // para o log do dia / resumo.json ate confirmarSaque (para o Desfazer poder apagar tudo sem rastro).
    void TempoScreen::finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem, bool tocarDesfecho) {
        acumular(nowMs);
        cron_.voltar();   // fecha uma distracao em aberto
        garantirDia(nowMs);
        if (!confirmado_) confirmarSaque(filesystem);   // defesa: nunca perde um fechamento anterior
        entradaPend_ = cron_.entrada(categoriaBatalha_, true);
        entradaPend_.abandonou = abandonou;
        distracoesPend_ = cron_.numDistracoes;
        if (emCiclo_) {
            ++cicloRodadas_;
            cicloFocadoMin_ += entradaPend_.minutosFocados;
        }
        liquidar(nowMs, filesystem, tocarDesfecho);
        humor_ = distraiu_ = -1;
        distracaoAberta_ = false;
        saqueAtivo_ = false;
        confirmado_ = false;
        reflexaoInicioMs_ = nowMs;
        overtime_ = false;
        fase_ = Fase::Reflexao;
    }

    // Concluir da sessao livre (decisao 23). Menos de 1 min: nada registrado (sem saque, sem log; aviso curto no Inicio). Senao
    // liquida (taxa x minuto focado, sem bonus perfeito, nao e vitoria) e abre o saque direto, sem Reflexao de humor.
    void TempoScreen::finalizarLivre(uint32_t nowMs, fs::FS* filesystem, bool tocarDesfecho) {
        acumular(nowMs);
        cron_.voltar();   // fecha uma distracao em aberto (o tempo distraido nao e pago)
        ocio_.zerar();
        evOcio_ = 0;
        if (!tempo::livre::conta(cron_.focadoMs)) {
            naoContaTxt_ = tempo::livre::textoNaoConta(cron_.focadoMs);
            naoContaAteMs_ = nowMs + 6000;
            if (naoContaAteMs_ == 0) naoContaAteMs_ = 1;
            livre_ = false;
            fase_ = Fase::Nenhuma;
            return;
        }
        garantirDia(nowMs);
        if (!confirmado_) confirmarSaque(filesystem);
        cronLivreSalvo_ = cron_;
        entradaPend_ = tempo::livre::entrada(cron_, categoriaBatalha_);
        distracoesPend_ = cron_.numDistracoes;
        ciclo_ = tempo::Ciclo{};
        emCiclo_ = false;
        liquidar(nowMs, filesystem, tocarDesfecho);
        humor_ = distraiu_ = -1;
        distracaoAberta_ = false;
        saqueAtivo_ = false;
        confirmado_ = false;
        reflexaoInicioMs_ = nowMs;
        overtime_ = false;
        livre_ = false;
        saqueLivre_ = true;
        fase_ = Fase::Reflexao;
        fecharReflexao(nowMs, filesystem, true, false);   // livre nao tem Reflexao: vai direto ao saque
    }

    void TempoScreen::liquidar(uint32_t nowMs, fs::FS* filesystem, bool tocarDesfecho) {
        const int nivelAntes = tempo::nivelDeXp(estado_.xpTotal);
        const int streakAntes = estado_.streak;
        const bool abandonou = entradaPend_.abandonou;
        instante_ = tempo::capturarBatalha(estado_);
        filaMed_.clear();
        eventoMedPend_.reset();
        const auto liquidacao = tempo::aplicarBatalha(estado_, entradaPend_);
        // Meditacao vencida marca a proxima ocorrencia pendente do habito de Meditacao (no horario); o check-in so entra na fila ao confirmar.
        if (liquidacao.vitoria && categoriaBatalha_ == tempo::Categoria::Meditacao) registrarHabitoMeditacao(filesystem);
        tempo::selarBatalha(instante_, estado_);
        liquidado_ = true;

        const std::string titulo = nomeBatalha_;
        resultado_ = {liquidacao, titulo, planejadoMin_, entradaPend_.minutosFocados,
                      tempo::nivelAtributo(estado_, liquidacao.atributo), estado_.streak, abandonou,
                      tempo::nivelDeXp(estado_.xpTotal) > nivelAntes, false};

        const auto agora = tempo::agora(nowMs);
        auto& evento = eventoPendente_;
        evento = tempo::EventoLog{};
        evento.data = agora.valida ? tempo::textoData(agora.data) : std::string{};
        evento.hora = horaTexto(agora);
        evento.tipo = "batalha";
        evento.projeto = projeto_ < 0 ? std::string{"Foco livre"} : titulo;
        evento.categoria = tempo::nomeCategoria(categoriaBatalha_);
        evento.projetoId = projeto_ >= 0 && size_t(projeto_) < lista_.projetos.size() ? lista_.projetos[projeto_].id : std::string{};
        evento.resultado = entradaPend_.livre ? "livre" : liquidacao.vitoria ? "vitoria" : "fuga";   // livre nao conta como vitoria
        evento.planejadoMin = entradaPend_.livre ? 0 : planejadoMin_;
        evento.focadoMin = entradaPend_.minutosFocados;
        evento.distraidoMin = entradaPend_.minutosDistraidos;
        evento.distracoes = distracoesPend_;
        evento.overtimeMin = liquidacao.minutosOvertime;
        evento.cr = liquidacao.cr;
        evento.xp = liquidacao.xp;
        evento.perfeita = liquidacao.perfeita;
        dataPendente_ = agora.valida ? agora.data : tempo::Data{};
        resultado_.conquista = conquistaNova(filesystem != nullptr ? filesystem : fs_, evento, streakAntes);
        if (tocarDesfecho) {   // no ciclo, concluirRodada escolhe o som (descanso); aqui so batalha unica / "Fim" / "Concluir". O Desfazer nao repete som.
            if (const auto id = som::escolher({liquidacao.vitoria ? som::SomId::Moeda : som::SomId::DesceAlerta,
                                               resultado_.subiuNivel ? som::SomId::PowerUp : som::SomId::Bip,
                                               resultado_.conquista ? som::SomId::Jingle : som::SomId::Bip}))
                pedirSom(*id);
        }
        if (filesystem == nullptr) filesystem = fs_;
        if (filesystem != nullptr) TempoStorage::saveState(*filesystem, estado_);
    }

    // Ciclo pomodoro: a rodada terminou (inimigo derrotado). Liquida a batalha sozinha. Rodadas intermediarias vao direto ao saque
    // curto (+ Desfazer) e depois a pausa, sem Reflexao; a ultima rodada abre a Reflexao do ciclo inteiro (decisao 22).
    void TempoScreen::concluirRodada(uint32_t nowMs, fs::FS* filesystem) {
        const bool pedeReflexao = tempo::rodadaPedeReflexao(ciclo_);
        finalizar(nowMs, false, filesystem, false);
        // Fim do foco -> descanso: um som so (a moeda/vitoria ficam na tela); nivel e conquista valem mais.
        if (const auto id = som::escolher({som::SomId::SuaveDescendo,
                                           resultado_.subiuNivel ? som::SomId::PowerUp : som::SomId::Bip,
                                           resultado_.conquista ? som::SomId::Jingle : som::SomId::Bip}))
            pedirSom(*id);
        if (!pedeReflexao) fecharReflexao(nowMs, filesystem, true, false);   // silencioso: saque curto + pausa
    }

    // Sai da Reflexao. mostrarSaque: abre o saque (humor_ ja decidido; -1 = sem humor); senao confirma direto (voltar/BOOT sem saque).
    void TempoScreen::fecharReflexao(uint32_t nowMs, fs::FS* filesystem, bool mostrarSaque, bool deReflexao) {
        if (fase_ != Fase::Reflexao) return;
        if (!liquidado_) liquidar(nowMs, filesystem, false);   // voltou da Desfeita: paga de novo (sem som)
        distracaoAberta_ = false;
        proximo_ = tempo::proximoPassoDoSaque(ciclo_);
        if (!mostrarSaque) {
            confirmarSaque(filesystem);
            ciclo_ = tempo::Ciclo{};
            emCiclo_ = false;
            fase_ = Fase::Nenhuma;
            return;
        }
        saqueAtivo_ = true;
        saqueDeReflexao_ = deReflexao;
        saqueInicioMs_ = nowMs;
        fase_ = Fase::Nenhuma;   // o saque e uma sobreposicao; a pausa so comeca ao sair dele (relogio ancorado em saqueInicioMs_)
    }

    // Ponto sem volta: grava o evento da batalha (com humor/distracao), o resumo.json do Perfil e o que a Meditacao enfileirou.
    void TempoScreen::confirmarSaque(fs::FS* filesystem) {
        if (confirmado_) return;
        confirmado_ = true;
        eventoPendente_.humor = tempo::humorParaLog(humor_);
        eventoPendente_.distraiu = tempo::distraiuParaLog(distraiu_);
        if (filesystem == nullptr) filesystem = fs_;
        if (filesystem != nullptr && liquidado_) {
            TempoStorage::appendLog(*filesystem, dataPendente_, eventoPendente_);
            TempoStorage::recordBattle(*filesystem, eventoPendente_, estado_.streak);   // resumo.json do Perfil
            for (const auto& item : filaMed_) TempoStorage::enqueueCheckin(*filesystem, item);
            if (eventoMedPend_) TempoStorage::appendLog(*filesystem, estado_.dia.data, *eventoMedPend_);
        }
        if (!filaMed_.empty()) checkinNovo_ = true;   // lume.12: o App sincroniza logo ("Feito" envia na hora)
        filaMed_.clear();
        eventoMedPend_.reset();
        instante_ = tempo::InstanteBatalha{};
    }

    // Depois do saque: no ciclo, a pausa (curta/longa) com o relogio contado desde o fim da rodada; senao, nada (Inicio/outra rodada).
    void TempoScreen::iniciarProximoPasso(uint32_t nowMs) {
        if (proximo_ != tempo::ProximoPasso::VoltarInicio && ciclo_.ativo) {
            ciclo_ = tempo::avancarCiclo(ciclo_);   // foco -> pausa
            descansoMs_ = int64_t(tempo::duracaoCicloMin(ciclo_)) * 60000;
            iniciarDescanso(nowMs, false);
            descansoInicioMs_ = std::min<uint64_t>(descansoInicioMs_, tempo::ms(saqueInicioMs_));   // a pausa conta desde o fim da rodada
            return;
        }
        ciclo_ = tempo::Ciclo{};
        emCiclo_ = false;
        fase_ = Fase::Nenhuma;
    }

    void TempoScreen::sairDoSaque(uint32_t nowMs, fs::FS* filesystem) {
        if (!saqueAtivo_) return;
        confirmarSaque(filesystem);
        saqueAtivo_ = false;
        iniciarProximoPasso(nowMs);
    }

    // BOOT / botao primario do saque: confirma e segue (pausa do ciclo -> Batalha; senao Inicio).
    void TempoScreen::acaoPrimariaSaque(uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        const bool pausa = proximo_ != tempo::ProximoPasso::VoltarInicio;
        sairDoSaque(nowMs, filesystem);
        screen = pausa && battleActive() ? Screen::TempoBatalha : Screen::TempoHoje;
    }

    bool TempoScreen::podeDesfazerBatalha(uint32_t nowMs) const {
        return saqueAtivo_ && !confirmado_ && liquidado_ && tempo::desfazerDisponivel(nowMs, saqueInicioMs_)
               && tempo::podeDesfazerBatalha(estado_, instante_);
    }

    // "Desfazer": reverte a liquidacao inteira (carteira, cofre, XP, atributos, totais do dia, streak, eventos do dia, Meditacao pelo
    // timer) e descarta o que ainda nao foi gravado (log, resumo.json, check-in). Volta a Reflexao (batalha unica / fim do ciclo) ou
    // ao Inicio (rodada intermediaria: o ciclo se encerra).
    bool TempoScreen::desfazerBatalha(uint32_t nowMs, fs::FS* filesystem, Screen& screen) {
        if (!podeDesfazerBatalha(nowMs)) return false;
        if (!tempo::desfazerBatalha(estado_, instante_)) return false;
        instante_ = tempo::InstanteBatalha{};
        filaMed_.clear();
        eventoMedPend_.reset();
        liquidado_ = false;
        if (filesystem == nullptr) filesystem = fs_;
        if (filesystem != nullptr) TempoStorage::saveState(*filesystem, estado_);
        saqueAtivo_ = false;
        if (saqueLivre_) {   // livre: volta para a sessao contando de onde parou (nao vai ao Inicio)
            saqueLivre_ = false;
            confirmado_ = true;
            livre_ = true;
            cron_ = cronLivreSalvo_;
            ocio_.zerar();
            ultimoMs_ = tempo::ms(nowMs);
            fase_ = Fase::Lutando;
            screen = Screen::TempoBatalha;
            return true;
        }
        if (saqueDeReflexao_) {
            humor_ = distraiu_ = -1;
            reflexaoInicioMs_ = nowMs;
            confirmado_ = false;
            fase_ = Fase::Reflexao;
            screen = Screen::TempoVitoria;
        } else {
            confirmado_ = true;
            ciclo_ = tempo::Ciclo{};
            emCiclo_ = false;
            fase_ = Fase::Nenhuma;
            screen = Screen::TempoHoje;
        }
        return true;
    }

    bool TempoScreen::registrarFocoLeitor(uint32_t nowMs, fs::FS* filesystem, const tempo::RodadaLeitor& rodada,
                                          std::string_view tituloLivro) {
        if (filesystem == nullptr) return false;   // sem SD: nao grava (e nao quebra)
        if (!carregado_) carregar(filesystem, true, nowMs);
        garantirDia(nowMs);
        const auto agora = tempo::agora(nowMs);
        const std::string data = agora.valida ? tempo::textoData(agora.data) : std::string{};
        const bool batalhaAtiva = battleActive() || emFechamento();
        const int nivelAntes = tempo::nivelDeXp(estado_.xpTotal);
        const int streakAntes = estado_.streak;
        const auto evento = tempo::liquidarFocoLeitor(estado_, rodada, batalhaAtiva, tituloLivro, data, horaTexto(agora));
        if (!evento) return false;
        // Mesmo quadro do "fim do foco" (SuaveDescendo, no App): nivel/conquista valem mais, senao a moeda.
        if (const auto id = som::escolher({som::SomId::Moeda,
                                           tempo::nivelDeXp(estado_.xpTotal) > nivelAntes ? som::SomId::PowerUp : som::SomId::Bip,
                                           conquistaNova(filesystem, *evento, streakAntes) ? som::SomId::Jingle : som::SomId::Bip}))
            pedirSom(*id);
        TempoStorage::saveState(*filesystem, estado_);
        if (agora.valida) {
            TempoStorage::appendLog(*filesystem, agora.data, *evento);
            TempoStorage::recordBattle(*filesystem, *evento, estado_.streak);   // resumo.json do Perfil
        }
        return true;
    }

    bool TempoScreen::tick(uint32_t nowMs, fs::FS* filesystem) {
        if (saqueAtivo_) {   // janela do Desfazer fechou: confirma; com pausa do ciclo a vez dela comeca sozinha
            if (!confirmado_ && !tempo::desfazerDisponivel(nowMs, saqueInicioMs_)) confirmarSaque(filesystem);
            if (confirmado_ && proximo_ != tempo::ProximoPasso::VoltarInicio) {
                saqueAtivo_ = false;
                iniciarProximoPasso(nowMs);
                return true;
            }
            return false;
        }
        if (fase_ == Fase::Descanso) {
            if (int64_t(tempo::ms(nowMs)) - int64_t(descansoInicioMs_) >= descansoMs_) {
                encerrarDescanso(nowMs);
                if (fase_ != Fase::Lutando) pedirSom(som::SomId::Bip);   // fim da pausa sem nova rodada (com rodada: SobeQuatro)
                return true;
            }
            return false;
        }
        if (fase_ == Fase::Reflexao) {
            if (nowMs - reflexaoInicioMs_ >= uint32_t(tempo::kReflexaoAutoSalvarSeg) * 1000U)
                fecharReflexao(nowMs, filesystem, true, true);   // 60 s sem resposta: salva SEM humor
            return false;
        }
        if (fase_ != Fase::Lutando && fase_ != Fase::Pausada) return false;
        acumular(nowMs);
        if (fase_ == Fase::Pausada && livre_ && tempo::livre::pausaEstourou(tempo::ms(nowMs) - pausaInicioMs_)) {
            finalizarLivre(nowMs, filesystem, true);   // pausado por mais de 30 min: conclui sozinho (paga ate a pausa)
            return true;
        }
        if (fase_ != Fase::Lutando) return false;
        if (evOcio_ != 0) {   // aviso de 90 min sem toque (qualquer sessao rodando)
            const int ev = evOcio_;
            evOcio_ = 0;
            if (ev == 1) {
                const auto ag = tempo::agora(nowMs);
                avisoMinutoDia_ = ag.valida ? ag.minutoDoDia : -1;
                pedirSom(som::SomId::AlertaLongo);
                return true;
            }
            concluirPorOcio(nowMs, filesystem);
            return true;
        }
        if (livre_) return false;   // livre: sem inimigo, sem hora extra, sem bipes de presenca
        const int64_t plano = int64_t(planejadoMin_) * 60000;
        if (!overtime_ && restanteMs() == 0 && ciclo_.ativo) {   // ciclo: inimigo derrotado = fim da rodada
            concluirRodada(nowMs, filesystem);
            return true;
        }
        if (!overtime_ && restanteMs() == 0) {   // inimigo derrotado: nao encerra, segue em overtime
            overtime_ = true;
            pedirSom(som::SomId::Vitoria);   // inimigo derrotado (os creditos/moeda vem ao concluir)
            return true;
        }
        // Hora extra SEM teto (economia, decisao 24): depois que o inimigo cai o Timer segue contando e pagando ate o Concluir.
        // A protecao contra "deixei rodando" e o aviso de 90 min sem toque (acima).
        bool bipe = false;
        if (!overtime_ && !cron_.distraido) {
            const int minutosFoco = static_cast<int>(cron_.focadoMs / 60000);
            if (minutosFoco >= ultimoBipeFocoMin_ + 10) {   // "presenca" a cada 10 min de foco
                ultimoBipeFocoMin_ = minutosFoco - minutosFoco % 10;
                bipe = true;
                pedirSom(som::SomId::Bip);
            }
            if (!avisoFimDado_ && plano >= 5 * 60000 && restanteMs() <= 2 * 60000) {   // aviso 2 min antes do fim
                avisoFimDado_ = true;
                bipe = true;
                pedirSom(som::SomId::Bip);
            }
        }
        return bipe;
    }

    // ===================================================================== entradas
    void TempoScreen::boot(uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        if (ocio_.ativo() && fase_ == Fase::Lutando) { continuarOcio(nowMs); return; }   // "ainda focando?": botao = Continuar
        if (screen == Screen::TempoVitoria) {
            if (saqueAtivo_) {   // BOOT = acao primaria do saque (proximo passo)
                acaoPrimariaSaque(nowMs, screen, filesystem);
            } else if (fase_ == Fase::Reflexao) {
                if (distracaoAberta_) distracaoAberta_ = false;
                else fecharReflexao(nowMs, filesystem, true, true);   // sem resposta: salva sem humor e mostra o saque
            } else {
                screen = Screen::TempoHoje;
            }
        } else if (screen == Screen::TempoBatalha) {
            if (fase_ == Fase::Lutando || fase_ == Fase::Pausada) pausarOuRetomar(nowMs);
            else if (fase_ == Fase::Descanso) encerrarDescanso(nowMs);
            else if (emFechamento() || saqueAtivo_) screen = Screen::TempoVitoria;
            else screen = Screen::TempoHoje;
        } else if (seletor_) {
            // seletor aberto: BOOT nao faz nada (escolha por toque)
        } else if (battleActive()) {
            screen = Screen::TempoBatalha;
        } else if (emFechamento() || saqueAtivo_) {
            screen = Screen::TempoVitoria;
        } else if (!carregado_) {
            carregar(filesystem, true, nowMs);
        } else {
            comecarSessao(nowMs);   // BOOT no Inicio = Comecar
            screen = Screen::TempoBatalha;
        }
    }

    void TempoScreen::back(uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        if (ocio_.ativo() && fase_ == Fase::Lutando) { continuarOcio(nowMs); return; }
        if (screen == Screen::TempoHoje) {
            if (seletor_) seletor_ = false;
            else {
                if (carregado_) salvarPreferencias();
                screen = Screen::Home;
            }
        } else {
            if (screen == Screen::TempoVitoria && distracaoAberta_) { distracaoAberta_ = false; return; }   // volta aos rostos
            if (saqueAtivo_) sairDoSaque(nowMs, filesystem);                                        // confirma; a pausa do ciclo segue
            else if (fase_ == Fase::Reflexao) fecharReflexao(nowMs, filesystem, false, true);       // sem resposta: salva sem humor
            screen = Screen::TempoHoje;  // batalha em curso continua em segundo plano
        }
    }

    // ======================================================================= telas
    Action TempoScreen::draw(ui::Context& ui, uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        const bool reentrada = nowMs - ultimoDesenhoMs_ > 1500;
        ultimoDesenhoMs_ = nowMs;
        if (!carregado_) carregar(filesystem, true, nowMs);
        else if (reentrada && screen == Screen::TempoHoje) carregar(filesystem, false, nowMs);
        tick(nowMs, filesystem);
        if (saqueAtivo_ && confirmado_ && proximo_ == tempo::ProximoPasso::VoltarInicio && screen != Screen::TempoVitoria)
            saqueAtivo_ = false;   // saque ja confirmado e fora da tela: nao reabre sozinho
        if (screen != Screen::TempoHoje) seletor_ = false;
        if (screen == Screen::TempoHoje) return seletor_ ? drawSeletor(ui, nowMs, screen) : drawInicio(ui, nowMs, screen);
        if (screen == Screen::TempoBatalha) return drawBatalha(ui, nowMs, screen);
        return drawVitoria(ui, nowMs, screen);
    }

    // ====================================================================== Inicio v4
    // Layout 640x172 (mockup tempo-regua4@2x): linha de cima = "‹ Inicio" (72 x 56) + cartao do foco (nome em 1 linha,
    // 2a linha = icone + categoria/"sem projeto" + cr/min, "Trocar ›" abre o seletor); embaixo a REGUA de 5 a 300 min (2 marchas)
    // (arrastar leva a fita e encaixa de 5 em 5; toque sem arrasto: metade esquerda -5, direita +5); a direita,
    // Comecar (sub "25 min · ate HH:MM" / "4 × 60 · ate HH:MM") e "4× rodadas" (repete a rodada escolhida 4 vezes).
    Action TempoScreen::drawInicio(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ == Fase::Reflexao || (saqueAtivo_ && !confirmado_)) { screen = Screen::TempoVitoria; return Action::None; }
        garantirDia(nowMs);
        const bool emBatalha = battleActive();
        const bool comProjeto = escolha_ >= 0 && size_t(escolha_) < titulos_.size();
        const tempo::Categoria cat = tempo::categoriaDoFoco(comProjeto, categoria_);
        const bool podeEditar = !emBatalha;
        const Rect areaRegua{8, 70, 460, 98};

        // ---- regua: o dedo leva a fita (so a ultima amostra por quadro, ver App::update); soltar encaixa de 5 em 5
        bool foiArrasto = false;
        if (podeEditar) {
            if (const auto* t = ui.touch()) {
                const bool dentro = t->x >= areaRegua.x && t->x < areaRegua.x + areaRegua.w && t->y >= areaRegua.y
                                    && t->y < areaRegua.y + areaRegua.h;
                if (hasTouch(*t, ui::TouchStart)) {
                    if (dentro) reguaArrasto_.comecar(int(t->x), duracaoMin_);
                    else reguaArrasto_.cancelar();
                } else if (reguaArrasto_.ativo && hasTouch(*t, ui::TouchMove)) {
                    reguaArrasto_.mover(int(t->x));
                }
                if (reguaArrasto_.ativo && hasTouch(*t, ui::TouchRelease)) {
                    if (hasTouch(*t, ui::TouchCancel)) {
                        reguaArrasto_.cancelar();
                    } else {
                        reguaArrasto_.mover(int(t->x));
                        foiArrasto = reguaArrasto_.moveu;
                        const int v = reguaArrasto_.soltar();
                        if (foiArrasto) duracaoMin_ = v;
                    }
                }
            }
        } else {
            reguaArrasto_.cancelar();
        }
        const bool arrastando = reguaArrasto_.ativo && reguaArrasto_.moveu;
        const double centro = reguaArrasto_.ativo ? reguaArrasto_.pos : tempo::regua::posicaoDaParada(duracaoMin_);
        const int valor = reguaArrasto_.ativo ? reguaArrasto_.minutos() : duracaoMin_;
        // 4x so ate 90 min por rodada: acima disso desliga (e o botao apaga)
        const bool cicloPermitido = tempo::ciclo4xPermitido(valor);
        if (pomodoro_ && !cicloPermitido && podeEditar) pomodoro_ = false;
        const bool ciclo = pomodoro_;

        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background);
        const uint16_t cLaranja = c565(kLaranja), cTonal = c565(kTonal);

        // ---- cartao do foco: nome em 1 linha (reticencias), icone + categoria/"sem projeto" + cr/min, Trocar ›
        {
            const Rect cartao{8, 6, 460, 56};
            const std::string nome = comProjeto ? (titulos_[escolha_].empty() ? std::string{"(sem título)"} : titulos_[escolha_])
                                                : std::string{tempo::nomeCategoria(cat)};
            const auto q = quebrar(nome, 350, 17, 15, 1);
            Bloco card(ui, cartao, SurfaceMuted, 14, 42 + comProjeto);
            card.suave = true;
            card.apertado = podeEditar && ui.pressed(cartao);
            if (!q.linhas.empty()) card.linha(22, 24, 352, q.linhas[0], q.tam, cTexto);
            card.linha(42, 46, 340, tempo::linhaTaxaDoFoco(comProjeto, categoria_), 15, cMudo);
            if (podeEditar) card.linha(344, 34, 110, "Trocar ›", 17, cLaranja, TextAlign::Right);
            card.marca(uint32_t(cat) * 4U + podeEditar * 2U + comProjeto);
            const uint16_t corIcone = comProjeto ? rgb565(120, 160, 255) : cLaranja;
            card.desenhar([&](Arduino_GFX& g, int, int) { icones::phosphor(g, iconeDaCategoria(cat), 30, 46, 15, corIcone, ui.color(SurfaceMuted)); });
            if (ui.firmTap(cartao, podeEditar)) {
                seletor_ = true;
                pagina_ = 0;
                return Action::None;
            }
        }

        // ---- regua (painel unico); toque sem arrasto = -5 / +5 conforme a metade
        {
            const bool avisoNaoConta = naoContaAteMs_ != 0 && int32_t(naoContaAteMs_ - nowMs) > 0;
            if (naoContaAteMs_ != 0 && !avisoNaoConta) naoContaAteMs_ = 0;
            if (avisoNaoConta) {   // "0:40 não conta": sessao livre com menos de 1 min nao registra nada
                Bloco aviso(ui, areaRegua, SurfaceMuted, 16, 0x4E4300U);
                aviso.suave = true;
                aviso.borda = 2;
                aviso.corBorda = c565(kAmbar);
                aviso.linha(areaRegua.x + 24, areaRegua.y + 36, 420, naoContaTxt_, 28, cTexto);   // Bloco: coordenadas absolutas da tela
                aviso.linha(areaRegua.x + 24, areaRegua.y + 74, 420, "menos de 1 min: nada foi registrado", 17, cMudo);
                aviso.desenhar();
            } else {
                VistaRegua vista;
                vista.centro = centro;
                vista.valor = valor;
                vista.arrastando = arrastando;
                vista.ativo = podeEditar;
                vista.x4 = ciclo;
                desenharRegua(ui, areaRegua, vista);
                if (ui.tap(areaRegua, podeEditar) && !foiArrasto) {
                    if (const auto* t = ui.touch()) {
                        // toque no ∞ (fixo na ponta ou visivel na fita) vai ao livre; o "‹ −5" do 5 NAO entra no ∞ (fica apagado)
                        if (tempo::regua::toqueNoInf(int(t->x) - areaRegua.x, centro)) duracaoMin_ = tempo::regua::kLivre;
                        else duracaoMin_ = tempo::regua::ajustarPorToque(duracaoMin_, tempo::regua::metadeDireita(int(t->x), areaRegua.x, areaRegua.w));
                    }
                }
            }
        }

        // ---- Comecar (ou voltar a batalha em curso) + 4x rodadas
        {
            Btn b;
            b.fundo = cLaranja;
            b.tinta = cBg;
            b.suave = true;
            if (emBatalha) {
                b.texto = "Voltar à batalha";
                b.tam = 17;
                if (botao3(ui, Rect{476, 6, 156, 94}, b)) screen = Screen::TempoBatalha;
            } else {
                const auto agora = tempo::agora(nowMs);
                b.texto = "Começar";
                b.sub = tempo::textoComecar(valor, ciclo, agora.valida, agora.minutoDoDia);
                b.tam = 21;
                b.comIcone = true;
                b.icone = IconeId::Play;
                if (botao3(ui, Rect{476, 6, 156, 94}, b)) {
                    comecarSessao(nowMs);
                    screen = Screen::TempoBatalha;
                    return Action::None;
                }
            }
            Btn c;
            c.texto = "4× rodadas";
            c.tam = 17;
            c.comIcone = true;
            c.icone = IconeId::ArrowsClockwise;
            c.fundo = ciclo ? c565(kSelFundo) : cTonal;
            c.tinta = ciclo ? cLaranja : (emBatalha || !cicloPermitido ? rgb565(90, 92, 90) : cTexto);
            c.selecionado = ciclo;
            c.corContorno = cLaranja;
            c.habilitado = !emBatalha && cicloPermitido;
            c.suave = true;
            if (botao3(ui, Rect{476, 108, 156, 60}, c)) pomodoro_ = !pomodoro_;
        }
        return Action::None;
    }

    // ====================================================================== Seletor (opcao B)
    // Pagina 1: uma linha com os 4 blocos de categoria do Foco livre + os 2 projetos usados por ultimo; demais paginas: 3 projetos
    // por pagina. Voltar separado de ▲ ▼ (sem rolagem por arrasto). Escolher volta ao Inicio ja preenchido.
    Action TempoScreen::drawSeletor(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        (void)screen;
        const size_t nProj = lista_.projetos.size();
        std::vector<std::string> ids;
        ids.reserve(nProj);
        for (const auto& p : lista_.projetos) ids.push_back(p.id);
        const auto primeira = tempo::projetosDaPrimeiraPagina(estado_.projetosRecentes, ids);
        const size_t paginas = tempo::paginasDoSeletor(nProj);
        pagina_ = std::min(pagina_, paginas - 1);
        const auto doProjeto = tempo::projetosDaPagina(pagina_, primeira, nProj);

        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cLaranja = c565(kLaranja);
        const uint16_t cPainel = ui.color(SurfaceMuted), cTonal = c565(kTonal), cSel = c565(kSelFundo);
        int y = 1;

        // ---- linha das 4 categorias do Foco livre (so na pagina 1)
        if (pagina_ == 0) {
            static constexpr tempo::Categoria kCats[4] = {tempo::Categoria::Trabalho, tempo::Categoria::Estudo,
                                                          tempo::Categoria::Leitura, tempo::Categoria::Meditacao};
            for (int i = 0; i < 4; ++i) {
                const bool sel = escolha_ < 0 && categoria_ == kCats[i];
                Btn b;
                b.texto = tempo::nomeCategoria(kCats[i]);
                b.tam = 15;
                b.comIcone = true;
                b.empilhado = true;
                b.icone = iconeDaCategoria(kCats[i]);
                b.fundo = sel ? cSel : cPainel;
                b.tinta = cTexto;
                b.tintaIcone = sel ? cLaranja : cMudo;
                b.selecionado = sel;
                b.corContorno = cLaranja;
                b.check = sel;
                if (botao3(ui, Rect{int16_t(6 + i * 128), int16_t(y), 121, 56}, b)) {
                    escolha_ = -1;
                    categoria_ = kCats[i];
                    seletor_ = false;
                    return Action::None;
                }
            }
            y = 58;
        }

        // ---- projetos da pagina
        for (int idx : doProjeto) {
            const auto& projeto = lista_.projetos[idx];
            const Rect rect{6, int16_t(y), 504, 56};
            const bool sel = escolha_ == idx;
            const std::string titulo = titulos_[idx].empty() ? std::string{"(sem título)"} : titulos_[idx];
            std::string info;
            if (projeto.vence && projeto.vence->size() >= 10)
                info = "vence " + projeto.vence->substr(8, 2) + "/" + projeto.vence->substr(5, 2);
            const int larguraInfo = info.empty() ? 0 : ui::sans::width(info, ui::sans::font(15)) + 12;
            const int maxw = 504 - 56 - 14 - larguraInfo - (sel ? 40 : 0);
            const auto q = quebrar(titulo, maxw, 21, 17);
            Bloco linha(ui, rect, SurfaceMuted, 14, 100 + sel);
            linha.corFixa = sel ? int32_t(cSel) : -1;
            linha.apertado = ui.pressed(rect);
            const int passo = q.tam == 21 ? 25 : 21;
            const int y0 = q.linhas.size() == 1 ? y + 28 : y + 28 - passo / 2;
            for (size_t i = 0; i < q.linhas.size(); ++i) linha.linha(62, y0 + int(i) * passo, maxw, q.linhas[i], q.tam, cTexto);
            const int xInfo = 510 - 14 - (sel ? 36 : 0);
            if (!info.empty()) linha.linha(xInfo - larguraInfo, y + 28, larguraInfo, info, 15, cMudo, TextAlign::Right);
            linha.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                if (sel) {
                    g.drawRoundRect(rect.x + dx, rect.y + dy, rect.w, rect.h, 14, cLaranja);
                    g.drawRoundRect(rect.x + dx + 1, rect.y + dy + 1, rect.w - 2, rect.h - 2, 13, cLaranja);
                    icones::phosphor(g, IconeId::CheckCircle, 510 - 24, y + 28, 24, cLaranja, cSel);
                }
                icones::phosphor(g, IconeId::Kanban, 34, y + 28, 24, sel ? cLaranja : c565(Cor{120, 160, 255}), sel ? cSel : cPainel);
            });
            if (ui.firmTap(rect)) {
                escolher(idx);
                seletor_ = false;
                return Action::None;
            }
            y += 57;
        }

        // ---- sem projetos do TickTick (na pagina 1, abaixo das categorias)
        if (nProj == 0 && pagina_ == 0) {
            const Rect painel{6, 58, 504, 113};
            std::string ultima = "última sincronização: nunca";
            if (lista_.gerado_em.size() >= 16)
                ultima = "última sincronização: " + lista_.gerado_em.substr(8, 2) + "/" + lista_.gerado_em.substr(5, 2) + " "
                         + lista_.gerado_em.substr(11, 5);
            const bool sincronizando = sincPedidaMs_ != 0 && nowMs - sincPedidaMs_ < 8000;
            Bloco vazio(ui, painel, SurfaceMuted, 14, 110 + sincronizando);
            vazio.linha(64, 82, 270, "Sem projetos do TickTick", 17, cTexto);
            vazio.linha(64, 110, 270, "Marque tarefas com a tag Projeto no", 13, cMudo);
            vazio.linha(64, 128, 270, "TickTick e sincronize pelo Wi-Fi.", 13, cMudo);
            vazio.linha(64, 152, 270, ultima, 13, c565(Cor{110, 112, 110}));
            vazio.desenhar([&](Arduino_GFX& g, int, int) {
                icones::phosphor(g, IconeId::CloudSlash, 36, 88, 32, cMudo, cPainel);
            });
            Btn b;
            b.texto = sincronizando ? "Sincronizando…" : "Sincronizar";
            b.tam = 17;
            b.comIcone = true;
            b.icone = IconeId::ArrowsClockwise;
            b.fundo = cLaranja;
            b.tinta = ui.color(Background);
            b.habilitado = !sincronizando;
            if (botao3(ui, Rect{338, 96, 164, 56}, b)) {
                pedidoSync_ = true;
                sincPedidaMs_ = nowMs ? nowMs : 1;
            }
        }

        // ---- coluna da direita: Voltar | pagina | ▲ ▼
        {
            Btn v;
            v.texto = "‹ Voltar";
            v.tam = 17;
            v.fundo = cTonal;
            v.tinta = cTexto;
            if (botao3(ui, Rect{518, 1, 116, 56}, v)) {
                seletor_ = false;
                return Action::None;
            }
            Bloco contador(ui, {518, 66, 116, 30}, Background, 0, 120);
            contador.linha(518, 81, 116, "pág. " + std::to_string(pagina_ + 1) + " de " + std::to_string(paginas), 15, cMudo,
                           TextAlign::Center);
            contador.desenhar();
            for (int k = 0; k < 2; ++k) {
                const bool sobe = k == 0;
                const bool ativo = sobe ? pagina_ > 0 : pagina_ + 1 < paginas;
                Btn b;
                b.comIcone = true;
                b.icone = sobe ? IconeId::CaretUp : IconeId::CaretDown;
                b.fundo = ativo ? cTonal : rgb565(18, 19, 18);
                b.tinta = ativo ? cTexto : rgb565(70, 72, 70);
                b.habilitado = ativo;
                b.tam = 28;
                if (botao3(ui, Rect{int16_t(sobe ? 518 : 578), 100, 56, 70}, b)) {
                    if (sobe) --pagina_;
                    else ++pagina_;
                    return Action::None;
                }
            }
        }
        return Action::None;
    }

    // "Fim · segure": o dedo apoiado enche o contorno em ~1 s; soltar antes mostra "segure para encerrar" por 3 s.
    double TempoScreen::progressoFim(bool apertado, uint32_t nowMs) {
        if (apertado) {
            if (!fimApertado_) {
                fimApertado_ = true;
                fimInicioMs_ = nowMs;
            }
            return std::min(1.0, double(nowMs - fimInicioMs_) / 1000.0);
        }
        if (fimApertado_) {
            fimApertado_ = false;
            fimDicaAteMs_ = nowMs + 3000;
        }
        return -1.0;
    }

    void TempoScreen::encerrarPeloFim(uint32_t nowMs, Screen& screen) {
        fimApertado_ = false;
        fimDicaAteMs_ = 0;
        acumular(nowMs);
        if (cron_.focadoMs < 60000) {   // menos de 1 min de foco: cancela sem log nem recompensa
            fase_ = Fase::Nenhuma;
            ciclo_ = tempo::Ciclo{};
            screen = Screen::TempoHoje;
        } else {
            ciclo_ = tempo::Ciclo{};   // encerrar no meio abandona o ciclo
            finalizar(nowMs, !overtime_, fs_);   // em overtime o inimigo ja caiu: nao e fuga
            screen = Screen::TempoVitoria;
        }
    }

    namespace {
        std::string fmtMS(int64_t totalSeg) {
            totalSeg = std::max<int64_t>(0, totalSeg);
            char t[16];
            std::snprintf(t, sizeof t, "%d:%02d", int(totalSeg / 60), int(totalSeg % 60));
            return t;
        }
    } // namespace

    // ====================================================================== Batalha v3
    // Layout 640x172: aneis a esquerda (0..172), texto no meio (176..468), botoes a direita (472..632).
    // Estados: normal, "Fim · segure" apertado, distraido, pausado, overtime (e a pausa do ciclo).
    Action TempoScreen::drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ == Fase::Reflexao || (saqueAtivo_ && !confirmado_)) { screen = Screen::TempoVitoria; return Action::None; }
        if (fase_ == Fase::Nenhuma) { screen = Screen::TempoHoje; return Action::None; }
        if (livre_ && (fase_ == Fase::Lutando || fase_ == Fase::Pausada)) return drawLivre(ui, nowMs, screen);
        if (ocio_.ativo() && fase_ == Fase::Lutando) return drawAvisoOcio(ui, nowMs, screen);
        const bool descanso = fase_ == Fase::Descanso;
        const bool pausada = fase_ == Fase::Pausada;
        const bool bonus = overtime_ && !descanso;
        const bool distr = cron_.distraido && !pausada;
        const std::string nome = nomeBatalha_.empty() ? std::string{"Sem projeto"} : nomeBatalha_;
        const int64_t planoMs = int64_t(planejadoMin_) * 60000;
        const auto agora = tempo::agora(nowMs);

        const uint16_t cLaranja = c565(kLaranja), cAmbar = c565(kAmbar), cAgua = c565(kAgua);
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background);
        const uint16_t cTonal = c565(kTonal), cClaro = c565(kClaro), cVerde = c565(Cor{60, 200, 110});
        const uint16_t cAzulClaro = c565(Cor{150, 205, 255}), cAgua2 = c565(Cor{140, 232, 222});

        // ---- numeros dos aneis
        double voce = 0.0, inimigo = 1.0;
        std::string centro, rotulo;
        int64_t restDescanso = 0;
        if (descanso) {
            restDescanso = std::max<int64_t>(0, descansoMs_ - (int64_t(tempo::ms(nowMs)) - int64_t(descansoInicioMs_)));
            voce = 1.0 - double(restDescanso) / double(descansoMs_);
            inimigo = 0.0;
            centro = fmtRelogio((restDescanso + 999) / 1000);
            rotulo = "pausa";
        } else if (bonus) {
            voce = 1.0 + std::fmod(double(overtimeMs()) / double(planoMs), 1.0);   // 2a volta cheia recomeca (sem teto)
            inimigo = 0.0;
            centro = "+" + fmtMS(overtimeMs() / 1000);
            rotulo = "extra";
        } else {
            voce = std::min(1.0, double(cron_.focadoMs) / double(planoMs));
            inimigo = std::min(1.0, double(restanteMs()) / double(planoMs));
            centro = fmtRelogio((restanteMs() + 999) / 1000);
            rotulo = pausada ? "parado" : "faltam";
        }
        voce = std::floor(voce * 720) / 720;      // quantiza: anel so e redesenhado quando o valor muda
        inimigo = std::floor(inimigo * 720) / 720;

        // ---- aneis + centro
        {
            Bloco aneis(ui, {0, 0, 172, 172}, Background, 0, 20 + surface());
            const bool longo = centro.size() > 5;   // "H:MM:SS" (a partir de 1 h): fonte 21 e caixa mais larga para caber no miolo
            aneis.linha(longo ? kCx - 42 : kCx - 40, 82, longo ? 84 : 80, centro, bonus || longo ? 21 : 26, bonus ? cAzulClaro : cTexto,
                        TextAlign::Center);
            aneis.linha(kCx - 40, 106, 80, rotulo, 13, cMudo, TextAlign::Center);
            aneis.marca(static_cast<uint32_t>(voce * 720) * 1031U + static_cast<uint32_t>(inimigo * 720));
            const Cor fundo = deRole(ui, Background);
            const double escuro = pausada ? 0.35 : 0.0;
            aneis.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                const auto esc = [&](Cor c) { return mistura(c, kPreto, escuro); };
                if (descanso) {
                    anel(g, dx, dy, kCx, kCy, kRaioFora, kEspessura, voce, kAgua, kAgua2, fundo, kAgua);
                    anel(g, dx, dy, kCx, kCy, kRaioDentro, kEspessura, 0.0, kCarmim, kVinho, fundo, kCarmim);
                } else if (bonus) {
                    const Cor b0 = esc(kLaranja), b1 = esc(kAmbar);
                    anel(g, dx, dy, kCx, kCy, kRaioFora, kEspessura, voce, esc(kAzul2), esc(kAzul), fundo, kAzul, &b0, &b1);
                    anel(g, dx, dy, kCx, kCy, kRaioDentro, kEspessura, 0.0, kCarmim, kVinho, fundo, kCarmim);
                } else if (distr) {   // neutro: o foco "esta fora"
                    anel(g, dx, dy, kCx, kCy, kRaioFora, kEspessura, voce, kCinzaA, kCinzaB, fundo, kCinzaA);
                    anel(g, dx, dy, kCx, kCy, kRaioDentro, kEspessura, inimigo, kVinhoApagado, kVinhoApagado2, fundo, kVinhoApagado);
                } else {
                    anel(g, dx, dy, kCx, kCy, kRaioFora, kEspessura, voce, esc(kLaranja), esc(kAmbar), fundo, kLaranja);
                    anel(g, dx, dy, kCx, kCy, kRaioDentro, kEspessura, inimigo, esc(kCarmim), esc(kVinho), fundo, kCarmim);
                }
            });
        }

        // ---- "Fim · segure": estado do dedo (calculado antes de pintar o texto)
        const Rect rectFimLargo{472, 108, 160, 56}, rectFimMini{556, 96, 76, 72};
        const bool fimMini = !descanso && !bonus && !pausada && !distr;
        const Rect rectFim = fimMini ? rectFimMini : rectFimLargo;
        const bool temFim = !descanso && !bonus;
        double progFim = -1.0;
        if (temFim) progFim = progressoFim(ui.pressed(rectFim), nowMs);
        const bool segurando = progFim >= 0.0;
        const bool dicaFim = segurando || (fimDicaAteMs_ != 0 && int32_t(fimDicaAteMs_ - nowMs) > 0);

        // ---- texto central
        const auto est = estimar();
        const std::string cr = fmtSinal(est.cr) + " cr", xp = fmtSinal(est.xp) + " XP";
        const bool curto = rotulo.empty();
        (void)curto;
        Bloco texto(ui, {176, 4, 292, 164}, Background, 0, 40 + surface());
        std::vector<std::function<void(Arduino_GFX&)>> icones;   // icones (logicos) pintados no desenho extra
        const auto icone = [&](IconeId id, int cx, int cy, int lado, uint16_t cor) {
            icones.push_back([=](Arduino_GFX& g) { icones::phosphor(g, id, cx, cy, lado, cor, cBg); });
        };
        const int rodada = ciclo_.ativo ? ciclo_.rodada : 1, rodadas = ciclo_.ativo ? tempo::kCicloRodadas : 1;
        const std::string rodadaTxt = "RODADA " + std::to_string(rodada) + " DE " + std::to_string(rodadas);
        std::string termina;
        if (agora.valida && !pausada && !descanso) {
            const int extra = ciclo_.ativo ? (tempo::kCicloRodadas - ciclo_.rodada) * (ciclo_.focoMin + ciclo_.pausaMin) : 0;
            termina = " · TERMINA " + fmtHoraRelogio(agora.minutoDoDia + int((restanteMs() + 59999) / 60000) + extra);
        }
        const auto nomeLinhas = [&](int y0, uint8_t tamA, uint8_t tamB, uint16_t cor) {
            const auto q = quebrar(nome, 282, tamA, tamB);
            const int passo = q.tam >= 21 ? 25 : 20;
            for (size_t i = 0; i < q.linhas.size(); ++i) texto.linha(176, y0 + int(i) * passo, 292, q.linhas[i], q.tam, cor);
        };
        const auto linhaBonus = [&](int yc) {
            switch (cron_.bonus()) {
            case tempo::BonusPerfeito::Disponivel:
                icone(IconeId::Fire, 184, yc, 16, cAmbar);
                texto.linha(198, yc, 270, "sem distrações · bônus +25%", 15, cAmbar);
                break;
            case tempo::BonusPerfeito::Mantido:
                icone(IconeId::Fire, 184, yc, 16, cAmbar);
                texto.linha(198, yc, 270, "distração curta · bônus mantido", 15, cAmbar);
                break;
            case tempo::BonusPerfeito::Perdido:
                icone(IconeId::Fire, 184, yc, 16, rgb565(110, 112, 110));
                texto.linha(198, yc, 270, "bônus perfeito perdido", 15, cMudo);
                break;
            default: break;
            }
        };

        if (descanso) {
            const int pausaMin = int(descansoMs_ / 60000);
            const bool longa = ciclo_.ativo && ciclo_.passo == tempo::PassoCiclo::PausaLonga;
            icone(IconeId::PauseCircle, 184, 16, 16, cAgua);
            texto.linha(198, 16, 270, ciclo_.ativo ? "PAUSA · RODADA " + std::to_string(ciclo_.rodada) + " DE " + std::to_string(tempo::kCicloRodadas)
                                                   : std::string{"PAUSA"}, 15, cAgua);
            texto.linha(176, 46, 292, std::string{longa ? "Pausa longa de " : "Pausa de "} + std::to_string(pausaMin) + " min", 21, cTexto);
            texto.linha(176, 84, 292, "respire; um bipe avisa quando acabar", 15, cMudo);
            texto.linha(176, 108, 292,
                        ciclo_.ativo ? (longa ? std::string{"depois o ciclo termina"}
                                              : "depois: rodada " + std::to_string(ciclo_.rodada + 1) + " de " + std::to_string(tempo::kCicloRodadas))
                                     : "depois você volta ao Início", 15, cMudo);
            texto.linha(176, 148, 292, "hoje: " + fmtHoraMin(estado_.dia.fochoPagoMin) + " de foco", 15, cMudo);
        } else if (pausada) {
            icone(IconeId::PauseCircle, 184, 16, 18, cAgua);
            texto.linha(198, 16, 270, "PAUSADO · " + fmtMS((int64_t(tempo::ms(nowMs)) - int64_t(pausaInicioMs_)) / 1000), 15, cAgua);
            nomeLinhas(44, 21, 17, cTexto);
            texto.linha(176, 110, 292, "o inimigo não se cura", 17, cAgua2);
            texto.linha(176, 136, 292, "o relógio parou · sem perda de bônus", 15, cMudo);
            if (dicaFim) {
                icone(IconeId::FlagCheckered, 186, 158, 16, cLaranja);
                texto.linha(202, 158, 266, "segure para encerrar", 15, cTexto);
            }
        } else if (bonus) {
            icone(IconeId::StarFour, 184, 16, 16, cAzulClaro);
            texto.linha(198, 16, 270, "TEMPO EXTRA · INIMIGO DERROTADO", 15, cAzulClaro);
            nomeLinhas(44, 21, 17, cTexto);
            icone(IconeId::CheckCircle, 186, 112, 20, cVerde);
            texto.linha(202, 112, 266, cr + " · " + xp + " já garantidos", 15, cVerde);
            texto.linha(176, 140, 292, "cada minuto extra conta", 15, cMudo);
        } else if (distr) {
            icone(IconeId::HandPalm, 184, 16, 16, cClaro);
            texto.linha(198, 16, 270, rodadaTxt + " · FORA DO FOCO", 15, cClaro);
            texto.linha(176, 46, 292, "distraído há " + fmtMS(int64_t(cron_.distracaoAtualMs / 1000)), 26, cTexto);
            nomeLinhas(80, 17, 17, cMudo);
            texto.linha(176, 128, 292, "o inimigo se cura enquanto durar", 15, cMudo);
            switch (cron_.bonus()) {
            case tempo::BonusPerfeito::EmRisco: texto.linha(176, 150, 292, "até 1 min não perde o bônus perfeito", 15, cMudo); break;
            case tempo::BonusPerfeito::Perdido: texto.linha(176, 150, 292, "bônus perfeito perdido", 15, cMudo); break;
            default: texto.linha(176, 150, 292, "volte ao foco e o anel vermelho esvazia", 15, cMudo); break;
            }
        } else {
            texto.linha(176, 16, 292, rodadaTxt + termina, 15, cMudo);
            nomeLinhas(40, 21, 17, cTexto);
            linhaBonus(112);
            if (dicaFim) {
                icone(IconeId::FlagCheckered, 186, 140, 20, cLaranja);
                texto.linha(202, 140, 266, "segure para encerrar", 17, cTexto);
            } else {
                texto.linha(176, 140, 292, "ao vencer: " + cr + " · " + xp, 15, cMudo);
            }
        }
        texto.marca(dicaFim * 4U + segurando * 2U + uint32_t(cron_.bonus()) * 8U);
        texto.desenhar([&](Arduino_GFX& g, int, int) {
            for (const auto& f : icones) f(g);
        });

        // ---- botoes
        const uint16_t cFundoApagado = c565(Cor{70, 42, 26}), cTintaApagada = c565(Cor{150, 100, 70});
        const auto botaoFim = [&](const Rect& r, bool mini) {
            Btn b;
            b.fundo = cTonal;
            b.tinta = cTexto;
            b.comIcone = true;
            b.icone = IconeId::FlagCheckered;
            b.progresso64 = segurando ? int(progFim * 64) : -1;
            if (mini) {
                b.texto = "Fim";
                b.tam = 15;
                b.empilhado = true;
                b.sub2 = "segure";
                b.tintaSub = cMudo;
            } else {
                b.texto = "Fim · segure";
                b.tam = 17;
            }
            botao3(ui, r, b);   // so captura o toque (apertado); o fim acontece pelo tempo segurado
        };
        const auto verificarFim = [&]() {
            if (segurando && progFim >= 1.0) {
                ui.cancelTouchUntilRelease();
                encerrarPeloFim(nowMs, screen);
                return true;
            }
            return false;
        };

        if (descanso) {
            Btn b;
            b.texto = "Encerrar pausa";
            b.tam = 17;
            b.fundo = cAgua;
            b.tinta = cBg;
            if (botao3(ui, Rect{472, 8, 160, 92}, b)) encerrarDescanso(nowMs);
        } else if (bonus) {
            Btn c;
            c.texto = "Concluir";
            c.tam = 21;
            c.comIcone = true;
            c.icone = IconeId::Check;
            c.fundo = cLaranja;
            c.tinta = cBg;
            if (botao3(ui, Rect{472, 8, 160, 92}, c)) {
                finalizar(nowMs, false, fs_);
                screen = Screen::TempoVitoria;
                return Action::None;
            }
            Btn p;
            p.texto = "Pausar";
            p.tam = 17;
            p.comIcone = true;
            p.icone = IconeId::Pause;
            p.fundo = cTonal;
            p.tinta = cTexto;
            if (botao3(ui, Rect{472, 108, 160, 56}, p)) pausarOuRetomar(nowMs);
        } else if (pausada) {
            Btn c;
            c.texto = "Continuar";
            c.tam = 21;
            c.comIcone = true;
            c.icone = IconeId::Play;
            c.fundo = cLaranja;
            c.tinta = cBg;
            if (botao3(ui, Rect{472, 8, 160, 92}, c)) pausarOuRetomar(nowMs);
            botaoFim(rectFimLargo, false);
            if (verificarFim()) return Action::None;
        } else if (distr) {
            Btn c;
            c.texto = "Voltei";
            c.tam = 21;
            c.comIcone = true;
            c.icone = IconeId::Check;
            c.fundo = cClaro;
            c.tinta = cBg;
            if (botao3(ui, Rect{472, 8, 160, 92}, c)) alternarDistracao(nowMs);
            botaoFim(rectFimLargo, false);
            if (verificarFim()) return Action::None;
        } else {
            Btn p;
            p.texto = "Pausar";
            p.tam = 21;
            p.comIcone = true;
            p.icone = IconeId::Pause;
            p.fundo = segurando ? cFundoApagado : cLaranja;
            p.tinta = segurando ? cTintaApagada : cBg;
            p.habilitado = !segurando;
            if (botao3(ui, Rect{472, 8, 160, 80}, p)) pausarOuRetomar(nowMs);
            Btn d;
            d.texto = "Distraí";
            d.tam = 15;
            d.comIcone = true;
            d.empilhado = true;
            d.icone = IconeId::HandPalm;
            d.fundo = cTonal;
            d.tinta = segurando ? rgb565(110, 112, 110) : cTexto;
            d.habilitado = !segurando;
            if (botao3(ui, Rect{472, 96, 76, 72}, d)) alternarDistracao(nowMs);
            botaoFim(rectFimMini, true);
            if (verificarFim()) return Action::None;
        }
        return Action::None;
    }


    // ====================================================================== Livre (parada ∞) e aviso de 90 min
    namespace {
        // ∞ pequeno desenhado por cobertura (mesma curva da regua) sobre o fundo `fundo`.
        void infinitoPequeno(Arduino_GFX& g, float cx, float cy, float larg, float alt, float esp, uint16_t cor, uint16_t fundo) {
            namespace rg = ui::regua;
            const rg::Infinito inf(cx, cy, larg, alt, esp);
            const rg::Rgb c = rg::deRgb565(cor), f = rg::deRgb565(fundo);
            g.startWrite();
            for (int y = int(cy - alt / 2 - 3); y <= int(cy + alt / 2 + 3); ++y)
                for (int x = int(cx - larg / 2 - 3); x <= int(cx + larg / 2 + 3); ++x) {
                    const float cob = inf.cobertura(x + 0.5f, y + 0.5f);
                    if (cob > 0) g.writePixel(int16_t(x), int16_t(y), ui::anel::Anel::para565(rg::mistura(f, c, cob), x, y));
                }
            g.endWrite();
        }

        // Fileira de 12 blocos de 5 min da hora (cheios = ate o gradiente laranja -> ambar; o bloco em andamento enche da esquerda).
        void fileiraDeBlocos(Arduino_GFX& g, int x0, int yc, int larguraTotal, const tempo::livre::Blocos& b, uint16_t fundo, uint16_t vazio) {
            constexpr int kN = tempo::livre::kBlocosPorHora, kAlt = 16;
            const double passo = double(larguraTotal) / kN;
            const int larg = int(std::lround(passo)) - 6;
            for (int i = 0; i < kN; ++i) {
                const int x = x0 + int(std::lround(passo * i));
                const double t = double(i) / (kN - 1);
                const uint16_t cheio = rgb565(uint8_t(255), uint8_t(std::lround(120 + 76 * t)), uint8_t(std::lround(50 + 30 * t)));
                retanguloSuave565(g, x, yc - kAlt / 2, larg, kAlt, 8, vazio, fundo);
                if (i < b.cheios) retanguloSuave565(g, x, yc - kAlt / 2, larg, kAlt, 8, cheio, fundo);
                else if (i == b.cheios && b.parcial > 0.04) {
                    const int w = std::max(16, int(std::lround(larg * b.parcial)));   // largura minima = 2 raios (cantos redondos inteiros)
                    retanguloSuave565(g, x, yc - kAlt / 2, std::min(w, larg), kAlt, 8, cheio, vazio);
                }
            }
        }
    } // namespace

    // Sessao livre (conta para cima, sem meta, sem inimigo): rodando / distraido / pausado. Mockup tempo-regua2 quadros 5-8, 11.
    Action TempoScreen::drawLivre(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (ocio_.ativo() && fase_ == Fase::Lutando) return drawAvisoOcio(ui, nowMs, screen);
        const bool pausada = fase_ == Fase::Pausada;
        const bool distr = cron_.distraido && !pausada;
        const auto agora = tempo::agora(nowMs);
        const uint16_t cLaranja = c565(kLaranja), cAmbar = c565(kAmbar), cAgua = c565(kAgua), cAgua2 = c565(Cor{140, 232, 222});
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background);
        const uint16_t cTonal = c565(kTonal), cClaro = c565(kClaro), cCinza = c565(kCinzaB), cVazio = c565(Cor{52, 34, 22});
        const uint64_t focadoMs = cron_.focadoMs;
        const auto blocos = tempo::livre::blocos(focadoMs);
        const auto est = tempo::liquidarBatalha(estado_, tempo::livre::entrada(cron_, categoriaBatalha_));
        const std::string nome = nomeBatalha_.empty() ? std::string{"Sem projeto"} : nomeBatalha_;

        const std::string relogio = tempo::livre::textoRelogio(distr ? cron_.distracaoAtualMs : focadoMs);   // m:ss; h:mm:ss a partir de 1 h
        const auto& f66 = ui::sans::font(66);
        const int wRelogio = ui::sans::width(relogio, f66);
        const int xCol = std::min(10 + wRelogio + 18, 300);
        const int wCol = 468 - xCol;

        Bloco texto(ui, {0, 0, 468, 172}, Background, 0, 0x4C00U + uint32_t(fase_) * 8U + distr);
        std::vector<std::function<void(Arduino_GFX&)>> extras;
        const auto icone = [&](IconeId id, int cx, int cy, int lado, uint16_t cor) {
            extras.push_back([=](Arduino_GFX& g) { icones::phosphor(g, id, cx, cy, lado, cor, cBg); });
        };
        // cabecalho
        if (pausada) {
            icone(IconeId::PauseCircle, 18, 14, 18, cAgua);
            texto.linha(34, 14, 300, "PAUSADO · " + fmtMS((int64_t(tempo::ms(nowMs)) - int64_t(pausaInicioMs_)) / 1000), 15, cAgua);
        } else if (distr) {
            icone(IconeId::HandPalm, 18, 14, 16, cClaro);
            texto.linha(34, 14, 300, "LIVRE · FORA DO FOCO", 15, cClaro);
        } else {
            extras.push_back([=](Arduino_GFX& g) { infinitoPequeno(g, 20.0f, 14.0f, 18.0f, 9.0f, 2.2f, cLaranja, cBg); });
            texto.linha(34, 14, 380, tempo::livre::cabecalhoRodando(inicioMinutoDia_), 15, cMudo);
        }
        // relogio grande + coluna do nome / ganho
        texto.linha(10, 64, wRelogio + 8, relogio, 66, pausada ? cCinza : cTexto);
        if (distr) {
            texto.linha(xCol, 48, wCol, "distraído", 21, cTexto);
            texto.linha(xCol, 76, wCol, "este tempo não é pago", 15, cMudo);
        } else if (pausada) {
            texto.linha(xCol, 48, wCol, "parado", 21, cAgua2);
            texto.linha(xCol, 76, wCol, "pausa não conta nem custa", 15, cMudo);
        } else {
            const auto q = quebrar(nome, wCol, 17, 15, 2);
            const int y0 = q.linhas.size() > 1 ? 36 : 46;
            for (size_t i = 0; i < q.linhas.size(); ++i) texto.linha(xCol, y0 + int(i) * 20, wCol, q.linhas[i], q.tam, cTexto);
            const int yg = y0 + int(q.linhas.size()) * 20 + 10;
            icone(IconeId::Star, xCol + 7, yg, 14, cAmbar);
            texto.linha(xCol + 22, yg, wCol - 22, fmtSinal(est.cr) + " cr · " + fmtSinal(est.xp) + " XP", 15, cAmbar);
        }
        // fileira de blocos (rotulo da hora a esquerda) + rodape
        texto.linha(10, 118, 36, tempo::livre::rotuloHora(blocos.hora), 15, cMudo);
        const int parcial8 = int(blocos.parcial * 8);
        texto.marca(uint32_t(blocos.cheios) * 131U + uint32_t(parcial8) * 17U + uint32_t(blocos.hora) * 7919U + est.tetoCrAplicado * 3U);
        std::string rodape;
        uint16_t cRodape = cMudo;
        if (pausada) {
            const int64_t restSeg = int64_t((tempo::livre::kPausaMaxMs - std::min<uint64_t>(tempo::livre::kPausaMaxMs, tempo::ms(nowMs) - pausaInicioMs_)) / 1000);
            rodape = agora.valida ? "pausado por mais de 30 min, conclui sozinho às " + fmtHoraRelogio(agora.minutoDoDia + int((restSeg + 59) / 60))
                                  : std::string{"pausado por mais de 30 min, conclui sozinho"};
        } else if (distr) {
            rodape = "o relógio parou em " + tempo::livre::textoRelogio(focadoMs) + " · volta de onde parou";
        } else if (est.tetoCrAplicado) {
            rodape = "passou de 6 h de foco hoje: créditos pela metade";   // teto diario (decisao 23)
            cRodape = cAmbar;
        } else {
            rodape = "sem meta · conclua quando quiser";
        }
        texto.linha(10, 154, 458, rodape, 15, cRodape);
        texto.desenhar([&](Arduino_GFX& g, int, int) {
            for (const auto& f : extras) f(g);
            fileiraDeBlocos(g, 50, 118, 414, blocos, cBg, cVazio);
        });

        // botoes
        const auto concluir = [&](const Rect& r, bool grande) {
            Btn c;
            c.texto = "Concluir";
            c.fundo = cTonal;
            c.tinta = cTexto;
            c.comIcone = true;
            c.icone = IconeId::Check;
            if (grande) {
                c.tam = 17;
            } else {
                c.tam = 15;
                c.empilhado = true;
            }
            if (botao3(ui, r, c)) {
                ui.cancelTouchUntilRelease();
                finalizarLivre(nowMs, fs_, true);
                screen = saqueAtivo_ ? Screen::TempoVitoria : Screen::TempoHoje;
                return true;
            }
            return false;
        };
        if (pausada || distr) {
            Btn c;
            c.texto = pausada ? "Continuar" : "Voltei";
            c.tam = 21;
            c.comIcone = true;
            c.icone = pausada ? IconeId::Play : IconeId::Check;
            c.fundo = pausada ? cLaranja : cClaro;
            c.tinta = cBg;
            if (botao3(ui, Rect{472, 8, 160, 92}, c)) {
                if (pausada) pausarOuRetomar(nowMs);
                else alternarDistracao(nowMs);
            }
            if (concluir(Rect{472, 108, 160, 56}, true)) return Action::None;
        } else {
            Btn p;
            p.texto = "Pausar";
            p.tam = 21;
            p.comIcone = true;
            p.icone = IconeId::Pause;
            p.fundo = cLaranja;
            p.tinta = cBg;
            if (botao3(ui, Rect{472, 8, 160, 80}, p)) pausarOuRetomar(nowMs);
            Btn d;
            d.texto = "Distraí";
            d.tam = 15;
            d.comIcone = true;
            d.empilhado = true;
            d.icone = IconeId::HandPalm;
            d.fundo = cTonal;
            d.tinta = cTexto;
            if (botao3(ui, Rect{472, 96, 76, 72}, d)) alternarDistracao(nowMs);
            if (concluir(Rect{556, 96, 76, 72}, false)) return Action::None;
        }
        return Action::None;
    }

    // "ainda focando?" (quadro 9): depois de 90 min sem toque, em QUALQUER sessao rodando. Contagem de 5 min; sem resposta conclui e
    // paga so ate a hora do aviso. Continuar (qualquer toque/botao tambem continua) ou Concluir.
    Action TempoScreen::drawAvisoOcio(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        avisoDesenhadoMs_ = nowMs != 0 ? nowMs : 1;
        const Rect rContinuar{472, 8, 160, 92}, rConcluir{472, 108, 160, 56};
        if (toqueNovo_) {   // toque comecou: so Concluir (dedo em cima dele) fica de fora; o resto continua a sessao
            toqueNovo_ = false;
            const bool noConcluir = toqueX_ >= rConcluir.x && toqueX_ < rConcluir.x + rConcluir.w && toqueY_ >= rConcluir.y
                                    && toqueY_ < rConcluir.y + rConcluir.h;
            if (!noConcluir) {
                ui.cancelTouchUntilRelease();
                continuarOcio(nowMs);
                return Action::None;
            }
        }
        const uint16_t cLaranja = c565(kLaranja), cAmbar = c565(kAmbar);
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background), cTonal = c565(kTonal);
        const int64_t restSeg = int64_t((ocio_.restanteMs() + 999) / 1000);
        const double feito = 1.0 - double(ocio_.restanteMs()) / double(tempo::livre::AvisoOcioso::kRespostaMs);
        const int larg = int(std::lround(448 * std::clamp(feito, 0.0, 1.0)));
        Bloco texto(ui, {0, 0, 468, 172}, Background, 0, 0x4D00U + livre_);
        std::vector<std::function<void(Arduino_GFX&)>> extras;
        extras.push_back([=](Arduino_GFX& g) { icones::phosphor(g, IconeId::Fire, 18, 14, 18, cLaranja, cBg); });
        texto.linha(34, 14, 400, livre_ ? "LIVRE · 90 MIN SEM TOQUE" : "TIMER · 90 MIN SEM TOQUE", 15, cLaranja);
        texto.linha(10, 58, 290, "ainda focando?", 28, cTexto);
        texto.linha(10, 96, 290, "qualquer toque continua", 17, cMudo);
        texto.linha(310, 62, 150, "conclui em", 15, cMudo);
        texto.linha(310, 96, 150, fmtMS(restSeg), 39, cLaranja);
        texto.linha(10, 156, 458, avisoMinutoDia_ >= 0 ? "sem resposta, paga até a hora do aviso (" + fmtHoraRelogio(avisoMinutoDia_) + ")"
                                                        : std::string{"sem resposta, paga até a hora do aviso"}, 15, cMudo);
        texto.marca(uint32_t(larg) * 7U);
        texto.desenhar([&](Arduino_GFX& g, int, int) {
            for (const auto& f : extras) f(g);
            retanguloSuave565(g, 10, 130, 448, 8, 4, c565(Cor{52, 34, 22}), cBg);
            if (larg > 0) retanguloSuave565(g, 10, 130, std::max(8, larg), 8, 4, cAmbar, c565(Cor{52, 34, 22}));
        });
        Btn c;
        c.texto = "Continuar";
        c.tam = 21;
        c.comIcone = true;
        c.icone = IconeId::Play;
        c.fundo = cLaranja;
        c.tinta = cBg;
        if (botao3(ui, rContinuar, c)) {
            ui.cancelTouchUntilRelease();
            continuarOcio(nowMs);
            return Action::None;
        }
        Btn d;
        d.texto = "Concluir";
        d.tam = 17;
        d.comIcone = true;
        d.icone = IconeId::Check;
        d.fundo = cTonal;
        d.tinta = cTexto;
        if (botao3(ui, rConcluir, d)) {
            ui.cancelTouchUntilRelease();
            ocio_.zerar();
            avisoDesenhadoMs_ = 0;
            ultimoMs_ = tempo::ms(nowMs);
            if (livre_) {
                finalizarLivre(nowMs, fs_, true);
                screen = saqueAtivo_ ? Screen::TempoVitoria : Screen::TempoHoje;
            } else {
                ciclo_ = tempo::Ciclo{};
                finalizar(nowMs, !overtime_, fs_);   // Concluir manual: paga o que foi focado ate agora (nao a hora do aviso)
                screen = Screen::TempoVitoria;
            }
        }
        return Action::None;
    }

    // ====================================================================== Fechamento v3 (Reflexao + Saque)
    namespace {
        // Rosto grande (humor 0..4) do mockup tempo3-fechamento: contorno de 3 px, olhos e boca em arco (Pessimo..Otimo).
        void rostoGrande(Arduino_GFX& g, int cx, int cy, int r, int humor, uint16_t cor, uint16_t fundo) {
            const bool pequeno = r < 16;   // versao miniatura (linha "humor salvo"): tracos de 2 px
            const float traco = pequeno ? 2.0f : 3.0f;   // contorno = anel cheio de cor unica, borda suavizada
            desenharAnelSuave(g, cx, cy, float(r) - traco / 2.0f, traco, 1.0, cor, cor, fundo, cor);
            const auto ponto = [&](int x, int y, int raio) {
                if (pequeno) g.fillRect(x - 1, y - 1, 2, 2, cor);
                else g.fillCircle(x, y, raio, cor);
            };
            for (const int ex : {-1, 1}) ponto(cx + int(std::lround(ex * r * 0.36)), cy - int(std::lround(r * 0.22)), 3);
            const double curva = (humor - 2) / 2.0 * (pequeno ? 2.4 : 1.0);   // miniatura: boca exagerada para ler o humor
            for (int k = 0; k <= 40; ++k) {
                const double u = -0.5 + k / 40.0;
                ponto(cx + int(std::lround(u * r * 1.1)), cy + int(std::lround((0.28 + curva * (0.18 - 0.7 * u * u)) * r)), 1);
            }
        }

        // Corta com reticencias para caber em `larg` px.
        std::string cortarLargura(std::string texto, int larg, uint8_t tam) {
            const auto& fonte = ui::sans::font(tam);
            if (ui::sans::width(texto, fonte) <= larg) return texto;
            while (!texto.empty() && ui::sans::width(texto + "...", fonte) > larg) {
                texto.pop_back();
                while (!texto.empty() && (static_cast<unsigned char>(texto.back()) & 0xC0U) == 0x80U) texto.pop_back();   // nao parte UTF-8
            }
            return texto + "...";
        }

        constexpr const char* kHumorNome[] = {"Péssimo", "Mal", "Ok", "Bem", "Ótimo"};
        constexpr const char* kDistraiuNome[] = {"Não", "Um pouco", "Muito"};
    } // namespace

    Action TempoScreen::drawVitoria(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (saqueAtivo_) return drawSaque(ui, nowMs, screen);
        if (fase_ != Fase::Reflexao) { screen = battleActive() ? Screen::TempoBatalha : Screen::TempoHoje; return Action::None; }
        return distracaoAberta_ ? drawDistracoes(ui, nowMs, screen) : drawReflexao(ui, nowMs, screen);
    }

    // Reflexao v3 (quadro 1): 5 rostos grandes (125 x 78 px), nenhum pre-selecionado, 1 toque salva (e abre o saque); chip opcional
    // "distracoes" (56 px) corrige o que nao foi marcado na hora. Sem resposta em 60 s salva SEM humor (tick).
    Action TempoScreen::drawReflexao(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        (void)screen;
        const auto& r = resultado_;
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background);
        const uint16_t cPainel = ui.color(SurfaceMuted), cTonal = c565(kTonal), cDim = c565(Cor{110, 112, 106});

        const std::string meio = emCiclo_ && cicloRodadas_ > 0
                                     ? "ciclo de " + std::to_string(cicloRodadas_) + (cicloRodadas_ == 1 ? " rodada · " : " rodadas · ") + fmtHoraMin(cicloFocadoMin_)
                                     : "foco · " + fmtHoraMin(r.focadoMin);
        const int larguraMeio = ui::sans::width(meio, ui::sans::font(15));
        Bloco topo(ui, {0, 0, 640, 28}, Background, 0, 0xC0DE);
        topo.linha(8, 13, 96, "COMO FOI?", 15, cMudo);
        topo.linha(108, 13, larguraMeio + 6, meio, 15, cTexto);
        const int xProj = 108 + larguraMeio + 16;
        topo.linha(xProj, 13, 632 - xProj, cortarLargura(r.projeto, 632 - xProj, 15), 15, cMudo, TextAlign::Right);
        topo.desenhar();

        int escolhido = -1;
        for (int i = 0; i < 5; ++i) {
            const Rect caixa{static_cast<int16_t>(8 + i * 125), 28, 119, 78};
            const bool apertado = ui.pressed(caixa);
            const uint32_t sig = ui::Context::combine(0x9100U + uint32_t(i), apertado);
            if (ui.redraw(caixa, sig, true)) {
                const auto rotulo = ui.prepareText({caixa.x, static_cast<int16_t>(caixa.y + 54), caixa.w, 22}, kHumorNome[i], 17, TextAlign::Center, 1);
                ui.paint(caixa, [&](Arduino_GFX& g, Rect alvo) {
                    const uint16_t fundo = apertado ? misturar565(cPainel, 0xFFFF, 0.2) : cPainel;
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, cBg);
                    g.fillRoundRect(caixa.x, caixa.y, caixa.w, caixa.h, 16, fundo);
                    if (apertado) {
                        g.drawRoundRect(caixa.x, caixa.y, caixa.w, caixa.h, 16, 0xFFFF);
                        g.drawRoundRect(caixa.x + 1, caixa.y + 1, caixa.w - 2, caixa.h - 2, 15, 0xFFFF);
                    }
                    rostoGrande(g, caixa.x + 60, caixa.y + 28, 25, i, cTexto, fundo);
                    ui.drawText(g, rotulo, cTexto, alvo.x - caixa.x, alvo.y - caixa.y, fundo);
                });
            }
            if (ui.firmTap(caixa)) escolhido = i;
        }

        // chip opcional "distracoes: N ›" (nao bloqueia o 1 toque) + dicas
        Btn chip;
        chip.texto = "distrações: " + (distraiu_ >= 0 ? std::string{kDistraiuNome[distraiu_]} : std::to_string(distracoesPend_)) + " ›";
        chip.tam = 17;
        chip.comIcone = true;
        chip.icone = IconeId::HandPalm;
        chip.fundo = cTonal;
        chip.tinta = cTexto;
        const bool abrirDistracoes = botao3(ui, Rect{8, 114, 222, 56}, chip);
        Bloco dicas(ui, {240, 114, 392, 56}, Background, 0, 0xD1CA);
        dicas.linha(246, 130, 380, "toque num rosto e já salva", 15, cMudo);
        dicas.linha(246, 152, 380, "sem resposta em " + std::to_string(tempo::kReflexaoAutoSalvarSeg) + " s: salva sem humor", 15, cDim);
        dicas.desenhar();

        if (escolhido >= 0) {
            humor_ = escolhido;
            ui.cancelTouchUntilRelease();
            fecharReflexao(nowMs, fs_, true, true);
        } else if (abrirDistracoes) {
            distracaoAberta_ = true;
            reflexaoInicioMs_ = nowMs;   // interagiu: o prazo de 60 s recomeca
        }
        return Action::None;
    }

    // Corrige "Se distraiu?" (Nao / Um pouco / Muito) so para o registro; volta aos rostos.
    Action TempoScreen::drawDistracoes(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        (void)screen;
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background);
        const uint16_t cTonal = c565(kTonal), cPainel = ui.color(SurfaceMuted), cAmbar = c565(kAmbar);
        Bloco topo(ui, {0, 0, 640, 28}, Background, 0, 0xC0DF);
        topo.linha(8, 13, 120, "DISTRAÇÕES", 15, cMudo);
        const std::string marcadas = distracoesPend_ == 0 ? std::string{"nenhuma marcada na hora"}
                                                          : std::to_string(distracoesPend_) + (distracoesPend_ == 1 ? " marcada na hora" : " marcadas na hora");
        topo.linha(136, 13, 496, marcadas + " · como foi no geral?", 15, cTexto);
        topo.desenhar();
        int escolhido = -1;
        for (int j = 0; j < 3; ++j) {
            Btn b;
            b.texto = kDistraiuNome[j];
            b.tam = 21;
            b.fundo = distraiu_ == j ? cAmbar : cPainel;
            b.tinta = distraiu_ == j ? cBg : cTexto;
            if (botao3(ui, Rect{static_cast<int16_t>(8 + j * 212), 28, 204, 78}, b)) escolhido = j;
        }
        Btn voltar;
        voltar.texto = "‹ Voltar";
        voltar.tam = 17;
        voltar.fundo = cTonal;
        voltar.tinta = cTexto;
        const bool vai = botao3(ui, Rect{8, 114, 222, 56}, voltar);
        Bloco dica(ui, {240, 114, 392, 56}, Background, 0, 0xD1CB);
        dica.linha(246, 130, 380, "só corrige o registro do dia", 15, cMudo);
        dica.linha(246, 152, 380, "não muda créditos nem XP", 15, cMudo);
        dica.desenhar();
        if (escolhido >= 0) {
            distraiu_ = escolhido;
            distracaoAberta_ = false;
            reflexaoInicioMs_ = nowMs;
        } else if (vai) {
            distracaoAberta_ = false;
        }
        return Action::None;
    }

    // Saque (quadro 2, estilo celebracao dos Habitos): resultado, nome do foco, +cr +XP, bonus, humor, Desfazer por 8 s e o proximo passo.
    Action TempoScreen::drawSaque(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        const auto& r = resultado_;
        const bool podeDesfazer = podeDesfazerBatalha(nowMs);
        const uint16_t cLaranja = c565(kLaranja), cAmbar = c565(kAmbar), cAgua = c565(kAgua);
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background);
        const uint16_t cTonal = c565(kTonal), cVerde = c565(Cor{60, 200, 110}), cPilula = rgb565(52, 40, 14);

        std::string titulo = saqueLivre_ ? "LIVRE CONCLUÍDO" : r.liq.vitoria ? (r.liq.perfeita ? "VITÓRIA PERFEITA" : "VITÓRIA") : "ENCERRADA";
        if (emCiclo_ && cicloRodadas_ > 0) titulo += " · RODADA " + std::to_string(cicloRodadas_) + " DE " + std::to_string(tempo::kCicloRodadas);
        const uint16_t cTitulo = r.liq.vitoria || saqueLivre_ ? cVerde : cMudo;

        if (saqueLivre_) {   // livre (quadro 12): tempo total grande, nome, +cr +XP; sem aneis, sem Reflexao de humor
            Bloco t(ui, {0, 0, 468, 172}, Background, 0, 0x5C00U);
            std::vector<std::function<void(Arduino_GFX&)>> ex;
            ex.push_back([=](Arduino_GFX& g) { icones::phosphor(g, IconeId::CheckCircle, 18, 14, 18, cVerde, cBg); });
            t.linha(34, 14, 400, titulo, 15, cVerde);
            const auto partes = tempo::livre::partesDoTempo(uint64_t(r.focadoMin) * 60000ULL);
            int x = 10;
            for (const auto& p : partes) {
                const uint8_t tam = p.grande ? 66 : 28;
                const int w = ui::sans::width(p.texto, ui::sans::font(tam)) + 4;
                t.textoCor(Rect{int16_t(x), int16_t(p.grande ? 64 - ui::sans::font(66).height / 2 : 85 - ui::sans::font(28).height / 2), int16_t(w), int16_t(ui::sans::font(tam).height)},
                           p.texto, tam, p.grande ? cTexto : cMudo);
                x += w + (p.grande ? 2 : 6);
            }
            t.linha(x + 8, 88, 468 - x - 8, "de foco", 17, cMudo);
            const std::string nomeL = cortarLargura(r.projeto.empty() ? std::string{"Sem projeto"} : r.projeto, 448, 17);
            t.linha(10, 124, 448, nomeL, 17, cTexto);
            const std::string crT = fmtSinal(r.liq.cr) + " cr", xpT = fmtSinal(r.liq.xp) + " XP";
            const int wc = ui::sans::width(crT, ui::sans::font(17)) + 24, wx = ui::sans::width(xpT, ui::sans::font(17)) + 24;
            t.linha(10 + 12, 154, wc, crT, 17, cAmbar);
            t.linha(10 + wc + 8 + 12, 154, wx, xpT, 17, cAmbar);
            t.marca(uint32_t(r.focadoMin) * 5U);
            t.desenhar([&](Arduino_GFX& g, int, int) {
                g.fillRoundRect(10, 140, wc, 28, 14, cPilula);
                g.fillRoundRect(10 + wc + 8, 140, wx, 28, 14, cPilula);
                for (const auto& f : ex) f(g);
            });
        }
        // ---- aneis (cheio = venceu; encerrada = fracao feita) + marca de vitoria
        const double plano = std::max(1.0, double(r.planejadoMin));
        const double voce = std::floor((r.liq.vitoria ? 1.0 : std::clamp(double(r.focadoMin) / plano, 0.0, 1.0)) * 720) / 720;
        if (!saqueLivre_) {
            Bloco aneis(ui, {0, 0, 172, 172}, Background, 0, 0x5A00U + r.liq.vitoria);
            aneis.marca(static_cast<uint32_t>(voce * 720));
            const Cor fundo = deRole(ui, Background);
            const bool venceu = r.liq.vitoria;
            aneis.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                anel(g, dx, dy, kCx, kCy, kRaioFora, kEspessura, voce, kLaranja, kAmbar, fundo, kLaranja);
                anel(g, dx, dy, kCx, kCy, kRaioDentro, kEspessura, 0.0, kCarmim, kVinho, fundo, kCarmim);
                icones::phosphor(g, venceu ? IconeId::Check : IconeId::FlagCheckered, kCx, kCy, 40, venceu ? cVerde : cMudo, cBg);
            });
        }

        // ---- texto central
        Bloco texto(ui, saqueLivre_ ? Rect{0, 0, 0, 0} : Rect{176, 4, 292, 164}, Background, 0, 0x5B00U + r.liq.vitoria * 2U + r.liq.perfeita);
        std::vector<std::function<void(Arduino_GFX&)>> extras;
        const auto icone = [&](IconeId id, int cx, int cy, int lado, uint16_t cor) {
            extras.push_back([=](Arduino_GFX& g) { icones::phosphor(g, id, cx, cy, lado, cor, cBg); });
        };
        icone(r.liq.vitoria ? IconeId::CheckCircle : IconeId::FlagCheckered, 184, 16, 18, cTitulo);
        texto.linha(198, 16, 270, titulo, 15, cTitulo);
        const auto q = quebrar(r.projeto.empty() ? std::string{"Sem projeto"} : r.projeto, 282, 21, 17);
        const int passoNome = q.tam >= 21 ? 25 : 20;
        for (size_t i = 0; i < q.linhas.size(); ++i) texto.linha(176, 40 + int(i) * passoNome, 292, q.linhas[i], q.tam, cTexto);
        const std::string crTxt = fmtSinal(r.liq.cr) + " cr", xpTxt = fmtSinal(r.liq.xp) + " XP";
        const int wCr = ui::sans::width(crTxt, ui::sans::font(17)) + 24, wXp = ui::sans::width(xpTxt, ui::sans::font(17)) + 24;
        texto.linha(176 + 12, 90, wCr, crTxt, 17, cAmbar);
        texto.linha(176 + wCr + 8 + 12, 90, wXp, xpTxt, 17, cAmbar);
        if (r.liq.perfeita) texto.linha(176, 134, 292, "inclui bônus perfeito +25%", 15, cVerde);
        else if (!r.liq.vitoria) texto.linha(176, 134, 292, r.abandonou ? "encerrada antes do fim" : "distrações demais", 15, cMudo);
        if (!saqueDeReflexao_) {
            texto.linha(176, 154, 292, "humor: só no fim do ciclo", 15, cMudo);
        } else if (humor_ >= 0 && humor_ <= 4) {
            texto.linha(202, 154, 266, std::string{"humor salvo: "} + kHumorNome[humor_], 15, cMudo);
            const int h = humor_;
            extras.push_back([=](Arduino_GFX& g) { rostoGrande(g, 187, 154, 9, h, cMudo, cBg); });
        } else {
            texto.linha(176, 154, 292, "sem humor", 15, cMudo);
        }
        texto.marca(uint32_t(humor_ + 1) * 31U + saqueDeReflexao_);
        if (!saqueLivre_) texto.desenhar([&](Arduino_GFX& g, int, int) {
            g.fillRoundRect(176, 76, wCr, 28, 14, cPilula);
            g.fillRoundRect(176 + wCr + 8, 76, wXp, 28, 14, cPilula);
            for (const auto& f : extras) f(g);
        });
        // ---- botoes: proximo passo (primario) + Desfazer (8 s) ou "Mais uma rodada"
        const int minutos = tempo::minutosProximoPasso(proximo_, ciclo_);
        const bool pausa = proximo_ != tempo::ProximoPasso::VoltarInicio;
        Btn primario;
        primario.texto = pausa ? "Pausa " + std::to_string(minutos) + " min" : "Voltar ao início";
        primario.sub = "próximo passo";
        primario.tam = 17;
        primario.comIcone = pausa;
        primario.icone = IconeId::Coffee;
        primario.fundo = pausa ? cAgua : cLaranja;
        primario.tinta = cBg;
        const bool vaiAdiante = botao3(ui, Rect{472, 8, 160, 92}, primario);
        bool desfez = false, maisUma = false;
        if (podeDesfazer) {
            Btn d;
            d.texto = "Desfazer";
            d.tam = 17;
            d.comIcone = true;
            d.icone = IconeId::ArrowCounterClockwise;
            d.fundo = cTonal;
            d.tinta = cTexto;
            if (saqueLivre_) d.sub = "volta a contar";
            desfez = botao3(ui, Rect{472, 108, 160, 56}, d);
        } else if (!pausa && !saqueLivre_) {
            Btn m;
            m.texto = "Mais uma rodada";
            m.tam = 15;
            m.comIcone = true;
            m.icone = IconeId::Play;
            m.fundo = cTonal;
            m.tinta = cTexto;
            maisUma = botao3(ui, Rect{472, 108, 160, 56}, m);
        }
        if (desfez) {
            ui.cancelTouchUntilRelease();
            desfazerBatalha(nowMs, fs_, screen);
        } else if (vaiAdiante) {
            ui.cancelTouchUntilRelease();
            acaoPrimariaSaque(nowMs, screen, fs_);
        } else if (maisUma) {
            ui.cancelTouchUntilRelease();
            sairDoSaque(nowMs, fs_);
            comecarSessao(nowMs);
            screen = battleActive() ? Screen::TempoBatalha : Screen::TempoHoje;
        }
        return Action::None;
    }

} // namespace screens
