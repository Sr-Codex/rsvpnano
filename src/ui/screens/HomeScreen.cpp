#include "ui/screens/HomeScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "tempo/Relogio.h"
#include "ui/Rgb565.h"

namespace screens {
    namespace {
        using ui::Rect;
        using ui::TextAlign;
        using namespace ui::themes;

        constexpr uint16_t kLaranja = rgb565(255, 120, 50);
        constexpr uint16_t kRoxo = rgb565(150, 120, 235);
        constexpr uint16_t kVerde = rgb565(25, 158, 112);
        constexpr uint16_t kAmbar = rgb565(255, 196, 80);
        constexpr uint16_t kTinta = rgb565(12, 13, 14);   // texto sobre o botao laranja

        using Desenho = std::function<void(Arduino_GFX&, int, int)>;   // (gfx, dx, dy) somados as coords absolutas

        // Regiao opaca com fundo uniforme e textos proprios; so repinta quando a assinatura muda.
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

            Bloco(ui::Context& u, Rect a, ColorRole f, int r, uint32_t semente = 0)
                : ui(u), area(a), fundo(f), raio(r), sig(ui::Context::combine(semente, static_cast<uint32_t>(f))) {}

            void texto(Rect r, std::string_view s, uint8_t tamanho, uint16_t cor, TextAlign alinhamento = TextAlign::Start) {
                sig = ui::Context::signature(s, ui::Context::combine(sig, tamanho * 65536U + cor));
                itens.push_back({ui.prepareText(r, s, tamanho, alinhamento, 1), cor});
            }
            void marca(uint32_t valor) { sig = ui::Context::combine(sig, valor); }

            void desenhar(const Desenho& extra = {}) {
                if (!ui.redraw(area, sig, true)) return;
                const uint16_t cor = ui.color(fundo);
                ui.paint(area, [&](Arduino_GFX& g, Rect alvo) {
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, ui.color(Background));
                    if (raio > 0) g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, raio, cor);
                    if (extra) extra(g, alvo.x - area.x, alvo.y - area.y);
                    for (const auto& item : itens) ui.drawText(g, item.layout, item.cor, alvo.x - area.x, alvo.y - area.y, cor);
                });
            }
        };

        // ------------------------------------------------------------------ icones (primitivas)
        void grossa(Arduino_GFX& g, int x1, int y1, int x2, int y2, uint16_t cor, bool larga = true) {
            g.drawLine(x1, y1, x2, y2, cor);
            if (!larga) return;
            if (std::abs(x2 - x1) >= std::abs(y2 - y1)) g.drawLine(x1, y1 + 1, x2, y2 + 1, cor);
            else g.drawLine(x1 + 1, y1, x2 + 1, y2, cor);
        }

        void arco(Arduino_GFX& g, int cx, int cy, double rx, double ry, int a0, int a1, uint16_t cor) {
            constexpr double kRad = 3.14159265358979 / 180.0;
            for (int a = a0; a <= a1; a += 8)
                g.fillCircle(cx + int(std::lround(rx * std::cos(a * kRad))), cy + int(std::lround(ry * std::sin(a * kRad))), 1, cor);
        }

        void iconeLivro(Arduino_GFX& g, int cx, int cy, uint16_t c) {
            for (int lado : {-1, 1}) {
                const int xo = cx + lado * 11, y0 = cy - 7, y1 = cy + 6;
                grossa(g, xo, y0, cx, cy - 5, c);
                grossa(g, cx, cy - 5, cx, cy + 8, c);
                grossa(g, cx, cy + 8, xo, y1, c);
                grossa(g, xo, y1, xo, y0, c);
            }
        }

        void iconeHabitos(Arduino_GFX& g, int cx, int cy, uint16_t c) {
            for (int i = 0; i < 3; ++i) {
                const int y = cy - 7 + i * 7;
                g.drawCircle(cx - 9, y, 3, c);
                g.drawLine(cx - 3, y, cx + 10, y, c);
                g.drawLine(cx - 3, y + 1, cx + 10, y + 1, c);
            }
            grossa(g, cx - 10, cy - 7, cx - 8, cy - 5, kVerde);
            grossa(g, cx - 8, cy - 5, cx - 5, cy - 10, kVerde);
        }

        void iconeHumor(Arduino_GFX& g, int cx, int cy, uint16_t c) {
            g.drawCircle(cx, cy, 10, c);
            g.drawCircle(cx, cy, 9, c);
            g.fillCircle(cx - 3, cy - 4, 1, c);
            g.fillCircle(cx + 3, cy - 4, 1, c);
            arco(g, cx, cy, 4.5, 4.5, 25, 155, c);
        }

        void iconeRecibo(Arduino_GFX& g, int cx, int cy, uint16_t c) {
            g.drawRect(cx - 8, cy - 10, 16, 19, c);
            g.drawRect(cx - 7, cy - 9, 14, 17, c);
            for (int i = 0; i < 3; ++i) g.drawLine(cx - 4, cy - 5 + i * 5, cx + 4, cy - 5 + i * 5, c);
        }

        void iconeAjustes(Arduino_GFX& g, int cx, int cy, uint16_t c) {
            constexpr double kRad = 3.14159265358979 / 180.0;
            for (int i = 0; i < 8; ++i) {
                const double a = i * 45 * kRad;
                grossa(g, cx + int(std::lround(7 * std::cos(a))), cy + int(std::lround(7 * std::sin(a))),
                       cx + int(std::lround(11.5 * std::cos(a))), cy + int(std::lround(11.5 * std::sin(a))), c);
            }
            g.drawCircle(cx, cy, 9, c);
            g.drawCircle(cx, cy, 8, c);
            g.fillCircle(cx, cy, 3, c);
        }

        std::string dataHora(const tempo::Agora& agora) {
            if (!agora.valida) return {};
            static constexpr const char* kSemana[] = {"Seg", "Ter", "Qua", "Qui", "Sex", "Sáb", "Dom"};
            static constexpr const char* kMes[] = {"jan", "fev", "mar", "abr", "mai", "jun",
                                                   "jul", "ago", "set", "out", "nov", "dez"};
            char texto[48];
            std::snprintf(texto, sizeof texto, "%02d:%02d · %s, %d %s", agora.minutoDoDia / 60, agora.minutoDoDia % 60,
                          kSemana[tempo::diaDaSemana(agora.data)], agora.data.dia, kMes[std::clamp(agora.data.mes, 1, 12) - 1]);
            return texto;
        }
    } // namespace

    Action HomeScreen::draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                            const HomeInfo& info) {
        const auto agora = tempo::agora(nowMs);
        const auto r = tempo.resumo(filesystem, nowMs);
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        using Estado = ResumoTempo::Estado;

        // ---- barra de status
        {
            std::string direita = info.wifi ? "Wi-Fi" : "Wi-Fi off";
            if (info.bateriaPresente)
                direita += " · " + std::to_string(info.bateriaPct) + "%" + (info.carregando ? " · carregando" : "");
            Bloco barra(ui, {0, 0, 640, 22}, Background, 0, 1);
            barra.texto({12, 2, 300, 18}, dataHora(agora), 11, cMudo);
            barra.texto({332, 2, 296, 18}, direita, 11, cMudo, TextAlign::Right);
            barra.desenhar();
        }

        // ---- cartao AGORA
        const Rect cartao{12, 24, 318, 140};
        {
            const bool batalha = r.estado == Estado::Batalha;
            std::string titulo = "AGORA";
            if (batalha) titulo += r.descanso ? " · PAUSA" : r.pausada ? " · PAUSADA" : r.bonus ? " · BÔNUS" : "";
            std::string nome, linha2, botao;
            if (r.estado == Estado::Reflexao) {
                nome = "Reflexão pendente";
                linha2 = "responda para salvar a batalha";
                botao = "Responder";
            } else if (batalha) {
                nome = r.descanso ? "Pausa" : r.projeto.empty() ? "Batalha" : r.projeto;
                linha2 = "você " + std::to_string(r.vocePct) + "% · inimigo " + std::to_string(r.inimigoPct) + "%";
                botao = "Voltar à batalha";
            } else {
                nome = r.projeto.empty() ? "Foco livre" : r.projeto;   // foco escolhido no Inicio do Tempo
                linha2 = "inimigo 100% · " + std::to_string(r.planejadoMin) + " min";
                botao = "Começar";
            }
            const std::string leitura = info.temLivro ? "Leitura: " + info.livro + " · " + std::to_string(info.livroPct) + "%"
                                                      : std::string{"Nenhum livro aberto"};
            Bloco bloco(ui, cartao, SurfaceMuted, 12, uint32_t(r.estado) + 10);
            bloco.texto({26, 30, 200, 18}, titulo, 11, kLaranja);
            if (r.estado != Estado::Reflexao) bloco.texto({40, 91, 60, 18}, r.centro, 11, cTexto, TextAlign::Center);
            bloco.texto({124, 48, 196, 24}, nome, 14, cTexto);
            bloco.texto({124, 76, 196, 18}, linha2, 11, cMudo);
            bloco.texto({124, 100, 196, 18}, leitura, 11, cMudo);
            bloco.marca(r.assinatura);
            bloco.marca(ui::Context::signature(botao));
            const Rect areaBotao{124, 126, 192, 30};
            const auto layoutBotao = ui.prepareText(areaBotao, botao, 13, TextAlign::Center, 1);
            bloco.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                g.fillRoundRect(areaBotao.x + dx, areaBotao.y + dy, areaBotao.w, areaBotao.h, 10, kLaranja);
                ui.drawText(g, layoutBotao, kTinta, dx, dy, kLaranja);
                const double voce = batalha ? r.vocePct / 100.0 : 0.0;
                const double inimigo = batalha ? r.inimigoPct / 100.0 : 1.0;
                desenharAneisMini(ui, g, cartao.x + dx + 58, cartao.y + dy + 76, 38, voce, inimigo, r.bonus);
            });
            if (ui.tap(areaBotao)) {
                if (r.estado == Estado::Reflexao || batalha) tempo.abrirBatalha(screen);
                else tempo.comecar(screen);   // abre o Inicio com o proximo projeto pre-selecionado
            }
        }

        // ---- grade de apps (3 x 2)
        static constexpr const char* kNomes[6] = {"Tempo", "Leitor", "Hábitos", "Humor", "Recibo", "Ajustes"};
        if (breveApp_ >= 0 && nowMs >= breveAteMs_) breveApp_ = -1;
        for (int i = 0; i < 6; ++i) {
            const int cx = 372 + (i % 3) * 96, cy = 58 + (i / 3) * 66;
            const Rect rect{int16_t(cx - 42), int16_t(cy - 30), 84, 60};
            const bool breve = breveApp_ == i;
            std::string infoCanto;
            if (i == 0) infoCanto = r.saldo + " cr";
            else if (i == 1 && info.temLivro) infoCanto = std::to_string(info.livroPct) + "%";
            Bloco cartaoApp(ui, rect, SurfaceMuted, 12, 100 + i * 7 + breve);
            cartaoApp.texto({int16_t(cx - 42), int16_t(cy + 5), 84, 18}, breve ? "em breve" : kNomes[i], 11, breve ? kAmbar : cTexto,
                            TextAlign::Center);
            if (!infoCanto.empty()) cartaoApp.texto({int16_t(cx), int16_t(cy - 28), 40, 13}, infoCanto, 9, cMudo, TextAlign::Right);
            if (i == 0) cartaoApp.marca(r.assinatura);
            cartaoApp.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                const int X = rect.x + dx + 42, Y = rect.y + dy + 22;
                switch (i) {
                case 0: {
                    const bool batalha = r.estado == Estado::Batalha;
                    desenharAneisMini(ui, g, X - 14, Y + 1, 12, batalha ? r.vocePct / 100.0 : 0.62, batalha ? r.inimigoPct / 100.0 : 0.38, r.bonus);
                    break;
                }
                case 1: iconeLivro(g, X, Y, kRoxo); break;
                case 2: iconeHabitos(g, X, Y, ui.color(Foreground)); break;
                case 3: iconeHumor(g, X, Y, kAmbar); break;
                case 4: iconeRecibo(g, X, Y, ui.color(Foreground)); break;
                default: iconeAjustes(g, X, Y, ui.color(Muted)); break;
                }
            });
            if (ui.tap(rect)) {
                if (i == 0) screen = Screen::TempoHoje;
                else if (i == 1) screen = Screen::Read;
                else if (i == 5) screen = Screen::Settings;
                else { breveApp_ = i; breveAteMs_ = nowMs + 2000; }
            }
        }
        return Action::None;
    }

} // namespace screens
