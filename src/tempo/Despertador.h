#pragma once

// Desligamento automatico com despertador (ADR-009, docs/adr/009-lembretes-sono-profundo-com-despertador.md).
// C++ puro, sem hardware: decide se, ao fim do descanso, a placa DESLIGA (solta o SYS_EN, como sempre) ou
// DORME ATE um lembrete de habito (deep sleep do ESP32 com timer, SYS_EN mantido).
//
// Regras (todas precisam valer para dormir; qualquer falha = Desligar):
//  1. Motivo AUTOMATICO (descanso expirou). "Desligar" manual / segurar PWR desliga SEMPRE.
//  2. Relogio valido (RTC/NTP): sem hora nao da para calcular o alarme.
//  3. Existe um lembrete pendente dentro do HORIZONTE (kHorizonteMaxS = 14 h; cobre uma noite inteira:
//     ultimo habito 21:00 -> primeiro da manha 07:00). Candidatos: proximo horario pendente de hoje, primeiro horario
//     de amanha (se o dia de hoje acabou), e a INSISTENCIA da medicacao (15 min depois do ultimo aviso, ate
//     kMaxInsistenciasMedicacao vezes e so enquanto a ocorrencia esta na janela/atrasada).
//  4. Bateria >= kBateriaMinimaPercent (15 %) ou carregando. Bateria desconhecida e nao carregando = Desligar.
//  5. Limite de ciclos: apos kMaxCiclosSemInteracao (3) despertares seguidos SEM interacao (nem toque nem botao),
//     so continua dormindo para lembretes ainda de HOJE (fim dos lembretes do dia); nada de amanha. Qualquer
//     interacao zera o contador. Protege a bateria se a placa foi esquecida numa gaveta.
//  6. Acorda kAntecedenciaS (5 s) ANTES do alvo (boot ~2-3 s), para o aviso tocar na hora certa.

#include <cstdint>
#include <optional>

#include "tempo/Habitos.h"
#include "tempo/Lembretes.h"
#include "tempo/Tipos.h"

namespace tempo {

    inline constexpr int64_t kHorizonteMaxS = 14LL * 3600;       // lembrete alem disso: desliga
    inline constexpr int kBateriaMinimaPercent = 15;
    inline constexpr int kMaxCiclosSemInteracao = 3;
    inline constexpr int64_t kAntecedenciaS = 5;
    inline constexpr int64_t kMinimoSonoS = 3;                   // nunca agenda o timer para menos que isso
    inline constexpr int64_t kAdiantadoMinS = 20;                // acordou com mais que isso faltando para o alvo = adiantado (oscilador RC)
    inline constexpr int kMaxReDormidas = 4;                     // protecao: no maximo N "dorme de novo" seguidos (relogio maluco nao vira laco)
    inline constexpr uint32_t kJanelaLembreteBootMs = 60U * 1000U;   // acordado esperando interacao apos despertar por timer

    enum class MotivoDesligar : uint8_t {
        Automatico,   // descanso expirou
        Manual,       // menu "Desligar" / PWR segurado
    };

    enum class OrigemDespertar : uint8_t { Nenhuma, Lembrete, InsistenciaMedicacao };

    enum class PorqueDesliga : uint8_t {
        Dorme,            // (nao desliga)
        Manual,
        RelogioInvalido,
        SemLembrete,      // nada pendente (ou todos fora do horizonte)
        ForaDoHorizonte,
        BateriaBaixa,
        LimiteDeCiclos,
    };

    struct EntradaDespertador {
        MotivoDesligar motivo = MotivoDesligar::Automatico;
        bool relogioValido = false;
        int64_t epochAgora = 0;                // UTC (time(nullptr))
        Data hoje;                             // data local
        int segundoDoDia = 0;                  // local, 0..86399
        const Estado* estado = nullptr;        // ocorrencias de hoje (so vale se estado->dia.data == hoje)
        std::optional<int> primeiroLembreteAmanhaMin;   // primeiro horario de amanha (nullopt = nenhum/desconhecido)
        int bateriaPercent = -1;               // -1 = desconhecida
        bool carregando = false;
        int ciclosSemInteracao = 0;            // despertares seguidos sem interacao
        int insistenciasMedicacao = 0;         // insistencias de medicacao ja feitas hoje (por despertar)
        std::optional<int64_t> ultimoAvisoMedicacaoEpoch;   // ultimo aviso de medicacao (nullopt = desconhecido)
    };

    struct DecisaoDespertador {
        bool dormir = false;
        int64_t acordarEpoch = 0;              // quando o timer dispara (alvo - antecedencia)
        int64_t alvoEpoch = 0;                 // instante do lembrete
        OrigemDespertar origem = OrigemDespertar::Nenhuma;
        PorqueDesliga porque = PorqueDesliga::SemLembrete;
    };

    DecisaoDespertador decidirDespertador(const EntradaDespertador& entrada);

    // Boot por timer (ADR-009, achado 4 da revisao): o timer de sono usa o oscilador RC interno e pode acordar adiantado
    // ou atrasado. Compara o relogio (RTC externo) com o alvo gravado antes de dormir.
    //  - adiantado (faltam > kAdiantadoMinS): dorme de novo ate o alvo SEM contar ciclo sem interacao;
    //  - atrasado ou na hora: toca o lembrete MESMO se passou da tolerancia de 5 min (so neste caso).
    struct AvaliacaoDespertar {
        bool adiantado = false;
        bool atrasado = false;          // passou do alvo (qualquer atraso)
        int64_t faltamS = 0;            // > 0 = ainda falta; < 0 = atraso
        uint32_t dormirS = 0;           // quando adiantado: segundos de novo sono (ja descontada a antecedencia)
    };
    AvaliacaoDespertar avaliarDespertar(int64_t epochAgora, int64_t alvoEpoch, int reDormidas);

    // Primeiro horario de lembrete (minuto do dia) que a lista de habitos gera em `dia`; nullopt = nenhum.
    std::optional<int> primeiroLembreteDoDia(const ListaHabitos& lista, const Data& dia);

    // Texto curto para log/diagnostico.
    const char* nomePorqueDesliga(PorqueDesliga p);

} // namespace tempo
