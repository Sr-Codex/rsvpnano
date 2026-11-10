#pragma once

#include <FS.h>

#include <string>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/Motor.h"
#include "ui/screens/ModernScreens.h"

namespace screens {

    // App "Tempo" v1: Hoje, Batalha (foco = luta contra o "inimigo" do projeto) e Vitoria.
    // Textos em pt-BR direto no codigo (fora do pacote de idiomas). Retangulos simples no lugar
    // dos personagens: os quadros 3D entram no passo 4.
    class TempoScreen {
    public:
        static bool owns(Screen screen) {
            return screen == Screen::TempoHoje || screen == Screen::TempoBatalha || screen == Screen::TempoVitoria;
        }

        // Avanca a batalha (acumula tempo) e a encerra quando o inimigo chega a 0 HP.
        // Devolve true uma vez, no instante em que uma batalha termina com vitoria.
        bool tick(uint32_t nowMs, fs::FS* filesystem);
        Action draw(ui::Context& ui, uint32_t nowMs, Screen& screen, fs::FS* filesystem);
        void boot(uint32_t nowMs, Screen& screen, fs::FS* filesystem);   // BOOT curto
        void back(uint32_t nowMs, Screen& screen, fs::FS* filesystem);   // PWR curto
        bool battleActive() const { return fase_ == Fase::Lutando || fase_ == Fase::Pausada; }
        uint8_t surface() const { return static_cast<uint8_t>(fase_) * 2 + (distraido_ ? 1 : 0); }

    private:
        enum class Fase : uint8_t { Nenhuma, Preparo, Lutando, Pausada, Resultado };

        void carregar(fs::FS* filesystem, bool recarregarEstado, uint32_t nowMs);
        void abrirPreparo(size_t indiceProjeto);
        void iniciar(uint32_t nowMs);
        void pausarOuRetomar(uint32_t nowMs);
        void alternarDistracao(uint32_t nowMs);
        void acumular(uint32_t nowMs);
        void finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem);
        void garantirDia(uint32_t nowMs);
        void salvar(fs::FS* filesystem);
        int64_t restanteMs() const;      // vida restante do inimigo (ms)
        int pontosDeVida() const;        // 0..100
        Action drawHoje(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawVitoria(ui::Context& ui, uint32_t nowMs, Screen& screen);

        bool carregado_ = false;
        uint32_t ultimoDesenhoMs_ = 0;
        fs::FS* fs_ = nullptr;
        tempo::Estado estado_;
        tempo::ListaProjetos lista_;
        std::vector<std::string> titulos_;   // titulos limpos (sem emoji), 1:1 com lista_.projetos
        size_t primeiro_ = 0;                // primeiro projeto visivel (rolagem)
        ui::modern::ListSwipe swipe_;

        Fase fase_ = Fase::Nenhuma;
        size_t projeto_ = 0;
        int planejadoMin_ = 25;
        uint64_t ultimoMs_ = 0;
        uint64_t focadoMs_ = 0;
        uint64_t distraidoMs_ = 0;
        uint64_t distracaoAtualMs_ = 0;
        bool distraido_ = false;
        int numDistracoes_ = 0;
        int maiorDistracaoMin_ = 0;

        struct Resultado {
            tempo::LiquidacaoBatalha liq;
            std::string projeto;
            int planejadoMin = 0;
            int focadoMin = 0;
            int nivelAtributo = 1;
            int streak = 0;
            bool abandonou = false;
        } resultado_;
    };

} // namespace screens
