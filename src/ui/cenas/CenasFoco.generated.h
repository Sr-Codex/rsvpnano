// GERADO por tools/gerar_cenas_foco.py (repositório lume-nano) — NÃO editar à mão.
// Cenas do Modo Foco: fundos JPEG + revelação progressiva + mapa de rede. Arte original (mockups qg1/qg2).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::cenas {

    struct Retangulo {
        int16_t x0, y0, x1, y1;   // x1/y1 exclusivos
    };

    struct Cena {
        const uint8_t* noite;      size_t noiteTam;        // foco (telas dinâmicas vazias)
        const uint8_t* vitoria;    size_t vitoriaTam;      // 100 %
        const uint8_t* amanhecer;  size_t amanhecerTam;    // hora extra
        Retangulo revela;                                  // tela que enche com o tempo
        const uint16_t* revelaPx;                          // RGB565, (x1-x0)*(y1-y0), linha a linha
        uint8_t revelaModo;            // 0 = fração do foco (linhas de cima); 1 = cabeçalho + 1 linha por nó
        int16_t linhaY0x10, linhaPassox10;   // modo 1: topo da 1ª linha e passo (em 1/10 px, relativos)
        uint8_t linhasCabecalho, linhasMax;
        Retangulo mapa;
        int16_t noX4[12], noY4[12];    // centros dos nós em 1/4 de pixel (coordenadas da tela)
        uint8_t ordem[12];             // ordem em que os nós acendem
        uint8_t elos[14][2];
        uint16_t corNoApagado, corNoAntes, corNoNovo, corEloApagado, corEloAntes, corEloNovo;
        uint16_t corCursor;
    };

    // Limiares de pontilhado (ruído azul 64x64, 0..255): nível = floor((v + limiar/256 * passo) / passo) por canal.
    extern const uint8_t kRuidoAzul64[64 * 64];

    extern const Cena kQgNeon;     // v1: QG neon (fita de LED, neon LUME, rack, gato)
    extern const Cena kQgCidade;   // v2: cidade cyberpunk na janela, chuva, terminal

} // namespace ui::cenas
