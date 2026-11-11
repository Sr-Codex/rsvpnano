#pragma once

// App "Habitos" (fase 1: dados, regras e sincronizacao). C++ puro (sem Arduino/FS): testavel no host.
// Decisoes e pesquisa da API: docs/app-tempo/habitos.md. Valores: docs/app-tempo/economia.md (secao 3 + interpretacoes).
//
// Fluxo: TickTick (Habitos ativos) -> /tempo/habitos.json -> ocorrencias do dia em estado.json (Dia.ocorrencias)
//        "Feito" na placa -> fila /tempo/habitos-fila.jsonl -> POST /open/v1/habit/{id}/checkin (Wi-Fi)
//        check-ins feitos no celular -> /tempo/habitos-checkins.json (sync) -> ocorrencias feitas (sem fila de volta)

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/Motor.h"
#include "tempo/Tipos.h"

namespace tempo {

    inline constexpr const char* kCaminhoHabitos = "/tempo/habitos.json";
    inline constexpr const char* kCaminhoHabitosTmp = "/tempo/habitos.json.tmp";
    inline constexpr const char* kCaminhoHabitosBak = "/tempo/habitos.json.bak";
    inline constexpr const char* kCaminhoFilaHabitos = "/tempo/habitos-fila.jsonl";
    inline constexpr const char* kCaminhoFilaHabitosTmp = "/tempo/habitos-fila.jsonl.tmp";
    inline constexpr const char* kCaminhoFilaHabitosBak = "/tempo/habitos-fila.jsonl.bak";
    inline constexpr const char* kCaminhoCheckinsHoje = "/tempo/habitos-checkins.json";
    inline constexpr const char* kCaminhoCheckinsHojeTmp = "/tempo/habitos-checkins.json.tmp";
    inline constexpr const char* kCaminhoCheckinsHojeBak = "/tempo/habitos-checkins.json.bak";
    inline constexpr size_t kMaxBytesHabitos = 48 * 1024;
    inline constexpr size_t kMaxBytesFila = 32 * 1024;
    inline constexpr size_t kMaxBytesCheckins = 48 * 1024;
    inline constexpr size_t kMaxHabitos = 40;
    inline constexpr size_t kMaxFila = 64;
    inline constexpr int kMaxTentativasFila = 10;   // acima disso a placa para de tentar (o item fica na fila)

    // ------------------------------------------------------------ definicoes (habitos.json)
    // Campos com os MESMOS nomes da API oficial do TickTick (GET /open/v1/habit), so os que usamos.
    struct DefHabito {
        std::string id;
        std::string name;                     // bruto (UTF-8, pode ter emoji)
        std::string repeatRule;               // RRULE, ex.: "RRULE:FREQ=WEEKLY;INTERVAL=1;BYDAY=MO,TU"
        std::vector<std::string> reminders;   // "HH:MM" locais
        double goal = 1.0;
        int targetStartDate = 0;              // AAAAMMDD (ancora de INTERVAL > 1); 0 = desconhecida
        std::string iconRes;                  // icone do TickTick ("habit_reading", "txt_<emoji>"); so visual; "" = sem (JSON antigo)
        std::string color;                    // "#RRGGBB" do TickTick ("" quando o icone e emoji); so visual
    };

    struct ListaHabitos {
        std::string fonte;
        std::string gerado_em;
        std::vector<DefHabito> habitos;
    };

    bool lerHabitos(std::string_view json, ListaHabitos& saida);

    // Habito ativo = status 0 e nao arquivado. archivedTime vazio/ausente ou ate 2001 (valor-sentinela "zero" dos
    // clientes Apple, ex.: "2001-01-01T00:00:00.000+0000") = nao arquivado.
    bool habitoArquivado(std::string_view archivedTime);

    // ------------------------------------------------------------------ nomes e tipos
    // Minusculas, sem acento, so ASCII (emoji e simbolos viram espaco). Base da deteccao do tipo.
    std::string normalizarNome(std::string_view bruto);
    // Nome para a tela: sem emoji/VS16/ZWJ/tons de pele. Vazio => "Meditação" (🧘), "Leitura" (📖), "Foco" (🧠), "Hábito".
    std::string nomeParaTela(std::string_view bruto);
    TipoHabito detectarTipo(std::string_view bruto);
    const char* nomeTipo(TipoHabito tipo);
    // Medicacao exige confirmacao em 2 toques na UI.
    bool exigeConfirmacao(TipoHabito tipo);

    // ------------------------------------------------------------------ repeticao (RRULE)
    struct RegraRepeticao {
        enum class Freq : uint8_t { Vazia, Diaria, Semanal, Mensal, Anual, NaoSuportada };
        Freq freq = Freq::Vazia;
        int intervalo = 1;
        uint8_t dias = 0;       // BYDAY: bit0 = segunda ... bit6 = domingo
        int diaDoMes = 0;       // BYMONTHDAY (mensal)
        bool temAte = false;
        Data ate;               // UNTIL
    };

    // Suporta FREQ=DAILY/WEEKLY (BYDAY, INTERVAL), MONTHLY (BYMONTHDAY ou dia do inicio) e YEARLY (dia/mes do inicio),
    // UNTIL; COUNT e ignorado. Regra vazia = todo dia. Qualquer outra coisa (HOURLY, BYSETPOS, "1MO"...) =
    // NaoSuportada: o habito NAO gera ocorrencias (nao arrisca penalizar o usuario com algo que nao entendemos).
    RegraRepeticao lerRegra(std::string_view regra);
    // inicioStamp = AAAAMMDD (targetStartDate; 0 = desconhecido: INTERVAL > 1 e mensal/anual sem BYMONTHDAY ficam sem ancora).
    bool valeNoDia(const RegraRepeticao& regra, const Data& dia, int inicioStamp);

    // ------------------------------------------------------------------ ocorrencias do dia
    std::vector<int> minutosDosLembretes(const std::vector<std::string>& reminders);   // ordenados, unicos, validos
    // Ocorrencias de todos os habitos que valem em `dia`, ordenadas (com horario por minuto; "qualquer hora" por ultimo).
    std::vector<OcorrenciaDia> gerarOcorrencias(const ListaHabitos& lista, const Data& dia);
    // Atualiza e.dia.ocorrencias com as definicoes: mantem as ja feitas, atualiza as pendentes, remove pendentes de
    // habitos que sumiram/nao valem mais. Devolve true se algo mudou.
    bool reconciliarDia(Estado& e, const ListaHabitos& lista);

    // ------------------------------------------------------------------ visao (para a tela da fase 2)
    enum class FaseOcorrencia : uint8_t {
        Futura,          // pendente, ainda antes da janela
        Agora,           // pendente, dentro da janela (ou "qualquer hora")
        Atrasada,        // pendente, depois da janela e ainda ate +2 h (vale 0 cr, 50 % XP)
        Perdida,         // pendente, alem de janela + 2 h (vai virar esquecida)
        Feita,           // feita no horario
        FeitaAtrasada,   // feita atrasada (0 cr, 50 % XP)
        Esquecida,       // esquecida (nao feita, ou feita tarde demais)
        Pulada,          // "Pular": sem cr/XP, fora das fracoes do dia
    };

    struct VistaOcorrencia {
        size_t indice = 0;       // indice em e.dia.ocorrencias
        std::string nome;
        TipoHabito tipo = TipoHabito::Geral;
        int minuto = -1;
        FaseOcorrencia fase = FaseOcorrencia::Futura;
        bool confirmar2Toques = false;
        double cr = 0.0;         // valor "no horario" do tipo
        double xp = 0.0;
        uint8_t icone = 9;       // tempo::IconeHabito (HabitosVista.h; 9 = Generico) — so visual
        int32_t cor = -1;        // 0xRRGGBB ja com contraste garantido; -1 = sem cor do TickTick
    };

    FaseOcorrencia faseDaOcorrencia(const OcorrenciaDia& oc, int minutoAgora);
    std::vector<VistaOcorrencia> ocorrenciasDoDia(const Estado& e, int minutoAgora);

    struct ProximaOcorrencia {
        size_t indice = 0;
        FaseOcorrencia fase = FaseOcorrencia::Futura;
    };
    // "AGORA/PRÓXIMO": 1) com horario na janela (a mais cedo); 2) atrasada (a mais cedo); 3) a proxima futura;
    // 4) "qualquer hora" pendente. nullopt = nada pendente.
    std::optional<ProximaOcorrencia> ocorrenciaAgora(const Estado& e, int minutoAgora);
    // Proximo horario de lembrete pendente (minuto >= minutoAgora) entre as ocorrencias com horario. nullopt = nenhum.
    std::optional<int> proximoLembrete(const Estado& e, int minutoAgora);
    // Proxima ocorrencia pendente do tipo (com horario mais cedo primeiro; "qualquer hora" por ultimo).
    std::optional<size_t> proximaPendenteDoTipo(const Estado& e, TipoHabito tipo);
    int ocorrenciasFeitas(const Estado& e, std::string_view habitoId);

    // ------------------------------------------------------------------ fila de check-ins (placa -> TickTick)
    // 1 item por (habito, dia): value = ocorrencias feitas no dia, goal = ocorrencias do dia.
    struct ItemFila {
        std::string id;           // habitId
        int stamp = 0;            // AAAAMMDD (data LOCAL)
        double valor = 1.0;       // value
        double meta = 1.0;        // goal
        std::string time;         // "yyyy-MM-ddTHH:mm:ss+0000" (UTC)
        int tentativas = 0;       // respostas HTTP de erro ate agora
        int ultimoHttp = 0;       // ultimo status (0 = falha de rede / nunca enviado)
    };

    int stampDe(const Data& data);
    Data dataDeStamp(int stamp);   // 0/invalido => Data{1970,1,1}
    // Instante "agora" (hora local da placa) em UTC no formato da API ("2026-11-10T11:10:00+0000").
    std::string tempoCheckinUtc(const Data& local, int minuto, int fusoMin = -180);

    std::string linhaFila(const ItemFila& item);                  // 1 linha JSON + '\n'
    std::string serializarFila(const std::vector<ItemFila>& fila);
    std::vector<ItemFila> lerFila(std::string_view jsonl);        // ignora linhas invalidas
    // Acrescenta; se ja ha item do mesmo (habito, dia) fica o de maior valor (nunca duplica).
    // A fila guarda no maximo kMaxFila itens (descarta os mais antigos).
    void enfileirar(std::vector<ItemFila>& fila, ItemFila novo);
    // Aplica o resultado de um envio: 200/201 remove (se nao entrou valor maior desde o envio); outro HTTP
    // incrementa tentativas; http == 0 (sem rede) so registra.
    void aplicarEnvio(std::vector<ItemFila>& fila, const ItemFila& enviado, int http);
    inline bool devePreEnviar(const ItemFila& item) { return item.tentativas < kMaxTentativasFila; }

    // ------------------------------------------------------------------ check-ins remotos (TickTick -> placa)
    struct CheckinRemoto {
        std::string habitId;
        int stamp = 0;
        double value = 1.0;
        double goal = 1.0;
        std::string time;         // hora do check-in como a API devolve (UTC); "" = desconhecida
    };

    struct ArquivoCheckins {
        std::string gerado_em;
        std::vector<CheckinRemoto> checkins;
    };

    bool lerCheckins(std::string_view json, ArquivoCheckins& saida);

    struct MomentoLocal {
        Data data;
        int minuto = 0;
    };
    // "2026-10-05T18:22:40.000+0000" / "...Z" / "...+00:00" -> data e minuto LOCAIS (fusoMin = deslocamento local).
    std::optional<MomentoLocal> interpretarTempo(std::string_view iso, int fusoMin = -180);

    // Marca como feitas as ocorrencias de hoje com check-in remoto. n = round(value) (limitado ao total); as novas
    // vao para as pendentes do habito: a mais proxima da hora do check-in primeiro (classificada pela hora do
    // check-in), as demais pelo horario planejado (no horario). Sem fila de volta.
    std::vector<ResultadoOcorrencia> aplicarCheckinsRemotos(Estado& e, const std::vector<CheckinRemoto>& checkins,
                                                            int fusoMin = -180);

    // ------------------------------------------------------------------ marcar na placa
    ItemFila itemFilaDoHabito(const Estado& e, std::string_view habitoId);
    // "Feito agora": registra no minuto atual do dia e enfileira o check-in.
    ResultadoOcorrencia marcarFeita(Estado& e, size_t indice, std::vector<ItemFila>& fila);
    // Batalha de Meditacao vencida: marca a proxima ocorrencia pendente do habito de Meditacao como feita NO HORARIO
    // (independente da hora atual), 1 por sessao, e enfileira.
    ResultadoOcorrencia marcarMeditacaoPorTimer(Estado& e, std::vector<ItemFila>& fila);

    // ------------------------------------------------------------------ desfazer (Habitos v2)
    enum class DestinoDesfazer : uint8_t {
        RemovidoDaFila,       // o check-in ainda nao tinha sido enviado e nao sobrou nenhuma ocorrencia feita: sai da fila
        SubstituidoNaFila,    // ainda nao enviado: o item da fila passa a ter o novo valor (menor)
        CorrecaoEnfileirada,  // JA enviado: enfileira o valor menor (a API "cria ou atualiza" por habito+dia). O caso valor 0
                              // NAO foi verificado na API (nao ha DELETE documentado): se o TickTick mantiver o check-in, a
                              // proxima sync o reaplica como feito (aplicarCheckinsRemotos).
    };
    // `novo` = itemFilaDoHabito(estado DEPOIS do desfazer). Diferente de `enfileirar`, aceita valor MENOR.
    DestinoDesfazer desfazerNaFila(std::vector<ItemFila>& fila, ItemFila novo);
    // Evento de log "habito" com resultado "desfeito" (o log e so de anexar; o Perfil usa removerDesfeitos).
    EventoLog eventoHabitoDesfeito(const Estado& e, size_t indice);
    // Remove do vetor os eventos "habito/desfeito" e o ultimo evento "habito" anterior do mesmo habito+horario que eles anulam.
    void removerDesfeitos(std::vector<EventoLog>& eventos);

    // Evento de log (tipo "habito") para o recibo/Perfil. origem: "" (placa), "ticktick" (celular), "timer".
    EventoLog eventoHabito(const Estado& e, const ResultadoOcorrencia& r, std::string_view origem);

} // namespace tempo
