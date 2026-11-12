#pragma once

// Modo Foco, cenas QG: desenha uma cena em tela cheia (640x172) no lugar da batalha escurecida.
// O que mostrar vem de tempo::cenafoco (progresso: revelacao, mapa, fundo) e tempo::cenaviva (v2: animacoes e relogio);
// a arte, de CenasFoco.generated.* (tools/gerar_cenas_foco.py).
//
//   preparar(cena, fundo)  decodifica o JPEG do fundo (JPEGDEC, RGB8888) direto para um buffer RGB565 640x172 na PSRAM,
//                          com pontilhado de ruido azul por canal na conversao 888 -> 565 (o mesmo dos mockups). So
//                          decodifica quando a cena ou o fundo mudam. Nada na RAM interna (a RAM interna e do TLS).
//   desenhar(ui, ...)      compositor por camadas (v2), tudo na PSRAM:
//                            fundo_  = JPEG decodificado (nunca muda)
//                            base_   = fundo + revelacao + mapa (recompostos no maximo ~1x/s, como no v1)
//                            camadas = luz viva por cima da base, cada uma com a sua caixa e a sua assinatura por quadro:
//                                      QG Neon: janela (chuva, janelas acendendo, drone) · relogio de neon + barra ·
//                                      roteador · rack · olhos do gato · teclado RGB · faixa do gabinete · vapor
//                                      QG Cidade: janela (gotas, carros, antenas, letreiros) · relogio de LED · fumaca
//                          A cada quadro (8/s) so as caixas que mudaram sao recompostas (base + TODAS as camadas que
//                          cruzam a caixa, na ordem) e enviadas a tela. Luz por soma "screen" (como os brilhos dos
//                          mockups): nucleo com borda suavizada (cobertura 4x4 / area exata) + halo gaussiano curto; o
//                          relogio ganha brilho curto e largo de verdade (desfoque da propria mascara). A janela usa a
//                          visibilidade da moldura/persiana (as gotas e luzes da cidade ficam atras das laminas).
//                          Pontilhado: base em 565 exato -> float (r = R5*8) -> luz -> pontilhado de ruido azul; pixel
//                          sem luz volta identico.
//   liberar()              devolve a PSRAM (sair do Modo Foco / da batalha).
// Tempos (decodificacao e composicao por quadro) vao para o log em nivel debug (tag "cena").

#include <cstddef>
#include <cstdint>

#include "tempo/CenaFocoEstado.h"
#include "tempo/CenaViva.h"
#include "ui/cenas/CenasFoco.generated.h"

namespace ui {
    class Context;
}

namespace ui::cenas {

    class CenaFoco {
    public:
        static constexpr int kLarg = 640, kAlt = 172;

        CenaFoco() = default;
        ~CenaFoco();
        CenaFoco(const CenaFoco&) = delete;
        CenaFoco& operator=(const CenaFoco&) = delete;

        // Deixa o fundo pronto na PSRAM. false = sem memoria ou JPEG invalido (o chamador volta a so escurecer).
        bool preparar(const Cena& cena, tempo::cenafoco::Fundo fundo);
        // Desenha o quadro (depois de preparar() com o mesmo fundo). Chamar entre beginFrame/endFrame.
        // `relogio` = o relogio da cena (tempo::cenaviva::relogio); `ms` = relogio das animacoes (millis).
        void desenhar(ui::Context& ui, const tempo::cenafoco::Quadro& quadro, const tempo::cenaviva::Relogio& relogio,
                      uint32_t ms);
        void liberar();
        bool pronta(const Cena& cena, tempo::cenafoco::Fundo fundo) const {
            return fundo_ != nullptr && cena_ == &cena && fundoAtual_ == fundo && valido_;
        }
        // Tempo (us) da ultima composicao que pintou alguma coisa e quantos pixels foram enviados (log/diagnostico).
        uint32_t ultimoQuadroUs() const { return ultimoQuadroUs_; }

        enum Camada : uint8_t { Janela, Relogio, Roteador, Rack, Olhos, Teclado, FaixaGabinete, Sprite, kCamadas };
        struct Instante;   // estado do quadro (interno)
        struct Tile;       // faixa de composicao (interno)

    private:
        bool alocar(const Cena& cena);
        bool decodificar(const uint8_t* jpeg, size_t tamanho);
        void comporRevelacao(const tempo::cenafoco::Quadro& q, int w, int h);
        void comporMapa(const tempo::cenafoco::Quadro& q, int w, int h);
        void copiarParaBase(const Retangulo& r);
        void prepararJanela();
        void prepararRelogio(const Instante& in);
        Retangulo caixa(int camada) const;
        uint32_t assinatura(int camada, const Instante& in) const;
        void aplicar(int camada, Tile& t, const Instante& in) const;
        void comporEPintar(ui::Context& ui, Retangulo r, const Instante& in);

        uint16_t* fundo_ = nullptr;      // 640x172 RGB565 (PSRAM): JPEG decodificado
        uint16_t* base_ = nullptr;       // 640x172 RGB565 (PSRAM): fundo + revelacao + mapa
        uint16_t* patchMapa_ = nullptr;  // retangulo do mapa no fundo NOITE (base do mapa no amanhecer)
        uint16_t* trabalho_ = nullptr;   // composicao da revelacao / do mapa (RGB565)
        float* rgb_ = nullptr;           // composicao do mapa em RGB 0..1 (3 floats por pixel)
        uint8_t* tocado_ = nullptr;      // pixels do mapa que mudaram (o resto fica exatamente como o fundo)
        size_t capTrabalho_ = 0, capMapa_ = 0, capJanela_ = 0, capRelogio_ = 0;
        // compositor das camadas (faixas de linhas de ate kCapTile pixels)
        float* tileRgb_ = nullptr;       // 0..255 por canal
        uint8_t* tileTocado_ = nullptr;
        uint16_t* tileSaida_ = nullptr;
        uint8_t* janelaVis_ = nullptr;   // visibilidade (0..255) de cada pixel da janela: 255 = vidro, 0 = moldura/lamina
        float* relMascara_ = nullptr;    // relogio: luz direta (0..1), brilho curto e brilho largo, na caixa `brilho`
        float* relCurto_ = nullptr;
        float* relLargo_ = nullptr;
        float* relTemp_ = nullptr;
        const Cena* cena_ = nullptr;
        const Cena* patchDe_ = nullptr;
        const Cena* janelaDe_ = nullptr;
        tempo::cenafoco::Fundo fundoAtual_ = tempo::cenafoco::Fundo::Noite;
        bool valido_ = false;
        uint32_t geracao_ = 0;           // muda a cada fundo decodificado (forca repintar tudo)
        uint32_t sigRevela_ = 0, sigMapa_ = 0, sigRelogioPronto_ = 0;
        uint32_t sigCamada_[kCamadas] = {};
        bool basePronta_ = false;
        uint32_t ultimoQuadroUs_ = 0;
    };

} // namespace ui::cenas
