// Lume Nano simulator: virtual Waveshare ESP32-S3-Touch-LCD-3.49 Rev2.
// Implements the Board::* interfaces the firmware uses; the SDL window (sim/main.cpp) shows the
// framebuffer and feeds touch/buttons.
#include "board/Board.h"
#include "board/BoardImu.h"
#include "board/BoardDeepSleep.h"
#include "som/Tocador.h"
#include "tempo/Relogio.h"

#include <canvas/Arduino_Canvas.h>
#include <esp_log.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
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
            // LUME_SIM_RENDER_DELAY_MS=N: cada quadro desenhado leva N ms a mais, como a placa (a batalha do Tempo passa de
            // 30 ms por quadro). Serve para reproduzir no Mac atrasos que dependem do render lento (fila de toques acumulando).
            static const unsigned long renderDelayMs = [] {
                const char* v = std::getenv("LUME_SIM_RENDER_DELAY_MS");
                const long ms = v ? std::strtol(v, nullptr, 10) : 0;
                if (ms > 0) std::fprintf(stderr, "[sim] render lento: +%ld ms por quadro\n", ms);
                return ms > 0 ? static_cast<unsigned long>(ms) : 0UL;
            }();
            if (renderDelayMs != 0) sim::sleepMs(renderDelayMs);
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

// ------------------------------------------------------------------ Audio
// Sem alto-falante: roda a MESMA tarefa/fila de audio do firmware (som::Tocador) com uma saida que so registra
// "[sim] som: <Nome>" no log e consome o PCM em tempo real (16 kHz estereo), para exercitar fila e prioridades.
namespace {
    som::Tocador gSimTocador;
    bool simPreparar(void*) { return true; }
    bool simEscrever(void*, const int16_t*, size_t amostras) {
        delay(static_cast<uint32_t>(amostras / 2 * 1000 / som::kTaxaHz));
        return true;
    }
    void simComecou(void*, som::SomId id) { std::fprintf(stderr, "[sim] som: %s\n", som::nome(id)); }
} // namespace
namespace Board::Audio {
    bool begin() { return true; }
    bool available() { return true; }
    bool play(const som::Pedido& pedido) {
        som::Saida saida;
        saida.preparar = &simPreparar;
        saida.escrever = &simEscrever;
        saida.aoComecar = &simComecou;
        return gSimTocador.iniciar(saida) && gSimTocador.tocar(pedido);
    }
    bool beep() { return play({som::SomId::Bip, 1.0f, som::Classe::Interface}); }
    void waitOrStop(uint32_t timeoutMs) { gSimTocador.esperarOuParar(timeoutMs); }
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

// ------------------------------------------------------------------ Deep sleep com despertador (ADR-009) - stub
// Sem deep sleep real: registra no log e fica "dormindo" (tela apagada) ate o PWR (boot normal) ou o despertador.
//   LUME_SIM_DEEPSLEEP_WAKE=1  -> acorda na hora como se o timer tivesse disparado (boot de lembrete); o relogio do
//                                 app (LUME_SIM_CLOCK) e ajustado para o instante do despertar.
// A memoria RTC (Board::DeepSleep::Rtc) atravessa o reinicio do simulador na variavel LUME_SIM_RTC.
namespace Board::DeepSleep {

    namespace {
        Rtc gRtc;
        bool gRtcLoaded = false;

        void loadRtc() {
            if (gRtcLoaded) return;
            gRtcLoaded = true;
            if (const char* text = std::getenv("LUME_SIM_RTC")) {
                unsigned magic = 0;
                int ciclos = 0, ins = 0, stamp = 0, origem = 0;
                long long ultimo = 0, alvo = 0;
                if (std::sscanf(text, "%u,%d,%d,%d,%lld,%lld,%d", &magic, &ciclos, &ins, &stamp, &ultimo, &alvo, &origem) == 7) {
                    gRtc.magic = magic;
                    gRtc.ciclosSemInteracao = ciclos;
                    gRtc.insistenciasMedicacao = ins;
                    gRtc.stampDia = stamp;
                    gRtc.ultimoAvisoMedicacaoEpoch = ultimo;
                    gRtc.alvoEpoch = alvo;
                    gRtc.origemProxima = static_cast<uint8_t>(origem);
                }
            }
        }
    } // namespace

    bool supported() { return true; }

    BootCause bootCause() {
        static const BootCause cause = [] {
            const char* text = std::getenv("LUME_SIM_BOOT_CAUSE");
            if (text == nullptr) return BootCause::PowerOn;
            if (std::string(text) == "timer") return BootCause::Timer;
            if (std::string(text) == "button") return BootCause::Button;
            return BootCause::PowerOn;
        }();
        return cause;
    }

    Rtc& rtc() {
        loadRtc();
        return gRtc;
    }

    bool enter(uint32_t seconds) {
        loadRtc();
        const auto now = tempo::agora(millis());
        char clock[40] = "";
        if (now.valida) {
            int64_t serial = tempo::diaSerial(now.data) * 86400LL + now.segundoDoDia + seconds;
            const tempo::Data data = tempo::dataDeSerial(serial / 86400);
            const int sod = static_cast<int>(serial % 86400);
            std::snprintf(clock, sizeof clock, "%04d-%02d-%02dT%02d:%02d:%02d", data.ano, data.mes, data.dia, sod / 3600,
                          sod / 60 % 60, sod % 60);
            std::fprintf(stderr, "[sim] deep sleep ate %02d:%02d (alvo do despertador: %s, em %u s)\n", sod / 3600,
                         sod / 60 % 60, clock, static_cast<unsigned>(seconds));
        } else {
            std::fprintf(stderr, "[sim] deep sleep por %u s (relogio invalido)\n", static_cast<unsigned>(seconds));
        }
        std::fflush(nullptr);
        char rtcText[160];
        std::snprintf(rtcText, sizeof rtcText, "%u,%d,%d,%d,%lld,%lld,%d", static_cast<unsigned>(gRtc.magic),
                      static_cast<int>(gRtc.ciclosSemInteracao), static_cast<int>(gRtc.insistenciasMedicacao),
                      static_cast<int>(gRtc.stampDia), static_cast<long long>(gRtc.ultimoAvisoMedicacaoEpoch),
                      static_cast<long long>(gRtc.alvoEpoch), static_cast<int>(gRtc.origemProxima));
        setenv("LUME_SIM_RTC", rtcText, 1);
        sim::board::panel().sleeping = true;
        sim::board::panel().backlight = false;
        const bool autoWake = std::getenv("LUME_SIM_DEEPSLEEP_WAKE") != nullptr;
        while (true) {
            if (autoWake) {
                setenv("LUME_SIM_BOOT_CAUSE", "timer", 1);
                if (clock[0] != 0) setenv("LUME_SIM_CLOCK", clock, 1);
                unsetenv("LUME_SIM_DEEPSLEEP_WAKE");
                std::fprintf(stderr, "[sim] despertador disparou (timer)\n");
                sim::requestRestart();
            }
            if (sim::board::buttons().power) {
                setenv("LUME_SIM_BOOT_CAUSE", "button", 1);
                unsetenv("LUME_SIM_CLOCK");
                std::fprintf(stderr, "[sim] PWR: acordou do deep sleep (boot normal)\n");
                sim::requestRestart();
            }
            delay(20);
        }
    }

} // namespace Board::DeepSleep
