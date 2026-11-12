#pragma once

// Sincronizacao TickTick -> /tempo/projetos.json em C++ puro (sem rede/Arduino): testavel no host.
// O firmware (src/network/TickTickSync.cpp) so faz o HTTPS; aqui ficam a leitura das respostas da API oficial
// (https://developer.ticktick.com/docs/openapi.md) e a montagem do projetos.json (projetos = coluna AGORA da lista PROJETOS).

#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/Habitos.h"

namespace tempo::ticktick {

    inline constexpr size_t kMaxTarefasApi = 200;                 // limite da API em task/filter
    inline constexpr size_t kMaxBytesResposta = 1024 * 1024;      // teto de memoria por resposta
    inline constexpr size_t kMaxTokenBytes = 512;
    inline constexpr size_t kMaxTagBytes = 64;                     // campo antigo "tag": aceito e ignorado (compatibilidade)
    inline constexpr size_t kMaxNomeBytes = 64;                    // nome configuravel da lista / coluna
    // Projetos = tarefas abertas da coluna "AGORA" da lista "PROJETOS" (o TickTick decide o que fazer; a placa so marca o
    // tempo). Os nomes batem por "contem", sem diferenciar maiusculas/acentos e ignorando emoji ("🎯 PROJETOS", "🚀 AGORA").
    inline constexpr const char* kListaPadrao = "PROJETOS";
    inline constexpr const char* kColunaPadrao = "AGORA";

    // Nome efetivo da lista/coluna: o configurado nos segredos ou, se vazio (config antiga, sem o campo), o padrao.
    std::string nomeEfetivo(std::string_view configurado, std::string_view padrao);

    // `nome` contem `alvo` (ambos normalizados por tempo::normalizarNome: minusculas, sem acento, emoji viram espaco).
    // `alvo` que normaliza para vazio nunca bate.
    bool nomeContem(std::string_view nome, std::string_view alvo);

    struct ListaAchada {
        std::string id;
        std::string nome;   // como veio da API (bruto)
    };
    // GET /open/v1/project -> a lista cujo nome contem `nomeLista` (prefere o nome igual; senao a 1a que contem).
    // Erros legiveis (aparecem na tela): "lista PROJETOS nao encontrada" / "Resposta do TickTick invalida".
    std::expected<ListaAchada, std::string> acharLista(std::string_view listasJson, std::string_view nomeLista);

    // O id vai no caminho da URL: so [A-Za-z0-9_-].
    bool idDeProjetoValido(std::string_view id);
    // "/project/{id}/data" ("" se o id for invalido).
    std::string caminhoDadosLista(std::string_view projectId);

    // projetos.json valido com 0 projetos (lista/coluna nao encontrada: o que sobrou da tag antiga nao pode continuar valendo).
    std::string projetosVazios(std::string_view geradoEm);

    // Resposta de GET /project/{id}/data ({"tasks":[...],"columns":[...]}; content/desc/items sao ignorados sem alocar) ->
    // projetos.json (mesmo esquema de tempo::lerProjetos): tarefas ABERTAS (status 0, sem parentId) da coluna cujo nome contem
    // `nomeColuna`, ordenadas por prioridade (maior primeiro) e vencimento (mais cedo primeiro; sem vencimento por ultimo),
    // limitadas a tempo::kMaxProjetos. Erros legiveis: "coluna AGORA nao encontrada" / "Resposta do TickTick invalida" /
    // "projetos.json grande demais".
    std::expected<std::string, std::string> montarProjetosDaColuna(std::string_view dadosJson, std::string_view nomeLista,
                                                                  std::string_view nomeColuna, std::string_view geradoEm);

    // ---- Habitos (API oficial: GET /habit, GET /habit/checkins, POST /habit/{id}/checkin)
    // habitos.json (mesmo esquema de tempo::lerHabitos) so com habitos ATIVOS (status 0 e nao arquivados; ver
    // tempo::habitoArquivado). nullopt = JSON invalido ou maior que o permitido.
    std::optional<std::string> montarHabitos(std::string_view habitosJson, std::string_view geradoEm);
    // Caminho do GET de check-ins de UM dia: "/habit/checkins?habitIds=A,B&from=AAAAMMDD&to=AAAAMMDD" ("" sem habitos).
    std::string caminhoCheckins(const ListaHabitos& habitos, int stamp);
    // habitos-checkins.json: so os check-ins com stamp == `stamp` (a resposta traz uma lista de agregados por
    // habito/ano, cada um com "checkins"). nullopt = JSON invalido.
    std::optional<std::string> montarCheckins(std::string_view respostaJson, int stamp, std::string_view geradoEm);
    // Corpo do POST /habit/{id}/checkin: {"stamp":20261110,"time":"2026-11-10T11:10:00+0000","value":1,"goal":2,"status":0}
    // status 2 = concluído (valor >= meta); 0 = em andamento ou desfeito. O app do TickTick só mostra "feito" com 2.
    // (campos do OpenAPI oficial; "status" fica de fora: o servidor deduz de value x goal).
    std::string corpoCheckin(const ItemFila& item);
    // "/habit/{id}/checkin"
    std::string caminhoPostCheckin(const ItemFila& item);

    // "AAAA-MM-DDTHH:MM:SSZ" (UTC). Epoch <= 0 devolve "".
    std::string iso8601Utc(int64_t epoch);

} // namespace tempo::ticktick
