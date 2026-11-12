#pragma once

// Modo Foco v2 (cenas QG vivas + tempo visivel na cena): o que muda em cada instante. C++ puro, sem hardware nem arte;
// o renderizador (src/ui/cenas/CenaFoco) so desenha o que estas funcoes decidem. Testes em test/test_cena_viva.
//
// Tempo: a cena viva anda a kQuadroMs (8 quadros/s). "fase" = a unidade de tempo dos mockups (tools/mockup_qg*.py): os
// GIFs aprovados tem 24 quadros = 3 s = 1 fase, entao velocidades e periodos vem de la (gotas em px por fase, teclado em
// seno de 2*pi*fase, carros que cruzam a janela em 1 fase...). Tudo e funcao do relogio (ms) -> mesma entrada, mesmo
// quadro (deterministico), sem estado escondido.
//
// Relogio da cena (B):  foco  -> tempo RESTANTE "MM:SS" (H:MM:SS a partir de 1 h), barra = foco / planejado
//                        vitoria -> "00:00", barra cheia        hora extra -> "+M:SS", barra cheia
//                        livre      -> tempo DECORRIDO, barra = posicao dentro da hora atual

#include <array>
#include <cstdint>

namespace tempo::cenaviva {

    inline constexpr uint32_t kQuadroMs = 125;     // 8 quadros/s
    inline constexpr uint32_t kFaseMs = 3000;      // 1 fase dos mockups = 3 s (24 quadros)
    inline constexpr int kQuadrosSprite = 24;      // vapor/fumaca pre-calculados: 1 fase

    uint32_t quadro(uint32_t ms);
    // Mistura inteira deterministica (sementes das animacoes) e numero 0..1 a partir dela.
    uint32_t misturar(uint32_t a, uint32_t b = 0, uint32_t c = 0);
    float aleatorio(uint32_t a, uint32_t b = 0, uint32_t c = 0);

    // ---- comum
    // Posicao (0..alt) de uma gota que comeca em u e desce `vel` px por fase (volta ao topo ao passar de alt).
    float gotaY(float u, float vel, uint32_t ms, float alt);
    int quadroSprite(uint32_t ms);                 // 0..kQuadrosSprite-1 (vapor/fumaca)

    // ---- QG Neon
    bool neonTreme(uint32_t quadro);               // o tubo do relogio de neon falha neste quadro (de vez em quando)
    bool gatoPisca(uint32_t quadro);               // olhos do gato fechados neste quadro
    bool roteadorAceso(int led, uint32_t ms);      // 1 dos 4 apagado, girando a cada 1/8 de fase (375 ms)
    bool rackAceso(int fila, int led, uint32_t ms, float probAceso);   // sorteio novo a cada 500 ms
    float teclaMistura(int tecla, uint32_t ms);    // 0..1 entre as duas cores do teclado (onda RGB)
    float faixaMistura(int led, uint32_t ms);      // 0..1 (faixa RGB do gabinete)
    bool janelaAcesa(int janela, uint32_t ms);     // janelas apagadas da cidade que acendem e apagam (~1 em 4)
    struct Drone {
        bool visivel = false;
        float x = 0, y = 0;
        bool vermelhoAceso = false;
    };
    inline constexpr uint32_t kDronePeriodoMs = 12000;   // passa 1 vez a cada 12 s ...
    // ... levando 1 fase (3 s) para ir de x0 - entra ate x1 + (vao - entra), como no mockup; so aparece dentro da janela.
    Drone drone(uint32_t ms, float x0, float x1, float yBase, float amp, float vao, float entra);

    // ---- QG Cidade
    bool antenaAcesa(int camada, uint32_t ms);     // alterna a cada 1/4 de fase (750 ms), camadas defasadas
    float letreiroForca(int letreiro, uint32_t ms);   // 0..1 (1 = normal): respira de leve e as vezes falha
    // Carro voador: cruza a janela [x0, x1) em 1 fase (sentido +1 = para a direita), comecando em fase0 (0..1).
    float carroVivaX(float fase0, int sentido, uint32_t ms, float x0, float x1);
    // Carro ao longe: x relativo a janela = mod(fase0 + vel * fase, largura + 40) - 20 (pode estar fora da janela).
    float carroLongeX(float fase0, float vel, uint32_t ms, float largura);
    bool doisPontosAcesos(uint32_t ms);            // ":" do relogio de LED pisca 1x/s

    // ---- Relogio da cena
    enum class EstadoRelogio : uint8_t { Foco, Vitoria, HoraExtra, Livre };
    struct EntradaRelogio {
        bool livre = false;            // sessao livre (conta para cima)
        bool horaExtra = false;        // inimigo zerado
        bool vitoria = false;          // janela da vitoria (fundo VITORIA)
        int64_t restanteMs = 0;        // vida restante do inimigo
        int64_t overtimeMs = 0;        // foco alem do planejado
        uint64_t focadoMs = 0;
        int64_t planoMs = 0;           // planejado (ms)
    };
    struct Relogio {
        std::array<char, 12> texto{};  // terminado em '\0'
        EstadoRelogio estado = EstadoRelogio::Foco;
        float fracao = 0.0f;           // trecho aceso da barra (0..1)
    };
    Relogio relogio(const EntradaRelogio& e);
    // "MM:SS" ate 59:59, "H:MM:SS" depois; com mais = true, "+M:SS" / "+H:MM:SS".
    void formatar(int64_t segundos, bool mais, std::array<char, 12>& saida);

    // ---- Digitos de 7 segmentos (relogio de LED do QG Cidade): bit 0..6 = a (cima), b (cima dir.), c (baixo dir.),
    // d (baixo), e (baixo esq.), f (cima esq.), g (meio). Fora de '0'..'9' = 0.
    uint8_t segmentos(char c);

} // namespace tempo::cenaviva
