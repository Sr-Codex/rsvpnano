#pragma once

// Motor da economia do app "Tempo" (v1). C++ puro, deterministico: tempo e datas entram como
// parametros. Fonte unica dos numeros: docs/app-tempo/economia.md (+ decisoes da rodada 1).

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace tempo {

    // ---------------------------------------------------------------- tempo
    struct Data {
        int ano = 1970;
        int mes = 1;   // 1..12
        int dia = 1;   // 1..31
    };

    bool operator==(const Data& a, const Data& b);
    bool operator<(const Data& a, const Data& b);

    int64_t diaSerial(const Data& d);        // dias desde 1970-01-01
    Data dataDeSerial(int64_t serial);
    int diaDaSemana(const Data& d);          // 0 = segunda ... 6 = domingo
    int64_t segundaDaSemana(const Data& d);  // serial da segunda-feira da semana
    int chaveMes(const Data& d);             // ano * 12 + (mes - 1)

    // ------------------------------------------------------------ categorias
    enum class Categoria : uint8_t {
        Trabalho,
        Projetos,
        Estudo,
        Leitura,
        Meditacao,
        Count,
    };
    constexpr size_t kNumCategorias = static_cast<size_t>(Categoria::Count);

    enum class Atributo : uint8_t { Oficio, Mente, Corpo, Energia, Count };
    constexpr size_t kNumAtributos = static_cast<size_t>(Atributo::Count);

    struct TaxaCategoria {
        double crPorMin;
        double xpPorMin;
        Atributo atributo;
    };

    // ---------------------------------------------------------------- habitos
    enum class Habito : uint8_t {
        Academia,
        Suplemento,
        Almoco,
        Banho,
        Dentes,
        Descanso,
        Medicacao,
        Count,
    };
    constexpr size_t kNumHabitos = static_cast<size_t>(Habito::Count);

    enum class StatusHabito : uint8_t { Pendente, NoHorario, Atrasado, Esquecido };

    // Tipo de habito DETECTADO pelo nome/emoji (habitos vindos do TickTick; ver tempo/Habitos.h).
    enum class TipoHabito : uint8_t {
        Geral,
        Academia,
        Medicacao,
        Meditacao,
        Suplemento,
        Almoco,
        Banho,
        Dentes,
        Descanso,
        Count,
    };

    // Uma ocorrencia de habito no dia (cada horario de lembrete = 1 ocorrencia). Persistida em estado.json.
    struct OcorrenciaDia {
        std::string habitoId;
        std::string nome;                        // ja limpo para a tela (sem emoji)
        TipoHabito tipo = TipoHabito::Geral;
        int indice = 0;                          // 0..total-1 dentro do habito no dia
        int total = 1;                           // ocorrencias do habito hoje (goal do check-in)
        int minuto = -1;                         // minuto planejado (0..1439); -1 = qualquer hora
        StatusHabito status = StatusHabito::Pendente;
        bool feita = false;                      // registrada (mesmo que tarde demais: status Esquecido)
        int minutoReal = -1;
        bool remota = false;                     // marcada a partir do TickTick (nao gera fila de volta)
    };

    struct ValorHabito {
        double cr;
        double xp;
        Atributo atributo;
        int janelaMin;  // +-janela (Descanso: ate +janela depois do planejado)
    };

    // ----------------------------------------------------------------- lazer
    enum class ClasseIntervalo : uint8_t { Descanso, Ok, Perdido };

    // --------------------------------------------------------------- batalha
    struct EntradaBatalha {
        Categoria categoria = Categoria::Projetos;
        int minutosPlanejados = 0;   // HP do inimigo
        int minutosFocados = 0;      // minutos de foco efetivos (inclui overtime e minutos curados)
        int minutosDistraidos = 0;   // tempo fora do foco (cada minuto cura 1 HP)
        int numDistracoes = 0;
        int maiorDistracaoMin = 0;
        bool abandonou = false;
        bool porPrazer = false;      // Leitura "por prazer": 0 cr, so XP
    };

    struct LiquidacaoBatalha {
        bool vitoria = false;
        bool perfeita = false;
        bool fuga = false;
        int curaMin = 0;
        int minutosPagos = 0;        // minutos base pagos (plano + overtime, ou focados na fuga)
        int minutosOvertime = 0;
        int perdidoMin = 0;          // minutos distraidos que viram Tempo perdido (> 2 min)
        double crBruto = 0.0;        // antes dos bonus
        double crBonus = 0.0;
        double cr = 0.0;             // final (depois do teto diario)
        double xp = 0.0;
        double bonusCrPct = 0.0;     // soma aditiva ja limitada (0..0,40)
        double bonusXpPct = 0.0;     // (0..0,50)
        bool tetoCrAplicado = false; // algum minuto passou das 6 h
        bool tetoXpAplicado = false; // algum minuto passou das 8 h
        Atributo atributo = Atributo::Oficio;
    };

    // ---------------------------------------------------------------- desafios
    struct Desafio {
        int nivel = 1;                 // 1..3
        int64_t criadoEmMin = 0;       // minutos absolutos (diaSerial * 1440 + minuto do dia)
        int64_t prazoEmMin = 0;        // criadoEmMin + 1440
        bool cumprido = false;
        int diasPenalizados = 0;       // dias de atraso ja cobrados (-1 coracao por dia)
    };

    // ----------------------------------------------------------------- eventos
    enum class TipoEvento : uint8_t {
        Batalha,
        Habito,
        Humor,
        TickTick,
        Lazer,
        Perdido,
        Transicao,
        DesafioDisparado,
        DesafioCumprido,
        DesafioVencido,
        FecharDia,
        Credito,
    };

    struct Evento {
        TipoEvento tipo = TipoEvento::Batalha;
        int minutoDoDia = 0;
        double cr = 0.0;       // + ganho / - gasto
        double xp = 0.0;
        int coracoesMeios = 0; // variacao
        int detalhe = 0;       // categoria / habito / nivel...
    };

    // --------------------------------------------------------------- equipamento
    // Bonus aditivos de itens (parametros de entrada; a loja e v2).
    struct Equipamento {
        std::array<double, kNumCategorias> crExtra{};   // ex.: espada +0,05 em Trabalho/Projetos
        std::array<double, kNumCategorias> xpExtra{};   // ex.: asas +0,05, capuz +0,10
        double refinoCrExtra = 0.0;                     // refino (<= 0,075), v2
        int coracoesExtraMeios = 0;                     // Gibao +1, Coraca +2
        bool guardaBrasa = false;                       // +1 folga de streak por semana
    };

    // ------------------------------------------------------------------ o dia
    struct Dia {
        Data data;
        bool aberto = false;
        bool fechado = false;
        bool recuperacao = false;
        std::array<StatusHabito, kNumHabitos> habitos{};
        int fochoPagoMin = 0;          // minutos de foco pagos hoje (tetos 6 h / 8 h)
        int fochoValidoMin = 0;        // foco concluido sem fuga (dia valido)
        int meditacaoMin = 0;          // minutos pagos de meditacao hoje (teto 30)
        int perdidoMin = 0;
        int okUsadoMin = 0;
        int descansoUsadoMin = 0;
        int descansoOrcamentoMin = 0;  // configurado pelo chamador no abrirDia
        int transicaoPendenteMin = 0;  // nao classificada: vira Perdido no fechamento
        std::vector<OcorrenciaDia> ocorrencias;  // habitos do TickTick hoje (vazio = sem habitos configurados)
        double habitosCr = 0.0;        // cr de habitos ja pagos hoje (teto 28)
        int humorRegistros = 0;
        int tickTick = 0;
        int ganhoCoracoesMeios = 0;    // recuperacao ja usada hoje (max 2 meios)
        double crGanho = 0.0;
        double crGasto = 0.0;
        double xpGanho = 0.0;
        bool desafioHoje = false;      // ja disparou 1 desafio novo hoje
        std::array<int, kNumAtributos> nivelAtributoInicio{};  // niveis no inicio do dia (bonus estaveis no dia)
        int minutoAtual = 0;           // relogio do dia (minuto 0..1439), definido pelo chamador
        int coracoesDeltaMeios = 0;    // variacao liquida de coracoes no dia
        std::vector<Evento> eventos;
    };

    // ----------------------------------------------------------------- estado
    struct Estado {
        // carteira
        double saldo = 0.0;            // pode ser negativo (divida)
        double cofre = 0.0;            // so loja/forja
        // progressao
        double xpTotal = 0.0;
        std::array<double, kNumAtributos> pontosAtributo{};
        // coracoes (em meios)
        int coracoesMeios = 10;
        bool heroiCaido = false;
        int resgateDiasValidos = 0;
        // desafios
        std::vector<Desafio> desafiosPendentes;
        int patamarDisparado = 0;               // maior patamar de divida (60 cr) ja tratado
        bool gatilhoFechamentoPendente = false;
        int fechamentosNegativosSeguidos = 0;
        int desafiosNaSemana = 0;
        // streak
        int streak = 0;
        int folgasUsadasSemana = 0;
        int64_t semanaRef = -1;                 // serial da segunda da semana corrente
        // salvaguardas
        int recuperacoesMesChave = -1;
        int recuperacoesUsadasMes = 0;
        bool ferias = false;
        int64_t feriasInicioSerial = 0;
        // equipamento e dia
        Equipamento equip;
        Dia dia;
        // preferencias da tela inicial do Tempo (lembradas entre sessoes)
        int ultimaCategoria = 0;       // Categoria (enum) da ultima sessao
        int ultimaDuracaoMin = 25;     // 5..60
        bool ciclopomodoro = false;    // interruptor "ciclo pomodoro 4x"
        int metaSemanalMin = 900;      // meta de foco da semana no Perfil (padrao 15 h; editavel no estado.json)
    };

    // ---------------------------------------------------------------- resumo
    struct ResumoDia {
        Data data;
        double crGanho = 0.0;
        double crGasto = 0.0;
        double saldoDoDia = 0.0;       // crGanho - crGasto
        double saldoFinal = 0.0;       // carteira ao fechar
        double xpGanho = 0.0;
        int perdidoMin = 0;
        int habitosNoHorario = 0;
        bool valido = false;
        bool limpo = false;
        bool fechamentoNegativo = false;
        int coracoesDeltaMeios = 0;
        bool streakQuebrado = false;
        bool folgaUsada = false;
        bool recuperacao = false;
    };

} // namespace tempo
