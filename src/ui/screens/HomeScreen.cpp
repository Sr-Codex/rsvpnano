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
        constexpr uint16_t kAmbar = rgb565(255, 196, 80);
        constexpr uint16_t kTinta = rgb565(12, 13, 14);   // texto sobre o botao laranja
        constexpr uint16_t kVerdeBat = rgb565(60, 200, 110);
        constexpr uint16_t kVerde = rgb565(25, 158, 112);   // habitos

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

        // Habitos: anel de progresso do dia (verde) com visto no centro.
        void iconeHabitos(Arduino_GFX& g, int cx, int cy, double frac, int raio = 17, int larg = 6, bool marca = true) {
            constexpr double kRad = 3.14159265358979 / 180.0;
            const uint16_t verde = rgb565(25, 158, 112), trilha = rgb565(50, 52, 50);
            const auto ponto = [&](int a, uint16_t c) {
                const double rad = (a - 90) * kRad;
                g.fillCircle(cx + int(std::lround(raio * std::cos(rad))), cy + int(std::lround(raio * std::sin(rad))), larg / 2, c);
            };
            for (int a = 0; a < 360; a += 4) ponto(a, trilha);
            const int ate = int(std::lround(360 * std::clamp(frac, 0.0, 1.0)));
            for (int a = 0; a <= ate; a += 4) ponto(a, verde);
            if (!marca) return;
            const double k = raio / 17.0;
            const auto P = [&](double dx, double dy) { return std::pair<int, int>{cx + int(std::lround(dx * k)), cy + int(std::lround(dy * k))}; };
            const auto a = P(-5, 0), b = P(-1.5, 4), c = P(5.5, -4);
            grossa(g, a.first, a.second, b.first, b.second, verde);
            grossa(g, b.first, b.second, c.first, c.second, verde);
        }

        std::string hora(const tempo::Agora& agora) {
            if (!agora.valida) return "--:--";
            char texto[8];
            std::snprintf(texto, sizeof texto, "%02d:%02d", agora.minutoDoDia / 60, agora.minutoDoDia % 60);
            return texto;
        }

        std::string data(const tempo::Agora& agora) {
            if (!agora.valida) return {};
            static constexpr const char* kSemana[] = {"Seg", "Ter", "Qua", "Qui", "Sex", "Sáb", "Dom"};
            static constexpr const char* kMes[] = {"jan", "fev", "mar", "abr", "mai", "jun",
                                                   "jul", "ago", "set", "out", "nov", "dez"};
            char texto[32];
            std::snprintf(texto, sizeof texto, "%s, %d %s", kSemana[tempo::diaDaSemana(agora.data)], agora.data.dia,
                          kMes[std::clamp(agora.data.mes, 1, 12) - 1]);
            return texto;
        }

        std::string horaMin(int minutos) {
            char texto[16];
            std::snprintf(texto, sizeof texto, "%dh%02d", minutos / 60, minutos % 60);
            return texto;
        }

        // Corta em `maximo` caracteres UTF-8 e acrescenta "..." (sem depender de glifo especial).
        std::string cortar(const std::string& texto, size_t maximo) {
            size_t letras = 0, i = 0;
            while (i < texto.size()) {
                if (letras == maximo) return texto.substr(0, i) + "...";
                ++i;
                while (i < texto.size() && (static_cast<unsigned char>(texto[i]) & 0xC0) == 0x80) ++i;
                ++letras;
            }
            return texto;
        }

        // Wi-Fi: tres arcos + ponto; desconectado = apagado e riscado.
        void iconeWifi(Arduino_GFX& g, int cx, int cy, bool conectado, uint16_t cor, uint16_t apagado, uint16_t risco) {
            constexpr double kRad = 3.14159265358979 / 180.0;
            const uint16_t c = conectado ? cor : apagado;
            for (int r : {5, 9, 13})
                for (int a = 228; a <= 312; a += 3) {
                    const int x = cx + int(std::lround(r * std::cos(a * kRad))), y = cy + int(std::lround(r * std::sin(a * kRad)));
                    g.fillRect(x, y, 2, 2, c);
                }
            g.fillCircle(cx, cy - 1, 2, c);
            if (!conectado) {
                g.drawLine(cx - 8, cy - 10, cx + 8, cy + 2, risco);
                g.drawLine(cx - 8, cy - 9, cx + 8, cy + 3, risco);
            }
        }

        // Bateria desenhada: corpo, ponta, nivel (verde; ambar carregando com raio; carmim abaixo de 15 %).
        void iconeBateria(Arduino_GFX& g, int x, int y, int pct, bool carregando, uint16_t contorno, uint16_t fundo) {
            pct = std::clamp(pct, 0, 100);
            const uint16_t nivel = carregando ? kAmbar : pct < 15 ? rgb565(235, 50, 90) : kVerdeBat;
            g.drawRoundRect(x, y, 26, 14, 3, contorno);
            g.fillRect(x + 26, y + 4, 2, 6, contorno);
            const int w = std::max(pct > 0 ? 2 : 0, 20 * pct / 100);
            if (w > 0) g.fillRect(x + 3, y + 3, w, 8, nivel);
            if (carregando) {   // raio
                g.fillTriangle(x + 15, y + 2, x + 9, y + 8, x + 14, y + 8, fundo);
                g.fillTriangle(x + 12, y + 6, x + 17, y + 6, x + 11, y + 12, fundo);
            }
        }
    } // namespace

    Action HomeScreen::draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                            const HomeInfo& info) {
        const auto agora = tempo::agora(nowMs);
        const auto r = tempo.resumo(filesystem, nowMs);
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        using Estado = ResumoTempo::Estado;

        // ---- barra de status: hora grande, data, Wi-Fi e bateria desenhados
        {
            Bloco barra(ui, {0, 0, 640, 24}, Background, 0, 1);
            barra.texto({14, 1, 60, 22}, hora(agora), 14, cTexto);
            barra.texto({78, 5, 110, 16}, data(agora), 10, cMudo);
            if (info.bateriaPresente) barra.texto({496, 5, 56, 16}, std::to_string(info.bateriaPct) + "%", 10, cMudo, TextAlign::Right);
            barra.marca(uint32_t(info.sincronizando) + uint32_t(info.wifi) * 2U + uint32_t(info.bateriaPresente) * 4U + uint32_t(info.carregando) * 8U
                        + uint32_t(info.bateriaPct) * 16U);
            const uint16_t fundo = ui.color(Background);
            barra.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                if (info.sincronizando) {   // ponto ambar (sync do TickTick em curso), ao lado do Wi-Fi
                    g.fillCircle(466 + dx, 12 + dy, 4, kAmbar);
                    g.drawCircle(466 + dx, 12 + dy, 6, rgb565(110, 84, 36));
                }
                iconeWifi(g, 480 + dx, 19 + dy, info.wifi, cTexto, rgb565(70, 72, 70), rgb565(235, 50, 90));
                if (info.bateriaPresente) iconeBateria(g, 558 + dx, 5 + dy, info.bateriaPct, info.carregando, cTexto, fundo);
                iconeAjustes(g, 618 + dx, 12 + dy, cMudo);   // Ajustes: engrenagem no topo direito (toque abaixo, depois dos blocos)
            });
        }

        // ---- cartao AGORA
        const Rect cartao{10, 26, 290, 138};
        {
            const bool batalha = r.estado == Estado::Batalha;
            const bool habito = r.estado == Estado::Livre && r.habAgora;   // sem batalha: mostra o habito da hora/atrasado
            std::string titulo = habito ? "AGORA · HÁBITO" : "AGORA";
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
            } else if (habito) {
                nome = cortar(r.habNome, 14);
                linha2 = r.habLinha;
                botao = "Abrir hábitos";
            } else {
                nome = r.projeto.empty() ? "Foco livre" : r.projeto;   // foco escolhido no Inicio do Tempo
                linha2 = "inimigo 100% · " + std::to_string(r.planejadoMin) + " min";
                botao = "Começar";
            }
            const std::string leitura = habito ? std::to_string(r.habFeitas) + " de " + std::to_string(r.habTotal) + " hábitos hoje"
                                      : info.temLivro ? "Leitura: " + cortar(info.livro, 16) + " · " + std::to_string(info.livroPct) + "%"
                                                      : std::string{"Nenhum livro aberto"};
            Bloco bloco(ui, cartao, SurfaceMuted, 12, uint32_t(r.estado) + 10);
            bloco.texto({24, 33, 200, 16}, titulo, 10, kLaranja);
            if (r.estado != Estado::Reflexao) bloco.texto({34, 94, 60, 16}, habito ? std::to_string(r.habFeitas) + "/" + std::to_string(r.habTotal) : r.centro, 11, cTexto, TextAlign::Center);
            bloco.texto({114, 50, 180, 24}, nome, 14, cTexto);
            bloco.texto({114, 74, 180, 16}, linha2, 11, cMudo);
            bloco.texto({114, 92, 180, 16}, leitura, 11, cMudo);
            bloco.marca(r.assinatura);
            bloco.marca(ui::Context::signature(botao));
            const Rect areaBotao{114, 124, 172, 30};
            const auto layoutBotao = ui.prepareText(areaBotao, botao, 13, TextAlign::Center, 1);
            bloco.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                g.fillRoundRect(areaBotao.x + dx, areaBotao.y + dy, areaBotao.w, areaBotao.h, 10, kLaranja);
                ui.drawText(g, layoutBotao, kTinta, dx, dy, kLaranja);
                if (habito) {
                    iconeHabitos(g, cartao.x + dx + 54, cartao.y + dy + 75, r.habTotal > 0 ? double(r.habFeitas) / r.habTotal : 0.0, 30, 9, false);
                    return;
                }
                const double voce = batalha ? r.vocePct / 100.0 : 0.0;
                const double inimigo = batalha ? r.inimigoPct / 100.0 : 1.0;
                desenharAneisMini(ui, g, cartao.x + dx + 54, cartao.y + dy + 75, 31, voce, inimigo, r.bonus);
            });
            if (ui.tap(areaBotao)) {
                if (habito) screen = Screen::Habitos;
                else if (r.estado == Estado::Reflexao || batalha) tempo.abrirBatalha(screen);
                else tempo.comecar(screen);   // abre o Inicio com o proximo projeto pre-selecionado
            }
        }

        // ---- 2 x 2 blocos grandes: Tempo, Leitor, Hábitos, Perfil (Ajustes = engrenagem na barra de status)
        static constexpr const char* kNomes[4] = {"Tempo", "Leitor", "Hábitos", "Perfil"};
        for (int i = 0; i < 4; ++i) {
            const Rect rect{int16_t(308 + (i % 2) * 164), int16_t(26 + (i / 2) * 70), 158, 64};
            std::string detalhe;
            uint16_t corDetalhe = cMudo;
            switch (i) {
            case 0:
                detalhe = r.saldo + " cr · " + horaMin(r.focoHojeMin);
                corDetalhe = kLaranja;
                break;
            case 1:
                detalhe = info.temLivro ? cortar(info.livro, 8) + " · " + std::to_string(info.livroPct) + "%" : std::string{"Biblioteca"};
                corDetalhe = kRoxo;
                break;
            case 2:
                if (r.habTotal == 0) {
                    detalhe = "Sem hábitos";
                } else {
                    detalhe = std::to_string(r.habFeitas) + "/" + std::to_string(r.habTotal);
                    if (r.habAtrasado) { detalhe += " · atrasado"; corDetalhe = kLaranja; }
                    else if (r.habProximoMin >= 0) { detalhe += " · próx " + hora({true, {}, r.habProximoMin, 0}); corDetalhe = kVerde; }
                    else { detalhe += " · completo"; corDetalhe = kVerde; }
                }
                break;
            default:
                detalhe = "nível " + std::to_string(r.nivel) + " · " + std::to_string(r.streak) + (r.streak == 1 ? " dia" : " dias");
                corDetalhe = kAmbar;
                break;
            }
            Bloco bloco(ui, rect, SurfaceMuted, 12, 100 + i * 7);
            bloco.texto({int16_t(rect.x + 58), int16_t(rect.y + 10), 96, 22}, kNomes[i], 15, cTexto);
            bloco.texto({int16_t(rect.x + 58), int16_t(rect.y + 36), 98, 16}, detalhe, 11, corDetalhe);
            if (i == 2) bloco.marca(uint32_t(r.habFeitas) * 41U + uint32_t(r.habTotal) * 43U + uint32_t(r.habProximoMin + 1) * 47U + r.habAtrasado * 59U);
            if (i == 0) bloco.marca(uint32_t(r.vocePct) * 131U + uint32_t(r.inimigoPct) * 17U + uint32_t(r.bonus) * 3U + uint32_t(r.estado == Estado::Batalha));
            const int icx = rect.x + 30, icy = rect.y + 32;
            bloco.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                const int X = icx + dx, Y = icy + dy;
                switch (i) {
                case 0: {
                    const bool batalha = r.estado == Estado::Batalha;
                    desenharAneisMini(ui, g, X, Y, 19, batalha ? r.vocePct / 100.0 : 0.62, batalha ? r.inimigoPct / 100.0 : 0.38, r.bonus);
                    break;
                }
                case 1: iconeLivro(g, X, Y, kRoxo); break;
                case 2: iconeHabitos(g, X, Y, r.habTotal > 0 ? double(r.habFeitas) / r.habTotal : 0.0); break;
                default: desenharAnelXp(ui, g, X, Y, 17, 7, r.xpFrac, SurfaceMuted); break;
                }
            });
            if (i == 3) {   // nivel dentro do anel (texto proprio, 2 passes: o Bloco ja foi pintado)
                const Rect numero{int16_t(icx - 14), int16_t(icy - 9), 28, 18};
                const std::string nivel = std::to_string(r.nivel);
                Bloco centro(ui, numero, SurfaceMuted, 2, 200 + uint32_t(r.nivel));
                centro.texto(numero, nivel, 13, cTexto, TextAlign::Center);
                centro.desenhar();
            }
            if (ui.tap(rect)) {
                if (i == 0) screen = Screen::TempoHoje;
                else if (i == 1) screen = Screen::Read;
                else if (i == 2) screen = Screen::Habitos;
                else screen = Screen::Perfil;
            }
        }
        // Ajustes: engrenagem na barra de status (alvo 48 x 40 px). Fica DEPOIS dos blocos: na faixa em que se sobrepoe ao
        // bloco Leitor (y 26..40) o ultimo toque registrado ganha, entao a engrenagem vence.
        if (ui.tap({592, 0, 48, 40})) screen = Screen::Settings;
        return Action::None;
    }

} // namespace screens
