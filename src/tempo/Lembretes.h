#pragma once

// App "Habitos" (fase 2): lembretes (bipe + aviso no topo) e deteccao de "pegou a placa". C++ puro, testavel no host.
//
// Regras (decisoes do usuario, docs/app-tempo/habitos.md):
//  - Na hora de cada ocorrencia COM horario (oc.minuto) e ainda pendente: aviso inicial (1x).
//  - Nao atendido: o aviso toca de novo QUANDO A PLACA FOR PEGA/MEXIDA (IMU), no maximo a cada 2 min por ocorrencia
//    e no maximo kMaxReavisos vezes (sem loop de bipe).
//  - Medicacao insiste sozinha 15 min depois do ultimo aviso (ate kMaxInsistenciasMedicacao vezes); "Ainda nao" adia 15 min.
//  - "Qualquer hora" (sem horario) nunca gera lembrete. So avisa enquanto a ocorrencia esta na janela ou atrasada (+2 h).

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "tempo/Habitos.h"

namespace tempo {

    inline constexpr uint32_t kIntervaloReavisoMs = 2U * 60U * 1000U;     // minimo entre avisos da mesma ocorrencia (re-aviso por movimento)
    inline constexpr uint32_t kInsistenciaMedicacaoMs = 15U * 60U * 1000U;
    inline constexpr int kMaxReavisos = 6;                  // re-avisos por movimento por ocorrencia
    inline constexpr int kMaxInsistenciasMedicacao = 4;     // insistencias automaticas da medicacao
    inline constexpr int kToleranciaAvisoInicialMin = 5;     // aviso inicial so se a ocorrencia venceu ha <= 5 min (senao so re-aviso por movimento)
    inline constexpr uint32_t kAvisoVisivelMs = 8000;       // quanto tempo o aviso fica no topo da tela

    // ------------------------------------------------------------------ "pegou a placa" (IMU)
    // Alimentar com a variacao de aceleracao (g) entre amostras consecutivas (~20 Hz). Dispara UMA vez por "pegada":
    // precisa de >= kPicosParaPegar amostras acima do limite dentro de kJanelaPegadaMs; depois so rearma quando a placa
    // ficar quieta (variacao < kLimiteQuieto) por kQuietoRearmaMs (histerese: segurar a placa tremendo nao dispara de novo).
    class DetectorPegada {
    public:
        static constexpr float kLimiteMovimento = 0.25f;   // g
        static constexpr float kLimiteQuieto = 0.10f;      // g
        static constexpr int kPicosParaPegar = 3;
        static constexpr uint32_t kJanelaPegadaMs = 1500;
        static constexpr uint32_t kQuietoRearmaMs = 1500;

        // true no instante em que a pegada e reconhecida.
        bool alimentar(uint32_t agoraMs, float variacaoG);
        void zerar();

    private:
        int picos_ = 0;
        uint32_t primeiroPicoMs_ = 0;
        bool armado_ = true;
        uint32_t quietoDesdeMs_ = 0;
        bool quieto_ = false;
    };

    // ------------------------------------------------------------------ agenda de lembretes
    struct Aviso {
        size_t indice = 0;            // ocorrencia (em Estado::dia.ocorrencias)
        std::string nome;
        int minuto = -1;              // horario planejado
        TipoHabito tipo = TipoHabito::Geral;
        bool repeticao = false;       // false = aviso inicial; true = re-aviso (movimento/insistencia/adiado)
        int outros = 0;               // outras ocorrencias que venceram no mesmo instante
    };

    class AgendaLembretes {
    public:
        // Chamar ~1x/s (fora do desenho). `pegouAgora` = pulso do DetectorPegada. Devolve o aviso a mostrar (e tocar o
        // bipe) ou nullopt. `agoraMs` = relogio monotonico REAL (millis), nao o relogio escalado do app.
        std::optional<Aviso> avaliar(const Estado& e, int minutoAgora, uint32_t agoraMs, bool pegouAgora);

        // "Ainda nao" (medicacao): sem aviso ate agoraMs + kAdiarMedicacaoMin; depois avisa de novo.
        void adiar(const Estado& e, size_t indice, uint32_t agoraMs, uint32_t adiarMs = 15U * 60U * 1000U);

        // "Desfazer" devolveu a ocorrencia a pendente: conta como ja avisada agora (nao toca de novo na hora; so o
        // re-aviso por movimento, com os limites de sempre).
        void jaAvisada(const Estado& e, size_t indice, uint32_t agoraMs);

        // Ocorrencia resolvida (feita/pulada): esquece o estado dela.
        void resolvida(const Estado& e, size_t indice);

        // Despertar por INSISTENCIA da medicacao (ADR-009): a agenda em RAM se perdeu no deep sleep. Arma as medicacoes
        // pendentes (na janela/atrasadas) para insistirem ja no proximo avaliar(); `insistenciasFeitas` inclui esta.
        void semearInsistenciaMedicacao(const Estado& e, int minutoAgora, uint32_t agoraMs, int insistenciasFeitas);

        // Despertar por timer (ADR-009): o lembrete que motivou o despertar toca MESMO atrasado (ignora a tolerancia de
        // kToleranciaAvisoInicialMin so para as ocorrencias pendentes marcadas para `minutoAlvo`). Chamar com o estado do dia carregado.
        void forcarAvisoInicial(const Estado& e, int minutoAlvo);

        void zerar() { itens_.clear(); stamp_ = 0; }

    private:
        struct Item {
            std::string habitoId;
            int indiceNoDia = 0;
            int avisos = 0;            // avisos ja dados
            int reavisos = 0;          // re-avisos por movimento
            int insistencias = 0;      // insistencias automaticas da medicacao
            uint32_t ultimoMs = 0;
            bool adiado = false;
            uint32_t adiadoAteMs = 0;
            bool forcarInicial = false;   // aviso inicial mesmo atrasado (despertar por timer)
        };
        Item* achar(const OcorrenciaDia& oc);
        Item& garantir(const OcorrenciaDia& oc);

        std::vector<Item> itens_;
        int stamp_ = 0;   // dia do estado (AAAAMMDD): virada do dia zera tudo
    };

} // namespace tempo
