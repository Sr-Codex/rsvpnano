#pragma once
// Lume Nano simulator: process-wide state shared by the shims, the virtual board and main.
#include <cstdint>
#include <string>

namespace sim {

    // Directory holding the virtual microSD (sd/) and NVS (nvs/). Set by main before setup().
    const std::string& stateDirectory();
    const std::string& sdDirectory();
    void setDirectories(std::string state, std::string sd);

    // esp_restart(): re-exec the simulator (same arguments); files and NVS persist like on the board.
    [[noreturn]] void requestRestart();
    // powerOff(): close the window and exit.
    [[noreturn]] void requestPowerOff();

} // namespace sim
