#pragma once

// Quando a UI do Tempo/Habitos relê habitos.json + check-ins do SD (lume.12, achados 3 e 8 da revisao).
// Antes: a cada 60 s (parse + alocacao constante = rotatividade de heap interna). Agora so quando:
//  - a sync do TickTick terminou (contador de versao mudou: os arquivos ja foram gravados por inteiro),
//  - o dia virou,
//  - a tela de Habitos abriu (pedido explicito).
// NUNCA enquanto a sync esta rodando (o job de fundo e quem grava esses arquivos: a UI espera o sinal de "terminou").
// C++ puro, testavel no host.

#include <cstdint>

namespace tempo {

    struct EntradaRecarga {
        bool viradaDoDia = false;
        uint32_t versaoSync = 0;     // contador atual (net::ticktick::versao())
        uint32_t versaoVista = 0;    // ultimo valor ja aplicado pela UI
        bool pedidoTela = false;     // tela de Habitos abriu / releitura que ficou pendente
        bool syncRodando = false;    // job do TickTick em andamento: nao ler os arquivos que ele grava
    };

    inline bool deveReler(const EntradaRecarga& e) {
        if (e.syncRodando) return false;
        return e.viradaDoDia || e.pedidoTela || e.versaoSync != e.versaoVista;
    }

    // true (e atualiza `vista`) quando o contador mudou desde a ultima vez: serve para copiar o status (strings) so
    // quando algo mudou, em vez de copiar a cada segundo.
    inline bool consumirVersao(uint32_t& vista, uint32_t atual) {
        if (atual == vista) return false;
        vista = atual;
        return true;
    }

} // namespace tempo
