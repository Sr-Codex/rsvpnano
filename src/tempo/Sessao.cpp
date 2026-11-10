#include "tempo/Sessao.h"

namespace tempo {

    Ciclo iniciarCiclo() {
        Ciclo c;
        c.ativo = true;
        return c;
    }

    Ciclo avancarCiclo(const Ciclo& c) {
        if (!c.ativo) return c;
        Ciclo n = c;
        switch (c.passo) {
        case PassoCiclo::Foco:
            n.passo = c.rodada < kCicloRodadas ? PassoCiclo::PausaCurta : PassoCiclo::PausaLonga;
            break;
        case PassoCiclo::PausaCurta:
            n.passo = PassoCiclo::Foco;
            n.rodada = c.rodada + 1;
            break;
        case PassoCiclo::PausaLonga:
            n.ativo = false;
            break;
        }
        return n;
    }

    int duracaoCicloMin(const Ciclo& c) {
        switch (c.passo) {
        case PassoCiclo::PausaCurta: return kCicloPausaMin;
        case PassoCiclo::PausaLonga: return kCicloPausaLongaMin;
        default: return kCicloFocoMin;
        }
    }

    namespace {
        constexpr Categoria kChips[kNumChips] = {Categoria::Trabalho, Categoria::Estudo, Categoria::Leitura,
                                                 Categoria::Meditacao, Categoria::Projetos};
    }

    Categoria categoriaDoChip(int chip) {
        return kChips[(chip >= 0 && chip < int(kNumChips)) ? chip : 0];
    }

    int chipDaCategoria(Categoria c) {
        for (int i = 0; i < int(kNumChips); ++i)
            if (kChips[i] == c) return i;
        return 0;
    }

    const char* nomeCategoria(Categoria c) {
        switch (c) {
        case Categoria::Trabalho: return "Trabalho";
        case Categoria::Estudo: return "Estudo";
        case Categoria::Leitura: return "Leitura";
        case Categoria::Meditacao: return "Meditação";
        default: return "Projetos";
        }
    }

    std::string nomeFocoLivre(Categoria c) {
        return std::string{"Foco livre · "} + nomeCategoria(c);
    }

} // namespace tempo
