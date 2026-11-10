#pragma once

#include <optional>

#include "tempo/Tipos.h"

namespace tempo {

    // ---- resultados auxiliares
    struct ResultadoHabito {
        bool registrado = false;  // false se o habito ja tinha status
        StatusHabito status = StatusHabito::Pendente;
        double cr = 0.0;
        double xp = 0.0;
        int coracoesMeios = 0;    // variacao pedida (medicacao)
    };

    // Habitos do TickTick (ocorrencias do dia; ver tempo/Habitos.h).
    struct ResultadoOcorrencia {
        bool registrado = false;  // false = indice invalido ou ocorrencia ja feita
        size_t indice = 0;
        StatusHabito status = StatusHabito::Pendente;
        double cr = 0.0;          // ja depois do teto de 28 cr/dia
        double xp = 0.0;
        int coracoesMeios = 0;    // variacao pedida (medicacao)
        bool tetoAplicado = false;
    };

    struct ResultadoTransicao {
        int excessoMin = 0;       // acima da tolerancia
        int descansoMin = 0;
        int okMin = 0;
        int perdidoMin = 0;
        int pendenteMin = 0;      // nao classificado (vira Perdido no fechamento)
        double custoCr = 0.0;
    };

    // ---- dia
    // Abre (ou avanca para) o dia `data`. Fecha automaticamente dias anteriores nao fechados
    // (sem o +20 XP do recibo) e dias vazios intermediarios; em Ferias so avanca a data.
    void abrirDia(Estado& e, const Data& data, int descansoOrcamentoMin = 0);
    // Define o relogio do dia (minuto 0..1439) e cobra desafios vencidos.
    void avancarRelogio(Estado& e, int minutoDoDia);
    // Fecha o dia. reciboVisto = o usuario abriu o recibo (+20 XP).
    ResumoDia fecharDia(Estado& e, bool reciboVisto);

    // ---- batalha
    LiquidacaoBatalha liquidarBatalha(const Estado& e, const EntradaBatalha& entrada);  // pura
    LiquidacaoBatalha aplicarBatalha(Estado& e, const EntradaBatalha& entrada);

    // ---- habitos, humor, TickTick
    StatusHabito classificarHabito(Habito habito, int minutoPlanejado, int minutoReal);  // pura
    ResultadoHabito registrarHabito(Estado& e, Habito habito, int minutoPlanejado, int minutoReal);
    // Classifica uma ocorrencia por tipo: minutoPlanejado < 0 ("qualquer hora") nunca atrasa. Descanso so atrasa para depois.
    StatusHabito classificarOcorrencia(TipoHabito tipo, int minutoPlanejado, int minutoReal);  // pura
    // Registra a ocorrencia `indice` de e.dia.ocorrencias como feita em `minutoReal`: cr/XP pelo tipo (teto de 28 cr/dia
    // somando todos os habitos), medicao atrasada -1 meio, esquecida (ou feita tarde demais) -2 meios.
    ResultadoOcorrencia registrarOcorrencia(Estado& e, size_t indice, int minutoReal, bool remota = false);
    // "Pular" (fase 2 dos Habitos): marca a ocorrencia pendente como Pulada. Sem cr/XP/coracoes, nao entra na fila do
    // TickTick e nao conta como esquecida (sai do denominador das fracoes do dia). registrado=false se ja estava resolvida.
    ResultadoOcorrencia pularOcorrencia(Estado& e, size_t indice);
    double registrarHumor(Estado& e);    // +2 XP, nunca credito; devolve XP
    double registrarTickTick(Estado& e); // +30 XP, max 3/dia; devolve XP

    // ---- transicao, perdido, lazer
    ResultadoTransicao registrarTransicao(Estado& e, int minutosIntervalo,
                                          std::optional<ClasseIntervalo> classe);
    ResultadoTransicao classificarTransicaoPendente(Estado& e, ClasseIntervalo classe, int minutos);
    double registrarPerdido(Estado& e, int minutos);  // devolve custo em cr
    double registrarLazer(Estado& e, int minutos);    // 1 cr/min; devolve custo

    // ---- carteira
    void creditar(Estado& e, double cr);              // paga divida primeiro; teto 600 + Cofre
    void debitar(Estado& e, double cr);
    int nivelDivida(double saldo);                    // floor(-saldo / 60) para saldo < 0

    // ---- desafios
    // Avalia gatilhos pendentes (patamar de divida e 2 fechamentos negativos); max 1 por dia.
    bool avaliarGatilhos(Estado& e);
    bool cumprirDesafio(Estado& e, size_t indice);
    bool lojaTravada(const Estado& e);                // desafio pendente ou heroi caido

    // ---- coracoes
    // delta em meios. Perdas sempre; ganhos limitados a +1/dia. Devolve a variacao aplicada.
    int aplicarCoracoes(Estado& e, int deltaMeios);

    // ---- salvaguardas
    bool declararRecuperacao(Estado& e);              // ate 12h, 2 por mes
    bool iniciarFerias(Estado& e);
    bool encerrarFerias(Estado& e, const Data& hoje); // exige >= 2 dias

} // namespace tempo
