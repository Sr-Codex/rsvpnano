#include "network/TickTickSync.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include <ctime>
#include <mutex>

#include "network/VerifiedDownload.h"
#include "network/WifiConnection.h"
#include "storage/tempo/TempoStorage.h"
#include "tempo/TickTick.h"

namespace net::ticktick {
    namespace {

        constexpr const char* kTag = "ticktick";
        constexpr uint32_t kHttpTimeoutMs = 15000;
        constexpr uint32_t kNtpRefreshMs = 12UL * 3600UL * 1000UL;

        std::mutex gStatusMutex;
        Status gStatus;
        bool gNtpDone = false;
        uint32_t gNtpMs = 0;

        void record(std::string lastSync, std::string error) {
            const std::lock_guard lock{gStatusMutex};
            if (!lastSync.empty()) gStatus.lastSync = std::move(lastSync);
            gStatus.error = std::move(error);
        }

        struct WifiScope {
            bool owned = WiFi.status() != WL_CONNECTED;
            ~WifiScope() {
                if (owned) net::disconnect();
            }
        };

        struct Body {
            std::string data;
            bool overflow = false;
        };

        esp_err_t onEvent(esp_http_client_event_t* event) {
            if (event->event_id == HTTP_EVENT_ON_DATA && event->data_len > 0) {
                auto& body = *static_cast<Body*>(event->user_data);
                if (body.data.size() + static_cast<size_t>(event->data_len) > tempo::ticktick::kMaxBytesResposta) {
                    body.overflow = true;
                    return ESP_FAIL;
                }
                body.data.append(static_cast<const char*>(event->data), static_cast<size_t>(event->data_len));
            }
            return ESP_OK;
        }

        struct Client {
            esp_http_client_handle_t value;
            ~Client() {
                if (value) esp_http_client_cleanup(value);
            }
        };

        std::string statusError(int status) {
            if (status == 401 || status == 403) return "401: token inválido ou expirado";
            if (status == 429) return "429: limite da API do TickTick; tente mais tarde";
            return "TickTick respondeu HTTP " + std::to_string(status);
        }

        // GET (corpo vazio) ou POST JSON. So devolve o corpo quando o status e 200.
        std::expected<std::string, std::string> call(esp_http_client_method_t method, const std::string& path,
                                                     const std::string& token, const std::string& json) {
            const std::string url = std::string{kApi} + path;
            const std::string bearer = "Bearer " + token;
            Body body;
            esp_http_client_config_t config{};
            config.url = url.c_str();
            config.method = method;
            config.crt_bundle_attach = esp_crt_bundle_attach;  // mesmo bundle de certificados do OTA/downloads
            config.timeout_ms = kHttpTimeoutMs;
            config.buffer_size = 2048;
            config.buffer_size_tx = 1024;
            config.disable_auto_redirect = true;
            config.event_handler = onEvent;
            config.user_data = &body;
            config.user_agent = "RSVP-Nano/0.2";
            Client client{esp_http_client_init(&config)};
            if (!client.value) return std::unexpected("Sem memória para a conexão");
            esp_http_client_set_header(client.value, "Authorization", bearer.c_str());
            esp_http_client_set_header(client.value, "Accept-Encoding", "identity");
            if (method == HTTP_METHOD_POST) {
                esp_http_client_set_header(client.value, "Content-Type", "application/json");
                esp_http_client_set_post_field(client.value, json.c_str(), static_cast<int>(json.size()));
            }
            const esp_err_t result = esp_http_client_perform(client.value);
            if (body.overflow) return std::unexpected("Resposta do TickTick grande demais");
            if (result != ESP_OK) return std::unexpected("Sem rede ou falha HTTPS com o TickTick");
            const int status = esp_http_client_get_status_code(client.value);
            if (status != 200) return std::unexpected(statusError(status));
            return std::move(body.data);
        }

        std::expected<size_t, std::string> run(const settings::NetworkSettings& network,
                                               const settings::DeviceSecrets& secrets, fs::FS& filesystem) {
            if (secrets.ticktickToken.empty()) return std::unexpected("Token do TickTick não configurado");
            const std::string tag = secrets.ticktickTag.empty() ? tempo::ticktick::kTagPadrao : secrets.ticktickTag;

            WifiScope wifi;
            if (wifi.owned && !net::connectSaved(network, secrets))
                return std::unexpected("Sem rede: Wi-Fi não conectou");
            if (auto clock = net::syncDownloadClock(); !clock) return std::unexpected(clock.error());
            if (!gNtpDone || millis() - gNtpMs >= kNtpRefreshMs) {
                if (net::syncNetworkClock()) {  // hora de rede -> sistema + RTC; falha aqui nao aborta
                    gNtpDone = true;
                    gNtpMs = millis();
                }
            }

            auto listas = call(HTTP_METHOD_GET, "/project", secrets.ticktickToken, {});
            if (!listas) return std::unexpected(listas.error());

            std::string tarefas;
            for (const auto& variante : tempo::ticktick::variantesTag(tag)) {
                auto resposta = call(HTTP_METHOD_POST, "/task/filter", secrets.ticktickToken,
                                     tempo::ticktick::corpoFiltro(variante));
                if (!resposta) return std::unexpected(resposta.error());
                const int quantas = tempo::ticktick::contarTarefas(*resposta);
                if (quantas < 0) return std::unexpected("Resposta do TickTick inválida");
                tarefas = std::move(*resposta);
                if (quantas > 0) break;  // vazio: tenta a variante em minúsculas
            }

            const std::string agora = tempo::ticktick::iso8601Utc(static_cast<int64_t>(time(nullptr)));
            auto json = tempo::ticktick::montarProjetos(tarefas, *listas, tag, agora);
            if (!json) return std::unexpected("Não foi possível montar projetos.json");
            if (!TempoStorage::saveProjects(filesystem, *json)) return std::unexpected("Falha ao gravar no cartão SD");
            tempo::ListaProjetos gravada;
            tempo::lerProjetos(*json, gravada);
            return gravada.projetos.size();
        }

    } // namespace

    Status status() {
        const std::lock_guard lock{gStatusMutex};
        return gStatus;
    }

    std::expected<size_t, std::string> sync(const settings::NetworkSettings& network,
                                            const settings::DeviceSecrets& secrets, fs::FS& filesystem) {
        auto result = run(network, secrets, filesystem);
        if (result) {
            record(tempo::ticktick::iso8601Utc(static_cast<int64_t>(time(nullptr))), {});
            ESP_LOGI(kTag, "sync ok: %u projetos", static_cast<unsigned>(*result));
        } else {
            record({}, result.error());
            ESP_LOGW(kTag, "sync falhou: %s", result.error().c_str());
        }
        return result;
    }

} // namespace net::ticktick
