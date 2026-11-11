#include <Arduino.h>
#include <esp_log.h>
#include "app/App.h"
#include "board/Board.h"
#include "board/BoardDeepSleep.h"
#include "logging/Logger.h"
#include "settings/NvsSecurity.h"
#include "update/OtaRollback.h"

App app;

// ADR-010: o core Arduino marcaria a imagem OTA como valida ANTES do setup(). Adiamos: quem confirma e o app, quando
// esta saudavel (src/update/OtaRollback.*). Sem isto, um firmware que sobe e trava nao voltaria sozinho.
extern "C" bool verifyRollbackLater() {
    return true;
}

void setup() {
    Serial.begin(115200);
    Logger::begin();
    delay(50);
    ota::iniciar();   // le o estado da imagem (pendente de verificacao?) e registra no log
    Board::System::begin();
    // Despertar por timer (ADR-009): o aviso nao pode esperar pela serial (a antecedencia e de poucos segundos).
    const bool bootDeTimer = Board::DeepSleep::bootCause() == Board::DeepSleep::BootCause::Timer;
    const uint32_t serialWaitStart = millis();
    while (!bootDeTimer && !Serial && millis() - serialWaitStart < 2000) {
        delay(10);
    }
    Board::System::logStartupDiagnostics();
    if (!settings::initializeNvsEncryption()) {
        ESP_LOGE("main", "encrypted NVS initialization failed; restarting");
        delay(1000);
        ESP.restart();
        return;
    }
    ESP_LOGI("main", "app setup task=%s core=%d", pcTaskGetName(nullptr), xPortGetCoreID());
    app.begin();
    Logger::checkpoint("running");
    ota::setupConcluido();
}

void loop() {
    const uint32_t now = millis();
    app.update(now);
}
