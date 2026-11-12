#pragma once

// Sessao do app "Tempo": ciclo pomodoro 4x e nomes de exibicao. C++ puro (testavel no host).

#include <string>
#include <vector>

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
        int focoMin = kCicloFocoMin;      // duracao de cada rodada (a escolhida na regua do Inicio)
        int pausaMin = kCicloPausaMin;    // pausa curta entre rodadas (pausaDoCiclo(focoMin))
    };

    // Pausa curta entre rodadas do 4x: rodada/5 arredondada a multiplo de 5 min, limitada a 5..15
    // (25 -> 5; 50/60 -> 10; 75/90 -> 15). A pausa longa depois da 4a rodada continua kCicloPausaLongaMin.
    int pausaDoCiclo(int rodadaMin);
    // Minutos de relogio do 4x do comeco ate o fim da ultima rodada: n * rodada + (n - 1) pausas curtas
    // (a pausa longa depois da 4a nao conta). Sem 4x: n = 1 -> so a rodada.
    int duracaoTotalMin(int rodadaMin, bool ciclo4x);

    Ciclo iniciarCiclo(int focoMin = kCicloFocoMin);   // rodada 1, foco; pausa curta derivada de focoMin
    Ciclo avancarCiclo(const Ciclo& c);   // o passo atual terminou; apos a pausa longa o ciclo acaba (ativo = false)
    int duracaoCicloMin(const Ciclo& c);  // minutos do passo atual

    // Categorias na ordem dos chips da tela inicial.
    inline constexpr size_t kNumChips = 5;
    Categoria categoriaDoChip(int chip);   // 0 Trabalho, 1 Estudo, 2 Leitura, 3 Meditacao, 4 Projetos
    int chipDaCategoria(Categoria c);
    const char* nomeCategoria(Categoria c);
    // "Estudo · sem projeto": nome do "inimigo" quando nao ha projeto.
    std::string nomeFocoLivre(Categoria c);
    // 2a linha do cartao do foco no Inicio: "Projetos · 0,40 cr/min" ou "sem projeto · 0,20 cr/min".
    std::string linhaTaxaDoFoco(bool temProjeto, Categoria livre);
    // Sub do botao Comecar: "25 min · ate 14:57" / "4 × 60 · ate 19:02" (sem relogio valido: so "25 min" / "4 × 60").
    // minutoDoDia = hora de parede em minutos desde 00:00 (ignorado se !horaValida).
    std::string textoComecar(int rodadaMin, bool ciclo4x, bool horaValida, int minutoDoDia);

    // ---- Foco do Inicio (Tempo v3). Com projeto a categoria e UNICA: Projetos (nunca Trabalho/Meditacao etc.).
    // Sem projeto (Foco livre) vale a categoria livre escolhida no seletor (Trabalho, Estudo, Leitura ou Meditacao).
    Categoria categoriaDoFoco(bool temProjeto, Categoria livre);
    Categoria categoriaLivreValida(int bruto);   // valor salvo -> categoria livre (Projetos/invalido vira Trabalho)
    // Taxa de creditos por minuto da categoria (para "0,40 cr/min").
    double crPorMinuto(Categoria c);
    // Texto "0,40 cr/min" (virgula decimal).
    std::string textoTaxa(Categoria c);

    // Projetos usados por ultimo: guarda ate kMaxRecentes ids (mais novo primeiro, sem repetir).
    inline constexpr size_t kMaxRecentes = 2;
    void registrarProjetoRecente(std::vector<std::string>& recentes, const std::string& id);
    // Indices (em `ids`, a lista de projetos) dos 2 projetos da pagina 1 do seletor: os recentes que ainda existem
    // e, se faltar, os primeiros da lista. No maximo kMaxRecentes (ou menos se nao ha projetos).
    std::vector<int> projetosDaPrimeiraPagina(const std::vector<std::string>& recentes, const std::vector<std::string>& ids);

    // Paginacao do seletor (opcao B): pagina 1 = linha de 4 categorias + 2 projetos recentes; demais = 3 projetos por pagina.
    inline constexpr size_t kProjetosPagina1 = 2;
    inline constexpr size_t kProjetosPorPagina = 3;
    size_t paginasDoSeletor(size_t numProjetos);
    // Indices dos projetos mostrados na pagina `pagina` (0 = primeira). `primeira` = projetosDaPrimeiraPagina().
    std::vector<int> projetosDaPagina(size_t pagina, const std::vector<int>& primeira, size_t numProjetos);

} // namespace tempo
