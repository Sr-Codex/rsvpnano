#pragma once

// Foco do leitor (src/focus/) contado no app "Tempo". C++ puro (testavel no host): observa a sessao de foco do
// leitor, fecha uma "rodada" por fase de FOCO (terminou ou foi interrompida) e converte em EntradaBatalha para o
// motor. A gravacao no SD e feita pelo App (TempoScreen::registrarFocoLeitor), nunca durante o desenho.

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "tempo/Codec.h"
#include "tempo/Tipos.h"

namespace tempo {

    inline constexpr const char* kOrigemLeitor = "leitor";

    enum class FaseLeitor : uint8_t { Fora, Foco, Pausado };   // Fora = pausa de intervalo, espera ou sessao parada

    struct EntradaFocoLeitor {
        FaseLeitor fase = FaseLeitor::Fora;
        uint32_t concluidas = 0;   // contador monotonico de fases de foco que terminaram pelo tempo (focus::Session)
        int planejadoMin = 0;      // duracao da fase de foco
        uint32_t agoraMs = 0;
        uint32_t palavra = 0;      // posicao de leitura (indice da palavra) agora
        bool lendo = true;         // leitor rodando (palavras passando) — so assim as palavras contam
    };

    struct RodadaLeitor {
        int planejadoMin = 0;
        int focadoMin = 0;         // minutos efetivamente focados (pausas fora)
        int pausas = 0;            // quantas vezes o foco foi pausado nesta rodada
        bool concluida = false;    // true = a fase de foco terminou pelo tempo
        uint32_t palavras = 0;     // palavras lidas de verdade (avanco com o leitor rodando; arrastar/pulos nao contam)
    };

    // Observa a sessao a cada atualizacao do App; devolve a rodada quando ela termina (conclusao ou parada/saida).
    // Pausas de intervalo (break) nunca contam: so a fase de foco abre rodada.
    class RastreadorLeitor {
    public:
        std::optional<RodadaLeitor> observar(const EntradaFocoLeitor& entrada);
        bool aberta() const { return aberta_; }

    private:
        bool aberta_ = false;
        FaseLeitor anterior_ = FaseLeitor::Fora;
        uint32_t ultimoMs_ = 0;
        uint64_t focoMs_ = 0;
        int pausas_ = 0;
        int planejadoMin_ = 0;
        uint32_t concluidasInicio_ = 0;
        uint32_t palavraFim_ = 0;
        uint32_t palavras_ = 0;
        bool lendoAnterior_ = false;
    };

    // "So quando estou lendo de verdade" (usuario, 2026-11-10): sem leitura real (modo so timer) a rodada nao conta.
    inline constexpr uint32_t kMinPalavrasLeitor = 30;

    // So vale liquidar com leitura real (>= kMinPalavrasLeitor) e pelo menos 1 minuto focado (concluida sempre vale).
    bool rodadaValida(const RodadaLeitor& rodada);

    // Anti-dupla-contagem: se uma batalha do Tempo esta em curso (ou em reflexao), o foco do leitor nao liquida.
    bool podeLiquidarLeitor(bool batalhaTempoAtiva);

    // Rodada concluida = vitoria; parada no meio = fuga (abandonou). Pausa no foco = 1 "distracao" sem cura:
    // tira o bonus de perfeita, mas nunca vira fuga nem cobra tempo perdido. Leitura DA creditos (nunca porPrazer).
    EntradaBatalha paraEntradaBatalha(const RodadaLeitor& rodada);

    // Liquida a rodada no estado do Tempo (mesmo motor: aplicarBatalha) e devolve o evento de log pronto
    // (categoria Leitura, sem projeto, origem "leitor", rotulo = titulo do livro). nullopt = nao liquidou
    // (batalha do Tempo ativa ou rodada < 1 min). O chamador grava log/resumo/estado no SD.
    std::optional<EventoLog> liquidarFocoLeitor(Estado& estado, const RodadaLeitor& rodada, bool batalhaTempoAtiva,
                                                std::string_view tituloLivro, std::string_view dataTexto,
                                                std::string_view horaTexto);

} // namespace tempo
