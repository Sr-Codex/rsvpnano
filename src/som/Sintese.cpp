#include "som/Sintese.h"

#include <algorithm>
#include <cmath>

namespace som {

    namespace {
        constexpr float C4 = 261.63f, G4 = 392.0f, C5 = 523.25f, D5 = 587.33f, E5 = 659.25f, G5 = 783.99f, A5 = 880.0f,
                        B5 = 987.77f, C6 = 1046.5f, E6 = 1318.51f, G6 = 1567.98f;

        // chip.py (estilo A): tudo duty 0,5; o decaimento do estilo A e fixo 1 - (i/n)^2 (campo decay ignorado).
        constexpr Nota kBip[] = {{C6, 90}};
        constexpr Nota kSobeTres[] = {{C5, 70}, {E5, 70}, {G5, 120}};
        // Moeda: decisao do usuario -> sol5 (0,06 s) e do6 (0,22 s) no lugar de si5 -> mi6 (intervalo original nosso).
        constexpr Nota kMoeda[] = {{G5, 60}, {C6, 220}};
        constexpr Nota kSobeQuatro[] = {{C5, 50}, {E5, 50}, {G5, 50}, {C6, 140}};
        constexpr Nota kDoisBipes[] = {{A5, 70}, {0.0f, 40}, {A5, 70}};
        constexpr Nota kDesceAlerta[] = {{G5, 80}, {E5, 80}, {C5, 140}};

        // chip2.py (estilo B)
        constexpr Nota kVitoria[] = {{C5, 100}, {E5, 100}, {G5, 100}, {C6, 100}, {0.0f, 40}, {G5, 80},
                                     {C6, 450, 0.5f, 1.2f, 0.01f}};
        constexpr Nota kFaseCompleta[] = {{G4, 90}, {C5, 90}, {E5, 90}, {G5, 90}, {C6, 90}, {E6, 90},
                                          {G6, 400, 0.5f, 1.0f}};
        constexpr Nota kPowerUp[] = {{400.0f, 550, 0.25f, 0.8f, 0.0f, 1600.0f}, {1600.0f, 250, 0.25f, 2.0f}};
        constexpr Nota kJingle[] = {{E5, 120}, {0.0f, 30}, {E5, 120}, {0.0f, 30}, {G5, 120}, {0.0f, 30},
                                    {C6, 120}, {0.0f, 30}, {B5, 120}, {C6, 500, 0.5f, 1.0f, 0.012f}};
        constexpr Nota kAlertaLongo[] = {{A5, 120, 0.25f}, {0.0f, 60}, {A5, 120, 0.25f}, {0.0f, 60},
                                         {E6, 120, 0.25f}, {0.0f, 60}, {A5, 400, 0.25f, 1.2f}};
        constexpr Nota kSuaveDescendo[] = {{C6, 140, 0.125f}, {G5, 140, 0.125f}, {E5, 140, 0.125f}, {C5, 140, 0.125f},
                                           {G4, 140, 0.125f}, {C4, 500, 0.125f, 1.0f}};

        constexpr Som kSons[kNumSons] = {
            {"Bip", Estilo::A, kBip},
            {"SobeTres", Estilo::A, kSobeTres},
            {"Moeda", Estilo::A, kMoeda},
            {"SobeQuatro", Estilo::A, kSobeQuatro},
            {"DoisBipes", Estilo::A, kDoisBipes},
            {"DesceAlerta", Estilo::A, kDesceAlerta},
            {"Vitoria", Estilo::B, kVitoria},
            {"FaseCompleta", Estilo::B, kFaseCompleta},
            {"PowerUp", Estilo::B, kPowerUp},
            {"Jingle", Estilo::B, kJingle},
            {"AlertaLongo", Estilo::B, kAlertaLongo},
            {"SuaveDescendo", Estilo::B, kSuaveDescendo},
        };

        constexpr float kPi2 = 6.28318530717958647692f;
        constexpr float kVolumeA = 0.25f, kVolumeB = 0.22f;
        constexpr uint32_t kMsParaAmostras = kTaxaHz / 1000;   // 16

        // Ataque da referencia (50/60 amostras a 44,1 kHz) reescalado para 16 kHz.
        constexpr uint32_t ataqueDe(Estilo e) {
            const uint32_t original = e == Estilo::A ? 50 : 60;
            return static_cast<uint32_t>(std::lround(double(original) * kTaxaHz / 44100.0));
        }
    } // namespace

    const Som& definicao(SomId id) {
        return kSons[std::min<size_t>(static_cast<size_t>(id), kNumSons - 1)];
    }

    const char* nome(SomId id) { return definicao(id).nome; }

    uint32_t totalAmostras(SomId id) {
        uint32_t total = 0;
        for (const Nota& n : definicao(id).notas) total += uint32_t(n.ms) * kMsParaAmostras;
        return total;
    }

    uint32_t duracaoMs(SomId id) {
        uint32_t total = 0;
        for (const Nota& n : definicao(id).notas) total += n.ms;
        return total;
    }

    float ganhoDoVolume(Volume v) {
        switch (v) {
        case Volume::Baixo: return 0.4f;
        case Volume::Alto: return 1.6f;   // pico ~0,4 do fundo de escala: o nivel do bipe antigo (12000/32768)
        default: return 1.0f;
        }
    }

    void Sintetizador::iniciar(SomId id, float ganho) {
        id_ = id;
        som_ = &definicao(id);
        ganho_ = ganho;
        nota_ = 0;
        terminou_ = som_->notas.empty();
        if (!terminou_) abrirNota();
    }

    void Sintetizador::abrirNota() {
        const Nota& nota = som_->notas[nota_];
        n_ = uint32_t(nota.ms) * kMsParaAmostras;
        i_ = 0;
        fase_ = 0.0;
        ataque_ = ataqueDe(som_->estilo);
    }

    size_t Sintetizador::gerar(int16_t* saida, size_t max) {
        size_t escritas = 0;
        while (escritas < max && !terminou_) {
            if (i_ >= n_) {   // proxima nota
                if (++nota_ >= som_->notas.size()) {
                    terminou_ = true;
                    break;
                }
                abrirNota();
                continue;
            }
            const Nota& nota = som_->notas[nota_];
            float amostra = 0.0f;
            if (nota.hz > 0.0f) {
                float f = nota.hz;
                const float t = float(i_) / float(kTaxaHz);
                if (nota.slideHz != 0.0f) f = nota.hz + (nota.slideHz - nota.hz) * (float(i_) / float(n_));
                if (nota.vib != 0.0f) f *= 1.0f + nota.vib * std::sin(kPi2 * 6.0f * t);
                double fase;
                if (som_->estilo == Estilo::A) {   // chip.py: fase de t*freq (sem vibrato nesse estilo)
                    fase = fase_;
                    fase_ += double(f) / kTaxaHz;
                } else {                            // chip2.py: incrementa antes de usar
                    fase_ += double(f) / kTaxaHz;
                    fase_ -= std::floor(fase_);
                    fase = fase_;
                }
                if (som_->estilo == Estilo::A) fase -= std::floor(fase);
                const float quadrada = fase < nota.duty ? 1.0f : -1.0f;
                float env = 1.0f;
                const float x = float(i_) / float(n_);
                if (som_->estilo == Estilo::A) {   // chip.py: env = 1 - (i/n)**2  ("decaimento rapido no fim")
                    env = 1.0f - x * x;
                } else if (nota.decay > 0.0f) {    // chip2.py: env = (1 - i/n) ** decay
                    const float resto = 1.0f - x;
                    env = nota.decay == 2.0f ? resto * resto : nota.decay == 1.0f ? resto : std::pow(resto, nota.decay);
                }
                if (i_ < ataque_) env *= float(i_) / float(ataque_);
                amostra = quadrada * (som_->estilo == Estilo::A ? kVolumeA : kVolumeB) * env * ganho_;
            }
            amostra = std::clamp(amostra, -1.0f, 1.0f);
            saida[escritas++] = static_cast<int16_t>(amostra * 32767.0f);
            ++i_;
        }
        return escritas;
    }

} // namespace som
