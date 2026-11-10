#pragma once

// Sessao do app "Tempo": ciclo pomodoro 4x e nomes de exibicao. C++ puro (testavel no host).

#include <string>

#include "tempo/Tipos.h"

namespace tempo {

    // Ciclo pomodoro: 25 min de foco -> 5 de pausa, 4 rodadas; depois da 4a rodada, pausa longa de 15 min.
    inline constexpr int kCicloRodadas = 4;
    inline constexpr int kCicloFocoMin = 25;
    inline constexpr int kCicloPausaMin = 5;
    inline constexpr int kCicloPausaLongaMin = 15;

    enum class PassoCiclo : uint8_t { Foco, PausaCurta, PausaLonga };

    struct Ciclo {
        bool ativo = false;
        PassoCiclo passo = PassoCiclo::Foco;
        int rodada = 1;   // 1..kCicloRodadas
    };

    Ciclo iniciarCiclo();                 // rodada 1, foco
    Ciclo avancarCiclo(const Ciclo& c);   // o passo atual terminou; apos a pausa longa o ciclo acaba (ativo = false)
    int duracaoCicloMin(const Ciclo& c);  // minutos do passo atual

    // Categorias na ordem dos chips da tela inicial.
    inline constexpr size_t kNumChips = 5;
    Categoria categoriaDoChip(int chip);   // 0 Trabalho, 1 Estudo, 2 Leitura, 3 Meditacao, 4 Projetos
    int chipDaCategoria(Categoria c);
    const char* nomeCategoria(Categoria c);
    // "Foco livre · Estudo": nome do "inimigo" quando nao ha projeto.
    std::string nomeFocoLivre(Categoria c);

} // namespace tempo
