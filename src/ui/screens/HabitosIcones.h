#pragma once

// Icones vetoriais do app Habitos v2, desenhados com primitivas do Arduino_GFX (a fonte nao tem emoji).
// Tracado proporcional ao tamanho; legivel de 24 a 40 px (s = meia largura: 12 a 20). Os desenhos seguem
// tools/mockup_habitos4.py (funcao icone) — original do projeto, traco tipo Tabler.

#include <Arduino_GFX.h>

#include <cstdint>

#include "tempo/HabitosVista.h"

namespace screens::icones {

    // Todos os desenhos abaixo usam coordenadas LOGICAS; dentro do callback de pintura (Bloco::desenhar) crie um
    // `Origem o(dx, dy)` com o deslocamento que o callback recebe (logico -> buffer). Nao se aplica a desenharAnelApple.
    class Origem {
    public:
        Origem(int dx, int dy);
        ~Origem();
        Origem(const Origem&) = delete;
        Origem& operator=(const Origem&) = delete;

    private:
        int antesDx_, antesDy_;
    };

    int deslocX();
    int deslocY();
    void preencherRR(Arduino_GFX& g, int x, int y, int w, int h, int raio, uint16_t cor);
    void preencherRetangulo(Arduino_GFX& g, int x, int y, int w, int h, uint16_t cor);
    void disco(Arduino_GFX& g, int cx, int cy, int raio, uint16_t cor);

    // Linha com espessura (px) e pontas arredondadas.
    void linha(Arduino_GFX& g, double x0, double y0, double x1, double y1, double espessura, uint16_t cor);
    // Polilinha com espessura.
    void polilinha(Arduino_GFX& g, const double* pontos, int n, double espessura, uint16_t cor, bool fechada = false);
    // Contorno de retangulo arredondado com espessura (para dentro).
    void contorno(Arduino_GFX& g, int x, int y, int w, int h, int raio, int espessura, uint16_t cor);
    // Disco + anel (contorno) com espessura.
    void aro(Arduino_GFX& g, int cx, int cy, int raio, int espessura, uint16_t cor);

    // Icone do habito centrado em (cx, cy); s = meia largura (12 -> 24 px). `fundo` = cor sob o icone (cápsula meio-preenchida).
    void habito(Arduino_GFX& g, tempo::IconeHabito icone, int cx, int cy, int s, uint16_t cor, uint16_t fundo);

    void visto(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor, double espessura = 0);   // check
    void pular(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor);                           // seta "pular"
    void chama(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor);                           // sequencia
    void desfazer(Arduino_GFX& g, int cx, int cy, int s, uint16_t cor);                        // seta curva

    // Estado visual do marcador (estado nunca so por cor: a FORMA muda).
    enum class Marca : uint8_t { Feito, Atual, Atrasado, Pendente, Pulado, Perdido };
    // Marcador circular de raio r com o icone dentro (feito = disco verde + visto; atual/atrasado = anel + icone colorido;
    // pendente = contorno + icone apagado; pulado = contorno tracejado + traco; perdido = contorno carmim + x).
    // `tom` = cor do TickTick (0xRRGGBB, contraste ja garantido; -1 = sem) para o contorno/icone do estado PENDENTE; os demais
    // estados mantem as cores de estado.
    void marcador(Arduino_GFX& g, tempo::IconeHabito icone, int cx, int cy, int r, Marca marca, uint16_t fundo, int32_t tom = -1);
    uint16_t de888(uint32_t rgb);

    // Cores do app (RGB565) — as do mockup aprovado.
    uint16_t verde();
    uint16_t verdeClaro();
    uint16_t laranja();
    uint16_t ambar();
    uint16_t carmim();
    uint16_t carmimClaro();
    uint16_t tinta();       // texto/visto sobre verde e laranja
    uint16_t apagado();     // icone/linha de pendente
    uint16_t linhaSutil();  // contorno de pendente
    uint16_t painel();      // fundo de botao secundario
    uint16_t fundoLaranja();
    uint16_t fundoCarmim();

    // Pontos do dia (feito = disco verde; atual/atrasado = anel; pendente = contorno).
    void ponto(Arduino_GFX& g, int cx, int cy, int r, Marca marca, uint16_t fundo);

    // Faiscas curtas em volta de um anel (celebracao): 10 tracos radiais; `t` 0..1 (cresce e some).
    void faiscas(Arduino_GFX& g, int cx, int cy, int raioAnel, double t);

} // namespace screens::icones
