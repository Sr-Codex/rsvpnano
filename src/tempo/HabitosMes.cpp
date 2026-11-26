#include "tempo/HabitosMes.h"

namespace tempo {
    MesHabitos::MesHabitos(int chave) : inicio{chave / 12, chave % 12 + 1, 1} {
        primeiro = (diaDaSemana(inicio) + 1) % 7;
        const int seguinte = chave + 1;
        dias = int(diaSerial({seguinte / 12, seguinte % 12 + 1, 1}) - diaSerial(inicio));
    }

    void incluirDia(DiasPorHabito& dias, int diaNoMesExibido, int diasAtras, std::vector<EventoLog>& eventos) {
        removerDesfeitos(eventos);
        for (const auto& ev : eventos) {
            // O log de hábito só é emitido por uma ação registrada (inclui a feita fora da janela).
            if (ev.tipo == "habito" && !ev.projetoId.empty() &&
                (ev.resultado == "no_horario" || ev.resultado == "atrasado" || ev.resultado == "esquecido")) {
                auto& habito = dias[ev.projetoId];
                if (diaNoMesExibido > 0) habito.dias |= bitDia(diaNoMesExibido);
                if (diasAtras >= 0 && diasAtras < int(habito.recentes.size())) habito.recentes.set(diasAtras);
                if (habito.nome.empty()) habito.nome = ev.projeto;
            }
        }
    }

    std::optional<size_t> horarioDoCartao(const Estado& e, std::string_view id, int minuto,
                                          std::optional<int> horarioEscolhido) {
        std::optional<size_t> melhor;
        int prioridade = 99;
        for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i) {
            const auto& oc = e.dia.ocorrencias[i];
            if (oc.habitoId != id) continue;
            if (horarioEscolhido && oc.minuto == *horarioEscolhido) return i;
            const auto fase = faseDaOcorrencia(oc, minuto);
            const int p = fase == FaseOcorrencia::Agora      ? 0
                          : fase == FaseOcorrencia::Atrasada ? 1
                          : fase == FaseOcorrencia::Futura   ? 2
                          : fase == FaseOcorrencia::Perdida  ? 3
                                                             : 4;
            if (p < prioridade) {
                melhor = i;
                prioridade = p;
            }
        }
        return melhor;
    }
} // namespace tempo
