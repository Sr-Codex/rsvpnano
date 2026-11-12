#pragma once

// Regua de duracao do Inicio do Tempo (v4): fita de paradas sob uma agulha fixa. Logica pura (sem tela), testavel no host.
//
// Duas marchas com a MESMA distancia entre paradas vizinhas (kPxPorParada): 5..60 min de 5 em 5 e 75..300 min de 15 em
// 15 (60 e a troca de marcha). A escala deixa de ser linear em minutos: tudo e dito em INDICE DE PARADA (posicao
// continua em "paradas"). A fita anda 1:1 com o dedo; ao soltar, encaixa na parada mais proxima.
// Etapa b (modo "livre"): uma parada extra a esquerda do 5, o "∞" (valor 0 = livre, sem meta), a kPosInf (-1,5 parada)
// de distancia do indice 0 (o vao e maior que o passo para ler como outra coisa). So a posicao e os limites mudam.

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

    inline constexpr int kLivre = 0;               // "minutos" do modo livre (parada ∞)
    inline constexpr double kPosInf = -1.5;        // posicao (em indices) da parada ∞: 1,5 passo antes do 5
    inline constexpr int kInfAreaX0 = 6, kInfAreaX1 = 50;   // botao do ∞ fixo (x no painel); no mockup 44 x 48
    inline constexpr double kAgulhaX = 230.0;      // x da agulha no painel (460 px de largura)
    inline constexpr double kInfXMinVisivel = 42.0; // abaixo disto o ∞ some da fita e fica fixo na ponta (mockup: x0 + 30)
    constexpr bool ehLivre(int minutos) { return minutos <= 0; }

    int numParadas();                    // 28 (sem contar o ∞)
    int minutoDaParada(int indice);      // indice limitado a 0..numParadas()-1
    int indiceDaParada(int minutos);     // parada mais proxima (empate: a menor), limitada a faixa (livre -> 0)

    // Posicao (em indices, fracionaria) de uma duracao: indice da parada mais proxima; livre (0) = kPosInf.
    double posicaoDaParada(int minutos);
    // x (px) da parada de `minutos` com a agulha em `cx` e a fita centrada em `centroPos` (indice fracionario).
    double xDaParada(int minutos, double centroPos, double cx);
    double xDoIndice(int indice, double centroPos, double cx);
    // Inverso: indice (fracionario) que esta sob a agulha quando a parada `minutos` esta em x.
    double centroDeX(int minutos, double x, double cx);

    double limitar(double pos);          // posicao dentro de kPosInf..numParadas()-1
    int encaixar(double pos);            // minutos da parada mais proxima da posicao (0 = ∞ livre, a esquerda de kPosInf/2)
    int limitarEncaixado(int minutos);   // valor salvo/qualquer inteiro -> minutos de uma parada valida (<= 0 = livre)

    // Duracao do TIMER para o BOOT do launcher (que sempre comeca um Timer, nunca o livre): a ultima da regua se for Timer; se a
    // ultima escolha foi o livre (0), a ultima duracao de Timer guardada (minimo 5).
    int duracaoDoTimer(int ultimaDaRegua, int ultimaDeTimer);

    // ∞ na fita: x (no painel) com a fita centrada em `centroPos`; some da fita (fica FIXO na ponta esquerda) quando passa de
    // kInfXMinVisivel; o toque no ∞ (fixo ou visivel) vai para o modo livre.
    double xDoInf(double centroPos);
    bool infFixo(double centroPos);
    bool toqueNoInf(int xPainel, double centroPos);

    // Opacidade (0..1) de uma marca em x: 1 no miolo, cai linear ate 0 nas pontas [xIni, xFim] (largura `larg`).
    float opacidadePonta(float x, float xIni, float xFim, float larg = kLarguraPontaPx);

    // Toque sem arrasto: metade esquerda do painel = parada anterior, direita = proxima (limites 5 e 300).
    bool metadeDireita(int x, int areaX, int areaW);
    // Livre: o toque a direita vai para 5; o toque a esquerda do 5 NAO entra no ∞ (so arrasto ou o toque no proprio ∞).
    int ajustarPorToque(int atual, bool direita);
    // Passo (min) do rotulo daquele lado a partir de `atual`; 0 = limite (rotulo apagado e sem acao).
    int passoDoLado(int atual, bool direita);

    // Marcas e textos
    bool paradaMaior(int minutos);           // ate 60: dezenas; depois: a cada 30 min
    bool paradaHoraCheia(int minutos);       // > 60 e multiplo de 60 (rotulo em cor mais clara)
    std::string rotuloDaParada(int minutos); // "10" ... "60", "1h30", "2 h", "2h30" ... ("∞" no livre)
    std::string textoDuracao(int minutos);   // "45 min", "2h30", "5 h", "livre" (sub do Comecar)
    struct ParteValor {
        std::string texto;
        bool grande;   // true = fonte de 39 px (so algarismos); false = 17 px (h / min), mesma linha de base
    };
    std::vector<ParteValor> partesDoValor(int minutos);   // "45"+"min" | "2"+"h"+"30" | "5"+"h" | livre: "livre" (so texto)

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
