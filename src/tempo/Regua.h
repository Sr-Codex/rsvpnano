#pragma once

// Regua de duracao do Inicio do Tempo (v4): fita de minutos sob uma agulha fixa. Logica pura (sem tela), testavel no host.
//
// A fita anda 1:1 com o dedo; ao soltar, encaixa no multiplo de 5. Faixa 5..90 min. Toda posicao passa por
// posicaoDaParada(): hoje e o proprio minuto; a etapa b (modo "livre") acrescenta uma parada extra a esquerda do 5
// mudando so essa funcao (e kMinParada).

#include <cstdint>

namespace tempo::regua {

    inline constexpr int kMinMin = 5;          // menor duracao
    inline constexpr int kMaxMin = 90;         // maior duracao
    inline constexpr int kPassoMin = 5;        // encaixe e passo do toque
    inline constexpr double kPxPorMin = 6.0;   // escala da fita (marcas de 5 em 5 min caem a cada 30 px, em pixel inteiro)
    inline constexpr int kZonaMortaPx = 8;     // o dedo precisa andar isto antes de virar arrasto (senao e toque)
    inline constexpr int kLarguraPontaPx = 50; // trecho em que as marcas somem nas pontas

    // Posicao (em "minutos de fita") de uma parada. Etapa a: identidade.
    double posicaoDaParada(int minutos);
    // x (px) de uma parada com a agulha em `cx` e a fita centrada em `centroMin` (minutos, pode ser fracionario).
    double xDaParada(int minutos, double centroMin, double cx);
    // Inverso: minuto (fracionario) que esta sob a agulha quando a parada `minutos` esta em x.
    double centroDeX(int minutos, double x, double cx);

    double limitar(double minutos);   // dentro de kMinMin..kMaxMin
    int encaixar(double minutos);     // multiplo de 5 mais proximo, dentro da faixa
    int limitarEncaixado(int minutos);   // valor salvo/qualquer inteiro -> multiplo de 5 dentro da faixa

    // Opacidade (0..1) de uma marca em x: 1 no miolo, cai linear ate 0 nas pontas [xIni, xFim] (largura `larg`).
    float opacidadePonta(float x, float xIni, float xFim, float larg = kLarguraPontaPx);

    // Toque sem arrasto: metade esquerda do painel = -5, direita = +5 (limites 5 e 90).
    bool metadeDireita(int x, int areaX, int areaW);
    int ajustarPorToque(int atual, bool direita);

    // Arrasto: o dedo leva a fita (arrastar para a direita faz o valor diminuir). Zona morta de kZonaMortaPx;
    // ao cruzar, a referencia passa para a posicao atual (a fita nao salta).
    struct Arrasto {
        bool ativo = false;
        bool moveu = false;      // passou da zona morta: nao e mais um toque
        int xRef = 0;
        double minutoRef = 0;    // minuto sob a agulha quando a referencia foi marcada
        double minuto = 0;       // minuto atual (fracionario, ja limitado)

        void comecar(int x, int minutoAtual);
        bool mover(int x);       // true se o minuto mudou
        int soltar();            // encerra e devolve o minuto encaixado (sem arrasto: o valor de partida)
        void cancelar() { ativo = false; moveu = false; }
    };

} // namespace tempo::regua
