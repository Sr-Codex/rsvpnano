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
#include "ui/SansFont.h"
#include "ui/screens/HabitosIcones.h"

namespace screens {
    namespace ic = icones;

    namespace {
        using ui::Rect;
        using ui::TextAlign;
        using namespace ui::themes;
        using tempo::FaseOcorrencia;
        using tempo::ModoCartao;

        constexpr uint16_t kLaranja = rgb565(255, 120, 50);
        constexpr uint16_t kAmbar = rgb565(255, 196, 80);
        constexpr uint16_t kCarmim = rgb565(240, 70, 100);
        constexpr uint16_t kVerde = rgb565(40, 190, 120);
        constexpr uint16_t kVerdeClaro = rgb565(130, 235, 175);
        constexpr uint16_t kLinha = rgb565(52, 54, 52);
        constexpr uint16_t kPainel = rgb565(34, 36, 35);          // botao secundario / linha da lista
        constexpr uint16_t kPainelFundo = rgb565(24, 26, 25);     // painel suave
        constexpr uint16_t kTinta = rgb565(12, 13, 14);           // texto sobre laranja / verde
        constexpr uint16_t kVerdeEscuro = rgb565(24, 52, 38);     // cartao "Proximo" da celebracao
        constexpr uint16_t kPilula = rgb565(40, 34, 16);          // "+2 cr +5 XP"
        constexpr uint16_t kFundoLaranja = rgb565(44, 28, 16);    // grupo/linha em foco
        constexpr uint16_t kAvisoFundo = rgb565(44, 24, 30);
        constexpr uint16_t kAvisoFundoGeral = rgb565(46, 34, 24);
        constexpr uint16_t kBarraApagada = rgb565(60, 120, 90);

        using Desenho = std::function<void(Arduino_GFX&, int, int)>;

        // Regiao opaca com textos proprios; so repinta quando a assinatura muda (mesma ideia do Home/Perfil).
        // O desenho extra recebe (dx, dy) = deslocamento logico -> buffer; os icones usam ic::Origem para isso.
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
            // Texto sobre um botao/pilula (fundo proprio): o fundo do antialiasing e o do botao.
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
                    if (extra) {
                        ic::Origem origem(alvo.x - area.x, alvo.y - area.y);
                        extra(g, alvo.x - area.x, alvo.y - area.y);
                    }
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
            std::snprintf(t, sizeof t, "%s %d %s", kSemana[tempo::diaDaSemana(d)], d.dia, kMes[std::clamp(d.mes, 1, 12) - 1]);
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

        int larguraTexto(const std::string& s, int tamanho) {
            return ui::sans::width(s, ui::sans::font(uint8_t(tamanho)));
        }

        const tempo::ValorHabito& valorDo(tempo::TipoHabito tipo) {
            return tempo::param::kValoresTipoHabito[static_cast<size_t>(tipo)];
        }

        // Marcador da ocorrencia (a forma muda com o estado, nunca so a cor). `foco` = ocorrencia em foco no cartao.
        ic::Marca marcaDe(FaseOcorrencia fase, bool foco) {
            switch (fase) {
            case FaseOcorrencia::Feita:
            case FaseOcorrencia::FeitaAtrasada: return ic::Marca::Feito;
            case FaseOcorrencia::Pulada: return ic::Marca::Pulado;
            case FaseOcorrencia::Esquecida:
            case FaseOcorrencia::Perdida: return ic::Marca::Perdido;
            case FaseOcorrencia::Atrasada: return ic::Marca::Atrasado;
            case FaseOcorrencia::Agora:
            case FaseOcorrencia::Futura: return foco ? ic::Marca::Atual : ic::Marca::Pendente;
            }
            return ic::Marca::Pendente;
        }

        // Anel (contorno) e pilula do aviso de lembrete (mantidos da v1).
        void anelAviso(Arduino_GFX& g, int cx, int cy, int r, uint16_t cor) {
            g.drawCircle(cx, cy, r, cor);
            g.drawCircle(cx, cy, r - 1, cor);
        }

        void pilulaAviso(Arduino_GFX& g, int cx, int cy, double s, uint16_t cor) {
            const int w = int(14 * s), h = std::max(4, int(7 * s)), x0 = cx - w / 2, y0 = cy - h / 2;
            g.drawRoundRect(x0, y0, w, h, h / 2, cor);
            g.fillRoundRect(cx, y0, w - w / 2, h, h / 2, cor);
            g.fillRect(cx, y0, w / 4, h, cor);
        }

        // Pilula de ganho ("+2 cr  +5 XP") com chama + sequencia ao lado; devolve o x final.
        constexpr int kAnelX = 86, kAnelY = 92, kAnelR = 72, kAnelL = 13, kAnelRi = 55, kAnelLi = 9;
    } // namespace

    // ====================================================================== cena
    struct HabitosScreen::Cena {
        uint32_t nowMs = 0;
        TempoScreen* tempo = nullptr;
        fs::FS* fs = nullptr;
        const HabitosInfo* info = nullptr;
        const tempo::Estado* estado = nullptr;
        std::vector<tempo::VistaOcorrencia> vistas;
        tempo::TotaisDia totais;
        double fracDia = 0.0;
        int minuto = 0;
        tempo::Data data;
        bool horaValida = false;
        tempo::Cartao cartao;
        bool temFoco = false;          // o cartao mostra uma ocorrencia (nao e dia completo / sem habitos)
        std::optional<size_t> foco;    // ocorrencia em foco para as marcas (a do cartao; na celebracao, a automatica)
        size_t n = 0;
        int sequencia = 0;
    };

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

    // Feitos dos ultimos 7 dias (o ultimo = hoje, do estado). Os outros 6 vem do log (eventos "habito" feitos, depois de
    // anular os "desfeito"). Relido so quando o dia ou o total de hoje muda.
    void HabitosScreen::carregarSemana(fs::FS* filesystem, const tempo::Data& hoje, int feitasHoje) {
        const int chave = tempo::stampDe(hoje) * 1000 + feitasHoje + 1;
        if (chave == semanaChave_) return;
        semanaChave_ = chave;
        semana_.fill(0);
        semana_[6] = feitasHoje;
        if (filesystem == nullptr) return;
        for (int i = 0; i < 6; ++i) {
            std::vector<tempo::EventoLog> eventos;
            TempoStorage::loadLogDay(*filesystem, tempo::dataDeSerial(tempo::diaSerial(hoje) - 6 + i), eventos);
            tempo::removerDesfeitos(eventos);
            for (const auto& e : eventos)
                if (e.tipo == "habito" && (e.resultado == "no_horario" || e.resultado == "atrasado")) ++semana_[size_t(i)];
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
            listaAberta_ = false;
            grupoAberto_ = false;
            listaRolagemPx_ = 0;
            grupoRolagemPx_ = 0;
            arrastando_ = false;
            celebra_ = false;
            semanaChave_ = 0;
            modoTela_ = 0;
            adiouEmMs_ = nowMs - 10000;
            carregarFila(filesystem);
            ultimaFilaMs_ = nowMs;
        } else if (nowMs - ultimaFilaMs_ > 4000) {
            ultimaFilaMs_ = nowMs;
            carregarFila(filesystem);
        }

        Cena c;
        c.nowMs = nowMs;
        c.tempo = &tempo;
        c.fs = filesystem;
        c.info = &info;
        const auto agora = tempo::agora(nowMs);
        c.estado = &tempo.estadoTempo();
        const tempo::Estado& estado = *c.estado;
        c.minuto = agora.valida ? agora.minutoDoDia : estado.dia.minutoAtual;
        c.data = agora.valida ? agora.data : estado.dia.data;
        c.horaValida = agora.valida;
        c.vistas = tempo::ocorrenciasDoDia(estado, c.minuto);
        c.n = c.vistas.size();
        c.totais = tempo::totaisDoDia(estado);
        c.fracDia = tempo::fracaoDoDia(c.totais);
        c.sequencia = estado.streak;
        if (selecionado_ && *selecionado_ >= c.n) selecionado_.reset();
        if (celebra_ && (!tempo::desfazerDisponivel(nowMs, celebraMs_) || !tempo.podeDesfazerHabito() || celebraIdx_ >= c.n)) celebra_ = false;
        c.cartao = tempo::montarCartao(estado, c.minuto, selecionado_);
        c.temFoco = c.cartao.modo != ModoCartao::SemHabitos && c.cartao.modo != ModoCartao::DiaCompleto;
        if (c.temFoco) c.foco = c.cartao.indice;

        const bool emPe = ui.width() < ui.height();
        const int modo = emPe ? (grupoAberto_ ? 4 : 3) : (listaAberta_ ? 2 : 1);
        if (modo != modoTela_) {   // a estrutura da tela mudou: limpa tudo e redesenha no proximo quadro
            modoTela_ = modo;
            ui.invalidate();
            return Action::None;
        }
        if (emPe) {
            if (grupoAberto_) desenharGrupoEmPe(ui, c);
            else desenharLinhaEmPe(ui, c, screen);
        } else if (listaAberta_) {
            desenharListaDia(ui, c);
        } else {
            desenharCartao(ui, c, screen);
        }
        return Action::None;
    }

    // ====================================================================== acoes
    void HabitosScreen::iniciarCelebracao(const Cena& c, size_t indice, const tempo::ResultadoOcorrencia& r, bool pulo, double janelaAntes) {
        celebra_ = true;
        celebraPulo_ = pulo;
        celebraMs_ = c.nowMs;
        celebraIdx_ = indice;
        celebraR_ = r;
        celebraJanela0_ = janelaAntes;
        selecionado_.reset();
    }

    void HabitosScreen::acaoFeito(ui::Context& ui, const Cena& c, size_t indice) {
        if (indice >= c.n) return;
        const double janela = tempo::fracaoJanela(c.estado->dia.ocorrencias[indice], c.minuto);
        tempo::ResultadoOcorrencia r;
        if (c.tempo->marcarHabito(c.nowMs, c.fs, indice, &r)) {
            iniciarCelebracao(c, indice, r, false, janela);
            ui.invalidate();
        }
    }

    void HabitosScreen::acaoPular(ui::Context& ui, const Cena& c, size_t indice) {
        if (indice >= c.n) return;
        const double janela = tempo::fracaoJanela(c.estado->dia.ocorrencias[indice], c.minuto);
        if (c.tempo->pularHabito(c.nowMs, c.fs, indice)) {
            iniciarCelebracao(c, indice, {}, true, janela);
            ui.invalidate();
        }
    }

    void HabitosScreen::acaoAdiar(const Cena& c, size_t indice) {
        c.tempo->adiarHabito(c.nowMs, indice);   // "Ainda nao": o lembrete volta em 15 min
        adiouEmMs_ = c.nowMs;
        adiouIdx_ = indice;
        adiouAteMin_ = (c.minuto + tempo::kAdiarMedicacaoMin) % 1440;
        selecionado_.reset();
    }

    void HabitosScreen::acaoDesfazer(ui::Context& ui, const Cena& c) {
        if (c.tempo->desfazerHabito(c.nowMs, c.fs)) {
            celebra_ = false;
            selecionado_.reset();
            ui.invalidate();
        } else {
            celebra_ = false;
        }
    }

    // Arrastar na vertical rola (rolagem em px, ao vivo). Zona morta de 8 px para o toque nao virar rolagem.
    void HabitosScreen::arrastar(ui::Context& ui, Rect area, int& rolagemPx, int maximoPx) {
        if (const auto* t = ui.touch()) {
            if (ui::hasTouch(*t, ui::TouchStart)) {
                arrastando_ = t->x >= area.x && t->x < area.x + area.w && t->y >= area.y && t->y < area.y + area.h;
                arrastoY_ = int16_t(t->y);
                moveu_ = false;
            } else if (arrastando_ && ui::hasTouch(*t, ui::TouchMove)) {
                const int dy = int(t->y) - arrastoY_;
                if (moveu_ || std::abs(dy) >= 8) {
                    moveu_ = true;
                    rolagemPx = std::clamp(rolagemPx - dy, 0, std::max(0, maximoPx));
                    arrastoY_ = int16_t(t->y);
                }
            }
            if (ui::hasTouch(*t, ui::TouchRelease) || ui::hasTouch(*t, ui::TouchCancel)) arrastando_ = false;
        }
        rolagemPx = std::clamp(rolagemPx, 0, std::max(0, maximoPx));
    }

    // ====================================================================== deitada (640 x 172): cartao (direcao B)
    void HabitosScreen::desenharCartao(ui::Context& ui, const Cena& c, Screen& screen) {
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cFundo = ui.color(Background);
        const tempo::Estado& estado = *c.estado;
        const auto& oc = estado.dia.ocorrencias;
        const auto& cartao = c.cartao;
        const uint32_t nowMs = c.nowMs;

        // ---- o que o quadro mostra
        enum class Rot : uint8_t { Nenhum, Habito, Visto, Seta };
        enum class Dir : uint8_t { Botoes, Celebracao, Semana, Vazio };
        std::string rotulo, nome, l2;
        uint16_t corRotulo = kAmbar, corL2 = cMudo;
        Rot rotIcone = Rot::Habito;
        auto iconeCartao = tempo::IconeHabito::Generico;
        int32_t tomCartao = -1;
        bool anelJanela = false, discoVisto = false, anelDiaCheio = false, medicacao = false;
        double fJanela = 0.0;
        uint16_t j0 = kLaranja, j1 = kAmbar;
        std::string horaCentro;
        Dir dir = Dir::Botoes;
        // botoes
        std::string botao1, botao2, botao2b;
        uint16_t corBotao1 = kLaranja, corTextoBotao1 = kTinta;
        bool botao1Ativo = false, botao2Ativo = false, botao1Duas = false, botaoVoltar = false;
        // celebracao / semana
        std::string pillTexto, chamaTexto, proxNome, proxHora;
        auto proxIcone = tempo::IconeHabito::Generico;
        bool proxExiste = false;
        tempo::AnimacaoCelebracao anim;
        std::string amanha;

        const bool celebrando = celebra_ && celebraIdx_ < c.n;
        const bool completo = c.cartao.modo == ModoCartao::DiaCompleto;
        const auto agoraProx = tempo::ocorrenciaAgora(estado, c.minuto);

        if (celebrando) {
            const auto& v = c.vistas[celebraIdx_];
            const auto& o = oc[celebraIdx_];
            iconeCartao = tempo::IconeHabito(v.icone);
            nome = v.nome;
            dir = Dir::Celebracao;
            anim = tempo::animacaoCelebracao(nowMs - celebraMs_);
            if (celebraPulo_) {
                rotulo = "PULADO · " + hhmm(o.minutoReal >= 0 ? o.minutoReal : c.minuto);
                corRotulo = cMudo;
                rotIcone = Rot::Seta;
                pillTexto = "sem cr nem XP";
                horaCentro = o.minuto >= 0 ? hhmm(o.minuto) : "";
            } else {
                rotulo = "FEITO · " + hhmm(o.minutoReal >= 0 ? o.minutoReal : c.minuto);
                corRotulo = kVerde;
                rotIcone = Rot::Visto;
                discoVisto = true;
                anelJanela = true;
                fJanela = celebraJanela0_ + (1.0 - celebraJanela0_) * anim.anel;
                if (celebraR_.cr > 0.05) pillTexto = "+" + numero(celebraR_.cr) + " cr  +" + numero(celebraR_.xp) + " XP";
                else pillTexto = "+" + numero(celebraR_.xp) + " XP";
            }
            if (c.sequencia > 0) chamaTexto = std::to_string(c.sequencia) + (c.sequencia == 1 ? " dia seguido" : " dias seguidos");
            if (agoraProx) {
                const auto& p = c.vistas[agoraProx->indice];
                proxExiste = true;
                proxNome = p.nome;
                proxHora = p.minuto >= 0 ? hhmm(p.minuto) : "";
                proxIcone = tempo::IconeHabito(p.icone);
            }
        } else if (completo) {
            dir = Dir::Semana;
            const bool tudo = c.totais.esquecidas == 0 && c.totais.pendentes == 0;
            anelDiaCheio = tudo;
            rotulo = tudo ? "DIA COMPLETO" : "SEM MAIS HÁBITOS HOJE";
            corRotulo = tudo ? kVerde : kAmbar;
            rotIcone = Rot::Nenhum;
            const int base = c.totais.total - c.totais.puladas;
            nome = std::to_string(c.totais.feitas) + " de " + std::to_string(base);
            pillTexto = "+" + numero(c.totais.cr) + " cr hoje";
            if (c.sequencia > 0) chamaTexto = std::to_string(c.sequencia) + (c.sequencia == 1 ? " dia" : " dias");
            if (const auto a = tempo::primeiroHabitoDeAmanha(c.tempo->listaHabitos(), c.data)) amanha = "amanhã: " + a->nome + " " + hhmm(a->minuto);
            discoVisto = tudo;
            carregarSemana(c.fs, c.data, c.totais.feitas);
        } else if (!c.temFoco) {   // sem habitos
            dir = Dir::Vazio;
            rotulo = "SEM HÁBITOS";
            corRotulo = cMudo;
            rotIcone = Rot::Nenhum;
            nome = "Nada hoje";
            l2 = "crie hábitos com lembrete no TickTick";
        } else {
            const auto& v = c.vistas[cartao.indice];
            const auto& o = oc[cartao.indice];
            const auto& val = valorDo(v.tipo);
            iconeCartao = tempo::IconeHabito(v.icone);
            tomCartao = v.cor;
            nome = v.nome;
            medicacao = cartao.confirmar2Toques;
            const std::string fimJanela = v.minuto >= 0 ? hhmm(v.minuto + val.janelaMin) : "";
            if (v.minuto >= 0) horaCentro = hhmm(v.minuto);
            anelJanela = v.minuto >= 0;
            fJanela = tempo::fracaoJanela(o, c.minuto);
            dir = Dir::Botoes;
            const bool adiou = nowMs - adiouEmMs_ < 6000 && adiouIdx_ == cartao.indice;
            switch (cartao.modo) {
            case ModoCartao::Proximo:
                rotulo = "PRÓXIMO · em " + duracao(cartao.minutosAte);
                corRotulo = kAmbar;
                if (medicacao) { rotulo = "MEDICAÇÃO · em " + duracao(cartao.minutosAte); corRotulo = kCarmim; }
                l2 = v.minuto >= 0 ? hhmm(v.minuto) + " · janela até " + fimJanela : "qualquer hora";
                break;
            case ModoCartao::Agora:
                rotulo = medicacao ? "MEDICAÇÃO · " + hhmm(v.minuto) : cartao.atrasoMin > 0 ? "AGORA · há " + duracao(cartao.atrasoMin) : "AGORA";
                corRotulo = medicacao ? kCarmim : kLaranja;
                if (medicacao) {
                    nome = "Tomou agora?";
                    l2 = adiou ? "ok · aviso de novo às " + hhmm(adiouAteMin_) : "registra " + hhmm(c.minuto) + " · toque confirma";
                    j0 = kCarmim; j1 = rgb565(255, 150, 170);
                    corBotao1 = kCarmim; corTextoBotao1 = cTexto;
                } else {
                    l2 = v.minuto >= 0 ? hhmm(v.minuto) + " · janela até " + fimJanela : "qualquer hora";
                }
                break;
            case ModoCartao::Atrasado:
                rotulo = medicacao ? "MEDICAÇÃO · " + hhmm(v.minuto) : "ATRASADO · " + duracao(cartao.atrasoMin);
                corRotulo = kCarmim;
                j0 = kCarmim; j1 = rgb565(255, 150, 170);
                corBotao1 = kCarmim; corTextoBotao1 = cTexto;
                if (medicacao) {
                    nome = "Tomou agora?";
                    l2 = adiou ? "ok · aviso de novo às " + hhmm(adiouAteMin_) : "era " + hhmm(v.minuto) + " · atrasada " + duracao(cartao.atrasoMin);
                } else {
                    l2 = "era " + hhmm(v.minuto) + " · ainda vale XP";
                }
                break;
            case ModoCartao::Perdido:
                rotulo = "PERDIDO";
                corRotulo = kCarmim;
                j0 = kCarmim; j1 = rgb565(255, 150, 170);
                if (medicacao) { nome = "Tomou agora?"; l2 = "era " + hhmm(v.minuto) + " · registra a hora, sem ganhos"; corBotao1 = kCarmim; corTextoBotao1 = cTexto; }
                else l2 = "era " + hhmm(v.minuto) + " · passou da janela";
                break;
            case ModoCartao::Feito:
                rotulo = std::string{o.status == tempo::StatusHabito::NoHorario ? "FEITO" : "FEITO ATRASADO"} + (o.minutoReal >= 0 ? " · " + hhmm(o.minutoReal) : "");
                corRotulo = kVerde;
                rotIcone = Rot::Visto;
                l2 = o.status == tempo::StatusHabito::NoHorario ? "no horário: +" + numero(val.cr) + " cr · +" + numero(val.xp) + " XP" : "atrasado: só XP";
                if (o.remota) l2 += " · pelo celular";
                anelJanela = false; discoVisto = true;
                break;
            case ModoCartao::Pulado:
                rotulo = "PULADO";
                corRotulo = cMudo;
                rotIcone = Rot::Seta;
                l2 = "sem cr nem XP · não conta como esquecido";
                anelJanela = false;
                break;
            case ModoCartao::Esquecido:
                rotulo = "ESQUECIDO";
                corRotulo = kCarmim;
                l2 = o.feita ? "feito fora da janela: sem ganhos" : (v.minuto >= 0 ? "era " + hhmm(v.minuto) + " · o dia fechou sem registro" : "o dia fechou sem registro");
                anelJanela = false;
                break;
            default: break;
            }
            // botoes
            if (cartao.podeFeito) {
                botao1 = medicacao ? "Sim, tomei agora" : "Feito";
                botao1Ativo = true;
                botao1Duas = medicacao;
            } else if (cartao.modo == ModoCartao::Proximo) botao1 = "Ainda cedo";
            else if (cartao.modo == ModoCartao::Perdido) botao1 = "Passou do prazo";
            if (cartao.podePular || (medicacao && cartao.podeFeito)) {
                botao2 = medicacao ? "Ainda não" : "Pular";
                botao2b = medicacao ? "avisa em 15 min" : "";
                botao2Ativo = true;
            }
            if (!cartao.podeFeito && !cartao.podePular) {   // informativo (escolhido na lista): voltar ao automatico
                botao1 = "Ver o próximo";
                botaoVoltar = true;
                botao1Ativo = true;
                corBotao1 = kPainel; corTextoBotao1 = cTexto;
            }
            if (!botao1Ativo) { corBotao1 = kPainel; corTextoBotao1 = cMudo; }
        }

        // ---- pontos do dia: forma por estado
        std::vector<ic::Marca> marcas(c.n);
        for (size_t i = 0; i < c.n; ++i) {
            bool foco = c.foco && *c.foco == i;
            marcas[i] = marcaDe(c.vistas[i].fase, foco);
        }
        const int passo = c.n > 0 ? std::clamp(280 / int(c.n), 6, 17) : 17;
        const int raioPonto = std::max(2, std::min(7, int(passo * 0.4)));
        const int pontosX = 188, pontosY = 147;

        // ---- bloco unico (opaco)
        Bloco b(ui, {0, 0, 640, 172}, Background, 0, 100 + uint32_t(cartao.modo) * 7U + uint32_t(dir) * 131U);
        b.marca(uint32_t(c.fracDia * 1000) * 31U + uint32_t(c.totais.feitas) * 7U + uint32_t(c.n));
        b.marca(uint32_t(fJanela * 400) * 17U + uint32_t(anelJanela) * 3U + uint32_t(discoVisto) * 5U + uint32_t(anelDiaCheio) * 11U);
        b.marca(uint32_t(tomCartao + 1) * 29U);
        b.marca(uint32_t(botao1Ativo) * 3U + uint32_t(botao2Ativo) * 5U + uint32_t(iconeCartao) * 13U + uint32_t(rotIcone) * 19U);
        b.marca(uint32_t(anim.anel * 100) * 3U + uint32_t(anim.disco * 100) * 5U + uint32_t(anim.faiscas * 100) * 7U);
        b.marca(uint32_t(proxExiste) * 3U + uint32_t(proxIcone) * 7U + uint32_t(c.sequencia > 0));
        if (dir == Dir::Semana) for (int v : semana_) b.marca(uint32_t(v) * 977U);
        for (size_t i = 0; i < marcas.size(); ++i) b.marca(uint32_t(marcas[i]) * (uint32_t(i) + 3U));

        // cabecalho
        b.texto({10, 2, 100, 20}, "‹ Hábitos", 15, cMudo);
        {
            std::string quando = dataTexto(c.data, c.horaValida);
            if (c.horaValida) quando += " · " + hhmm(c.minuto);
            b.texto({182, 2, 200, 22}, quando, 17, rgb565(150, 152, 146));
            std::string sync;
            uint16_t corSync = cMudo;
            if (c.info->sincronizando) { sync = "sincronizando…"; corSync = kAmbar; }
            else if (!c.info->erroSync.empty()) { sync = "erro de sync"; corSync = kCarmim; }
            else if (filaParados_ > 0) { sync = std::to_string(filaParados_) + " parados"; corSync = kAmbar; }
            else if (filaTotal_ > 0) sync = std::to_string(filaTotal_) + " aguardam Wi-Fi";
            if (!sync.empty()) b.texto({326, 5, 138, 17}, sync, 13, corSync, TextAlign::Right);
        }
        // centro do anel
        if (!discoVisto && !anelDiaCheio && c.temFoco) {
            b.textoEm({kAnelX - 36, kAnelY + 14, 72, 22}, horaCentro, 17, cMudo, cFundo, TextAlign::Center);
        }
        if (dir != Dir::Semana && c.n > 0) {   // contador ao lado dos pontos
            const int base = c.totais.total - c.totais.puladas;
            b.texto({pontosX + int(c.n) * passo + 2, pontosY - 11, 70, 22}, std::to_string(c.totais.feitas) + "/" + std::to_string(base), 17,
                    c.totais.feitas > 0 ? kVerde : cMudo);
        }
        // texto central
        const int tx = 182;
        const int rotX = rotIcone == Rot::Nenhum ? tx : tx + 28;
        b.texto({rotX, 27, 280, 20}, rotulo, 15, corRotulo);
        b.texto({tx, 47, 282, 36}, nome, medicacao ? 26 : 28, cTexto);
        int pillFim = tx;
        if (dir == Dir::Celebracao || dir == Dir::Semana) {
            const int pw = larguraTexto(pillTexto, 17) + 28;
            b.textoEm({tx, 88, pw, 22}, pillTexto, 17, kAmbar, kPilula, TextAlign::Center);
            pillFim = tx + pw;
            if (!chamaTexto.empty()) b.texto({pillFim + 30, 88, 250 - (pillFim - tx), 22}, chamaTexto, 17, cTexto);
            if (!amanha.empty()) b.texto({tx, 120, 280, 22}, amanha, 17, cMudo);
        } else {
            b.texto({tx, 88, 282, 22}, l2, 17, corL2);
        }
        // direita
        const Rect rPri{472, 22, 160, 76}, rSec{472, 106, 160, 56};
        if (dir == Dir::Botoes) {
            if (botao1Duas) {
                b.textoEm({rPri.x, 33, rPri.w, 27}, "Sim,", 21, cTexto, kCarmim);
                b.textoEm({rPri.x, 58, rPri.w, 27}, "tomei agora", 21, cTexto, kCarmim);
            } else if (!botao1.empty()) {
                const bool ativoGrande = botao1Ativo && !botaoVoltar;
                b.textoEm({ativoGrande ? rPri.x + 56 : rPri.x, ativoGrande ? 44 : 48, ativoGrande ? 100 : rPri.w, ativoGrande ? 33 : 27}, botao1,
                          ativoGrande ? 26 : 21, corTextoBotao1, corBotao1);
            }
            if (!botao2.empty()) {
                if (!botao2b.empty()) {
                    b.textoEm({rSec.x, 110, rSec.w, 22}, botao2, 17, cMudo, kPainel);
                    b.textoEm({rSec.x, 132, rSec.w, 17}, botao2b, 13, rgb565(130, 132, 126), kPainel);
                } else {
                    b.textoEm({rSec.x + 44, 123, 100, 22}, botao2, 17, cMudo, kPainel);
                }
            }
        } else if (dir == Dir::Celebracao) {
            b.textoEm({rPri.x, 28, rPri.w, 20}, proxExiste ? "Próximo" : "Hoje", 15, cMudo, kVerdeEscuro);
            if (proxExiste) {
                b.textoEm({rPri.x + 48, 48, rPri.w - 54, 22}, proxNome, 17, cTexto, kVerdeEscuro, TextAlign::Start);
                b.textoEm({rPri.x + 48, 70, rPri.w - 54, 20}, proxHora.empty() ? "qualquer hora" : proxHora, 15, cMudo, kVerdeEscuro, TextAlign::Start);
            } else {
                b.textoEm({rPri.x + 48, 58, rPri.w - 54, 22}, "Dia completo", 17, cTexto, kVerdeEscuro, TextAlign::Start);
            }
            b.textoEm({rSec.x + 44, 123, 100, 22}, "Desfazer", 17, cMudo, kPainel, TextAlign::Center);
        } else if (dir == Dir::Semana) {
            b.textoEm({472, 28, 160, 20}, "SEMANA", 15, cMudo, kPainelFundo);
            for (int i = 0; i < 7; ++i) {
                static constexpr const char* kIni[] = {"S", "T", "Q", "Q", "S", "S", "D"};
                const int dia = tempo::diaDaSemana(tempo::dataDeSerial(tempo::diaSerial(c.data) - 6 + i));
                b.textoEm({480 + i * 21, 140, 20, 20}, kIni[dia], 15, rgb565(120, 122, 116), kPainelFundo, TextAlign::Center);
            }
        }

        b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
            // anel do dia (externo) e janela do habito (interno): estilo Apple, o mesmo da batalha
            desenharAnelApple(g, kAnelX + dx, kAnelY + dy, kAnelR, kAnelL, anelDiaCheio ? 1.0 : c.fracDia, kVerde, kVerdeClaro, cFundo);
            if (anelJanela) desenharAnelApple(g, kAnelX + dx, kAnelY + dy, kAnelRi, kAnelLi, fJanela, j0, j1, cFundo);
            else if (!anelDiaCheio && c.temFoco) desenharAnelApple(g, kAnelX + dx, kAnelY + dy, kAnelRi, kAnelLi, 0.0, kLinha, kLinha, cFundo);
            // centro
            if (anelDiaCheio) {
                ic::disco(g, kAnelX, kAnelY, 34, discoVisto ? kVerde : kLinha);
                if (discoVisto) ic::vistoDisco(g, kAnelX, kAnelY, 20, kTinta, kVerde);
            } else if (discoVisto) {
                const int r = int(30 * anim.disco) ;
                const int raio = celebrando ? r : 30;
                if (raio > 2) ic::disco(g, kAnelX, kAnelY, raio, kVerde);
                if (raio > 18) ic::vistoDisco(g, kAnelX, kAnelY, std::max(6, int(raio * 0.7)), kTinta, kVerde);
            } else if (c.temFoco || celebrando) {
                ic::habito(g, iconeCartao, kAnelX, kAnelY - 7, 15, tomCartao >= 0 ? ic::de888(uint32_t(tomCartao)) : cTexto, cFundo);
            }
            if (celebrando && !celebraPulo_) ic::faiscas(g, kAnelX, kAnelY, kAnelR, anim.faiscas);
            // icone do rotulo
            if (rotIcone == Rot::Habito) ic::habito(g, iconeCartao, tx + 12, 37, 9, corRotulo, cFundo);
            else if (rotIcone == Rot::Visto) ic::visto(g, tx + 12, 37, 9, corRotulo, cFundo);
            else if (rotIcone == Rot::Seta) ic::pular(g, tx + 12, 37, 9, corRotulo, cFundo);
            if (dir == Dir::Celebracao || dir == Dir::Semana) ic::preencherRR(g, tx, 86, pillFim - tx, 26, 13, kPilula);
            // chama da sequencia
            if ((dir == Dir::Celebracao || dir == Dir::Semana) && !chamaTexto.empty()) ic::chama(g, pillFim + 16, 99, 9, kAmbar, cFundo);
            // pontos do dia
            if (dir != Dir::Semana && c.n > 0) {
                for (size_t i = 0; i < c.n; ++i) ic::ponto(g, pontosX + int(i) * passo, pontosY, raioPonto, marcas[i], cFundo);
            }
            // direita
            if (dir == Dir::Botoes) {
                if (!botao1.empty()) {
                    ic::preencherRR(g, rPri.x, rPri.y, rPri.w, rPri.h, 16, corBotao1);
                    if (botao1Ativo && botao1 == "Feito") ic::visto(g, 506, 60, 12, corTextoBotao1, corBotao1);
                }
                if (!botao2.empty()) {
                    ic::preencherRR(g, rSec.x, rSec.y, rSec.w, rSec.h, 16, kPainel);
                    if (botao2b.empty()) ic::pular(g, 512, 134, 9, cMudo, kPainel);
                }
            } else if (dir == Dir::Celebracao) {
                ic::preencherRR(g, rPri.x, rPri.y, rPri.w, rPri.h, 16, kVerdeEscuro);
                if (proxExiste) ic::habito(g, proxIcone, 500, 68, 11, cTexto, kVerdeEscuro);
                else ic::visto(g, 500, 68, 10, kVerdeClaro, kVerdeEscuro);
                ic::preencherRR(g, rSec.x, rSec.y, rSec.w, rSec.h, 16, kPainel);
                ic::desfazer(g, 512, 134, 9, cMudo, kPainel);
            } else if (dir == Dir::Semana) {
                ic::preencherRR(g, 472, 22, 160, 140, 16, kPainelFundo);
                int maximo = 1;
                for (int v : semana_) maximo = std::max(maximo, v);
                for (int i = 0; i < 7; ++i) {
                    const int bx = 480 + i * 21;
                    const int v = semana_[size_t(i)];
                    const int h = v > 0 ? std::max(6, 70 * v / maximo) : 3;
                    ic::preencherRR(g, bx, 134 - h, 14, h, 4, v == 0 ? kLinha : (v >= maximo ? kVerde : kBarraApagada));
                }
            }
        });

        // ---- toques (SEMPRE os mesmos 4, na mesma ordem: o Context identifica o slot pela posicao)
        const bool voltar = ui.tap({0, 0, 120, 36});
        const bool abrirLista = ui.tap({176, 126, 290, 46}, dir != Dir::Semana && c.n > 0);
        const bool primario = ui.tap(rPri, dir == Dir::Celebracao ? true : dir == Dir::Botoes && botao1Ativo);
        const bool secundario = ui.tap(rSec, dir == Dir::Celebracao ? true : dir == Dir::Botoes && botao2Ativo);

        if (voltar) {
            screen = Screen::Home;
            return;
        }
        if (abrirLista) {
            listaAberta_ = true;
            const size_t alvo = c.foco ? *c.foco : 0;
            listaRolagemPx_ = tempo::rolagemPxPara(c.n, alvo, tempo::kAlturaLinhaLista, 142);
            return;
        }
        if (dir == Dir::Celebracao) {
            if (primario) celebra_ = false;                 // "Proximo": segue para o proximo (some o Desfazer)
            else if (secundario) acaoDesfazer(ui, c);
            return;
        }
        if (dir != Dir::Botoes || !c.temFoco) return;
        if (primario) {
            if (cartao.podeFeito) acaoFeito(ui, c, cartao.indice);
            else {   // "Voltar ao proximo" (item informativo escolhido na lista)
                selecionado_.reset();
            }
        } else if (secundario) {
            if (cartao.podeFeito && cartao.confirmar2Toques) acaoAdiar(c, cartao.indice);
            else if (cartao.podePular) acaoPular(ui, c, cartao.indice);
        }
    }

    // ====================================================================== deitada: lista do dia (linhas >= 56 px, rolagem vertical)
    void HabitosScreen::desenharListaDia(ui::Context& ui, const Cena& c) {
        constexpr int kTopo = 30, kAlt = 142, kPasso = tempo::kAlturaLinhaLista;
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cFundo = ui.color(Background);
        const tempo::Estado& estado = *c.estado;
        arrastar(ui, {0, kTopo, 640, kAlt}, listaRolagemPx_, std::max(0, int(c.n) * kPasso - kAlt));
        const int rol = listaRolagemPx_;

        // linhas
        std::optional<size_t> tocada;
        {
            Bloco l(ui, {0, kTopo, 640, kAlt}, Background, 0, 310 + uint32_t(rol));
            for (size_t i = 0; i < c.n; ++i) l.marca(uint32_t(c.vistas[i].fase) * (uint32_t(i) + 5U));
            if (c.foco) l.marca(uint32_t(*c.foco) * 977U);
            struct Linha { int y; const tempo::VistaOcorrencia* v; ic::Marca marca; bool foco; std::string direita; uint16_t corDireita; std::string sub; };
            std::vector<Linha> linhas;
            for (size_t i = 0; i < c.n; ++i) {
                const int y = kTopo + int(i) * kPasso - rol;
                if (y + kPasso <= kTopo || y >= kTopo + kAlt) continue;
                const auto& v = c.vistas[i];
                const auto& o = estado.dia.ocorrencias[i];
                const bool foco = c.foco && *c.foco == i;
                std::string sub = v.minuto >= 0 ? hhmm(v.minuto) : "qualquer hora";
                std::string direita;
                uint16_t cd = cMudo;
                switch (v.fase) {
                case FaseOcorrencia::Feita: direita = "feito " + hhmm(o.minutoReal); cd = kVerde; break;
                case FaseOcorrencia::FeitaAtrasada: direita = "feito " + hhmm(o.minutoReal) + " · só XP"; cd = kAmbar; break;
                case FaseOcorrencia::Esquecida: direita = "esquecido"; cd = kCarmim; break;
                case FaseOcorrencia::Perdida: direita = "perdido"; cd = kCarmim; break;
                case FaseOcorrencia::Pulada: direita = "pulado"; break;
                case FaseOcorrencia::Atrasada: direita = "atrasado " + duracao(std::max(1, c.minuto - v.minuto)); cd = kCarmim; break;
                case FaseOcorrencia::Agora: direita = v.minuto > c.minuto ? "em " + duracao(v.minuto - c.minuto) : "agora"; cd = kLaranja; break;
                case FaseOcorrencia::Futura: direita = v.minuto > c.minuto ? "em " + duracao(v.minuto - c.minuto) : ""; break;
                }
                linhas.push_back({y, &v, marcaDe(v.fase, foco), foco, direita, cd, sub});
                const bool apagada = v.fase == FaseOcorrencia::Futura || v.fase == FaseOcorrencia::Pulada;
                const int ty = y;
                l.texto({68, ty + 4, 330, 27}, v.nome, 21, apagada ? cMudo : cTexto);
                l.texto({68, ty + 31, 180, 22}, sub, 17, cMudo);
                l.texto({330, ty + 17, 286, 22}, direita, 17, cd, TextAlign::Right);
            }
            l.desenhar([&](Arduino_GFX& g, int, int) {
                for (const auto& ln : linhas) {
                    ic::preencherRR(g, 6, ln.y + 1, 628, kPasso - 4, 12, ln.foco ? kFundoLaranja : kPainelFundo);
                    if (ln.foco) {
                        // contorno laranja (2 px)
                        g.drawRoundRect(6 + ic::deslocX(), ln.y + 1 + ic::deslocY(), 628, kPasso - 4, 12, kLaranja);
                        g.drawRoundRect(7 + ic::deslocX(), ln.y + 2 + ic::deslocY(), 626, kPasso - 6, 11, kLaranja);
                    }
                    ic::marcador(g, tempo::IconeHabito(ln.v->icone), 38, ln.y + kPasso / 2 - 1, 18, ln.marca,
                                 ln.foco ? kFundoLaranja : kPainelFundo, ln.v->cor);
                }
                // barra de rolagem
                if (int(c.n) * kPasso > kAlt) {
                    const int total = int(c.n) * kPasso;
                    const int hb = std::max(16, kAlt * kAlt / total);
                    const int yb = kTopo + (kAlt - hb) * rol / std::max(1, total - kAlt);
                    ic::preencherRR(g, 636, yb, 3, hb, 1, kLinha);
                }
            });
        }
        // cabecalho (depois da lista: cobre o que ela desenha acima de kTopo; repinta junto com ela)
        {
            Bloco h(ui, {0, 0, 640, kTopo}, Background, 0, 300 + uint32_t(rol));
            h.marca(uint32_t(c.totais.feitas) * 7U + uint32_t(c.n));
            h.texto({10, 2, 200, 24}, "‹ Hoje", 17, cMudo);
            h.texto({470, 2, 160, 24}, std::to_string(c.totais.feitas) + " de " + std::to_string(c.totais.total - c.totais.puladas), 17, kVerde, TextAlign::Right);
            h.texto({200, 4, 270, 20}, "toque num hábito para ver", 15, rgb565(120, 122, 116), TextAlign::Center);
            h.desenhar();
        }
        // toques: voltar + uma por linha visivel (mesma ordem sempre)
        const bool voltar = ui.tap({0, 0, 140, 40});
        for (size_t i = 0; i < c.n; ++i) {
            const int y = kTopo + int(i) * kPasso - rol;
            if (y + kPasso <= kTopo || y >= kTopo + kAlt) continue;
            const int yy = std::max(y, kTopo), alt = std::min(y + kPasso, kTopo + kAlt) - yy;
            if (ui.tap({0, int16_t(yy), 640, int16_t(alt)})) tocada = i;
        }
        (void)cFundo;
        if (voltar) {
            listaAberta_ = false;
            return;
        }
        if (tocada) {
            // tocar seleciona o item no cartao; tocar no que ja e o automatico volta ao automatico
            const bool ehAuto = tempo::montarCartao(estado, c.minuto, std::nullopt).indice == *tocada && c.cartao.modo != ModoCartao::DiaCompleto;
            selecionado_ = ehAuto ? std::nullopt : std::optional<size_t>(*tocada);
            listaAberta_ = false;
            celebra_ = false;
        }
    }

    // ====================================================================== em pe (172 x 640): linha do tempo proporcional (P1)
    void HabitosScreen::desenharLinhaEmPe(ui::Context& ui, const Cena& c, Screen& screen) {
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cFundo = ui.color(Background);
        const tempo::Estado& estado = *c.estado;
        const auto& cartao = c.cartao;
        const bool celebrando = celebra_ && celebraIdx_ < c.n;
        const auto grupos = tempo::agruparLinhaDoTempo(c.vistas, c.foco);
        std::vector<size_t> semHora;
        for (const auto& v : c.vistas)
            if (v.minuto < 0) semHora.push_back(v.indice);

        // eixo 06h-23h, ampliado se algum horario cair fora
        int t0 = 6 * 60, t1 = 23 * 60;
        for (const auto& g : grupos) {
            if (g.minutoIni < t0) t0 = g.minutoIni / 60 * 60;
            if (g.minutoFim > t1) t1 = std::min(24 * 60, (g.minutoFim + 59) / 60 * 60);
        }
        const int y0 = 112, y1 = semHora.empty() ? 548 : 500, ex = 40;
        const auto Y = [&](int m) { return y0 + int(std::lround(double(std::clamp(m, t0, t1) - t0) / double(std::max(1, t1 - t0)) * (y1 - y0))); };
        std::vector<int> ideal;
        for (const auto& g : grupos) ideal.push_back(Y(g.minutoIni));
        const std::vector<int> pos = tempo::posicoesNoEixo(ideal, 48, y1);

        struct Cx { ic::Marca marca; int yy; bool foco; };
        std::vector<Cx> cxs;
        Bloco b(ui, {0, 0, 172, 640}, Background, 0, 400 + uint32_t(c.minuto));
        b.marca(uint32_t(c.fracDia * 1000) * 31U + uint32_t(c.n) + uint32_t(celebrando) * 7919U);
        for (size_t i = 0; i < c.n; ++i) b.marca(uint32_t(c.vistas[i].fase) * (uint32_t(i) + 3U));
        if (c.foco) b.marca(uint32_t(*c.foco) * 6101U);

        b.texto({10, 4, 100, 20}, "‹ Hábitos", 15, cMudo);
        b.textoEm({46 - 30, 62 - 11, 60, 22}, std::to_string(c.totais.feitas) + "/" + std::to_string(c.totais.total - c.totais.puladas), 17, cTexto, cFundo, TextAlign::Center);
        {
            char t[8];
            std::snprintf(t, sizeof t, "%02d:%02d", c.minuto / 60, c.minuto % 60);
            b.texto({88, 28, 80, 33}, t, 26, cTexto);
        }
        std::string linha2 = c.sequencia > 0 ? std::to_string(c.sequencia) + (c.sequencia == 1 ? " dia" : " dias") : "+" + numero(c.totais.cr) + " cr";
        b.texto({106, 68, 62, 22}, linha2, 17, cTexto);
        if (c.info->sincronizando) b.texto({70, 96, 100, 17}, "sincronizando…", 13, kAmbar, TextAlign::Right);
        for (int h = (t0 + 179) / 180 * 3; h * 60 <= t1; h += 3) {
            char t[8];
            std::snprintf(t, sizeof t, "%02d", h % 24);
            b.texto({4, Y(h * 60) - 8, 24, 17}, t, 13, rgb565(120, 122, 116));
        }
        // grupos: texto e destaque
        for (size_t gi = 0; gi < grupos.size(); ++gi) {
            const auto& g = grupos[gi];
            const auto& rep = c.vistas[g.representante];
            const int yy = pos[gi];
            const bool pend = !g.todasResolvidas;
            std::string nome = rep.nome;
            if (g.indices.size() > 1) nome += " +" + std::to_string(g.indices.size() - 1);
            std::string hora = hhmm(g.minutoIni);
            if (g.minutoFim != g.minutoIni) hora += "–" + hhmm(g.minutoFim);
            const uint16_t corNome = g.temFoco ? kLaranja : g.atrasado ? kCarmim : (pend ? cMudo : cTexto);
            b.texto({62, yy - 21, 108, 22}, nome, larguraTexto(nome, 17) <= 106 ? 17 : 15, corNome);
            b.texto({62, yy + 1, 108, 20}, hora, 15, cMudo);
            ic::Marca m = g.todasFeitas ? ic::Marca::Feito : marcaDe(rep.fase, g.temFoco);
            if (g.todasResolvidas && !g.todasFeitas) m = marcaDe(rep.fase, false);
            cxs.push_back({m, yy, g.temFoco});
            b.marca(uint32_t(m) * 53U + uint32_t(yy) * 7U);
            if (g.indices.size() > 1) b.textoEm({ex + 11 - 8, yy - 11 - 9, 16, 17}, std::to_string(g.indices.size()), 13, kTinta, cTexto, TextAlign::Center);
        }
        // sem horario
        if (!semHora.empty()) {
            const auto& v = c.vistas[semHora.front()];
            std::string nome = v.nome + (semHora.size() > 1 ? " +" + std::to_string(semHora.size() - 1) : "");
            b.texto({66, 513, 100, 22}, nome, 17, cMudo);
            b.texto({66, 535, 100, 20}, "sem horário", 15, rgb565(120, 122, 116));
        }
        const int yAgora = Y(c.minuto);
        // a linha "agora" nunca cruza o texto (nome/horario) de um grupo: vai ao vao livre mais proximo, na ordem temporal
        int yLinhaAgora;
        {
            std::vector<int> meia;
            size_t acima = 0;
            for (const auto& g : grupos) {
                meia.push_back(g.temFoco ? 24 : 22);   // destaque do foco = +-24; texto = -21..+21
                if (g.minutoIni <= c.minuto) ++acima;
            }
            yLinhaAgora = tempo::posicaoLinhaAgora(yAgora, pos, meia, acima, y0 - 12, semHora.empty() ? 556 : 504);
        }
        b.textoEm({3, yLinhaAgora - 8, 38, 17}, "agora", 13, kTinta, kLaranja, TextAlign::Center);

        // ---- base: Feito (62 px) / Sim, tomei + Ainda nao / celebracao com Desfazer
        const Rect rPri = celebrando ? Rect{6, 566, 94, 70} : (cartao.confirmar2Toques && cartao.podeFeito ? Rect{6, 570, 100, 62} : Rect{6, 570, 160, 62});
        const Rect rSec = celebrando ? Rect{106, 566, 60, 70} : Rect{110, 570, 56, 62};
        const bool medicacao = c.temFoco && cartao.confirmar2Toques && cartao.podeFeito;
        const bool podeFeito = !celebrando && c.temFoco && cartao.podeFeito;
        std::string subBotao;
        if (c.temFoco) {
            const auto& v = c.vistas[cartao.indice];
            subBotao = v.nome + (v.minuto >= 0 ? " " + hhmm(v.minuto) : "");
        }
        uint16_t corFeito = kLaranja, corTxtFeito = kTinta;
        if (c.temFoco && (cartao.modo == ModoCartao::Atrasado || medicacao)) { corFeito = kCarmim; corTxtFeito = cTexto; }
        if (celebrando) {
            const std::string cr = celebraPulo_ ? "pulado" : (celebraR_.cr > 0.05 ? "+" + numero(celebraR_.cr) + " cr" : "+" + numero(celebraR_.xp) + " XP");
            b.textoEm({rPri.x + 30, rPri.y + 8, 62, 22}, cr, 17, celebraPulo_ ? cMudo : kTinta, celebraPulo_ ? kPainel : kVerde, TextAlign::Center);
            if (!celebraPulo_ && celebraR_.cr > 0.05) b.textoEm({rPri.x + 30, rPri.y + 34, 62, 22}, "+" + numero(celebraR_.xp) + " XP", 15, kTinta, kVerde, TextAlign::Center);
            b.textoEm({rSec.x, rSec.y + 38, rSec.w, 22}, "Desfazer", 13, cMudo, kPainel, TextAlign::Center);
        } else if (medicacao) {
            b.textoEm({rPri.x, rPri.y + 8, rPri.w, 22}, "Sim, tomei", 17, cTexto, kCarmim);
            b.textoEm({rPri.x, rPri.y + 32, rPri.w, 20}, "agora", 15, cTexto, kCarmim);
            b.textoEm({rSec.x, rSec.y + 8, rSec.w, 22}, "Ainda", 15, cMudo, kPainel);
            b.textoEm({rSec.x, rSec.y + 30, rSec.w, 22}, "não", 15, cMudo, kPainel);
        } else if (podeFeito) {
            b.textoEm({rPri.x + 56, rPri.y + 1, 100, 33}, "Feito", 26, corTxtFeito, corFeito);
            b.textoEm({rPri.x + 20, rPri.y + 38, 136, 20}, subBotao, 15, rgb565(70, 34, 10), corFeito);
        } else if (!c.temFoco) {
            b.textoEm({rPri.x, rPri.y + 16, rPri.w, 33}, c.n == 0 ? "Sem hábitos" : "Dia completo", 21, cMudo, kPainelFundo);
        } else {
            b.textoEm({rPri.x, rPri.y + 8, rPri.w, 27}, cartao.modo == ModoCartao::Proximo ? "Ainda cedo" : "Só ver", 21, cMudo, kPainelFundo);
            b.textoEm({rPri.x, rPri.y + 36, rPri.w, 20}, subBotao, 15, rgb565(120, 122, 116), kPainelFundo);
        }

        b.desenhar([&](Arduino_GFX& g, int dx, int dy) {
            desenharAnelApple(g, 46 + dx, 62 + dy, 30, 9, c.fracDia, kVerde, kVerdeClaro, cFundo);
            ic::preencherRR(g, ex - 1, y0, 3, std::max(0, yAgora - y0), 1, kVerde);
            ic::preencherRR(g, ex - 1, yAgora, 3, std::max(0, y1 - yAgora), 1, kLinha);
            if (c.sequencia > 0) ic::chama(g, 94, 79, 7, kAmbar, cFundo);
            // destaque do grupo em foco
            for (size_t gi = 0; gi < grupos.size(); ++gi) {
                if (!grupos[gi].temFoco) continue;
                ic::preencherRR(g, 58, pos[gi] - 24, 108, 48, 10, kFundoLaranja);
                g.drawRoundRect(58 + dx, pos[gi] - 24 + dy, 108, 48, 10, kLaranja);
                g.drawRoundRect(59 + dx, pos[gi] - 23 + dy, 106, 46, 9, kLaranja);
            }
            for (size_t gi = 0; gi < grupos.size(); ++gi) {
                const auto& rep = c.vistas[grupos[gi].representante];
                ic::marcador(g, tempo::IconeHabito(rep.icone), ex, pos[gi], 13, cxs[gi].marca, cxs[gi].foco ? kFundoLaranja : cFundo, rep.cor);
                if (grupos[gi].indices.size() > 1) ic::disco(g, ex + 11, pos[gi] - 11, 8, cTexto);
            }
            if (!semHora.empty()) {
                const auto& v = c.vistas[semHora.front()];
                ic::marcador(g, tempo::IconeHabito(v.icone), ex, 534, 13, marcaDe(v.fase, false), cFundo, v.cor);
            }
            g.drawFastHLine(28 + dx, yLinhaAgora + dy, 138, kLaranja);
            ic::preencherRR(g, 3, yLinhaAgora - 8, 38, 17, 6, kLaranja);
            // base
            if (celebrando) {
                ic::preencherRR(g, rPri.x, rPri.y, rPri.w, rPri.h, 14, celebraPulo_ ? kPainel : kVerde);
                if (!celebraPulo_) ic::visto(g, rPri.x + 17, rPri.y + 35, 9, kTinta, kVerde);
                else ic::pular(g, rPri.x + 17, rPri.y + 35, 8, cMudo, kPainel);
                ic::preencherRR(g, rSec.x, rSec.y, rSec.w, rSec.h, 14, kPainel);
                ic::desfazer(g, rSec.x + 30, rSec.y + 24, 10, cMudo, kPainel);
            } else if (medicacao) {
                ic::preencherRR(g, rPri.x, rPri.y, rPri.w, rPri.h, 16, kCarmim);
                ic::preencherRR(g, rSec.x, rSec.y, rSec.w, rSec.h, 16, kPainel);
            } else if (podeFeito) {
                ic::preencherRR(g, rPri.x, rPri.y, rPri.w, rPri.h, 16, corFeito);
                ic::visto(g, rPri.x + 26, rPri.y + 20, 11, corTxtFeito, corFeito);
            } else {
                ic::preencherRR(g, rPri.x, rPri.y, rPri.w, rPri.h, 16, c.temFoco || c.n == 0 ? kPainelFundo : rgb565(24, 52, 38));
            }
        });

        // ---- toques: voltar, base (primario/secundario) e uma por grupo (mesma ordem sempre)
        const bool voltar = ui.tap({0, 0, 110, 40});
        const bool primario = ui.tap(rPri, celebrando ? false : podeFeito);
        const bool secundario = ui.tap(rSec, celebrando || medicacao);
        std::optional<size_t> aberto;
        for (size_t gi = 0; gi < grupos.size(); ++gi)
            if (ui.tap({0, int16_t(pos[gi] - 24), 172, 48})) aberto = gi;
        const bool abrirSemHora = !semHora.empty() && ui.tap({0, 504, 172, 56});

        if (voltar) { screen = Screen::Home; return; }
        if (secundario) {
            if (celebrando) acaoDesfazer(ui, c);
            else if (medicacao) acaoAdiar(c, cartao.indice);
            return;
        }
        if (primario && podeFeito) { acaoFeito(ui, c, cartao.indice); return; }
        if (aberto) { grupoAberto_ = true; grupoMinuto_ = grupos[*aberto].minutoIni; grupoRolagemPx_ = 0; return; }
        if (abrirSemHora) { grupoAberto_ = true; grupoMinuto_ = -1; grupoRolagemPx_ = 0; }
    }

    // ====================================================================== em pe: lista do grupo (estilo P2; Feito dentro da linha)
    void HabitosScreen::desenharGrupoEmPe(ui::Context& ui, const Cena& c) {
        constexpr int kTopo = 60, kFim = 560;
        const uint16_t cMudo = ui.color(Muted), cTexto = ui.color(Foreground), cFundo = ui.color(Background);
        const tempo::Estado& estado = *c.estado;
        const bool celebrando = celebra_ && celebraIdx_ < c.n;
        // itens do grupo aberto (as sem horario quando grupoMinuto_ < 0)
        std::vector<size_t> idx;
        const auto grupos = tempo::agruparLinhaDoTempo(c.vistas, c.foco);
        if (grupoMinuto_ < 0) {
            for (const auto& v : c.vistas) if (v.minuto < 0) idx.push_back(v.indice);
        } else {
            for (const auto& g : grupos) if (g.minutoIni == grupoMinuto_) idx = g.indices;
        }
        if (idx.empty()) { grupoAberto_ = false; ui.invalidate(); return; }

        struct Linha { size_t i; int y, h; bool acionavel, med; tempo::Cartao cart; };
        std::vector<Linha> linhas;
        int total = 0;
        for (size_t i : idx) {
            const tempo::Cartao cart = tempo::montarCartao(estado, c.minuto, i);
            const bool med = cart.confirmar2Toques && cart.podeFeito;
            const bool acionavel = cart.podeFeito || cart.podePular;
            const int h = acionavel ? (med ? 56 + 52 + 50 : 56 + 52) : tempo::kAlturaLinhaLista;
            linhas.push_back({i, total, h, acionavel, med, cart});
            total += h;
        }
        arrastar(ui, {0, kTopo, 172, kFim - kTopo}, grupoRolagemPx_, std::max(0, total - (kFim - kTopo)));
        const int rol = grupoRolagemPx_;

        std::vector<std::pair<Rect, int>> botoes;   // rect + tipo: 1 feito, 2 pular, 3 adiar
        {
            Bloco l(ui, {0, kTopo, 172, kFim - kTopo}, Background, 0, 510 + uint32_t(rol));
            for (const auto& ln : linhas) {
                const auto& v = c.vistas[ln.i];
                const auto& o = estado.dia.ocorrencias[ln.i];
                l.marca(uint32_t(v.fase) * (uint32_t(ln.i) + 5U) + uint32_t(ln.acionavel));
                const int y = kTopo + ln.y - rol;
                if (y + ln.h <= kTopo || y >= kFim) continue;
                std::string sub = v.minuto >= 0 ? hhmm(v.minuto) : "qualquer hora";
                switch (v.fase) {
                case FaseOcorrencia::Feita: case FaseOcorrencia::FeitaAtrasada: sub += " · feito " + hhmm(o.minutoReal); break;
                case FaseOcorrencia::Pulada: sub += " · pulado"; break;
                case FaseOcorrencia::Esquecida: case FaseOcorrencia::Perdida: sub += " · perdido"; break;
                case FaseOcorrencia::Atrasada: sub += " · atrasado"; break;
                case FaseOcorrencia::Futura: if (v.minuto > c.minuto) sub += " · em " + duracao(v.minuto - c.minuto); break;
                default: break;
                }
                l.texto({50, y + 5, 116, 22}, v.nome, 17, v.fase == FaseOcorrencia::Futura ? cMudo : cTexto);
                l.texto({50, y + 29, 116, 20}, sub, 15, cMudo);
                if (ln.acionavel) {
                    if (ln.med) {
                        l.textoEm({8, y + 58, 156, 46}, "Sim, tomei agora", 15, cTexto, kCarmim);
                        l.textoEm({8, y + 110, 156, 38}, "Ainda não · avisa em 15 min", 13, cMudo, kPainel);
                    } else {
                        const uint16_t corF = ln.cart.modo == ModoCartao::Atrasado ? kCarmim : kLaranja;
                        if (!ln.cart.podeFeito) l.textoEm({8, y + 68, 100, 22}, "Ainda cedo", 15, cMudo, kPainel);
                        if (ln.cart.podeFeito) l.textoEm({40, y + 63, 70, 33}, "Feito", 21, ln.cart.modo == ModoCartao::Atrasado ? cTexto : kTinta, corF);
                    }
                }
            }
            l.desenhar([&](Arduino_GFX& g, int, int) {
                for (const auto& ln : linhas) {
                    const auto& v = c.vistas[ln.i];
                    const int y = kTopo + ln.y - rol;
                    if (y + ln.h <= kTopo || y >= kFim) continue;
                    const bool foco = c.foco && *c.foco == ln.i;
                    ic::preencherRR(g, 4, y + 1, 164, ln.h - 4, 12, foco ? kFundoLaranja : kPainelFundo);
                    if (foco) {
                        g.drawRoundRect(4 + ic::deslocX(), y + 1 + ic::deslocY(), 164, ln.h - 4, 12, kLaranja);
                        g.drawRoundRect(5 + ic::deslocX(), y + 2 + ic::deslocY(), 162, ln.h - 6, 11, kLaranja);
                    }
                    ic::marcador(g, tempo::IconeHabito(v.icone), 26, y + 27, 16, marcaDe(v.fase, foco), foco ? kFundoLaranja : kPainelFundo, v.cor);
                    if (ln.acionavel) {
                        if (ln.med) {
                            ic::preencherRR(g, 8, y + 58, 156, 46, 12, kCarmim);
                            ic::preencherRR(g, 8, y + 108, 156, 40, 12, kPainel);
                        } else {
                            const uint16_t corF = ln.cart.modo == ModoCartao::Atrasado ? kCarmim : kLaranja;
                            if (ln.cart.podeFeito) {
                                ic::preencherRR(g, 8, y + 58, 100, 42, 12, corF);
                                ic::visto(g, 26, y + 79, 9, ln.cart.modo == ModoCartao::Atrasado ? cTexto : kTinta, corF);
                            } else {
                                ic::preencherRR(g, 8, y + 58, 100, 42, 12, kPainel);
                            }
                            ic::preencherRR(g, 112, y + 58, 52, 42, 12, kPainel);
                            ic::pular(g, 138, y + 79, 9, cMudo, kPainel);
                        }
                    }
                }
            });
            // retangulos de toque dos botoes (clipados a area da lista)
            for (const auto& ln : linhas) {
                if (!ln.acionavel) continue;
                const int y = kTopo + ln.y - rol;
                const auto clip = [&](int ry, int rh) -> Rect {
                    const int a = std::max(ry, kTopo), z = std::min(ry + rh, kFim);
                    return {0, int16_t(a), 0, int16_t(std::max(0, z - a))};
                };
                if (ln.med) {
                    Rect r1 = clip(y + 58, 46), r2 = clip(y + 108, 40);
                    r1.x = 8; r1.w = 156; r2.x = 8; r2.w = 156;
                    botoes.push_back({r1, int(ln.i) * 10 + 1});
                    botoes.push_back({r2, int(ln.i) * 10 + 3});
                } else {
                    Rect r1 = clip(y + 58, 42), r2 = clip(y + 58, 42);
                    r1.x = 8; r1.w = 100; r2.x = 112; r2.w = 52;
                    if (ln.cart.podeFeito) botoes.push_back({r1, int(ln.i) * 10 + 1});
                    botoes.push_back({r2, int(ln.i) * 10 + 2});
                }
            }
        }
        {   // cabecalho
            Bloco h(ui, {0, 0, 172, kTopo}, Background, 0, 500 + uint32_t(rol));
            std::string titulo = grupoMinuto_ < 0 ? "sem horário" : hhmm(grupoMinuto_);
            if (grupoMinuto_ >= 0 && idx.size() > 1) titulo = hhmm(c.vistas[idx.front()].minuto) + "–" + hhmm(c.vistas[idx.back()].minuto);
            h.texto({10, 4, 150, 22}, "‹ Voltar", 17, cMudo);
            h.texto({10, 30, 150, 26}, titulo + " · " + std::to_string(idx.size()), 21, cTexto);
            h.desenhar();
        }
        // rodape: celebracao com Desfazer
        {
            Bloco r(ui, {0, kFim, 172, 640 - kFim}, Background, 0, 520 + uint32_t(celebrando) * 77U + uint32_t(rol));
            if (celebrando) {
                const std::string cr = celebraPulo_ ? "pulado" : (celebraR_.cr > 0.05 ? "+" + numero(celebraR_.cr) + " cr" : "+" + numero(celebraR_.xp) + " XP");
                r.textoEm({38, kFim + 14, 62, 22}, cr, 17, celebraPulo_ ? cMudo : kTinta, celebraPulo_ ? kPainel : kVerde, TextAlign::Center);
                if (!celebraPulo_ && celebraR_.cr > 0.05) r.textoEm({38, kFim + 40, 62, 22}, "+" + numero(celebraR_.xp) + " XP", 15, kTinta, kVerde, TextAlign::Center);
                r.textoEm({106, kFim + 46, 60, 22}, "Desfazer", 13, cMudo, kPainel, TextAlign::Center);
            }
            r.desenhar([&](Arduino_GFX& g, int, int) {
                if (!celebrando) return;
                ic::preencherRR(g, 6, kFim + 6, 94, 70, 14, celebraPulo_ ? kPainel : kVerde);
                if (!celebraPulo_) ic::visto(g, 24, kFim + 41, 9, kTinta, kVerde); else ic::pular(g, 24, kFim + 41, 8, cMudo, kPainel);
                ic::preencherRR(g, 106, kFim + 6, 60, 70, 14, kPainel);
                ic::desfazer(g, 136, kFim + 30, 10, cMudo, kPainel);
            });
        }
        // toques: voltar, desfazer, botoes das linhas
        const bool voltar = ui.tap({0, 0, 130, 56});
        const bool desfazer = ui.tap({106, kFim + 6, 60, 70}, celebrando);
        int pressionado = -1;
        for (const auto& [rect, tipo] : botoes)
            if (rect.h > 8 && ui.tap(rect)) pressionado = tipo;
        (void)cFundo;
        if (voltar) { grupoAberto_ = false; return; }
        if (desfazer) { acaoDesfazer(ui, c); return; }
        if (pressionado >= 0) {
            const size_t i = size_t(pressionado / 10);
            switch (pressionado % 10) {
            case 1: acaoFeito(ui, c, i); break;
            case 2: acaoPular(ui, c, i); break;
            case 3: acaoAdiar(c, i); break;
            default: break;
            }
        }
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
            if (med) pilulaAviso(g, r.x + 20 + dx, cy, 1.0, kCarmim);
            else {
                anelAviso(g, r.x + 20 + dx, cy, 8, kLaranja);
                g.fillCircle(r.x + 20 + dx, cy, 3, kLaranja);
            }
            ui.drawText(g, layout, ui.color(Foreground), dx, dy, fundo);
            if (!estreito) ui.drawText(g, dica, ui.color(Muted), dx, dy, fundo);
        });
    }

} // namespace screens
