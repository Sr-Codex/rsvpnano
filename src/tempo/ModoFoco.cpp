#include "tempo/ModoFoco.h"

#include <algorithm>

namespace tempo::modofoco {

    bool podeEscurecer(const Contexto& c) {
        if (c.ajuste == Ajuste::Desligado || !c.telaBatalha) return false;
        if (c.fase != Fase::Focando && c.fase != Fase::HoraExtra && c.fase != Fase::PausaCiclo) return false;
        return !c.segurandoFim && !c.dedoNaTela && !c.avisoHabito && !c.emPe;
    }

    bool noite(int minutoDoDia) {
        if (minutoDoDia < 0) return false;   // hora desconhecida: fator de dia (o mais claro dos dois)
        return minutoDoDia >= kNoiteInicioMin || minutoDoDia < kNoiteFimMin;
    }

    uint8_t brilhoEscurecido(uint8_t brilhoUsuario, int minutoDoDia) {
        const int usuario = std::clamp<int>(brilhoUsuario, 1, 100);
        const int fator = noite(minutoDoDia) ? kFatorNoitePct : kFatorDiaPct;
        int alvo = (usuario * fator + 50) / 100;
        alvo = std::max<int>(alvo, kPisoPct);
        alvo = std::min(alvo, usuario);   // nunca mais claro que o escolhido (usuario abaixo do piso: nao escurece)
        return static_cast<uint8_t>(alvo);
    }

    float Controlador::nivelEm(uint32_t nowMs) const {
        switch (estado_) {
        case Estado::Normal: return 0.0f;
        case Estado::Escuro: return 1.0f;
        case Estado::Escurecendo:
            return std::min(1.0f, static_cast<float>(nowMs - rampaInicioMs_) / static_cast<float>(kDescerMs));
        case Estado::Acordando:
            return std::max(0.0f, rampaNivelInicial_ - static_cast<float>(nowMs - rampaInicioMs_) / static_cast<float>(kSubirMs));
        }
        return 0.0f;
    }

    void Controlador::acordar(uint32_t nowMs) {
        if (estado_ != Estado::Escurecendo && estado_ != Estado::Escuro) return;   // Normal/Acordando: nada a fazer
        rampaNivelInicial_ = nivelEm(nowMs);
        rampaInicioMs_ = nowMs;
        estado_ = rampaNivelInicial_ > 0.0f ? Estado::Acordando : Estado::Normal;
    }

    bool Controlador::toque(uint32_t nowMs) {
        const bool escuro = estado_ == Estado::Escuro
            || (estado_ == Estado::Escurecendo && nivelEm(nowMs) >= kRampaConsomeAPartirDe);
        bool consumir = false;
        if (escuro) {   // 1o toque com a tela escura: so acorda; os botoes armam kArmarMs depois
            consumir = true;
            armando_ = true;
            acordouMs_ = nowMs;
        } else if (armando_ && nowMs - acordouMs_ < kArmarMs) {
            consumir = true;   // toque duplo rapido logo depois de acordar: ainda nao arma
        } else {
            armando_ = false;
        }
        acordar(nowMs);   // rampa no comeco (escuro=false) tambem cancela
        ultimaAtividadeMs_ = nowMs;
        temAtividade_ = true;
        return consumir;
    }

    void Controlador::botao(uint32_t nowMs) {
        if (estado_ == Estado::Escurecendo || estado_ == Estado::Escuro) {
            armando_ = true;   // um toque junto com o botao tambem nao aciona nada
            acordouMs_ = nowMs;
        }
        acordar(nowMs);
        ultimaAtividadeMs_ = nowMs;
        temAtividade_ = true;
    }

    uint8_t Controlador::atualizar(const Contexto& c, uint32_t nowMs) {
        if (!temAtividade_) {
            ultimaAtividadeMs_ = nowMs;
            temAtividade_ = true;
        }
        if (!podeEscurecer(c)) {
            ultimaAtividadeMs_ = nowMs;   // os 10 s so contam com as condicoes valendo ("Voltei"/"Continuar" zeram)
            acordar(nowMs);
        } else if (estado_ == Estado::Normal && nowMs - ultimaAtividadeMs_ >= kOciosoMs) {
            estado_ = Estado::Escurecendo;
            rampaInicioMs_ = nowMs;
        }
        nivel_ = nivelEm(nowMs);
        if (estado_ == Estado::Escurecendo && nivel_ >= 1.0f) estado_ = Estado::Escuro;
        if (estado_ == Estado::Acordando && nivel_ <= 0.0f) estado_ = Estado::Normal;
        if (armando_ && nowMs - acordouMs_ >= kArmarMs) armando_ = false;

        const int usuario = std::clamp<int>(c.brilhoUsuario, 1, 100);
        const int alvo = brilhoEscurecido(static_cast<uint8_t>(usuario), c.minutoDoDia);
        const float valor = static_cast<float>(usuario) - static_cast<float>(usuario - alvo) * nivel_;
        return static_cast<uint8_t>(std::clamp<int>(static_cast<int>(valor + 0.5f), 1, 100));
    }

} // namespace tempo::modofoco
