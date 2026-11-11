#include "som/Regras.h"

namespace som {

    Classe classeDe(SomId id) {
        switch (id) {
        case SomId::Bip: return Classe::Interface;
        case SomId::DoisBipes: return Classe::Lembrete;
        case SomId::AlertaLongo: return Classe::Medicacao;
        default: return Classe::Jogo;
        }
    }

    bool emSilencio(const Config& config, int minutoDoDia) {
        if (!config.silencioAtivo || minutoDoDia < 0) return false;
        const int inicio = config.silencioInicioMin, fim = config.silencioFimMin;
        if (inicio == fim) return false;
        if (inicio < fim) return minutoDoDia >= inicio && minutoDoDia < fim;
        return minutoDoDia >= inicio || minutoDoDia < fim;
    }

    Decisao decidir(SomId id, const Config& config, int minutoDoDia) {
        Decisao d;
        d.ganho = ganhoDoVolume(config.volume);
        const Classe classe = classeDe(id);
        if (classe == Classe::Medicacao) {   // medicacao toca sempre
            d.tocar = true;
        } else if (classe == Classe::Jogo && !config.sonsJogo) {
            d.motivo = Motivo::SonsDeJogoDesligados;
        } else if (emSilencio(config, minutoDoDia)) {   // lembrete comum, jogo e bip respeitam o silencio
            d.motivo = Motivo::Silencio;
        } else {
            d.tocar = true;
        }
        return d;
    }

    int valorDoEvento(SomId id) {
        switch (id) {
        case SomId::Jingle: return 100;
        case SomId::PowerUp: return 90;
        case SomId::FaseCompleta: return 80;
        case SomId::Vitoria: return 70;
        case SomId::Moeda: return 60;
        case SomId::SuaveDescendo: return 50;
        case SomId::SobeQuatro: return 40;
        case SomId::SobeTres: return 30;
        case SomId::DesceAlerta: return 20;
        default: return 10;
        }
    }

    std::optional<SomId> escolher(std::initializer_list<SomId> candidatos) {
        std::optional<SomId> melhor;
        for (const SomId id : candidatos)
            if (!melhor || valorDoEvento(id) > valorDoEvento(*melhor)) melhor = id;
        return melhor;
    }

    void Arbitro::guardar(const Pedido& pedido) {
        if (!aguardando_ || pedido.classe >= aguardando_->classe) aguardando_ = pedido;
    }

    Arbitro::Acao Arbitro::pedir(const Pedido& pedido) {
        if (!atual_) {
            atual_ = pedido;
            return Acao::TocarAgora;
        }
        if (pedido.classe > atual_->classe) {
            if (atual_->classe == Classe::Lembrete) guardar(*atual_);   // nao perde o lembrete interrompido
            atual_ = pedido;
            return Acao::TocarAgora;
        }
        if (pedido.classe == atual_->classe) {
            if (pedido.classe == Classe::Jogo) {
                atual_ = pedido;
                return Acao::TocarAgora;
            }
            if (pedido.classe == Classe::Interface || pedido.som == atual_->som) return Acao::Descartar;
            guardar(pedido);
            return Acao::Aguardar;
        }
        if (pedido.classe >= Classe::Lembrete) {
            if (aguardando_ && aguardando_->som == pedido.som) return Acao::Descartar;
            guardar(pedido);
            return Acao::Aguardar;
        }
        return Acao::Descartar;
    }

    std::optional<Pedido> Arbitro::concluir() {
        atual_ = aguardando_;
        aguardando_.reset();
        return atual_;
    }

    void Arbitro::parar() {
        atual_.reset();
        aguardando_.reset();
    }

} // namespace som
