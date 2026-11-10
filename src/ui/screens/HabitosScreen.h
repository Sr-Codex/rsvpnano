#pragma once

#include <FS.h>

#include <optional>
#include <string>

#include "tempo/Lembretes.h"
#include "ui/screens/TempoScreen.h"

namespace screens {

    // App "Habitos" (fase 2). Deitada (640x172): cartao AGORA/PROXIMO a esquerda (Feito agora / Pular; medicacao = "Sim, tomei
    // agora" / "Ainda nao") + lista rolavel a direita. Em pe (172x640, IMU): linha do tempo vertical do dia. A logica
    // (qual cartao, selecao, rolagem) e pura e testada em src/tempo/HabitosVista.*; aqui so desenho e toque.
    // Textos em pt-BR direto no codigo (fora do pacote de idiomas). PWR ou "‹ HABITOS" volta ao Home.
    struct HabitosInfo {
        std::string erroSync;   // net::ticktick::status().habitsError ("" = ok)
    };

    class HabitosScreen {
    public:
        Action draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                    const HabitosInfo& info);
        void back(Screen& screen) const { screen = Screen::Home; }   // PWR

    private:
        void carregarFila(fs::FS* filesystem);
        void desenharDeitada(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                             const HabitosInfo& info, int minuto, const tempo::Data& data, bool horaValida);
        void desenharEmPe(ui::Context& ui, Screen& screen, const tempo::Estado& estado, int minuto, const tempo::Data& data);

        bool aberta_ = false;
        uint32_t ultimoDesenhoMs_ = 0;
        uint32_t ultimaFilaMs_ = 0;
        std::optional<size_t> selecionado_;   // ocorrencia escolhida na lista (nullopt = automatica)
        int rolagem_ = 0;
        bool rolagemManual_ = false;          // o usuario rolou: nao recentrar sozinho
        bool tracking_ = false;
        int16_t x0_ = 0, y0_ = 0;
        int filaTotal_ = 0;                   // check-ins aguardando Wi-Fi
        int filaParados_ = 0;                 // itens que estouraram as tentativas (HTTP de erro)
        int ultimaFocoLinha_ = -1;
        uint32_t adiouEmMs_ = 0;              // "Ainda nao": confirmacao curta no cartao
        size_t adiouIdx_ = 0;
        int adiouAteMin_ = 0;
    };

    // Aviso de lembrete no topo de qualquer tela (App). Desenha por cima do que ja esta na tela; o App repinta por
    // `invalidate()` ao esconder. `rect` serve de area de toque (tocar abre os Habitos).
    ui::Rect avisoHabitoRect(const ui::Context& ui);
    void desenharAvisoHabito(ui::Context& ui, const tempo::Aviso& aviso);

} // namespace screens
