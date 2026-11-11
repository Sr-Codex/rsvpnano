#pragma once

#include <FS.h>

#include <array>
#include <optional>
#include <string>

#include "tempo/Lembretes.h"
#include "ui/screens/TempoScreen.h"

namespace screens {

    // App "Habitos" v2. Deitada (640x172, direcao B): anel do dia a esquerda (externo = dia em verde; interno = janela do habito),
    // texto ao centro, pontos do dia embaixo (tocar abre a lista do dia) e acoes a direita (Feito 160x76 / Pular 160x56);
    // celebracao apos marcar (anel completa, faiscas, "+cr +XP", chama, "Proximo" e "Desfazer" por 8 s) e dia completo
    // (anel cheio + semana). Em pe (172x640, IMU, P1): cabecalho com anel do dia, eixo 06h-23h proporcional com grupos
    // ("Almoco +2"), linha "agora" e botao Feito fixo; tocar num grupo abre a lista do grupo (estilo P2). A logica (cartao,
    // grupos, selecao, rolagem, desfazer) e pura e testada em src/tempo/HabitosVista.* e Motor; aqui so desenho e toque.
    // Textos em pt-BR direto no codigo (fora do pacote de idiomas). PWR ou "‹ Habitos" volta ao Home.
    struct HabitosInfo {
        std::string erroSync;   // net::ticktick::status().habitsError ("" = ok)
        bool sincronizando = false;   // sync do TickTick em segundo plano (indicador discreto)
    };

    class HabitosScreen {
    public:
        Action draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                    const HabitosInfo& info);
        void back(Screen& screen) const { screen = Screen::Home; }   // PWR

    private:
        struct Cena;   // dados do quadro (estado + derivados), montados em draw()
        void carregarFila(fs::FS* filesystem);
        void carregarSemana(fs::FS* filesystem, const tempo::Data& hoje, int feitasHoje);
        void desenharCartao(ui::Context& ui, const Cena& c, Screen& screen);
        void desenharListaDia(ui::Context& ui, const Cena& c);
        void desenharLinhaEmPe(ui::Context& ui, const Cena& c, Screen& screen);
        void desenharGrupoEmPe(ui::Context& ui, const Cena& c);
        // acoes (chamadas DEPOIS do desenho: o estado muda para o proximo quadro)
        void acaoFeito(ui::Context& ui, const Cena& c, size_t indice);
        void acaoPular(ui::Context& ui, const Cena& c, size_t indice);
        void acaoAdiar(const Cena& c, size_t indice);
        void acaoDesfazer(ui::Context& ui, const Cena& c);
        void iniciarCelebracao(const Cena& c, size_t indice, const tempo::ResultadoOcorrencia& r, bool pulo, double janelaAntes);
        void arrastar(ui::Context& ui, ui::Rect area, int& rolagemPx, int maximoPx);

        bool aberta_ = false;
        uint32_t ultimoDesenhoMs_ = 0;
        uint32_t ultimaFilaMs_ = 0;
        int modoTela_ = 0;                    // 1 cartao, 2 lista do dia, 3 linha em pe, 4 grupo em pe (mudou = invalida a tela)
        std::optional<size_t> selecionado_;   // ocorrencia escolhida na lista (nullopt = automatica)
        bool listaAberta_ = false;            // deitada: lista do dia
        int listaRolagemPx_ = 0;
        bool grupoAberto_ = false;            // em pe: lista do grupo
        int grupoMinuto_ = 0;                 // minuto inicial do grupo aberto (identifica o grupo)
        int grupoRolagemPx_ = 0;
        bool arrastando_ = false;
        bool moveu_ = false;
        int16_t arrastoY_ = 0;
        int filaTotal_ = 0;                   // check-ins aguardando Wi-Fi
        int filaParados_ = 0;                 // itens que estouraram as tentativas (HTTP de erro)
        uint32_t adiouEmMs_ = 0;              // "Ainda nao": confirmacao curta no cartao
        size_t adiouIdx_ = 0;
        int adiouAteMin_ = 0;
        // celebracao (Feito/Pular recente; some em 8 s, ao desfazer ou ao tocar em "Proximo")
        bool celebra_ = false;
        bool celebraPulo_ = false;
        uint32_t celebraMs_ = 0;
        size_t celebraIdx_ = 0;
        tempo::ResultadoOcorrencia celebraR_;
        double celebraJanela0_ = 0.0;
        // semana (dia completo): feitos dos ultimos 7 dias, lidos do log 1x por dia/valor
        int semanaChave_ = 0;
        std::array<int, 7> semana_{};
    };

    // Aviso de lembrete no topo de qualquer tela (App). Desenha por cima do que ja esta na tela; o App repinta por
    // `invalidate()` ao esconder. `rect` serve de area de toque (tocar abre os Habitos).
    ui::Rect avisoHabitoRect(const ui::Context& ui);
    void desenharAvisoHabito(ui::Context& ui, const tempo::Aviso& aviso);

} // namespace screens
