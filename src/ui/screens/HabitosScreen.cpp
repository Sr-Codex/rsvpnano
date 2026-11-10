#include "ui/screens/HabitosScreen.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <vector>

#include "storage/tempo/TempoStorage.h"
#include "tempo/HabitosVista.h"
#include "tempo/Parametros.h"
#include "tempo/Relogio.h"
#include "ui/Rgb565.h"

namespace screens {
    namespace {
        using ui::Rect;
        using ui::TextAlign;
        using namespace ui::themes;

        constexpr uint16_t kLaranja = rgb565(255, 120, 50);
        constexpr uint16_t kAmbar = rgb565(255, 196, 80);
        constexpr uint16_t kCarmim = rgb565(235, 50, 90);
        constexpr uint16_t kVerde = rgb565(25, 158, 112);
        constexpr uint16_t kVerdeClaro = rgb565(120, 230, 170);
        constexpr uint16_t kLinha = rgb565(50, 52, 50);
        constexpr uint16_t kEscuro = rgb565(40, 42, 41);        // botao secundario
        constexpr uint16_t kTinta = rgb565(12, 13, 14);         // texto sobre laranja / visto
        constexpr uint16_t kLinhaSel = rgb565(34, 40, 37);      // linha escolhida na lista
        constexpr uint16_t kLinhaAtraso = rgb565(48, 34, 26);   // linha em foco e atrasada
        constexpr uint16_t kAvisoFundo = rgb565(44, 24, 30);
        constexpr uint16_t kAvisoFundoGeral = rgb565(46, 34, 24);

        using Desenho = std::function<void(Arduino_GFX&, int, int)>;

        // Regiao opaca com textos proprios; so repinta quando a assinatura muda (mesma ideia do Home/Perfil).
        struct Bloco {
            ui::Context& ui;
            Rect area;
            uint16_t cor;
            int raio;
            uint32_t sig;
            struct Item {
                ui::TextLayout layout;
                uint16_t cor;
                uint16_t fundo;
            };
            std::vector<Item> itens;

            Bloco(ui::Context& u, Rect a, uint16_t fundo, int r, uint32_t semente)
                : ui(u), area(a), cor(fundo), raio(r), sig(ui::Context::combine(semente, fundo)) {}
            Bloco(ui::Context& u, Rect a, ColorRole fundo, int r, uint32_t semente) : Bloco(u, a, u.color(fundo), r, semente) {}

            struct Q {
                int x, y, w, h;
            };
            void texto(Q q, std::string_view s, uint8_t tamanho, uint16_t cor, TextAlign alinhamento = TextAlign::Start) {
                // A fonte proporcional so e usada se a caixa tiver a altura do corpo; tamanhos < 10 cairiam na fonte de pixels.
                tamanho = std::max<uint8_t>(10, tamanho);
                const int minima = tamanho >= 39 ? 50 : tamanho >= 28 ? 36 : tamanho >= 26 ? 33 : tamanho >= 21 ? 27
                                 : tamanho >= 17 ? 22 : tamanho >= 15 ? 20 : tamanho >= 14 ? 19 : tamanho >= 13 ? 17 : 15;
                const Rect r{int16_t(q.x), int16_t(q.y), int16_t(q.w), int16_t(std::max(q.h, minima))};
                sig = ui::Context::signature(s, ui::Context::combine(sig, tamanho * 65536U + cor));
                itens.push_back({ui.prepareText(r, s, tamanho, alinhamento, 1), cor, this->cor});
            }
            // Texto sobre um botao (fundo proprio): o fundo do antialiasing e o do botao.
            void textoEm(Q q, std::string_view s, uint8_t tamanho, uint16_t cor, uint16_t fundo, TextAlign a = TextAlign::Center) {
                texto(q, s, tamanho, cor, a);
                itens.back().fundo = fundo;
            }
            void marca(uint32_t valor) { sig = ui::Context::combine(sig, valor); }

            void desenhar(const Desenho& extra = {}) {
                if (!ui.redraw(area, sig, true)) return;
                ui.paint(area, [&](Arduino_GFX& g, Rect alvo) {
                    g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, ui.color(Background));
                    if (raio > 0) g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, raio, cor);
                    else g.fillRect(alvo.x, alvo.y, alvo.w, alvo.h, cor);
                    if (extra) extra(g, alvo.x - area.x, alvo.y - area.y);
                    for (const auto& item : itens) ui.drawText(g, item.layout, item.cor, alvo.x - area.x, alvo.y - area.y, item.fundo);
                });
            }
        };

        // ------------------------------------------------------------------ formatacao
        std::string hhmm(int minuto) {
            char t[8];
            std::snprintf(t, sizeof t, "%02d:%02d", std::clamp(minuto, 0, 1439) / 60, std::clamp(minuto, 0, 1439) % 60);
            return t;
        }

        std::string duracao(int minutos) {   // "20 min" / "1h20"
            if (minutos < 60) return std::to_string(minutos) + " min";
            char t[16];
            std::snprintf(t, sizeof t, "%dh%02d", minutos / 60, minutos % 60);
            return t;
        }

        std::string dataTexto(const tempo::Data& d, bool valida) {
            if (!valida) return {};
            static constexpr const char* kSemana[] = {"Seg", "Ter", "Qua", "Qui", "Sex", "Sáb", "Dom"};
            static constexpr const char* kMes[] = {"jan", "fev", "mar", "abr", "mai", "jun", "jul", "ago", "set", "out", "nov", "dez"};
            char t[32];
            std::snprintf(t, sizeof t, "%s, %d %s", kSemana[tempo::diaDaSemana(d)], d.dia, kMes[std::clamp(d.mes, 1, 12) - 1]);
            return t;
        }

        std::string numero(double v) {
            char t[24];
            std::snprintf(t, sizeof t, "%.0f", std::round(v));
            return t;
        }

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

        size_t letras(const std::string& texto) {
            size_t n = 0;
            for (char c : texto)
                if ((static_cast<unsigned char>(c) & 0xC0) != 0x80) ++n;
            return n;
        }

        // ------------------------------------------------------------------ icones desenhados (a fonte nao tem emoji/visto)
        void anel(Arduino_GFX& g, int cx, int cy, int r, uint16_t cor) {
            g.drawCircle(cx, cy, r, cor);
            g.drawCircle(cx, cy, r - 1, cor);
        }

        void traco(Arduino_GFX& g, int x1, int y1, int x2, int y2, uint16_t cor) {
            g.drawLine(x1, y1, x2, y2, cor);
            if (std::abs(x2 - x1) >= std::abs(y2 - y1)) g.drawLine(x1, y1 + 1, x2, y2 + 1, cor);
            else g.drawLine(x1 + 1, y1, x2 + 1, y2, cor);
        }

        void visto(Arduino_GFX& g, int cx, int cy, int r, uint16_t cor) {
            const double k = r / 7.0;
            const auto P = [&](double dx, double dy) { return std::pair<int, int>{cx + int(std::lround(dx * k)), cy + int(std::lround(dy * k))}; };
            const auto a = P(-3.5, 0.2), b = P(-1.0, 2.8), c = P(3.8, -2.8);
            traco(g, a.first, a.second, b.first, b.second, cor);
            traco(g, b.first, b.second, c.first, c.second, cor);
        }

        void marcaFeito(Arduino_GFX& g, int cx, int cy, int r, uint16_t cor) {
            g.fillCircle(cx, cy, r, cor);
            visto(g, cx, cy, r, kTinta);
        }

        // Cápsula (medicação): contorno + metade cheia.
        void pilula(Arduino_GFX& g, int cx, int cy, double s, uint16_t cor) {
            const int w = int(14 * s), h = std::max(4, int(7 * s)), x0 = cx - w / 2, y0 = cy - h / 2;
            g.drawRoundRect(x0, y0, w, h, h / 2, cor);
            g.fillRoundRect(cx, y0, w - w / 2, h, h / 2, cor);
            g.fillRect(cx, y0, w / 4, h, cor);
        }

        // Anel de progresso (verde) de 0..1, comecando no topo.
        void anelProgresso(Arduino_GFX& g, int cx, int cy, int r, int larg, double frac, uint16_t cor, uint16_t trilha) {
            constexpr double kRad = 3.14159265358979 / 180.0;
            const int meio = std::max(1, larg / 2);
            for (int a = 0; a < 360; a += 3) {
                const double rad = (a - 90) * kRad;
                g.fillCircle(cx + int(std::lround(r * std::cos(rad))), cy + int(std::lround(r * std::sin(rad))), meio, trilha);
            }
            const int ate = int(std::lround(360 * std::clamp(frac, 0.0, 1.0)));
            for (int a = 0; a <= ate; a += 3) {
                const double rad = (a - 90) * kRad;
                g.fillCircle(cx + int(std::lround(r * std::cos(rad))), cy + int(std::lround(r * std::sin(rad))), meio, cor);
            }
        }

        // Marcador da ocorrencia conforme a fase.
        void marcador(Arduino_GFX& g, int cx, int cy, int r, tempo::FaseOcorrencia fase, bool medicacao) {
            using F = tempo::FaseOcorrencia;
            switch (fase) {
            case F::Feita: marcaFeito(g, cx, cy, r, medicacao ? kCarmim : kVerde); break;
            case F::FeitaAtrasada: marcaFeito(g, cx, cy, r, kAmbar); break;
            case F::Atrasada:
            case F::Agora: {
                const uint16_t c = medicacao ? kCarmim : kLaranja;
                anel(g, cx, cy, r, c);
                g.fillCircle(cx, cy, std::max(2, int(r * 0.45)), c);
                break;
            }
            case F::Esquecida:
            case F::Perdida: {
                anel(g, cx, cy, r, kCarmim);
                const int d = std::max(2, r / 2);
                traco(g, cx - d, cy - d, cx + d, cy + d, kCarmim);
                traco(g, cx - d, cy + d, cx + d, cy - d, kCarmim);
                break;
            }
            case F::Pulada:
                anel(g, cx, cy, r, rgb565(120, 122, 120));
                g.drawFastHLine(cx - r / 2, cy, r, rgb565(120, 122, 120));
                break;
            default: anel(g, cx, cy, r, medicacao ? kCarmim : rgb565(140, 142, 140)); break;
            }
        }

        // Texto da direita (valor/estado) e cor para a linha da lista.
        std::pair<std::string, uint16_t> direita(const tempo::VistaOcorrencia& v, uint16_t mudo) {
            using F = tempo::FaseOcorrencia;
            switch (v.fase) {
            case F::Feita: return {"+" + numero(v.cr) + " cr", kVerde};
            case F::FeitaAtrasada: return {"só XP", kAmbar};
            case F::Esquecida: return {"esquecido", kCarmim};
            case F::Perdida: return {"perdido", kCarmim};
            case F::Pulada: return {"pulado", mudo};
            case F::Atrasada: return {"atrasado", kLaranja};
            default: return {numero(v.cr) + " cr", mudo};
            }
        }

        const tempo::ValorHabito& valorDo(tempo::TipoHabito tipo) {
            return tempo::param::kValoresTipoHabito[static_cast<size_t>(tipo)];
        }
    } // namespace

    // ====================================================================== fila (rodape)
    void HabitosScreen::carregarFila(fs::FS* filesystem) {
        filaTotal_ = 0;
        filaParados_ = 0;
        if (filesystem == nullptr) return;
        for (const auto& item : TempoStorage::loadQueue(*filesystem)) {
            ++filaTotal_;
            if (!tempo::devePreEnviar(item)) ++filaParados_;
        }
    }

    // ====================================================================== draw
    Action HabitosScreen::draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                               const HabitosInfo& info) {
        const bool entrada = !aberta_ || nowMs - ultimoDesenhoMs_ > 800;
        ultimoDesenhoMs_ = nowMs;
        if (entrada) {
            if (!tempo.estadoCarregado()) tempo.resumo(filesystem, nowMs);
            aberta_ = true;
            selecionado_.reset();
            rolagemManual_ = false;
            tracking_ = false;
            ultimaFocoLinha_ = -1;
            adiouEmMs_ = nowMs - 10000;
            carregarFila(filesystem);
            ultimaFilaMs_ = nowMs;
        } else if (nowMs - ultimaFilaMs_ > 4000) {
            ultimaFilaMs_ = nowMs;
            carregarFila(filesystem);
        }
        const auto agora = tempo::agora(nowMs);
        const tempo::Estado& estado = tempo.estadoTempo();
        const int minuto = agora.valida ? agora.minutoDoDia : estado.dia.minutoAtual;
        const tempo::Data data = agora.valida ? agora.data : estado.dia.data;
        if (ui.width() < ui.height()) desenharEmPe(ui, screen, estado, minuto, data);
        else desenharDeitada(ui, nowMs, screen, tempo, filesystem, info, minuto, data, agora.valida);
        return Action::None;
    }

    // ====================================================================== deitada (640 x 172)
    void HabitosScreen::desenharDeitada(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                                        const HabitosInfo& info, int minuto, const tempo::Data& data, bool horaValida) {
        using tempo::ModoCartao;
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        const tempo::Estado& estado = tempo.estadoTempo();
        const auto vistas = tempo::ocorrenciasDoDia(estado, minuto);
        const size_t n = vistas.size();
        const auto totais = tempo::totaisDoDia(estado);
        if (selecionado_ && *selecionado_ >= n) selecionado_.reset();
        tempo::Cartao cartao = tempo::montarCartao(estado, minuto, selecionado_);
        const bool temFoco = cartao.modo != ModoCartao::SemHabitos && cartao.modo != ModoCartao::DiaCompleto;

        // lista: recentra no item em foco quando o foco muda (e o usuario nao rolou)
        const int foco = temFoco ? int(cartao.indice) : -1;
        if (foco != ultimaFocoLinha_ && !rolagemManual_) rolagem_ = tempo::rolagemPara(n, temFoco ? cartao.indice : 0);
        ultimaFocoLinha_ = foco;
        rolagem_ = tempo::rolar(rolagem_, 0, n);

        // ---- gesto na lista: arrastar na vertical rola
        if (const auto* t = ui.touch()) {
            if (hasTouch(*t, ui::TouchStart)) {
                tracking_ = t->x >= 306 && t->y >= 26;
                x0_ = int16_t(t->x);
                y0_ = int16_t(t->y);
            }
            if (hasTouch(*t, ui::TouchCancel)) tracking_ = false;
            if (tracking_ && hasTouch(*t, ui::TouchRelease)) {
                tracking_ = false;
                const int dx = int(t->x) - x0_, dy = int(t->y) - y0_;
                if (std::abs(dy) >= 20 && std::abs(dy) > std::abs(dx)) {
                    const int linhas = std::max(1, (std::abs(dy) + 12) / 25);
                    rolagem_ = tempo::rolar(rolagem_, dy < 0 ? linhas : -linhas, n);
                    rolagemManual_ = true;
                }
            }
        }

        // ---- barra superior
        {
            Bloco barra(ui, {0, 0, 640, 26}, Background, 0, 1);
            barra.texto({14, 5, 90, 16}, "‹ HÁBITOS", 10, cMudo);
            std::string quando = dataTexto(data, horaValida);
            if (horaValida) quando += " · " + hhmm(minuto);
            barra.texto({108, 4, 140, 16}, quando, 11, cTexto);
            const int base = totais.total - totais.puladas;
            const double frac = base > 0 ? double(totais.feitas) / base : 0.0;
            barra.texto({480, 4, 70, 16}, std::to_string(totais.feitas) + "/" + std::to_string(base), 11, kVerde);
            barra.texto({500, 5, 126, 16}, "+" + numero(totais.cr) + " cr hoje", 10, kAmbar, TextAlign::Right);
            barra.marca(uint32_t(frac * 1000));
            barra.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                g.fillRoundRect(250 + dx, 10 + dy, 220, 6, 3, kLinha);
                if (frac > 0) g.fillRoundRect(250 + dx, 10 + dy, std::max(6, int(220 * frac)), 6, 3, kVerde);
            });
            if (ui.tap({0, 0, 112, 28})) screen = Screen::Home;
        }

        // ---- cartao
        const Rect areaCartao{10, 26, 290, 128};
        const Rect areaPrimario{24, 108, 172, 34}, areaSecundario{204, 108, 84, 34};
        bool primario = false, secundario = false;
        {
            std::string titulo, nome, l1, l2, botao1, botao2;
            uint16_t corTitulo = kAmbar, corL1 = cMudo, corBotao = kLaranja, corTextoBotao = kTinta;
            bool medicacao = false, anelDia = false, botaoHabilitado = false, temBotao2 = false;
            const auto vista = temFoco ? &vistas[cartao.indice] : nullptr;
            const auto& oc = estado.dia.ocorrencias;
            if (vista) {
                const auto& v = *vista;
                nome = v.nome;
                medicacao = cartao.confirmar2Toques;
                const auto& val = valorDo(v.tipo);
                const int antes = v.tipo == tempo::TipoHabito::Descanso ? 15 : val.janelaMin;
                const std::string janela = v.minuto >= 0 ? "janela " + hhmm(v.minuto - antes) + "–" + hhmm(v.minuto + val.janelaMin) : "";
                const std::string ganho = "no horário: +" + numero(val.cr) + " cr · +" + numero(val.xp) + " XP";
                if (medicacao) { corBotao = kCarmim; corTextoBotao = cTexto; }
                switch (cartao.modo) {
                case ModoCartao::Proximo:
                    titulo = "PRÓXIMO · em " + duracao(cartao.minutosAte);
                    l1 = v.minuto >= 0 ? hhmm(v.minuto) + " · " + janela : "qualquer hora";
                    l2 = ganho;
                    break;
                case ModoCartao::Agora:
                    titulo = medicacao ? "AGORA · confirme" : "AGORA";
                    corTitulo = medicacao ? kCarmim : kLaranja;
                    if (medicacao) {
                        l1 = hhmm(v.minuto) + " · você tomou agora (" + hhmm(minuto) + ")?";
                        corL1 = cTexto;
                        l2 = "registra a hora · +" + numero(val.cr) + " cr · +" + numero(val.xp) + " XP";
                        if (nowMs - adiouEmMs_ < 6000 && adiouIdx_ == cartao.indice) l2 = "ok · aviso de novo às " + hhmm(adiouAteMin_);
                    } else {
                        l1 = v.minuto >= 0 ? hhmm(v.minuto) + (cartao.atrasoMin > 0 ? " · há " + duracao(cartao.atrasoMin) + " · ainda no horário" : " · agora") : "qualquer hora";
                        l2 = ganho;
                    }
                    break;
                case ModoCartao::Atrasado:
                    titulo = medicacao ? "AGORA · confirme" : "AGORA";
                    corTitulo = kLaranja;
                    if (medicacao) {
                        l1 = "era " + hhmm(v.minuto) + " · atrasada " + duracao(cartao.atrasoMin);
                        corL1 = kLaranja;
                        l2 = "registra a hora · só XP · -½ coração";
                        if (nowMs - adiouEmMs_ < 6000 && adiouIdx_ == cartao.indice) l2 = "ok · aviso de novo às " + hhmm(adiouAteMin_);
                    } else {
                        l1 = "era " + hhmm(v.minuto) + " · atrasado " + duracao(cartao.atrasoMin);
                        corL1 = kLaranja;
                        l2 = "no horário: +" + numero(val.cr) + " cr · agora: só XP";
                    }
                    break;
                case ModoCartao::Perdido:
                    titulo = "PERDIDO";
                    corTitulo = kCarmim;
                    l1 = "era " + hhmm(v.minuto) + " · passou da janela";
                    corL1 = kCarmim;
                    l2 = medicacao ? "registra a hora · sem ganhos · -1 coração" : "não rende mais cr nem XP · pode pular";
                    break;
                case ModoCartao::Feito:
                    titulo = "FEITO";
                    corTitulo = kVerde;
                    {
                        const auto& o = oc[cartao.indice];
                        l1 = o.minutoReal >= 0 ? "às " + hhmm(o.minutoReal) + (o.remota ? " · pelo celular" : "") : "feito";
                        l2 = o.status == tempo::StatusHabito::NoHorario ? "no horário: +" + numero(val.cr) + " cr · +" + numero(val.xp) + " XP"
                                                                         : "atrasado: só XP";
                    }
                    break;
                case ModoCartao::Pulado:
                    titulo = "PULADO";
                    l1 = "sem cr nem XP";
                    l2 = "não conta como esquecido";
                    break;
                case ModoCartao::Esquecido:
                    titulo = "ESQUECIDO";
                    corTitulo = kCarmim;
                    l1 = v.minuto >= 0 ? "era " + hhmm(v.minuto) : "";
                    l2 = oc[cartao.indice].feita ? "feito fora da janela: sem ganhos" : "o dia fechou sem registro";
                    break;
                default: break;
                }
                // botoes
                if (cartao.podeFeito) {
                    botao1 = medicacao ? "Sim, tomei agora" : "Feito agora";
                    botaoHabilitado = true;
                } else if (cartao.modo == ModoCartao::Proximo || cartao.modo == ModoCartao::Perdido) {
                    botao1 = "Ainda cedo";
                    if (cartao.modo == ModoCartao::Perdido) botao1 = "Passou do prazo";
                }
                if (cartao.podePular || (medicacao && cartao.podeFeito)) {
                    botao2 = medicacao ? "Ainda não" : "Pular";
                    temBotao2 = true;
                }
                if (!cartao.podeFeito && !cartao.podePular && !medicacao) {   // informativo (escolhido na lista): voltar ao automatico
                    botao1.clear();
                    botao2 = "Voltar ao próximo";
                    temBotao2 = true;
                }
                if (medicacao && cartao.modo == ModoCartao::Proximo && !cartao.podeFeito) {   // medicacao fora da janela
                    botao1 = "Ainda cedo";
                    temBotao2 = false;
                }
            } else if (cartao.modo == ModoCartao::DiaCompleto) {
                anelDia = true;
                const bool completo = totais.esquecidas == 0 && totais.pendentes == 0;
                titulo = completo ? "DIA COMPLETO" : "SEM MAIS HÁBITOS HOJE";
                corTitulo = completo ? kVerde : kAmbar;
                const int base = totais.total - totais.puladas;
                nome = std::to_string(totais.feitas) + " de " + std::to_string(base);
                l1 = "+" + numero(totais.cr) + " cr hoje";
                const double frac = base > 0 ? double(totais.feitas) / base : 0.0;
                l2 = frac >= 0.8 ? "80%+ no horário: +½ coração" : "meta do dia: 80% no horário";
            } else {   // SemHabitos
                titulo = "SEM HÁBITOS";
                corTitulo = cMudo;
                nome = "Nada hoje";
                l1 = "crie hábitos com lembrete no TickTick";
                l2 = "e sincronize pelo Wi-Fi";
            }

            const bool desenhaBotoes = !anelDia && cartao.modo != ModoCartao::SemHabitos;
            Bloco b(ui, areaCartao, SurfaceMuted, 12, 20 + uint32_t(cartao.modo));
            b.texto({24, 33, 262, 16}, titulo, 10, corTitulo);
            if (anelDia) {
                b.texto({116, 44, 172, 27}, nome, 22, cTexto);
                b.texto({116, 76, 172, 16}, l1, 12, kAmbar);
                b.texto({116, 96, 176, 16}, l2, 10, cMudo);
                b.texto({116, 114, 176, 16}, "pulados " + std::to_string(totais.puladas) + " · esquecidos " + std::to_string(totais.esquecidas), 10, cMudo);
            } else {
                b.texto({24, 47, 262, 27}, nome, 22, cTexto);
                b.texto({24, 76, 266, 16}, l1, 11, corL1);
                b.texto({24, 91, 266, 16}, l2, 10, cMudo);
            }
            if (desenhaBotoes && !botao1.empty())
                b.textoEm({areaPrimario.x, areaPrimario.y + 8, areaPrimario.w, 18}, botao1, 14, botaoHabilitado ? corTextoBotao : cMudo,
                          botaoHabilitado ? corBotao : kEscuro);
            const Rect areaBotao2 = botao1.empty() ? Rect{24, 108, 264, 34} : areaSecundario;
            if (desenhaBotoes && temBotao2)
                b.textoEm({areaBotao2.x, areaBotao2.y + 8, areaBotao2.w, 18}, botao2, 12, cMudo, kEscuro);
            b.marca(uint32_t(botaoHabilitado) * 3U + uint32_t(temBotao2) * 5U + uint32_t(botao1.empty()) * 7U + uint32_t(totais.feitas) * 131U);
            const int ncar = int(letras(nome));
            b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                if (anelDia) {
                    const int base = totais.total - totais.puladas;
                    const double frac = base > 0 ? double(totais.feitas) / base : 0.0;
                    anelProgresso(g, 66 + dx, 90 + dy, 28, 10, frac, kVerde, kLinha);
                    if (totais.esquecidas == 0 && totais.pendentes == 0) marcaFeito(g, 66 + dx, 90 + dy, 13, kVerde);
                    return;
                }
                if (medicacao) pilula(g, 24 + int(ncar * 11.6) + 20 + dx, 61 + dy, 1.1, kCarmim);
                if (desenhaBotoes && !botao1.empty()) g.fillRoundRect(areaPrimario.x + dx, areaPrimario.y + dy, areaPrimario.w, areaPrimario.h, 11, botaoHabilitado ? corBotao : kEscuro);
                if (desenhaBotoes && temBotao2) g.fillRoundRect(areaBotao2.x + dx, areaBotao2.y + dy, areaBotao2.w, areaBotao2.h, 11, kEscuro);
            });
            // A sequencia de reivindicacoes do Context (slots) precisa ser a mesma a cada quadro: sempre 2 toques aqui,
            // mesmo desabilitados (senao o slot de um bloco ja pintado vira "toque" e o Context apaga o que acabou de pintar).
            primario = ui.tap(areaPrimario, desenhaBotoes && botaoHabilitado && !botao1.empty());
            secundario = ui.tap(areaBotao2, desenhaBotoes && temBotao2);
        }

        // ---- lista (5 linhas)
        constexpr int kLinhaY0 = 28, kPasso = 25, kLinhaH = 22;
        if (n == 0) {
            Bloco vazio(ui, {310, 28, 320, 122}, Background, 0, 40);
            vazio.texto({310, 70, 320, 20}, "Nenhum hábito hoje", 12, cMudo, TextAlign::Center);
            vazio.desenhar();
        }
        for (size_t i = 0; i < tempo::kLinhasListaHabitos && rolagem_ + i < n; ++i) {
            const size_t idx = size_t(rolagem_) + i;
            const auto& v = vistas[idx];
            const bool emFoco = temFoco && idx == cartao.indice;
            const bool escolhido = selecionado_ && *selecionado_ == idx;
            const bool med = tempo::exigeConfirmacao(v.tipo);
            const int y = kLinhaY0 + int(i) * kPasso;
            const uint16_t fundo = emFoco ? (v.fase == tempo::FaseOcorrencia::Atrasada ? kLinhaAtraso : kLinhaSel) : ui.color(SurfaceMuted);
            const bool apagada = v.fase == tempo::FaseOcorrencia::Futura || v.fase == tempo::FaseOcorrencia::Pulada
                              || v.fase == tempo::FaseOcorrencia::Esquecida;
            const auto [textoDireita, corDireita] = direita(v, cMudo);
            const Rect rect{310, int16_t(y), 320, kLinhaH};
            Bloco b(ui, rect, fundo, 7, 60 + uint32_t(v.fase) * 3U + uint32_t(emFoco) * 17U + uint32_t(escolhido) * 29U);
            b.texto({342, y + 3, 190, 15}, v.nome, 12, apagada ? cMudo : cTexto);
            b.texto({496, y + 3, 64, 15}, v.minuto >= 0 ? hhmm(v.minuto) : "—", 11, cMudo, TextAlign::Right);
            b.texto({566, y + 4, 56, 15}, textoDireita, 10, corDireita, TextAlign::Right);
            const int ncar = int(letras(v.nome));
            b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
                marcador(g, 326 + dx, y + kLinhaH / 2 + dy, 7, v.minuto < 0 && v.fase == tempo::FaseOcorrencia::Agora ? tempo::FaseOcorrencia::Futura : v.fase, med);
                if (escolhido) g.drawRoundRect(310 + dx, y + dy, 320, kLinhaH, 7, kLaranja);
                if (med) pilula(g, 342 + int(ncar * 6.6) + 14 + dx, y + kLinhaH / 2 + dy, 0.7, kCarmim);
            });
            if (ui.tap(rect)) {
                selecionado_ = tempo::alternarSelecao(selecionado_, idx);
            }
        }
        {   // sempre reivindicado (vazio quando cabe tudo): mantem a sequencia de slots estavel
            Bloco rodape(ui, {400, 154, 230, 16}, Background, 0, 50);
            if (n > tempo::kLinhasListaHabitos) {
                const size_t ate = std::min(n, size_t(rolagem_) + tempo::kLinhasListaHabitos);
                rodape.texto({400, 154, 230, 16}, std::to_string(rolagem_ + 1) + "–" + std::to_string(ate) + " de " + std::to_string(n) + " · arraste",
                             10, cMudo, TextAlign::Right);
            }
            rodape.desenhar();
        }

        // ---- rodape: erro de sync de habitos / itens parados na fila (discreto)
        {
            std::string texto;
            uint16_t cor = cMudo;
            if (!info.erroSync.empty()) {
                texto = "Sync de hábitos: " + cortar(info.erroSync, 40);
                cor = kCarmim;
            } else if (filaParados_ > 0) {
                texto = std::to_string(filaParados_) + (filaParados_ == 1 ? " check-in parado" : " check-ins parados") + " na fila (erro do TickTick)";
                cor = kAmbar;
            } else if (filaTotal_ > 0) {
                texto = std::to_string(filaTotal_) + (filaTotal_ == 1 ? " check-in aguarda" : " check-ins aguardam") + " o Wi-Fi";
            }
            Bloco rodape(ui, {10, 156, 384, 16}, Background, 0, 55);
            if (!texto.empty()) rodape.texto({14, 156, 376, 16}, texto, 10, cor);
            rodape.desenhar();
        }

        // ---- acoes (depois do desenho: o estado muda para o proximo quadro)
        if (primario && temFoco && cartao.podeFeito) {
            if (tempo.marcarHabito(nowMs, filesystem, cartao.indice)) {
                selecionado_.reset();
                rolagemManual_ = false;
                ui.invalidate();
            }
        } else if (secundario && temFoco) {
            if (cartao.podeFeito && cartao.confirmar2Toques) {
                tempo.adiarHabito(nowMs, cartao.indice);   // "Ainda nao": o lembrete volta em 15 min
                adiouEmMs_ = nowMs;
                adiouIdx_ = cartao.indice;
                adiouAteMin_ = (minuto + tempo::kAdiarMedicacaoMin) % 1440;
                selecionado_.reset();
                rolagemManual_ = false;
            } else if (cartao.podePular) {
                if (tempo.pularHabito(nowMs, filesystem, cartao.indice)) {
                    selecionado_.reset();
                    rolagemManual_ = false;
                    ui.invalidate();
                }
            } else {
                selecionado_.reset();   // "Voltar ao próximo"
                rolagemManual_ = false;
            }
        }
    }

    // ====================================================================== em pe (172 x 640): linha do tempo vertical
    void HabitosScreen::desenharEmPe(ui::Context& ui, Screen& screen, const tempo::Estado& estado, int minuto, const tempo::Data& data) {
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground);
        const auto vistas = tempo::ocorrenciasDoDia(estado, minuto);
        const auto totais = tempo::totaisDoDia(estado);
        const int base = totais.total - totais.puladas;
        const double frac = base > 0 ? double(totais.feitas) / base : 0.0;

        // faixa de horas: 06h-23h, ampliada se algum horario cair fora
        int primeiro = 24 * 60, ultimo = 0, semHora = 0;
        for (const auto& v : vistas) {
            if (v.minuto < 0) { ++semHora; continue; }
            primeiro = std::min(primeiro, v.minuto);
            ultimo = std::max(ultimo, v.minuto);
        }
        int t0 = 6 * 60, t1 = 23 * 60;
        if (primeiro < t0) t0 = primeiro / 60 * 60;
        if (ultimo > t1) t1 = std::min(24 * 60, (ultimo + 59) / 60 * 60);
        const int y0 = 80, y1 = semHora > 0 ? 566 : 620, x = 44;
        const auto Y = [&](int m) { return y0 + int(std::lround(double(std::clamp(m, t0, t1) - t0) / double(std::max(1, t1 - t0)) * (y1 - y0))); };

        Bloco b(ui, {0, 0, 172, 640}, Background, 0, 900 + uint32_t(minuto));
        b.marca(uint32_t(frac * 1000) * 31U + uint32_t(vistas.size()));
        b.texto({12, 6, 100, 16}, "‹ HÁBITOS", 10, cMudo);
        b.texto({12, 24, 120, 22}, dataTexto(data, data.ano >= 2025), 16, cTexto);
        b.texto({100, 28, 60, 16}, std::to_string(totais.feitas) + "/" + std::to_string(base), 12, kVerde, TextAlign::Right);
        for (int h = (t0 + 119) / 120 * 2; h * 60 <= t1; h += 2) {
            char t[8];
            std::snprintf(t, sizeof t, "%02dh", h % 24);
            b.texto({10, Y(h * 60) - 8, 32, 15}, t, 10, cMudo);
        }
        struct Pos { int linhaY, textoY; };
        std::vector<Pos> pos(vistas.size());
        int anterior = -999;
        for (size_t i = 0; i < vistas.size(); ++i) {
            const auto& v = vistas[i];
            if (v.minuto < 0) continue;
            const int yy = Y(v.minuto);
            const int ty = std::max(yy, anterior + 30);
            pos[i] = {yy, ty};
            anterior = ty;
            using F = tempo::FaseOcorrencia;
            const bool atras = v.fase == F::Atrasada || v.fase == F::Agora;
            const bool apagada = v.fase == F::Futura || v.fase == F::Pulada;
            b.texto({58, ty - 9, 110, 18}, v.nome, 12, atras ? kLaranja : apagada ? cMudo : cTexto);
            std::string sub = hhmm(v.minuto);
            if (v.fase == F::Atrasada) sub += " · atrasado";
            else if (v.fase == F::Pulada) sub += " · pulado";
            else if (v.fase == F::Esquecida || v.fase == F::Perdida) sub += " · perdido";
            b.texto({58, ty + 8, 110, 15}, sub, 10, atras ? kLaranja : v.fase == F::Esquecida || v.fase == F::Perdida ? kCarmim : cMudo);
        }
        int ySem = y1 + 22;
        std::vector<size_t> semHoraIdx;
        for (size_t i = 0; i < vistas.size(); ++i)
            if (vistas[i].minuto < 0 && semHoraIdx.size() < 3) semHoraIdx.push_back(i);
        if (!semHoraIdx.empty()) b.texto({12, ySem - 2, 150, 15}, "SEM HORÁRIO", 10, cMudo);
        for (size_t k = 0; k < semHoraIdx.size(); ++k)
            b.texto({34, ySem + 14 + int(k) * 22, 130, 18}, vistas[semHoraIdx[k]].nome, 12,
                    vistas[semHoraIdx[k]].fase == tempo::FaseOcorrencia::Futura ? cMudo : cTexto);
        const int yAgora = Y(minuto);
        char hora[8];
        std::snprintf(hora, sizeof hora, "%02d:%02d", minuto / 60, minuto % 60);
        b.texto({8, yAgora - 8, 34, 16}, hora, 10, kTinta, TextAlign::Center);
        // o texto da hora fica sobre a pilula laranja
        b.itens.back().fundo = kLaranja;

        b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
            g.fillRoundRect(12 + dx, 56 + dy, 148, 5, 2, kLinha);
            if (frac > 0) g.fillRoundRect(12 + dx, 56 + dy, std::max(5, int(148 * frac)), 5, 2, kVerde);
            const int ya = yAgora + dy;
            g.fillRect(x - 1 + dx, y0 + dy, 3, std::max(0, ya - y0), kVerde);
            g.fillRect(x - 1 + dx, ya, 3, std::max(0, y1 - yAgora), kLinha);
            g.drawFastHLine(8 + dx, ya, 156, kLaranja);
            g.fillRoundRect(6 + dx, ya - 8, 34, 16, 6, kLaranja);
            for (size_t i = 0; i < vistas.size(); ++i) {
                const auto& v = vistas[i];
                if (v.minuto < 0) continue;
                marcador(g, x + dx, pos[i].linhaY + dy, 7, v.fase, tempo::exigeConfirmacao(v.tipo));
                if (pos[i].textoY != pos[i].linhaY) g.drawLine(x + 8 + dx, pos[i].linhaY + dy, 54 + dx, pos[i].textoY + dy - 2, kLinha);
                if (tempo::exigeConfirmacao(v.tipo)) pilula(g, 160 + dx - 8, pos[i].textoY + dy, 0.6, kCarmim);
            }
            for (size_t k = 0; k < semHoraIdx.size(); ++k)
                marcador(g, 20 + dx, ySem + 23 + int(k) * 22 + dy, 6, vistas[semHoraIdx[k]].fase == tempo::FaseOcorrencia::Agora ? tempo::FaseOcorrencia::Futura : vistas[semHoraIdx[k]].fase, false);
        });
        if (ui.tap({0, 0, 110, 28})) screen = Screen::Home;
    }

    // ====================================================================== aviso (lembrete) no topo de qualquer tela
    ui::Rect avisoHabitoRect(const ui::Context& ui) {
        if (ui.width() < ui.height()) return {4, 4, 164, 38};
        return {150, 4, 340, 36};
    }

    void desenharAvisoHabito(ui::Context& ui, const tempo::Aviso& aviso) {
        const Rect r = avisoHabitoRect(ui);
        const bool med = aviso.tipo == tempo::TipoHabito::Medicacao;
        const uint16_t cor = med ? kCarmim : kLaranja;
        const uint16_t fundo = med ? kAvisoFundo : kAvisoFundoGeral;
        std::string titulo = aviso.nome + " · " + hhmm(aviso.minuto);
        if (aviso.outros > 0) titulo += "  +" + std::to_string(aviso.outros);
        const bool estreito = r.w < 200;
        const auto layout = ui.prepareText({int16_t(r.x + 40), int16_t(r.y + 9), int16_t(r.w - (estreito ? 46 : 160)), 19}, titulo, 13, TextAlign::Start, 1);
        const auto dica = ui.prepareText({int16_t(r.x + r.w - 120), int16_t(r.y + 10), 110, 16}, "tocar para abrir", 10, TextAlign::Right, 1);
        ui.paint(r, [&](Arduino_GFX& g, Rect alvo) {
            const int dx = alvo.x - r.x, dy = alvo.y - r.y;
            g.fillRoundRect(alvo.x, alvo.y, alvo.w, alvo.h, 12, cor);
            g.fillRoundRect(alvo.x + 2, alvo.y + 2, alvo.w - 4, alvo.h - 4, 10, fundo);
            const int cy = r.y + r.h / 2 + dy;
            if (med) pilula(g, r.x + 20 + dx, cy, 1.0, kCarmim);
            else {
                anel(g, r.x + 20 + dx, cy, 8, kLaranja);
                g.fillCircle(r.x + 20 + dx, cy, 3, kLaranja);
            }
            ui.drawText(g, layout, ui.color(Foreground), dx, dy, fundo);
            if (!estreito) ui.drawText(g, dica, ui.color(Muted), dx, dy, fundo);
        });
    }

} // namespace screens
