#include "tempo/HabitosVista.h"

#include <algorithm>

namespace tempo {

    TotaisDia totaisDoDia(const Estado& e) {
        TotaisDia t;
        t.total = static_cast<int>(e.dia.ocorrencias.size());
        t.cr = e.dia.habitosCr;
        for (const OcorrenciaDia& oc : e.dia.ocorrencias) {
            if (oc.feita) ++t.feitas;
            else if (oc.status == StatusHabito::Pulado) ++t.puladas;
            else if (oc.status == StatusHabito::Esquecido) ++t.esquecidas;
            else ++t.pendentes;
        }
        return t;
    }

    namespace {
        Cartao cartaoDe(const Estado& e, size_t indice, int minutoAgora) {
            const OcorrenciaDia& oc = e.dia.ocorrencias[indice];
            Cartao c;
            c.indice = indice;
            c.fase = faseDaOcorrencia(oc, minutoAgora);
            c.confirmar2Toques = exigeConfirmacao(oc.tipo);
            const bool med = c.confirmar2Toques;
            switch (c.fase) {
            case FaseOcorrencia::Futura:   // fora da janela: nada de "Feito agora" (cairia como esquecido); pular sim
                c.modo = ModoCartao::Proximo;
                c.minutosAte = oc.minuto >= 0 ? std::max(1, oc.minuto - minutoAgora) : 0;
                c.podePular = !med;
                break;
            case FaseOcorrencia::Agora:
                if (oc.minuto >= 0 && oc.minuto > minutoAgora) {
                    c.modo = ModoCartao::Proximo;
                    c.minutosAte = oc.minuto - minutoAgora;
                } else {
                    c.modo = ModoCartao::Agora;
                    c.atrasoMin = oc.minuto >= 0 ? minutoAgora - oc.minuto : 0;
                }
                c.podeFeito = true;
                c.podePular = true;
                break;
            case FaseOcorrencia::Atrasada:
                c.modo = ModoCartao::Atrasado;
                c.atrasoMin = minutoAgora - oc.minuto;
                c.podeFeito = true;
                c.podePular = true;
                break;
            case FaseOcorrencia::Perdida:
                c.modo = ModoCartao::Perdido;
                c.atrasoMin = minutoAgora - oc.minuto;
                c.podeFeito = med;      // medicacao: ainda vale registrar a hora real; os demais so pulam
                c.podePular = true;
                break;
            case FaseOcorrencia::Feita:
            case FaseOcorrencia::FeitaAtrasada:
                c.modo = ModoCartao::Feito;
                break;
            case FaseOcorrencia::Pulada:
                c.modo = ModoCartao::Pulado;
                break;
            case FaseOcorrencia::Esquecida:
                c.modo = ModoCartao::Esquecido;
                break;
            }
            return c;
        }
    } // namespace

    Cartao montarCartao(const Estado& e, int minutoAgora, std::optional<size_t> selecionado) {
        Cartao c;
        if (e.dia.ocorrencias.empty()) return c;   // SemHabitos
        if (selecionado && *selecionado < e.dia.ocorrencias.size()) return cartaoDe(e, *selecionado, minutoAgora);
        if (const auto proxima = ocorrenciaAgora(e, minutoAgora)) return cartaoDe(e, proxima->indice, minutoAgora);
        c.modo = ModoCartao::DiaCompleto;
        return c;
    }

    std::optional<size_t> alternarSelecao(std::optional<size_t> atual, size_t tocado) {
        if (atual && *atual == tocado) return std::nullopt;
        return tocado;
    }

    int rolagemPara(size_t n, size_t foco, size_t visiveis) {
        if (n <= visiveis) return 0;
        const int maximo = static_cast<int>(n - visiveis);
        return std::clamp(static_cast<int>(foco) - 2, 0, maximo);
    }

    int rolar(int atual, int delta, size_t n, size_t visiveis) {
        const int maximo = n > visiveis ? static_cast<int>(n - visiveis) : 0;
        return std::clamp(atual + delta, 0, maximo);
    }

    std::optional<int> minutoProximoHabito(const Estado& e, int minutoAgora) {
        std::optional<int> melhor;
        for (const OcorrenciaDia& oc : e.dia.ocorrencias) {
            if (oc.feita || oc.status != StatusHabito::Pendente || oc.minuto < 0 || oc.minuto <= minutoAgora) continue;
            if (!melhor || oc.minuto < *melhor) melhor = oc.minuto;
        }
        return melhor;
    }

} // namespace tempo
