#pragma once

// Modo "livre" do Tempo (parada ∞ da regua; economia.md decisao 23) e aviso de 90 min sem toque (decisoes 23/24).
// C++ puro, relogio injetado (deltas em ms): testavel no host. A tela (TempoScreen) so liga estes pedacos ao desenho e ao SD.
//
// Livre = conta PARA CIMA, sem meta e sem inimigo. Reaproveita Cronometro (focadoMs / distraidoMs) e o Motor
// (EntradaBatalha::livre): taxa da categoria x minuto focado, sem bonus perfeito, sem vitoria, tempo distraido nao e pago.

#include <cstdint>
#include <string>
#include <vector>

#include "tempo/Cronometro.h"
#include "tempo/Tipos.h"

namespace tempo::livre {

    inline constexpr uint64_t kMinimoMs = 60000;              // menos de 1 min: nao registra nada
    inline constexpr uint64_t kPausaMaxMs = 30ULL * 60000;    // pausado por mais de 30 min: conclui sozinho
    inline constexpr int kBlocosPorHora = 12;                 // 12 blocos de 5 min por hora
    inline constexpr int kBlocoMin = 5;

    constexpr bool conta(uint64_t focadoMs) { return focadoMs >= kMinimoMs; }
    constexpr bool pausaEstourou(uint64_t pausadoMs) { return pausadoMs > kPausaMaxMs; }

    // Entrada do motor da sessao livre (minutos focados inteiros; distraido fica de fora do pagamento).
    EntradaBatalha entrada(const Cronometro& c, Categoria categoria);

    // Fileira de blocos: `hora` = 1.. ("1ª h", "2ª h"; ao virar a hora recomeca), `cheios` 0..12 blocos de 5 min completos,
    // `parcial` 0..1 = andamento do bloco seguinte.
    struct Blocos {
        int hora = 1;
        int cheios = 0;
        double parcial = 0.0;
    };
    Blocos blocos(uint64_t focadoMs);
    std::string rotuloHora(int hora);                 // "1ª h"
    // Tempo total do saque: "1h12" -> {"1","h","12"}; abaixo de 1 h {"42","min"}. grande = algarismos (fonte grande).
    struct ParteTempo {
        std::string texto;
        bool grande;
    };
    std::vector<ParteTempo> partesDoTempo(uint64_t focadoMs);
    std::string textoTempo(uint64_t focadoMs);        // "1h12" / "42 min"
    std::string textoNaoConta(uint64_t focadoMs);     // "0:40 não conta"
    // Rotulo do canto, depois do simbolo ∞ (desenhado por curva, sem glifo de fonte): "LIVRE · DESDE 14:20"; < 0 = sem hora.
    std::string cabecalhoRodando(int minutoDesdeDoDia);

    // Aviso de 90 min sem resposta: paga so o foco ate a hora do aviso (o que veio depois nao conta).
    constexpr uint64_t pagoAteOAviso(uint64_t focadoMs, uint64_t focadoNoAvisoMs) { return focadoMs < focadoNoAvisoMs ? focadoMs : focadoNoAvisoMs; }

    // "ainda focando?" depois de 90 min sem toque (qualquer sessao rodando). Conta SO o tempo rodando (pausado/distraido
    // tem regras proprias e nao contam): 90 min sem toque -> aviso; 5 min sem resposta -> conclui e paga ate a hora do aviso.
    class AvisoOcioso {
    public:
        static constexpr uint64_t kLimiteMs = 90ULL * 60000;
        static constexpr uint64_t kRespostaMs = 5ULL * 60000;
        enum class Evento : uint8_t { Nada, Avisar, Concluir };

        // Avanca `deltaMs` de relogio; `contando` = sessao rodando (nao pausada nem distraida).
        Evento avancar(uint64_t deltaMs, bool contando);
        // Toque na tela ou botao fisico (inclusive o que so acorda o Modo Foco): zera os 90 min e fecha o aviso.
        void tocou();
        void continuar() { tocou(); }
        bool ativo() const { return ativo_; }
        uint64_t ociosoMs() const { return ociosoMs_; }
        uint64_t restanteMs() const { return ativo_ ? (kRespostaMs > esperaMs_ ? kRespostaMs - esperaMs_ : 0) : kRespostaMs; }
        void zerar() { *this = AvisoOcioso{}; }

    private:
        uint64_t ociosoMs_ = 0;
        uint64_t esperaMs_ = 0;
        bool ativo_ = false;
    };

} // namespace tempo::livre
