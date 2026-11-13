#pragma once

// Modo Foco, cenas QG (etapa 1): o que a cena mostra em cada instante. C++ puro, sem hardware nem arte: o renderizador
// (src/ui/cenas/CenaFoco) so copia/pinta o que este modulo decide. Testes em test/test_cena_foco.
//
//   focando     fundo NOITE (telas dinamicas vazias) + revelacao (tela que enche) + mapa (nos acendendo)
//   100 %       fundo VITORIA por kVitoriaMs, so se a cena estava na tela quando o inimigo zerou (arte pronta, sem mapa)
//   hora extra  fundo AMANHECER (telas cheias, arte pronta) + mapa com todos os nos desta sessao acesos
//
// Nos do mapa (12, na ordem `ordem` da arte):
//   novos  = clamp(round(minutos planejados / 5), 1, 12)      (1 no a cada ~5 min desta sessao)
//   antes  = min(minutos de foco pagos HOJE antes desta sessao / 5, 12 - novos)   (sessoes anteriores do dia)
//   no j desta sessao (0..novos-1): g = clamp(p * novos - j, 0, 1)   (0 = apagado, 1 = aceso)
//   tomados (k) = nos desta sessao ja completos (g = 1)

#include <array>
#include <cstdint>

namespace tempo::cenafoco {

    inline constexpr int kNos = 12;
    inline constexpr int kMinutosPorNo = 5;
    inline constexpr uint32_t kVitoriaMs = 10000;   // vitoria na tela antes do amanhecer
    inline constexpr float kCheioMinimo = 0.12f;    // QG Neon: a tela ja comeca com um pouco de codigo

    enum class Fundo : uint8_t { Noite, Vitoria, Amanhecer };
    enum class TipoNo : uint8_t { Apagado, Antes, Novo };

    struct No {
        TipoNo tipo = TipoNo::Apagado;
        float g = 0.0f;   // 0..1 (Antes = 1)
    };

    struct Entrada {
        double p = 0.0;                 // foco / planejado (0..1)
        bool horaExtra = false;         // inimigo zerado, seguindo no bonus
        bool vitoria = false;           // janela da vitoria (Roteiro::vitoria)
        int minutosPlanejados = 25;
        int minutosFocoAntes = 0;       // foco pago hoje antes desta sessao (tempo::Dia::fochoPagoMin)
    };

    struct Quadro {
        Fundo fundo = Fundo::Noite;
        int novos = 1, antes = 0;
        int tomados = 0;                // k
        float cheio = kCheioMinimo;     // QG Neon: fracao da tela de codigo
        bool revelar = true;            // so no fundo noite (vitoria/amanhecer ja tem as telas cheias na arte)
        bool mapa = true;               // desenhar o mapa por cima (vitoria = arte pronta)
        std::array<No, kNos> nos{};     // por indice do no (nao pela ordem)
    };

    int nosNovos(int minutosPlanejados);
    int nosAntes(int minutosFocoAntes, int novos);
    // `ordem[i]` = indice do i-esimo no a acender.
    Quadro calcular(const Entrada& e, const uint8_t (&ordem)[kNos]);

    // ---- Revelacao (retangulo `alt` linhas de altura, coordenadas relativas ao topo do retangulo)
    // QG Neon (modo 0): primeiras round(alt * cheio) linhas do sprite.
    int linhasModo0(int alt, float cheio);
    // QG Cidade (modo 1): cabecalho + 1 linha de terminal por no tomado (no maximo linhasMax). Linha i comeca em
    // y0x10/10 + i * passox10/10; copia ate o comeco da linha seguinte (o fim da ultima linha mostrada).
    int linhasModo1(int alt, int y0x10, int passox10, int cabecalho, int linhasMax, int tomados);
    // Cursor (modo 1) na linha seguinte a ultima mostrada: topo em 1/10 de px (relativo). Com k = 0 fica logo depois do
    // cabecalho: a arte do fundo noite vem SEM cursor (a placa desenha e faz piscar).
    int cursorModo1Topox10(int y0x10, int passox10, int cabecalho, int linhasMax, int tomados);

    // ---- Vitoria: a cena estava na tela quando a batalha passou de focando para hora extra -> kVitoriaMs de vitoria.
    // Qualquer despertar (cena saiu da tela) encerra a vitoria; quem volta depois ve o amanhecer.
    class Roteiro {
    public:
        // Uma vez por volta do laco: `horaExtra` = fase atual; `cenaNaTela` = a cena foi desenhada no ultimo quadro.
        void atualizar(bool focando, bool horaExtra, bool cenaNaTela, uint32_t nowMs);
        bool vitoria(uint32_t nowMs) const;
        void reiniciar() { *this = Roteiro{}; }

    private:
        bool focandoAntes_ = false;
        bool ativa_ = false;
        uint32_t inicioMs_ = 0;
    };

} // namespace tempo::cenafoco
