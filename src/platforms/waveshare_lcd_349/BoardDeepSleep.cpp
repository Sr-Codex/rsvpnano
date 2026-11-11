#include "board/BoardDeepSleep.h"

#if !defined(LUME_SIM)

#include <Arduino.h>
#include <Wire.h>
#include <driver/rtc_io.h>
#include <esp_log.h>
#include <esp_sleep.h>

#include "board/BoardImu.h"
#include "drivers/gpio/tca9554/Tca9554.h"
#include "platforms/waveshare_lcd_349/PanelSleep.h"
#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"

namespace {
    RTC_DATA_ATTR Board::DeepSleep::Rtc gRtc;
    constexpr uint8_t kImuCtrl7Reg = 0x08;   // QMI8658: 0 = acelerometro e giroscopio desligados
}

namespace Board::DeepSleep {

    bool supported() {
        return true;
    }

    BootCause bootCause() {
        static const BootCause cause = [] {
            switch (esp_sleep_get_wakeup_cause()) {
            case ESP_SLEEP_WAKEUP_TIMER: return BootCause::Timer;
            case ESP_SLEEP_WAKEUP_EXT0: return BootCause::Button;
            default: return BootCause::PowerOn;
            }
        }();
        return cause;
    }

    Rtc& rtc() {
        return gRtc;
    }

    bool enter(uint32_t seconds) {
        using namespace WaveshareLcd349;
        if (seconds == 0)
            return false;
        constexpr gpio_num_t kWake = System::kLightSleepWakeGpio;   // GPIO16: botao PWR (RTC-capable)

        // PWR solto antes de armar (nivel baixo = pressionado; armar com o botao preso acordaria na hora).
        pinMode(static_cast<int>(kWake), INPUT_PULLUP);
        for (uint32_t waited = 0; digitalRead(static_cast<int>(kWake)) == LOW && waited < 3000; waited += 10)
            delay(10);

        // 1) painel em sleep (SLPIN) e backlight apagado (PWM + BL_EN). O App ja chamou Display::sleep(); repetir e inofensivo.
        PanelSleep::enter();
        // 2) audio cortado (AUDIO_EN = 0)
        BoardDrivers::Tca9554::configureOutputPin(Wire1, Tca9554Wiring::kAddress, Tca9554Wiring::kAudioEnablePin, false,
                                                  Tca9554Wiring::kReleaseBusBeforeRead);
        // 3) IMU no menor consumo (sensores desligados; o boot religa em motionReader_.begin())
        Board::Imu::writeRegister(Board::Imu::address(), kImuCtrl7Reg, 0x00);
        // 4) SYS_EN (EXIO6) NAO e tocado: continua em 1 -> 3V3 vivo, TCA9554/RTC/SD/IMU/audio mantidos.

        // 5) despertadores: timer + ext0 no PWR (mesmo nivel logico do sono leve: LOW = pressionado; pull-up interno)
        if (esp_sleep_enable_timer_wakeup(static_cast<uint64_t>(seconds) * 1000000ULL) != ESP_OK) {
            ESP_LOGE("sleep", "deep sleep: timer wakeup failed");
            return false;
        }
        if (esp_sleep_enable_ext0_wakeup(kWake, 0) != ESP_OK) {
            ESP_LOGE("sleep", "deep sleep: ext0 wakeup failed");
            return false;
        }
        rtc_gpio_pulldown_dis(kWake);
        rtc_gpio_pullup_en(kWake);
        ESP_LOGI("sleep", "deep sleep por %u s (timer + PWR/GPIO16 LOW); SYS_EN mantido", static_cast<unsigned>(seconds));
        Serial.flush();
        esp_deep_sleep_start();
        return false;   // nunca chega aqui
    }

} // namespace Board::DeepSleep

#endif // !LUME_SIM
