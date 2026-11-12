#pragma once

// Relogio do app "Tempo": hora de parede local + escala de tempo opcional.
// Sem RTC ainda (passo 7): usa time() do sistema se valido (ano >= 2025); senao "data desconhecida".
// A escala (padrao 1) e um ponto de teste: so o simulador a altera (sim/main.cpp).

#include <cstdint>
#include <string>
#include <string_view>

#include "tempo/Tipos.h"

namespace tempo {

    struct Agora {
        bool valida = false;       // false = data/hora desconhecidas (sem RTC nem hora do sistema)
        Data data;
        int minutoDoDia = 0;       // 0..1439
        int segundoDoDia = 0;      // 0..86399
    };

    constexpr int kAnoMinimoValido = 2025;

    // Escala de tempo do app (1 = tempo real). Afeta ms() e agora().
    void definirEscala(uint32_t escala);
    uint32_t escala();

    // Milissegundos do app a partir do millis() do firmware (millis * escala).
    uint64_t ms(uint32_t nowMs);

    // Converte segundos-epoch em data/hora; utc=true ignora o fuso (testes).
    Agora deEpoch(int64_t epoch, bool utc = false);

    // Hora de parede agora. Ancora na 1a chamada com hora de sistema valida e avanca por ms().
    Agora agora(uint32_t nowMs);

    // Epoch (UTC, s) agora pelo relogio do app; 0 se a hora e desconhecida. Na placa = hora do sistema (RTC/NTP).
    int64_t epochAgora(uint32_t nowMs);

    // "AAAA-MM-DDTHH:MM:SS..." (UTC; sufixo apos os segundos ignorado) -> epoch (s). 0 = invalido.
    int64_t epochDeIso(std::string_view iso);
    // Hora da ultima sincronizacao para a tela, em hora LOCAL (fuso `fusoMin`, padrao param::kFusoMinutos): "hoje 14:20",
    // "ontem 14:20" ou "12/11 14:20" (dia por meia-noite local). `agoraEpoch` <= 0 (sem relogio) = sempre "dd/mm hh:mm".
    // iso invalido/vazio = "nunca".
    std::string textoHoraSincronizacao(std::string_view iso, int64_t agoraEpoch, int fusoMin);

    // Reancora o relogio (ex.: quando o RTC/NTP fornecer a hora). epoch invalido => desancora.
    void ancorar(int64_t epoch, uint32_t nowMs);

} // namespace tempo
