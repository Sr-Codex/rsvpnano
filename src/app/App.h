#pragma once

#include "library/CoverStoreSd.h"

#include <Arduino.h>
#include <Preferences.h>

#include <optional>
#include <string>
#include <atomic>
#include <string_view>

#include "board/BoardDisplay.h"
#include "som/Regras.h"
#include "tempo/Despertador.h"
#include "dictionary/Dictionary.h"
#include "board/BoardPower.h"
#include "companion/http/CompanionApi.h"
#include "companion/serial/CompanionSerial.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "input/Input.h"
#include "localization/LocaleCatalog.h"
#include "feeds/RssFeeds.h"
#include "settings/SettingsStore.h"
#include "library/StorageManager.h"
#include "library/BookSettingsEditor.h"
#include "ui/Ui.h"
#include "ui/screens/ChaptersScreen.h"
#include "ui/screens/LibraryScreen.h"
#include "ui/screens/ReaderScreen.h"
#include "ui/screens/OnboardingScreen.h"
#include "ui/screens/TouchCheckScreen.h"
#include "ui/screens/Screens.h"
#include "ui/screens/ModernScreens.h"
#include "ui/screens/StandbyScreen.h"
#include "ui/screens/TempoScreen.h"
#include "ui/screens/HomeScreen.h"
#include "ui/screens/PerfilScreen.h"
#include "ui/screens/HabitosScreen.h"
#include "focus/FocusOrientation.h"
#include "tempo/Lembretes.h"
#include "update/OtaUpdater.h"
#include "usb/UsbMassStorageManager.h"

class App {
public:
    void begin();
    void update(uint32_t nowMs);

private:
    enum class JobKind : uint8_t {
        None,
        Rss,
        StorageCheck,
        OtaCheck,
        OtaInstall,
        Book,
        Library,
        Typography,
        OnboardingStorage,
        OnlineDictionary,
        BookSettings,
        DictionaryInstall,
        Covers,
        TickTick,
    };

    struct JobUpdate {
        bool complete = false;
        char title[24] = {};
        char line1[96] = {};
        char line2[96] = {};
        int progressPercent = -1;
        bool rebootRequired = false;
        uint32_t storageRequest = 0;
        OnboardingStorage::Progress storage;
    };

    void renderScreen(uint32_t nowMs);
    void handleScreenAction(screens::Action action, uint32_t nowMs);
    void handleInput(Input::ActionMask actions, uint32_t nowMs);
    void handleTouch(uint32_t nowMs);
    void runRss();
    void updateTickTickSync(uint32_t nowMs);
    void runBookOpen(size_t index, uint32_t nowMs);
    bool requestTypographyRefresh();
    void updateBackgroundJob();
    bool startBackgroundJob(JobKind kind);
    static void backgroundJobEntry(void* context);
    void runBackgroundJob();
    void recoverOnboardingStorage();
    void enqueueJobUpdate(JobUpdate update, bool mustSucceed = false);
    void showTransientStatus(std::string_view title, std::string_view line1, std::string_view line2,
                             uint32_t durationMs, screens::Screen destination, int progressPercent = -1);
    void applySettings();
    void loadAppearanceSettings();
    void reloadUiAssets();
    void enterUsbTransfer(uint32_t nowMs);
    void exitUsbTransfer(screens::Screen destination = screens::Screen::Reader);
    void runOtaCheck(bool install, bool switching = false, bool automatic = false);
    void enterStandby(uint32_t nowMs);
    void exitStandby(uint32_t nowMs);
    void lightSleepFromStandby();
    // automatic = desligamento por descanso expirado (pode virar deep sleep com despertador, ADR-009); false = manual.
    void powerOff(uint32_t nowMs, bool automatic = false);
    tempo::DecisaoDespertador decidirSono(uint32_t nowMs);
    void iniciarLembreteBoot(uint32_t nowMs);
    static void renderStorageStatus(void* context, const char* title, const char* line1, const char* line2,
                                    int progressPercent);
    bool backgroundJobActive() const {
        return jobKind_ != JobKind::None;
    }
    bool typographyJobActive() const {
        return jobKind_ == JobKind::Typography;
    }
    bool onboardingJobActive() const {return jobKind_==JobKind::OnboardingStorage;}

    ui::Context immediateUi_{Board::Display::gfx()};
    settings::SettingsStore settingsStore_;
    locales::Catalog localeCatalog_;
    Board::Power::BatteryState battery_;
    screens::ReaderScreen readerScreen_{Board::Display::gfx(), settingsStore_.settings().reading};
    screens::LibraryScreen libraryScreen_;
    covers::CoverStore coverStore_{covers::sdIo()};
    std::string jobCoverPath_;
    int openAfterCoverJob_ = -1; // book tapped while a thumbnail was being built
    BookSettingsEditor bookSettings_;
    screens::ChaptersScreen chaptersScreen_;
    screens::InterfaceScreen interfaceScreen_;
    screens::NetworkScreen networkScreen_;
    StorageManager storage_;
    Preferences prefs_;
    screens::FocusScreen focusScreen_;
    screens::TempoScreen tempoScreen_;
    tempo::RastreadorLeitor focoLeitor_;   // foco do leitor contado no Tempo (App::contarFocoLeitor)
    void contarFocoLeitor(uint32_t nowMs);
    screens::OnboardingScreen onboardingScreen_;
    screens::TouchCheckScreen touchCheckScreen_;
    dictionary::Dictionary dictionary_;
    screens::modern::DictionarySetup dictionarySetup_;
    std::atomic<bool> dictionaryCancel_{false};
    bool dictionaryWifiReturn_ = false;
    screens::HomeScreen homeScreen_;
    screens::PerfilScreen perfilScreen_;
    screens::HabitosScreen habitosScreen_;
    // Habitos (fase 2): lembretes (bipe + aviso no topo), "pegou a placa" (IMU) e orientacao em pe dos Habitos
    focus::OrientationReader motionReader_;
    tempo::DetectorPegada pegada_;
    bool pegouPendente_ = false;
    uint32_t motionSamples_ = 0;
    uint32_t habitosTickMs_ = 0;
    std::string habitsError_;
    tempo::Aviso avisoHabito_;
    bool avisoAtivo_ = false;
    bool avisoToque_ = false;
    bool avisoPintar_ = false;
    uint32_t avisoAteMs_ = 0;
    uint32_t avisoPintadoMs_ = 0;
    // Boot "de lembrete" (despertou do deep sleep pelo timer): sem introducao, direto nos Habitos, 60 s esperando interacao.
    bool lembreteBoot_ = false;
    bool lembreteBootIniciado_ = false;
    uint32_t lembreteBootMs_ = 0;
    uint32_t lembreteAtividade0_ = 0;
    bool tickHabitos(uint32_t nowMs);                 // IMU + manutencao + lembretes (fora do desenho). true = tocou um aviso novo
    bool handleAvisoTouch(uint32_t nowMs);            // toque no aviso abre os Habitos (consome o toque)
    void desenharAvisoHabito(uint32_t nowMs);         // por cima da tela atual, enquanto ativo
    bool avisoPermitido() const;
    // Sons (docs/app-tempo/sons.md): configuracao dos Ajustes, decisao (volume, jogo, silencio) e fila de audio.
    som::Config configSom() const;
    void tocarSom(som::SomId id, uint32_t nowMs);     // decide e enfileira (lembretes: direto, sem esperar o quadro)
    void acumularSom(som::SomId id);                  // eventos do mesmo quadro viram UM som (o de maior valor)
    void liberarSom(uint32_t nowMs);
    std::optional<som::SomId> somDoQuadro_;
    bool focoAtivoAntes_ = false;
    void sincronizarOrientacaoHabitos();              // em pe (IMU) so com os Habitos abertos
    screens::Screen lastRenderedScreen_ = screens::Screen::Home;
    bool settingsFromHome_ = false;
    OtaUpdater::Offer otaOffer_;
    bool otaAutomatic_ = false;
    OtaUpdater::Target otaTarget_ = OtaUpdater::Target::Current;
    CompanionApi companionApi_{settingsStore_,   storage_,       localeCatalog_, immediateUi_, readerScreen_,
                               interfaceScreen_, networkScreen_, libraryScreen_, focusScreen_};
    UsbMassStorageManager usbTransfer_;
    CompanionSerial serialCompanion_{companionApi_, usbTransfer_};
    screens::StandbyScreen standbyScreen_;
    QueueHandle_t jobQueue_ = nullptr;
    JobKind jobKind_ = JobKind::None;
    size_t jobBookIndex_ = 0;
    bool jobBookLoaded_ = false;
    size_t pendingBookIndex_ = 0;
    bool bookOpenPending_ = false;
    bool typographyRefreshPending_ = false;
    bool typographyOpensBook_ = false;
    screens::Screen screen_ = screens::Screen::Status;
    screens::Screen statusDestination_ = screens::Screen::Reader;
    uint32_t bootMs_ = 0;
    uint32_t lastActivityMs_ = 0;
    uint32_t standbyEnteredMs_ = 0;
    uint32_t statusUntilMs_ = 0;
    bool restartAfterStatus_ = false;
    std::atomic<bool> onboardingStorageCancel_{false};
    uint32_t onboardingJobRequest_=0,onboardingJobStartedMs_=0;
    bool onboardingJobWasMounted_=false,onboardingStorageNeedsRecovery_=false,onboardingReloadUiPending_=false;
    // Sincronizacao TickTick -> /tempo/projetos.json (segundo plano; ver updateTickTickSync)
    bool tickTickWasLauncher_ = false;
    bool tickTickTried_ = false;
    uint32_t tickTickLastMs_ = 0;
};
