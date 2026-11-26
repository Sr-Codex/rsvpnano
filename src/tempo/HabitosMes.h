#pragma once

#include <bitset>
#include <map>
#include "tempo/HabitosVista.h"

namespace tempo {
    struct MesHabitos {
        explicit MesHabitos(int chave);
        Data inicio;
        int primeiro; // coluna do dia 1, domingo = 0
        int dias;
        int linhas() const { return (primeiro + dias + 6) / 7; }
    };

    constexpr int kSemanasHabitos = 12;
    using DiasRecentes = std::bitset<7 * kSemanasHabitos>; // índice zero = hoje
    constexpr int diasAtrasNaGrade(int diaDaSemanaHoje, int coluna, int linha) {
        return 7 * (kSemanasHabitos - 1 - coluna) + diaDaSemanaHoje - linha;
    }

    // Um bit por dia com pelo menos um registro feito. Vários horários contam uma vez.
    struct HistoricoHabito {
        uint32_t dias = 0;
        std::string nome;
        DiasRecentes recentes;
    };
    using DiasPorHabito = std::map<std::string, HistoricoHabito, std::less<>>;
    inline uint32_t bitDia(int dia) { return uint32_t{1} << (dia - 1); }
    void incluirDia(DiasPorHabito& dias, int diaNoMesExibido, int diasAtras, std::vector<EventoLog>& eventos);
    std::optional<size_t> horarioDoCartao(const Estado& e, std::string_view id, int minuto,
                                          std::optional<int> horarioEscolhido);
} // namespace tempo
