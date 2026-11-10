#pragma once
// Lume Nano simulator: state shared between the virtual board (App thread) and the SDL window (main thread).
#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

namespace sim::board {

    // Physical panel (portrait, like the AXS15231B): 172 x 640 RGB565.
    constexpr int kPanelWidth = 172;
    constexpr int kPanelHeight = 640;

    struct Frame {
        std::mutex mutex;
        std::vector<uint16_t> pixels = std::vector<uint16_t>(kPanelWidth * kPanelHeight, 0);
        uint32_t serial = 0;             // incremented on every flush
        std::atomic<uint8_t> uiRotation{1}; // ui::Orientation the firmware draws with (gfx rotation)
    };
    Frame& frame();

    // Extra quarter turns applied by the user (key R) on top of the UI orientation.
    std::atomic<uint8_t>& manualTurns();

    // Presentation = orientation the window shows: by default the UI's own orientation, so the
    // UI is always upright; same numbering as ui::Orientation / Arduino_GFX rotation.
    inline uint8_t presentation() { return static_cast<uint8_t>((frame().uiRotation + manualTurns()) & 3U); }
    constexpr int viewWidth(uint8_t p) { return (p & 1U) ? kPanelHeight : kPanelWidth; }
    constexpr int viewHeight(uint8_t p) { return (p & 1U) ? kPanelWidth : kPanelHeight; }

    // Physical panel pixel / raw touch coordinate shown at view (vx, vy) for presentation p.
    // Matches Arduino_Canvas rotation and ui::Context::mapTouch, so mouse and pixels agree.
    constexpr void viewToPanel(uint8_t p, int vx, int vy, int& px, int& py) {
        switch (p & 3U) {
        case 0: px = vx; py = vy; break;
        case 1: px = kPanelWidth - 1 - vy; py = vx; break;
        case 2: px = kPanelWidth - 1 - vx; py = kPanelHeight - 1 - vy; break;
        default: px = vy; py = kPanelHeight - 1 - vx; break;
        }
    }

    struct Panel {
        std::atomic<bool> backlight{true};
        std::atomic<bool> sleeping{false};
        std::atomic<uint8_t> brightnessPercent{100};
    };
    Panel& panel();

    // Touch in VIEW coordinates of the presentation active when it was pressed.
    struct TouchState {
        std::atomic<bool> down{false};
        std::atomic<int> x{0};
        std::atomic<int> y{0};
        std::atomic<uint8_t> presentation{1};
    };
    TouchState& touch();

    struct Buttons {
        std::atomic<bool> boot{false};
        std::atomic<bool> power{false};
    };
    Buttons& buttons();

    struct PowerState {
        std::atomic<int> batteryPercent{80};
        std::atomic<bool> usbPower{true};
    };
    PowerState& power();

    // Virtual QMI8658: gravity in g along the sensor axes (+ a decaying "shake" pulse).
    enum class Pose : uint8_t { FlatFaceUp, FlatFaceDown, ShortAUp, ShortBUp };
    struct Motion {
        std::atomic<Pose> pose{Pose::FlatFaceUp};
        std::atomic<uint32_t> shakeUntilMs{0};
    };
    Motion& motion();
    const char* poseLabel(Pose pose);

} // namespace sim::board
