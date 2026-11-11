#pragma once

#include <Arduino.h>

#include "som/Regras.h"

namespace Board::Audio {

    bool begin();
    // Bip simples (som::SomId::Bip) pela fila de audio: nao bloqueia.
    bool beep();
    bool available();
    // Enfileira um som (tarefa de audio; sintese em blocos). Nao bloqueia. false = descartado / sem audio.
    bool play(const som::Pedido& pedido);
    // Antes de dormir/desligar: espera a fila esvaziar (ate timeoutMs) e, se estourar, para o som com seguranca.
    void waitOrStop(uint32_t timeoutMs);

} // namespace Board::Audio
