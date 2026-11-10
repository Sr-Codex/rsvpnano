// Lume Nano simulator entry point.
//   - The real firmware App (setup/loop like src/main.cpp) runs on a "loopTask" thread.
//   - The main thread owns the SDL window: shows the panel, turns the mouse into touch and the
//     keyboard into the board's BOOT/PWR buttons.
//   - --script/--screenshot allow unattended runs (used to verify the simulator itself).
#include <Arduino.h>
#include <SDL.h>
#include <unistd.h>
#include <zlib.h>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <mutex>
#include <sstream>
#include <string>
#include <thread>
#include <vector>

#include "app/App.h"
#include "board/Board.h"
#include "focus/FocusOrientation.h"
#include "tempo/Relogio.h"
#include "SimBoard.h"
#include "SimRuntime.h"

namespace {

    struct Options {
        int scale = 1;  // tamanho real da tela (640x172 pontos); --scale 2 para ampliar
        bool headless = false;
        std::string sd;
        std::string state;
        std::string script;
        long quitAfterMs = -1;
    };

    Options gOptions;
    char** gArgv = nullptr;
    std::atomic<bool> gQuit{false};
    std::string gStateDir;
    std::string gSdDir;

    // ------------------------------------------------------------ PNG of the user-visible screen
    void putU32(std::vector<uint8_t>& out, uint32_t v) {
        for (int i = 3; i >= 0; --i)
            out.push_back(static_cast<uint8_t>(v >> (8 * i)));
    }

    void chunk(std::vector<uint8_t>& png, const char* type, const std::vector<uint8_t>& data) {
        putU32(png, static_cast<uint32_t>(data.size()));
        std::vector<uint8_t> body(type, type + 4);
        body.insert(body.end(), data.begin(), data.end());
        png.insert(png.end(), body.begin(), body.end());
        putU32(png, static_cast<uint32_t>(crc32(0, body.data(), static_cast<uInt>(body.size()))));
    }

    // RGB888 of the visible screen for presentation p, including backlight/brightness.
    std::vector<uint8_t> viewRgb(uint8_t p) {
        using namespace sim::board;
        const int kViewWidth = viewWidth(p), kViewHeight = viewHeight(p);
        std::vector<uint16_t> pixels;
        {
            std::lock_guard lock(frame().mutex);
            pixels = frame().pixels;
        }
        const bool lit = panel().backlight && !panel().sleeping;
        const float level = lit ? 0.35f + 0.65f * static_cast<float>(panel().brightnessPercent) / 100.0f : 0.0f;
        std::vector<uint8_t> rgb(kViewWidth * kViewHeight * 3);
        for (int vy = 0; vy < kViewHeight; ++vy) {
            for (int vx = 0; vx < kViewWidth; ++vx) {
                int px = 0, py = 0;
                viewToPanel(p, vx, vy, px, py);
                const uint16_t c = pixels[py * kPanelWidth + px];
                const uint8_t r = static_cast<uint8_t>(((c >> 11) & 0x1F) * 255 / 31);
                const uint8_t g = static_cast<uint8_t>(((c >> 5) & 0x3F) * 255 / 63);
                const uint8_t b = static_cast<uint8_t>((c & 0x1F) * 255 / 31);
                uint8_t* out = &rgb[(vy * kViewWidth + vx) * 3];
                out[0] = static_cast<uint8_t>(r * level);
                out[1] = static_cast<uint8_t>(g * level);
                out[2] = static_cast<uint8_t>(b * level);
            }
        }
        return rgb;
    }

    bool writeScreenshot(const std::string& path) {
        using namespace sim::board;
        const uint8_t p = presentation();
        const int kViewWidth = viewWidth(p), kViewHeight = viewHeight(p);
        const auto rgb = viewRgb(p);
        std::vector<uint8_t> raw;
        raw.reserve((kViewWidth * 3 + 1) * kViewHeight);
        for (int y = 0; y < kViewHeight; ++y) {
            raw.push_back(0);
            raw.insert(raw.end(), rgb.begin() + y * kViewWidth * 3, rgb.begin() + (y + 1) * kViewWidth * 3);
        }
        uLongf compressedSize = compressBound(static_cast<uLong>(raw.size()));
        std::vector<uint8_t> compressed(compressedSize);
        if (compress2(compressed.data(), &compressedSize, raw.data(), static_cast<uLong>(raw.size()), 9) != Z_OK)
            return false;
        compressed.resize(compressedSize);
        std::vector<uint8_t> png = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1A, '\n'};
        std::vector<uint8_t> header;
        putU32(header, kViewWidth);
        putU32(header, kViewHeight);
        header.insert(header.end(), {8, 2, 0, 0, 0});
        chunk(png, "IHDR", header);
        chunk(png, "IDAT", compressed);
        chunk(png, "IEND", {});
        std::error_code error;
        std::filesystem::create_directories(std::filesystem::path(path).parent_path(), error);
        std::ofstream out(path, std::ios::binary);
        out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
        std::fprintf(stderr, "[sim] screenshot %s\n", path.c_str());
        return static_cast<bool>(out);
    }

    // ------------------------------------------------------------ scripted input
    // "tap 320,86 @1500; hold 320,86 900 @3000; drag 600,86 40,86 400 @5000; press boot @6000;
    //  long boot 1500 @7000; shot out.png @8000; quit @9000"   (@ = ms since start)
    struct Step {
        unsigned long at = 0;
        std::string verb;
        std::vector<std::string> args;
    };

    std::vector<Step> parseScript(const std::string& text) {
        std::vector<Step> steps;
        std::stringstream all(text);
        std::string item;
        while (std::getline(all, item, ';')) {
            std::stringstream words(item);
            Step step;
            std::string word;
            while (words >> word) {
                if (word[0] == '@')
                    step.at = std::stoul(word.substr(1));
                else if (step.verb.empty())
                    step.verb = word;
                else
                    step.args.push_back(word);
            }
            if (!step.verb.empty())
                steps.push_back(step);
        }
        return steps;
    }

    void parsePoint(const std::string& text, int& x, int& y) {
        const auto comma = text.find(',');
        x = std::stoi(text.substr(0, comma));
        y = std::stoi(text.substr(comma + 1));
    }

    void touchAt(bool down, int x, int y) {
        auto& t = sim::board::touch();
        // A press keeps the presentation it started in (the UI may rotate mid-gesture).
        const uint8_t p = down && t.down ? t.presentation.load() : sim::board::presentation();
        t.presentation = p;
        t.x = std::clamp(x, 0, sim::board::viewWidth(p) - 1);
        t.y = std::clamp(y, 0, sim::board::viewHeight(p) - 1);
        t.down = down;
    }

    void setPose(sim::board::Pose pose) {
        sim::board::motion().pose = pose;
        std::fprintf(stderr, "[sim] IMU: %s\n", sim::board::poseLabel(pose));
    }

    void shake() {
        sim::board::motion().shakeUntilMs = static_cast<uint32_t>(sim::millis() + 700);
        std::fprintf(stderr, "[sim] IMU: chacoalhar / pegou na mão (0,7 s)\n");
    }

    void turnBoard() {
        auto& turns = sim::board::manualTurns();
        turns = static_cast<uint8_t>((turns + 1) & 3U);
        std::fprintf(stderr, "[sim] placa girada +90° (rotação manual %d°)\n", turns * 90);
    }

    bool parsePose(const std::string& name, sim::board::Pose& pose) {
        using sim::board::Pose;
        if (name == "up" || name == "flat") pose = Pose::FlatFaceUp;
        else if (name == "down") pose = Pose::FlatFaceDown;
        else if (name == "a") pose = Pose::ShortAUp;
        else if (name == "b") pose = Pose::ShortBUp;
        else return false;
        return true;
    }

    std::atomic<bool>& button(const std::string& name) {
        return name == "power" || name == "pwr" ? sim::board::buttons().power : sim::board::buttons().boot;
    }

    void runScript(std::vector<Step> steps) {
        for (const auto& step : steps) {
            while (sim::millis() < step.at && !gQuit)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
            if (gQuit)
                return;
            std::fprintf(stderr, "[sim] script @%lu %s\n", sim::millis(), step.verb.c_str());
            int x = 0, y = 0;
            if (step.verb == "tap" && step.args.size() >= 1) {
                parsePoint(step.args[0], x, y);
                touchAt(true, x, y);
                std::this_thread::sleep_for(std::chrono::milliseconds(120));
                touchAt(false, x, y);
            } else if ((step.verb == "down" || step.verb == "move") && step.args.size() >= 1) {
                parsePoint(step.args[0], x, y);   // dedo continua apoiado ate o "up" (captura no meio de um arrasto)
                touchAt(true, x, y);
            } else if (step.verb == "up") {
                if (!step.args.empty()) parsePoint(step.args[0], x, y);
                touchAt(false, x, y);
            } else if (step.verb == "hold" && step.args.size() >= 2) {
                parsePoint(step.args[0], x, y);
                touchAt(true, x, y);
                std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(step.args[1])));
                touchAt(false, x, y);
            } else if (step.verb == "drag" && step.args.size() >= 3) {
                int x2 = 0, y2 = 0;
                parsePoint(step.args[0], x, y);
                parsePoint(step.args[1], x2, y2);
                const int duration = std::max(1, std::stoi(step.args[2]));
                const int steps = std::max(2, duration / 15);
                for (int i = 0; i <= steps; ++i) {
                    touchAt(true, x + (x2 - x) * i / steps, y + (y2 - y) * i / steps);
                    std::this_thread::sleep_for(std::chrono::milliseconds(duration / steps));
                }
                touchAt(false, x2, y2);
            } else if (step.verb == "press" && !step.args.empty()) {
                button(step.args[0]) = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(150));
                button(step.args[0]) = false;
            } else if (step.verb == "long" && step.args.size() >= 2) {
                button(step.args[0]) = true;
                std::this_thread::sleep_for(std::chrono::milliseconds(std::stoi(step.args[1])));
                button(step.args[0]) = false;
            } else if (step.verb == "pose" && !step.args.empty()) {
                sim::board::Pose pose;
                if (parsePose(step.args[0], pose)) setPose(pose);
            } else if (step.verb == "shake") {
                shake();
            } else if (step.verb == "imucheck") {
                // Self-test: the firmware's own OrientationReader against every virtual pose.
                focus::OrientationReader reader;
                std::fprintf(stderr, "[sim] imucheck begin=%d\n", reader.begin() ? 1 : 0);
                const char* names[] = {"Unknown", "ShortA", "ShortB", "Flat"};
                for (const auto pose : {sim::board::Pose::FlatFaceUp, sim::board::Pose::FlatFaceDown,
                                        sim::board::Pose::ShortAUp, sim::board::Pose::ShortBUp}) {
                    setPose(pose);
                    focus::Orientation seen = focus::Orientation::Unknown;
                    for (int i = 0; i < 25; ++i) {
                        seen = reader.update(static_cast<uint32_t>(sim::millis()));
                        std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    }
                    const auto index = static_cast<size_t>(seen);
                    std::fprintf(stderr, "[sim] imucheck %s -> %s\n", sim::board::poseLabel(pose),
                                 index < 4 ? names[index] : "?");
                }
                shake();
                focus::Orientation during = reader.update(static_cast<uint32_t>(sim::millis()));
                for (int i = 0; i < 12; ++i) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(50));
                    during = reader.update(static_cast<uint32_t>(sim::millis()));
                }
                const auto shaking = static_cast<size_t>(during);
                std::fprintf(stderr, "[sim] imucheck chacoalhando 0,6 s -> %s (pose estável anterior se mantém)\n",
                             shaking < 4 ? names[shaking] : "?");
            } else if (step.verb == "turn") {
                turnBoard();
            } else if (step.verb == "shot" && !step.args.empty()) {
                writeScreenshot(step.args[0]);
            } else if (step.verb == "quit") {
                gQuit = true;
            } else {
                std::fprintf(stderr, "[sim] unknown script step: %s\n", step.verb.c_str());
            }
        }
    }

    // ------------------------------------------------------------ firmware thread (like src/main.cpp)
    App gApp;

    void firmwareMain() {
        pthread_setname_np("loopTask");
        Serial.begin(115200);
        Board::System::begin();
        Board::System::logStartupDiagnostics();
        ESP_LOGI("main", "app setup task=%s core=%d", pcTaskGetName(nullptr), xPortGetCoreID());
        // Escala de tempo opcional do app Tempo (so no simulador): LUME_SIM_TIME_SCALE=60 => 1 min real = 1 h.
        if (const char* scale = std::getenv("LUME_SIM_TIME_SCALE")) {
            const long value = std::atol(scale);
            if (value > 1) {
                tempo::definirEscala(static_cast<uint32_t>(value));
                std::fprintf(stderr, "[sim] LUME_SIM_TIME_SCALE=%ld (app Tempo)\n", value);
            }
        }
        gApp.begin();
        // Relogio de parede opcional do app Tempo (so no simulador): LUME_SIM_CLOCK=2026-11-10T14:20 (hora local do Mac).
        // Serve para capturar o app Habitos em horarios especificos (a placa real usa o RTC).
        if (const char* clock = std::getenv("LUME_SIM_CLOCK")) {
            std::tm tm{};
            if (strptime(clock, "%Y-%m-%dT%H:%M", &tm) != nullptr) {
                tm.tm_isdst = -1;
                const std::time_t epoch = std::mktime(&tm);
                if (epoch > 0) {
                    tempo::ancorar(static_cast<int64_t>(epoch), millis());
                    std::fprintf(stderr, "[sim] LUME_SIM_CLOCK=%s (app Tempo)\n", clock);
                }
            }
        }
        while (!gQuit) {
            gApp.update(millis());
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
    }

    // ------------------------------------------------------------ window
    void usage() {
        std::puts(
            "lume-sim [--scale N] [--sd DIR] [--state DIR] [--headless] [--script \"...\"] [--quit-after MS]\n"
            "Teclas: B/Espaço = BOOT (segurar = pressão longa) · P/Esc = PWR · S = captura PNG ·\n"
            "        1 = deitada tela p/ cima · 2 = tela p/ baixo · 3 = em pé ponta A · 4 = em pé ponta B ·\n"
            "        H = chacoalhar/pegou na mão · R = girar a placa 90° · [ ] = bateria -/+ 10% ·\n"
            "        U = liga/desliga USB · Q = sair");
    }

    void parseOptions(int argc, char** argv) {
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            const auto next = [&]() -> std::string { return i + 1 < argc ? argv[++i] : ""; };
            if (arg == "--scale") gOptions.scale = std::max(1, std::atoi(next().c_str()));
            else if (arg == "--sd") gOptions.sd = next();
            else if (arg == "--state") gOptions.state = next();
            else if (arg == "--script") gOptions.script = next();
            else if (arg == "--quit-after") gOptions.quitAfterMs = std::atol(next().c_str());
            else if (arg == "--headless") gOptions.headless = true;
            else if (arg == "-h" || arg == "--help") { usage(); std::exit(0); }
            else { std::fprintf(stderr, "unknown option %s\n", arg.c_str()); usage(); std::exit(2); }
        }
    }

    void handleKey(SDL_Keycode key, bool down) {
        auto& buttons = sim::board::buttons();
        switch (key) {
        case SDLK_z:  // Z X C = BOOT PWR RESET (lado a lado no teclado)
        case SDLK_b:
        case SDLK_SPACE: buttons.boot = down; break;
        case SDLK_x:
        case SDLK_p:
        case SDLK_ESCAPE: buttons.power = down; break;
        default: break;
        }
        if (!down)
            return;
        auto& power = sim::board::power();
        using sim::board::Pose;
        if (key == SDLK_1) setPose(Pose::FlatFaceUp);
        else if (key == SDLK_2) setPose(Pose::FlatFaceDown);
        else if (key == SDLK_3) setPose(Pose::ShortAUp);
        else if (key == SDLK_4) setPose(Pose::ShortBUp);
        else if (key == SDLK_h) shake();
        else if (key == SDLK_r) turnBoard();
        else if (key == SDLK_s) {
            const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
            writeScreenshot(gStateDir + "/screenshots/lume-" + std::to_string(stamp) + ".png");
        } else if (key == SDLK_LEFTBRACKET) {
            power.batteryPercent = std::max(0, power.batteryPercent - 10);
        } else if (key == SDLK_RIGHTBRACKET) {
            power.batteryPercent = std::min(100, power.batteryPercent + 10);
        } else if (key == SDLK_u) {
            power.usbPower = !power.usbPower;
        } else if (key == SDLK_c) {
            sim::requestRestart();  // botão RESET (atrás da placa): reinicia, mantém SD e NVS
        } else if (key == SDLK_q) {
            gQuit = true;
        }
    }

    void runWindow() {
        using namespace sim::board;
        uint8_t shown = presentation();
        SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "nearest");  // pixels nítidos (sem suavizar) como na placa
        SDL_Window* window = SDL_CreateWindow("Lume Nano — simulador (3.49 Rev2)", SDL_WINDOWPOS_CENTERED,
                                              SDL_WINDOWPOS_CENTERED, viewWidth(shown) * gOptions.scale,
                                              viewHeight(shown) * gOptions.scale, SDL_WINDOW_ALLOW_HIGHDPI);
        SDL_Renderer* renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
        if (!renderer)
            renderer = SDL_CreateRenderer(window, -1, SDL_RENDERER_SOFTWARE);
        SDL_Texture* texture = nullptr;
        const auto layout = [&](uint8_t p) {
            shown = p;
            if (texture) SDL_DestroyTexture(texture);
            texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGB24, SDL_TEXTUREACCESS_STREAMING, viewWidth(p),
                                        viewHeight(p));
            SDL_SetTextureScaleMode(texture, SDL_ScaleModeNearest);
            SDL_SetWindowSize(window, viewWidth(p) * gOptions.scale, viewHeight(p) * gOptions.scale);
            SDL_RenderSetLogicalSize(renderer, viewWidth(p), viewHeight(p));
            SDL_RenderSetIntegerScale(renderer, SDL_TRUE);  // múltiplos inteiros: cada pixel da placa = N×N
        };
        layout(shown);
        bool mouseDown = false;
        while (!gQuit) {
            if (presentation() != shown && !mouseDown)
                layout(presentation()); // UI switched portrait/landscape (or R was pressed)
            SDL_Event event;
            while (SDL_PollEvent(&event)) {
                switch (event.type) {
                case SDL_QUIT: gQuit = true; break;
                case SDL_KEYDOWN:
                    if (!event.key.repeat) handleKey(event.key.keysym.sym, true);
                    break;
                case SDL_KEYUP: handleKey(event.key.keysym.sym, false); break;
                case SDL_MOUSEBUTTONDOWN:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouseDown = true;
                        touchAt(true, event.button.x, event.button.y);
                    }
                    break;
                case SDL_MOUSEBUTTONUP:
                    if (event.button.button == SDL_BUTTON_LEFT) {
                        mouseDown = false;
                        touchAt(false, event.button.x, event.button.y);
                    }
                    break;
                case SDL_MOUSEMOTION:
                    if (mouseDown) touchAt(true, event.motion.x, event.motion.y);
                    break;
                default: break;
                }
            }
            const auto rgb = viewRgb(shown);
            SDL_UpdateTexture(texture, nullptr, rgb.data(), viewWidth(shown) * 3);
            SDL_RenderClear(renderer);
            SDL_RenderCopy(renderer, texture, nullptr, nullptr);
            SDL_RenderPresent(renderer);
            SDL_Delay(16);
        }
        SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
    }

} // namespace

// ------------------------------------------------------------ SimRuntime.h
namespace sim {
    const std::string& stateDirectory() { return gStateDir; }
    const std::string& sdDirectory() { return gSdDir; }
    void setDirectories(std::string state, std::string sd) {
        gStateDir = std::move(state);
        gSdDir = std::move(sd);
    }
    void requestRestart() {
        std::fprintf(stderr, "[sim] esp_restart(): reiniciando o simulador\n");
        std::fflush(nullptr);
        execv(gArgv[0], gArgv);
        std::_Exit(3);
    }
    void requestPowerOff() {
        std::fprintf(stderr, "[sim] desligado pelo firmware (PWR longo)\n");
        std::fflush(nullptr);
        std::_Exit(0);
    }
} // namespace sim

int main(int argc, char** argv) {
    gArgv = argv;
    parseOptions(argc, argv);
    const char* envSd = std::getenv("LUME_SIM_SD");
    const char* envState = std::getenv("LUME_SIM_STATE");
    const std::string state = !gOptions.state.empty() ? gOptions.state : (envState ? envState : "sim/state");
    const std::string sd = !gOptions.sd.empty() ? gOptions.sd : (envSd ? envSd : state + "/sd");
    std::error_code error;
    std::filesystem::create_directories(state, error);
    sim::setDirectories(std::filesystem::absolute(state).string(), std::filesystem::absolute(sd).string());
    std::fprintf(stderr, "[sim] microSD = %s · NVS = %s/nvs\n", sim::sdDirectory().c_str(), sim::stateDirectory().c_str());

    std::thread firmware(firmwareMain);
    firmware.detach();
    if (!gOptions.script.empty())
        std::thread(runScript, parseScript(gOptions.script)).detach();

    if (gOptions.headless) {
        while (!gQuit && (gOptions.quitAfterMs < 0 || static_cast<long>(sim::millis()) < gOptions.quitAfterMs))
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
    } else {
        if (SDL_Init(SDL_INIT_VIDEO) != 0) {
            std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
            return 1;
        }
        if (gOptions.quitAfterMs >= 0)
            std::thread([] {
                while (static_cast<long>(sim::millis()) < gOptions.quitAfterMs)
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                gQuit = true;
            }).detach();
        runWindow();
        SDL_Quit();
    }
    std::fflush(nullptr);
    std::_Exit(0); // firmware threads never return (like the board)
}
