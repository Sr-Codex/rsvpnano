#pragma once

// Fechamento da batalha (Tempo v3, etapa 2): quando pedir a Reflexao, o que "Desfazer" reverte e qual e o proximo passo.
// C++ puro (sem Arduino/FS): testavel no host. A tela (TempoScreen) so liga estes pedacos ao SD e ao desenho.
//
// Desfazer da batalha (decisao 22): a liquidacao e capturada INTEIRA (copia do Estado antes de aplicarBatalha) e selada com
// uma "assinatura" do que ela mexeu (carteira, cofre, XP, atributos, streak, totais e eventos do dia). So desfaz se o estado
// ainda bate com a assinatura (nada mais mexeu desde entao) e no mesmo dia. Nao ha `fecharDia` no meio: dia diferente = nao desfaz.
// O log do dia, o resumo.json e o check-in da Meditacao ficam FORA daqui: a tela so os grava ao confirmar o saque
// (fim da janela de 8 s / proximo passo), entao desfazer antes disso nao deixa rastro neles.

#include <array>
#include <cstddef>
#include <memory>

#include "tempo/Sessao.h"
#include "tempo/Tipos.h"

namespace tempo {

    // ---- Reflexao ("Como foi?")
    // A Reflexao so aparece no fim do ciclo pomodoro (ultima rodada, ou quando o usuario encerra o ciclo no meio) e depois
    // de toda batalha unica. Rodadas intermediarias liquidam em silencio e vao direto a pausa. `c` = ciclo NO MOMENTO em
    // que a rodada termina (ciclo encerrado pelo usuario = Ciclo{} inativo).
    bool rodadaPedeReflexao(const Ciclo& c);

    // Tempo sem resposta ate salvar SEM humor.
    inline constexpr int kReflexaoAutoSalvarSeg = 60;

    // Humor 0..4 (Pessimo..Otimo), -1 = sem resposta -> valor do log: 1..5, 0 = sem humor (nunca inventa "Bem").
    int humorParaLog(int humor);
    // Distracao corrigida no chip: 0 Nao, 1 Um pouco, 2 Muito, -1 = nao corrigida -> log 1..3, 0 = nao informado.
    int distraiuParaLog(int distraiu);

    // ---- Proximo passo do saque
    enum class ProximoPasso : uint8_t { VoltarInicio, PausaCurta, PausaLonga };
    // Depois de liquidar a rodada do ciclo `c` (ainda no passo de foco): pausa curta, longa (apos a 4a rodada) ou nada
    // (sessao unica / ciclo encerrado).
    ProximoPasso proximoPassoDoSaque(const Ciclo& c);
    int minutosProximoPasso(ProximoPasso p);   // 0 = VoltarInicio

    // ---- Desfazer da batalha
    struct AssinaturaBatalha {
        double saldo = 0.0, cofre = 0.0, xpTotal = 0.0;
        std::array<double, kNumAtributos> pontosAtributo{};
        int streak = 0;
        int coracoesMeios = 0;
        Data dia;
        bool diaFechado = false;
        size_t eventos = 0;
        double crGanho = 0.0, xpGanho = 0.0, habitosCr = 0.0;
        int fochoPagoMin = 0, fochoValidoMin = 0, meditacaoMin = 0;
        size_t ocorrenciasFeitas = 0;   // ocorrencias de habito ja resolvidas
        bool operator==(const AssinaturaBatalha&) const = default;
    };
    AssinaturaBatalha assinaturaDoEstado(const Estado& e);

    struct InstanteBatalha {
        bool valido = false;
        std::shared_ptr<const Estado> antes;   // copia do estado ANTES da liquidacao (heap: o Estado e grande)
        AssinaturaBatalha depois;              // assinatura DEPOIS (selada por selarBatalha)
    };
    InstanteBatalha capturarBatalha(const Estado& e);              // ANTES de aplicarBatalha (e da Meditacao por timer)
    void selarBatalha(InstanteBatalha& instante, const Estado& e); // DEPOIS
    bool podeDesfazerBatalha(const Estado& e, const InstanteBatalha& instante);
    // Devolve o estado ao de antes da liquidacao (true) ou nao faz nada (false). Saldo, cofre, XP, atributos, totais do dia,
    // streak, eventos do dia e ocorrencias de habito (Meditacao pelo timer) voltam ao que eram.
    bool desfazerBatalha(Estado& e, const InstanteBatalha& instante);

} // namespace tempo
