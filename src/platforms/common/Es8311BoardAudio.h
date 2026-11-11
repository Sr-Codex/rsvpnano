#pragma once

#include <Arduino.h>

#include <esp_log.h>
#include "board/BoardPower.h"
#include "drivers/audio/es8311/Es8311.h"
#include "som/Tocador.h"

namespace {

    constexpr char kAudioTag[] = "audio";
    constexpr uint32_t kAudioStartupDelayMs = 15;
    constexpr uint32_t kWriteTimeoutMs = 250;

    bool enableAudioRail() {
        return Board::Power::enableAudioPowerIfAvailable();
    }

    // Sons gerados na placa (src/som): tarefa de audio + fila. O codec so e configurado (I2C) na thread de quem pede.
    som::Tocador gTocador;

    bool prepararSaida(void* contexto) {
        auto& context = *static_cast<BoardDrivers::Es8311::Context*>(contexto);
        if (!BoardDrivers::Es8311::available(context)) {
            if (!enableAudioRail()) {
                ESP_LOGW(kAudioTag, "Audio rail unavailable");
                return false;
            }
            delay(kAudioStartupDelayMs);
            if (!BoardDrivers::Es8311::begin(context)) return false;
        }
        return enableAudioRail() && BoardDrivers::Es8311::prepareOutput(context);
    }

    bool escreverSaida(void* contexto, const int16_t* estereo, size_t amostras) {
        auto& context = *static_cast<BoardDrivers::Es8311::Context*>(contexto);
        if (BoardDrivers::Es8311::writeSamples(context, estereo, amostras, kWriteTimeoutMs)) return true;
        ESP_LOGW(kAudioTag, "Retrying speaker write after recovering output path");
        if (!enableAudioRail() || !BoardDrivers::Es8311::recoverOutputPath(context)) return false;
        return BoardDrivers::Es8311::writeSamples(context, estereo, amostras, kWriteTimeoutMs);
    }

    void aoComecarSom(void*, som::SomId id) {
        ESP_LOGI(kAudioTag, "som: %s", som::nome(id));
    }

} // namespace

namespace BoardPlatform::Es8311BoardAudio {

    bool begin(BoardDrivers::Es8311::Context& context) {
        if (BoardDrivers::Es8311::available(context)) {
            return true;
        }

        if (!enableAudioRail()) {
            ESP_LOGW(kAudioTag, "Audio rail unavailable");
            return false;
        }

        delay(kAudioStartupDelayMs);
        return BoardDrivers::Es8311::begin(context);
    }

    bool play(BoardDrivers::Es8311::Context& context, const som::Pedido& pedido) {
        som::Saida saida;
        saida.preparar = &prepararSaida;
        saida.escrever = &escreverSaida;
        saida.aoComecar = &aoComecarSom;
        saida.contexto = &context;
        return gTocador.iniciar(saida) && gTocador.tocar(pedido);
    }

    void waitOrStop(uint32_t timeoutMs) {
        gTocador.esperarOuParar(timeoutMs);
    }

    bool available(const BoardDrivers::Es8311::Context& context) {
        return BoardDrivers::Es8311::available(context);
    }

} // namespace BoardPlatform::Es8311BoardAudio
