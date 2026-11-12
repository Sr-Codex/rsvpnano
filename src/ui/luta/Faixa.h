#pragma once

// Faixa da batalha em cabo de guerra (etapa c do Tempo): compoe os pixels da regiao animada (rotulos VOCE x INIMIGO + a barra
// com a frente de choque, faiscas, raios, setas, hora extra azul e o po do golpe final) num buffer RGB e entrega RGB565 com
// pontilhado ordenado (o mesmo do anel suave). Sem Arduino_GFX e sem alocacao propria: quem chama entrega a memoria
// (PSRAM no aparelho, vetores nos testes), o que deixa tudo testavel no host. Mockup: tools/mockup_tempo_luta.py (opcao_b,
// b_estado, derrota_b, desintegrar). Pixels do alvo: x 0..468, y 98..152 da tela de 640x172 (a faixa e a unica parte que
// anima; o resto da tela redesenha como sempre).

#include <cstdint>
#include <string>

#include "tempo/Luta.h"
#include "ui/icons/PhosphorIcons.generated.h"

namespace ui::luta {

    inline constexpr int kLarg = 468, kAlt = 54;       // regiao animada
    inline constexpr int kTelaX = 0, kTelaY = 98;      // onde ela fica na tela
    inline constexpr int kBarraX0 = 8, kBarraX1 = 460; // barra (x), em coordenadas da regiao
    inline constexpr int kBarraY0 = 20, kBarraY1 = 44; // 24 px de altura (tela: y 118..142)
    inline constexpr int kBarraLarg = kBarraX1 - kBarraX0, kBarraAlt = kBarraY1 - kBarraY0;
    inline constexpr int kPedacoPx = 120;              // golpe final: largura do ultimo pedaco carmim
    inline constexpr int kCamadaX0 = kBarraX1 - kPedacoPx - 4;   // camada do inimigo (golpe final): x de partida
    inline constexpr int kCamadaLarg = kLarg - kCamadaX0;
    inline constexpr uint32_t kMaxParticulas = 1400;

    struct Cor {
        float r = 0, g = 0, b = 0;
    };

    struct Rotulo {
        std::string texto;
        Cor cor;
        bool temIcone = false;
        icons::phosphor::Id icone = icons::phosphor::Id::Sword;
    };

    // Tudo o que o desenho de UM quadro precisa.
    struct Quadro {
        tempo::luta::Modo modo = tempo::luta::Modo::Focando;
        float frente = 0.4f;        // 0..1 (tempo::luta::frente)
        float extra = 0.0f;         // hora extra: volta 0..1
        float pausa = 1.0f;         // pausa do ciclo: quanto da pausa ainda falta (1 -> 0)
        float pulso = 1.0f;         // 0,75..1
        bool animar = true;         // false = quadro de repouso (sem faiscas/raios/setas)
        bool tregua = false;        // hora extra PAUSADA: a mesma barra, ~50 % apagada, divisoria fria (modo = HoraExtra)
        uint32_t quadro = 0;        // semente das faiscas deste quadro
        float golpe = 0.0f;         // progresso do po (0..tempo::luta::kProgGolpe)
        Cor fundo{0, 0, 0};
        Rotulo esq, dir;
    };

    // Memoria entregue por quem chama (tamanhos em comentario). Nada aqui e liberado pela Faixa.
    struct Memoria {
        uint8_t* rgb = nullptr;                 // kLarg * kAlt * 3
        uint16_t* saida = nullptr;              // kLarg * kAlt
        uint8_t* pilula = nullptr;              // kBarraLarg * kBarraAlt (cobertura do trilho)
        uint8_t* camada = nullptr;              // kCamadaLarg * kAlt * 3 (camada do inimigo, so no golpe final); pode ser nullptr
        tempo::luta::Particula* particulas = nullptr;   // kMaxParticulas (idem)
        uint16_t* blocos = nullptr;             // kMaxParticulas * 2 (x, y do bloco de cada particula) (idem)
    };

    class Faixa {
    public:
        explicit Faixa(const Memoria& m);

        // Compoe um quadro completo; o resultado fica em saida().
        void compor(const Quadro& q);
        const uint16_t* saida() const { return mem_.saida; }

        // Golpe final: renderiza a camada do inimigo (ultimo pedaco carmim + rotulo) e amostra 1 particula por bloco de 2x2 px.
        // Seed fixa -> as mesmas particulas. false = sem memoria para isso.
        bool prepararPoeira(const Rotulo& inimigo, uint32_t semente, const Cor& fundo);
        void descartarPoeira() { nParticulas_ = 0; }
        bool poeiraPronta() const { return nParticulas_ > 0; }
        uint32_t numParticulas() const { return nParticulas_; }
        const tempo::luta::Particula* particulas() const { return mem_.particulas; }

    private:
        void limpar(const Cor& fundo);
        void rotulo(const Rotulo& r, bool aDireita, float apagado, const Cor& fundo);
        void barra(const Quadro& q);
        void luzes(const Quadro& q);
        void poeira(const Quadro& q);
        void converter();

        Memoria mem_;
        uint32_t nParticulas_ = 0;
        bool camadaPronta_ = false;
    };

    // ---- utilitarios expostos para os testes / a camada do inimigo
    float frenteX(float frente);                         // x da frente (regiao) para frente 0..1
    Cor misturarCor(const Cor& a, const Cor& b, float t);
    // Altura (0..) e largura do texto do rotulo na fonte de 15 px (ink), para alinhar a direita.
    int larguraTexto(const std::string& texto, int tamanho);

} // namespace ui::luta
