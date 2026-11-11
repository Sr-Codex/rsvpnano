#include <cstdio>
#include <iterator>
#include <string>

#include "ui/screens/ModernScreens.h"
#include "ui/screens/ScreenCommon.h"

// Ajustes > Sons (docs/app-tempo/sons.md): volume (baixo/medio/alto), sons de jogo liga/desliga (lembretes sempre
// tocam), horario de silencio (liga/desliga + inicio/fim em passos de 30 min; lembrete comum respeita, medicacao nao)
// e "Ouvir" para testar o volume; mais, na coluna da direita, o "Modo Foco" do app Tempo (Desligado / Escurecer / Cena:
// apos 10 s sem toque a batalha escurece ou da lugar a uma cena) e a "Cena" (QG Neon / QG Cidade), que so aparece com o
// Modo Foco em Cena. Textos em pt-BR direto no codigo (como o app Tempo).
//
//   [Volume ][Sons de jogo][Silencio ]  [Modo Foco]
//   [- Inicio +][- Fim +]   [Ouvir]     [Cena     ]   <- so com Modo Foco = Cena
namespace screens {
    namespace {
        std::string hora(int minuto) {
            char texto[8];
            std::snprintf(texto, sizeof texto, "%02d:%02d", minuto / 60 % 24, minuto % 60);
            return texto;
        }

        constexpr const char* kVolumes[] = {"Baixo", "Médio", "Alto"};
        constexpr const char* kModosFoco[] = {"Desligado", "Escurecer", "Cena"};   // settings::FocusMode
        constexpr const char* kCenas[] = {"QG Neon", "QG Cidade"};                 // settings::FocusScene

        template<typename Hora>
        bool passo(ui::Context& ui, int16_t x, const char* nome, Hora& valor, bool ativo) {
            using ui::modern::button;
            using ui::modern::Style;
            bool mudou = false;
            if (button(ui, {x, 111, 46, 53}, "−", Style::Secondary, ativo, {}, 22)) {
                valor = valor <= 0 ? 1410 : int(valor) - 30;
                mudou = true;
            }
            ui.label({int16_t(x + 50), 114, 98, 20}, nome, 11, ui::themes::Muted, ui::TextAlign::Center);
            ui.label({int16_t(x + 50), 132, 98, 30}, ativo ? hora(valor) : std::string{"--:--"}, 22,
                     ativo ? ui::themes::Foreground : ui::themes::Muted, ui::TextAlign::Center);
            if (button(ui, {int16_t(x + 152), 111, 46, 53}, "+", Style::Secondary, ativo, {}, 22)) {
                valor.cycle();
                mudou = true;
            }
            return mudou;
        }
    } // namespace

    bool sonsSettings(ui::Context& ui, settings::InterfaceSettings& config, Screen& screen, bool& testar) {
        using ui::modern::button;
        using ui::modern::Style;
        bool mudou = false;
        if (modern::header(ui, "Sons e foco", screen, Screen::Settings) == Action::Resume) screen = Screen::Reader;

        if (button(ui, {12, 51, 162, 53}, "Volume", Style::Secondary, true, kVolumes[config.soundVolume], 14,
                   ui::TextAlign::Start)) {
            config.soundVolume.cycle();
            mudou = true;
            testar = true;   // ouve o novo volume
        }
        if (button(ui, {182, 51, 162, 53}, "Sons de jogo", Style::Secondary, true,
                   config.gameSounds ? "Ligados" : "Desligados", 14, ui::TextAlign::Start)) {
            config.gameSounds = !config.gameSounds;
            mudou = true;
        }
        const std::string detalheSilencio = config.quietHours
            ? hora(config.quietStartMinute) + " às " + hora(config.quietEndMinute) : std::string{"Desligado"};
        if (button(ui, {352, 51, 162, 53}, "Silêncio", Style::Secondary, true, detalheSilencio, 14,
                   ui::TextAlign::Start)) {
            config.quietHours = !config.quietHours;
            mudou = true;
        }

        mudou |= passo(ui, 12, "Início", config.quietStartMinute, config.quietHours);
        mudou |= passo(ui, 221, "Fim", config.quietEndMinute, config.quietHours);
        if (button(ui, {430, 111, 84, 53}, "Ouvir", Style::Primary, true, "Testa", 14, ui::TextAlign::Start))
            testar = true;
        // Modo Foco (app Tempo): Desligado -> Escurecer -> Cena -> Desligado. A cena escolhida fica guardada mesmo com o
        // Modo Foco em outro valor.
        const auto modo = static_cast<size_t>(config.focusMode) % std::size(kModosFoco);
        if (button(ui, {522, 51, 106, 53}, "Modo Foco", Style::Secondary, true, kModosFoco[modo], 14,
                   ui::TextAlign::Start)) {
            config.focusMode = static_cast<settings::FocusMode>((modo + 1) % std::size(kModosFoco));
            mudou = true;
        }
        if (config.focusMode == settings::FocusMode::scene) {
            const auto cena = static_cast<size_t>(config.focusScene) % std::size(kCenas);
            if (button(ui, {522, 111, 106, 53}, "Cena", Style::Secondary, true, kCenas[cena], 14, ui::TextAlign::Start)) {
                config.focusScene = static_cast<settings::FocusScene>((cena + 1) % std::size(kCenas));
                mudou = true;
            }
        }
        return mudou;
    }

} // namespace screens
