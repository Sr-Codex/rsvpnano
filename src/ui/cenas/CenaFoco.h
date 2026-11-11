#pragma once

// Modo Foco, cenas QG (etapa 1): desenha uma cena em tela cheia (640x172) no lugar da batalha escurecida.
// O que mostrar vem de tempo::cenafoco (logica pura); a arte, de CenasFoco.generated.* (tools/gerar_cenas_foco.py).
//
//   preparar(cena, fundo)  decodifica o JPEG do fundo (JPEGDEC, RGB8888) direto para um buffer RGB565 640x172 na PSRAM,
//                          com pontilhado de ruido azul por canal na conversao 888 -> 565 (o mesmo dos mockups). So
//                          decodifica quando a cena ou o fundo mudam. Nada na RAM interna (a RAM interna e do TLS).
//   desenhar(ui, quadro)   3 regioes opacas do ui::Context, cada uma so repinta quando a assinatura muda:
//                          tela inteira = fundo (so quando o fundo muda) · revelacao (fundo + linhas do sprite + cursor)
//                          · mapa (fundo + elos e nos com borda suavizada, brilho em volta, anel de "conectando").
//   liberar()              devolve a PSRAM (sair do Modo Foco / da batalha).
// Tempos (decodificacao e composicao) vao para o log em nivel debug (tag "cena").

#include <cstddef>
#include <cstdint>

#include "tempo/CenaFocoEstado.h"
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
        void desenhar(ui::Context& ui, const tempo::cenafoco::Quadro& quadro);
        void liberar();
        bool pronta(const Cena& cena, tempo::cenafoco::Fundo fundo) const {
            return fundo_ != nullptr && cena_ == &cena && fundoAtual_ == fundo && valido_;
        }

    private:
        bool alocar(const Cena& cena);
        bool decodificar(const uint8_t* jpeg, size_t tamanho);
        void comporRevelacao(const tempo::cenafoco::Quadro& q, int w, int h);
        void comporMapa(const tempo::cenafoco::Quadro& q, int w, int h);

        uint16_t* fundo_ = nullptr;      // 640x172 RGB565 (PSRAM)
        uint16_t* patchMapa_ = nullptr;  // retangulo do mapa no fundo NOITE (base do mapa no amanhecer)
        uint16_t* trabalho_ = nullptr;   // composicao de uma regiao (RGB565)
        float* rgb_ = nullptr;           // composicao do mapa em RGB 0..1 (3 floats por pixel)
        uint8_t* tocado_ = nullptr;      // pixels do mapa que mudaram (o resto fica exatamente como o fundo)
        size_t capTrabalho_ = 0, capMapa_ = 0;
        const Cena* cena_ = nullptr;
        const Cena* patchDe_ = nullptr;
        tempo::cenafoco::Fundo fundoAtual_ = tempo::cenafoco::Fundo::Noite;
        bool valido_ = false;
        uint32_t geracao_ = 0;           // muda a cada fundo decodificado (forca repintar as 3 regioes)
    };

} // namespace ui::cenas
