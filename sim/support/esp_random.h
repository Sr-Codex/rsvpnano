#pragma once
#include <cstdint>
#include <random>
inline uint32_t esp_random() { static std::random_device device; return device(); }
inline void esp_fill_random(void* buffer, size_t length) {
    auto* bytes = static_cast<uint8_t*>(buffer);
    for (size_t i = 0; i < length; ++i) bytes[i] = static_cast<uint8_t>(esp_random());
}
