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
    // O 4x so existe ate 90 min por rodada (acima disso o botao apaga e o ciclo desliga).
    inline constexpr int kCiclo4xMaxMin = 90;
    constexpr bool ciclo4xPermitido(int rodadaMin) { return rodadaMin > 0 && rodadaMin <= kCiclo4xMaxMin; }   // livre (0): nunca
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
    // Relogio da batalha: "MM:SS" ate 59:59 e "H:MM:SS" a partir de 1 h (duracoes de ate 5 h + hora extra).
    std::string formatarRelogio(long long totalSeg);
    // "Estudo · sem projeto": nome do "inimigo" quando nao ha projeto.
    std::string nomeFocoLivre(Categoria c);
    // 2a linha do cartao do foco no Inicio: "Projeto · 0,40 cr/min" ou "sem projeto · 0,20 cr/min".
    std::string linhaTaxaDoFoco(bool temProjeto, Categoria livre);
    // Sub do botao Comecar: "25 min · ate 14:57" / "4 × 60 · ate 19:02" / "2h30 · ate HH:MM" / "5 h · ate HH:MM"
    // (sem relogio valido: so a duracao). 4x acima de 90 min por rodada e ignorado.
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

    // Categoria do "sem projeto" no Tempo (Trabalho, Estudo, Meditacao): valor salvo -> categoria. Leitura (o app Leitor conta a
    // leitura; a categoria continua no motor), Projetos e valores invalidos viram Trabalho.
    Categoria categoriaSemProjetoValida(int bruto);
    // "0,20" (taxa sem a unidade, para o seletor: "sem projeto · 0,20").
    std::string textoTaxaCurta(Categoria c);

    // Projeto lembrado pelo id do TickTick: acha o indice na lista atual (a ordem muda a cada sincronizacao).
    // `listaLida` = false (arquivo ilegivel / sync gravando): nao julga, mantem o que esta (indice -1, saiu = false).
    // id lembrado que nao esta mais na lista (lida) = saiu: o Tempo volta para "sem projeto" e avisa.
    struct ResolucaoProjeto {
        int indice = -1;     // -1 = sem projeto
        bool saiu = false;   // havia projeto lembrado e ele nao esta mais na coluna AGORA
    };
    ResolucaoProjeto resolverProjeto(const std::string& id, const std::vector<std::string>& ids, bool listaLida);

    // Paginacao do "Trocar ›": 2 projetos por pagina (setas so se houver mais de 2).
    inline constexpr size_t kProjetosPorPagina = 2;
    size_t paginasDoSeletor(size_t numProjetos);                       // >= 1
    std::vector<int> projetosDaPagina(size_t pagina, size_t numProjetos);   // indices da pagina (0 = primeira)

} // namespace tempo
