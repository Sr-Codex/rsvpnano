#include "tempo/Despertador.h"

#include <algorithm>

namespace tempo {

    namespace {

        struct Candidato {
            int64_t alvo = 0;
            OrigemDespertar origem = OrigemDespertar::Nenhuma;
            bool amanha = false;
        };

        void considerar(std::optional<Candidato>& melhor, const Candidato& c) {
            if (!melhor || c.alvo < melhor->alvo) melhor = c;
        }

    } // namespace

    const char* nomePorqueDesliga(PorqueDesliga p) {
        switch (p) {
            case PorqueDesliga::Dorme: return "dorme";
            case PorqueDesliga::Manual: return "manual";
            case PorqueDesliga::RelogioInvalido: return "relogio invalido";
            case PorqueDesliga::SemLembrete: return "sem lembrete";
            case PorqueDesliga::ForaDoHorizonte: return "fora do horizonte";
            case PorqueDesliga::BateriaBaixa: return "bateria baixa";
            case PorqueDesliga::LimiteDeCiclos: return "limite de ciclos";
        }
        return "?";
    }

    std::optional<int> primeiroLembreteDoDia(const ListaHabitos& lista, const Data& dia) {
        std::optional<int> melhor;
        for (const OcorrenciaDia& oc : gerarOcorrencias(lista, dia))
            if (oc.minuto >= 0 && (!melhor || oc.minuto < *melhor)) melhor = oc.minuto;
        return melhor;
    }

    DecisaoDespertador decidirDespertador(const EntradaDespertador& in) {
        DecisaoDespertador d;
        if (in.motivo == MotivoDesligar::Manual) {
            d.porque = PorqueDesliga::Manual;
            return d;
        }
        if (!in.relogioValido || in.epochAgora <= 0) {
            d.porque = PorqueDesliga::RelogioInvalido;
            return d;
        }
        const bool bateriaOk = in.carregando || (in.bateriaPercent >= kBateriaMinimaPercent);
        if (!bateriaOk) {
            d.porque = PorqueDesliga::BateriaBaixa;
            return d;
        }

        const bool limite = in.ciclosSemInteracao >= kMaxCiclosSemInteracao;
        const int minutoAgora = in.segundoDoDia / 60;
        std::optional<Candidato> melhor;
        bool viuAmanhaBloqueado = false;
        const bool estadoDeHoje = in.estado != nullptr && in.estado->dia.data == in.hoje;

        if (estadoDeHoje) {
            for (const OcorrenciaDia& oc : in.estado->dia.ocorrencias) {
                if (oc.feita || oc.status != StatusHabito::Pendente || oc.minuto < 0) continue;
                // 1) horario ainda por vir hoje
                const int64_t segAlvo = static_cast<int64_t>(oc.minuto) * 60;
                if (segAlvo > in.segundoDoDia)
                    considerar(melhor, {in.epochAgora + (segAlvo - in.segundoDoDia), OrigemDespertar::Lembrete, false});
                // 2) insistencia da medicacao (so na janela/atrasada, sem passar da virada do dia)
                if (oc.tipo == TipoHabito::Medicacao && in.insistenciasMedicacao < kMaxInsistenciasMedicacao
                    && minutoAgora >= oc.minuto) {
                    const FaseOcorrencia faseAgora = faseDaOcorrencia(oc, minutoAgora);
                    if (faseAgora == FaseOcorrencia::Agora || faseAgora == FaseOcorrencia::Atrasada) {
                        const int64_t base = in.ultimoAvisoMedicacaoEpoch.value_or(in.epochAgora);
                        const int64_t alvo = std::max(base + kInsistenciaMedicacaoMs / 1000, in.epochAgora + kMinimoSonoS + kAntecedenciaS);
                        const int64_t segNoAlvo = in.segundoDoDia + (alvo - in.epochAgora);
                        if (segNoAlvo < 86400) {
                            const FaseOcorrencia faseAlvo = faseDaOcorrencia(oc, static_cast<int>(segNoAlvo / 60));
                            if (faseAlvo == FaseOcorrencia::Agora || faseAlvo == FaseOcorrencia::Atrasada)
                                considerar(melhor, {alvo, OrigemDespertar::InsistenciaMedicacao, false});
                        }
                    }
                }
            }
        }
        // 3) primeiro horario de amanha: so se nada mais hoje (o dia acabou para lembretes) e sem limite de ciclos
        if (!melhor && in.primeiroLembreteAmanhaMin) {
            if (limite) {
                viuAmanhaBloqueado = true;
            } else {
                const int64_t seg = static_cast<int64_t>(86400 - in.segundoDoDia) + static_cast<int64_t>(*in.primeiroLembreteAmanhaMin) * 60;
                melhor = Candidato{in.epochAgora + seg, OrigemDespertar::Lembrete, true};
            }
        }

        if (!melhor) {
            d.porque = viuAmanhaBloqueado ? PorqueDesliga::LimiteDeCiclos : PorqueDesliga::SemLembrete;
            return d;
        }
        if (melhor->alvo - in.epochAgora > kHorizonteMaxS) {
            d.porque = PorqueDesliga::ForaDoHorizonte;
            return d;
        }
        d.dormir = true;
        d.porque = PorqueDesliga::Dorme;
        d.alvoEpoch = melhor->alvo;
        d.origem = melhor->origem;
        d.acordarEpoch = std::max(melhor->alvo - kAntecedenciaS, in.epochAgora + kMinimoSonoS);
        return d;
    }

} // namespace tempo
