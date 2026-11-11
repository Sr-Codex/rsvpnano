#pragma once

// Cronometro da batalha do Tempo (foco / distracao / pausa). C++ puro (testavel no host).
// Regras (economia.md, decisoes 18 e 19):
//   - PAUSAR = interrupcao legitima: o relogio PARA e o inimigo NAO se cura (avancar com correndo = false nao muda nada).
//   - DISTRAI = desvio: o relogio segue (conta como distracao) e cada minuto cura 1 minuto de HP (ate o plano).
//   - 1 distracao de ate 1 min (kDistracaoToleradaSeg) por batalha NAO perde o bonus de vitoria perfeita.

#include <cstdint>

#include "tempo/Tipos.h"

namespace tempo {

    // Estado do bonus de vitoria perfeita (+25 %) durante a batalha.
    enum class BonusPerfeito : uint8_t {
        Indisponivel,   // plano < 15 min: nao ha bonus
        Disponivel,     // nenhuma distracao ate agora
        Mantido,        // 1 distracao curta (<= 1 min) ja encerrada: o bonus segue valendo
        EmRisco,        // distraido agora, 1a distracao ainda dentro de 1 min: voltar ate 1 min mantem o bonus
        Perdido,        // 2a distracao ou distracao acima de 1 min
    };

    struct Cronometro {
        int planejadoMin = 25;
        uint64_t focadoMs = 0;
        uint64_t distraidoMs = 0;
        uint64_t distracaoAtualMs = 0;
        uint64_t comboMs = 0;              // foco continuo sem distracao
        int numDistracoes = 0;
        int maiorDistracaoMin = 0;
        bool distraido = false;

        void zerar(int planejado);
        // Soma `deltaMs`. Parado (pausa, descanso, reflexao) = correndo false: NADA muda (o inimigo nao se cura).
        void avancar(uint64_t deltaMs, bool correndo);
        void distrair();                   // "Distrai": abre uma distracao (ignorado se ja distraido)
        void voltar();                     // "Voltei": fecha a distracao
        void fecharDistracao() { voltar(); }

        int64_t planoMs() const { return int64_t(planejadoMin) * 60000; }
        int64_t restanteMs() const;        // vida restante do inimigo
        int64_t overtimeMs() const;        // foco alem do necessario para zerar o inimigo
        int pontosDeVida() const;          // 0..100
        int distraidoSeg() const { return static_cast<int>(distraidoMs / 1000); }
        BonusPerfeito bonus() const;
        // Entrada do motor. `ateAgora` = overtime (minutos focados reais); senao estima o "ao terminar" (zera o inimigo).
        EntradaBatalha entrada(Categoria categoria, bool ateAgora) const;
    };

} // namespace tempo
