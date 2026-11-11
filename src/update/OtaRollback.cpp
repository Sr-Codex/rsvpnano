#include "update/OtaRollback.h"

#include "update/OtaConfirmacao.h"

#if !defined(LUME_SIM) && defined(ESP_PLATFORM)
#include <esp_log.h>
#include <esp_ota_ops.h>

namespace ota {
    namespace {
        constexpr const char* kTag = "ota";
        Confirmador gConf;
        EstadoImagem gEstado = EstadoImagem::SemEstado;
        uint8_t gFalhas = 0;

        EstadoImagem lerEstado() {
            const esp_partition_t* rodando = esp_ota_get_running_partition();
            esp_ota_img_states_t estado;
            if (rodando == nullptr || esp_ota_get_state_partition(rodando, &estado) != ESP_OK) return EstadoImagem::SemEstado;
            switch (estado) {
            case ESP_OTA_IMG_PENDING_VERIFY: return EstadoImagem::Pendente;
            case ESP_OTA_IMG_VALID: return EstadoImagem::Valida;
            default: return EstadoImagem::Outro;
            }
        }

        void confirmar(const char* motivo) {
            if (esp_ota_mark_app_valid_cancel_rollback() == ESP_OK) {
                gConf.confirmada();
                ESP_LOGI(kTag, "ota: imagem nova confirmada (%s)", motivo);
            } else if (++gFalhas >= 3) {
                gConf.confirmada();   // desiste de insistir (nao fica em laco de erro)
                ESP_LOGE(kTag, "ota: falha ao confirmar a imagem (%s)", motivo);
            } else {
                ESP_LOGW(kTag, "ota: falha ao confirmar a imagem (%s); tentando de novo", motivo);
            }
        }
    } // namespace

    void iniciar() {
        gEstado = lerEstado();
        if (gEstado == EstadoImagem::Pendente)
            ESP_LOGI(kTag, "ota: aguardando confirmação (imagem nova em teste; reiniciar antes disso volta a imagem anterior)");
        else
            ESP_LOGI(kTag, "ota: imagem sem confirmação pendente (estado=%d)", static_cast<int>(gEstado));
    }
    void setupConcluido() { gConf.setupConcluido(); }
    void toqueOk(bool ok) { gConf.toqueOk(ok); }
    void sdMontado(bool ok) { gConf.sdMontado(ok); }
    void primeiroQuadro(uint32_t nowMs) { gConf.primeiroQuadro(nowMs); }
    void tick(uint32_t nowMs) {
        if (gEstado != EstadoImagem::Pendente || gConf.jaConfirmada()) return;
        gConf.tick(nowMs);
        if (gConf.deveConfirmar(gEstado, nowMs)) confirmar("laço saudável");
    }
    void confirmarAntesDeReiniciar(const char* motivo) {
        if (gConf.deveConfirmarAntesDeReiniciar(gEstado)) confirmar(motivo);
        else if (gEstado == EstadoImagem::Pendente && !gConf.jaConfirmada())
            ESP_LOGW(kTag, "ota: reiniciando SEM confirmar (%s; ainda não saudável) -> o bootloader vai voltar a imagem anterior", motivo);
    }
} // namespace ota

#else

namespace ota {
    void iniciar() {}
    void setupConcluido() {}
    void toqueOk(bool) {}
    void sdMontado(bool) {}
    void primeiroQuadro(uint32_t) {}
    void tick(uint32_t) {}
    void confirmarAntesDeReiniciar(const char*) {}
} // namespace ota

#endif
