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
#include "tempo/Sessao.h"
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
        std::string fmtRelogio(int64_t totalSeg) {
            totalSeg = std::max<int64_t>(0, totalSeg);
            char texto[24];
            if (totalSeg >= 3600)
                std::snprintf(texto, sizeof texto, "%d:%02d:%02d", int(totalSeg / 3600), int(totalSeg / 60 % 60), int(totalSeg % 60));
            else
                std::snprintf(texto, sizeof texto, "%02d:%02d", int(totalSeg / 60), int(totalSeg % 60));
            return texto;
        }

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
                if (!ui.redraw(area, sig, true)) return;
                const uint16_t base = corFixa >= 0 ? uint16_t(corFixa) : ui.color(fundo);
                const uint16_t cor = apertado ? misturar565(base, 0xFFFF, 0.16) : base;
                ui.paint(area, [&](Arduino_GFX& g, Rect alvo) {
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, ui.color(Background));
                    if (raio > 0) g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, raio, cor);
                    else g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, cor);
                    if (apertado && raio > 0) {
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
        constexpr int kIniCx = 80, kIniCy = 106, kIniRaio = 56, kIniLarg = 12;   // anel unico do Inicio (v3)
        constexpr Cor kTonal{34, 36, 35}, kSelFundo{44, 28, 16}, kClaro{228, 228, 220};
        constexpr Cor kCinzaA{120, 122, 116}, kCinzaB{165, 166, 158}, kVinhoApagado{120, 56, 72}, kVinhoApagado2{150, 70, 92};
        constexpr int kAutoSalvarSeg = 60;

        uint16_t c565(Cor c) { return rgb565(uint8_t(c.r), uint8_t(c.g), uint8_t(c.b)); }
        Cor mistura(Cor a, Cor b, double t) {
            t = std::clamp(t, 0.0, 1.0);
            return {int(a.r + (b.r - a.r) * t), int(a.g + (b.g - a.g) * t), int(a.b + (b.b - a.b) * t)};
        }
        Cor deRole(ui::Context& ui, ColorRole papel) {
            const uint16_t v = ui.color(papel);  // RGB565 -> RGB888
            return {((v >> 11) & 31) * 255 / 31, ((v >> 5) & 63) * 255 / 63, (v & 31) * 255 / 31};
        }

        // Anel estilo Apple: trilho (cor do anel 22 % sobre o fundo), arco em degrade ao longo do angulo
        // (segmentos de 4 graus), comecando no topo no sentido horario, pontas redondas; frac > 1 = 2a volta por
        // cima com sombra sob a ponta. (ox, oy) = origem da regiao no alvo de pintura.
        void anel(Arduino_GFX& g, int ox, int oy, int cx, int cy, int raio, int larg, double frac, Cor c0, Cor c1,
                  Cor fundo, Cor baseTrilho, const Cor* volta1a0 = nullptr, const Cor* volta1a1 = nullptr) {
            const int X = ox + cx, Y = oy + cy;
            const double ro = raio + larg / 2.0, ri = raio - larg / 2.0;
            g.fillCircle(X, Y, int(ro), c565(mistura(fundo, baseTrilho, 0.22)));
            g.fillCircle(X, Y, int(ri), c565(fundo));
            if (frac <= 0.0005) return;
            constexpr double kRad = 3.14159265358979 / 180.0;
            const auto ponto = [&](double ang, double r, int& px, int& py) {
                px = X + int(std::lround(r * std::cos(ang * kRad)));
                py = Y + int(std::lround(r * std::sin(ang * kRad)));
            };
            const auto segCor = [&](double a0, double a1, double k0, double k1, Cor d0, Cor d1) {
                const int n = std::max(2, int(std::ceil((a1 - a0) / 4.0)));
                for (int i = 0; i < n; ++i) {
                    const double aa = a0 + (a1 - a0) * i / n;
                    const double bb = a0 + (a1 - a0) * (i + 1) / n + 0.8;
                    const uint16_t cor = c565(mistura(d0, d1, k0 + (k1 - k0) * (i + 0.5) / n));
                    int x1, y1, x2, y2, x3, y3, x4, y4;
                    ponto(aa, ro, x1, y1); ponto(bb, ro, x2, y2); ponto(bb, ri, x3, y3); ponto(aa, ri, x4, y4);
                    g.fillTriangle(x1, y1, x2, y2, x3, y3, cor);
                    g.fillTriangle(x1, y1, x3, y3, x4, y4, cor);
                }
            };
            const auto seg = [&](double a0, double a1, double k0, double k1) { segCor(a0, a1, k0, k1, c0, c1); };
            const auto capa = [&](double ang, Cor c) {
                int px, py;
                ponto(ang, raio, px, py);
                g.fillCircle(px, py, larg / 2, c565(c));
            };
            const double volta1 = std::min(frac, 1.0);
            capa(-90, c0);
            seg(-90, -90 + 360 * volta1, 0, volta1);
            if (frac <= 1.0) {
                capa(-90 + 360 * volta1, mistura(c0, c1, volta1));
                return;
            }
            if (volta1a0 != nullptr && volta1a1 != nullptr) {   // 1a volta completa nas cores de base (overtime: laranja)
                capa(-90, *volta1a0);
                segCor(-90, 269.9, 0, 1, *volta1a0, *volta1a1);
            }
            const double fim = -90 + 360 * std::min(frac - 1.0, 0.999);
            int px, py;
            ponto(fim + 7, raio, px, py);  // sombra escura sob/adiante da ponta da 2a volta
            g.fillCircle(px, py, larg / 2 + 1, c565(mistura(c1, kPreto, 0.6)));
            capa(-90, c0);
            seg(-90, fim, 0, 1);
            capa(fim, c1);
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
                                         + uint32_t(int(b.icone)) * 8192U + uint32_t(b.progresso64 + 1) * 262144U));
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
                    g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 16, fundo);
                    if (b.selecionado || apertado) {
                        const uint16_t cc = b.selecionado ? (b.corContorno != 0 ? b.corContorno : tinta) : uint16_t(0xFFFF);   // apertado = contorno branco
                        g.drawRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 16, cc);
                        g.drawRoundRect(alvo.x + 1, alvo.y + 1, alvo.w - 2, alvo.h - 2, 15, cc);
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

        // Rosto (humor 0..4) com circulos/arcos: a fonte nao tem emoji.
        void rosto(Arduino_GFX& g, int cx, int cy, int humor, uint16_t cor) {
            g.drawCircle(cx, cy, 11, cor);
            g.drawCircle(cx, cy, 10, cor);
            g.fillCircle(cx - 4, cy - 3, 2, cor);
            g.fillCircle(cx + 4, cy - 3, 2, cor);
            constexpr double kRad = 3.14159265358979 / 180.0;
            if (humor == 2) {
                g.fillRect(cx - 5, cy + 3, 11, 2, cor);
            } else if (humor > 2) {
                const double h = humor == 3 ? 3 : 5;  // sorriso: metade de baixo de uma elipse
                for (int a = 20; a <= 160; a += 6)
                    g.fillCircle(cx + int(std::lround(5 * std::cos(a * kRad))), cy + 4 + int(std::lround(h * std::sin(a * kRad))), 1, cor);
            } else {
                const double h = humor == 1 ? 3 : 5;  // carranca: metade de cima
                for (int a = 200; a <= 340; a += 6)
                    g.fillCircle(cx + int(std::lround(5 * std::cos(a * kRad))), cy + 4 + int(h) + int(std::lround(h * std::sin(a * kRad))), 1, cor);
            }
        }

        std::string fmtHoraRelogio(int minutoDoDia) {
            char texto[8];
            minutoDoDia = ((minutoDoDia % 1440) + 1440) % 1440;
            std::snprintf(texto, sizeof texto, "%02d:%02d", minutoDoDia / 60, minutoDoDia % 60);
            return texto;
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
        if (fase_ == Fase::Reflexao) {
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
            if (r.descanso) r.centro = "pausa";
            else r.centro = r.bonus ? "+" + fmtRelogio(overtimeMs() / 1000) : fmtRelogio((restanteMs() + 999) / 1000);
        } else {
            r.projeto = escolha_ >= 0 && size_t(escolha_) < titulos_.size() ? titulos_[escolha_]
                                                                           : tempo::nomeFocoLivre(categoria_);
            r.centro = fmtRelogio(int64_t(ciclo_.ativo || pomodoro_ ? tempo::kCicloFocoMin : duracaoMin_) * 60);
            r.planejadoMin = pomodoro_ ? tempo::kCicloFocoMin : duracaoMin_;
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
        screen = fase_ == Fase::Reflexao ? Screen::TempoVitoria : Screen::TempoBatalha;
    }

    // Botao do cartao AGORA (sem batalha): abre o Inicio com o proximo projeto (o 1o da lista) ja escolhido.
    bool TempoScreen::comecar(Screen& screen) {
        if (battleActive() || fase_ == Fase::Reflexao) return false;
        seletor_ = false;
        if (!lista_.projetos.empty()) escolher(0);
        screen = Screen::TempoHoje;
        return true;
    }

    // BOOT curto no launcher: Foco livre imediato com a ultima categoria/duracao (ignora o ciclo pomodoro).
    void TempoScreen::focoRapido(uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        if (!carregado_) carregar(filesystem, true, nowMs);
        if (battleActive() || fase_ == Fase::Reflexao) {
            abrirBatalha(screen);
            return;
        }
        seletor_ = false;
        escolha_ = -1;
        ciclo_ = tempo::Ciclo{};
        planejadoMin_ = duracaoMin_;
        categoriaBatalha_ = categoria_;
        projeto_ = -1;
        nomeBatalha_ = tempo::nomeFocoLivre(categoriaBatalha_);
        salvarPreferencias();
        ligarBatalha(nowMs);
        screen = Screen::TempoBatalha;
    }

    void TempoScreen::escolher(int indice) {
        // Com projeto a categoria e UNICA (Projetos, vem de tempo::categoriaDoFoco); categoria_ guarda so a do Foco livre.
        escolha_ = indice >= 0 && size_t(indice) < lista_.projetos.size() ? indice : -1;
    }

    void TempoScreen::salvarPreferencias() {
        estado_.ultimaCategoria = static_cast<int>(tempo::categoriaLivreValida(static_cast<int>(categoria_)));
        estado_.ultimaDuracaoMin = duracaoMin_;
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
        for (const auto& item : fila) TempoStorage::enqueueCheckin(*filesystem, item);
        if (!fila.empty()) checkinNovo_ = true;   // lume.12: o App sincroniza logo ("Feito" envia na hora)
        gravarEventoHabito(filesystem, r, "timer");
    }

    void TempoScreen::atualizarHabitos(fs::FS* filesystem, uint32_t nowMs) {
        if (filesystem == nullptr || fase_ == Fase::Lutando || fase_ == Fase::Pausada || fase_ == Fase::Descanso) return;
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
        if (!carregado_ || fase_ != Fase::Nenhuma) return;   // em batalha/reflexao: a manutencao relê depois (versaoVista_ fica velha de proposito)
        versaoVista_ = versao;
        recargaPedida_ = false;
        recarregarProjetos(filesystem);
        atualizarHabitos(filesystem, nowMs);
    }

    void TempoScreen::manutencaoHabitos(uint32_t nowMs, fs::FS* filesystem, uint32_t versaoSync) {
        if (!carregado_ || filesystem == nullptr || fase_ != Fase::Nenhuma) return;   // nada durante batalha/reflexao
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
            duracaoMin_ = std::clamp(estado_.ultimaDuracaoMin / 5 * 5, 5, 60);
            pomodoro_ = estado_.ciclopomodoro;
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
        if (battleActive() || fase_ == Fase::Reflexao) return;
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
        ciclo_ = pomodoro_ ? tempo::iniciarCiclo() : tempo::Ciclo{};
        planejadoMin_ = pomodoro_ ? tempo::kCicloFocoMin : duracaoMin_;
        salvarPreferencias();
        ligarBatalha(nowMs);
    }

    void TempoScreen::ligarBatalha(uint32_t nowMs) {
        cron_.zerar(planejadoMin_);
        overtime_ = false;
        arrastando_ = false;
        fimApertado_ = false;
        fimDicaAteMs_ = 0;
        ultimoMs_ = tempo::ms(nowMs);
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

    void TempoScreen::finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem, bool tocarDesfecho) {
        acumular(nowMs);
        cron_.voltar();   // fecha uma distracao em aberto
        garantirDia(nowMs);
        const int nivelAntes = tempo::nivelDeXp(estado_.xpTotal);
        const int streakAntes = estado_.streak;
        tempo::EntradaBatalha entrada = cron_.entrada(categoriaBatalha_, true);
        entrada.abandonou = abandonou;
        const auto liquidacao = tempo::aplicarBatalha(estado_, entrada);
        // Meditacao vencida marca a proxima ocorrencia pendente do habito de Meditacao (no horario) e enfileira o check-in.
        if (liquidacao.vitoria && categoriaBatalha_ == tempo::Categoria::Meditacao) registrarHabitoMeditacao(filesystem);

        const std::string titulo = nomeBatalha_;
        resultado_ = {liquidacao, titulo, planejadoMin_, entrada.minutosFocados,
                      tempo::nivelAtributo(estado_, liquidacao.atributo), estado_.streak, abandonou,
                      tempo::nivelDeXp(estado_.xpTotal) > nivelAntes, false};

        // O evento do log e gravado ao salvar a reflexao (campos humor/distraiu). A economia ja foi liquidada
        // e persistida aqui: a reflexao nunca da credito nem XP.
        const auto agora = tempo::agora(nowMs);
        auto& evento = eventoPendente_;
        evento = tempo::EventoLog{};
        evento.data = agora.valida ? tempo::textoData(agora.data) : std::string{};
        evento.hora = horaTexto(agora);
        evento.tipo = "batalha";
        evento.projeto = projeto_ < 0 ? std::string{"Foco livre"} : titulo;
        evento.categoria = tempo::nomeCategoria(categoriaBatalha_);
        evento.projetoId = projeto_ >= 0 && size_t(projeto_) < lista_.projetos.size() ? lista_.projetos[projeto_].id : std::string{};
        evento.resultado = liquidacao.vitoria ? "vitoria" : "fuga";
        evento.planejadoMin = planejadoMin_;
        evento.focadoMin = entrada.minutosFocados;
        evento.distraidoMin = entrada.minutosDistraidos;
        evento.distracoes = cron_.numDistracoes;
        evento.overtimeMin = liquidacao.minutosOvertime;
        evento.cr = liquidacao.cr;
        evento.xp = liquidacao.xp;
        evento.perfeita = liquidacao.perfeita;
        dataPendente_ = agora.valida ? agora.data : tempo::Data{};
        resultado_.conquista = conquistaNova(filesystem != nullptr ? filesystem : fs_, evento, streakAntes);
        if (tocarDesfecho) {   // no ciclo, concluirRodada escolhe o som (descanso); aqui so batalha unica / "Fim" / "Concluir"
            if (const auto id = som::escolher({liquidacao.vitoria ? som::SomId::Moeda : som::SomId::DesceAlerta,
                                               resultado_.subiuNivel ? som::SomId::PowerUp : som::SomId::Bip,
                                               resultado_.conquista ? som::SomId::Jingle : som::SomId::Bip}))
                pedirSom(*id);
        }

        humor_ = distraiu_ = -1;
        reflexaoInicioMs_ = nowMs;
        overtime_ = false;
        fase_ = Fase::Reflexao;
        if (filesystem != nullptr) TempoStorage::saveState(*filesystem, estado_);
    }

    // Ciclo pomodoro: a rodada terminou (inimigo derrotado). Liquida a batalha sozinha, grava o log sem a reflexao
    // (nao da para pedir resposta no meio do ciclo) e entra na pausa curta ou longa.
    void TempoScreen::concluirRodada(uint32_t nowMs, fs::FS* filesystem) {
        finalizar(nowMs, false, filesystem, false);
        // Fim do foco -> descanso: um som so (a moeda/vitoria ficam na tela); nivel e conquista valem mais.
        if (const auto id = som::escolher({som::SomId::SuaveDescendo,
                                           resultado_.subiuNivel ? som::SomId::PowerUp : som::SomId::Bip,
                                           resultado_.conquista ? som::SomId::Jingle : som::SomId::Bip}))
            pedirSom(*id);
        salvarReflexao(filesystem);
        ciclo_ = tempo::avancarCiclo(ciclo_);   // foco -> pausa
        descansoMs_ = int64_t(tempo::duracaoCicloMin(ciclo_)) * 60000;
        iniciarDescanso(nowMs, false);
    }

    void TempoScreen::salvarReflexao(fs::FS* filesystem) {
        if (fase_ != Fase::Reflexao) return;
        eventoPendente_.humor = humor_ >= 0 ? humor_ + 1 : 0;
        eventoPendente_.distraiu = distraiu_ >= 0 ? distraiu_ + 1 : 0;
        if (filesystem == nullptr) filesystem = fs_;
        if (filesystem != nullptr) {
            TempoStorage::appendLog(*filesystem, dataPendente_, eventoPendente_);
            TempoStorage::recordBattle(*filesystem, eventoPendente_, estado_.streak);   // resumo.json do Perfil
        }
        fase_ = Fase::Nenhuma;
    }

    bool TempoScreen::registrarFocoLeitor(uint32_t nowMs, fs::FS* filesystem, const tempo::RodadaLeitor& rodada,
                                          std::string_view tituloLivro) {
        if (filesystem == nullptr) return false;   // sem SD: nao grava (e nao quebra)
        if (!carregado_) carregar(filesystem, true, nowMs);
        garantirDia(nowMs);
        const auto agora = tempo::agora(nowMs);
        const std::string data = agora.valida ? tempo::textoData(agora.data) : std::string{};
        const bool batalhaAtiva = battleActive() || fase_ == Fase::Reflexao;
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
        if (fase_ == Fase::Descanso) {
            if (int64_t(tempo::ms(nowMs)) - int64_t(descansoInicioMs_) >= descansoMs_) {
                encerrarDescanso(nowMs);
                if (fase_ != Fase::Lutando) pedirSom(som::SomId::Bip);   // fim da pausa sem nova rodada (com rodada: SobeQuatro)
                return true;
            }
            return false;
        }
        if (fase_ == Fase::Reflexao) {
            if (nowMs - reflexaoInicioMs_ >= uint32_t(kAutoSalvarSeg) * 1000U) salvarReflexao(filesystem);
            return false;
        }
        if (fase_ != Fase::Lutando && fase_ != Fase::Pausada) return false;
        acumular(nowMs);
        if (fase_ != Fase::Lutando) return false;
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
        if (overtime_ && overtimeMs() >= plano) {   // teto de +100 % do planejado
            finalizar(nowMs, false, filesystem);
            return true;
        }
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
        if (screen == Screen::TempoVitoria) {
            salvarReflexao(filesystem);
            screen = Screen::TempoHoje;
        } else if (screen == Screen::TempoBatalha) {
            if (fase_ == Fase::Lutando || fase_ == Fase::Pausada) pausarOuRetomar(nowMs);
            else if (fase_ == Fase::Descanso) encerrarDescanso(nowMs);
            else if (fase_ == Fase::Reflexao) screen = Screen::TempoVitoria;
            else screen = Screen::TempoHoje;
        } else if (seletor_) {
            // seletor aberto: BOOT nao faz nada (escolha por toque)
        } else if (battleActive()) {
            screen = Screen::TempoBatalha;
        } else if (fase_ == Fase::Reflexao) {
            screen = Screen::TempoVitoria;
        } else if (!carregado_) {
            carregar(filesystem, true, nowMs);
        } else {
            comecarSessao(nowMs);   // BOOT no Inicio = Comecar
            screen = Screen::TempoBatalha;
        }
    }

    void TempoScreen::back(uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        (void)nowMs;
        if (screen == Screen::TempoHoje) {
            if (seletor_) seletor_ = false;
            else {
                if (carregado_) salvarPreferencias();
                screen = Screen::Home;
            }
        } else {
            if (screen == Screen::TempoVitoria || fase_ == Fase::Reflexao) salvarReflexao(filesystem);   // sem resposta
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
        if (screen != Screen::TempoHoje) seletor_ = false;
        if (screen == Screen::TempoHoje) return seletor_ ? drawSeletor(ui, nowMs, screen) : drawInicio(ui, nowMs, screen);
        if (screen == Screen::TempoBatalha) return drawBatalha(ui, nowMs, screen);
        return drawVitoria(ui, nowMs, screen);
    }

    // ====================================================================== Inicio v3
    // Layout 640x172 (mockup tempo3-foco, quadros 1-2): anel de minutos + -5/+5 a esquerda, cartao unico "FOCO EM" no meio
    // (toque abre o seletor), Comecar grande + ciclo a direita. Com projeto NAO existe seletor de categoria (e unica).
    Action TempoScreen::drawInicio(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ == Fase::Reflexao) { screen = Screen::TempoVitoria; return Action::None; }
        garantirDia(nowMs);
        const bool emBatalha = battleActive();
        const bool ciclo = pomodoro_;
        const bool comProjeto = escolha_ >= 0 && size_t(escolha_) < titulos_.size();
        const tempo::Categoria cat = tempo::categoriaDoFoco(comProjeto, categoria_);

        // ---- arrastar o dedo em volta do anel ajusta 5..60 min em passos de 5 (so sem batalha e sem ciclo)
        if (!emBatalha && !ciclo) {
            if (const auto* t = ui.touch()) {
                const double dx = double(t->x) - kIniCx, dy = double(t->y) - kIniCy;
                const double dist = std::sqrt(dx * dx + dy * dy);
                if (hasTouch(*t, ui::TouchStart)) arrastando_ = dist >= 34 && dist <= 70;
                if (arrastando_ && (hasTouch(*t, ui::TouchStart) || hasTouch(*t, ui::TouchMove) || hasTouch(*t, ui::TouchRelease))
                    && dist > 4) {
                    double f = std::atan2(dx, -dy) / (2 * 3.14159265358979);
                    if (f < 0) f += 1.0;
                    int m = int(std::lround(f * 12));
                    if (m == 0) m = f < 0.5 ? 1 : 12;
                    duracaoMin_ = std::clamp(m, 1, 12) * 5;
                }
                if (hasTouch(*t, ui::TouchCancel) || hasTouch(*t, ui::TouchRelease)) arrastando_ = false;
            }
        } else {
            arrastando_ = false;
        }
        const int minutos = ciclo ? tempo::kCicloFocoMin : duracaoMin_;
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background);
        const uint16_t cLaranja = c565(kLaranja), cPainel = ui.color(SurfaceMuted), cTonal = c565(kTonal);
        const bool podeEditar = !emBatalha;

        // ---- "‹ Início"
        {
            const Rect area{0, 0, 120, 38};
            Bloco voltar(ui, area, Background, 0, 90);
            voltar.apertado = ui.pressed(area);
            voltar.linha(10, 14, 100, "‹ Início", 13, cMudo);
            voltar.desenhar();
            if (ui.firmTap(area)) {
                salvarPreferencias();
                screen = Screen::Home;
                return Action::None;
            }
        }

        // ---- anel + minutos no centro
        {
            Bloco aneis(ui, {0, 40, 172, 132}, Background, 0, 20);
            aneis.linha(kIniCx - 40, 100, 80, std::to_string(minutos), 28, cTexto, TextAlign::Center);
            aneis.linha(kIniCx - 40, 124, 80, "min", 15, cMudo, TextAlign::Center);
            aneis.marca(static_cast<uint32_t>(minutos));
            const Cor fundo = deRole(ui, Background);
            aneis.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                anel(g, dx, dy, kIniCx, kIniCy, kIniRaio, kIniLarg, minutos / 60.0, kLaranja, kAmbar, fundo, kLaranja);
            });
        }

        // ---- -5 / +5
        for (int k = 0; k < 2; ++k) {
            Btn b;
            b.texto = k == 0 ? "+5" : "−5";
            b.tam = 21;
            b.fundo = cTonal;
            b.tinta = ciclo || emBatalha ? rgb565(90, 92, 90) : cTexto;
            b.habilitado = !ciclo && !emBatalha;
            if (botao3(ui, Rect{150, int16_t(k == 0 ? 42 : 106), 56, 56}, b))
                duracaoMin_ = std::clamp(duracaoMin_ + (k == 0 ? 5 : -5), 5, 60);
        }

        // ---- cartao "FOCO EM": nome real do projeto (ate 2 linhas), etiqueta de categoria + cr/min, rodadas e fim
        {
            const Rect cartao{216, 8, 252, 156};
            const std::string nome = comProjeto ? (titulos_[escolha_].empty() ? std::string{"(sem título)"} : titulos_[escolha_])
                                                : tempo::nomeFocoLivre(cat);
            const auto q = quebrar(nome, 222, 21, 17);
            const int rodadas = ciclo ? tempo::kCicloRodadas : 1;
            std::string fim = std::to_string(rodadas) + (rodadas == 1 ? " rodada" : " rodadas");
            if (const auto agora = tempo::agora(nowMs); agora.valida) {
                const int total = rodadas * minutos + (rodadas - 1) * tempo::kCicloPausaMin;
                fim += " · termina " + fmtHoraRelogio(agora.minutoDoDia + total);
            }
            const char* nomeCat = tempo::nomeCategoria(cat);
            const int larguraCat = ui::sans::width(nomeCat, ui::sans::font(15));
            Bloco card(ui, cartao, SurfaceMuted, 16, 42 + comProjeto);
            card.apertado = podeEditar && ui.pressed(cartao);
            card.linha(232, 24, 120, "FOCO EM", 15, cMudo);
            if (podeEditar) card.linha(332, 24, 120, "Trocar ›", 17, cLaranja, TextAlign::Right);
            const int passo = q.tam == 21 ? 25 : 21;
            for (size_t i = 0; i < q.linhas.size(); ++i) card.linha(232, 52 + int(i) * passo, 222, q.linhas[i], q.tam, cTexto);
            if (!comProjeto && q.linhas.size() == 1) card.linha(232, 80, 222, "sem projeto", 15, cMudo);
            card.linha(262, 114, larguraCat + 8, nomeCat, 15, cTexto);
            card.linha(232 + 24 + larguraCat + 26, 114, 120, tempo::textoTaxa(cat), 15, cMudo);
            card.linha(232, 148, 226, fim, 15, cMudo);
            card.marca(uint32_t(cat));
            card.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                g.fillRoundRect(232 + dx, 100 + dy, 24 + larguraCat + 14, 28, 10, cTonal);
                icones::phosphor(g, iconeDaCategoria(cat), 248, 114, 16, cLaranja, cTonal);
            });
            if (ui.firmTap(cartao, podeEditar)) {
                seletor_ = true;
                pagina_ = 0;
                return Action::None;
            }
        }

        // ---- Comecar (ou voltar a batalha em curso) + ciclo
        {
            Btn b;
            b.fundo = cLaranja;
            b.tinta = cBg;
            if (emBatalha) {
                b.texto = "Voltar à batalha";
                b.tam = 17;
                if (botao3(ui, Rect{476, 8, 156, 92}, b)) screen = Screen::TempoBatalha;
            } else {
                b.texto = "Começar";
                b.sub = std::to_string(minutos) + " min";
                b.tam = 21;
                b.comIcone = true;
                b.icone = IconeId::Play;
                if (botao3(ui, Rect{476, 8, 156, 92}, b)) {
                    comecarSessao(nowMs);
                    screen = Screen::TempoBatalha;
                    return Action::None;
                }
            }
            Btn c;
            c.texto = ciclo ? "ciclo 4×" : "ciclo";
            c.tam = 17;
            c.comIcone = true;
            c.icone = IconeId::ArrowsClockwise;
            c.fundo = ciclo ? c565(kSelFundo) : cTonal;
            c.tinta = ciclo ? cLaranja : cMudo;
            c.selecionado = ciclo;
            c.corContorno = cLaranja;
            c.habilitado = !emBatalha;
            if (botao3(ui, Rect{476, 108, 156, 56}, c)) pomodoro_ = !pomodoro_;
        }
        (void)cPainel;
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
        if (fase_ == Fase::Reflexao) { screen = Screen::TempoVitoria; return Action::None; }
        if (fase_ == Fase::Nenhuma) { screen = Screen::TempoHoje; return Action::None; }
        const bool descanso = fase_ == Fase::Descanso;
        const bool pausada = fase_ == Fase::Pausada;
        const bool bonus = overtime_ && !descanso;
        const bool distr = cron_.distraido && !pausada;
        const std::string nome = nomeBatalha_.empty() ? std::string{"Foco livre"} : nomeBatalha_;
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
            voce = 1.0 + std::min(1.0, double(overtimeMs()) / double(planoMs));
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
            aneis.linha(kCx - 40, 82, 80, centro, bonus ? 21 : 26, bonus ? cAzulClaro : cTexto, TextAlign::Center);
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
            const int extra = ciclo_.ativo ? (tempo::kCicloRodadas - ciclo_.rodada) * (tempo::kCicloFocoMin + tempo::kCicloPausaMin) : 0;
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
            texto.linha(176, 140, 292, "cada minuto extra conta · até +" + std::to_string(planejadoMin_) + " min", 15, cMudo);
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

    // Reflexao "COMO FOI?" (substitui a antiga Vitoria). So registra no log; nao rende cr nem XP.
    Action TempoScreen::drawVitoria(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ != Fase::Reflexao) { screen = Screen::TempoHoje; return Action::None; }
        const auto& r = resultado_;
        const uint16_t cLaranja = c565(kLaranja), cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        const uint16_t cFundoBtn = ui.color(SurfaceMuted), cBg = ui.color(Background), cVerde = c565(kVerde);
        static constexpr const char* kHumor[] = {"Péssimo", "Mal", "Ok", "Bem", "Ótimo"};

        Bloco topo(ui, {8, 4, 470, 48}, Background, 0, 80);
        topo.textoCor({16, 8, 300, 18}, "COMO FOI?", 10, cMudo);
        topo.textoCor({16, 24, 460, 26}, r.projeto + " · " + std::to_string(r.focadoMin) + " min", 16, cTexto);
        topo.desenhar();

        for (int i = 0; i < 5; ++i) {
            const Rect caixa{static_cast<int16_t>(16 + i * 92), 56, 84, 52};
            const bool sel = humor_ == i;
            const uint32_t sig = ui::Context::combine(0x9000U + i, sel);
            if (ui.redraw(caixa, sig, true)) {
                const auto rotulo = ui.prepareText({caixa.x, static_cast<int16_t>(caixa.y + 34), caixa.w, 16}, kHumor[i], 11, TextAlign::Center, 1);
                ui.paint(caixa, [&](Arduino_GFX& g, Rect alvo) {
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, cBg);
                    g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 10, cFundoBtn);
                    if (sel) {
                        g.drawRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 10, cLaranja);
                        g.drawRoundRect(alvo.x + 1, alvo.y + 1, alvo.w - 2, alvo.h - 2, 9, cLaranja);
                    }
                    rosto(g, alvo.x + 42, alvo.y + 20, i, sel ? cLaranja : cMudo);
                    ui.drawText(g, rotulo, sel ? cTexto : cMudo, alvo.x - caixa.x, alvo.y - caixa.y, cFundoBtn);
                });
            }
            if (ui.tap(caixa)) humor_ = i;
        }

        ui.label({16, 117, 200, 16}, "Se distraiu?", 11, Muted);
        static constexpr const char* kDistraiu[] = {"Não", "Um pouco", "Muito"};
        static constexpr int kLargDistraiu[] = {100, 100, 100};
        for (int j = 0; j < 3; ++j) {
            const Rect b{static_cast<int16_t>(16 + j * 108), 134, static_cast<int16_t>(kLargDistraiu[j]), 30};
            const bool sel = distraiu_ == j;
            if (botao(ui, b, kDistraiu[j], sel ? (j == 0 ? cVerde : c565(kAmbar)) : cFundoBtn, sel ? cBg : cTexto, 12)) distraiu_ = j;
        }
        const int restante = std::max(0, kAutoSalvarSeg - int((nowMs - reflexaoInicioMs_) / 1000));
        if (botao(ui, {360, 134, 118, 30}, "Salvar · " + std::to_string(restante) + "s", cLaranja, cBg, 13)) {
            salvarReflexao(fs_);
            screen = Screen::TempoHoje;
            return Action::None;
        }

        Bloco saque(ui, {488, 10, 142, 152}, SurfaceMuted, 10, r.liq.perfeita ? 81 : 82);
        saque.textoCor({502, 18, 120, 16}, "SAQUE", 10, cMudo);
        saque.textoCor({502, 36, 120, 24}, fmtSinal(r.liq.cr) + " cr", 18, cLaranja);
        saque.textoCor({502, 62, 124, 18}, fmtSinal(r.liq.xp) + " XP · " + nomeAtributo(r.liq.atributo), 12, cTexto);
        if (r.liq.perfeita) saque.textoCor({502, 92, 124, 16}, "Vitória perfeita!", 11, cVerde);
        else if (!r.liq.vitoria) saque.textoCor({502, 92, 124, 16}, r.abandonou ? "encerrada antes do fim" : "distrações demais", 11, cMudo);
        saque.textoCor({502, 126, 124, 16}, "somado à carteira", 10, cMudo);
        saque.desenhar();
        return Action::None;
    }

} // namespace screens
