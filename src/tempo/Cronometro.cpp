#include "tempo/Cronometro.h"

#include <algorithm>

#include "tempo/Parametros.h"

namespace tempo {

    void Cronometro::zerar(int planejado) {
        *this = Cronometro{};
        planejadoMin = planejado;
    }

    void Cronometro::avancar(uint64_t deltaMs, bool correndo) {
        if (!correndo) return;
        if (distraido) {
            distraidoMs += deltaMs;
            distracaoAtualMs += deltaMs;
        } else {
            focadoMs += deltaMs;
            comboMs += deltaMs;
        }
    }

    void Cronometro::distrair() {
        if (distraido) return;
        distraido = true;
        distracaoAtualMs = 0;
        comboMs = 0;
        ++numDistracoes;
    }

    void Cronometro::voltar() {
        if (!distraido) return;
        distraido = false;
        maiorDistracaoMin = std::max(maiorDistracaoMin, static_cast<int>(distracaoAtualMs / 60000));
    }

    int64_t Cronometro::restanteMs() const {
        const int64_t cura = std::min<int64_t>(int64_t(distraidoMs), planoMs());
        return std::max<int64_t>(0, planoMs() + cura - int64_t(focadoMs));
    }

    int64_t Cronometro::overtimeMs() const {
        const int64_t cura = std::min<int64_t>(int64_t(distraidoMs), planoMs());
        return std::max<int64_t>(0, int64_t(focadoMs) - (planoMs() + cura));
    }

    int Cronometro::pontosDeVida() const {
        return static_cast<int>(std::min<int64_t>(100, restanteMs() * 100 / std::max<int64_t>(1, planoMs())));
    }

    BonusPerfeito Cronometro::bonus() const {
        if (planejadoMin < param::kDuracaoMinBonusPerfeita) return BonusPerfeito::Indisponivel;
        if (numDistracoes == 0) return BonusPerfeito::Disponivel;
        if (numDistracoes >= 2 || distraidoSeg() > param::kDistracaoToleradaSeg) return BonusPerfeito::Perdido;
        return distraido ? BonusPerfeito::EmRisco : BonusPerfeito::Mantido;
    }

    EntradaBatalha Cronometro::entrada(Categoria categoria, bool ateAgora) const {
        EntradaBatalha e;
        e.categoria = categoria;
        e.minutosPlanejados = planejadoMin;
        e.minutosDistraidos = static_cast<int>(distraidoMs / 60000);
        e.segundosDistraidos = distraidoSeg();
        e.numDistracoes = numDistracoes;
        e.maiorDistracaoMin = std::max(maiorDistracaoMin, static_cast<int>(distracaoAtualMs / 60000));
        e.minutosFocados = ateAgora ? static_cast<int>(focadoMs / 60000)
                                    : planejadoMin + std::min(e.minutosDistraidos, planejadoMin);
        return e;
    }

} // namespace tempo
