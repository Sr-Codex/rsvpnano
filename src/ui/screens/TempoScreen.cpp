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
        constexpr int kCx = 86, kCy = 86;      // centro dos aneis
        constexpr int kRaioFora = 70, kRaioDentro = 48, kEspessura = 18;
        constexpr int64_t kDescansoMs = 5 * 60 * 1000;
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

    // ====================================================================== launcher
    ResumoTempo TempoScreen::resumo(fs::FS* filesystem, uint32_t nowMs) {
        if (!carregado_) carregar(filesystem, true, nowMs);
        else if (nowMs - ultimoDesenhoMs_ > 1500 && !battleActive()) carregar(filesystem, false, nowMs);
        ultimoDesenhoMs_ = nowMs;
        ResumoTempo r;
        r.saldo = std::to_string(static_cast<long>(std::lround(estado_.saldo)));   // inteiro: cabe no cartao do launcher
        r.temProjetos = !titulos_.empty();
        r.planejadoMin = planejadoMin_;
        if (fase_ == Fase::Reflexao) {
            r.estado = ResumoTempo::Estado::Reflexao;
        } else if (battleActive()) {
            r.estado = ResumoTempo::Estado::Batalha;
            r.pausada = fase_ == Fase::Pausada;
            r.descanso = fase_ == Fase::Descanso;
            r.bonus = overtime_ && !r.descanso;
            r.projeto = projeto_ < titulos_.size() ? titulos_[projeto_] : std::string{};
            const int64_t plano = std::max<int64_t>(1, int64_t(planejadoMin_) * 60000);
            r.inimigoPct = r.descanso ? 0 : pontosDeVida();
            r.vocePct = r.descanso ? 100 : int(std::min<int64_t>(100, int64_t(focadoMs_) * 100 / plano));
            if (r.descanso) r.centro = "pausa";
            else r.centro = r.bonus ? "+" + fmtRelogio(overtimeMs() / 1000) : fmtRelogio((restanteMs() + 999) / 1000);
        } else {
            r.projeto = titulos_.empty() ? std::string{} : titulos_.front();
            r.centro = fmtRelogio(int64_t(planejadoMin_) * 60);
        }
        r.assinatura = ui::Context::signature(r.projeto + "|" + r.centro + "|" + r.saldo,
                                              uint32_t(r.estado) * 1000003U + r.pausada * 7U + r.descanso * 11U + r.bonus * 13U
                                                  + uint32_t(r.vocePct) * 131U + uint32_t(r.inimigoPct) * 17U + r.temProjetos);
        return r;
    }

    void TempoScreen::abrirBatalha(Screen& screen) {
        screen = fase_ == Fase::Reflexao ? Screen::TempoVitoria : Screen::TempoBatalha;
    }

    bool TempoScreen::comecar(Screen& screen) {
        if (battleActive() || fase_ == Fase::Reflexao || lista_.projetos.empty()) return false;
        abrirPreparo(0);
        screen = Screen::TempoBatalha;
        return true;
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
            salvar(filesystem);
        }
        lista_ = tempo::ListaProjetos{};
        titulos_.clear();
        if (filesystem != nullptr) TempoStorage::loadProjects(*filesystem, lista_);
        for (const auto& projeto : lista_.projetos) titulos_.push_back(tempo::limparTitulo(projeto.titulo));
        primeiro_ = 0;
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

    // Estimativa do motor: preparo/foco = "ao terminar" (zera o inimigo no plano + cura); overtime = "ate agora".
    tempo::LiquidacaoBatalha TempoScreen::estimar(bool preparo) const {
        tempo::EntradaBatalha e;
        e.categoria = tempo::Categoria::Projetos;
        e.minutosPlanejados = planejadoMin_;
        e.minutosDistraidos = static_cast<int>(distraidoMs_ / 60000);
        e.numDistracoes = numDistracoes_;
        e.maiorDistracaoMin = std::max(maiorDistracaoMin_, static_cast<int>(distracaoAtualMs_ / 60000));
        if (preparo) {
            e.minutosFocados = planejadoMin_;
        } else if (overtime_) {
            e.minutosFocados = static_cast<int>(focadoMs_ / 60000);
        } else {
            e.minutosFocados = planejadoMin_ + std::min(e.minutosDistraidos, planejadoMin_);
        }
        return tempo::liquidarBatalha(estado_, e);
    }

    void TempoScreen::abrirPreparo(size_t indice) {
        projeto_ = indice;
        fase_ = Fase::Preparo;
        focadoMs_ = distraidoMs_ = distracaoAtualMs_ = comboMs_ = 0;
        distraido_ = false;
        overtime_ = false;
        arrastando_ = false;
        numDistracoes_ = maiorDistracaoMin_ = 0;
        ultimoBipeFocoMin_ = 0;
        avisoFimDado_ = false;
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
        descansoInicioMs_ = tempo::ms(nowMs);
        fase_ = Fase::Descanso;
    }

    void TempoScreen::encerrarDescanso() {
        if (fase_ != Fase::Descanso) return;
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

        // O evento do log e gravado ao salvar a reflexao (campos humor/distraiu). A economia ja foi liquidada
        // e persistida aqui: a reflexao nunca da credito nem XP.
        const auto agora = tempo::agora(nowMs);
        auto& evento = eventoPendente_;
        evento = tempo::EventoLog{};
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
        evento.overtimeMin = liquidacao.minutosOvertime;
        evento.cr = liquidacao.cr;
        evento.xp = liquidacao.xp;
        dataPendente_ = agora.valida ? agora.data : tempo::Data{};

        humor_ = distraiu_ = -1;
        reflexaoInicioMs_ = nowMs;
        overtime_ = false;
        fase_ = Fase::Reflexao;
        if (filesystem != nullptr) TempoStorage::saveState(*filesystem, estado_);
    }

    void TempoScreen::salvarReflexao(fs::FS* filesystem) {
        if (fase_ != Fase::Reflexao) return;
        eventoPendente_.humor = humor_ >= 0 ? humor_ + 1 : 0;
        eventoPendente_.distraiu = distraiu_ >= 0 ? distraiu_ + 1 : 0;
        if (filesystem == nullptr) filesystem = fs_;
        if (filesystem != nullptr) TempoStorage::appendLog(*filesystem, dataPendente_, eventoPendente_);
        fase_ = Fase::Nenhuma;
    }

    bool TempoScreen::tick(uint32_t nowMs, fs::FS* filesystem) {
        if (fase_ == Fase::Descanso) {
            if (int64_t(tempo::ms(nowMs)) - int64_t(descansoInicioMs_) >= kDescansoMs) {
                encerrarDescanso();
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
            if (fase_ == Fase::Preparo) iniciar(nowMs);
            else if (fase_ == Fase::Lutando || fase_ == Fase::Pausada) pausarOuRetomar(nowMs);
            else if (fase_ == Fase::Descanso) encerrarDescanso();
            else if (fase_ == Fase::Reflexao) screen = Screen::TempoVitoria;
            else screen = Screen::TempoHoje;
        } else if (battleActive()) {
            screen = Screen::TempoBatalha;
        } else if (fase_ == Fase::Reflexao) {
            screen = Screen::TempoVitoria;
        } else if (!carregado_) {
            carregar(filesystem, true, nowMs);
        } else if (primeiro_ < lista_.projetos.size()) {
            abrirPreparo(primeiro_);
            screen = Screen::TempoBatalha;
        }
    }

    void TempoScreen::back(uint32_t nowMs, Screen& screen, fs::FS* filesystem) {
        (void)nowMs;
        if (screen == Screen::TempoHoje) {
            screen = Screen::Home;
        } else {
            if (screen == Screen::TempoVitoria || fase_ == Fase::Reflexao) salvarReflexao(filesystem);   // sem resposta
            else if (screen == Screen::TempoBatalha && fase_ == Fase::Preparo) fase_ = Fase::Nenhuma;
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
        if (fase_ == Fase::Reflexao) { screen = Screen::TempoVitoria; return Action::None; }
        garantirDia(nowMs);
        const auto agora = tempo::agora(nowMs);

        // cabecalho
        if (button(ui, {8, 0, 94, 43}, "‹ Início", Style::Plain, true, {}, 13)) { screen = Screen::Home; return Action::None; }
        ui.label({112, 0, 250, 43}, "HOJE · " + dataCabecalho(agora), 13, Muted);
        ui.label({372, 0, 130, 43}, "Foco " + fmtHoraMin(estado_.dia.fochoPagoMin), 13, Foreground, TextAlign::Right);
        ui.label({508, 0, 124, 43}, fmtNum(estado_.saldo) + " cr", 17, Accent, TextAlign::Right);
        ui::modern::rule(ui, {0, 43, 640, 1});

        // cartao AGORA
        const Rect cartao{12, 52, 300, 112};
        Bloco agoraCartao(ui, cartao, SurfaceMuted, 8, battleActive() ? 1 : 2);
        const bool lutando = battleActive();
        const bool descanso = fase_ == Fase::Descanso;
        agoraCartao.texto({28, 58, 200, 20}, lutando ? (descanso ? "AGORA · PAUSA" : fase_ == Fase::Pausada ? "AGORA · PAUSADA"
                                                         : overtime_ ? "AGORA · BÔNUS" : "AGORA · LUTANDO")
                                                     : "AGORA", 11, Accent);
        if (lutando) {
            const std::string nome = descanso && !descansoVolta_ ? std::string{"Pausa de 5 min"}
                                   : projeto_ < titulos_.size() ? titulos_[projeto_] : std::string{};
            agoraCartao.texto({28, 78, 200, 24}, nome, 15, Foreground);
            const int64_t restDescanso = std::max<int64_t>(0, kDescansoMs - (int64_t(tempo::ms(nowMs)) - int64_t(descansoInicioMs_)));
            agoraCartao.texto({28, 102, 190, 40},
                              descanso ? fmtRelogio((restDescanso + 999) / 1000)
                              : overtime_ ? "+" + fmtRelogio(overtimeMs() / 1000)
                                          : fmtRelogio((restanteMs() + 999) / 1000), 30, Foreground);
            const int vida = descanso ? int(restDescanso * 100 / kDescansoMs) : pontosDeVida();
            agoraCartao.marca(static_cast<uint32_t>(vida));
            agoraCartao.texto({28, 146, 200, 16}, (descanso ? std::string{"de 5 min"} : "de " + std::to_string(planejadoMin_) + " min")
                                                   + " · toque para abrir", 11, Muted);
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

    // Batalha "C3": preparo, foco, distraido, overtime e pausa. Layout 640x172: aneis a esquerda (0..172),
    // texto e botoes no meio (x = 182), painel a direita (488..630).
    Action TempoScreen::drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen) {
        if (fase_ == Fase::Reflexao) { screen = Screen::TempoVitoria; return Action::None; }
        if (fase_ == Fase::Nenhuma) { screen = Screen::TempoHoje; return Action::None; }
        const bool preparo = fase_ == Fase::Preparo;
        const bool descanso = fase_ == Fase::Descanso;
        const bool pausada = fase_ == Fase::Pausada;
        const bool bonus = overtime_ && !preparo && !descanso;
        const bool distr = distraido_;
        const std::string nome = projeto_ < titulos_.size() && !titulos_[projeto_].empty() ? titulos_[projeto_] : "Projeto";
        const int64_t planoMs = int64_t(planejadoMin_) * 60000;

        // ---- preparo: arrastar o dedo em volta do anel externo ajusta 5..60 min em passos de 5
        if (preparo) {
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
                    planejadoMin_ = std::clamp(m, 1, 12) * 5;
                }
                if (hasTouch(*t, ui::TouchCancel) || hasTouch(*t, ui::TouchRelease)) arrastando_ = false;
            }
        }

        // ---- numeros dos aneis
        double voce = 0.0, inimigo = 1.0;
        Cor corVoce0 = kLaranja, corVoce1 = kAmbar;
        std::string centro, rotulo;
        Cor corCentro = deRole(ui, Foreground);
        int64_t restDescanso = 0;
        if (preparo) {
            centro = std::to_string(planejadoMin_);
            rotulo = "min";
        } else if (descanso) {
            restDescanso = std::max<int64_t>(0, kDescansoMs - (int64_t(tempo::ms(nowMs)) - int64_t(descansoInicioMs_)));
            voce = 1.0 - double(restDescanso) / kDescansoMs;
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
            const bool grande = preparo;
            aneis.textoCor(Rect{int16_t(kCx - 40), int16_t(kCy - (grande ? 18 : 15)), 80, int16_t(grande ? 28 : 24)}, centro, grande ? 22 : bonus ? 14 : 17,
                           c565(corCentro), TextAlign::Center);
            aneis.textoCor(Rect{int16_t(kCx - 40), int16_t(kCy + (grande ? 6 : 5)), 80, 16}, rotulo, 9, ui.color(Muted), TextAlign::Center);
            aneis.marca(static_cast<uint32_t>(voce * 720) * 1031U + static_cast<uint32_t>(inimigo * 720));
            const Cor fundo = deRole(ui, Background);
            const double previa = preparo ? planejadoMin_ / 60.0 : 0.0;
            aneis.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                const int ox = dx, oy = dy;
                anel(g, ox, oy, kCx, kCy, kRaioFora, kEspessura, preparo ? previa : voce,
                     preparo ? mistura(fundo, kLaranja, 0.5) : corVoce0, preparo ? mistura(fundo, kAmbar, 0.5) : corVoce1, fundo, corVoce0);
                if (preparo) {  // previa da duracao: arco apagado + marcador na ponta (arraste o dedo)
                    const double ang = (-90 + 360 * previa) * 3.14159265358979 / 180.0;
                    const int px = ox + kCx + int(std::lround(kRaioFora * std::cos(ang)));
                    const int py = oy + kCy + int(std::lround(kRaioFora * std::sin(ang)));
                    g.fillCircle(px, py, 8, c565(mistura(fundo, kLaranja, 0.75)));
                    g.fillCircle(px, py, 4, c565(kAmbar));
                }
                anel(g, ox, oy, kCx, kCy, kRaioDentro, kEspessura, descanso ? 0.0 : inimigo, kCarmim, kVinho, fundo, kCarmim);
            });
        }

        // ---- texto central
        const auto estimativa = estimar(preparo);
        Bloco texto(ui, {176, 4, 308, 118}, Background, 0, 40 + surface());
        const uint16_t cLaranja = c565(kLaranja), cAzul = c565(kAzul), cCarmim = c565(kCarmim), cAgua = c565(kAgua);
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        const auto agora = tempo::agora(nowMs);
        const int nivel = tempo::nivelDeXp(estado_.xpTotal);
        if (preparo) {
            texto.textoCor({182, 8, 300, 18}, "INIMIGO · nível " + std::to_string(nivel), 10, cMudo);
            texto.textoCor({182, 24, 300, 28}, nome, 18, cTexto);
            texto.textoCor({182, 53, 300, 20}, "anel de fora: você ganhando vida", 11, cLaranja);
            texto.textoCor({182, 71, 300, 20}, "anel de dentro: o inimigo morrendo", 11, cCarmim);
            texto.textoCor({182, 92, 300, 18}, "arraste o dedo no anel de fora: 5 a 60 min", 10, cMudo);
        } else if (descanso) {
            texto.textoCor({182, 8, 300, 18}, "PAUSA", 10, cAgua);
            texto.textoCor({182, 24, 300, 28}, "Pausa de 5 minutos", 18, cTexto);
            texto.textoCor({182, 53, 300, 20}, "respire; um bipe avisa quando acabar", 11, cMudo);
            texto.textoCor({182, 71, 300, 20}, descansoVolta_ ? "depois você volta ao bônus" : "depois você volta ao Hoje", 11, cMudo);
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
                linha = "rodada de " + std::to_string(planejadoMin_) + " min";
                if (agora.valida && !pausada)
                    linha += " · termina " + fmtHoraRelogio(agora.minutoDoDia + int((restanteMs() + 59999) / 60000));
            }
            texto.textoCor({182, 77, 300, 18}, linha, 10, cMudo);
        }
        texto.desenhar();

        // ---- painel direito
        Bloco painel(ui, {488, 10, 142, 152}, SurfaceMuted, 10, 60 + surface());
        const uint16_t cVerde = c565(kVerde);
        if (preparo) {
            painel.textoCor({502, 18, 120, 16}, "RECOMPENSA", 10, cMudo);
            painel.textoCor({502, 36, 120, 24}, "+" + fmtNum(estimativa.cr) + " cr", 18, cLaranja);
            painel.textoCor({502, 60, 122, 18}, "+" + fmtNum(estimativa.xp) + " XP · " + nomeAtributo(estimativa.atributo), 12, cTexto);
            int sessoes = 0;
            for (const auto& ev : estado_.dia.eventos) sessoes += ev.tipo == tempo::TipoEvento::Batalha;
            painel.textoCor({502, 90, 120, 16}, "Hoje", 10, cMudo);
            painel.textoCor({502, 108, 124, 18}, fmtHoraMin(estado_.dia.fochoPagoMin) + " foco · " + std::to_string(sessoes)
                                                   + (sessoes == 1 ? " sessão" : " sessões"), 11, cTexto);
        } else if (descanso) {
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
        if (preparo) {
            if (botao(ui, {182, 124, 168, 36}, "Começar batalha", cLaranja, cBg, 13)) iniciar(nowMs);
            if (botao(ui, {360, 124, 110, 36}, "Pausa", cFundoBtn, cMudo, 12)) iniciarDescanso(nowMs, false);
        } else if (descanso) {
            if (botao(ui, {182, 124, 168, 36}, "Encerrar pausa", c565(kAgua), cBg, 13)) encerrarDescanso();
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
                    screen = Screen::TempoHoje;
                } else {
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
