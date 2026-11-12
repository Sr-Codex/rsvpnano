#include "tempo/TickTick.h"

#include <glaze/json.hpp>

#include <algorithm>
#include <ctime>
#include <map>

#include "tempo/Codec.h"

namespace tempo::ticktick {
    namespace api {

        constexpr glz::opts kLeitura{.error_on_unknown_keys = false};

        // Campos que usamos da API; o resto da tarefa (content, desc, items...) e ignorado sem alocar.
        struct TarefaApi {
            std::string id;
            std::string title;
            std::optional<std::string> projectId;
            std::optional<std::string> columnId;
            std::optional<std::string> parentId;
            std::optional<int> status;
            std::optional<int> priority;
            std::optional<std::string> dueDate;
            std::optional<std::vector<std::string>> tags;
        };

        struct ListaApi {
            std::string id;
            std::string name;
            std::optional<bool> closed;
        };

        struct ColunaApi {
            std::string id;
            std::string name;
        };

        // GET /project/{id}/data
        struct DadosListaApi {
            std::optional<std::vector<TarefaApi>> tasks;
            std::optional<std::vector<ColunaApi>> columns;
        };

        struct HabitoApi {
            std::string id;
            std::string name;
            std::optional<std::string> repeatRule;
            std::optional<std::vector<std::string>> reminders;
            std::optional<double> goal;
            std::optional<int> status;
            std::optional<std::string> archivedTime;
            std::optional<int> targetStartDate;
            std::optional<std::string> iconRes;
            std::optional<std::string> color;
        };

        struct CheckinItemApi {
            std::optional<int> stamp;
            std::optional<std::string> time;
            std::optional<double> value;
            std::optional<double> goal;
        };

        struct CheckinAggApi {
            std::string habitId;
            std::optional<std::vector<CheckinItemApi>> checkins;
        };

        struct CorpoCheckin {
            int stamp = 0;
            std::string time;
            double value = 1.0;
            double goal = 1.0;
            int status = 2;   // 2 = concluído; 0 = em andamento/desfeito. Sem isto o APP do TickTick ignora o check-in
        };

    } // namespace api

    using api::ColunaApi;
    using api::DadosListaApi;
    using api::ListaApi;
    using api::TarefaApi;
    using api::kLeitura;

    namespace {

        // Indice do melhor casamento: nome igual (normalizado) ganha; senao o 1o que contem. -1 = nenhum.
        template <typename Itens, typename Nome>
        int melhorPorNome(const Itens& itens, std::string_view alvo, Nome nomeDe) {
            const std::string alvoNorm = normalizarNome(alvo);
            if (alvoNorm.empty()) return -1;
            int contem = -1;
            for (size_t i = 0; i < itens.size(); ++i) {
                const std::string nome = normalizarNome(nomeDe(itens[i]));
                if (nome == alvoNorm) return static_cast<int>(i);
                if (contem < 0 && nome.find(alvoNorm) != std::string::npos) contem = static_cast<int>(i);
            }
            return contem;
        }

    } // namespace

    bool nomeContem(std::string_view nome, std::string_view alvo) {
        const std::string alvoNorm = normalizarNome(alvo);
        return !alvoNorm.empty() && normalizarNome(nome).find(alvoNorm) != std::string::npos;
    }

    std::expected<ListaAchada, std::string> acharLista(std::string_view listasJson, std::string_view nomeLista) {
        std::vector<ListaApi> listas;
        if (glz::read<kLeitura>(listas, listasJson)) return std::unexpected(std::string{"Resposta do TickTick inválida"});
        // listas arquivadas (closed) nao contam
        listas.erase(std::remove_if(listas.begin(), listas.end(), [](const ListaApi& l) { return l.closed.value_or(false); }),
                     listas.end());
        const int i = melhorPorNome(listas, nomeLista, [](const ListaApi& l) -> std::string_view { return l.name; });
        if (i < 0 || listas[static_cast<size_t>(i)].id.empty())
            return std::unexpected("lista " + std::string{nomeLista} + " não encontrada");
        return ListaAchada{listas[static_cast<size_t>(i)].id, listas[static_cast<size_t>(i)].name};
    }

    bool idDeProjetoValido(std::string_view id) {
        if (id.empty() || id.size() > 64) return false;
        for (const char c : id)
            if (!((c >= '0' && c <= '9') || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == '-')) return false;
        return true;
    }

    std::string caminhoDadosLista(std::string_view projectId) {
        if (!idDeProjetoValido(projectId)) return {};
        return "/project/" + std::string{projectId} + "/data";
    }

    std::string projetosVazios(std::string_view geradoEm) {
        ListaProjetos saida;
        saida.fonte = "ticktick";
        saida.gerado_em = std::string{geradoEm};
        std::string json;
        if (glz::write_json(saida, json)) return {};
        return json;
    }

    std::expected<std::string, std::string> montarProjetosDaColuna(std::string_view dadosJson, std::string_view nomeLista,
                                                                  std::string_view nomeColuna, std::string_view geradoEm) {
        DadosListaApi dados;
        if (glz::read<kLeitura>(dados, dadosJson)) return std::unexpected(std::string{"Resposta do TickTick inválida"});
        const auto colunas = dados.columns.value_or(std::vector<ColunaApi>{});
        const int c = melhorPorNome(colunas, nomeColuna, [](const ColunaApi& col) -> std::string_view { return col.name; });
        if (c < 0 || colunas[static_cast<size_t>(c)].id.empty())
            return std::unexpected("coluna " + std::string{nomeColuna} + " não encontrada");
        const std::string& colunaId = colunas[static_cast<size_t>(c)].id;

        ListaProjetos saida;
        saida.fonte = "ticktick";
        saida.gerado_em = std::string{geradoEm};
        for (auto& t : dados.tasks.value_or(std::vector<TarefaApi>{})) {
            if (t.id.empty() || t.columnId.value_or("") != colunaId) continue;
            if (t.status.value_or(0) != 0) continue;                              // so abertas
            if (t.parentId && !t.parentId->empty()) continue;                     // subtarefa nao e projeto
            Projeto p;
            p.id = std::move(t.id);
            p.titulo = std::move(t.title);
            p.projectId = t.projectId.value_or("");
            p.lista = std::string{nomeLista};
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
        if (glz::write_json(saida, json)) return std::unexpected(std::string{"Não foi possível montar projetos.json"});
        // Ainda acima do teto: tira os de menor prioridade ate caber (nao deixa o Tempo sem projetos).
        while (json.size() > kMaxBytesProjetos && saida.projetos.size() > 1) {
            saida.projetos.resize(saida.projetos.size() - std::max<size_t>(1, saida.projetos.size() / 8));
            json.clear();
            if (glz::write_json(saida, json)) return std::unexpected(std::string{"Não foi possível montar projetos.json"});
        }
        if (json.size() > kMaxBytesProjetos) return std::unexpected(std::string{"projetos.json grande demais"});
        return json;
    }

    std::optional<std::string> montarHabitos(std::string_view habitosJson, std::string_view geradoEm) {
        std::vector<api::HabitoApi> habitos;
        if (glz::read<kLeitura>(habitos, habitosJson)) return std::nullopt;
        ListaHabitos saida;
        saida.fonte = "ticktick";
        saida.gerado_em = std::string{geradoEm};
        for (auto& h : habitos) {
            if (h.id.empty() || h.status.value_or(0) != 0) continue;
            if (h.archivedTime && habitoArquivado(*h.archivedTime)) continue;
            DefHabito d;
            d.id = std::move(h.id);
            d.name = std::move(h.name);
            d.repeatRule = h.repeatRule.value_or("");
            d.reminders = h.reminders.value_or(std::vector<std::string>{});
            d.goal = h.goal.value_or(1.0);
            d.targetStartDate = h.targetStartDate.value_or(0);
            d.iconRes = h.iconRes.value_or("");
            d.color = h.color.value_or("");
            saida.habitos.push_back(std::move(d));
        }
        if (saida.habitos.size() > kMaxHabitos) saida.habitos.resize(kMaxHabitos);
        std::string json;
        if (glz::write_json(saida, json) || json.size() > kMaxBytesHabitos) return std::nullopt;
        return json;
    }

    std::string caminhoCheckins(const ListaHabitos& habitos, int stamp) {
        if (habitos.habitos.empty()) return {};
        std::string caminho = "/habit/checkins?habitIds=";
        bool primeiro = true;
        for (const DefHabito& h : habitos.habitos) {
            if (!primeiro) caminho += ',';
            primeiro = false;
            caminho += h.id;
        }
        // A API trata `to` como EXCLUSIVO (medido na conta real, 2026-11-11: from=to=hoje -> [] ;
        // from=hoje&to=amanhã -> os check-ins de hoje). Sem isto a placa nunca via o que foi marcado no celular.
        const int amanha = stampDe(dataDeSerial(diaSerial(dataDeStamp(stamp)) + 1));
        caminho += "&from=" + std::to_string(stamp) + "&to=" + std::to_string(amanha);
        return caminho;
    }

    std::optional<std::string> montarCheckins(std::string_view respostaJson, int stamp, std::string_view geradoEm) {
        std::vector<api::CheckinAggApi> agregados;
        if (glz::read<kLeitura>(agregados, respostaJson)) return std::nullopt;
        ArquivoCheckins saida;
        saida.gerado_em = std::string{geradoEm};
        for (auto& agg : agregados) {
            if (!agg.checkins) continue;
            for (auto& c : *agg.checkins) {
                if (c.stamp.value_or(0) != stamp) continue;
                CheckinRemoto r;
                r.habitId = agg.habitId;
                r.stamp = stamp;
                r.value = c.value.value_or(1.0);
                r.goal = c.goal.value_or(1.0);
                r.time = c.time.value_or("");
                saida.checkins.push_back(std::move(r));
            }
        }
        std::string json;
        if (glz::write_json(saida, json) || json.size() > kMaxBytesCheckins) return std::nullopt;
        return json;
    }

    std::string corpoCheckin(const ItemFila& item) {
        api::CorpoCheckin corpo;
        corpo.stamp = item.stamp;
        corpo.time = item.time;
        corpo.value = item.valor;
        corpo.goal = item.meta;
        // Medido na conta real (2026-11-11): sem "status" a API grava (status null) mas o app não mostra como feito.
        corpo.status = (item.meta > 0.0 && item.valor >= item.meta) ? 2 : 0;
        std::string saida;
        if (glz::write_json(corpo, saida)) return {};
        return saida;
    }

    std::string caminhoPostCheckin(const ItemFila& item) {
        return "/habit/" + item.id + "/checkin";
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
