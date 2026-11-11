#include "tempo/Lembretes.h"

#include <algorithm>

#include "tempo/HabitosVista.h"

namespace tempo {

    bool DetectorPegada::alimentar(uint32_t agoraMs, float variacaoG) {
        // histerese: so rearma depois de ficar quieto
        if (variacaoG < kLimiteQuieto) {
            if (!quieto_) {
                quieto_ = true;
                quietoDesdeMs_ = agoraMs;
            }
            if (!armado_ && agoraMs - quietoDesdeMs_ >= kQuietoRearmaMs) {
                armado_ = true;
                picos_ = 0;
            }
        } else {
            quieto_ = false;
        }
        if (!armado_) return false;
        if (variacaoG < kLimiteMovimento) {
            if (picos_ > 0 && agoraMs - primeiroPicoMs_ > kJanelaPegadaMs) picos_ = 0;
            return false;
        }
        if (picos_ == 0 || agoraMs - primeiroPicoMs_ > kJanelaPegadaMs) {
            picos_ = 1;
            primeiroPicoMs_ = agoraMs;
            return false;
        }
        if (++picos_ >= kPicosParaPegar) {
            armado_ = false;
            picos_ = 0;
            quieto_ = false;
            return true;
        }
        return false;
    }

    void DetectorPegada::zerar() {
        picos_ = 0;
        armado_ = true;
        quieto_ = false;
    }

    // ------------------------------------------------------------------ agenda
    AgendaLembretes::Item* AgendaLembretes::achar(const OcorrenciaDia& oc) {
        for (Item& it : itens_)
            if (it.habitoId == oc.habitoId && it.indiceNoDia == oc.indice) return &it;
        return nullptr;
    }

    AgendaLembretes::Item& AgendaLembretes::garantir(const OcorrenciaDia& oc) {
        if (Item* it = achar(oc)) return *it;
        Item novo;
        novo.habitoId = oc.habitoId;
        novo.indiceNoDia = oc.indice;
        itens_.push_back(std::move(novo));
        return itens_.back();
    }

    void AgendaLembretes::adiar(const Estado& e, size_t indice, uint32_t agoraMs, uint32_t adiarMs) {
        if (indice >= e.dia.ocorrencias.size()) return;
        Item& it = garantir(e.dia.ocorrencias[indice]);
        it.adiado = true;
        it.adiadoAteMs = agoraMs + adiarMs;
    }

    void AgendaLembretes::jaAvisada(const Estado& e, size_t indice, uint32_t agoraMs) {
        if (indice >= e.dia.ocorrencias.size()) return;
        stamp_ = stampDe(e.dia.data);
        Item& it = garantir(e.dia.ocorrencias[indice]);
        it.avisos = std::max(it.avisos, 1);
        it.ultimoMs = agoraMs;
        it.adiado = false;
    }

    void AgendaLembretes::resolvida(const Estado& e, size_t indice) {
        if (indice >= e.dia.ocorrencias.size()) return;
        const OcorrenciaDia& oc = e.dia.ocorrencias[indice];
        std::erase_if(itens_, [&](const Item& it) { return it.habitoId == oc.habitoId && it.indiceNoDia == oc.indice; });
    }

    void AgendaLembretes::semearInsistenciaMedicacao(const Estado& e, int minutoAgora, uint32_t agoraMs, int insistenciasFeitas) {
        stamp_ = stampDe(e.dia.data);
        for (const OcorrenciaDia& oc : e.dia.ocorrencias) {
            if (oc.feita || oc.status != StatusHabito::Pendente || oc.minuto < 0 || oc.tipo != TipoHabito::Medicacao) continue;
            if (minutoAgora < oc.minuto) continue;
            const FaseOcorrencia fase = faseDaOcorrencia(oc, minutoAgora);
            if (fase != FaseOcorrencia::Agora && fase != FaseOcorrencia::Atrasada) continue;
            Item& it = garantir(oc);
            it.avisos = 1;
            it.reavisos = 0;
            it.insistencias = std::max(0, insistenciasFeitas - 1);   // o avaliar() soma a desta
            it.ultimoMs = agoraMs - kInsistenciaMedicacaoMs;          // vence agora
            it.adiado = false;
        }
    }

    void AgendaLembretes::forcarAvisoInicial(const Estado& e, int minutoAlvo) {
        stamp_ = stampDe(e.dia.data);
        for (const OcorrenciaDia& oc : e.dia.ocorrencias) {
            if (oc.feita || oc.status != StatusHabito::Pendente || oc.minuto != minutoAlvo) continue;
            Item& it = garantir(oc);
            if (it.avisos == 0) it.forcarInicial = true;
        }
    }

    std::optional<Aviso> AgendaLembretes::avaliar(const Estado& e, int minutoAgora, uint32_t agoraMs, bool pegouAgora) {
        const int stamp = stampDe(e.dia.data);
        if (stamp != stamp_) {   // virada do dia: estado novo
            itens_.clear();
            stamp_ = stamp;
        }
        std::optional<Aviso> escolhido;
        int vencidos = 0;
        for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i) {
            const OcorrenciaDia& oc = e.dia.ocorrencias[i];
            if (oc.feita || oc.status != StatusHabito::Pendente || oc.minuto < 0) continue;   // so com horario e pendente
            if (minutoAgora < oc.minuto) continue;                                             // ainda nao e a hora
            const FaseOcorrencia fase = faseDaOcorrencia(oc, minutoAgora);
            if (fase != FaseOcorrencia::Agora && fase != FaseOcorrencia::Atrasada) continue;  // perdida: nao insiste mais

            Item& it = garantir(oc);
            const bool med = oc.tipo == TipoHabito::Medicacao;
            bool disparar = false;
            bool repeticao = false;
            if (it.adiado) {
                if (static_cast<int32_t>(agoraMs - it.adiadoAteMs) >= 0) {   // "Ainda nao" venceu
                    disparar = true;
                    repeticao = true;
                    it.adiado = false;
                }
            } else if (it.avisos == 0) {
                if (it.forcarInicial || minutoAgora - oc.minuto <= kToleranciaAvisoInicialMin) {
                    disparar = true;   // aviso inicial (1x), na hora
                } else {
                    // Ligou/abriu tarde (ex.: placa desligada na hora): nao apita do nada; fica armada para o re-aviso por movimento.
                    it.avisos = 1;
                    it.ultimoMs = agoraMs - kIntervaloReavisoMs;
                }
            } else {
                const uint32_t desde = agoraMs - it.ultimoMs;
                if (pegouAgora && desde >= kIntervaloReavisoMs && it.reavisos < kMaxReavisos) {
                    disparar = true;
                    repeticao = true;
                    ++it.reavisos;
                } else if (med && desde >= kInsistenciaMedicacaoMs && it.insistencias < kMaxInsistenciasMedicacao) {
                    disparar = true;
                    repeticao = true;
                    ++it.insistencias;
                }
            }
            if (!disparar) continue;
            ++it.avisos;
            it.ultimoMs = agoraMs;
            if (!escolhido) {
                Aviso a;
                a.indice = i;
                a.nome = oc.nome;
                a.minuto = oc.minuto;
                a.tipo = oc.tipo;
                a.repeticao = repeticao;
                escolhido = std::move(a);
            } else {
                ++vencidos;
            }
        }
        if (escolhido) escolhido->outros = vencidos;
        return escolhido;
    }

} // namespace tempo
