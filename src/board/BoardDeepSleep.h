#pragma once

#include <cstdint>

#include "board/BoardConfig.h"

// Deep sleep com despertador (ADR-009): o ESP32 dorme com o SYS_EN MANTIDO e acorda por timer (proximo lembrete de
// habito) ou pelo botao PWR. So a placa Waveshare LCD 3.49 (e o simulador) implementam; as demais respondem
// "nao suportado" e o desligamento automatico segue como antes (soltar o SYS_EN).
namespace Board::DeepSleep {

    enum class BootCause : uint8_t {
        PowerOn,   // energia / reset / software restart / qualquer outro: boot normal
        Timer,     // acordou do deep sleep pelo despertador: boot "de lembrete"
        Button,    // acordou do deep sleep pelo botao PWR: boot normal
    };

    // Memoria que sobrevive ao deep sleep (RTC). So vale quando bootCause() != PowerOn; no boot normal o App zera.
    struct Rtc {
        uint32_t magic = 0;
        int32_t ciclosSemInteracao = 0;       // despertares por timer seguidos sem interacao
        int32_t insistenciasMedicacao = 0;    // despertares de insistencia de medicacao no dia
        int32_t stampDia = 0;                 // AAAAMMDD a que insistenciasMedicacao se refere
        int64_t ultimoAvisoMedicacaoEpoch = 0;   // 0 = desconhecido
        int64_t alvoEpoch = 0;                // lembrete que motivou o ultimo sono
        uint8_t origemProxima = 0;            // tempo::OrigemDespertar do ultimo sono
    };
    inline constexpr uint32_t kRtcMagic = 0x4C554D39;   // "LUM9"

#if defined(RSVP_BOARD_HAS_DEEP_SLEEP)
    bool supported();
    BootCause bootCause();   // lido uma vez no boot (esp_sleep_get_wakeup_cause)
    Rtc& rtc();
    // Entra em deep sleep por `seconds` (timer) e PWR (ext0, GPIO16 em nivel BAIXO). Chamar DEPOIS de salvar tudo e de
    // fechar SD/entrada. Nao volta em caso de sucesso (o ESP32 reinicia no acordar); devolve false se falhar.
    bool enter(uint32_t seconds);
#else
    inline bool supported() { return false; }
    inline BootCause bootCause() { return BootCause::PowerOn; }
    inline Rtc& rtc() { static Rtc r; return r; }
    inline bool enter(uint32_t) { return false; }
#endif

} // namespace Board::DeepSleep
