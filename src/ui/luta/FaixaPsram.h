#pragma once

// Dono da memoria da Faixa da batalha (etapa c): tudo na PSRAM (a RAM interna e do TLS/Wi-Fi). Aloca uma vez, ao entrar na batalha;
// o App devolve ao sair da tela (liberar). A camada do inimigo e as particulas do golpe final so sao alocadas quando o golpe comeca.

#include "ui/luta/Faixa.h"

namespace ui::luta {

    class FaixaPsram {
    public:
        FaixaPsram() = default;
        ~FaixaPsram() { liberar(); }
        FaixaPsram(const FaixaPsram&) = delete;
        FaixaPsram& operator=(const FaixaPsram&) = delete;

        // Aloca o essencial (rgb, saida, pilula). false = sem PSRAM (o chamador cai no desenho simples).
        bool garantir();
        // Aloca tambem a camada do inimigo + particulas (golpe final). false = sem memoria: o golpe vira so um clarao.
        bool garantirPoeira();
        void liberar();
        bool pronta() const { return faixa_ != nullptr; }
        bool temPoeira() const { return mem_.camada != nullptr; }
        Faixa* faixa() { return faixa_; }

    private:
        Memoria mem_;
        Faixa* faixa_ = nullptr;
        alignas(Faixa) unsigned char armazem_[sizeof(Faixa)];
    };

} // namespace ui::luta
