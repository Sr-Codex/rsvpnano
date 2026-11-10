#pragma once

// PCF85063 (RTC I2C, endereco 0x51): conversao BCD/data em C++ puro, testavel no host.
// Registradores 0x04..0x0A: segundos (bit 7 = OS, oscilador parou), minutos, horas (24 h), dia, dia da semana,
// mes (bit 7 = seculo, ignorado), ano 00..99 (= 2000..2099). O relogio guarda UTC.

#include <array>
#include <cstddef>
#include <cstdint>

namespace BoardDrivers::Pcf85063 {

    inline constexpr uint8_t kAddress = 0x51;
    inline constexpr uint8_t kRegCtrl1 = 0x00;
    inline constexpr uint8_t kRegSeconds = 0x04;
    inline constexpr size_t kTimeRegisters = 7;  // 0x04..0x0A

    struct DateTime {
        int year = 2000;  // 2000..2099
        int month = 1;    // 1..12
        int day = 1;      // 1..31
        int hour = 0;
        int minute = 0;
        int second = 0;
    };

    using TimeRegisters = std::array<uint8_t, kTimeRegisters>;

    constexpr uint8_t toBcd(int value) {
        return static_cast<uint8_t>(((value / 10) << 4) | (value % 10));
    }
    constexpr int fromBcd(uint8_t value) {
        return (value >> 4) * 10 + (value & 0x0F);
    }

    bool validDateTime(const DateTime& value);
    // false = data fora de 2000..2099 ou campo invalido.
    bool encode(const DateTime& value, TimeRegisters& out);
    // false = BCD invalido ou data impossivel. oscillatorStopped = bit OS (hora nao confiavel).
    bool decode(const TimeRegisters& in, DateTime& out, bool& oscillatorStopped);

    // Epoch UTC <-> data/hora UTC (sem depender de TZ). epoch fora de 2000..2099 => false.
    bool toEpoch(const DateTime& value, int64_t& epoch);
    bool fromEpoch(int64_t epoch, DateTime& out);

} // namespace BoardDrivers::Pcf85063
