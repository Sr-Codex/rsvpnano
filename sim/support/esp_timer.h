#pragma once
// Simulador: esp_timer_get_time() = microssegundos desde o boot (relogio monotono do Mac).
#include <chrono>
#include <cstdint>
inline int64_t esp_timer_get_time() {
    static const auto inicio = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::microseconds>(std::chrono::steady_clock::now() - inicio).count();
}
