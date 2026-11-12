#pragma once

// Regua de duracao do Inicio do Tempo (v4): fita de paradas sob uma agulha fixa. Logica pura (sem tela), testavel no host.
//
// Duas marchas com a MESMA distancia entre paradas vizinhas (kPxPorParada): 5..60 min de 5 em 5 e 75..300 min de 15 em
// 15 (60 e a troca de marcha). A escala deixa de ser linear em minutos: tudo e dito em INDICE DE PARADA (posicao
// continua em "paradas"). A fita anda 1:1 com o dedo; ao soltar, encaixa na parada mais proxima.
// Ponto de extensao da etapa b (modo "livre"): uma parada extra a esquerda do 5 = indice negativo, mudando so
// posicaoDaParada() e os limites.

#include <cstdint>
#include <string>
#include <vector>

namespace tempo::regua {

    inline constexpr int kMinMin = 5;          // menor duracao
    inline constexpr int kMaxMin = 300;        // maior duracao (5 h)
    inline constexpr int kTrocaMarchaMin = 60; // ate aqui de 5 em 5; depois de 15 em 15
    inline constexpr int kPassoCurtoMin = 5;
    inline constexpr int kPassoLongoMin = 15;
    inline constexpr int kPrimeiraLongaMin = 75;
    inline constexpr double kPxPorParada = 30.0;   // distancia entre paradas vizinhas nas duas marchas
    inline constexpr int kZonaMortaPx = 8;         // o dedo precisa andar isto antes de virar arrasto (senao e toque)
    inline constexpr int kLarguraPontaPx = 50;     // trecho em que as marcas somem nas pontas

    int numParadas();                    // 28
    int minutoDaParada(int indice);      // indice limitado a 0..numParadas()-1
    int indiceDaParada(int minutos);     // parada mais proxima (empate: a menor), limitada a faixa

    // Posicao (em indices, fracionaria) de uma duracao. Hoje = indice da parada mais proxima.
    double posicaoDaParada(int minutos);
    // x (px) da parada de `minutos` com a agulha em `cx` e a fita centrada em `centroPos` (indice fracionario).
    double xDaParada(int minutos, double centroPos, double cx);
    double xDoIndice(int indice, double centroPos, double cx);
    // Inverso: indice (fracionario) que esta sob a agulha quando a parada `minutos` esta em x.
    double centroDeX(int minutos, double x, double cx);

    double limitar(double pos);          // posicao dentro de 0..numParadas()-1
    int encaixar(double pos);            // minutos da parada mais proxima da posicao
    int limitarEncaixado(int minutos);   // valor salvo/qualquer inteiro -> minutos de uma parada valida

    // Opacidade (0..1) de uma marca em x: 1 no miolo, cai linear ate 0 nas pontas [xIni, xFim] (largura `larg`).
    float opacidadePonta(float x, float xIni, float xFim, float larg = kLarguraPontaPx);

    // Toque sem arrasto: metade esquerda do painel = parada anterior, direita = proxima (limites 5 e 300).
    bool metadeDireita(int x, int areaX, int areaW);
    int ajustarPorToque(int atual, bool direita);
    // Passo (min) do rotulo daquele lado a partir de `atual`; 0 = limite (rotulo apagado e sem acao).
    int passoDoLado(int atual, bool direita);

    // Marcas e textos
    bool paradaMaior(int minutos);           // ate 60: dezenas; depois: a cada 30 min
    bool paradaHoraCheia(int minutos);       // > 60 e multiplo de 60 (rotulo em cor mais clara)
    std::string rotuloDaParada(int minutos); // "10" ... "60", "1h30", "2 h", "2h30" ...
    std::string textoDuracao(int minutos);   // "45 min", "2h30", "5 h" (sub do Comecar)
    struct ParteValor {
        std::string texto;
        bool grande;   // true = fonte de 39 px (so algarismos); false = 17 px (h / min), mesma linha de base
    };
    std::vector<ParteValor> partesDoValor(int minutos);   // "45"+"min" | "2"+"h"+"30" | "5"+"h"

    // Arrasto: o dedo leva a fita (arrastar para a direita faz o valor diminuir). Zona morta de kZonaMortaPx;
    // ao cruzar, a referencia passa para a posicao atual (a fita nao salta).
    struct Arrasto {
        bool ativo = false;
        bool moveu = false;      // passou da zona morta: nao e mais um toque
        int xRef = 0;
        double posRef = 0;       // indice sob a agulha quando a referencia foi marcada
        double pos = 0;          // indice atual (fracionario, ja limitado)

        void comecar(int x, int minutoAtual);
        bool mover(int x);       // true se a posicao mudou
        int soltar();            // encerra e devolve os minutos encaixados (sem arrasto: o valor de partida)
        int minutos() const { return encaixar(pos); }
        void cancelar() { ativo = false; moveu = false; }
    };

} // namespace tempo::regua
