#include "ui/screens/TempoScreen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>

#include "storage/tempo/TempoStorage.h"
#include "tempo/Progressao.h"
#include "tempo/Relogio.h"

namespace screens {
    namespace {
        using ui::Rect;
        using ui::TextAlign;
        using ui::modern::Style;
        using ui::modern::button;
        using namespace ui::themes;

        constexpr int kLinhasVisiveis = 4;
        constexpr int kAlturaLinha = 27;
        constexpr std::array<int, 3> kDuracoes{15, 25, 50};

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

            Bloco(ui::Context& u, Rect a, ColorRole f, int r, uint32_t semente = 0)
                : ui(u), area(a), fundo(f), raio(r), sig(ui::Context::combine(semente, static_cast<uint32_t>(f))) {}

            void texto(Rect r, std::string_view s, uint8_t tamanho, ColorRole papel, TextAlign alinhamento = TextAlign::Start,
                       uint8_t linhas = 1) {
                sig = ui::Context::signature(s, ui::Context::combine(sig, tamanho * 32U + papel));
                itens.push_back({ui.prepareText(r, s, tamanho, alinhamento, linhas), ui.color(papel)});
            }
            void marca(uint32_t valor) { sig = ui::Context::combine(sig, valor); }

            void desenhar(const Desenho& extra = {}) {
                if (!ui.redraw(area, sig, true)) return;
                const uint16_t cor = ui.color(fundo);
                ui.paint(area, [&](Arduino_GFX& g, Rect alvo) {
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, ui.color(Background));
                    if (raio > 0) g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, raio, cor);
                    else g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, cor);
                    if (extra) extra(g, alvo.x - area.x, alvo.y - area.y);
                    for (const auto& item : itens)
                        ui.drawText(g, item.layout, item.cor, alvo.x - area.x, alvo.y - area.y, cor);
                });
            }
        };

        uint16_t verde() { return rgb565(46, 160, 110); }

        void coracao(Arduino_GFX& g, int x, int y, uint16_t cor) {
            g.fillCircle(x + 4, y + 4, 4, cor);
            g.fillCircle(x + 10, y + 4, 4, cor);
            g.fillTriangle(x, y + 6, x + 14, y + 6, x + 7, y + 14, cor);
        }

        void heroi(Arduino_GFX& g, int x, int y, bool vitoria) {  // caixa de ~90x80
            const auto aco = rgb565(128, 136, 152), escuro = rgb565(74, 80, 94), capa = rgb565(240, 130, 50);
            const auto espada = rgb565(236, 206, 120);
            g.fillEllipse(x + 46, y + 76, 38, 4, rgb565(24, 30, 28));
            g.fillRoundRect(x + 50, y + 32, 24, 40, 4, capa);                 // capa/escudo
            g.fillRoundRect(x + 24, y + 36, 36, 36, 5, aco);                  // corpo
            g.fillRect(x + 24, y + 56, 36, 5, escuro);                        // cinto
            g.fillRect(x + 26, y + 70, 12, 6, escuro);                        // botas
            g.fillRect(x + 44, y + 70, 12, 6, escuro);
            g.fillCircle(x + 42, y + 22, 17, aco);                            // elmo
            g.fillRect(x + 30, y + 19, 26, 7, escuro);                        // viseira
            g.fillCircle(x + 42, y + 6, 3, rgb565(220, 70, 60));              // pluma
            if (vitoria) {                                                    // espada erguida
                g.fillRect(x + 14, y + 4, 4, 42, espada);
                g.fillRect(x + 8, y + 46, 16, 4, escuro);
            } else {                                                          // espada em guarda
                g.fillRect(x + 4, y + 52, 28, 4, espada);
                g.fillRect(x + 2, y + 48, 4, 12, escuro);
            }
        }

        void inimigo(Arduino_GFX& g, int x, int y, bool caido, bool curando) {  // caixa de ~96x72
            const auto osso = curando ? rgb565(250, 190, 120) : rgb565(224, 218, 198), olho = rgb565(30, 24, 24);
            const auto roupa = rgb565(150, 50, 62), chifre = rgb565(236, 190, 160);
            if (!caido) {
                g.fillEllipse(x + 48, y + 68, 34, 4, rgb565(24, 30, 28));
                g.fillRoundRect(x + 30, y + 36, 36, 30, 5, roupa);
                g.fillCircle(x + 48, y + 22, 19, osso);
                g.fillRect(x + 38, y + 18, 8, 8, olho);
                g.fillRect(x + 52, y + 18, 8, 8, olho);
                g.fillRect(x + 42, y + 31, 14, 3, olho);
                g.fillTriangle(x + 34, y + 10, x + 28, y - 2, x + 42, y + 6, chifre);
                g.fillTriangle(x + 62, y + 10, x + 68, y - 2, x + 54, y + 6, chifre);
            } else {
                g.fillEllipse(x + 48, y + 66, 40, 4, rgb565(24, 30, 28));
                g.fillRoundRect(x + 34, y + 46, 44, 18, 5, roupa);
                g.fillCircle(x + 24, y + 54, 15, osso);
                g.drawLine(x + 17, y + 49, x + 22, y + 54, olho);   // olhos em X
                g.drawLine(x + 22, y + 49, x + 17, y + 54, olho);
                g.drawLine(x + 26, y + 49, x + 31, y + 54, olho);
                g.drawLine(x + 31, y + 49, x + 26, y + 54, olho);
            }
        }

        // Cabecalho da batalha: titulo + corações. Retorna true se tocou em "voltar".
        void coracoes(ui::Context& ui, Rect area, int meios, int maximoMeios) {
            regiao(ui, area, ui::Context::combine(static_cast<uint32_t>(meios), static_cast<uint32_t>(maximoMeios)),
                   [&](Arduino_GFX& g, int dx, int dy) {
                       const int total = std::max(1, (maximoMeios + 1) / 2);
                       for (int i = 0; i < total; ++i) {
                           const int estado = std::clamp(meios - i * 2, 0, 2);
                           const uint16_t cor = estado == 2 ? rgb565(232, 84, 84)
                                              : estado == 1 ? rgb565(150, 64, 64) : rgb565(66, 59, 53);
                           coracao(g, area.x + dx + i * 18, area.y + dy + 2, cor);
                       }
                   });
        }
    } // namespace

    // ====================================================================== estado
    void TempoScreen::garantirDia(uint32_t nowMs) {
        const auto agora = tempo::agora(nowMs);
        if (!agora.valida) return;
        if (!estado_.dia.aberto || !(estado_.dia.data == agora.data)) tempo::abrirDia(estado_, agora.data);
        tempo::avancarRelogio(estado_, agora.minutoDoDia);
    }

    void TempoScreen::salvar(fs::FS* filesystem) {
        if (filesystem != nullptr) TempoStorage::saveState(*filesystem, estado_);
    }

    void TempoScreen::carregar(fs::FS* filesystem, bool recarregarEstado, uint32_t nowMs) {
        fs_ = filesystem;
        if (recarregarEstado) {
            estado_ = tempo::Estado{};
            if (filesystem != nullptr) TempoStorage::loadState(*filesystem, estado_);
            garantirDia(nowMs);
            carregado_ = true;
            salvar(filesystem);
        }
        lista_ = tempo::ListaProjetos{};
        titulos_.clear();
        if (filesystem != nullptr) TempoStorage::loadProjects(*filesystem, lista_);
        for (const auto& projeto : lista_.projetos) titulos_.push_back(tempo::limparTitulo(projeto.titulo));
        primeiro_ = 0;
    }

    void TempoScreen::acumular(uint32_t nowMs) {
        const uint64_t agora = tempo::ms(nowMs);
        const uint64_t delta = agora > ultimoMs_ ? agora - ultimoMs_ : 0;
        ultimoMs_ = agora;
        if (fase_ != Fase::Lutando) return;
        if (distraido_) {
            distraidoMs_ += delta;
            distracaoAtualMs_ += delta;
        } else {
            focadoMs_ += delta;
        }
    }

    int64_t TempoScreen::restanteMs() const {
        const int64_t plano = int64_t(planejadoMin_) * 60000;
        const int64_t cura = std::min<int64_t>(int64_t(distraidoMs_), plano);
        return std::max<int64_t>(0, plano + cura - int64_t(focadoMs_));
    }

    int TempoScreen::pontosDeVida() const {
        const int64_t plano = int64_t(planejadoMin_) * 60000;
        return static_cast<int>(std::min<int64_t>(100, restanteMs() * 100 / std::max<int64_t>(1, plano)));
    }

    void TempoScreen::abrirPreparo(size_t indice) {
        projeto_ = indice;
        fase_ = Fase::Preparo;
        focadoMs_ = distraidoMs_ = distracaoAtualMs_ = 0;
        distraido_ = false;
        numDistracoes_ = maiorDistracaoMin_ = 0;
    }

    void TempoScreen::iniciar(uint32_t nowMs) {
        if (fase_ != Fase::Preparo) return;
        ultimoMs_ = tempo::ms(nowMs);
        fase_ = Fase::Lutando;
    }

    void TempoScreen::pausarOuRetomar(uint32_t nowMs) {
        acumular(nowMs);
        if (fase_ == Fase::Lutando) fase_ = Fase::Pausada;
        else if (fase_ == Fase::Pausada) { fase_ = Fase::Lutando; ultimoMs_ = tempo::ms(nowMs); }
    }

    void TempoScreen::alternarDistracao(uint32_t nowMs) {
        if (fase_ != Fase::Lutando) return;
        acumular(nowMs);
        if (!distraido_) {
            distraido_ = true;
            distracaoAtualMs_ = 0;
            ++numDistracoes_;
        } else {
            distraido_ = false;
            maiorDistracaoMin_ = std::max(maiorDistracaoMin_, static_cast<int>(distracaoAtualMs_ / 60000));
        }
    }

    void TempoScreen::finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem) {
        acumular(nowMs);
        if (distraido_) {
            distraido_ = false;
            maiorDistracaoMin_ = std::max(maiorDistracaoMin_, static_cast<int>(distracaoAtualMs_ / 60000));
        }
        garantirDia(nowMs);
        tempo::EntradaBatalha entrada;
        entrada.categoria = tempo::Categoria::Projetos;
        entrada.minutosPlanejados = planejadoMin_;
        entrada.minutosFocados = static_cast<int>(focadoMs_ / 60000);
        entrada.minutosDistraidos = static_cast<int>(distraidoMs_ / 60000);
        entrada.numDistracoes = numDistracoes_;
        entrada.maiorDistracaoMin = maiorDistracaoMin_;
        entrada.abandonou = abandonou;
        const auto liquidacao = tempo::aplicarBatalha(estado_, entrada);

        const std::string titulo = projeto_ < titulos_.size() ? titulos_[projeto_] : std::string{};
        resultado_ = {liquidacao, titulo, planejadoMin_, entrada.minutosFocados,
                      tempo::nivelAtributo(estado_, liquidacao.atributo), estado_.streak, abandonou};

        tempo::EventoLog evento;
        const auto agora = tempo::agora(nowMs);
        evento.data = agora.valida ? tempo::textoData(agora.data) : std::string{};
        evento.hora = horaTexto(agora);
        evento.tipo = "batalha";
        evento.projeto = titulo;
        evento.projetoId = projeto_ < lista_.projetos.size() ? lista_.projetos[projeto_].id : std::string{};
        evento.resultado = liquidacao.vitoria ? "vitoria" : "fuga";
        evento.planejadoMin = planejadoMin_;
        evento.focadoMin = entrada.minutosFocados;
        evento.distraidoMin = entrada.minutosDistraidos;
        evento.distracoes = numDistracoes_;
        evento.cr = liquidacao.cr;
        evento.xp = liquidacao.xp;
        fase_ = Fase::Resultado;

        if (filesystem != nullptr) {
            TempoStorage::saveState(*filesystem, estado_);
            TempoStorage::appendLog(*filesystem, agora.valida ? agora.data : tempo::Data{}, evento);
        }
    }

    bool TempoScreen::tick(uint32_t nowMs, fs::FS* filesystem) {
        if (fase_ != Fase::Lutando && fase_ != Fase::Pausada) return false;
        acumular(nowMs);
        if (fase_ == Fase::Lutando && restanteMs() == 0) {
            finalizar(nowMs, false, filesystem);
            return true;
        }
        return false;
    }

    // ===================================================================== entradas
    void TempoScreen::boot(uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        if (screen == Screen::TempoVitoria) {
            fase_ = Fase::Nenhuma;
            screen = Screen::TempoHoje;
        } else if (screen == Screen::TempoBatalha) {
            if (fase_ == Fase::Preparo) iniciar(nowMs);
            else if (fase_ == Fase::Lutando || fase_ == Fase::Pausada) pausarOuRetomar(nowMs);
            else if (fase_ == Fase::Resultado) screen = Screen::TempoVitoria;
            else screen = Screen::TempoHoje;
        } else if (battleActive()) {
            screen = Screen::TempoBatalha;
        } else if (fase_ == Fase::Resultado) {
            screen = Screen::TempoVitoria;
        } else if (!carregado_) {
            carregar(filesystem, true, nowMs);
        } else if (primeiro_ < lista_.projetos.size()) {
            abrirPreparo(primeiro_);
            screen = Screen::TempoBatalha;
        }
    }

    void TempoScreen::back(uint32_t nowMs, Screen& screen, fs::FS*) {
        (void)nowMs;
        if (screen == Screen::TempoHoje) {
            screen = Screen::Read;
        } else {
            if (screen == Screen::TempoVitoria || (screen == Screen::TempoBatalha && fase_ == Fase::Preparo)
                || fase_ == Fase::Resultado)
                fase_ = Fase::Nenhuma;
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
        if (screen == Screen::TempoHoje) return drawHoje(ui, nowMs, screen);
        if (screen == Screen::TempoBatalha) return drawBatalha(ui, nowMs, screen);
        return drawVitoria(ui, nowMs, screen);
    }

    Action TempoScreen::drawHoje(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ == Fase::Resultado) { screen = Screen::TempoVitoria; return Action::None; }
        garantirDia(nowMs);
        const auto agora = tempo::agora(nowMs);

        // cabecalho
        if (button(ui, {8, 0, 94, 43}, "‹ Menu", Style::Plain, true, {}, 13)) { screen = Screen::Read; return Action::None; }
        ui.label({112, 0, 250, 43}, "HOJE · " + dataCabecalho(agora), 13, Muted);
        ui.label({372, 0, 130, 43}, "Foco " + fmtHoraMin(estado_.dia.fochoPagoMin), 13, Foreground, TextAlign::Right);
        ui.label({508, 0, 124, 43}, fmtNum(estado_.saldo) + " cr", 17, Accent, TextAlign::Right);
        ui::modern::rule(ui, {0, 43, 640, 1});

        // cartao AGORA
        const Rect cartao{12, 52, 300, 112};
        Bloco agoraCartao(ui, cartao, SurfaceMuted, 8, battleActive() ? 1 : 2);
        const bool lutando = battleActive();
        agoraCartao.texto({28, 58, 200, 20}, lutando ? (fase_ == Fase::Pausada ? "AGORA · PAUSADA" : "AGORA · LUTANDO")
                                                     : "AGORA", 11, Accent);
        if (lutando) {
            const std::string nome = projeto_ < titulos_.size() ? titulos_[projeto_] : std::string{};
            agoraCartao.texto({28, 78, 200, 24}, nome, 15, Foreground);
            agoraCartao.texto({28, 102, 190, 40}, fmtRelogio((restanteMs() + 999) / 1000), 30, Foreground);
            const int vida = pontosDeVida();
            agoraCartao.marca(static_cast<uint32_t>(vida));
            agoraCartao.texto({28, 146, 200, 16}, "de " + std::to_string(planejadoMin_) + " min · toque para abrir", 11, Muted);
            const bool pausada = fase_ == Fase::Pausada;
            agoraCartao.marca(pausada);
            agoraCartao.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                const int cx = cartao.x + dx + 258, cy = cartao.y + dy + 50;
                g.fillCircle(cx, cy, 26, ui.color(AccentBar));
                const auto tinta = ui.color(OnAccent);
                if (pausada) g.fillTriangle(cx - 9, cy - 13, cx - 9, cy + 13, cx + 13, cy, tinta);
                else { g.fillRect(cx - 10, cy - 12, 7, 24, tinta); g.fillRect(cx + 3, cy - 12, 7, 24, tinta); }
                const int barra = cartao.x + dx + 28, by = cartao.y + dy + 82, bw = 190;  // vida do inimigo
                g.fillRoundRect(barra, by, bw, 6, 3, ui.color(ProgressTrack));
                if (vida > 0) g.fillRoundRect(barra, by, std::max(6, bw * vida / 100), 6, 3, ui.color(AccentBar));
            });
            if (ui.tap(cartao)) { screen = Screen::TempoBatalha; return Action::None; }
        } else {
            agoraCartao.texto({28, 80, 260, 24}, "Nenhuma batalha em curso", 15, Foreground);
            agoraCartao.texto({28, 106, 190, 36}, fmtHoraMin(estado_.dia.fochoPagoMin), 30, Foreground);
            agoraCartao.texto({28, 146, 270, 16}, "foco de hoje · escolha um projeto", 11, Muted);
            agoraCartao.desenhar();
        }

        // lista de projetos
        const size_t total = titulos_.size();
        primeiro_ = std::min(primeiro_, total > kLinhasVisiveis ? total - kLinhasVisiveis : 0);
        ui.label({324, 48, 190, 16}, "PROJETOS DO DIA", 11, Muted);
        if (total > kLinhasVisiveis)
            ui.label({520, 48, 108, 16},
                     std::to_string(primeiro_ + 1) + "–" + std::to_string(std::min<size_t>(total, primeiro_ + kLinhasVisiveis))
                         + " / " + std::to_string(total),
                     11, Muted, TextAlign::Right);
        const int giro = swipe_.update(ui, {324, 64, 304, kLinhasVisiveis * kAlturaLinha});
        if (giro > 0 && primeiro_ + kLinhasVisiveis < total) primeiro_ += 1;
        if (giro < 0 && primeiro_ > 0) primeiro_ -= 1;
        if (total == 0) {
            ui.label({324, 70, 304, 96}, "Sem projetos. Copie projetos.json para a pasta /tempo do cartão SD.", 13, Muted,
                     TextAlign::Start, 3);
            return Action::None;
        }
        for (int i = 0; i < kLinhasVisiveis && primeiro_ + i < total; ++i) {
            const size_t indice = primeiro_ + i;
            const Rect linha{324, static_cast<int16_t>(64 + i * kAlturaLinha), 304, kAlturaLinha};
            const auto& projeto = lista_.projetos[indice];
            Bloco item(ui, linha, Background, 0, 3);
            const std::string nome = titulos_[indice].empty() ? "(sem título)" : titulos_[indice];
            item.texto({static_cast<int16_t>(linha.x + 28), linha.y, 222, linha.h}, nome, 15, Foreground);
            std::string vence;
            if (projeto.vence && projeto.vence->size() >= 10)  // AAAA-MM-DD...
                vence = projeto.vence->substr(8, 2) + "/" + projeto.vence->substr(5, 2);
            item.texto({static_cast<int16_t>(linha.x + 252), linha.y, 52, linha.h}, vence, 11, Muted, TextAlign::Right);
            item.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                g.drawCircle(linha.x + dx + 10, linha.y + dy + linha.h / 2, 8, ui.color(Muted));
                g.drawCircle(linha.x + dx + 10, linha.y + dy + linha.h / 2, 7, ui.color(Muted));
                g.fillRect(linha.x + dx, linha.y + dy + linha.h - 1, linha.w, 1, ui.color(Outline));
            });
            if (ui.tap(linha)) {
                abrirPreparo(indice);
                screen = Screen::TempoBatalha;
                return Action::None;
            }
        }
        return Action::None;
    }

    Action TempoScreen::drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ == Fase::Resultado) { screen = Screen::TempoVitoria; return Action::None; }
        if (fase_ == Fase::Nenhuma) { screen = Screen::TempoHoje; return Action::None; }
        const bool preparo = fase_ == Fase::Preparo;
        const bool pausada = fase_ == Fase::Pausada;
        const std::string nome = projeto_ < titulos_.size() && !titulos_[projeto_].empty() ? titulos_[projeto_] : "Projeto";

        if (button(ui, {4, 0, 104, 28}, "‹ Hoje", Style::Plain, true, {}, 11, TextAlign::Start)) {
            back(nowMs, screen, nullptr);
            return Action::None;
        }
        ui.label({112, 0, 120, 28}, preparo ? "BATALHA · PREPARO" : "BATALHA · FOCO", 11, Muted);
        coracoes(ui, {16, 30, 120, 18}, estado_.coracoesMeios, tempo::coracoesMaxMeios(estado_));

        // personagens (retangulos simples; quadros 3D no passo 4)
        regiao(ui, {12, 46, 124, 80}, 1, [&](Arduino_GFX& g, int dx, int dy) { heroi(g, 28 + dx, 44 + dy, false); });
        const bool curando = distraido_;
        regiao(ui, {250, 48, 130, 76}, 10 + curando, [&](Arduino_GFX& g, int dx, int dy) {
            inimigo(g, 268 + dx, 52 + dy, false, curando);
        });

        // nome + vida do inimigo
        ui.label({236, 4, 206, 24}, nome, 15, Foreground);
        const int vida = preparo ? 100 : pontosDeVida();
        regiao(ui, {236, 30, 206, 16}, 100 + vida, [&](Arduino_GFX& g, int dx, int dy) {
            g.fillRoundRect(236 + dx, 33 + dy, 160, 10, 5, ui.color(ProgressTrack));
            if (vida > 0) g.fillRoundRect(236 + dx, 33 + dy, std::max(10, 160 * vida / 100), 10, 5, rgb565(232, 84, 84));
        });
        ui.label({402, 28, 40, 18}, std::to_string(vida) + "%", 11, Muted);

        // cartao FALTAM
        Bloco cartao(ui, {452, 8, 176, 156}, SurfaceMuted, 8, preparo ? 7 : 8);
        const auto estimativa = tempo::liquidarBatalha(
            estado_, [&] {
                tempo::EntradaBatalha e;
                e.categoria = tempo::Categoria::Projetos;
                e.minutosPlanejados = e.minutosFocados = planejadoMin_;
                return e;
            }());
        cartao.texto({466, 14, 150, 18}, preparo ? "DURAÇÃO" : pausada ? "PAUSADA" : "FALTAM", 11, Muted);
        cartao.texto({466, 34, 150, 44}, preparo ? std::to_string(planejadoMin_) + " min"
                                                 : fmtRelogio((restanteMs() + 999) / 1000), 36, Foreground);
        cartao.texto({466, 82, 150, 16}, preparo ? "toque na duração abaixo"
                                                 : "de " + std::to_string(planejadoMin_) + " min · distrações " + std::to_string(numDistracoes_),
                     11, Muted);
        cartao.texto({466, 102, 150, 16}, "Recompensa", 11, Muted);
        cartao.texto({466, 118, 150, 22}, "+" + fmtNum(estimativa.cr) + " cr · +" + fmtNum(estimativa.xp) + " XP", 15, Accent);
        cartao.texto({466, 142, 156, 16}, curando ? "inimigo se curando!" : "distração = inimigo se cura", 11,
                     curando ? Accent : Muted);
        cartao.desenhar();

        // botoes
        if (preparo) {
            for (size_t i = 0; i < kDuracoes.size(); ++i) {
                const Rect r{static_cast<int16_t>(12 + i * 78), 128, 72, 40};
                if (button(ui, r, std::to_string(kDuracoes[i]), planejadoMin_ == kDuracoes[i] ? Style::Accent : Style::Secondary,
                           true, {}, 15))
                    planejadoMin_ = kDuracoes[i];
            }
            if (button(ui, {252, 128, 188, 40}, "Iniciar batalha", Style::Primary, true, {}, 15)) iniciar(nowMs);
        } else {
            if (button(ui, {12, 128, 132, 40}, pausada ? "Retomar" : "Pausar", Style::Primary, true, {}, 15)) pausarOuRetomar(nowMs);
            if (button(ui, {152, 128, 160, 40}, distraido_ ? "Voltei ao foco" : "Me distraí", distraido_ ? Style::Accent : Style::Secondary,
                       !pausada, {}, 15))
                alternarDistracao(nowMs);
            if (button(ui, {320, 128, 120, 40}, "Encerrar", Style::Secondary, true, {}, 15)) {
                acumular(nowMs);
                if (focadoMs_ < 60000) {   // menos de 1 min de foco: cancela sem log nem recompensa
                    fase_ = Fase::Nenhuma;
                    screen = Screen::TempoHoje;
                } else {
                    finalizar(nowMs, true, fs_);
                    screen = Screen::TempoVitoria;
                }
            }
        }
        return Action::None;
    }

    Action TempoScreen::drawVitoria(ui::Context& ui, uint32_t, Screen& screen) {
        if (fase_ != Fase::Resultado) { screen = Screen::TempoHoje; return Action::None; }
        const auto& r = resultado_;
        const bool vitoria = r.liq.vitoria;
        ui.label({16, 4, 220, 24}, vitoria ? "BATALHA · CONCLUÍDA" : "BATALHA · ENCERRADA", 11, Muted);
        regiao(ui, {12, 30, 110, 100}, vitoria, [&](Arduino_GFX& g, int dx, int dy) { heroi(g, 20 + dx, 34 + dy, vitoria); });
        regiao(ui, {236, 96, 160, 70}, 3, [&](Arduino_GFX& g, int dx, int dy) { inimigo(g, 268 + dx, 92 + dy, true, false); });
        ui.label({186, 30, 260, 40}, vitoria ? "VITÓRIA!" : "FUGA", 30, vitoria ? Accent : Muted, TextAlign::Center);
        ui.label({186, 70, 260, 20}, r.projeto, 13, Foreground, TextAlign::Center);
        ui.label({186, 90, 260, 20}, fmtRelogio(int64_t(r.focadoMin) * 60) + " de foco · plano " + std::to_string(r.planejadoMin) + " min",
                 11, Muted, TextAlign::Center);
        ui.label({186, 110, 260, 20},
                 vitoria ? (r.liq.perfeita ? "Foco perfeito! +25%" : "")
                         : (r.abandonou ? "Você encerrou antes do fim" : "Distrações demais"),
                 11, Accent, TextAlign::Center);

        Bloco saque(ui, {452, 8, 176, 156}, SurfaceMuted, 8, vitoria);
        saque.texto({466, 14, 150, 16}, "SAQUE", 11, Muted);
        saque.texto({466, 36, 80, 22}, "Créditos", 13, Foreground);
        saque.texto({546, 36, 70, 22}, fmtSinal(r.liq.cr), 15, Accent, TextAlign::Right);
        saque.texto({466, 62, 80, 22}, "XP", 13, Foreground);
        saque.texto({546, 62, 70, 22}, fmtSinal(r.liq.xp), 15, Foreground, TextAlign::Right);
        saque.texto({466, 88, 90, 22}, nomeAtributo(r.liq.atributo), 13, Foreground);
        saque.texto({546, 88, 70, 22}, "nível " + std::to_string(r.nivelAtributo), 13, Foreground, TextAlign::Right);
        saque.texto({466, 114, 80, 22}, "Streak", 13, Foreground);
        saque.texto({546, 114, 70, 22}, r.streak > 0 ? std::to_string(r.streak) + " dias" : "–", 13, r.streak > 0 ? Foreground : Muted,
                    TextAlign::Right);
        saque.texto({466, 140, 150, 16}, "toque: voltar ao Hoje", 11, Muted);
        saque.desenhar();
        if (ui.tap({0, 0, 640, 172})) {
            fase_ = Fase::Nenhuma;
            screen = Screen::TempoHoje;
        }
        return Action::None;
    }

} // namespace screens
