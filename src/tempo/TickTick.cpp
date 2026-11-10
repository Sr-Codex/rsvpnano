#include "tempo/TickTick.h"

#include <glaze/json.hpp>

#include <algorithm>
#include <ctime>
#include <map>

#include "tempo/Codec.h"

namespace tempo::ticktick {
    namespace api {

        constexpr glz::opts kLeitura{.error_on_unknown_keys = false};

        // Campos que usamos da API; o resto da tarefa (content, desc, items...) e ignorado.
        struct TarefaApi {
            std::string id;
            std::string title;
            std::optional<std::string> projectId;
            std::optional<int> priority;
            std::optional<std::string> dueDate;
            std::optional<std::vector<std::string>> tags;
        };

        struct ListaApi {
            std::string id;
            std::string name;
        };

        struct CorpoFiltro {
            std::vector<std::string> tag;
            std::vector<int> status;
        };

    } // namespace api

    using api::CorpoFiltro;
    using api::ListaApi;
    using api::TarefaApi;
    using api::kLeitura;

    std::vector<std::string> variantesTag(std::string_view tag) {
        std::vector<std::string> saida;
        if (tag.empty()) return saida;
        saida.emplace_back(tag);
        std::string minuscula{tag};
        for (char& c : minuscula)
            if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        if (minuscula != tag) saida.push_back(std::move(minuscula));
        return saida;
    }

    std::string corpoFiltro(std::string_view tag) {
        CorpoFiltro corpo{{std::string{tag}}, {0}};
        std::string saida;
        if (glz::write_json(corpo, saida)) return {};
        return saida;
    }

    int contarTarefas(std::string_view tarefasJson) {
        std::vector<TarefaApi> tarefas;
        if (glz::read<kLeitura>(tarefas, tarefasJson)) return -1;
        return static_cast<int>(tarefas.size());
    }

    std::optional<std::string> montarProjetos(std::string_view tarefasJson, std::string_view listasJson,
                                              std::string_view tag, std::string_view geradoEm) {
        std::vector<TarefaApi> tarefas;
        if (glz::read<kLeitura>(tarefas, tarefasJson)) return std::nullopt;
        std::vector<ListaApi> listas;
        if (!listasJson.empty() && glz::read<kLeitura>(listas, listasJson)) return std::nullopt;

        std::map<std::string, std::string, std::less<>> nomes;
        for (auto& lista : listas) nomes[lista.id] = std::move(lista.name);

        ListaProjetos saida;
        saida.fonte = "ticktick";
        saida.tag = std::string{tag};
        saida.gerado_em = std::string{geradoEm};
        for (auto& t : tarefas) {
            if (t.id.empty()) continue;
            Projeto p;
            p.id = std::move(t.id);
            p.titulo = std::move(t.title);
            p.projectId = t.projectId.value_or("");
            const auto nome = nomes.find(p.projectId);
            p.lista = nome == nomes.end() ? "?" : nome->second;
            p.prioridade = t.priority.value_or(0);
            if (t.dueDate && !t.dueDate->empty()) p.vence = std::move(t.dueDate);
            p.tags = t.tags.value_or(std::vector<std::string>{});
            saida.projetos.push_back(std::move(p));
        }
        std::stable_sort(saida.projetos.begin(), saida.projetos.end(), [](const Projeto& a, const Projeto& b) {
            if (a.prioridade != b.prioridade) return a.prioridade > b.prioridade;
            return a.vence.value_or("9999") < b.vence.value_or("9999");
        });
        if (saida.projetos.size() > kMaxProjetos) saida.projetos.resize(kMaxProjetos);

        std::string json;
        if (glz::write_json(saida, json)) return std::nullopt;
        // Ainda acima do teto: tira os de menor prioridade ate caber (nao deixa o Tempo sem projetos).
        while (json.size() > kMaxBytesProjetos && saida.projetos.size() > 1) {
            saida.projetos.resize(saida.projetos.size() - std::max<size_t>(1, saida.projetos.size() / 8));
            json.clear();
            if (glz::write_json(saida, json)) return std::nullopt;
        }
        if (json.size() > kMaxBytesProjetos) return std::nullopt;
        return json;
    }

    std::string iso8601Utc(int64_t epoch) {
        if (epoch <= 0) return {};
        const std::time_t t = static_cast<std::time_t>(epoch);
        std::tm tm{};
        if (gmtime_r(&t, &tm) == nullptr) return {};
        char buf[24];
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:%02dZ", tm.tm_year + 1900, tm.tm_mon + 1,
                      tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec);
        return buf;
    }

} // namespace tempo::ticktick
