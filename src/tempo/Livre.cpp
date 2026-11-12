#include "tempo/Livre.h"

#include <algorithm>
#include <cstdio>

namespace tempo::livre {

    EntradaBatalha entrada(const Cronometro& c, Categoria categoria) {
        EntradaBatalha e;
        e.livre = true;
        e.categoria = categoria;
        e.minutosFocados = static_cast<int>(c.focadoMs / 60000);
        e.minutosPlanejados = e.minutosFocados;
        e.minutosDistraidos = static_cast<int>(c.distraidoMs / 60000);
        e.segundosDistraidos = c.distraidoSeg();
        e.numDistracoes = c.numDistracoes;
        e.maiorDistracaoMin = std::max(c.maiorDistracaoMin, static_cast<int>(c.distracaoAtualMs / 60000));
        return e;
    }

    Blocos blocos(uint64_t focadoMs) {
        Blocos b;
        const uint64_t hora = focadoMs / 3600000ULL;
        const uint64_t naHora = focadoMs % 3600000ULL;
        b.hora = static_cast<int>(hora) + 1;
        const uint64_t bloco = static_cast<uint64_t>(kBlocoMin) * 60000ULL;
        b.cheios = static_cast<int>(naHora / bloco);
        b.parcial = double(naHora % bloco) / double(bloco);
        return b;
    }

    std::string rotuloHora(int hora) { return std::to_string(hora) + "ª h"; }

    std::vector<ParteTempo> partesDoTempo(uint64_t focadoMs) {
        const uint64_t min = focadoMs / 60000ULL;
        if (min < 60) return {{std::to_string(min), true}, {"min", false}};
        char r[8];
        std::snprintf(r, sizeof r, "%02d", int(min % 60));
        return {{std::to_string(min / 60), true}, {"h", false}, {r, true}};
    }

    std::string textoTempo(uint64_t focadoMs) {
        const uint64_t min = focadoMs / 60000ULL;
        if (min < 60) return std::to_string(min) + " min";
        char t[24];
        std::snprintf(t, sizeof t, "%dh%02d", int(min / 60), int(min % 60));
        return t;
    }

    std::string textoRelogio(uint64_t ms) {
        const uint64_t s = ms / 1000ULL;
        char t[24];
        if (s < 3600) std::snprintf(t, sizeof t, "%d:%02d", int(s / 60), int(s % 60));
        else std::snprintf(t, sizeof t, "%d:%02d:%02d", int(s / 3600), int(s / 60 % 60), int(s % 60));
        return t;
    }

    std::string textoNaoConta(uint64_t focadoMs) {
        const uint64_t s = focadoMs / 1000ULL;
        char t[40];
        std::snprintf(t, sizeof t, "%d:%02d não conta", int(s / 60), int(s % 60));
        return t;
    }

    std::string cabecalhoRodando(int minutoDesdeDoDia) {
        if (minutoDesdeDoDia < 0) return "LIVRE";
        const int m = ((minutoDesdeDoDia % 1440) + 1440) % 1440;
        char t[40];
        std::snprintf(t, sizeof t, "LIVRE · DESDE %02d:%02d", m / 60, m % 60);
        return t;
    }

    AvisoOcioso::Evento AvisoOcioso::avancar(uint64_t deltaMs, bool contando) {
        if (!contando) return Evento::Nada;
        if (ativo_) {
            esperaMs_ += deltaMs;
            return esperaMs_ >= kRespostaMs ? Evento::Concluir : Evento::Nada;
        }
        ociosoMs_ += deltaMs;
        if (ociosoMs_ >= kLimiteMs) {
            ativo_ = true;
            esperaMs_ = 0;
            return Evento::Avisar;
        }
        return Evento::Nada;
    }

    void AvisoOcioso::tocou() {
        ociosoMs_ = 0;
        esperaMs_ = 0;
        ativo_ = false;
    }

} // namespace tempo::livre
