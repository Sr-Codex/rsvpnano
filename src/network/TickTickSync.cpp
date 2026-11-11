#include "network/TickTickSync.h"

#include <Arduino.h>
#include <WiFi.h>
#include <esp_crt_bundle.h>
#include <esp_heap_caps.h>
#include <esp_http_client.h>
#include <esp_log.h>

#include <ctime>
#include <atomic>
#include <mutex>
#include <utility>
#include <vector>

#include "network/VerifiedDownload.h"
#include "network/WifiConnection.h"
#include "storage/tempo/TempoStorage.h"
#include "tempo/Habitos.h"
#include "tempo/Relogio.h"
#include "tempo/TickTick.h"

namespace net::ticktick {
    namespace {

        constexpr const char* kTag = "ticktick";
        constexpr uint32_t kHttpTimeoutMs = 15000;
        constexpr uint32_t kNtpRefreshMs = 12UL * 3600UL * 1000UL;

        std::mutex gStatusMutex;
        Status gStatus;
        std::atomic<uint32_t> gVersao{0};
        bool gNtpDone = false;
        uint32_t gNtpMs = 0;

        void record(std::string lastSync, std::string error) {
            const std::lock_guard lock{gStatusMutex};
            if (!lastSync.empty()) gStatus.lastSync = std::move(lastSync);
            gStatus.error = std::move(error);
        }

        void recordHabits(std::string error) {
            const std::lock_guard lock{gStatusMutex};
            gStatus.habitsError = std::move(error);
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

        std::string statusError(int status) {
            if (status == 401 || status == 403) return "401: token inválido ou expirado";
            if (status == 429) return "429: limite da API do TickTick; tente mais tarde";
            return "TickTick respondeu HTTP " + std::to_string(status);
        }

        struct Reply {
            int status = 0;        // 0 = sem rede / falha HTTPS
            std::string body;
            std::string error;     // so quando status == 0 (falha local)
        };

        // Uma conexão HTTPS (keep-alive) reaproveitada pela sync inteira (lume.12). Antes cada chamada abria e fechava
        // um TLS (~40 KB contíguos de SRAM interna); depois de 3–4 seguidas o maior bloco livre caía para ~24 KB e o
        // check-in falhava com ESP_ERR_HTTP_CONNECT (medido na placa, lume.11). Agora o TLS é alocado uma vez, no
        // começo, com a memória inteira. Destruir a Sessao ANTES de desligar o Wi-Fi (ordem de declaração em run()).
        struct Sessao {
            esp_http_client_handle_t handle = nullptr;
            Body body;
            ~Sessao() { fechar(); }
            void fechar() {
                if (handle) {
                    esp_http_client_cleanup(handle);
                    handle = nullptr;
                }
            }
        };

        std::string erroTransporte(esp_err_t result) {
            // Diagnóstico (lume.11): inclui o erro do ESP-IDF e a memória interna livre, p/ distinguir Wi-Fi, TLS,
            // tempo esgotado e falta de memória.
            return std::string{"Falha HTTPS ("} + esp_err_to_name(result) + ", maior bloco livre "
                   + std::to_string(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT) / 1024U)
                   + " KB, Wi-Fi " + (WiFi.status() == WL_CONNECTED ? "ok" : "caiu") + ")";
        }

        // GET (corpo vazio) ou POST JSON pela sessão. Devolve o status HTTP e o corpo, qualquer que seja o status.
        // Conexão reaproveitada que falhar (servidor fechou o keep-alive) = 1 nova tentativa com conexão nova.
        Reply request(Sessao& sessao, esp_http_client_method_t method, const std::string& path, const std::string& token,
                      const std::string& json) {
            const std::string url = std::string{kApi} + path;
            const std::string bearer = "Bearer " + token;
            Reply reply;
            for (int tentativa = 0; tentativa < 2; ++tentativa) {
                const bool reaproveitada = sessao.handle != nullptr;
                if (!reaproveitada) {
                    esp_http_client_config_t config{};
                    config.url = url.c_str();
                    config.method = method;
                    config.crt_bundle_attach = esp_crt_bundle_attach;  // mesmo bundle de certificados do OTA/downloads
                    config.timeout_ms = kHttpTimeoutMs;
                    config.buffer_size = 2048;
                    config.buffer_size_tx = 1024;
                    config.disable_auto_redirect = true;
                    config.keep_alive_enable = true;
                    config.event_handler = onEvent;
                    config.user_data = &sessao.body;
                    config.user_agent = "RSVP-Nano/0.2";
                    sessao.handle = esp_http_client_init(&config);
                    if (!sessao.handle) {
                        reply.error = "Sem memória para a conexão";
                        return reply;
                    }
                } else {
                    esp_http_client_set_url(sessao.handle, url.c_str());
                    esp_http_client_set_method(sessao.handle, method);
                }
                sessao.body = {};
                esp_http_client_set_header(sessao.handle, "Authorization", bearer.c_str());
                esp_http_client_set_header(sessao.handle, "Accept-Encoding", "identity");
                if (method == HTTP_METHOD_POST) {
                    esp_http_client_set_header(sessao.handle, "Content-Type", "application/json");
                    esp_http_client_set_post_field(sessao.handle, json.c_str(), static_cast<int>(json.size()));
                } else {
                    esp_http_client_delete_header(sessao.handle, "Content-Type");
                    esp_http_client_set_post_field(sessao.handle, nullptr, 0);
                }
                const esp_err_t result = esp_http_client_perform(sessao.handle);
                if (sessao.body.overflow) {
                    sessao.fechar();   // resposta cortada no meio: a conexão fica em estado indefinido
                    reply.error = "Resposta do TickTick grande demais";
                    return reply;
                }
                if (result != ESP_OK) {
                    sessao.fechar();
                    reply.error = erroTransporte(result);
                    ESP_LOGW(kTag, "%s %s%s: %s", method == HTTP_METHOD_POST ? "POST" : "GET", path.c_str(),
                             reaproveitada ? " (conexão reaproveitada)" : "", reply.error.c_str());
                    if (reaproveitada) continue;   // keep-alive caiu: tenta 1x com conexão nova
                    return reply;
                }
                reply.status = esp_http_client_get_status_code(sessao.handle);
                reply.body = std::move(sessao.body.data);
                return reply;
            }
            return reply;
        }

        // GET (corpo vazio) ou POST JSON. So devolve o corpo quando o status e 200.
        std::expected<std::string, std::string> call(Sessao& sessao, esp_http_client_method_t method,
                                                     const std::string& path, const std::string& token,
                                                     const std::string& json) {
            Reply reply = request(sessao, method, path, token, json);
            if (reply.status == 0) return std::unexpected(std::move(reply.error));
            if (reply.status != 200) return std::unexpected(statusError(reply.status));
            return std::move(reply.body);
        }

        // Falha de transporte (status 0) nos hábitos: 1 nova tentativa após uma pausa curta (lume.11).
        Reply requestComRetry(Sessao& sessao, esp_http_client_method_t method, const std::string& path,
                              const std::string& token, const std::string& json) {
            Reply reply = request(sessao, method, path, token, json);
            if (reply.status == 0) {
                delay(800);
                reply = request(sessao, method, path, token, json);
            }
            return reply;
        }

        std::expected<std::string, std::string> callComRetry(Sessao& sessao, esp_http_client_method_t method,
                                                             const std::string& path, const std::string& token,
                                                             const std::string& json) {
            Reply reply = requestComRetry(sessao, method, path, token, json);
            if (reply.status == 0) return std::unexpected(std::move(reply.error));
            if (reply.status != 200) return std::unexpected(statusError(reply.status));
            return std::move(reply.body);
        }

        // Hábitos, passo 1 (lume.12: roda ANTES dos projetos — o "Feito" do usuário é o que mais importa chegar):
        // fila persistente -> POST /habit/{id}/checkin (escopo tasks:write). Devolve "" ou o erro.
        std::string enviarFilaHabitos(Sessao& sessao, const settings::DeviceSecrets& secrets, fs::FS& filesystem) {
            std::string error;
            const auto queue = TempoStorage::loadQueue(filesystem);
            std::vector<std::pair<tempo::ItemFila, int>> results;
            for (const auto& item : queue) {
                if (!tempo::devePreEnviar(item)) continue;
                const Reply reply = requestComRetry(sessao, HTTP_METHOD_POST, tempo::ticktick::caminhoPostCheckin(item),
                                                    secrets.ticktickToken, tempo::ticktick::corpoCheckin(item));
                results.emplace_back(item, reply.status);
                if (reply.status == 0) {   // sem rede: guarda e para
                    error = "check-in: " + reply.error;
                    break;
                }
                if (reply.status != 200 && reply.status != 201) {
                    error = "Check-in recusado: " + statusError(reply.status);
                    if (reply.status == 401 || reply.status == 403) break;   // token sem permissão de escrita
                }
            }
            if (!results.empty()) TempoStorage::applyQueueResults(filesystem, results);
            return error;
        }

        // Hábitos, passo 2: baixa os hábitos ativos e os check-ins de hoje (feitos no celular).
        // Melhor esforço: devolve "" se tudo deu certo, senão o erro; nunca derruba a sync dos projetos.
        std::string baixarHabitos(Sessao& sessao, const settings::DeviceSecrets& secrets, fs::FS& filesystem,
                                  const std::string& agora) {
            auto habits = callComRetry(sessao, HTTP_METHOD_GET, "/habit", secrets.ticktickToken, {});
            if (!habits) return "lista de hábitos: " + habits.error();
            const auto json = tempo::ticktick::montarHabitos(*habits, agora);
            if (!json || !TempoStorage::saveHabits(filesystem, *json)) return "Não foi possível gravar habitos.json";
            tempo::ListaHabitos list;
            tempo::lerHabitos(*json, list);

            const auto today = tempo::deEpoch(static_cast<int64_t>(time(nullptr)));
            const std::string path =
                today.valida ? tempo::ticktick::caminhoCheckins(list, tempo::stampDe(today.data)) : std::string{};
            if (!path.empty()) {
                auto checkins = callComRetry(sessao, HTTP_METHOD_GET, path, secrets.ticktickToken, {});
                if (!checkins) return "check-ins de hoje: " + checkins.error();
                const auto file = tempo::ticktick::montarCheckins(*checkins, tempo::stampDe(today.data), agora);
                if (!file || !TempoStorage::saveHabitCheckins(filesystem, *file))
                    return "Não foi possível gravar habitos-checkins.json";
            }
            return {};
        }

        std::expected<size_t, std::string> run(const settings::NetworkSettings& network,
                                               const settings::DeviceSecrets& secrets, fs::FS& filesystem) {
            if (secrets.ticktickToken.empty()) return std::unexpected("Token do TickTick não configurado");
            const std::string tag = secrets.ticktickTag.empty() ? tempo::ticktick::kTagPadrao : secrets.ticktickTag;

            WifiScope wifi;
            // Falhas antes dos hábitos também aparecem na tela de Hábitos ("erro de sync"), senão ela fica muda.
            if (wifi.owned && !net::connectSaved(network, secrets)) {
                recordHabits("Sem rede: Wi-Fi não conectou");
                return std::unexpected("Sem rede: Wi-Fi não conectou");
            }
            if (auto clock = net::syncDownloadClock(); !clock) {
                recordHabits(clock.error());
                return std::unexpected(clock.error());
            }
            if (!gNtpDone || millis() - gNtpMs >= kNtpRefreshMs) {
                if (net::syncNetworkClock()) {  // hora de rede -> sistema + RTC; falha aqui nao aborta
                    gNtpDone = true;
                    gNtpMs = millis();
                }
            }

            Sessao sessao;   // declarada DEPOIS do WifiScope: é destruída (TLS fechado) antes do Wi-Fi desligar
            const std::string erroFila = enviarFilaHabitos(sessao, secrets, filesystem);
            // lume.16: hábitos ANTES dos projetos. Antes, uma falha nos projetos (ex.: memória) impedia de baixar os
            // hábitos e a tela não avisava nada (hábito novo "Escovar Dente" não aparecia, 2026-11-11).
            const std::string agora = tempo::ticktick::iso8601Utc(static_cast<int64_t>(time(nullptr)));
            const std::string erroBaixar = baixarHabitos(sessao, secrets, filesystem, agora);
            recordHabits(!erroFila.empty() ? erroFila : erroBaixar);   // não derruba a sync dos projetos

            auto listas = call(sessao, HTTP_METHOD_GET, "/project", secrets.ticktickToken, {});
            if (!listas) return std::unexpected(listas.error());

            std::string tarefas;
            for (const auto& variante : tempo::ticktick::variantesTag(tag)) {
                auto resposta = call(sessao, HTTP_METHOD_POST, "/task/filter", secrets.ticktickToken,
                                     tempo::ticktick::corpoFiltro(variante));
                if (!resposta) return std::unexpected(resposta.error());
                const int quantas = tempo::ticktick::contarTarefas(*resposta);
                if (quantas < 0) return std::unexpected("Resposta do TickTick inválida");
                tarefas = std::move(*resposta);
                if (quantas > 0) break;  // vazio: tenta a variante em minúsculas
            }

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

    uint32_t versao() {
        return gVersao.load(std::memory_order_acquire);
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
        gVersao.fetch_add(1, std::memory_order_release);   // por ultimo: os arquivos e o status ja estao gravados
        return result;
    }

} // namespace net::ticktick
