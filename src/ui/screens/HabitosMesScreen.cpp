#include "ui/screens/HabitosMesScreen.h"

#include <algorithm>
#include <bit>
#include <cstdio>

#include "storage/tempo/TempoStorage.h"
#include "tempo/Relogio.h"
#include "ui/ClippedGfx.h"
#include "ui/FormaSuaveGfx.h"
#include "ui/Rgb565.h"
#include "ui/SansFont.h"
#include "ui/screens/HabitosIcones.h"

namespace screens {
    namespace {
        using ui::Rect;
        using ui::TextAlign;
        using ui::themes::rgb565;
        namespace ic = icones;

        constexpr auto fundo = rgb565(16, 16, 25), painel = rgb565(32, 32, 43), borda = rgb565(54, 54, 65);
        constexpr auto tinta = rgb565(240, 240, 246), mudo = rgb565(170, 171, 188), lilas = rgb565(185, 170, 255);
        constexpr auto futuro = rgb565(72, 72, 84), chip = rgb565(41, 40, 52);
        constexpr const char* meses[] = {"Janeiro", "Fevereiro", "Março",    "Abril",   "Maio",     "Junho",
                                         "Julho",   "Agosto",    "Setembro", "Outubro", "Novembro", "Dezembro"};
        constexpr const char* curtos[] = {"Jan", "Fev", "Mar", "Abr", "Mai", "Jun",
                                          "Jul", "Ago", "Set", "Out", "Nov", "Dez"};
        constexpr const char* semana[] = {"Seg", "Ter", "Qua", "Qui", "Sex", "Sáb", "Dom"};
        constexpr const char* semanaCurta[] = {"2ª", "3ª", "4ª", "5ª", "6ª", "Sá", "Do"};
        constexpr const char* abas[] = {"Mês", "5 dias", "Semanas"};

        Rect area(int x, int y, int w, int h) { return {int16_t(x), int16_t(y), int16_t(w), int16_t(h)}; }
        uint32_t hash(std::string_view s, uint32_t base = 0) { return ui::Context::signature(s, base); }

        // A área recebida pelo callback pode ser uma faixa do buffer do LCD.
        struct Pintor {
            ui::Context& ui;
            Arduino_GFX& g;
            int dx, dy;
            void caixa(Rect r, int raio, uint16_t cor, uint16_t sob, bool contorno = false) {
                ui::forma::retanguloArredondado(g, r.x + dx, r.y + dy, r.w, r.h, raio, cor, sob, contorno, borda);
            }
            void texto(Rect r, std::string_view s, int tamanho, uint16_t cor, uint16_t sob, TextAlign a = TextAlign::Left) {
                ui.drawText(g, ui.prepareText(r, s, tamanho, a, 1), cor, dx, dy, sob);
            }
        };

        template <class Desenhar> void pintar(ui::Context& ui, Rect r, uint32_t sig, Desenhar desenhar) {
            if (!ui.redraw(r, sig, true)) return;
            ui.paint(r, [&](Arduino_GFX& out, Rect alvo) {
                ui::ClippedGfx g(out, alvo);
                ic::Origem origem(alvo.x - r.x, alvo.y - r.y, fundo);
                Pintor p{ui, g, alvo.x - r.x, alvo.y - r.y};
                g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, fundo);
                desenhar(p);
            });
        }

        struct CartaoMes {
            std::string id, nome;
            tempo::IconeHabito icone;
            uint16_t cor;
            uint32_t dias;
            tempo::DiasRecentes recentes;
        };

        uint16_t tom(std::string_view cor, std::string_view id) {
            const int32_t rgb = tempo::corDoHabito(cor);
            if (rgb >= 0) return rgb565(rgb >> 16, rgb >> 8, rgb);
            static constexpr uint16_t paleta[] = {rgb565(80, 219, 193), rgb565(255, 194, 81),  rgb565(182, 154, 255),
                                                  rgb565(255, 154, 85), rgb565(247, 124, 191), rgb565(128, 182, 255)};
            return paleta[hash(id) % std::size(paleta)];
        }

        void calendario(Pintor& p, const tempo::MesHabitos& mes, const CartaoMes& h, int hoje, int x, int y, int passoX,
                        int altura, int lado) {
            const int passoY = std::min(lado + 4, (altura - lado) / (mes.linhas() - 1));
            const uint16_t neutro = ic::misturarAlfa(h.cor, painel, 2);
            for (int i = 0; i < 7; ++i)
                p.texto(area(x + i * passoX - 2, y - 17, lado + 4, 15), std::string_view("DSTQQSS").substr(i, 1), 11, mudo,
                        painel, TextAlign::Center);
            for (int d = 1; d <= mes.dias; ++d) {
                int pos = mes.primeiro + d - 1, cx = x + (pos % 7) * passoX, cy = y + (pos / 7) * passoY;
                if (hoje == d) p.caixa(area(cx - 2, cy - 2, lado + 4, lado + 4), 4, tinta, painel);
                uint16_t base = hoje == d ? tinta : painel;
                if (d > hoje) {
                    p.caixa(area(cx, cy, lado, lado), 3, futuro, base);
                    p.caixa(area(cx + 1, cy + 1, lado - 2, lado - 2), 2, painel, futuro);
                } else {
                    p.caixa(area(cx, cy, lado, lado), 3, (h.dias & tempo::bitDia(d)) ? h.cor : neutro, base);
                }
            }
        }

        void badge(Pintor& p, const CartaoMes& h, int x, int y) {
            const auto tomBadge = ic::misturarAlfa(h.cor, painel, 3);
            p.caixa(area(x, y, 28, 28), 14, tomBadge, painel);
            ic::habito(p.g, h.icone, x + 14, y + 14, 10, h.cor, tomBadge);
        }
        std::string diasFeitos(uint32_t dias) { return std::popcount(dias) == 1 ? "dia feito" : "dias feitos"; }

        void cartao(Pintor& p, Rect r, const CartaoMes& h, const tempo::MesHabitos& mes, int hoje, bool emPe,
                    std::string_view status) {
            const int x = r.x, y = r.y;
            p.caixa(r, 13, painel, fundo, true);
            badge(p, h, x + 9, y + 8);
            p.texto(area(x + 43, y + 6, r.w - 49, 32), h.nome, 17, tinta, painel);
            calendario(p, mes, h, hoje, x + 13, y + 58, emPe ? 20 : 17, emPe ? 68 : 64, emPe ? 10 : 9);
            if (!status.empty()) {
                Rect aviso = emPe ? area(x + 13, y + 124, r.w - 20, 22) : area(x + 120, y + 78, 76, 25);
                p.texto(aviso, status, 13, mudo, painel, emPe ? TextAlign::Left : TextAlign::Center);
                return;
            }
            const auto total = std::to_string(std::popcount(h.dias));
            if (emPe)
                p.texto(area(x + 13, y + 124, r.w - 20, 22), total + " " + diasFeitos(h.dias), 13, mudo, painel);
            else {
                p.texto(area(x + 122, y + 60, 68, 39), total, 28, h.cor, painel, TextAlign::Right);
                p.texto(area(x + 119, y + 94, 71, 19), diasFeitos(h.dias), 13, mudo, painel, TextAlign::Right);
            }
        }

        Rect aba(int i, bool pe) {
            constexpr int retrato[] = {4, 48, 96, 168}, paisagem[] = {319, 369, 425, 503};
            const int* x = pe ? retrato : paisagem;
            return area(x[i], pe ? 32 : 0, x[i + 1] - x[i], pe ? 33 : 35);
        }

        int centroDoDia(int coluna, bool pe) { return pe ? 83 + 18 * coluna : 266 + 82 * coluna; }

        void cabecalhoCincoDias(Pintor& p, Rect r, const tempo::Data& hoje, size_t total, bool pe) {
            p.caixa(r, 7, painel, fundo);
            p.texto(area(10, r.y, pe ? 67 : 180, r.h), std::to_string(total) + (total == 1 ? " hábito" : " hábitos"),
                    pe ? 13 : 15, mudo, painel);
            for (int j = 0; j < 5; ++j) {
                auto data = tempo::dataDeSerial(tempo::diaSerial(hoje) + j - 4);
                const int x = centroDoDia(j, pe);
                const auto cor = j == 4 ? tinta : mudo;
                const auto indice = tempo::diaDaSemana(data);
                if (pe) {
                    p.texto(area(x - 9, r.y, 18, 17), semanaCurta[indice], 11, cor, painel, TextAlign::Center);
                    p.texto(area(x - 10, r.y + 17, 20, 18), std::to_string(data.dia), 13, cor, painel, TextAlign::Center);
                } else {
                    p.texto(area(x - 36, r.y, 72, r.h), std::string(semana[indice]) + " " + std::to_string(data.dia),
                            13, cor, painel, TextAlign::Center);
                }
            }
        }

        void linhaCincoDias(Pintor& p, Rect r, const CartaoMes& h, bool pe) {
            p.caixa(r, 1, painel, fundo);
            const int cy = r.y + r.h / 2;
            ic::habito(p.g, h.icone, pe ? 18 : 30, pe ? r.y + 13 : cy, pe ? 8 : 10, h.cor, painel);
            Rect nome = pe ? area(9, r.y + 22, 65, 39) : area(51, r.y, 190, r.h);
            auto layout = p.ui.prepareText(nome, h.nome, pe ? 13 : 17, TextAlign::Left, pe ? 2 : 1);
            p.ui.drawText(p.g, layout, tinta, p.dx, p.dy, painel);
            const int lado = pe ? 14 : 22;
            const auto neutro = ic::misturarAlfa(h.cor, painel, 2);
            for (int j = 0; j < 5; ++j)
                p.caixa(area(centroDoDia(j, pe) - lado / 2, cy - lado / 2, lado, lado),
                        pe ? 4 : 6, h.recentes[4 - j] ? h.cor : neutro, painel);
        }

        void cartaoSemanas(Pintor& p, Rect r, const CartaoMes& h, int diaSemana, bool pe) {
            p.caixa(r, 12, painel, fundo, true);
            ic::habito(p.g, h.icone, r.x + 17, r.y + 17, 9, h.cor, painel);
            constexpr int ladoCheck = 30;
            const auto botao = area(r.x + r.w - 7 - ladoCheck, r.y + 3, ladoCheck, ladoCheck);
            const int nomeX = r.x + 32;
            p.texto(area(nomeX, r.y + 2, botao.x - nomeX - 4, 29), h.nome, pe ? 15 : 17, tinta, painel);
            const auto corCheck = h.recentes[0] ? h.cor : chip;
            p.caixa(botao, 9, corCheck, painel, !h.recentes[0]);
            const float cx = botao.x + p.dx + ladoCheck / 2, cy = botao.y + p.dy + ladoCheck / 2;
            const float xs[] = {cx - 7, cx - 2, cx + 7}, ys[] = {cy + 1, cy + 6, cy - 6};
            ui::forma::polilinha(p.g, xs, ys, 3, 2.2f, h.recentes[0] ? fundo : mudo, corCheck);
            const int lado = pe ? 8 : 11, passo = pe ? 10 : 21;
            const auto neutro = ic::misturarAlfa(h.cor, painel, 2);
            for (int linha = 0; linha < 7; ++linha) {
                const int y = r.y + 36 + linha * (pe ? 15 : 13);
                p.texto(area(r.x + 7, y - 4, 28, lado + 8), semana[linha], 11, linha == diaSemana ? tinta : mudo, painel);
                for (int col = 0; col < tempo::kSemanasHabitos; ++col) {
                    const int atras = tempo::diasAtrasNaGrade(diaSemana, col, linha);
                    const int x = r.x + r.w - 11 - lado - (tempo::kSemanasHabitos - 1 - col) * passo;
                    const auto cor = atras < 0 ? futuro : h.recentes[atras] ? h.cor : neutro;
                    if (!atras) p.caixa(area(x - 1, y - 1, lado + 2, lado + 2), 4, tinta, painel);
                    p.caixa(area(x, y, lado, lado), 3, cor, atras == 0 ? tinta : painel);
                    if (atras < 0) p.caixa(area(x + 1, y + 1, lado - 2, lado - 2), 2, painel, cor);
                }
            }
        }

        std::string hora(int min) {
            if (min < 0) return "Qualquer hora";
            char s[8];
            std::snprintf(s, sizeof s, "%02d:%02d", min / 60, min % 60);
            return s;
        }

        struct Botao {
            Rect r;
            std::string texto;
            uint16_t cor, letras;
            bool ativo;
            void desenhar(Pintor& p) const {
                p.caixa(r, 13, cor, fundo, cor == painel);
                const bool icone = r.w > 200 && (texto == "Marcar feito" || texto == "Pular hoje" || texto == "Desfazer");
                if (!icone) {
                    p.texto(r, texto, r.w > 200 ? 21 : 17, letras, cor, TextAlign::Center);
                    return;
                }
                const int w = ui::sans::width(texto, ui::sans::font(21)), x = r.x + (r.w - w - 30) / 2, y = r.y + r.h / 2;
                if (texto == "Marcar feito")
                    ic::visto(p.g, x + 10, y, 10, letras, cor);
                else if (texto == "Desfazer")
                    ic::desfazer(p.g, x + 10, y, 10, letras, cor);
                else
                    ic::pular(p.g, x + 10, y, 10, letras, cor);
                p.texto(area(x + 30, r.y, w + 5, r.h), texto, 21, letras, cor);
            }
        };
    } // namespace

    void HabitosMesScreen::mostrarErroRegistro(ui::Context& ui, TempoScreen& tempo, Screen& screen) {
        const bool pendente = tempo.avisoRegistro() == TempoScreen::AvisoRegistro::Pendente;
        const bool cheia = tempo.avisoRegistro() == TempoScreen::AvisoRegistro::FilaCheia;
        const Rect tela = area(0, 0, ui.width(), ui.height());
        const Rect botao = area((tela.w - 144) / 2, tela.h / 2 + 24, 144, 48);
        pintar(ui, tela, static_cast<uint32_t>(tempo.avisoRegistro()), [&](Pintor& p) {
            p.texto(area(3, tela.h / 2 - 65, tela.w - 6, 38), pendente ? "Registro pendente" : cheia ? "Fila cheia" : "Registro não salvo",
                    17, tinta, fundo, TextAlign::Center);
            p.texto(area(3, tela.h / 2 - 22, tela.w - 6, 32), cheia ? "Sincronize o TickTick" : "Confira o cartão SD", 15, mudo, fundo, TextAlign::Center);
            p.caixa(botao, 13, painel, fundo);
            p.texto(botao, "Voltar", 17, lilas, painel, TextAlign::Center);
        });
        if (ui.firmTap(botao)) {
            if (pendente) screen = Screen::Home;
            else tempo.fecharAvisoRegistro();
            ui.invalidate();
        }
    }

    void HabitosMesScreen::abrirAviso(const tempo::Aviso& aviso, bool confirmarDose) {
        *this = {};
        vista_ = Vista::Detalhe;
        selecionado_ = aviso.habitoId;
        horario_ = aviso.minuto;
        aviso_ = aviso;
        diaConfirmacao_ = confirmarDose ? aviso.stamp : 0;
    }

    void HabitosMesScreen::back(Screen& screen) {
        aviso_.reset();
        if (vista_ == Vista::Cartoes) {
            screen = Screen::Home;
            return;
        }
        vista_ = Vista::Cartoes;
        selecionado_.clear();
        diaConfirmacao_ = 0;
        arrastando_ = moveu_ = false;
    }

    std::string_view HabitosMesScreen::carregarHistorico(fs::FS* fs, const tempo::Data& hoje, const tempo::MesHabitos& mes) {
        const int stamp = tempo::stampDe(hoje);
        const auto serial = tempo::diaSerial(hoje);
        const auto primeiroDoMes = tempo::diaSerial(mes.inicio);
        auto inicio = primeiroDoMes;
        if (vista_ == Vista::Cartoes) {
            if (modo_ == Modo::CincoDias) inicio = serial - 4;
            if (modo_ == Modo::Semanas) inicio = tempo::segundaDaSemana(hoje) - 7 * (tempo::kSemanasHabitos - 1);
        }
        if (dataCache_ != stamp || mesCache_ != mes_ || fsCache_ != fs) {
            cartoesExibidos_.clear();
            cartaoTocado_.clear();
            dias_.clear();
            erroMaisRecente_.reset();
            dataCache_ = stamp;
            mesCache_ = mes_;
            fsCache_ = fs;
            proximoDiaExclusivo_ = std::min(serial, primeiroDoMes + mes.dias);
            ++revisaoHistorico_;
        }
        // Lê de trás para a frente. Uma vista mais ampla continua de onde a anterior parou.
        if (proximoDiaExclusivo_ > inicio) {
            const auto dia = --proximoDiaExclusivo_;
            const auto data = tempo::dataDeSerial(dia);
            std::vector<tempo::EventoLog> eventos;
            if (TempoStorage::loadLogDay(*fs, data, eventos)) {
                if (!eventos.empty()) {
                    tempo::incluirDia(dias_, tempo::chaveMes(data) == mes_ ? data.dia : 0, int(serial - dia), eventos);
                    ++revisaoHistorico_;
                }
            } else if (!erroMaisRecente_ && fs->exists(tempo::caminhoLog(data).c_str())) {
                erroMaisRecente_ = dia;
            }
        }
        if (proximoDiaExclusivo_ > inicio) return "Lendo...";
        return erroMaisRecente_ && *erroMaisRecente_ >= inicio ? "Incompleto" : "";
    }

    void HabitosMesScreen::draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* fs,
                                const HabitosInfo& info) {
        if (!tempo.estadoCarregado() && fs) tempo.resumo(fs, nowMs);
        const auto agora = tempo::agora(nowMs);
        const auto& e = tempo.estadoTempo();
        const bool pe = ui.width() < ui.height();
        const Rect tela = area(0, 0, ui.width(), ui.height());
        const bool grade = vista_ == Vista::Cartoes;
        const bool cinco = modo_ == Modo::CincoDias, semanas = modo_ == Modo::Semanas;
        const Rect voltar = area(0, 0, 45, grade ? (pe ? 32 : 35) : 42);
        const int topoGrade = pe ? 68 : 36;
        const int topoCorpo = grade ? topoGrade + (cinco ? (pe ? 39 : 26) : 0) : (pe ? 82 : 42);
        const Rect corpo = area(0, topoCorpo, tela.w, tela.h - topoCorpo);
        if (!agora.valida || !fs) {
            fsCache_ = nullptr;
            diaConfirmacao_ = 0;
            pintar(ui, tela, !fs ? 1 : 2, [&](Pintor& p) {
                p.texto(voltar, "‹", 26, mudo, fundo, TextAlign::Center);
                p.texto(corpo, !fs ? "Insira o cartão SD" : "Ajuste o relógio", 17, tinta, fundo, TextAlign::Center);
            });
            if (ui.firmTap(voltar)) screen = Screen::Home;
            return;
        }
        std::optional<size_t> indiceAviso;
        if (aviso_) {
            if (vista_ != Vista::AvisoExpirado && tempo::stampDe(agora.data) == aviso_->stamp && e.dia.data == agora.data) {
                for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i) {
                    const auto& oc = e.dia.ocorrencias[i];
                    if (oc.habitoId == selecionado_ && oc.minuto == aviso_->minuto && oc.indice == aviso_->indiceNoDia) {
                        indiceAviso = i;
                        break;
                    }
                }
            }
            if (!indiceAviso) {
                vista_ = Vista::AvisoExpirado;
                diaConfirmacao_ = 0;
                pintar(ui, tela, hash("ocorrencia-indisponivel"), [&](Pintor& p) {
                    p.texto(voltar, "‹", 26, mudo, fundo, TextAlign::Center);
                    p.texto(corpo, "Horário indisponível", 17, tinta, fundo, TextAlign::Center);
                });
                if (ui.firmTap(voltar)) back(screen);
                return;
            }
        }
        const int mesAtual = tempo::chaveMes(agora.data);
        if (!mes_ || (dataCache_ && mes_ == tempo::chaveMes(tempo::dataDeStamp(dataCache_)))) mes_ = mesAtual;
        mes_ = std::min(mes_, mesAtual);
        const tempo::MesHabitos mes(mes_);
        const int hoje = mes_ == mesAtual ? agora.data.dia : 32;
        const auto statusHistorico = carregarHistorico(fs, agora.data, mes);
        std::vector<CartaoMes> cards;
        const auto adicionar = [&](const std::string& id, const std::string& nome, tempo::TipoHabito tipo,
                                   const std::string& icone, const std::string& cor) {
            auto it = dias_.find(id);
            cards.push_back({id, nome, tempo::iconeDoHabito(tipo, nome, icone), tom(cor, id),
                             it == dias_.end() ? 0 : it->second.dias,
                             it == dias_.end() ? tempo::DiasRecentes{} : it->second.recentes});
        };
        for (const auto& h : tempo.listaHabitos().habitos)
            adicionar(h.id, tempo::nomeParaTela(h.name), tempo::detectarTipo(h.name), h.iconRes, h.color);
        for (const auto& oc : e.dia.ocorrencias) {
            auto it = std::find_if(cards.begin(), cards.end(), [&](const auto& h) { return h.id == oc.habitoId; });
            if (it == cards.end()) {
                adicionar(oc.habitoId, oc.nome, oc.tipo, oc.iconRes, oc.color);
                it = cards.end() - 1;
            }
            if (hoje != 32 && e.dia.data == agora.data && oc.feita) {
                it->dias |= tempo::bitDia(hoje);
                it->recentes.set(0);
            }
        }
        for (const auto& [id, hist] : dias_) {
            if (!cinco && !semanas && !hist.dias) continue;
            if (cinco && (hist.recentes & tempo::DiasRecentes{0b11111}).none()) continue;
            if (std::none_of(cards.begin(), cards.end(), [&](const auto& h) { return h.id == id; }))
                adicionar(id, hist.nome.empty() ? "Hábito arquivado" : hist.nome, tempo::detectarTipo(hist.nome), {}, {});
        }
        auto escolhido = std::find_if(cards.begin(), cards.end(), [&](const auto& h) { return h.id == selecionado_; });
        if (vista_ == Vista::Detalhe && escolhido == cards.end()) {
            back(screen);
            ui.invalidate();
            return;
        }
        const int porPagina = semanas ? 2 : 3;
        const int passoLinha = cinco ? 63 : semanas ? 153 : 155;
        const int paginas = std::max(1, (int(cards.size()) + porPagina - 1) / porPagina);
        pagina_ = std::clamp(pagina_, 0, paginas - 1);
        const int maxRolagem = std::max(0, int(cards.size()) * passoLinha - corpo.h);
        if (grade && pe) rolagem_ = std::clamp(rolagem_, 0, maxRolagem);
        if (vista_ == Vista::Cartoes) {
            if (const auto* t = ui.touch()) {
                if (ui::hasTouch(*t, ui::TouchStart)) {
                    arrastando_ = ui::contains(corpo, t->x, t->y);
                    toqueX_ = t->x;
                    toqueY_ = t->y;
                    rolagemInicial_ = rolagem_;
                    moveu_ = false;
                    // O histórico pode ter mudado neste quadro. O dedo viu a grade do quadro anterior.
                    cartaoTocado_.clear();
                    for (const auto& alvo : cartoesExibidos_) {
                        if (ui::contains(alvo.area, t->x, t->y)) {
                            cartaoTocado_ = alvo.id;
                            break;
                        }
                    }
                }
                if (ui::hasTouch(*t, ui::TouchCancel)) {
                    arrastando_ = false;
                    moveu_ = true;
                }
                if (arrastando_) {
                    int dx = int(t->x) - toqueX_, dy = int(t->y) - toqueY_;
                    moveu_ |= std::abs(dx) > 10 || std::abs(dy) > 10;
                    if (pe && moveu_) rolagem_ = std::clamp(rolagemInicial_ - dy, 0, maxRolagem);
                    if (ui::hasTouch(*t, ui::TouchRelease)) {
                        if (!pe && std::abs(dx) > 32 && std::abs(dx) > std::abs(dy))
                            pagina_ = std::clamp(pagina_ + (dx < 0 ? 1 : -1), 0, paginas - 1);
                        arrastando_ = false;
                    }
                }
            }
        }
        const bool detalhe = vista_ == Vista::Detalhe, seletor = vista_ == Vista::Meses;
        const bool diaAcertado = e.dia.data == agora.data && hoje != 32;
        const auto idx =
            aviso_ ? indiceAviso
                   : detalhe && diaAcertado ? tempo::horarioDoCartao(e, escolhido->id, agora.minutoDoDia, horario_) : std::nullopt;
        const auto regra = idx ? tempo::montarCartao(e, agora.minutoDoDia, idx) : tempo::Cartao{};
        const bool med = idx && regra.confirmar2Toques;
        const int stamp = tempo::stampDe(agora.data);
        if (!med || !regra.podeFeito || diaConfirmacao_ != stamp || horario_ != e.dia.ocorrencias[*idx].minuto)
            diaConfirmacao_ = 0;
        desfazer_ &= tempo::desfazerDisponivel(nowMs, marcouMs_) && tempo.podeDesfazerHabito();
        adiou_ &= nowMs - adiouMs_ < 6000;
        const Rect dataBotao = area(47, 6, 84, 22);
        const Rect toqueMes = area(46, 0, 90, pe ? 32 : 35);
        const Rect anterior = pe ? area(8, 42, 60, 38) : area(510, 0, 48, grade ? 35 : 42);
        const Rect seguinte = pe ? area(104, 42, 60, 38) : area(592, 0, 48, grade ? 35 : 42);
        const Rect faixaHora = pe ? area(4, 44, 164, 36) : area(277, 0, 200, 42);
        std::string titulo = detalhe ? escolhido->nome : seletor ? "Meses" : "Hábitos";
        std::string periodo = std::string(curtos[mes.inicio.mes - 1]) + " " + std::to_string(mes.inicio.ano);
        if (grade && (cinco || semanas)) {
            const auto inicio = tempo::dataDeSerial(tempo::diaSerial(agora.data) - 4);
            periodo = cinco ? std::to_string(inicio.dia) + (inicio.mes != agora.data.mes ? std::string(" ") + curtos[inicio.mes - 1] : "") + "–" : "";
            periodo += std::to_string(agora.data.dia) + " " + curtos[agora.data.mes - 1];
        }
        const bool varios = !aviso_ && idx && e.dia.ocorrencias[*idx].total > 1;
        std::string rotuloHora = idx ? hora(e.dia.ocorrencias[*idx].minuto) + (varios ? "  ›" : "") : "";
        const std::string sync = info.sincronizando ? "Sincronizando" : (!info.erroSync.empty() ? "Erro de sync" : "");
        const std::string_view avisoTopo = sync.empty() && (cinco || semanas) ? statusHistorico : std::string_view(sync);
        uint32_t cab = hash(titulo, hash(periodo, hash(sync, hash(rotuloHora))));
        cab = hash(avisoTopo, ui::Context::combine(cab, cards.size()));
        cab = ui::Context::combine(cab, (anoSeletor_ * 64 + pagina_) * 64 + paginas);
        cab = ui::Context::combine(cab, tempo::stampDe(agora.data));
        const Rect topo = area(0, 0, tela.w, corpo.y);
        pintar(ui, topo, cab, [&](Pintor& p) {
            if (grade) p.texto(area(6, 0, 22, 34), "‹", 21, tinta, fundo, TextAlign::Center);
            else p.texto(voltar, "‹", 26, mudo, fundo, TextAlign::Center);
            const Rect nome = grade ? area(151, 0, 93, 35) : area(37, 0, pe ? 132 : (detalhe ? 240 : 110), 40);
            if (detalhe && ui::sans::width(titulo, ui::sans::font(21)) > nome.w) {
                const auto layout = ui.prepareText(nome, titulo, 15, TextAlign::Left, 2);
                ui.drawText(p.g, layout, tinta, p.dx, p.dy, fundo);
            } else if (!(grade && pe)) p.texto(nome, titulo, grade ? 17 : 21, tinta, fundo);
            if (seletor) {
                p.texto(anterior, "‹", 21, anoSeletor_ > tempo::kAnoMinimoValido ? lilas : mudo, fundo, TextAlign::Center);
                p.texto(seguinte, "›", 21, anoSeletor_ < agora.data.ano ? lilas : mudo, fundo, TextAlign::Center);
                p.texto(pe ? area(53, 42, 66, 38) : area(544, 0, 65, 42), std::to_string(anoSeletor_), 15, tinta, fundo,
                        TextAlign::Center);
            } else if (detalhe) {
                p.texto(faixaHora, pe && hoje == 32 ? periodo : rotuloHora, 15, varios ? lilas : mudo, fundo,
                        TextAlign::Center);
                if (!pe)
                    p.texto(area(478, 0, 150, 40),
                            hoje == 32 ? periodo : "Hoje, " + std::to_string(hoje) + " " + curtos[mes.inicio.mes - 1], 15,
                            mudo, fundo, TextAlign::Right);
            } else {
                if (modo_ == Modo::Mes) {
                    p.caixa(dataBotao, 10, chip, fundo);
                    p.texto(area(49, 6, 65, 22), periodo, 13, tinta, chip, TextAlign::Center);
                    ic::phosphor(p.g, icons::phosphor::Id::CaretDown, 120, 17, 12, lilas, chip);
                } else p.texto(area(47, 0, 100, 34), periodo, 13, mudo, fundo);
                const auto primeiraAba = aba(0, pe), ultimaAba = aba(2, pe);
                p.caixa(area(primeiraAba.x, primeiraAba.y + 5, ultimaAba.x + ultimaAba.w - primeiraAba.x, 24),
                        12, chip, fundo);
                for (int i = 0; i < 3; ++i) {
                    const auto alvo = aba(i, pe);
                    const auto rotulo = area(alvo.x, alvo.y + 5, alvo.w, 24);
                    const bool ativo = i == static_cast<int>(modo_);
                    const auto base = ativo ? rgb565(70, 64, 91) : chip;
                    if (ativo) p.caixa(area(rotulo.x + 4, rotulo.y + 3, rotulo.w - 8, 18), 9, base, chip);
                    p.texto(rotulo, abas[i], 13, ativo ? tinta : mudo, base, TextAlign::Center);
                }
                if (cinco)
                    cabecalhoCincoDias(p, area(4, topoGrade, tela.w - 8, corpo.y - topoGrade - 1), agora.data, cards.size(), pe);
                if (!pe && paginas > 1) {
                    if (pagina_ > 0) p.texto(anterior, "‹", 21, lilas, fundo, TextAlign::Center);
                    p.texto(area(548, 0, 59, 42), std::to_string(pagina_ + 1) + " de " + std::to_string(paginas), 13, mudo,
                            fundo, TextAlign::Center);
                    if (pagina_ + 1 < paginas) p.texto(seguinte, "›", 21, lilas, fundo, TextAlign::Center);
                }
                if (!avisoTopo.empty())
                    p.texto(pe ? area(145, 0, 25, 32) : area(219, 0, 99, 35),
                            pe ? (avisoTopo == "Incompleto" || !info.erroSync.empty() ? "!" : "…") : avisoTopo,
                            13, lilas, fundo, TextAlign::Center);
            }
        });
        if (ui.firmTap(voltar)) {
            back(screen);
            return;
        }
        if (seletor) {
            const int colunas = pe ? 2 : 6, passoX = tela.w / colunas, passoY = pe ? 74 : 63;
            if (ui.firmTap(anterior, anoSeletor_ > tempo::kAnoMinimoValido)) --anoSeletor_;
            if (ui.firmTap(seguinte, anoSeletor_ < agora.data.ano)) ++anoSeletor_;
            const auto celula = [&](int i) {
                return area((i % colunas) * passoX + 4, corpo.y + (i / colunas) * passoY + 3, passoX - 8, passoY - 6);
            };
            pintar(ui, corpo, ui::Context::combine(mes_, anoSeletor_ * 12 + mesAtual), [&](Pintor& p) {
                for (int i = 0; i < 12; ++i) {
                    const int chave = anoSeletor_ * 12 + i;
                    const auto bg = chave == mes_ ? lilas : painel;
                    const Rect b = celula(i);
                    p.caixa(b, 12, bg, fundo);
                    p.texto(b, curtos[i], 17,
                            chave == mes_       ? fundo
                            : chave <= mesAtual ? tinta
                                                : mudo,
                            bg, TextAlign::Center);
                }
            });
            for (int i = 0; i < 12; ++i) {
                const int chave = anoSeletor_ * 12 + i;
                if (ui.firmTap(celula(i), chave <= mesAtual)) {
                    mes_ = chave;
                    vista_ = Vista::Cartoes;
                    return;
                }
            }
            return;
        }
        if (!detalhe) {
            for (int i = 0; i < 3; ++i) {
                if (ui.firmTap(aba(i, pe), i != static_cast<int>(modo_))) {
                    modo_ = static_cast<Modo>(i);
                    if (modo_ != Modo::Mes) mes_ = mesAtual;
                    pagina_ = rolagem_ = 0;
                    cartoesExibidos_.clear();
                    cartaoTocado_.clear();
                    return;
                }
            }
            if (ui.firmTap(toqueMes, modo_ == Modo::Mes)) {
                cartoesExibidos_.clear();
                anoSeletor_ = mes.inicio.ano;
                vista_ = Vista::Meses;
                return;
            }
            if (!pe) {
                if (ui.firmTap(anterior, pagina_ > 0)) --pagina_;
                if (ui.firmTap(seguinte, pagina_ + 1 < paginas)) ++pagina_;
            }
            const auto rectCartao = [&](int i) {
                if (cinco)
                    return area(4, corpo.y + (pe ? passoLinha * i - rolagem_ : 35 * (i % porPagina)),
                                tela.w - 8, pe ? passoLinha - 1 : 34);
                if (pe) return area(semanas ? 5 : 7, corpo.y + passoLinha * i - rolagem_, semanas ? 161 : 155, 148);
                if (semanas) return area(8 + 316 * (i % porPagina), corpo.y, 308, 131);
                return area(8 + 210 * (i % porPagina), corpo.y, 204, 130);
            };
            const int inicio = pe ? rolagem_ / passoLinha : pagina_ * porPagina;
            const int fim = std::min(int(cards.size()), inicio + (pe ? (corpo.h + passoLinha - 1) / passoLinha + 1 : porPagina));
            uint32_t sig = ui::Context::combine(tempo::stampDe(agora.data), mes_);
            sig = ui::Context::combine(sig, revisaoHistorico_);
            sig = hash(statusHistorico, ui::Context::combine(sig, rolagem_ + pagina_ * 65536 + int(cards.size())));
            for (int i = inicio; i < fim; ++i) {
                const auto& h = cards[i];
                sig = hash(h.nome, ui::Context::combine(sig, h.dias));
                sig = ui::Context::combine(sig, h.cor + 65536U * unsigned(h.icone));
            }
            pintar(ui, corpo, sig, [&](Pintor& p) {
                for (int i = inicio; i < fim; ++i) {
                    const Rect r = rectCartao(i);
                    if (r.y + r.h <= corpo.y || r.y >= tela.h) continue;
                    if (cinco) linhaCincoDias(p, r, cards[i], pe);
                    else if (semanas) cartaoSemanas(p, r, cards[i], tempo::diaDaSemana(agora.data), pe);
                    else cartao(p, r, cards[i], mes, hoje, pe, statusHistorico);
                }
                if (pe && maxRolagem) {
                    const int altura = corpo.h * corpo.h / (maxRolagem + corpo.h);
                    p.caixa(area(166, corpo.y + (corpo.h - altura) * rolagem_ / maxRolagem, 3, altura), 1, mudo, fundo);
                }
                if (cards.empty()) {
                    const auto mensagem = statusHistorico == "Lendo..." ? "Lendo histórico..."
                                          : !statusHistorico.empty()   ? "Histórico incompleto"
                                                                       : "Sem hábitos";
                    p.texto(corpo, mensagem, 17, mudo, fundo, TextAlign::Center);
                }
            });
            cartoesExibidos_.resize(fim - inicio);
            for (int i = inicio; i < fim; ++i) {
                const auto alvo = ui::intersection(corpo, rectCartao(i));
                auto& exibido = cartoesExibidos_[i - inicio];
                exibido.area = alvo;
                exibido.id = cards[i].id;
                if (alvo.h > 0 && ui.firmTap(alvo) && !moveu_ && cartaoTocado_ == exibido.id) {
                    selecionado_ = cards[i].id;
                    cartoesExibidos_.clear();
                    horario_.reset();
                    desfazer_ = adiou_ = false;
                    vista_ = Vista::Detalhe;
                    return;
                }
            }
            return;
        }

        const auto& h = *escolhido;
        if (varios && ui.firmTap(faixaHora)) {
            for (size_t n = 1; n <= e.dia.ocorrencias.size(); ++n) {
                const auto& oc = e.dia.ocorrencias[(*idx + n) % e.dia.ocorrencias.size()];
                if (oc.habitoId == h.id) {
                    horario_ = oc.minuto;
                    break;
                }
            }
            desfazer_ = adiou_ = false;
            diaConfirmacao_ = 0;
            return;
        }
        const bool feito = idx && e.dia.ocorrencias[*idx].feita;
        std::string principal = hoje == 32                                ? "Histórico"
                                : !idx                                    ? "Sem horário hoje"
                                : feito                                   ? "Feito hoje"
                                : regra.modo == tempo::ModoCartao::Pulado ? "Pulado hoje"
                                : regra.podeFeito
                                    ? (med ? (diaConfirmacao_ ? "Sim, tomei agora" : "Registrar dose") : "Marcar feito")
                                : regra.modo == tempo::ModoCartao::Proximo ? "Ainda cedo"
                                                                           : "Fora da janela";
        std::string secundaria = diaConfirmacao_ ? "Cancelar"
                                 : desfazer_     ? "Desfazer"
                                 : med           ? (adiou_ ? "Lembrete adiado" : "Adiar 15 min")
                                 : varios        ? "Pular horário"
                                                 : "Pular hoje";
        const bool podeMarcar = idx && regra.podeFeito && !desfazer_;
        const bool podeSec = desfazer_ || (idx && (med ? regra.podeFeito : regra.podePular));
        const Rect quadro = pe ? area(7, 96, 158, 235) : area(8, 42, 301, 125);
        const Botao prim{pe ? area(7, 429, 158, 78) : area(317, 42, 315, 61), principal, podeMarcar ? h.cor : painel,
                         podeMarcar ? fundo : mudo, podeMarcar};
        const Botao sec{pe ? area(7, 519, 158, 65) : area(317, 110, 315, 57), secundaria, painel, tinta, podeSec};
        uint32_t sig = hash(statusHistorico, hash(principal, hash(secundaria, hash(h.nome, h.dias))));
        sig = ui::Context::combine(sig, tempo::stampDe(agora.data));
        sig = ui::Context::combine(sig, mes_ * 16 + sec.ativo * 2 + prim.ativo);
        sig = ui::Context::combine(sig, h.cor + 65536U * unsigned(h.icone));
        pintar(ui, corpo, sig, [&](Pintor& p) {
            p.caixa(quadro, 13, painel, fundo, true);
            int x = quadro.x, y = quadro.y;
            badge(p, h, x + 10, y + 8);
            p.texto(area(x + 46, y + 6, pe ? 104 : 116, 32), meses[mes.inicio.mes - 1], 17, tinta, painel);
            calendario(p, mes, h, hoje, pe ? x + 13 : x + 145, pe ? y + 65 : y + 30, pe ? 20 : 19, pe ? 84 : 88, 11);
            if (statusHistorico.empty()) {
                p.texto(area(x + 15, y + (pe ? 147 : 50), 100, 39), std::to_string(std::popcount(h.dias)), 28, h.cor,
                        painel);
                p.texto(area(x + 15, y + (pe ? 188 : 92), 115, 24), diasFeitos(h.dias), 15, mudo, painel);
            } else
                p.texto(area(x + 15, y + (pe ? 147 : 65), 115, 30), statusHistorico, 15, mudo, painel);
            prim.desenhar(p);
            if (sec.ativo) sec.desenhar(p);
        });
        // A sincronização, a virada do dia ou o fim do Desfazer podem mudar a ação enquanto o dedo está apoiado.
        uint32_t acao = hash(principal, hash(secundaria, hash(h.id, stamp)));
        if (idx) acao = ui::Context::combine(acao, e.dia.ocorrencias[*idx].minuto + 2);
        if (const auto* t = ui.touch(); t && ui::hasTouch(*t, ui::TouchStart)) toqueAcao_ = acao;
        if (ui.firmTap(prim.r, prim.ativo) && toqueAcao_ == acao) {
            horario_ = e.dia.ocorrencias[*idx].minuto;
            if (med && !diaConfirmacao_) {
                diaConfirmacao_ = stamp;
                return;
            }
            diaConfirmacao_ = 0;
            if (tempo.marcarHabito(nowMs, fs, *idx)) {
                marcouMs_ = nowMs;
                desfazer_ = true;
            }
        } else if (ui.firmTap(sec.r, sec.ativo) && toqueAcao_ == acao) {
            if (diaConfirmacao_)
                diaConfirmacao_ = 0;
            else if (desfazer_) {
                tempo.desfazerHabito(nowMs, fs);
                desfazer_ = false;
            } else if (med) {
                tempo.adiarHabito(nowMs, *idx);
                adiouMs_ = nowMs;
                adiou_ = true;
            } else {
                horario_ = e.dia.ocorrencias[*idx].minuto;
                if (tempo.pularHabito(nowMs, fs, *idx)) {
                    marcouMs_ = nowMs;
                    desfazer_ = true;
                }
            }
        }
    }
} // namespace screens
