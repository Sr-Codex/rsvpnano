#pragma once

#include <cstdint>

#include "ui/Ui.h"

namespace ui {

    // Uma volta do laco de toques do App: quantas amostras sairam da fila e quantos arrastos foram juntados sem entregar.
    struct VoltaToque {
        uint16_t amostras = 0;
        uint16_t movesDescartados = 0;
    };

    // Esvazia a fila de toques de uma volta do laco do App.
    //   * Start, Release, Cancel e Hold (e qualquer evento que nao seja arrasto puro) vao para `tratar()` na ordem e na hora.
    //   * De uma sequencia de arrastos puros (so TouchMove) so o ULTIMO e entregue: no fim da volta, quando a fila secou.
    //     Se chegar outro evento antes, o arrasto adiado e descartado: Soltar/Cancelar carregam a posicao final do dedo.
    //   * `podeJuntar()` decide, a cada arrasto, se a tela atual aceita juntar (o leitor trata cada amostra sem desenhar).
    // Motivo: cada evento entregue redesenha a tela; quando o quadro passa do intervalo do controlador (30 ms na 3.49), as
    // amostras acumulam e a soltura so seria tratada depois do backlog (Fim · segure completava com o dedo ja fora).
    // `nowMs` e o mesmo relogio que `tratar()` atualiza (ele le millis() depois de desenhar).
    template<typename PodeJuntar, typename Tratar>
    VoltaToque esvaziarToques(Context& ui, uint32_t& nowMs, PodeJuntar&& podeJuntar, Tratar&& tratar) {
        VoltaToque volta;
        bool adiado = false;
        while (ui.pollTouch(nowMs)) {
            ++volta.amostras;
            if (ui.lastTouchIsPureMove() && podeJuntar()) {
                if (adiado) ++volta.movesDescartados;   // o arrasto anterior ficou velho
                adiado = true;
                continue;
            }
            if (adiado) ++volta.movesDescartados;
            adiado = false;
            tratar();
        }
        if (adiado) {   // a fila secou com um arrasto pendente: entrega so ele
            ui.replayLastTouch();
            tratar();
        }
        return volta;
    }

} // namespace ui
