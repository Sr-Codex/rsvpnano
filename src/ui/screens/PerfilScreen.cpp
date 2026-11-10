#include "ui/screens/PerfilScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>

#include "storage/tempo/TempoStorage.h"
#include "tempo/FocoLeitor.h"
#include "tempo/Progressao.h"
#include "tempo/Relogio.h"
#include "ui/Rgb565.h"
#include "ui/screens/TempoScreen.h"

namespace screens {
    namespace {
        using ui::Rect;
        using ui::TextAlign;
        using namespace ui::themes;

        constexpr uint16_t kLaranja = rgb565(255, 120, 50);
        constexpr uint16_t kAmbar = rgb565(255, 196, 80);
        constexpr uint16_t kCarmim = rgb565(235, 50, 90);
        constexpr uint16_t kRoxo = rgb565(150, 120, 235);
        constexpr uint16_t kAgua = rgb565(60, 190, 170);
        constexpr uint16_t kVerde = rgb565(25, 158, 112);
        constexpr uint16_t kLinha = rgb565(50, 52, 50);
        constexpr uint16_t kCorChip[tempo::kNumChips] = {rgb565(255, 140, 60), rgb565(150, 120, 235), rgb565(60, 190, 170),
                                                          rgb565(90, 200, 255), rgb565(255, 196, 80)};
        constexpr uint16_t kCorConquista[6] = {kLaranja, kAmbar, kCarmim, kRoxo, kAgua, kVerde};

        using Desenho = std::function<void(Arduino_GFX&, int, int)>;

        // Regiao opaca com textos proprios; so repinta quando a assinatura muda.
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

            Bloco(ui::Context& u, Rect a, ColorRole f, int r, uint32_t semente)
                : ui(u), area(a), fundo(f), raio(r), sig(ui::Context::combine(semente, static_cast<uint32_t>(f))) {}

            struct Q {
                int x, y, w, h;
            };
            void texto(Q q, std::string_view s, uint8_t tamanho, uint16_t cor, TextAlign alinhamento = TextAlign::Start,
                       uint8_t linhas = 1) {
                // A fonte proporcional so e usada se a caixa tiver a altura do corpo; tamanhos < 10 cairiam na fonte de pixels.
                tamanho = std::max<uint8_t>(10, tamanho);
                const int minima = tamanho >= 39 ? 50 : tamanho >= 28 ? 36 : tamanho >= 26 ? 33 : tamanho >= 21 ? 27
                                 : tamanho >= 17 ? 22 : tamanho >= 15 ? 20 : tamanho >= 14 ? 19 : tamanho >= 13 ? 17 : 15;
                const Rect r{int16_t(q.x), int16_t(q.y), int16_t(q.w), int16_t(std::max(q.h, minima * std::max<int>(1, linhas)))};
                sig = ui::Context::signature(s, ui::Context::combine(sig, tamanho * 65536U + cor));
                itens.push_back({ui.prepareText(r, s, tamanho, alinhamento, linhas), cor});
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

        // ------------------------------------------------------------ formatacao
        std::string horaMin(int minutos) {   // "5h15"
            char t[16];
            std::snprintf(t, sizeof t, "%dh%02d", minutos / 60, minutos % 60);
            return t;
        }

        std::string horasDecimal(int minutos) {   // "11,5 h"
            char t[16];
            std::snprintf(t, sizeof t, "%.1f h", minutos / 60.0);
            for (char* c = t; *c; ++c)
                if (*c == '.') *c = ',';
            return t;
        }

        std::string milhar(int64_t v) {   // 1.810
            std::string s = std::to_string(v), saida;
            for (size_t i = 0; i < s.size(); ++i) {
                if (i > 0 && (s.size() - i) % 3 == 0) saida += '.';
                saida += s[i];
            }
            return saida;
        }

        std::string numero(double v) {
            char t[24];
            std::snprintf(t, sizeof t, "%.0f", std::round(v));
            return t;
        }

        // ------------------------------------------------------------ desenho
        void barra(Arduino_GFX& g, int x, int y, int w, double frac, uint16_t cor, int h = 7) {
            g.fillRoundRect(x, y, w, h, h / 2, kLinha);
            if (frac > 0) g.fillRoundRect(x, y, std::max(h, int(w * std::min(frac, 1.0))), h, h / 2, cor);
        }

        // Coracao: estado 0 vazio, 1 meio (metade esquerda), 2 cheio.
        void coracao(Arduino_GFX& g, int x, int y, int estado) {
            const auto inteiro = [&](uint16_t c) {
                g.fillCircle(x + 4, y + 4, 4, c);
                g.fillCircle(x + 10, y + 4, 4, c);
                g.fillTriangle(x, y + 6, x + 14, y + 6, x + 7, y + 14, c);
            };
            inteiro(kLinha);
            if (estado == 2) inteiro(kCarmim);
            else if (estado == 1) {
                g.fillCircle(x + 4, y + 4, 4, kCarmim);
                g.fillTriangle(x, y + 6, x + 7, y + 6, x + 7, y + 14, kCarmim);
                g.fillRect(x + 4, y + 2, 4, 5, kCarmim);
            }
        }

        // Marca de "ganha" (visto) em Ø de ~26 px, tinta escura.
        void visto(Arduino_GFX& g, int cx, int cy, uint16_t cor) {
            for (int k = -1; k <= 1; ++k) {
                g.drawLine(cx - 6, cy + k, cx - 2, cy + 4 + k, cor);
                g.drawLine(cx - 2, cy + 4 + k, cx + 6, cy - 5 + k, cor);
            }
        }

        void pontilhado(Arduino_GFX& g, int x0, int x1, int y, uint16_t cor) {
            for (int x = x0; x < x1; x += 6) g.drawFastHLine(x, y, 3, cor);
        }

        std::string dataTexto(const tempo::Data& d) {
            static constexpr const char* kSemana[] = {"Seg", "Ter", "Qua", "Qui", "Sex", "Sáb", "Dom"};
            static constexpr const char* kMes[] = {"jan", "fev", "mar", "abr", "mai", "jun",
                                                   "jul", "ago", "set", "out", "nov", "dez"};
            char t[32];
            std::snprintf(t, sizeof t, "%s, %d %s", kSemana[tempo::diaDaSemana(d)], d.dia, kMes[std::clamp(d.mes, 1, 12) - 1]);
            return t;
        }
    } // namespace

    // ===================================================================== dados
    void PerfilScreen::carregar(fs::FS* filesystem, uint32_t nowMs) {
        estado_ = tempo::Estado{};
        resumo_ = tempo::Resumo{};
        semana_ = tempo::Semana{};
        hoje_.clear();
        const auto agora = tempo::agora(nowMs);
        if (filesystem != nullptr) TempoStorage::loadState(*filesystem, estado_);
        dataValida_ = agora.valida || estado_.dia.aberto;
        data_ = agora.valida ? agora.data : estado_.dia.data;
        if (filesystem != nullptr && dataValida_) {
            resumo_ = TempoStorage::loadOrRebuildSummary(*filesystem, data_);
            const int64_t segunda = tempo::segundaDaSemana(data_);
            for (int i = 0; i < 7; ++i) {
                std::vector<tempo::EventoLog> eventos;
                const auto dia = tempo::dataDeSerial(segunda + i);
                TempoStorage::loadLogDay(*filesystem, dia, eventos);
                for (const auto& e : eventos) tempo::somarSemana(semana_, e, segunda);
                if (segunda + i == tempo::diaSerial(data_)) hoje_ = std::move(eventos);
            }
        }
        diaSemana_ = dataValida_ ? tempo::diaDaSemana(data_) : 0;
        conquistas_ = tempo::avaliarConquistas(resumo_, estado_.streak);
        ++carga_;
        carregado_ = true;
    }

    void PerfilScreen::mudarPagina(int nova) {
        nova = std::clamp(nova, 0, kPaginas - 1);
        if (nova == pagina_) return;
        pagina_ = nova;
        rolagem_ = 0;
    }

    // ====================================================================== draw
    Action PerfilScreen::draw(ui::Context& ui, uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        const bool entrada = !carregado_ || nowMs - ultimoDesenhoMs_ > 800;
        ultimoDesenhoMs_ = nowMs;
        if (entrada) {
            carregar(filesystem, nowMs);
            pagina_ = 0;
            rolagem_ = 0;
            tracking_ = false;
        }

        // ---- gesto: deslizar na horizontal troca de pagina; na vertical rola as listas
        if (const auto* t = ui.touch()) {
            if (hasTouch(*t, ui::TouchStart)) {
                tracking_ = true;
                x0_ = int16_t(t->x);
                y0_ = int16_t(t->y);
            }
            if (hasTouch(*t, ui::TouchCancel)) tracking_ = false;
            if (tracking_ && hasTouch(*t, ui::TouchRelease)) {
                tracking_ = false;
                const int dx = int(t->x) - x0_, dy = int(t->y) - y0_;
                if (std::abs(dx) >= 40 && std::abs(dx) > std::abs(dy)) {
                    mudarPagina(pagina_ + (dx < 0 ? 1 : -1));
                } else if (std::abs(dy) >= 24 && std::abs(dy) > std::abs(dx)) {
                    rolagem_ = std::max(0, rolagem_ + (dy < 0 ? 1 : -1));   // limitado por pagina ao desenhar
                }
            }
        }

        switch (pagina_) {
        case 0: paginaFicha(ui); break;
        case 1: paginaSemana(ui); break;
        case 2: paginaProjetos(ui); break;
        case 3: paginaConquistas(ui); break;
        default: paginaHoje(ui); break;
        }

        // ---- bolinhas de pagina (tocar numa delas vai para a pagina)
        {
            const Rect area{290, 158, 60, 14};
            Bloco pontos(ui, area, Background, 0, 300 + uint32_t(pagina_));
            pontos.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                for (int i = 0; i < kPaginas; ++i) {
                    const int cx = 320 + int(std::lround((i - (kPaginas - 1) / 2.0) * 11)) + dx;
                    g.fillCircle(cx, 166 + dy, 3, i == pagina_ ? kLaranja : kLinha);
                }
            });
            if (const auto* t = ui.touch(); t != nullptr && hasTouch(*t, ui::TouchTap)
                && t->x >= area.x && t->x < area.x + area.w && t->y >= area.y)
                mudarPagina(int(std::lround((int(t->x) - 320) / 11.0 + (kPaginas - 1) / 2.0)));
        }

        // ---- "‹ Inicio": canto inferior do painel (todas as paginas)
        if (ui.tap({488, 136, 142, 22})) screen = Screen::Home;
        return Action::None;
    }

    // Painel lateral (488,10,142,148): fundo + textos; "‹ Inicio" no rodape.
    namespace {
        struct Painel {
            Bloco b;
            Painel(ui::Context& ui, uint32_t semente) : b(ui, {488, 10, 142, 148}, SurfaceMuted, 10, semente) {
                b.texto({502, 140, 100, 16}, "‹ Início", 10, ui.color(Muted));
            }
        };
    } // namespace

    // -------------------------------------------------------------------- Ficha
    void PerfilScreen::paginaFicha(ui::Context& ui) {
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        const int nivel = tempo::nivelDeXp(estado_.xpTotal);
        const int64_t base = tempo::xpAcumulado(nivel);
        const int proximo = tempo::xpProximo(nivel);
        const double frac = nivel >= 30 ? 1.0 : std::clamp((estado_.xpTotal - double(base)) / std::max(1, proximo), 0.0, 1.0);
        const int maxMeios = tempo::coracoesMaxMeios(estado_);
        const int meios = std::clamp(estado_.coracoesMeios, 0, maxMeios);

        Bloco b(ui, {0, 0, 488, 158}, Background, 0, 10);
        b.marca(carga_);
        b.texto({30, 54, 100, 50}, std::to_string(nivel), 39, cTexto, TextAlign::Center);
        b.texto({30, 100, 100, 16}, "NÍVEL", 9, cMudo, TextAlign::Center);
        b.texto({166, 6, 316, 20}, std::string{"HYDEKEL · "} + tempo::tituloDoNivel(nivel), 13, cTexto);
        b.texto({166, 28, 316, 14},
                nivel >= 30 ? "XP " + milhar(int64_t(estado_.xpTotal)) + " · nível máximo"
                            : "XP " + milhar(int64_t(estado_.xpTotal - double(base))) + " / " + milhar(proximo) + " para o nível "
                                  + std::to_string(nivel + 1),
                10, cMudo);
        const int totalCor = (maxMeios + 1) / 2;
        char vida[24];
        std::snprintf(vida, sizeof vida, "%s/%d vida", meios % 2 ? (std::to_string(meios / 2) + ",5").c_str() : std::to_string(meios / 2).c_str(), totalCor);
        b.texto({166 + totalCor * 18 + 4, 46, 90, 14}, vida, 10, cMudo);

        static constexpr const char* kNomes[tempo::kNumAtributos] = {"Ofício", "Mente", "Corpo", "Energia"};
        const uint16_t cores[tempo::kNumAtributos] = {kLaranja, kRoxo, kVerde, kAgua};
        int niveis[tempo::kNumAtributos];
        for (size_t i = 0; i < tempo::kNumAtributos; ++i) {
            niveis[i] = tempo::nivelAtributo(estado_, static_cast<tempo::Atributo>(i));
            const int y = 70 + int(i) * 22;
            b.texto({166, y, 58, 16}, kNomes[i], 11, cTexto);
            b.texto({434, y, 52, 16}, "nv " + std::to_string(niveis[i]), 10, cMudo);
        }
        b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
            desenharAnelXp(ui, g, 80 + dx, 84 + dy, 62, 16, frac, Background);
            for (int i = 0; i < totalCor; ++i) {
                const int estado = meios >= 2 * (i + 1) ? 2 : meios == 2 * i + 1 ? 1 : 0;
                coracao(g, 166 + i * 18 + dx, 45 + dy, estado);
            }
            for (size_t i = 0; i < tempo::kNumAtributos; ++i)
                barra(g, 226 + dx, 74 + int(i) * 22 + dy, 200, niveis[i] / 20.0, cores[i]);
        });

        Painel p(ui, 11);
        p.b.marca(carga_);
        p.b.texto({502, 18, 120, 14}, "CARTEIRA", 10, cMudo);
        p.b.texto({502, 32, 124, 26}, numero(estado_.saldo) + " cr", 20, kLaranja);
        p.b.texto({502, 62, 120, 14}, "Streak", 10, cMudo);
        p.b.texto({502, 76, 124, 20}, std::to_string(estado_.streak) + (estado_.streak == 1 ? " dia" : " dias"), 15, cTexto);
        p.b.texto({502, 102, 120, 14}, "Vitórias", 10, cMudo);
        p.b.texto({502, 116, 124, 20}, std::to_string(resumo_.vitorias) + " no total", 15, cTexto);
        p.b.desenhar();
    }

    // ------------------------------------------------------------------- Semana
    void PerfilScreen::paginaSemana(ui::Context& ui) {
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        const int meta = std::max(60, estado_.metaSemanalMin);
        Bloco b(ui, {0, 0, 488, 158}, Background, 0, 20);
        b.marca(carga_);
        b.texto({16, 6, 250, 14}, "SEMANA · foco por dia", 10, cMudo);
        b.texto({270, 6, 200, 14}, horaMin(semana_.total) + " · meta " + std::to_string(meta / 60) + "h", 10, cTexto, TextAlign::Right);
        static constexpr const char* kDias[7] = {"S", "T", "Q", "Q", "S", "S", "D"};
        for (int i = 0; i < 7; ++i)
            b.texto({int16_t(30 + i * 62), 143, 34, 14}, kDias[i], 10, i == diaSemana_ ? kLaranja : cMudo, TextAlign::Center);
        const int metaDia = meta / 7;
        const double escala = std::min(0.5, 104.0 / std::max({semana_.maiorDia, metaDia, 60}));
        const int yMeta = 138 - int(std::lround(metaDia * escala));
        b.texto({420, yMeta - 12, 50, 12}, "meta", 8, cMudo, TextAlign::Right);
        b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
            const int base = 138 + dy;
            for (int i = 0; i < 7; ++i) {
                const int x = 30 + i * 62 + dx;
                int y = base;
                for (size_t c = 0; c < tempo::kNumChips; ++c) {
                    const int h = int(std::lround(semana_.min[static_cast<size_t>(i)][c] * escala));
                    if (h <= 0) continue;
                    g.fillRect(x, y - h, 34, h, kCorChip[c]);
                    y -= h + 1;
                }
            }
            pontilhado(g, 20 + dx, 470 + dx, yMeta + dy, rgb565(110, 112, 110));
        });

        Painel p(ui, 21);
        p.b.marca(carga_);
        static constexpr const char* kCats[tempo::kNumChips] = {"Trabalho", "Estudo", "Leitura", "Meditação", "Projetos"};
        for (int i = 0; i < int(tempo::kNumChips); ++i) {
            const int y = 16 + i * 20;
            p.b.texto({518, y, 70, 14}, kCats[i], 10, cTexto);
            p.b.texto({570, y, 52, 14}, semana_.porCategoria[size_t(i)] > 0 ? horaMin(semana_.porCategoria[size_t(i)]) : "–", 10, cMudo,
                      TextAlign::Right);
        }
        if (semana_.perdidoMin > 0) p.b.texto({502, 120, 124, 14}, "perdido: " + horaMin(semana_.perdidoMin), 10, kCarmim);
        p.b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
            for (int i = 0; i < int(tempo::kNumChips); ++i) g.fillRoundRect(502 + dx, 18 + i * 20 + dy, 10, 10, 3, kCorChip[i]);
        });
    }

    // ----------------------------------------------------------------- Projetos
    void PerfilScreen::paginaProjetos(ui::Context& ui) {
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        std::vector<const tempo::ProjetoResumo*> lista;
        for (const auto& p : resumo_.projetos) lista.push_back(&p);
        std::sort(lista.begin(), lista.end(), [](auto* a, auto* b) { return a->minutos > b->minutos; });
        constexpr int kVisiveis = 4;
        const int total = int(lista.size());
        rolagem_ = std::clamp(rolagem_, 0, std::max(0, total - kVisiveis));
        const int maxMin = total > 0 ? std::max(1, lista[0]->minutos) : 1;

        Bloco b(ui, {0, 0, 488, 158}, Background, 0, 30);
        b.marca(carga_ * 64U + uint32_t(rolagem_));
        b.texto({16, 6, 250, 14}, "PROJETOS · inimigos derrotados", 10, cMudo);
        if (total > kVisiveis)
            b.texto({290, 6, 180, 14}, std::to_string(rolagem_ + 1) + "–" + std::to_string(std::min(total, rolagem_ + kVisiveis)) + " de "
                                           + std::to_string(total) + " · arraste", 9, cMudo, TextAlign::Right);
        if (total == 0) b.texto({16, 60, 440, 20}, "Nenhuma batalha de projeto ainda", 12, cMudo);
        for (int i = 0; i < kVisiveis && rolagem_ + i < total; ++i) {
            const auto& p = *lista[size_t(rolagem_ + i)];
            const int y = 26 + i * 33;
            b.texto({16, y, 340, 18}, p.nome, 13, cTexto);
            b.texto({360, y + 3, 70, 16}, horasDecimal(p.minutos), 12, cTexto);
            b.texto({400, y + 3, 70, 14}, std::to_string(p.vitorias) + (p.vitorias == 1 ? " vitória" : " vitórias"), 10, cMudo, TextAlign::Right);
        }
        b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
            for (int i = 0; i < kVisiveis && rolagem_ + i < total; ++i)
                barra(g, 16 + dx, 26 + i * 33 + 19 + dy, 330, double(lista[size_t(rolagem_ + i)]->minutos) / maxMin, kLaranja, 6);
        });

        const int faixa = tempo::faixaMaisFocada(resumo_);
        Painel p(ui, 31);
        p.b.marca(carga_);
        p.b.texto({502, 18, 120, 14}, "FOCO LIVRE", 10, cMudo);
        p.b.texto({502, 32, 124, 24}, horaMin(resumo_.focoLivreMin), 18, cTexto);
        p.b.texto({502, 62, 124, 14}, "Mais focado às", 10, cMudo);
        p.b.texto({502, 76, 124, 20}, faixa >= 0 ? std::to_string(faixa) + "h–" + std::to_string(faixa + 2) + "h" : std::string{"–"}, 15, cTexto);
        p.b.texto({502, 102, 124, 14}, "Média por dia", 10, cMudo);
        p.b.texto({502, 116, 124, 20}, horaMin(tempo::mediaFocoPorDia(resumo_)), 15, cTexto);
        p.b.desenhar();
    }

    // --------------------------------------------------------------- Conquistas
    void PerfilScreen::paginaConquistas(ui::Context& ui) {
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        const int ganhas = tempo::conquistasGanhas(conquistas_);
        const int linhas = (int(tempo::kNumConquistas) + 2) / 3;
        rolagem_ = std::clamp(rolagem_, 0, std::max(0, linhas - 2));

        Bloco b(ui, {0, 0, 488, 158}, Background, 0, 40);
        b.marca(carga_ * 64U + uint32_t(rolagem_));
        b.texto({16, 6, 250, 14}, "CONQUISTAS · " + std::to_string(ganhas) + " de " + std::to_string(tempo::kNumConquistas), 10, cMudo);
        if (linhas > 2) b.texto({290, 6, 180, 14}, "arraste para ver mais", 9, cMudo, TextAlign::Right);
        for (int i = 0; i < int(tempo::kNumConquistas); ++i) {
            const int linha = i / 3 - rolagem_;
            if (linha < 0 || linha > 1) continue;
            const auto& c = conquistas_[size_t(i)];
            const int cx = 16 + (i % 3) * 156, cy = 24 + linha * 66;
            b.texto({cx + 44, cy + 6, 100, 16}, c.nome, 12, c.ganha ? cTexto : cMudo);
            b.texto({cx + 44, cy + 24, 100, 30}, c.desc, 10, cMudo, TextAlign::Start, 2);
            if (!c.ganha) b.texto({cx + 10, cy + 17, 26, 20}, "?", 13, ui.color(Background), TextAlign::Center);
        }
        b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
            const uint16_t painel = ui.color(SurfaceMuted);
            for (int i = 0; i < int(tempo::kNumConquistas); ++i) {
                const int linha = i / 3 - rolagem_;
                if (linha < 0 || linha > 1) continue;
                const auto& c = conquistas_[size_t(i)];
                const int cx = 16 + (i % 3) * 156 + dx, cy = 24 + linha * 66 + dy;
                const uint16_t cor = kCorConquista[c.cor % 6];
                g.fillRoundRect(cx, cy, 148, 60, 10, painel);
                if (c.ganha) {
                    g.drawRoundRect(cx, cy, 148, 60, 10, cor);
                    g.drawRoundRect(cx + 1, cy + 1, 146, 58, 9, cor);
                }
                g.fillCircle(cx + 23, cy + 30, 13, c.ganha ? cor : kLinha);
                if (c.ganha) visto(g, cx + 23, cy + 30, ui.color(Background));
            }
        });

        const int prox = tempo::proximaConquista(conquistas_);
        Painel p(ui, 41);
        p.b.marca(carga_);
        p.b.texto({502, 18, 120, 14}, "PRÓXIMA", 10, cMudo);
        if (prox >= 0) {
            const auto& c = conquistas_[size_t(prox)];
            const uint16_t cor = kCorConquista[c.cor % 6];
            p.b.texto({502, 34, 124, 20}, c.nome, 15, cor);
            p.b.texto({502, 58, 124, 26}, std::to_string(c.atual) + "/" + std::to_string(c.meta) + " " + c.unidade, 10, cTexto, TextAlign::Start, 2);
            p.b.texto({502, 106, 124, 14}, "faltam " + std::to_string(c.meta - c.atual), 10, cMudo);
            p.b.marca(uint32_t(c.atual) * 31U + uint32_t(prox));
            p.b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                barra(g, 502 + dx, 90 + dy, 112, double(c.atual) / c.meta, cor);
            });
        } else {
            p.b.texto({502, 34, 124, 20}, "Todas!", 15, kVerde);
            p.b.desenhar();
        }
    }

    // --------------------------------------------------------------------- Hoje
    void PerfilScreen::paginaHoje(ui::Context& ui) {
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        constexpr int kVisiveis = 6;
        const int total = int(hoje_.size());
        rolagem_ = std::clamp(rolagem_, 0, std::max(0, total - kVisiveis));
        int focoLog = 0;
        double ganhoLog = 0.0;
        for (const auto& e : hoje_) {
            focoLog += e.focadoMin;
            ganhoLog += e.cr;
        }
        const bool diaEstado = estado_.dia.aberto && dataValida_ && estado_.dia.data == data_;
        const int foco = diaEstado ? std::max(estado_.dia.fochoPagoMin, 0) : focoLog;
        const double ganhos = diaEstado ? estado_.dia.crGanho : ganhoLog;
        const double gastos = diaEstado ? estado_.dia.crGasto : 0.0;

        Bloco b(ui, {0, 0, 488, 158}, Background, 0, 50);
        b.marca(carga_ * 64U + uint32_t(rolagem_));
        b.texto({16, 6, 40, 12}, "HORA", 8, cMudo);
        b.texto({64, 6, 150, 12}, "ATIVIDADE", 8, cMudo);
        b.texto({346, 6, 56, 12}, "TEMPO", 8, cMudo, TextAlign::Right);
        b.texto({420, 6, 50, 12}, "CR", 8, cMudo, TextAlign::Right);
        if (total == 0) b.texto({16, 62, 440, 20}, "Nenhuma batalha hoje ainda", 12, cMudo, TextAlign::Center);
        for (int i = 0; i < kVisiveis && rolagem_ + i < total; ++i) {
            const auto& e = hoje_[size_t(rolagem_ + i)];
            const int y = 24 + i * 22;
            std::string atividade = e.origem == tempo::kOrigemLeitor ? "Leitura · " + (e.projeto.empty() ? std::string{"livro"} : e.projeto)
                                  : e.projetoId.empty() ? "Foco livre" + (e.categoria.empty() ? std::string{} : " · " + e.categoria) : e.projeto;
            b.texto({16, y + 2, 44, 14}, e.hora.size() >= 5 ? e.hora.substr(0, 5) : std::string{"--:--"}, 10, cMudo);
            b.texto({64, y, 270, 18}, atividade, 12, cTexto);
            b.texto({340, y + 2, 62, 14}, e.focadoMin >= 60 ? horaMin(e.focadoMin) : std::to_string(e.focadoMin) + " min", 10, cMudo, TextAlign::Right);
            const uint16_t corCr = e.cr > 0 ? kVerde : e.cr < 0 ? kCarmim : cMudo;
            b.texto({410, y + 2, 60, 14}, (e.cr > 0 ? "+" : "") + numero(e.cr), 10, corCr, TextAlign::Right);
        }
        if (total > kVisiveis)
            b.texto({290, 146, 180, 12}, std::to_string(rolagem_ + 1) + "–" + std::to_string(std::min(total, rolagem_ + kVisiveis)) + " de "
                                             + std::to_string(total) + " · arraste", 8, cMudo, TextAlign::Right);
        b.desenhar([&](Arduino_GFX& g, int dx, int dy) { pontilhado(g, 16 + dx, 470 + dx, 20 + dy, kLinha); });

        Painel p(ui, 51);
        p.b.marca(carga_);
        p.b.texto({502, 10, 124, 16}, "FOCO", 9, cMudo);
        p.b.texto({502, 23, 124, 20}, horaMin(foco), 15, cTexto);
        p.b.texto({502, 40, 124, 16}, "CRÉDITOS GANHOS", 9, cMudo);
        p.b.texto({502, 53, 124, 20}, "+" + numero(ganhos), 15, kVerde);
        p.b.texto({502, 70, 124, 16}, "GASTOS", 9, cMudo);
        p.b.texto({502, 83, 124, 20}, (gastos > 0 ? "-" : "") + numero(gastos), 15, gastos > 0 ? kCarmim : cMudo);
        p.b.texto({502, 100, 124, 16}, "SALDO TOTAL", 9, cMudo);
        p.b.texto({502, 113, 124, 20}, numero(estado_.saldo) + " cr", 15, kLaranja);
        p.b.desenhar();
    }

} // namespace screens
