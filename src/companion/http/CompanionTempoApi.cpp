#include "companion/http/CompanionApi.h"

#include <utility>

#include "board/BoardStorage.h"
#include "network/TickTickSync.h"
#include "storage/tempo/TempoStorage.h"
#include "tempo/TickTick.h"

namespace {

    namespace api = companion::api;

    [[nodiscard]] bool tokenValid(const std::string& token) {
        if (token.size() > tempo::ticktick::kMaxTokenBytes) return false;
        for (const char c : token)
            if (static_cast<unsigned char>(c) <= ' ' || c == 0x7F) return false;
        return true;
    }

    [[nodiscard]] bool tagValid(const std::string& tag) {
        if (tag.size() > tempo::ticktick::kMaxTagBytes) return false;
        for (const char c : tag)
            if (static_cast<unsigned char>(c) < ' ' || c == 0x7F) return false;
        return true;
    }

} // namespace

auto CompanionApi::getTickTick(httpd_req_t& request) -> companion::api::Result<companion::api::TickTickInfo> {
    (void) request;
    const auto& secrets = settingsStore_.secrets();
    const auto status = net::ticktick::status();
    // O token nunca sai da placa: so se ha token, a tag e o resultado da ultima sincronizacao.
    return api::TickTickInfo{.configurado = !secrets.ticktickToken.empty(),
                             .tag = secrets.ticktickTag,
                             .ultimaSync = status.lastSync,
                             .erro = status.error};
}

companion::api::Result<> CompanionApi::putTickTick(httpd_req_t& request) {
    return readJson<api::TickTickUpdate>(request, 1024, "TickTick payload exceeds 1 KB")
        .and_then([this](api::TickTickUpdate update) -> companion::api::Result<> {
            if (update.token && !tokenValid(*update.token)) {
                return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_ticktick_token",
                                                      "Token must be at most 512 characters without spaces", "token"));
            }
            if (update.tag && !tagValid(*update.tag)) {
                return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_ticktick_tag",
                                                      "Tag must be at most 64 bytes", "tag"));
            }
            if (!update.token && !update.tag) {
                return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_ticktick",
                                                      "Send token and/or tag", "token"));
            }
            auto& secrets = settingsStore_.secrets();
            if (update.token) secrets.ticktickToken = std::move(*update.token);
            if (update.tag) secrets.ticktickTag = std::move(*update.tag);
            settingsStore_.acceptSecretChanges();  // gravado na NVS (criptografada) no ciclo normal de persistencia
            return {};
        });
}

companion::api::Result<> CompanionApi::putTempoProjects(httpd_req_t& request) {
    return readBody(request, tempo::kMaxBytesProjetos, "projetos.json exceeds 96 KB")
        .and_then([this](std::string body) -> companion::api::Result<> {
            tempo::ListaProjetos check;
            if (!tempo::lerProjetos(body, check)) {
                return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_projects",
                                                      "Body is not a valid projetos.json"));
            }
            if (!storage_.mounted() || !TempoStorage::saveProjects(Board::Storage::filesystem(), body)) {
                return std::unexpected(api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "projects_save_failed",
                                                      "Could not write /tempo/projetos.json"));
            }
            return {};
        });
}

companion::api::Result<> CompanionApi::putTempoHabits(httpd_req_t& request) {
    return readBody(request, tempo::kMaxBytesHabitos, "habitos.json exceeds 48 KB")
        .and_then([this](std::string body) -> companion::api::Result<> {
            tempo::ListaHabitos check;
            if (!tempo::lerHabitos(body, check)) {
                return std::unexpected(api::httpError(HTTP_CODE_UNPROCESSABLE_ENTITY, "invalid_habits",
                                                      "Body is not a valid habitos.json"));
            }
            if (!storage_.mounted() || !TempoStorage::saveHabits(Board::Storage::filesystem(), body)) {
                return std::unexpected(api::httpError(HTTP_CODE_INTERNAL_SERVER_ERROR, "habits_save_failed",
                                                      "Could not write /tempo/habitos.json"));
            }
            return {};
        });
}
