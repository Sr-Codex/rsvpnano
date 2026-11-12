#pragma once

// "Fim · segure" opcao A (enche): o laranja sobe POR DENTRO do botao, de baixo para cima, proporcional ao progresso; o topo e uma
// reta horizontal e, onde o laranja ja passou, icone e textos trocam para a cor escura. Logica pura (testavel): so decide em que
// linha esta o nivel e de que lado dela cada linha fica; quem desenha compoe as camadas.

#include <algorithm>
#include <cmath>

namespace ui::fim {

    enum class Camada { Base, Transicao, Cheia };

    struct Nivel {
        int linha = 0;          // linha de transicao: acima = base (escura), abaixo = cheia (laranja)
        float cobertura = 0;    // fracao da linha de transicao que ja e laranja (sub-pixel)
        bool encerrando = false;   // 100 %: botao todo laranja com o check
    };

    // Botao de altura h com topo em y; p = progresso 0..1.
    inline Nivel nivel(int y, int h, double p) {
        Nivel n;
        p = std::clamp(p, 0.0, 1.0);
        const double yl = double(y) + double(h) - double(h) * p;   // topo do laranja
        n.linha = int(std::floor(yl));
        n.cobertura = float(std::clamp(double(n.linha) + 1.0 - yl, 0.0, 1.0));
        n.encerrando = p >= 1.0;
        return n;
    }

    inline Camada camadaDaLinha(int py, const Nivel& n) {
        if (py < n.linha) return Camada::Base;
        if (py == n.linha) return n.cobertura > 0.0f ? Camada::Transicao : Camada::Base;
        return Camada::Cheia;
    }

    // true = icone/texto na cor escura (sobre o laranja); a linha de transicao troca quando metade ou mais dela e laranja.
    inline bool textoEscuro(int py, const Nivel& n) {
        const Camada c = camadaDaLinha(py, n);
        return c == Camada::Cheia || (c == Camada::Transicao && n.cobertura >= 0.5f);
    }

    // Ao chegar a 100 % o botao fica todo laranja com o check "encerrando" por kEncerrandoMs antes de encerrar de fato.
    inline constexpr unsigned kEncerrandoMs = 450;
    inline constexpr int kVencidoMs = 2000;   // estado esquecido (a tela saiu da batalha no meio): descarta

    // `quando` = instante em que o encerramento deve ocorrer (0 = nao agendado).
    inline bool agendado(unsigned quando) { return quando != 0; }
    inline unsigned agendar(unsigned agoraMs) { const unsigned q = agoraMs + kEncerrandoMs; return q == 0 ? 1u : q; }
    inline bool hora(unsigned agoraMs, unsigned quando) { return quando != 0 && int(agoraMs - quando) >= 0; }
    inline bool vencido(unsigned agoraMs, unsigned quando) { return quando != 0 && int(agoraMs - quando) > kVencidoMs; }

} // namespace ui::fim
