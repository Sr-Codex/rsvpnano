#pragma once

#include <FS.h>

#include <string>

#include "ui/screens/TempoScreen.h"

namespace screens {

    // Launcher "B · Agora + apps": barra de status, cartao AGORA (batalha do Tempo / proximo projeto) e grade de
    // 6 apps. Textos em pt-BR direto no codigo (fora do pacote de idiomas).
    struct HomeInfo {
        bool wifi = false;
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
        // Tempo -> Hoje; apps "em breve" mostram um aviso de 2 s e nao navegam.
        Action draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                    const HomeInfo& info);

    private:
        uint32_t breveAteMs_ = 0;
        int breveApp_ = -1;
    };

} // namespace screens
