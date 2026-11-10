#include "ui/screens/TempoScreen.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <functional>

#include "storage/tempo/TempoStorage.h"
#include "tempo/Parametros.h"
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
            void textoCor(Rect r, std::string_view s, uint8_t tamanho, uint16_t cor, TextAlign alinhamento = TextAlign::Start) {
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
                    else g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, cor);
                    if (extra) extra(g, alvo.x - area.x, alvo.y - area.y);
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
        constexpr int kCx = 86, kCy = 86;      // centro dos aneis
        constexpr int kRaioFora = 70, kRaioDentro = 48, kEspessura = 18;
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
                  Cor fundo, Cor baseTrilho) {
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
            const auto seg = [&](double a0, double a1, double k0, double k1) {
                const int n = std::max(2, int(std::ceil((a1 - a0) / 4.0)));
                for (int i = 0; i < n; ++i) {
                    const double aa = a0 + (a1 - a0) * i / n;
                    const double bb = a0 + (a1 - a0) * (i + 1) / n + 0.8;
                    const uint16_t cor = c565(mistura(c0, c1, k0 + (k1 - k0) * (i + 0.5) / n));
                    int x1, y1, x2, y2, x3, y3, x4, y4;
                    ponto(aa, ro, x1, y1); ponto(bb, ro, x2, y2); ponto(bb, ri, x3, y3); ponto(aa, ri, x4, y4);
                    g.fillTriangle(x1, y1, x2, y2, x3, y3, cor);
                    g.fillTriangle(x1, y1, x3, y3, x4, y4, cor);
                }
            };
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
            r.vocePct = r.descanso ? 100 : int(std::min<int64_t>(100, int64_t(focadoMs_) * 100 / plano));
            if (r.descanso) r.centro = "pausa";
            else r.centro = r.bonus ? "+" + fmtRelogio(overtimeMs() / 1000) : fmtRelogio((restanteMs() + 999) / 1000);
        } else {
            r.projeto = escolha_ >= 0 && size_t(escolha_) < titulos_.size() ? titulos_[escolha_]
                                                                           : tempo::nomeFocoLivre(categoria_);
            r.centro = fmtRelogio(int64_t(ciclo_.ativo || pomodoro_ ? tempo::kCicloFocoMin : duracaoMin_) * 60);
            r.planejadoMin = pomodoro_ ? tempo::kCicloFocoMin : duracaoMin_;
        }
        r.assinatura = ui::Context::signature(r.projeto + "|" + r.centro + "|" + r.saldo,
                                              uint32_t(r.estado) * 1000003U + r.pausada * 7U + r.descanso * 11U + r.bonus * 13U
                                                  + uint32_t(r.vocePct) * 131U + uint32_t(r.inimigoPct) * 17U + r.temProjetos
                                                  + uint32_t(r.nivel) * 977U + uint32_t(r.xpFrac * 100) * 31U + uint32_t(r.streak) * 53U
                                                  + uint32_t(r.focoHojeMin) * 7U);
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
        if (indice >= 0 && size_t(indice) < lista_.projetos.size()) {
            escolha_ = indice;
            categoria_ = tempo::Categoria::Projetos;   // com projeto, a categoria vai para Projetos
        } else {
            escolha_ = -1;
        }
    }

    void TempoScreen::salvarPreferencias() {
        estado_.ultimaCategoria = static_cast<int>(categoria_);
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
            categoria_ = estado_.ultimaCategoria >= 0 && estado_.ultimaCategoria < int(tempo::kNumCategorias)
                             ? static_cast<tempo::Categoria>(estado_.ultimaCategoria) : tempo::Categoria::Trabalho;
            duracaoMin_ = std::clamp(estado_.ultimaDuracaoMin / 5 * 5, 5, 60);
            pomodoro_ = estado_.ciclopomodoro;
            salvar(filesystem);
        }
        lista_ = tempo::ListaProjetos{};
        titulos_.clear();
        if (filesystem != nullptr) TempoStorage::loadProjects(*filesystem, lista_);
        for (const auto& projeto : lista_.projetos) titulos_.push_back(tempo::limparTitulo(projeto.titulo));
        primeiro_ = 0;
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
        if (fase_ != Fase::Lutando) return;
        if (distraido_) {
            distraidoMs_ += delta;
            distracaoAtualMs_ += delta;
        } else {
            focadoMs_ += delta;
            comboMs_ += delta;
        }
    }

    int64_t TempoScreen::restanteMs() const {
        const int64_t plano = int64_t(planejadoMin_) * 60000;
        const int64_t cura = std::min<int64_t>(int64_t(distraidoMs_), plano);
        return std::max<int64_t>(0, plano + cura - int64_t(focadoMs_));
    }

    int64_t TempoScreen::overtimeMs() const {
        const int64_t plano = int64_t(planejadoMin_) * 60000;
        const int64_t cura = std::min<int64_t>(int64_t(distraidoMs_), plano);
        return std::max<int64_t>(0, int64_t(focadoMs_) - (plano + cura));
    }

    int TempoScreen::pontosDeVida() const {
        const int64_t plano = int64_t(planejadoMin_) * 60000;
        return static_cast<int>(std::min<int64_t>(100, restanteMs() * 100 / std::max<int64_t>(1, plano)));
    }

    // Estimativa do motor: foco = "ao terminar" (zera o inimigo no plano + cura); overtime = "ate agora".
    tempo::LiquidacaoBatalha TempoScreen::estimar() const {
        tempo::EntradaBatalha e;
        e.categoria = categoriaBatalha_;
        e.minutosPlanejados = planejadoMin_;
        e.minutosDistraidos = static_cast<int>(distraidoMs_ / 60000);
        e.numDistracoes = numDistracoes_;
        e.maiorDistracaoMin = std::max(maiorDistracaoMin_, static_cast<int>(distracaoAtualMs_ / 60000));
        if (overtime_) {
            e.minutosFocados = static_cast<int>(focadoMs_ / 60000);
        } else {
            e.minutosFocados = planejadoMin_ + std::min(e.minutosDistraidos, planejadoMin_);
        }
        return tempo::liquidarBatalha(estado_, e);
    }

    // Comeca a batalha (ou o ciclo pomodoro) com a escolha do Inicio.
    void TempoScreen::comecarSessao(uint32_t nowMs) {
        if (battleActive() || fase_ == Fase::Reflexao) return;
        if (escolha_ >= 0 && size_t(escolha_) < titulos_.size()) {
            projeto_ = escolha_;
            categoriaBatalha_ = categoria_;
            nomeBatalha_ = titulos_[escolha_].empty() ? std::string{"Projeto"} : titulos_[escolha_];
        } else {
            projeto_ = -1;
            categoriaBatalha_ = categoria_;
            nomeBatalha_ = tempo::nomeFocoLivre(categoriaBatalha_);
        }
        ciclo_ = pomodoro_ ? tempo::iniciarCiclo() : tempo::Ciclo{};
        planejadoMin_ = pomodoro_ ? tempo::kCicloFocoMin : duracaoMin_;
        salvarPreferencias();
        ligarBatalha(nowMs);
    }

    void TempoScreen::ligarBatalha(uint32_t nowMs) {
        focadoMs_ = distraidoMs_ = distracaoAtualMs_ = comboMs_ = 0;
        distraido_ = false;
        overtime_ = false;
        arrastando_ = false;
        numDistracoes_ = maiorDistracaoMin_ = 0;
        ultimoBipeFocoMin_ = 0;
        avisoFimDado_ = false;
        ultimoMs_ = tempo::ms(nowMs);
        fase_ = Fase::Lutando;
    }

    void TempoScreen::pausarOuRetomar(uint32_t nowMs) {
        acumular(nowMs);
        if (fase_ == Fase::Lutando) fase_ = Fase::Pausada;
        else if (fase_ == Fase::Pausada) { fase_ = Fase::Lutando; ultimoMs_ = tempo::ms(nowMs); }
    }

    void TempoScreen::alternarDistracao(uint32_t nowMs) {
        if (fase_ != Fase::Lutando || overtime_) return;
        acumular(nowMs);
        if (!distraido_) {
            distraido_ = true;
            distracaoAtualMs_ = 0;
            comboMs_ = 0;
            ++numDistracoes_;
        } else {
            distraido_ = false;
            maiorDistracaoMin_ = std::max(maiorDistracaoMin_, static_cast<int>(distracaoAtualMs_ / 60000));
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

    void TempoScreen::finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem) {
        acumular(nowMs);
        if (distraido_) {
            distraido_ = false;
            maiorDistracaoMin_ = std::max(maiorDistracaoMin_, static_cast<int>(distracaoAtualMs_ / 60000));
        }
        garantirDia(nowMs);
        tempo::EntradaBatalha entrada;
        entrada.categoria = categoriaBatalha_;
        entrada.minutosPlanejados = planejadoMin_;
        entrada.minutosFocados = static_cast<int>(focadoMs_ / 60000);
        entrada.minutosDistraidos = static_cast<int>(distraidoMs_ / 60000);
        entrada.numDistracoes = numDistracoes_;
        entrada.maiorDistracaoMin = maiorDistracaoMin_;
        entrada.abandonou = abandonou;
        const auto liquidacao = tempo::aplicarBatalha(estado_, entrada);

        const std::string titulo = nomeBatalha_;
        resultado_ = {liquidacao, titulo, planejadoMin_, entrada.minutosFocados,
                      tempo::nivelAtributo(estado_, liquidacao.atributo), estado_.streak, abandonou};

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
        evento.distracoes = numDistracoes_;
        evento.overtimeMin = liquidacao.minutosOvertime;
        evento.cr = liquidacao.cr;
        evento.xp = liquidacao.xp;
        evento.perfeita = liquidacao.perfeita;
        dataPendente_ = agora.valida ? agora.data : tempo::Data{};

        humor_ = distraiu_ = -1;
        reflexaoInicioMs_ = nowMs;
        overtime_ = false;
        fase_ = Fase::Reflexao;
        if (filesystem != nullptr) TempoStorage::saveState(*filesystem, estado_);
    }

    // Ciclo pomodoro: a rodada terminou (inimigo derrotado). Liquida a batalha sozinha, grava o log sem a reflexao
    // (nao da para pedir resposta no meio do ciclo) e entra na pausa curta ou longa.
    void TempoScreen::concluirRodada(uint32_t nowMs, fs::FS* filesystem) {
        finalizar(nowMs, false, filesystem);
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

    bool TempoScreen::tick(uint32_t nowMs, fs::FS* filesystem) {
        if (fase_ == Fase::Descanso) {
            if (int64_t(tempo::ms(nowMs)) - int64_t(descansoInicioMs_) >= descansoMs_) {
                encerrarDescanso(nowMs);
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
            return true;
        }
        if (overtime_ && overtimeMs() >= plano) {   // teto de +100 % do planejado
            finalizar(nowMs, false, filesystem);
            return true;
        }
        bool bipe = false;
        if (!overtime_ && !distraido_) {
            const int minutosFoco = static_cast<int>(focadoMs_ / 60000);
            if (minutosFoco >= ultimoBipeFocoMin_ + 10) {   // "presenca" a cada 10 min de foco
                ultimoBipeFocoMin_ = minutosFoco - minutosFoco % 10;
                bipe = true;
            }
            if (!avisoFimDado_ && plano >= 5 * 60000 && restanteMs() <= 2 * 60000) {   // aviso 2 min antes do fim
                avisoFimDado_ = true;
                bipe = true;
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

    // ====================================================================== Inicio (estilo Session)
    // Layout 640x172: aneis a esquerda (0..172), foco/categoria/botoes no meio (x = 182), painel HOJE a direita.
    Action TempoScreen::drawInicio(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ == Fase::Reflexao) { screen = Screen::TempoVitoria; return Action::None; }
        garantirDia(nowMs);
        const bool emBatalha = battleActive();
        const bool ciclo = pomodoro_;

        // ---- arrastar o dedo em volta do anel externo ajusta 5..60 min em passos de 5 (so sem batalha e sem ciclo)
        if (!emBatalha && !ciclo) {
            if (const auto* t = ui.touch()) {
                const double dx = double(t->x) - kCx, dy = double(t->y) - kCy;
                const double dist = std::sqrt(dx * dx + dy * dy);
                if (hasTouch(*t, ui::TouchStart)) arrastando_ = dist >= 36 && dist <= 94;
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

        // ---- aneis + minutos no centro
        {
            Bloco aneis(ui, {0, 0, 172, 172}, Background, 0, 20);
            aneis.textoCor(Rect{int16_t(kCx - 40), int16_t(kCy - 18), 80, 28}, std::to_string(minutos), 22, ui.color(Foreground),
                           TextAlign::Center);
            aneis.textoCor(Rect{int16_t(kCx - 40), int16_t(kCy + 6), 80, 16}, "min", 9, ui.color(Muted), TextAlign::Center);
            aneis.marca(static_cast<uint32_t>(minutos));
            const Cor fundo = deRole(ui, Background);
            aneis.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                anel(g, dx, dy, kCx, kCy, kRaioFora, kEspessura, minutos / 60.0, kLaranja, kAmbar, fundo, kLaranja);
                anel(g, dx, dy, kCx, kCy, kRaioDentro, kEspessura, 1.0, kCarmim, kVinho, fundo, kCarmim);
            });
        }

        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cBg = ui.color(Background);
        const uint16_t cLaranja = c565(kLaranja), cPainel = ui.color(SurfaceMuted);
        const bool livre = escolha_ < 0 || size_t(escolha_) >= titulos_.size();

        // ---- "QUAL E O SEU FOCO?" + caixa
        {
            Bloco rotulo(ui, {182, 6, 288, 18}, Background, 0, 41);
            rotulo.textoCor({182, 6, 288, 18}, "QUAL É O SEU FOCO?", 10, cMudo);
            rotulo.desenhar();

            const Rect caixa{182, 26, 288, 30};
            Bloco bloco(ui, caixa, SurfaceMuted, 10, 42);
            const std::string nome = livre ? std::string{"Foco livre"} : titulos_[escolha_].empty() ? std::string{"(sem título)"} : titulos_[escolha_];
            bloco.textoCor({194, 26, int16_t(livre ? 150 : 192), 30}, nome, 14, cTexto);
            bloco.textoCor(livre ? Rect{350, 26, 108, 30} : Rect{392, 26, 66, 30}, livre ? "escolher projeto ›" : "trocar ›", 10, livre ? cLaranja : cMudo, TextAlign::Right);
            bloco.desenhar();
            if (ui.tap(caixa, !emBatalha)) { seletor_ = true; primeiro_ = 0; return Action::None; }
        }

        // ---- categoria arrastavel: trilho com 5 paradas + puxador ("garra") que encaixa na parada mais proxima
        {
            constexpr int kTx0 = 198, kTx1 = 454, kTy = 88, kRaioPux = 13;
            const int n = int(tempo::kNumChips);
            const Rect areaTrilho{182, 70, 288, 36};
            const auto xParada = [&](int i) { return kTx0 + (kTx1 - kTx0) * i / (n - 1); };
            const auto paradaMaisPerto = [&](int x) {
                int melhor = 0;
                for (int i = 1; i < n; ++i)
                    if (std::abs(x - xParada(i)) < std::abs(x - xParada(melhor))) melhor = i;
                return melhor;
            };
            if (!emBatalha) {
                if (const auto* t = ui.touch()) {
                    if (hasTouch(*t, ui::TouchStart)) {
                        puxando_ = t->x >= areaTrilho.x && t->x < areaTrilho.x + areaTrilho.w && t->y >= areaTrilho.y
                                   && t->y < areaTrilho.y + areaTrilho.h;
                    }
                    if (puxando_ && (hasTouch(*t, ui::TouchStart) || hasTouch(*t, ui::TouchMove) || hasTouch(*t, ui::TouchRelease))) {
                        puxadorX_ = std::clamp(int(t->x), kTx0, kTx1);
                        categoria_ = tempo::categoriaDoChip(paradaMaisPerto(puxadorX_));
                    }
                    if (hasTouch(*t, ui::TouchRelease) || hasTouch(*t, ui::TouchCancel)) puxando_ = false;   // encaixa
                }
            } else {
                puxando_ = false;
            }
            const int sel = tempo::chipDaCategoria(categoria_);
            const int hx = puxando_ ? puxadorX_ : xParada(sel);
            const Cor corSel = kCorCat[sel];
            const char* nomeSel = tempo::nomeCategoria(categoria_);

            Bloco cab(ui, {182, 50, 288, 20}, Background, 0, 45);
            cab.textoCor({182, 52, 70, 16}, "CATEGORIA", 10, cMudo);
            cab.textoCor({250, 49, 120, 22}, nomeSel, 16, c565(corSel));
            cab.textoCor({372, 52, 66, 16}, "ciclo 4×", 10, ciclo ? cLaranja : cMudo, TextAlign::Right);
            cab.marca(uint32_t(sel) * 4U + ciclo * 2U);
            cab.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                const int x = 444 + dx, y = 51 + dy;
                g.fillRoundRect(x, y, 22, 12, 6, ciclo ? cLaranja : rgb565(50, 52, 50));
                g.fillCircle(x + (ciclo ? 16 : 6), y + 6, 4, cTexto);
            });
            if (ui.tap({372, 48, 98, 22}, !emBatalha)) pomodoro_ = !pomodoro_;

            Bloco trilho(ui, areaTrilho, Background, 0, 46);
            trilho.marca(uint32_t(sel) * 1024U + uint32_t(hx) * 2U + puxando_ + (emBatalha ? 8192U : 0U));
            const uint16_t cLinha = rgb565(50, 52, 50);
            trilho.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                const int y = kTy + dy;
                g.fillRoundRect(kTx0 + dx, y - 3, kTx1 - kTx0, 6, 3, cLinha);
                g.fillRoundRect(kTx0 + dx, y - 3, std::max(6, hx - kTx0), 6, 3, c565(corSel));
                for (int i = 0; i < n; ++i)
                    g.fillCircle(xParada(i) + dx, y, 4, xParada(i) <= hx ? c565(kCorCat[i]) : cMudo);
                const int r = puxando_ ? kRaioPux + 2 : kRaioPux;
                g.fillCircle(hx + dx, y, r + 2, cBg);
                g.fillCircle(hx + dx, y, r, c565(corSel));
                for (int k = -4; k <= 4; k += 4) g.fillRect(hx + dx + k - 1, y - 6, 2, 13, cBg);   // "garras"
            });

            Bloco dica(ui, {182, 106, 288, 16}, Background, 0, 43);
            dica.textoCor({182, 106, 288, 16}, ciclo ? "ciclo 4×: 25 foco · 5 pausa · 15 longa" : "anel: 5–60 min · arraste o puxador", 10, cMudo);
            dica.marca(ciclo);
            dica.desenhar();
        }

        // ---- botoes
        if (emBatalha) {
            if (botao(ui, {182, 124, 288, 36}, "Voltar à batalha", cLaranja, cBg, 14)) screen = Screen::TempoBatalha;
        } else {
            if (botao(ui, {182, 124, 178, 36}, "Começar", cLaranja, cBg, 14)) {
                comecarSessao(nowMs);
                screen = Screen::TempoBatalha;
                return Action::None;
            }
            if (botao(ui, {370, 124, 100, 36}, "Pausa", cPainel, cMudo, 12)) {
                ciclo_ = tempo::Ciclo{};
                descansoMs_ = int64_t(tempo::kCicloPausaMin) * 60000;
                iniciarDescanso(nowMs, false);
                screen = Screen::TempoBatalha;
                return Action::None;
            }
        }

        // ---- painel HOJE
        {
            int sessoes = 0;
            for (const auto& ev : estado_.dia.eventos) sessoes += ev.tipo == tempo::TipoEvento::Batalha;
            const Rect area{488, 10, 142, 152};
            Bloco painel(ui, area, SurfaceMuted, 10, 44);
            const std::string foco = fmtHoraMin(estado_.dia.fochoPagoMin);
            painel.textoCor({502, 18, 100, 16}, "HOJE", 10, cMudo);
            painel.textoCor({502, 34, 66, 28}, foco, 18, cTexto);
            painel.textoCor({566, 42, 60, 16}, "de foco", 10, cMudo);
            painel.textoCor({502, 84, 124, 16}, sessoes <= 6 ? std::to_string(sessoes) + " de 6 sessões" : std::to_string(sessoes) + " sessões",
                            10, cMudo);
            painel.textoCor({502, 106, 124, 20}, fmtNum(estado_.saldo) + " cr", 14, cLaranja);
            painel.textoCor({502, 128, 124, 16}, "‹ Início", 10, cMudo);
            painel.marca(static_cast<uint32_t>(std::min(sessoes, 6)));
            painel.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                for (int i = 0; i < 6; ++i) {
                    const int cx = 506 + i * 11 + dx, cy = 74 + dy;
                    if (i < sessoes) g.fillCircle(cx, cy, 4, cLaranja);
                    else g.drawCircle(cx, cy, 4, cMudo);
                }
            });
            if (ui.tap({488, 118, 142, 44})) {
                salvarPreferencias();
                screen = Screen::Home;
            }
        }
        return Action::None;
    }

    // Seletor de foco: "Foco livre" primeiro, depois os projetos do TickTick. Escolher volta ao Inicio.
    Action TempoScreen::drawSeletor(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        (void)nowMs;
        (void)screen;
        if (button(ui, {8, 0, 100, 30}, "‹ Voltar", Style::Plain, true, {}, 12)) { seletor_ = false; return Action::None; }
        ui.label({120, 6, 340, 18}, "ESCOLHA O FOCO (opcional)", 10, Muted);

        const size_t total = 1 + titulos_.size();
        constexpr int kLinhas = 4;
        constexpr int kPasso = 34;
        primeiro_ = std::min(primeiro_, total > size_t(kLinhas) ? total - kLinhas : 0);
        const int giro = swipe_.update(ui, {12, 28, 458, 136});
        if (giro > 0 && primeiro_ + kLinhas < total) primeiro_ += 1;
        if (giro < 0 && primeiro_ > 0) primeiro_ -= 1;

        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cLaranja = c565(kLaranja);
        for (int i = 0; i < kLinhas && primeiro_ + i < total; ++i) {
            const size_t linha = primeiro_ + i;               // 0 = Foco livre; n = projeto n-1
            const int y = 28 + i * kPasso;
            const Rect rect{12, static_cast<int16_t>(y), 458, 30};
            const bool sel = int(linha) - 1 == escolha_;
            std::string titulo, sub;
            if (linha == 0) {
                titulo = "Foco livre";
                sub = "sem projeto · você escolhe a categoria";
            } else {
                const auto& projeto = lista_.projetos[linha - 1];
                titulo = titulos_[linha - 1].empty() ? std::string{"(sem título)"} : titulos_[linha - 1];
                sub = "PROJETOS · ";
                if (projeto.vence && projeto.vence->size() >= 10)
                    sub += "vence " + projeto.vence->substr(8, 2) + "/" + projeto.vence->substr(5, 2);
                else sub += "TickTick";
            }
            Bloco item(ui, rect, SurfaceMuted, 8, 50 + sel);
            item.textoCor({26, int16_t(y), 430, 18}, titulo, 12, cTexto);
            item.textoCor({26, int16_t(y + 16), 430, 13}, sub, 10, cMudo);
            item.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                if (!sel) return;
                g.drawRoundRect(rect.x + dx, rect.y + dy, rect.w, rect.h, 8, cLaranja);
                g.drawRoundRect(rect.x + dx + 1, rect.y + dy + 1, rect.w - 2, rect.h - 2, 7, cLaranja);
            });
            if (ui.tap(rect)) {
                escolher(int(linha) - 1);
                seletor_ = false;
                return Action::None;
            }
        }
        if (total > size_t(kLinhas))
            ui.label({300, 160, 170, 12},
                     std::to_string(primeiro_ + 1) + "–" + std::to_string(std::min<size_t>(total, primeiro_ + kLinhas)) + " de "
                         + std::to_string(total) + " · arraste",
                     10, Muted, TextAlign::Right);

        Bloco dica(ui, {488, 10, 142, 152}, SurfaceMuted, 10, 55);
        dica.textoCor({502, 18, 100, 16}, "DICA", 10, cMudo);
        dica.textoCor({502, 38, 124, 18}, "Sem projeto =", 11, cTexto);
        dica.textoCor({502, 54, 124, 18}, "Foco livre.", 11, cTexto);
        dica.textoCor({502, 80, 124, 16}, "Projetos vêm do", 10, cMudo);
        dica.textoCor({502, 94, 124, 16}, "TickTick (tag", 10, cMudo);
        dica.textoCor({502, 108, 124, 16}, "Projeto).", 10, cMudo);
        dica.desenhar();
        return Action::None;
    }

    // Batalha "C3": foco, distraido, overtime e pausa. Layout 640x172: aneis a esquerda (0..172),
    // texto e botoes no meio (x = 182), painel a direita (488..630).
    Action TempoScreen::drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ == Fase::Reflexao) { screen = Screen::TempoVitoria; return Action::None; }
        if (fase_ == Fase::Nenhuma) { screen = Screen::TempoHoje; return Action::None; }
        const bool descanso = fase_ == Fase::Descanso;
        const bool pausada = fase_ == Fase::Pausada;
        const bool bonus = overtime_ && !descanso;
        const bool distr = distraido_;
        const std::string nome = nomeBatalha_.empty() ? std::string{"Foco livre"} : nomeBatalha_;
        const int64_t planoMs = int64_t(planejadoMin_) * 60000;

        // ---- numeros dos aneis
        double voce = 0.0, inimigo = 1.0;
        Cor corVoce0 = kLaranja, corVoce1 = kAmbar;
        std::string centro, rotulo;
        Cor corCentro = deRole(ui, Foreground);
        int64_t restDescanso = 0;
        if (descanso) {
            restDescanso = std::max<int64_t>(0, descansoMs_ - (int64_t(tempo::ms(nowMs)) - int64_t(descansoInicioMs_)));
            voce = 1.0 - double(restDescanso) / double(descansoMs_);
            inimigo = 0.0;
            corVoce0 = kAgua; corVoce1 = kAgua2;
            centro = fmtRelogio((restDescanso + 999) / 1000);
            rotulo = "pausa";
        } else if (bonus) {
            voce = 1.0 + std::min(1.0, double(overtimeMs()) / double(planoMs));
            inimigo = 0.0;
            corVoce0 = kAzul; corVoce1 = kAzul2;
            centro = "+" + fmtRelogio(overtimeMs() / 1000);
            rotulo = "bônus";
            corCentro = kAzul;
        } else {
            voce = std::min(1.0, double(focadoMs_) / double(planoMs));
            inimigo = std::min(1.0, double(restanteMs()) / double(planoMs));
            centro = fmtRelogio((restanteMs() + 999) / 1000);
            rotulo = pausada ? "pausada" : "faltam";
        }
        voce = std::floor(voce * 720) / 720;      // quantiza: anel so e redesenhado quando o valor muda
        inimigo = std::floor(inimigo * 720) / 720;

        // ---- aneis + centro
        {
            Bloco aneis(ui, {0, 0, 172, 172}, Background, 0, 20 + surface());
            const bool grande = false;
            aneis.textoCor(Rect{int16_t(kCx - 40), int16_t(kCy - (grande ? 18 : 15)), 80, int16_t(grande ? 28 : 24)}, centro, grande ? 22 : bonus ? 14 : 17,
                           c565(corCentro), TextAlign::Center);
            aneis.textoCor(Rect{int16_t(kCx - 40), int16_t(kCy + (grande ? 6 : 5)), 80, 16}, rotulo, 9, ui.color(Muted), TextAlign::Center);
            aneis.marca(static_cast<uint32_t>(voce * 720) * 1031U + static_cast<uint32_t>(inimigo * 720));
            const Cor fundo = deRole(ui, Background);
            aneis.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                const int ox = dx, oy = dy;
                anel(g, ox, oy, kCx, kCy, kRaioFora, kEspessura, voce, corVoce0, corVoce1, fundo, corVoce0);
                anel(g, ox, oy, kCx, kCy, kRaioDentro, kEspessura, descanso ? 0.0 : inimigo, kCarmim, kVinho, fundo, kCarmim);
            });
        }

        // ---- texto central
        const auto estimativa = estimar();
        Bloco texto(ui, {176, 4, 308, 118}, Background, 0, 40 + surface());
        const uint16_t cLaranja = c565(kLaranja), cAzul = c565(kAzul), cCarmim = c565(kCarmim), cAgua = c565(kAgua);
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        const auto agora = tempo::agora(nowMs);
        const std::string rodadaTxt = ciclo_.ativo ? "rodada " + std::to_string(ciclo_.rodada) + " de " + std::to_string(tempo::kCicloRodadas)
                                                   : std::string{};
        if (descanso) {
            const int pausaMin = int(descansoMs_ / 60000);
            const bool longa = ciclo_.ativo && ciclo_.passo == tempo::PassoCiclo::PausaLonga;
            texto.textoCor({182, 8, 300, 18}, ciclo_.ativo ? "PAUSA · " + rodadaTxt : std::string{"PAUSA"}, 10, cAgua);
            texto.textoCor({182, 24, 300, 28}, std::string{longa ? "Pausa longa de " : "Pausa de "} + std::to_string(pausaMin) + " minutos", 18, cTexto);
            texto.textoCor({182, 53, 300, 20}, "respire; um bipe avisa quando acabar", 11, cMudo);
            texto.textoCor({182, 71, 300, 20},
                           ciclo_.ativo ? (longa ? std::string{"depois o ciclo termina"}
                                                 : "depois: rodada " + std::to_string(ciclo_.rodada + 1) + " de " + std::to_string(tempo::kCicloRodadas))
                                        : descansoVolta_ ? "depois você volta ao bônus" : "depois você volta ao Início", 11, cMudo);
        } else if (bonus) {
            texto.textoCor({182, 8, 300, 18}, pausada ? "PAUSADO · INIMIGO DERROTADO" : "INIMIGO DERROTADO · CONTINUANDO", 10, cAzul);
            texto.textoCor({182, 24, 300, 28}, nome, 18, cTexto);
            texto.textoCor({182, 53, 300, 20}, "seu anel passou de 100% e dá a 2ª volta", 11, cMudo);
            texto.textoCor({182, 71, 300, 20}, "o trabalho não acabou? siga — conta até +" + std::to_string(planejadoMin_) + " min", 11, cMudo);
        } else {
            texto.textoCor({182, 8, 300, 18},
                           pausada ? std::string{"PAUSADA"}
                           : distr ? std::string{"DISTRAÍDO · o inimigo se cura"}
                                   : "COMBO " + std::to_string(int(comboMs_ / 60000)) + " min sem distração",
                           10, pausada ? cMudo : distr ? cAzul : cLaranja);
            texto.textoCor({182, 24, 300, 28}, nome, 18, cTexto);
            texto.textoCor({182, 53, 110, 20}, "você " + std::to_string(int(std::min<uint64_t>(100, focadoMs_ * 100 / uint64_t(planoMs))) ) + "%", 13, cLaranja);
            texto.textoCor({292, 53, 150, 20}, "inimigo " + std::to_string(pontosDeVida()) + "%", 13, cCarmim);
            std::string linha;
            if (distr) linha = "volte ao foco e o anel vermelho volta a esvaziar";
            else {
                linha = (ciclo_.ativo ? rodadaTxt + " · " : std::string{"rodada de "}) + std::to_string(planejadoMin_) + " min";
                if (agora.valida && !pausada)
                    linha += " · termina " + fmtHoraRelogio(agora.minutoDoDia + int((restanteMs() + 59999) / 60000));
            }
            texto.textoCor({182, 77, 300, 18}, linha, 10, cMudo);
        }
        texto.desenhar();

        // ---- painel direito
        Bloco painel(ui, {488, 10, 142, 152}, SurfaceMuted, 10, 60 + surface());
        const uint16_t cVerde = c565(kVerde);
        if (descanso) {
            painel.textoCor({502, 18, 120, 16}, "PAUSA", 10, cMudo);
            painel.textoCor({502, 36, 120, 24}, fmtRelogio((restDescanso + 999) / 1000), 18, cAgua);
            painel.textoCor({502, 62, 122, 18}, "restantes", 12, cTexto);
            painel.textoCor({502, 90, 120, 16}, "Hoje", 10, cMudo);
            painel.textoCor({502, 108, 124, 18}, fmtHoraMin(estado_.dia.fochoPagoMin) + " de foco", 11, cTexto);
        } else if (bonus) {
            painel.textoCor({502, 18, 120, 16}, "ATÉ AGORA", 10, cMudo);
            painel.textoCor({502, 36, 120, 24}, "+" + fmtNum(estimativa.cr) + " cr", 18, cLaranja);
            painel.textoCor({502, 62, 122, 18}, "+" + fmtNum(estimativa.xp) + " XP", 12, cTexto);
            painel.textoCor({502, 92, 124, 18}, std::to_string(int(focadoMs_ / 60000)) + " min de foco", 11, cTexto);
        } else {
            painel.textoCor({502, 18, 120, 16}, "AO TERMINAR", 10, cMudo);
            painel.textoCor({502, 36, 120, 24}, "+" + fmtNum(estimativa.cr) + " cr", 18, cLaranja);
            painel.textoCor({502, 62, 122, 18}, "+" + fmtNum(estimativa.xp) + " XP", 12, cTexto);
            painel.textoCor({502, 90, 120, 16}, "Distrações", 10, cMudo);
            painel.textoCor({502, 108, 60, 22}, std::to_string(numDistracoes_), 16,
                            numDistracoes_ > tempo::param::kMaxDistracoes ? cCarmim : cVerde);
        }
        painel.desenhar();

        // ---- botoes
        const uint16_t cFundoBtn = ui.color(SurfaceMuted), cBg = ui.color(Background);
        if (descanso) {
            if (botao(ui, {182, 124, 168, 36}, "Encerrar pausa", c565(kAgua), cBg, 13)) encerrarDescanso(nowMs);
        } else if (bonus) {
            if (botao(ui, {182, 124, 138, 36}, "Concluir", cLaranja, cBg, 13)) {
                finalizar(nowMs, false, fs_);
                screen = Screen::TempoVitoria;
            } else if (botao(ui, {330, 124, 140, 36}, pausada ? "Retomar" : "Pausa de 5 min", cFundoBtn, cTexto, 12)) {
                if (pausada) pausarOuRetomar(nowMs);
                else iniciarDescanso(nowMs, true);
            }
        } else {
            if (botao(ui, {182, 124, 88, 36}, pausada ? "Retomar" : "Pausar", cLaranja, cBg, 13)) pausarOuRetomar(nowMs);
            if (botao(ui, {280, 124, 110, 36}, distr ? "Voltei" : "Me distraí", distr ? cAzul : cFundoBtn, distr ? cBg : cTexto, 13, !pausada))
                alternarDistracao(nowMs);
            if (botao(ui, {400, 124, 70, 36}, "Fim", cFundoBtn, cMudo, 13)) {
                acumular(nowMs);
                if (focadoMs_ < 60000) {   // menos de 1 min de foco: cancela sem log nem recompensa
                    fase_ = Fase::Nenhuma;
                    ciclo_ = tempo::Ciclo{};
                    screen = Screen::TempoHoje;
                } else {
                    ciclo_ = tempo::Ciclo{};   // encerrar no meio abandona o ciclo
                    finalizar(nowMs, true, fs_);
                    screen = Screen::TempoVitoria;
                }
            }
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
        saque.textoCor({502, 126, 124, 16}, "vai para o recibo", 10, cMudo);
        saque.desenhar();
        return Action::None;
    }

} // namespace screens
