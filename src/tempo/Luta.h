#pragma once

// Batalha em "cabo de guerra" (etapa c do Tempo; mockup docs/app-tempo/mockups/tempo-batalha/tempo-luta-B-estados@2x.png, opcao B
// escolhida pelo dono). Logica PURA (sem tela, testavel no host): onde fica a frente de choque, qual aparencia cada estado
// tem, as fases da animacao (golpe final), as particulas de po e quando a animacao deve parar.
// O desenho (composicao dos pixels) fica em ui/luta/Faixa.*.

#include <cstdint>

#include "tempo/Cronometro.h"

namespace tempo::luta {

    // ------------------------------------------------------------------ tempo da animacao
    inline constexpr uint32_t kQuadroMs = 90;         // ~11 quadros/s (o dono pediu 10-12)
    inline constexpr uint32_t kGolpeFinalMs = 1500;   // o inimigo vira po; depois segue sozinho para a hora extra
    inline constexpr uint32_t kPeriodoPulsoQuadros = 10;   // o brilho da frente "respira" a cada ~0,9 s

    constexpr uint32_t indiceDoQuadro(uint32_t nowMs) { return nowMs / kQuadroMs; }

    // ------------------------------------------------------------------ posicao da frente
    // Parte "sua" da luta: foco / (foco + vida restante do inimigo). Sem distracao e igual ao progresso (foco/plano); a cura do
    // inimigo aumenta a vida restante e EMPURRA a frente de volta; inimigo zerado (ou hora extra) = 1.
    double frente(const Cronometro& c);
    // Hora extra: fracao (0..1) de volta do tamanho do plano; ao completar a volta recomeca (sem teto, decisao 24).
    double voltaExtra(const Cronometro& c);

    // ------------------------------------------------------------------ estado -> aparencia
    enum class Modo : uint8_t { Focando, FimSegurando, Distraido, Pausado, PausaCiclo, GolpeFinal, HoraExtra };

    struct EntradaModo {
        bool descanso = false;      // pausa do ciclo 4x
        bool pausada = false;       // "Pausar"
        bool distraido = false;     // "Distrai" (so conta se nao estiver pausada)
        bool horaExtra = false;     // inimigo zerado, seguindo no bonus
        bool golpeFinal = false;    // dentro dos kGolpeFinalMs apos zerar (so vale com horaExtra)
        bool fimSegurando = false;  // "Fim · segure" em andamento
    };
    Modo modoDe(const EntradaModo& e);

    // Efeitos ligados em cada modo (o renderizador so le isto; cores e geometria ficam la).
    struct Efeitos {
        float apagado = 0.0f;         // 0..1: quanto as cores da barra se misturam com o fundo
        bool choque = false;          // linha branco-quente + clarao na frente
        bool faiscasInimigo = false;  // faiscas para o lado do inimigo (direita)
        bool raios = false;           // pequenos raios rachando a parte carmim
        bool faiscasRecuo = false;    // faiscas para a esquerda (o inimigo empurra)
        bool setas = false;           // ‹‹‹ carmim avancando sobre o laranja
        bool divisoria = false;       // divisoria fria (tregua)
        bool poeira = false;          // o ultimo pedaco do inimigo vira po
        bool azul = false;            // hora extra: a parte extra entra em azul pela esquerda
        bool verdeAgua = false;       // pausa do ciclo: barra verde-agua esvaziando
        bool frenteCarmim = false;    // distraido: clarao carmim na frente (em vez de branco-quente)
        bool animada = false;         // tem alguma coisa se mexendo (faiscas, raios, setas, po)
    };
    Efeitos efeitos(Modo m);

    // ------------------------------------------------------------------ golpe final
    // Progresso do po em "unidades do mockup": 0 = inteiro; a ultima particula some em kProgFrente + kRuidoLiberacao + kVidaParticula = 1,2. kGolpeFinalMs
    // corresponde a kProgGolpe (1,25), logo no fim todas ja sumiram.
    inline constexpr float kProgGolpe = 1.25f;
    inline constexpr float kProgFrente = 0.60f;   // ate aqui a frente de liberacao percorre o pedaco (resto = ruido + vida)
    inline constexpr float kRuidoLiberacao = 0.10f;
    inline constexpr float kVidaParticula = 0.50f;

    constexpr bool golpeFinalAtivo(uint32_t msDesdeZerar) { return msDesdeZerar < kGolpeFinalMs; }
    // Progresso do po para `msDesdeZerar` (limitado: depois do fim fica em kProgGolpe).
    float progressoGolpe(uint32_t msDesdeZerar);
    // Posicao (0..1) da frente de liberacao dentro do pedaco: a frente de choque do golpe final anda por ai.
    float frenteDoGolpe(float prog);

    // ------------------------------------------------------------------ particulas (po)
    // Gerador pseudo-aleatorio proprio (xorshift32): a mesma semente da as mesmas particulas em qualquer plataforma.
    class Rng {
    public:
        explicit Rng(uint32_t semente = 1) : s_(semente != 0 ? semente : 0x9E3779B9u) {}
        uint32_t proximo() {
            s_ ^= s_ << 13;
            s_ ^= s_ >> 17;
            s_ ^= s_ << 5;
            return s_;
        }
        float unidade() { return float(proximo() >> 8) * (1.0f / 16777216.0f); }       // [0, 1)
        float entre(float a, float b) { return a + (b - a) * unidade(); }

    private:
        uint32_t s_;
    };

    // Onde a particula nasce: posicao no pedaco, f = 0..1 (esquerda -> direita; quando a frente passa por f ela se solta) e cor.
    struct Semente {
        float x = 0, y = 0, f = 0;
        uint8_t r = 0, g = 0, b = 0;
    };
    // Vento do po (px por unidade de progresso): sobe e vai um pouco para a direita.
    struct Vento {
        float vx0 = -10, vx1 = 40, vy0 = 30, vy1 = 100;
    };
    struct Particula {
        float x = 0, y = 0;        // origem
        float t0 = 0;              // progresso em que se solta
        float vx = 0, vy = 0;      // velocidades (px / unidade de progresso)
        uint8_t r = 0, g = 0, b = 0;
    };
    struct PontoPo {
        float x = 0, y = 0, a = 0;   // posicao e opacidade (0..1)
        uint8_t r = 0, g = 0, b = 0; // cor ja esquentada/esfriada
        bool solta = false;          // false = ainda parada no lugar de origem
    };
    Particula criarParticula(const Semente& s, Rng& rng, const Vento& vento = Vento{});
    // Posicao, opacidade e cor da particula no progresso `prog`. a chega a 0 quando idade >= kVidaParticula.
    PontoPo estadoParticula(const Particula& p, float prog);

    // ------------------------------------------------------------------ quando animar
    struct ContextoAnimacao {
        bool telaBatalha = true;     // a tela da batalha esta na frente
        bool escurecida = false;     // Modo Foco: tela escurecendo/escura
        bool cenaNaTela = false;     // Modo Foco: cena QG em tela cheia
        bool emPe = false;           // placa em pe (IMU)
    };
    // A animacao so corre com a batalha na frente, tela clara e placa deitada. Parada = quadro de repouso (sem faisca no meio).
    bool deveAnimar(const ContextoAnimacao& c);

    // Brilho "respirando" 0,5..1 (0,75 +- 0,25, como o mockup; quadro de repouso = 1).
    float pulso(uint32_t quadro, bool animar);

} // namespace tempo::luta
