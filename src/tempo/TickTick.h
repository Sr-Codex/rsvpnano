#pragma once

// Sincronizacao TickTick -> /tempo/projetos.json em C++ puro (sem rede/Arduino): testavel no host.
// O firmware (src/network/TickTickSync.cpp) so faz o HTTPS; aqui ficam o corpo do filtro, a leitura das
// respostas da API oficial (https://developer.ticktick.com/docs/openapi.md) e a montagem do projetos.json
// no mesmo esquema que tools/ticktick_sync.py (pull) gera.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace tempo::ticktick {

    inline constexpr size_t kMaxTarefasApi = 200;                 // limite da API em task/filter
    inline constexpr size_t kMaxBytesResposta = 1024 * 1024;      // teto de memoria por resposta
    inline constexpr size_t kMaxTokenBytes = 512;
    inline constexpr size_t kMaxTagBytes = 64;
    inline constexpr const char* kTagPadrao = "Projeto\xF0\x9F\x93\x9A";  // "Projeto" + livros

    // Tag exata e, se diferente, a variante em minusculas ASCII (a API costuma devolver tags em minusculas).
    std::vector<std::string> variantesTag(std::string_view tag);

    // Corpo de POST /open/v1/task/filter: {"tag":["..."],"status":[0]}.
    std::string corpoFiltro(std::string_view tag);

    // Quantas tarefas a resposta de task/filter traz (-1 = JSON invalido). Usado para decidir a 2a tentativa.
    int contarTarefas(std::string_view tarefasJson);

    // Monta o projetos.json (mesmo esquema de tempo::lerProjetos). Ordena por prioridade (maior primeiro) e
    // vencimento (mais cedo primeiro; sem vencimento por ultimo), limita a tempo::kMaxProjetos e confere o
    // tamanho maximo. nullopt = JSON de tarefas/listas invalido ou resultado maior que o permitido.
    std::optional<std::string> montarProjetos(std::string_view tarefasJson, std::string_view listasJson,
                                              std::string_view tag, std::string_view geradoEm);

    // "AAAA-MM-DDTHH:MM:SSZ" (UTC). Epoch <= 0 devolve "".
    std::string iso8601Utc(int64_t epoch);

} // namespace tempo::ticktick
