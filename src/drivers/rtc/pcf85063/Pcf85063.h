#pragma once

#include <Arduino.h>
#include <Wire.h>

#include <cstdint>

namespace BoardDrivers::Pcf85063 {

    bool probe(TwoWire& wire);
    // true = hora lida e confiavel (oscilador nao parou, data valida). epoch UTC.
    bool readEpoch(TwoWire& wire, int64_t& epoch);
    bool writeEpoch(TwoWire& wire, int64_t epoch);

} // namespace BoardDrivers::Pcf85063
