#pragma once

#include <cstdint>

// Relogio de parede da placa: fuso e RTC de bateria.
// Waveshare LCD 3.49: America/Sao_Paulo (UTC-3, sem horario de verao) + RTC PCF85063 (UTC).
// Outras placas e o simulador: no-ops (o simulador segue a hora do Mac).
namespace Board::Clock {

    // Fuso POSIX usado no boot da placa: UTC-3 fixo.
    inline constexpr const char* kTimeZone = "<-03>3";

    // Define o fuso e, se o RTC tiver hora valida (ano >= 2025), ajusta o relogio do sistema (settimeofday).
    // Chamar depois de Board::System::begin() abrir o barramento I2C.
    void begin();

    // Reaplica o fuso (configTime() do Arduino sobrescreve TZ).
    void applyTimeZone();

    // Grava a hora do sistema no RTC (so se for valida, ano >= 2025). Chamar apos obter hora de rede (SNTP).
    void saveSystemTime();

} // namespace Board::Clock
