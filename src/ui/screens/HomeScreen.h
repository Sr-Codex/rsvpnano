#pragma once

#include <FS.h>

#include <string>

#include "ui/screens/TempoScreen.h"

namespace screens {

    // Launcher "B2": barra de status (hora, data, Wi-Fi e bateria desenhados), cartao AGORA (batalha do Tempo /
    // proximo projeto) e 2 x 2 blocos grandes (Tempo, Leitor, Perfil, Ajustes). Textos em pt-BR direto no codigo (fora do pacote de idiomas).
    struct HomeInfo {
        bool wifi = false;
        bool sincronizando = false;   // sync do TickTick em segundo plano: ponto ambar discreto na barra de status
        bool bateriaPresente = false;
        int bateriaPct = 0;
        bool carregando = false;
        bool temLivro = false;
        std::string livro;
        int livroPct = 0;
    };

    class HomeScreen {
    public:
        // Desenha so o que mudou. Leitor -> menu do leitor (Screen::Read); Ajustes -> Screen::Settings;
        // Tempo -> Inicio; Perfil -> Screen::Perfil.
        Action draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                    const HomeInfo& info);
    };

} // namespace screens
