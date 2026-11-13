#include "tempo/CenaFocoEstado.h"

#include <algorithm>
#include <cmath>

namespace tempo::cenafoco {

    int nosNovos(int minutosPlanejados) {
        const int m = std::max(0, minutosPlanejados);
        return std::clamp((m + kMinutosPorNo / 2) / kMinutosPorNo, 1, kNos);   // round(m / 5), meio para cima
    }

    int nosAntes(int minutosFocoAntes, int novos) {
        const int livres = kNos - std::clamp(novos, 1, kNos);
        return std::clamp(std::max(0, minutosFocoAntes) / kMinutosPorNo, 0, livres);
    }

    Quadro calcular(const Entrada& e, const uint8_t (&ordem)[kNos]) {
        Quadro q;
        q.novos = nosNovos(e.minutosPlanejados);
        q.antes = nosAntes(e.minutosFocoAntes, q.novos);
        const double p = e.horaExtra ? 1.0 : std::clamp(std::isfinite(e.p) ? e.p : 0.0, 0.0, 1.0);
        q.fundo = e.horaExtra ? (e.vitoria ? Fundo::Vitoria : Fundo::Amanhecer) : Fundo::Noite;
        q.revelar = q.fundo == Fundo::Noite;
        q.mapa = q.fundo != Fundo::Vitoria;
        q.cheio = kCheioMinimo + (1.0f - kCheioMinimo) * static_cast<float>(p);
        q.tomados = std::min(q.novos, static_cast<int>(std::floor(p * q.novos + 1e-6)));
        for (int i = 0; i < q.antes; ++i) {
            const int idx = ordem[i] % kNos;
            q.nos[idx] = {TipoNo::Antes, 1.0f};
        }
        for (int j = 0; j < q.novos; ++j) {
            const int idx = ordem[(q.antes + j) % kNos] % kNos;
            const double g = std::clamp(p * q.novos - j, 0.0, 1.0);
            q.nos[idx] = {TipoNo::Novo, static_cast<float>(g)};
        }
        return q;
    }

    int linhasModo0(int alt, float cheio) {
        if (alt <= 0) return 0;
        const float c = std::clamp(std::isfinite(cheio) ? cheio : 0.0f, 0.0f, 1.0f);
        return std::clamp(static_cast<int>(std::lround(static_cast<double>(alt) * c)), 0, alt);
    }

    int linhasModo1(int alt, int y0x10, int passox10, int cabecalho, int linhasMax, int tomados) {
        if (alt <= 0) return 0;
        const int linhas = cabecalho + std::clamp(tomados, 0, std::max(0, linhasMax));
        return std::clamp((y0x10 + linhas * passox10 + 5) / 10, 0, alt);
    }

    int cursorModo1Topox10(int y0x10, int passox10, int cabecalho, int linhasMax, int tomados) {
        const int linha = cabecalho + std::clamp(tomados, 0, std::max(0, linhasMax));
        return y0x10 + linha * passox10 + 4;   // a arte desenha o cursor 0,4 px abaixo do topo da linha
    }

    void Roteiro::atualizar(bool focando, bool horaExtra, bool cenaNaTela, uint32_t nowMs) {
        if (focandoAntes_ && horaExtra && cenaNaTela) {   // o inimigo zerou com a cena na tela
            ativa_ = true;
            inicioMs_ = nowMs;
        }
        if (!horaExtra || !cenaNaTela) ativa_ = false;    // acordou (ou saiu da hora extra): quem volta ve o amanhecer
        if (ativa_ && nowMs - inicioMs_ >= kVitoriaMs) ativa_ = false;
        focandoAntes_ = focando;
    }

    bool Roteiro::vitoria(uint32_t nowMs) const {
        return ativa_ && nowMs - inicioMs_ < kVitoriaMs;
    }

} // namespace tempo::cenafoco
