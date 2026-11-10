// Lume Nano simulator: virtual Waveshare ESP32-S3-Touch-LCD-3.49 Rev2.
// Implements the Board::* interfaces the firmware uses; the SDL window (sim/main.cpp) shows the
// framebuffer and feeds touch/buttons.
#include "board/Board.h"
#include "board/BoardImu.h"

#include <canvas/Arduino_Canvas.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <mutex>
#include <filesystem>

#include "platforms/waveshare_lcd_349/WaveshareLcd349.h"
#include "SimBoard.h"
#include "SimRuntime.h"

namespace sim::board {
    Frame& frame() { static Frame value; return value; }
    Panel& panel() { static Panel value; return value; }
    TouchState& touch() { static TouchState value; return value; }
    Buttons& buttons() { static Buttons value; return value; }
    PowerState& power() { static PowerState value; return value; }
    Motion& motion() { static Motion value; return value; }
    std::atomic<uint8_t>& manualTurns() { static std::atomic<uint8_t> value{0}; return value; }
    const char* poseLabel(Pose pose) {
        switch (pose) {
        case Pose::FlatFaceUp: return "deitada, tela para cima";
        case Pose::FlatFaceDown: return "deitada, tela para baixo";
        case Pose::ShortAUp: return "em pé, ponta A para cima";
        case Pose::ShortBUp: return "em pé, ponta B para cima";
        }
        return "?";
    }
} // namespace sim::board

namespace {

    // Full-frame canvas like the board's RowPrefixCanvas; flush() publishes to the window.
    class SimCanvas final : public Arduino_Canvas {
    public:
        SimCanvas() : Arduino_Canvas(sim::board::kPanelWidth, sim::board::kPanelHeight, nullptr) {}

        void flush(bool = false) override {
            auto& shared = sim::board::frame();
            const uint16_t* pixels = getFramebuffer();
            if (!pixels)
                return;
            std::lock_guard lock(shared.mutex);
            std::memcpy(shared.pixels.data(), pixels, shared.pixels.size() * sizeof(uint16_t));
            ++shared.serial;
            if (shared.uiRotation != getRotation()) {
                static const char* kNames[] = {"Portrait", "LandscapeFlipped", "PortraitFlipped", "Landscape"};
                std::fprintf(stderr, "[sim] UI orientation -> %s\n", kNames[getRotation() & 3U]);
            }
            shared.uiRotation = getRotation(); // ui::Context::setOrientation → gfx.setRotation
        }
    };

    SimCanvas& canvas() {
        static SimCanvas value;
        return value;
    }

    fs::FS gSd;
    bool gSdMounted = false;

} // namespace

// ------------------------------------------------------------------ Display
namespace Board::Display {

    bool begin() {
        if (!canvas().begin(GFX_SKIP_OUTPUT_BEGIN)) {
            ESP_LOGE("display", "simulated canvas allocation failed");
            return false;
        }
        // Start in the board's default UI orientation so the window doesn't flash portrait at boot.
        canvas().setRotation(static_cast<uint8_t>(WaveshareLcd349::DisplayWiring::kDefaultUiOrientation));
        canvas().fillScreen(0x0000);
        canvas().flush();
        ESP_LOGI("display", "simulated panel ready %dx%d", sim::board::kPanelWidth, sim::board::kPanelHeight);
        return true;
    }

    Arduino_GFX& gfx() { return canvas(); }
    ui::Orientation defaultUiOrientation() { return WaveshareLcd349::DisplayWiring::kDefaultUiOrientation; }
    ui::Orientation rotatedUiOrientation() { return ui::opposite(WaveshareLcd349::DisplayWiring::kDefaultUiOrientation); }
    uint16_t nativeWidth() { return WaveshareLcd349::DisplayWiring::kPanelWidth; }
    uint16_t nativeHeight() { return WaveshareLcd349::DisplayWiring::kPanelHeight; }
    size_t txChunkBytes() { return WaveshareLcd349::DisplayWiring::kTxChunkBytes; }
    void setBacklight(bool on) { sim::board::panel().backlight = on; }
    void setBrightness(uint8_t percent) { sim::board::panel().brightnessPercent = percent; }
    void sleep() { sim::board::panel().sleeping = true; }
    void wake() {
        canvas().flush(true);
        sim::board::panel().sleeping = false;
        sim::board::panel().backlight = true;
    }
    bool pushColors(uint16_t x, uint16_t y, uint16_t width, uint16_t height, const uint16_t* data) {
        canvas().draw16bitRGBBitmap(x, y, const_cast<uint16_t*>(data), width, height);
        return true;
    }

} // namespace Board::Display

// ------------------------------------------------------------------ Input (same mapping as the 3.49)
namespace Board::Input {

    bool begin() { return true; }
    void end() {}
    void cancel() {}
    ::Input::ControlTiming controlTiming() { return {}; }

    ::Input::PressActions currentActions() {
        ::Input::PressActions actions = {};
        if (sim::board::buttons().boot) {
            actions.shortPress |= ::Input::ActionSelect | ::Input::ActionPlayPause;
            actions.longPress |= ::Input::ActionStandby;
        }
        if (sim::board::buttons().power) {
            actions.shortPress |= ::Input::ActionOpenMenu | ::Input::ActionBack;
            actions.longPress |= ::Input::ActionPowerOff;
        }
        return actions;
    }

    ui::TouchSurface touchSurface() {
        return {WaveshareLcd349::DisplayWiring::kPanelWidth, WaveshareLcd349::DisplayWiring::kPanelHeight};
    }

    ::Input::TouchTiming touchTiming() {
        return {
            .readyPollIntervalMs = WaveshareLcd349::TouchWiring::kReadyPollIntervalMs,
            .pollIntervalMs = WaveshareLcd349::TouchWiring::kPollIntervalMs,
        };
    }

    bool beginTouch() { return true; }
    bool touchReady() { return sim::board::touch().down; }

    bool readTouch(ui::TouchContact& contact) {
        auto& touch = sim::board::touch();
        int px = 0, py = 0;
        sim::board::viewToPanel(touch.presentation, touch.x, touch.y, px, py);
        contact = {touch.down.load(), static_cast<uint16_t>(px), static_cast<uint16_t>(py)};
        return true;
    }

} // namespace Board::Input

// ------------------------------------------------------------------ Power
namespace Board::Power {

    void begin() {}
    bool enableAudioPowerIfAvailable() { return false; }

    bool readBatteryStatus(BatteryStatus& status) {
        const int percent = sim::board::power().batteryPercent;
        status.present = true;
        status.percent = static_cast<uint8_t>(percent);
        status.voltage = 3.3f + 0.9f * static_cast<float>(percent) / 100.0f;
        return true;
    }

    Diagnostics readDiagnostics() {
        Diagnostics diagnostics;
        diagnostics.available = true;
        diagnostics.externalPowerPresent = sim::board::power().usbPower;
        return diagnostics;
    }

    bool externalPowerPresent() { return sim::board::power().usbPower; }
    bool powerOff() { sim::requestPowerOff(); }
    bool powerButtonHeld() { return sim::board::buttons().power; }

} // namespace Board::Power

// ------------------------------------------------------------------ Storage (host directory = microSD)
namespace Board::Storage {

    fs::FS& filesystem() { return gSd; }

    bool mount(const char*, int) {
        std::error_code error;
        const auto& root = sim::sdDirectory();
        if (!std::filesystem::is_directory(root, error)) {
            ESP_LOGW("storage", "virtual microSD folder missing: %s (card absent)", root.c_str());
            return false;
        }
        gSd.setRoot(root);
        gSdMounted = true;
        return true;
    }

    void end() { gSdMounted = false; }

    uint64_t cardSize() { return gSdMounted ? 32ull * 1024 * 1024 * 1024 : 0; }
    CardType cardType() { return gSdMounted ? CardType::Sdhc : CardType::None; }
    bool supportsFrequencySelection() { return false; }
    bool setSdMmcPins() { return true; }

} // namespace Board::Storage

// ------------------------------------------------------------------ System
namespace Board::System {

    void begin() {}

    EspLightSleep::WakeReason lightSleep(uint32_t timeoutMs) {
        // Light sleep: wait for a touch/button (or the timeout) like the board's wake GPIOs.
        const uint32_t start = millis();
        while (millis() - start < timeoutMs) {
            if (sim::board::touch().down || sim::board::buttons().boot || sim::board::buttons().power)
                return EspLightSleep::WakeReason::input;
            delay(10);
        }
        return EspLightSleep::WakeReason::timer;
    }

    const char* wakeLabel(bool) { return "Hold PWR to start"; }
    void logStartupDiagnostics() { ESP_LOGI("diag", "simulated board: %s", WaveshareLcd349::Revision::kBoardLabel); }

} // namespace Board::System

// ------------------------------------------------------------------ Audio (absent in the simulator)
namespace Board::Audio {
    bool begin() { return false; }
    bool beep() { return false; }
    bool available() { return false; }
} // namespace Board::Audio

// Virtual QMI8658 at register level (what focus::OrientationReader talks to).
namespace {
    constexpr uint8_t kQmiAddress = 0x6B;
    constexpr uint8_t kWhoAmIReg = 0x00, kCtrl7Reg = 0x08, kAccelStartReg = 0x35, kResetResultReg = 0x4D, kResetReg = 0x60;
    std::mutex gImuMutex;
    std::array<uint8_t, 128> gImuRegisters = [] {
        std::array<uint8_t, 128> r{};
        r[kWhoAmIReg] = 0x05;
        r[0x01] = 0x7C; // revision id
        return r;
    }();

    void accelerometer(float& x, float& y, float& z) {
        using sim::board::Pose;
        auto& motion = sim::board::motion();
        x = y = z = 0;
        switch (motion.pose.load()) {
        case Pose::FlatFaceUp: z = 1; break;
        case Pose::FlatFaceDown: z = -1; break;
        case Pose::ShortAUp: x = 1; break;
        case Pose::ShortBUp: x = -1; break;
        }
        const uint32_t now = millis();
        if (now < motion.shakeUntilMs) { // picked up / shaken: ~6 Hz, up to 1.6 g on every axis
            const float t = static_cast<float>(now) / 1000.0f;
            x += 1.6f * std::sin(t * 37.0f);
            y += 1.1f * std::sin(t * 41.0f + 1.0f);
            z += 0.8f * std::sin(t * 29.0f + 2.0f);
        }
    }

    void writeAccel() {
        float g[3];
        accelerometer(g[0], g[1], g[2]);
        const bool enabled = gImuRegisters[kCtrl7Reg] & 0x01;
        for (int axis = 0; axis < 3; ++axis) {
            const float counts = enabled ? std::clamp(g[axis] * 8192.0f, -32768.0f, 32767.0f) : 0.0f; // ±4 g
            const auto raw = static_cast<int16_t>(counts);
            gImuRegisters[kAccelStartReg + axis * 2] = static_cast<uint8_t>(raw & 0xFF);
            gImuRegisters[kAccelStartReg + axis * 2 + 1] = static_cast<uint8_t>((raw >> 8) & 0xFF);
        }
    }
} // namespace

namespace Board::Imu {
    bool available() { return true; }
    const char* wireName() { return "sim"; }
    uint8_t address() { return kQmiAddress; }
    bool probeAddress(uint8_t address) { return address == kQmiAddress; }
    bool readRegister(uint8_t address, uint8_t reg, uint8_t& value) {
        return readRegisters(address, reg, &value, 1);
    }
    bool writeRegister(uint8_t address, uint8_t reg, uint8_t value) {
        if (address != kQmiAddress || reg >= gImuRegisters.size())
            return false;
        std::lock_guard lock(gImuMutex);
        if (reg == kResetReg && value == 0xB0) { // soft reset: defaults + "reset done"
            gImuRegisters.fill(0);
            gImuRegisters[kWhoAmIReg] = 0x05;
            gImuRegisters[0x01] = 0x7C;
            gImuRegisters[kResetResultReg] = 0x80;
            return true;
        }
        gImuRegisters[reg] = value;
        return true;
    }
    bool readRegisters(uint8_t address, uint8_t startReg, uint8_t* buffer, size_t len) {
        if (address != kQmiAddress || !buffer || startReg + len > gImuRegisters.size())
            return false;
        std::lock_guard lock(gImuMutex);
        writeAccel();
        std::memcpy(buffer, gImuRegisters.data() + startReg, len);
        return true;
    }
} // namespace Board::Imu
