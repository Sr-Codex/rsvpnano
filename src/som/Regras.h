#pragma once

// Regras dos sons do Lume (docs/app-tempo/sons.md): classe/prioridade, volume, "sons de jogo", horario de silencio,
// arbitragem do que toca quando chega um pedido novo e escolha do UNICO som de um evento composto. C++ puro, testavel.

#include <cstdint>
#include <initializer_list>
#include <optional>

#include "som/Sintese.h"

namespace som {

    // Prioridade crescente. Medicacao ignora silencio e a chave de sons de jogo; Lembrete ignora so a chave de jogo.
    enum class Classe : uint8_t {
        Interface,   // Bip (confirmacao simples)
        Jogo,        // vitoria, moeda, nivel, conquista, habito feito, comecar, distracao, descanso...
        Lembrete,    // DoisBipes (lembrete de habito comum)
        Medicacao,   // AlertaLongo (medicacao e insistencia)
    };
    Classe classeDe(SomId id);

    struct Config {
        Volume volume = Volume::Medio;
        bool sonsJogo = true;                 // desliga so a classe Jogo
        bool silencioAtivo = false;
        uint16_t silencioInicioMin = 23 * 60;   // minuto do dia (padrao 23:00)
        uint16_t silencioFimMin = 7 * 60;       // padrao 07:00
    };

    // Dentro da janela [inicio, fim) (cruza a meia-noite se inicio > fim; inicio == fim = janela vazia).
    // minutoDoDia < 0 (relogio invalido) = nunca em silencio.
    bool emSilencio(const Config& config, int minutoDoDia);

    enum class Motivo : uint8_t { Toca, SonsDeJogoDesligados, Silencio };
    struct Decisao {
        bool tocar = false;
        Motivo motivo = Motivo::Toca;
        float ganho = 1.0f;
    };
    Decisao decidir(SomId id, const Config& config, int minutoDoDia);

    // Dentre varios sons candidatos de UM evento composto, o de maior valor (Jingle > PowerUp > FaseCompleta > Vitoria
    // > Moeda > SuaveDescendo > SobeQuatro > SobeTres > DesceAlerta > ...). Lista vazia -> nullopt.
    int valorDoEvento(SomId id);
    std::optional<SomId> escolher(std::initializer_list<SomId> candidatos);

    struct Pedido {
        SomId som = SomId::Bip;
        float ganho = 1.0f;
        Classe classe = Classe::Interface;
    };

    // Arbitro do que toca (sem relogio nem RTOS). Regras:
    //  - nada tocando: toca agora;
    //  - classe MAIOR que a do som atual: interrompe (um Lembrete interrompido por Medicacao fica aguardando);
    //  - mesma classe: Jogo substitui o atual (vale o mais recente); Interface e o mesmo som repetido sao descartados;
    //    Lembrete/Medicacao diferentes aguardam (1 vaga) e tocam quando o atual terminar;
    //  - classe MENOR: Lembrete/Medicacao aguardam; Jogo/Interface sao descartados.
    class Arbitro {
    public:
        enum class Acao : uint8_t { TocarAgora, Aguardar, Descartar };

        Acao pedir(const Pedido& pedido);
        // O som atual terminou: devolve o que deve tocar a seguir (o que aguardava) ou nullopt.
        std::optional<Pedido> concluir();
        void parar();
        bool ocupado() const { return atual_.has_value(); }
        const std::optional<Pedido>& atual() const { return atual_; }
        const std::optional<Pedido>& aguardando() const { return aguardando_; }

    private:
        void guardar(const Pedido& pedido);
        std::optional<Pedido> atual_;
        std::optional<Pedido> aguardando_;
    };

} // namespace som
