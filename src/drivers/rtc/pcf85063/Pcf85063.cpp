#include "drivers/rtc/pcf85063/Pcf85063.h"

#include "drivers/rtc/pcf85063/Pcf85063Codec.h"

namespace BoardDrivers::Pcf85063 {

    bool probe(TwoWire& wire) {
        wire.beginTransmission(kAddress);
        return wire.endTransmission(true) == 0;
    }

    bool readEpoch(TwoWire& wire, int64_t& epoch) {
        wire.beginTransmission(kAddress);
        wire.write(kRegSeconds);
        if (wire.endTransmission(true) != 0) return false;
        if (wire.requestFrom(kAddress, kTimeRegisters, true) != kTimeRegisters) return false;
        TimeRegisters regs{};
        for (auto& reg : regs) reg = static_cast<uint8_t>(wire.read());
        DateTime time;
        bool oscillatorStopped = false;
        if (!decode(regs, time, oscillatorStopped) || oscillatorStopped) return false;
        return toEpoch(time, epoch);
    }

    bool writeEpoch(TwoWire& wire, int64_t epoch) {
        DateTime time;
        TimeRegisters regs{};
        if (!fromEpoch(epoch, time) || !encode(time, regs)) return false;
        wire.beginTransmission(kAddress);
        wire.write(kRegSeconds);
        for (const uint8_t reg : regs) wire.write(reg);
        return wire.endTransmission(true) == 0;
    }

} // namespace BoardDrivers::Pcf85063
