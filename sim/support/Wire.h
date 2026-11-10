#pragma once
// Lume Nano simulator: no I2C bus (touch/IMU/expander are virtual in sim/platform).
#include <Arduino.h>
class TwoWire : public Stream {
public:
    bool begin(int = -1, int = -1, uint32_t = 0) { return true; }
    bool end() { return true; }
    void setClock(uint32_t) {}
    void setTimeOut(uint16_t) {}
    void beginTransmission(uint8_t) {}
    uint8_t endTransmission(bool = true) { return 2; } // NACK: no device
    size_t requestFrom(uint8_t, size_t, bool = true) { return 0; }
    size_t write(uint8_t) override { return 0; }
    size_t write(const uint8_t*, size_t) override { return 0; }
    using Print::write;
};
inline TwoWire Wire;
inline TwoWire Wire1;
