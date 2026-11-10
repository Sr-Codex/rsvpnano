#pragma once

#include "tempo/Tipos.h"

namespace tempo {

    // XP para ir do nivel n ao n+1: arredondar_a_10(60 * n^1,5).
    int xpProximo(int nivel);
    // XP acumulado necessario para ESTAR no nivel n (nivel 1 = 0).
    int64_t xpAcumulado(int nivel);
    // Nivel (1..30) a partir do XP acumulado.
    int nivelDeXp(double xp);
    // nivel_attr = floor(sqrt(pontos / K)), limitado a 20.
    int nivelAtributo(Atributo atributo, double pontos);
    int nivelAtributo(const Estado& estado, Atributo atributo);

    // Efeitos dos atributos / equipamento.
    int coracoesMaxMeios(const Estado& estado);
    int toleranciaTransicaoMin(const Estado& estado);
    int folgasStreakMax(const Estado& estado);

} // namespace tempo
