// GERADO por tools/gerar_cenas_foco.py (repositório lume-nano) — NÃO editar à mão.
// Cenas do Modo Foco: fundos JPEG + revelação progressiva + mapa de rede + cena viva/relógio (v2). Arte original
// (mockups qg1/qg2).
#pragma once

#include <cstddef>
#include <cstdint>

namespace ui::cenas {

    struct Retangulo {
        int16_t x0, y0, x1, y1;   // x1/y1 exclusivos
    };

    // ---- Modo Foco v2: cena viva + relógio (tools/gerar_cenas_foco.py; lógica de tempo em tempo/CenaViva.h)
    struct Cor {
        uint8_t r, g, b;
    };
    struct Caixa8 {
        int16_t x0, y0, x1, y1;   // 1/8 px, coordenadas de borda (o pixel x cobre [x, x+1)), x1/y1 exclusivos
    };
    struct LuzCor {               // elipse (LED, olho, luz de antena) ou retângulo, com a cor acesa
        Caixa8 caixa;
        Cor cor;
    };
    struct Gota {                 // y = janela.y0 + mod(u + vel * fase, altura da janela); traço (x, y) -> (x + dx, y + comp)
        int16_t x8, u8, comp8, dx8, vel8;   // vel em 1/8 px por fase (1 fase = 3 s, o laço dos GIFs dos mockups)
    };
    struct JanelaAcende {         // janela apagada da arte que pode acender (QG Neon)
        Caixa8 caixa;
        uint8_t forca;            // 0..255 da cor
    };
    struct Antena {               // luz vermelha no alto de um prédio (QG Cidade): pisca por camada
        Caixa8 caixa;
        uint8_t camada;
    };
    struct CarroLonge {           // carro ao longe (QG Cidade): x = mod(fase0 + vel * fase, largura + 40) - 20 (relativo à janela)
        int16_t y8, vel8, fase08;
        Cor cor;
    };
    struct CarroViva {            // carro voador (QG Cidade): cruza a janela inteira em 1 fase, no sentido dado
        int16_t y8;
        int8_t sentido;
        uint8_t fase0x100;
        Cor farol;
    };
    struct SpriteLuz {            // luz pré-calculada (núcleo + halo curto) por quadro: luz = tinta * px / 255 (soma "screen")
        Retangulo r;
        uint8_t quadros;
        Cor tinta;
        const uint8_t* px;        // quadros x (x1-x0) x (y1-y0)
    };
    struct RelogioCena {
        uint8_t estilo;           // 0 = neon (fonte do firmware, Atkinson); 1 = LED de 7 segmentos
        uint8_t fonte;            // neon: tamanho da fonte
        int16_t centroX8, centroY8;            // neon: centro horizontal e LINHA DE BASE; LED: centro da linha de dígitos
        int16_t digAlt8, digLarg8, digAvanco8, segEsp8;   // LED: caixa do dígito, avanço e espessura do segmento
        int16_t barraX0x8, barraX1x8, barraY8, barraEsp8;
        uint8_t tuboApagado, forcaFoco, forcaVitoria, forcaTreme, forcaBarra;   // 0..255; forcaTreme multiplica a força
        //                                                                  quando o tubo treme; forcaBarra 0 = a do texto
        Cor corFoco, corVitoria, corHoraExtra;
        uint16_t largoGanhox100[3];            // brilho largo (sigma 9) por fundo: noite, vitória, amanhecer
        Retangulo brilho;                      // região que o relógio pinta (texto + barra + o alcance do brilho largo)
    };
    struct CenaViva {
        uint8_t tipo;                          // 0 = QG Neon, 1 = QG Cidade
        uint16_t curtoSigmax100, curtoGanhox100;   // halo curto (luzes vivas e relógio), igual à composição dos mockups
        uint16_t satx100[2];                   // fotografia (QG Cidade): saturação normal / vitória (100 = sem grade)
        uint8_t vinheta;                       // 1 = vinheta da fotografia (QG Cidade) nas luzes que a placa desenha
        RelogioCena relogio;
        Retangulo janela;
        const Caixa8* oclusores; uint8_t nOclusores;   // moldura, montantes e lâminas da persiana (na frente da cidade)
        const Gota* gotas; uint8_t nGotas;
        Cor corGota; uint8_t forcaGota[3]; uint8_t largGota8;    // por fundo; largura do traço
        Cor corPonta; uint8_t forcaPonta[3]; uint8_t raioPonta8; // pingo na ponta da gota (QG Cidade)
        const LuzCor* roteador; uint8_t nRoteador; uint8_t roteadorApagado;
        const LuzCor* rack; uint8_t nRack; uint8_t rackAceso, rackApagado; Cor rackVitoria;
        const LuzCor* olhos; uint8_t nOlhos;
        const Caixa8* teclas; uint8_t nTeclas; const uint8_t* teclasVis;   // visível 0..255, nTeclas por fundo
        Cor teclaA, teclaB, teclaVitoria; uint8_t teclaForca;
        const Caixa8* faixa; uint8_t nFaixa;
        const JanelaAcende* janelas; uint8_t nJanelas; Cor corJanela;
        int16_t droneY8, droneAmp8, droneVao8, droneEntra8, droneR8, droneBrancoDx8, droneBrancoR8;
        Cor droneCor, droneApagado, droneBranco;
        SpriteLuz sprite;                      // vapor da caneca (QG Neon) / fumaça do cinzeiro (QG Cidade)
        const Antena* antenas; uint8_t nAntenas; Cor corAntena;
        const Caixa8* letreiros; uint8_t nLetreiros;
        const CarroLonge* carrosLonge; uint8_t nCarrosLonge;
        const CarroViva* carrosViva; uint8_t nCarrosViva;
    };

    struct Cena {
        const uint8_t* noite;      size_t noiteTam;        // foco (telas dinâmicas vazias; sem a luz viva)
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
        const CenaViva* viva;          // v2: o que a placa anima/escreve por cima do fundo
    };

    // Limiares de pontilhado (ruído azul 64x64, 0..255): nível = floor((v + limiar/256 * passo) / passo) por canal.
    extern const uint8_t kRuidoAzul64[64 * 64];

    extern const Cena kQgNeon;     // v1: QG neon (fita de LED, relógio de neon, rack, gato)
    extern const Cena kQgCidade;   // v2: cidade cyberpunk na janela, chuva, terminal, relógio de LED

} // namespace ui::cenas
