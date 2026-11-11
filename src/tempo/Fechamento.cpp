#include "tempo/Fechamento.h"

#include <algorithm>

namespace tempo {

    bool rodadaPedeReflexao(const Ciclo& c) {
        if (!c.ativo) return true;   // sessao unica ou ciclo encerrado pelo usuario
        return c.passo == PassoCiclo::Foco && c.rodada >= kCicloRodadas;
    }

    int humorParaLog(int humor) { return humor >= 0 && humor <= 4 ? humor + 1 : 0; }
    int distraiuParaLog(int distraiu) { return distraiu >= 0 && distraiu <= 2 ? distraiu + 1 : 0; }

    ProximoPasso proximoPassoDoSaque(const Ciclo& c) {
        if (!c.ativo || c.passo != PassoCiclo::Foco) return ProximoPasso::VoltarInicio;
        return c.rodada < kCicloRodadas ? ProximoPasso::PausaCurta : ProximoPasso::PausaLonga;
    }

    int minutosProximoPasso(ProximoPasso p) {
        switch (p) {
        case ProximoPasso::PausaCurta: return kCicloPausaMin;
        case ProximoPasso::PausaLonga: return kCicloPausaLongaMin;
        default: return 0;
        }
    }

    AssinaturaBatalha assinaturaDoEstado(const Estado& e) {
        AssinaturaBatalha a;
        a.saldo = e.saldo;
        a.cofre = e.cofre;
        a.xpTotal = e.xpTotal;
        a.pontosAtributo = e.pontosAtributo;
        a.streak = e.streak;
        a.coracoesMeios = e.coracoesMeios;
        a.dia = e.dia.data;
        a.diaFechado = e.dia.fechado;
        a.eventos = e.dia.eventos.size();
        a.crGanho = e.dia.crGanho;
        a.xpGanho = e.dia.xpGanho;
        a.habitosCr = e.dia.habitosCr;
        a.fochoPagoMin = e.dia.fochoPagoMin;
        a.fochoValidoMin = e.dia.fochoValidoMin;
        a.meditacaoMin = e.dia.meditacaoMin;
        a.ocorrenciasFeitas = size_t(std::count_if(e.dia.ocorrencias.begin(), e.dia.ocorrencias.end(),
                                                   [](const OcorrenciaDia& o) { return o.status != StatusHabito::Pendente; }));
        return a;
    }

    InstanteBatalha capturarBatalha(const Estado& e) {
        InstanteBatalha i;
        i.antes = std::make_shared<const Estado>(e);
        return i;
    }

    void selarBatalha(InstanteBatalha& instante, const Estado& e) {
        if (!instante.antes) return;
        instante.depois = assinaturaDoEstado(e);
        instante.valido = true;
    }

    bool podeDesfazerBatalha(const Estado& e, const InstanteBatalha& instante) {
        if (!instante.valido || !instante.antes) return false;
        if (e.dia.fechado || !(e.dia.data == instante.antes->dia.data)) return false;
        return assinaturaDoEstado(e) == instante.depois;   // algo mais mexeu no estado: nao arrisca
    }

    bool desfazerBatalha(Estado& e, const InstanteBatalha& instante) {
        if (!podeDesfazerBatalha(e, instante)) return false;
        e = *instante.antes;
        return true;
    }

} // namespace tempo
