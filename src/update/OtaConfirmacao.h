#pragma once

// Confirmacao da imagem OTA nova (rollback automatico, ADR-010). C++ puro, sem hardware.
//
// O bootloader tem CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE: a imagem recem-gravada nasce PENDING_VERIFY e, se a placa
// reiniciar sem que o app a marque como valida, o bootloader volta para a imagem anterior. O core Arduino marca a
// imagem como valida antes do setup(); aqui o app adia isso (verifyRollbackLater() = true em main.cpp) e so confirma
// quando esta SAUDAVEL (setup concluido + primeiro quadro + toque/SD + laco rodando sem travar).
//
// Armadilha: acordar do deep sleep e desligar/religar tambem sao RESETS. Por isso, antes de dormir/desligar/reiniciar,
// quem ja chegou a "setup + primeiro quadro" confirma na hora (deveConfirmarAntesDeReiniciar).

#include <cstdint>

namespace ota {

    // Estado da particao em execucao (esp_ota_get_state_partition). SemEstado = a chamada falhou ou a imagem veio de
    // fabrica/por cabo (sem otadata): nada a confirmar.
    enum class EstadoImagem : uint8_t { SemEstado, Pendente, Valida, Outro };

    inline constexpr uint32_t kLacoSaudavelMs = 10000;   // laco rodando sem travar desde o primeiro quadro
    inline constexpr uint32_t kLacoSemSdMs = 60000;      // sem cartao SD o criterio e mais longo (cartao ausente nao e culpa do firmware)
    inline constexpr uint32_t kLacunaMaximaMs = 5000;    // intervalo entre dois tick() acima disso = suspeita de travamento; reinicia a contagem

    class Confirmador {
    public:
        void setupConcluido() { setup_ = true; }
        void toqueOk(bool ok) { toque_ = ok; }
        void sdMontado(bool ok) { sd_ = ok; }
        void primeiroQuadro(uint32_t nowMs) {
            if (!quadro_) {
                quadro_ = true;
                inicioLacoMs_ = nowMs;
                ultimoTickMs_ = nowMs;
            }
        }

        // Chamar a cada volta do laco. Mede a "saude" do laco: uma lacuna grande entre chamadas zera a contagem.
        void tick(uint32_t nowMs) {
            if (!quadro_) return;
            if (static_cast<uint32_t>(nowMs - ultimoTickMs_) > kLacunaMaximaMs) inicioLacoMs_ = nowMs;
            ultimoTickMs_ = nowMs;
        }

        // Saudavel o bastante para confirmar no laco normal?
        bool deveConfirmar(EstadoImagem estado, uint32_t nowMs) const {
            if (estado != EstadoImagem::Pendente || confirmada_) return false;
            if (!setup_ || !quadro_ || !toque_) return false;
            const uint32_t exigidoMs = sd_ ? kLacoSaudavelMs : kLacoSemSdMs;
            return static_cast<uint32_t>(nowMs - inicioLacoMs_) >= exigidoMs;
        }

        // Antes de dormir (deep sleep), desligar ou reiniciar de proposito: setup + primeiro quadro bastam.
        bool deveConfirmarAntesDeReiniciar(EstadoImagem estado) const {
            return estado == EstadoImagem::Pendente && !confirmada_ && setup_ && quadro_;
        }

        void confirmada() { confirmada_ = true; }
        bool jaConfirmada() const { return confirmada_; }
        bool setup() const { return setup_; }
        bool quadro() const { return quadro_; }

    private:
        bool setup_ = false;
        bool quadro_ = false;
        bool toque_ = false;
        bool sd_ = false;
        bool confirmada_ = false;
        uint32_t inicioLacoMs_ = 0;
        uint32_t ultimoTickMs_ = 0;
    };

} // namespace ota
