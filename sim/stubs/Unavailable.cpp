// Lume Nano simulator: subsystems that need the radio, USB or ESP-IDF internals.
// They keep the firmware's public API but report "not available in the simulator", so the UI
// flows still run (and show their error states) on the Mac.
#include <Arduino.h>
#include <esp_log.h>

#include <chrono>
#include <thread>

#include "FirmwareVersion.generated.h"
#include "companion/http/CompanionApi.h"
#include "companion/serial/CompanionSerial.h"
#include "dictionary/DictionaryInstaller.h"
#include "feeds/RssFeeds.h"
#include "logging/Logger.h"
#include "network/VerifiedDownload.h"
#include "network/WifiConnection.h"
#include "settings/NvsSecurity.h"
#include "sleep/EspLightSleep.h"
#include "ui/screens/Screens.h"
#include "update/OtaUpdater.h"
#include "update/SwitchSettings.h"
#include "usb/UsbMassStorageManager.h"
#include "board/BoardStorage.h"

namespace {
    constexpr char kUnavailable[] = "Indisponível no simulador";
}

// ------------------------------------------------------------------ Companion (Wi-Fi HTTP + USB serial)
bool CompanionApi::begin() {
    statusLine1_ = kUnavailable;
    statusLine2_ = "Sem Wi-Fi no Mac";
    ESP_LOGW("companion", "companion sync is not simulated");
    return false;
}
void CompanionApi::end() {
    active_ = false;
    statusLine1_ = "Idle";
    statusLine2_.clear();
}
bool CompanionApi::active() const { return active_; }
void CompanionApi::renderStatus(bool) {
    screens::status(ui_, ui_.text(UiText::Sync), statusLine1(), statusLine2());
}
std::string_view CompanionApi::statusLine1() const { return statusLine1_; }
std::string_view CompanionApi::statusLine2() const { return statusLine2_; }

void CompanionSerial::update(uint32_t) {}
void CompanionSerial::close() {}
bool CompanionSerial::active() const noexcept { return false; }

// ------------------------------------------------------------------ USB pen drive (MSC)
UsbMassStorageManager* UsbMassStorageManager::instance_ = nullptr;
UsbMassStorageManager::UsbMassStorageManager() { instance_ = this; }
bool UsbMassStorageManager::begin(bool) {
    statusMessage_ = kUnavailable;
    return false;
}
void UsbMassStorageManager::end() { active_ = false; }
bool UsbMassStorageManager::active() const { return false; }
bool UsbMassStorageManager::ejected() const { return false; }
uint64_t UsbMassStorageManager::cardSizeBytes() const { return 0; }
const char* UsbMassStorageManager::statusMessage() const { return statusMessage_; }

// ------------------------------------------------------------------ OTA
std::string_view OtaUpdater::currentVersion() { return kFirmwareVersion; }

OtaUpdater::Result OtaUpdater::checkOnly(const settings::DeviceSettings&, const settings::DeviceSecrets&,
                                         StatusCallback callback, void* context, Target, Offer* offer) {
    if (offer)
        *offer = {};
    if (callback)
        callback(context, "Updates", "Checking", "", -1);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    return {.summary = kUnavailable, .detail = "OTA só funciona na placa", .rebootRequired = false};
}

OtaUpdater::Result OtaUpdater::checkAndInstall(const settings::DeviceSettings& settings,
                                               const settings::DeviceSecrets& secrets, StatusCallback callback,
                                               void* context, const Offer*) {
    return checkOnly(settings, secrets, callback, context);
}

std::expected<void, std::string> OtaUpdater::finalizeInstall(settings::SettingsStore&, const Offer&) {
    return std::unexpected(std::string(kUnavailable));
}

// ------------------------------------------------------------------ RSS
RssFeeds::Result RssFeeds::check(Preferences&, const settings::DeviceSettings&, const settings::DeviceSecrets&,
                                 StatusCallback callback, void* context) {
    if (callback)
        callback(context, "RSS", "Checking", "", -1);
    std::this_thread::sleep_for(std::chrono::milliseconds(400));
    return {.summary = kUnavailable, .detail = "Sem Wi-Fi no Mac"};
}

// ------------------------------------------------------------------ Dictionary download
namespace dictionary {
    // Packs copied onto the virtual SD by hand still work offline (lookup is local).
    bool installedPack() { return Board::Storage::filesystem().exists("/dictionaries/english.oewn"); }
    bool recoverInstall() { return true; } // nothing is ever half-downloaded in the simulator
    std::expected<void, std::string> installEnglish(const settings::DeviceSettings&, const settings::DeviceSecrets&,
                                                    bool, const InstallStatus& status,
                                                    const std::function<bool()>&) {
        if (status)
            status(kUnavailable, "Sem Wi-Fi no Mac", -1);
        return std::unexpected(std::string(kUnavailable));
    }
} // namespace dictionary

// ------------------------------------------------------------------ Network
namespace net {
    std::expected<size_t, std::string> download(std::string_view, size_t, size_t, const DownloadSink&,
                                                const DownloadControl&) {
        return std::unexpected(std::string(kUnavailable));
    }
    std::expected<std::string, std::string> downloadText(std::string_view, size_t) {
        return std::unexpected(std::string(kUnavailable));
    }
    std::expected<void, std::string> syncDownloadClock() { return std::unexpected(std::string(kUnavailable)); }
    bool httpsUrl(std::string_view url) { return url.starts_with("https://"); }

    std::expected<void, std::error_code> connectStation(const char*, const char*, const WifiProgress& progress,
                                                        uint32_t) {
        if (progress)
            progress(100);
        return std::unexpected(std::make_error_code(std::errc::network_unreachable));
    }
    std::vector<settings::WifiProfile> savedConnectionOrder(const settings::NetworkSettings&,
                                                           const settings::DeviceSecrets&) {
        return {};
    }
    std::expected<void, std::error_code> connectSaved(const settings::NetworkSettings&, const settings::DeviceSecrets&,
                                                      const WifiProgress& progress) {
        return connectStation(nullptr, nullptr, progress);
    }
    void disconnect() {}
} // namespace net

// ------------------------------------------------------------------ NVS encryption (host file NVS is plain)
namespace settings {
    NvsEncryptionState nvsEncryptionState() { return NvsEncryptionState::Unavailable; }
    bool initializeNvsEncryption() { return true; }
    bool enableNvsEncryption(Preferences&, SettingsStore&) { return false; }
} // namespace settings

// ------------------------------------------------------------------ Light sleep (Board::System handles waking)
EspLightSleep::WakeReason EspLightSleep::wait(std::span<const gpio_num_t>, uint32_t timeoutMs) {
    delay(std::min<uint32_t>(timeoutMs, 50));
    return WakeReason::timer;
}

// ------------------------------------------------------------------ Logger (stderr only)
void Logger::begin() {}
void Logger::logResetReason() { ESP_LOGI("main", "reset reason: simulator start"); }
void Logger::checkpoint(const char* phase) { ESP_LOGI("main", "checkpoint %s", phase ? phase : ""); }
void Logger::startupCheckpoint(const char* phase) { ESP_LOGD("main", "startup %s", phase ? phase : ""); }

// ------------------------------------------------------------------ Online dictionary lookup
#include "dictionary/Dictionary.h"
void dictionary::Dictionary::lookupOnline(settings::SettingsStore&) { message = kUnavailable; }
