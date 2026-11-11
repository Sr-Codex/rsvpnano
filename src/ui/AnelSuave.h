#pragma once

// Anel de progresso "estilo Apple" com borda suavizada (anti-aliasing), sem depender da biblioteca grafica.
//
// Cada pixel recebe a cor proporcional a quanto dele fica dentro da forma (borda de fora, borda de dentro, pontas
// redondas e a sombra da 2a volta). O degrade e continuo ao longo do angulo (sem fatias). Logica pura: o desenho na
// tela fica em quem chama (`pintar` entrega pixel a pixel), o que permite testar no `native_test`.
//
// Geometria: centro do pixel (x, y) = coordenada inteira; angulo 0 no topo, sentido horario; frac > 1 = 2a volta
// por cima com sombra sob a ponta (mesmo comportamento do anel anterior, feito com triangulos sem suavizacao).

#include <cstdint>

namespace ui::anel {

    struct Rgb {
        float r = 0, g = 0, b = 0;
    };

    struct Params {
        int cx = 0, cy = 0;          // centro, em coordenadas do alvo de pintura
        float raio = 0, larg = 0;    // raio da linha central e espessura
        double frac = 0;             // 0..1 = 1a volta; 1..2 = 2a volta por cima
        Rgb c0, c1;                  // degrade do arco (inicio -> fim)
        Rgb fundo;                   // cor por tras do anel (o miolo e pintado com ela)
        Rgb baseTrilho;              // trilho = 22 % desta cor sobre o fundo
        const Rgb* trilhoFixo = nullptr;   // ou: trilho exatamente nesta cor
        const Rgb* volta1a0 = nullptr;   // se frac > 1: cores da 1a volta completa (ex.: overtime laranja)
        const Rgb* volta1a1 = nullptr;
    };

    // Geometria e camadas pre-calculadas (uma vez por anel).
    class Anel {
    public:
        explicit Anel(const Params& p);

        // Cor do pixel (x, y). Retorna false se o anel nao toca esse pixel (fica como esta na tela).
        bool cor(int x, int y, Rgb& out) const;

        // Retangulo que contem todos os pixels tocados (inclusivo).
        int x0() const { return x0_; }
        int y0() const { return y0_; }
        int x1() const { return x1_; }
        int y1() const { return y1_; }
        // Raio (inteiro) do miolo que e so cor de fundo: quem desenha pode preencher com um circulo cheio e
        // pular esses pixels.
        int raioMiolo() const { return raioMiolo_; }

        // Pinta todos os pixels tocados (fora do miolo, se `pularMiolo`). `put(x, y, rgb565)`.
        template <typename Put>
        void pintar(Put&& put, bool pularMiolo) const {
            const float lim2 = pularMiolo ? float(raioMiolo_) * float(raioMiolo_) : -1.0f;
            Rgb c;
            for (int y = y0_; y <= y1_; ++y) {
                for (int x = x0_; x <= x1_; ++x) {
                    const float dx = float(x - cx_), dy = float(y - cy_);
                    if (dx * dx + dy * dy < lim2) continue;
                    if (cor(x, y, c)) put(x, y, para565(c, x, y));
                }
            }
        }

        static uint16_t para565(const Rgb& c);                 // sem pontilhado
        static uint16_t para565(const Rgb& c, int x, int y);   // com pontilhado ordenado (posicao do pixel)
        static Rgb grade(const Rgb& c);                        // cor alinhada a grade do RGB565

    private:
        struct Arco {
            float tFim = 0;          // fracao da volta coberta (>= 1 = volta inteira)
            Rgb a0, a1;
            float kMul = 1;          // cor = mistura(a0, a1, t * kMul)
            Rgb a1Fim;               // cor da ponta final
            bool pontas = true;
            float sx = 0, sy = 0;    // centro da ponta inicial (relativo ao centro)
            float ex = 0, ey = 0;    // centro da ponta final
        };
        void montarArco(Arco& a, float tFim, Rgb a0, Rgb a1, float kMul, bool pontas) const;

        int cx_, cy_;
        float ro_, ri_, hw_, raio_;
        Rgb fundo_, trilho_;
        Arco arcos_[2];
        int nArcos_ = 0;
        bool temSombra_ = false;
        float sombraX_ = 0, sombraY_ = 0, sombraR_ = 0;
        Rgb sombraCor_;
        int sombraAntesDoArco_ = 0;   // indice do arco antes do qual a sombra entra
        int x0_, y0_, x1_, y1_, raioMiolo_;
    };

} // namespace ui::anel
