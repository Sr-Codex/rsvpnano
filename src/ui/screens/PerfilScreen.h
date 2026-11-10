#pragma once

#include <FS.h>

#include <vector>

#include "tempo/Codec.h"
#include "tempo/Perfil.h"
#include "ui/screens/ModernScreens.h"

namespace screens {

    // App "Perfil" (lume.5): 5 paginas trocadas por deslizar o dedo na horizontal (Ficha, Semana, Projetos,
    // Conquistas, Hoje/recibo). Rolagem vertical nas listas. PWR ou "‹ Inicio" volta ao Home. Os dados sao lidos do
    // SD uma vez ao abrir (cache) e nunca dentro do loop de desenho. Textos em pt-BR direto no codigo.
    class PerfilScreen {
    public:
        static constexpr int kPaginas = 5;

        Action draw(ui::Context& ui, uint32_t nowMs, Screen& screen, fs::FS* filesystem);
        void back(Screen& screen) const { screen = Screen::Home; }   // PWR
        uint8_t surface() const { return static_cast<uint8_t>(pagina_); }
        int pagina() const { return pagina_; }

    private:
        void carregar(fs::FS* filesystem, uint32_t nowMs);
        void mudarPagina(int nova);

        void paginaFicha(ui::Context& ui);
        void paginaSemana(ui::Context& ui);
        void paginaProjetos(ui::Context& ui);
        void paginaConquistas(ui::Context& ui);
        void paginaHoje(ui::Context& ui);

        bool carregado_ = false;
        uint32_t ultimoDesenhoMs_ = 0;
        uint32_t carga_ = 0;      // muda a cada carregamento (assinatura)
        int pagina_ = 0;
        int rolagem_ = 0;         // primeira linha visivel das listas
        bool tracking_ = false;
        int16_t x0_ = 0, y0_ = 0;

        tempo::Estado estado_;
        tempo::Resumo resumo_;
        tempo::Semana semana_;
        std::vector<tempo::EventoLog> hoje_;
        tempo::Data data_;
        bool dataValida_ = false;
        int diaSemana_ = 0;       // 0 = segunda
        std::array<tempo::Conquista, tempo::kNumConquistas> conquistas_{};
    };

} // namespace screens
