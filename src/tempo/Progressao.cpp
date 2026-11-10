#include "tempo/Progressao.h"

#include <algorithm>
#include <cmath>

#include "tempo/Parametros.h"

namespace tempo {

    int xpProximo(int nivel) {
        if (nivel < 1) {
            nivel = 1;
        }
        const double n = static_cast<double>(nivel);
        const double bruto = 60.0 * n * std::sqrt(n);  // 60 * n^1,5
        return static_cast<int>(std::floor(bruto / 10.0 + 0.5)) * 10;
    }

    int64_t xpAcumulado(int nivel) {
        int64_t total = 0;
        for (int n = 1; n < nivel; ++n) {
            total += xpProximo(n);
        }
        return total;
    }

    int nivelDeXp(double xp) {
        int nivel = 1;
        int64_t acumulado = 0;
        while (nivel < param::kNivelMax) {
            const int64_t proximo = acumulado + xpProximo(nivel);
            if (xp < static_cast<double>(proximo)) {
                break;
            }
            acumulado = proximo;
            ++nivel;
        }
        return nivel;
    }

    int nivelAtributo(Atributo atributo, double pontos) {
        if (pontos <= 0.0) {
            return 0;
        }
        const double k = param::kK[static_cast<size_t>(atributo)];
        const int nivel = static_cast<int>(std::floor(std::sqrt(pontos / k)));
        return std::min(nivel, param::kNivelAtributoMax);
    }

    int nivelAtributo(const Estado& estado, Atributo atributo) {
        return nivelAtributo(atributo, estado.pontosAtributo[static_cast<size_t>(atributo)]);
    }

    int coracoesMaxMeios(const Estado& estado) {
        int meios = param::kCoracoesBaseMeios + estado.equip.coracoesExtraMeios;
        const int corpo = nivelAtributo(estado, Atributo::Corpo);
        if (corpo >= 10) {
            meios += 1;
        }
        if (corpo >= 20) {
            meios += 1;
        }
        return std::min(meios, param::kCoracoesMaxMeios);
    }

    int toleranciaTransicaoMin(const Estado& estado) {
        const int energia = nivelAtributo(estado, Atributo::Energia);
        return param::kToleranciaTransicaoMin + std::min(energia / 4, 5);
    }

    int folgasStreakMax(const Estado& estado) {
        return param::kFolgasStreakBase + (estado.equip.guardaBrasa ? 1 : 0);
    }

} // namespace tempo
