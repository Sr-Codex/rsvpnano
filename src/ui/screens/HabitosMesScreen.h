#pragma once

#include <vector>

#include "tempo/HabitosMes.h"
#include "ui/screens/HabitosScreen.h"

namespace screens {
    class HabitosMesScreen {
      public:
        void draw(ui::Context& ui, uint32_t nowMs, Screen& screen, TempoScreen& tempo, fs::FS* filesystem,
                  const HabitosInfo& info);
        static void mostrarErroRegistro(ui::Context& ui, TempoScreen& tempo, Screen& screen);
        void back(Screen& screen);
        void abrirAviso(const tempo::Aviso& aviso, bool confirmarDose = false);
        uint8_t surface() const { return 10 + static_cast<uint8_t>(vista_) + 4 * static_cast<uint8_t>(modo_); }

      private:
        enum class Vista { Cartoes, Detalhe, Meses, AvisoExpirado };
        enum class Modo { Mes, CincoDias, Semanas };
        Vista vista_ = Vista::Cartoes;
        Modo modo_ = Modo::Mes;
        int mes_ = 0, anoSeletor_ = 0, pagina_ = 0, rolagem_ = 0;
        int dataCache_ = 0, mesCache_ = 0;
        int64_t proximoDiaExclusivo_ = 0;
        std::optional<int64_t> erroMaisRecente_;
        uint32_t revisaoHistorico_ = 0;
        fs::FS* fsCache_ = nullptr;
        tempo::DiasPorHabito dias_;
        std::string selecionado_;
        std::optional<int> horario_;
        std::optional<tempo::Aviso> aviso_;
        uint32_t marcouMs_ = 0, adiouMs_ = 0;
        bool desfazer_ = false, adiou_ = false;
        int diaConfirmacao_ = 0;
        uint32_t toqueAcao_ = 0;
        bool arrastando_ = false, moveu_ = false;
        int toqueX_ = 0, toqueY_ = 0, rolagemInicial_ = 0;
        struct AlvoCartao {
            ui::Rect area;
            std::string id;
        };
        std::vector<AlvoCartao> cartoesExibidos_;
        std::string cartaoTocado_;
        std::string_view carregarHistorico(fs::FS* fs, const tempo::Data& hoje, const tempo::MesHabitos& mes);
    };
} // namespace screens
