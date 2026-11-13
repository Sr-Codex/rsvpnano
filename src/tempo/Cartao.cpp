#include "tempo/Cartao.h"

namespace tempo::cartao {

    Leitura classificar(Arquivo estado, Arquivo backup, bool temOutrosDados) {
        if (estado == Arquivo::Valido || backup == Arquivo::Valido) return Leitura::Ok;
        if (estado == Arquivo::Ausente && backup == Arquivo::Ausente && !temOutrosDados) return Leitura::Ausente;
        return Leitura::Ilegivel;
    }

    Situacao aposCarregar(bool temCartao, Leitura leitura) {
        if (!temCartao) return Situacao::SemCartao;
        return leitura == Leitura::Ilegivel ? Situacao::Ilegivel : Situacao::Ok;
    }

    bool podeGravar(Situacao s) { return s == Situacao::Ok || s == Situacao::FalhaGravar; }

    Situacao aposGravar(Situacao s, bool gravou) {
        if (!podeGravar(s)) return s;
        return gravou ? Situacao::Ok : Situacao::FalhaGravar;
    }

    const char* aviso(Situacao s) {
        switch (s) {
        case Situacao::SemCartao: return "sem cartão SD · o progresso não é salvo";
        case Situacao::Ilegivel: return "cartão SD com problema · não está salvando";
        case Situacao::FalhaGravar: return "falha ao gravar no cartão SD";
        default: return nullptr;
        }
    }

} // namespace tempo::cartao
