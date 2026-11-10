#include "board/BoardClock.h"

#include "board/BoardConfig.h"

#if defined(RSVP_BOARD_HAS_PCF85063) && !defined(LUME_SIM)

#include <Arduino.h>
#include <Wire.h>
#include <esp_log.h>
#include <sys/time.h>

#include <ctime>

#include "drivers/rtc/pcf85063/Pcf85063.h"
#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"

namespace Board::Clock {
    namespace {
        constexpr int64_t kMinValidEpoch = 1735689600;  // 2025-01-01T00:00:00Z

        TwoWire& rtcWire() {
            return Wire1;  // mesmo barramento do IMU (SDA 47 / SCL 48)
        }
    } // namespace

    void applyTimeZone() {
        setenv("TZ", kTimeZone, 1);
        tzset();
    }

    void begin() {
        applyTimeZone();
        int64_t epoch = 0;
        if (!BoardDrivers::Pcf85063::readEpoch(rtcWire(), epoch) || epoch < kMinValidEpoch) {
            ESP_LOGW("clock", "RTC sem hora valida; aguardando hora de rede");
            return;
        }
        if (time(nullptr) >= kMinValidEpoch) return;  // sistema ja tem hora (ex.: reinicio por software)
        const timeval now{.tv_sec = static_cast<time_t>(epoch), .tv_usec = 0};
        settimeofday(&now, nullptr);
        ESP_LOGI("clock", "relogio do sistema ajustado pelo RTC: %lld", static_cast<long long>(epoch));
    }

    void saveSystemTime() {
        const int64_t epoch = static_cast<int64_t>(time(nullptr));
        if (epoch < kMinValidEpoch) return;
        if (BoardDrivers::Pcf85063::writeEpoch(rtcWire(), epoch))
            ESP_LOGI("clock", "RTC atualizado com a hora de rede");
        else
            ESP_LOGW("clock", "falha ao gravar o RTC");
    }

} // namespace Board::Clock

#else

namespace Board::Clock {
    void begin() {}
    void applyTimeZone() {}
    void saveSystemTime() {}
} // namespace Board::Clock

#endif
