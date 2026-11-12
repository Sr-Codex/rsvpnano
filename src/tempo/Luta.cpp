#include "tempo/Luta.h"

#include <algorithm>
#include <cmath>

namespace tempo::luta {

    double frente(const Cronometro& c) {
        const double foco = double(c.focadoMs);
        const double total = foco + double(c.restanteMs());
        if (total <= 0.0) return 0.0;
        return std::clamp(foco / total, 0.0, 1.0);
    }

    double voltaExtra(const Cronometro& c) {
        const int64_t plano = c.planoMs();
        const int64_t extra = c.overtimeMs();
        if (plano <= 0 || extra <= 0) return 0.0;
        return std::fmod(double(extra) / double(plano), 1.0);
    }

    Modo modoDe(const EntradaModo& e) {
        if (e.descanso) return Modo::PausaCiclo;
        if (e.pausada) return Modo::Pausado;
        if (e.horaExtra) return e.golpeFinal ? Modo::GolpeFinal : Modo::HoraExtra;
        if (e.distraido) return Modo::Distraido;
        if (e.fimSegurando) return Modo::FimSegurando;
        return Modo::Focando;
    }

    Efeitos efeitos(Modo m) {
        Efeitos e;
        switch (m) {
        case Modo::Focando:
            e.choque = e.faiscasInimigo = e.raios = e.animada = true;
            break;
        case Modo::FimSegurando:   // a luta "segura a respiracao": cores ~30 % apagadas, choque sem faiscas
            e.apagado = 0.30f;
            e.choque = true;
            break;
        case Modo::Distraido:      // laranja apagado, clarao carmim, faiscas para a esquerda e setas
            e.apagado = 0.35f;     // so o laranja (o carmim segue cheio)
            e.choque = e.frenteCarmim = e.faiscasRecuo = e.setas = e.animada = true;
            break;
        case Modo::Pausado:        // tregua: tudo ~50 % apagado, divisoria fria, nada se mexe
            e.apagado = 0.50f;
            e.divisoria = true;
            break;
        case Modo::PausaCiclo:     // mesma linguagem da tregua, em verde-agua
            e.verdeAgua = true;
            e.divisoria = true;
            break;
        case Modo::GolpeFinal:
            e.choque = e.poeira = e.animada = true;
            break;
        case Modo::HoraExtra:
            e.azul = e.choque = e.faiscasInimigo = e.animada = true;   // faiscas (azuis) na ponta do extra
            break;
        case Modo::Resultado:      // saque: a barra parada onde a luta terminou; nada se mexe
            break;
        }
        return e;
    }

    float progressoGolpe(uint32_t msDesdeZerar) {
        if (msDesdeZerar >= kGolpeFinalMs) return kProgGolpe;
        return kProgGolpe * float(msDesdeZerar) / float(kGolpeFinalMs);
    }

    float frenteDoGolpe(float prog) { return std::clamp(prog / kProgFrente, 0.0f, 1.0f); }

    Particula criarParticula(const Semente& s, Rng& rng, const Vento& v) {
        Particula p;
        p.x = s.x;
        p.y = s.y;
        const float n1 = rng.unidade(), n2 = rng.unidade(), n3 = rng.unidade();
        p.t0 = std::clamp(s.f, 0.0f, 1.0f) * kProgFrente + n1 * kRuidoLiberacao;
        p.vx = v.vx0 + (v.vx1 - v.vx0) * n2;
        p.vy = v.vy0 + (v.vy1 - v.vy0) * n3;
        p.r = s.r;
        p.g = s.g;
        p.b = s.b;
        return p;
    }

    namespace {
        inline uint8_t misturar8(uint8_t a, float b, float t) { return uint8_t(std::clamp(a + (b - a) * t, 0.0f, 255.0f) + 0.5f); }
    } // namespace

    PontoPo estadoParticula(const Particula& p, float prog) {
        PontoPo o;
        o.x = p.x;
        o.y = p.y;
        o.r = p.r;
        o.g = p.g;
        o.b = p.b;
        if (prog <= p.t0) {
            o.a = 1.0f;
            return o;
        }
        const float idade = prog - p.t0;
        o.solta = true;
        o.a = std::max(0.0f, 1.0f - idade / kVidaParticula);
        if (o.a <= 0.0f) return o;
        o.x = p.x + idade * p.vx;
        o.y = p.y - idade * p.vy + std::sin((p.x + idade * 60.0f) * 0.35f) * 3.0f * idade * 4.0f;
        // esquenta (branco-rosado) e depois esfria em cinza, como no mockup (desintegrar)
        const float k = std::min(1.0f, idade / 0.25f);
        const float q = 0.6f * (1.0f - k);
        uint8_t r = misturar8(p.r, 255.0f, q), g = misturar8(p.g, 214.0f, q), b = misturar8(p.b, 200.0f, q);
        r = misturar8(r, 150.0f, 0.5f * k);
        g = misturar8(g, 120.0f, 0.5f * k);
        b = misturar8(b, 120.0f, 0.5f * k);
        o.r = r;
        o.g = g;
        o.b = b;
        return o;
    }

    bool deveAnimar(const ContextoAnimacao& c) { return c.telaBatalha && !c.escurecida && !c.cenaNaTela && !c.emPe; }

    float pulso(uint32_t quadro, bool animar) {
        if (!animar) return 1.0f;
        constexpr float kDoisPi = 6.2831853f;
        return 0.75f + 0.25f * std::sin(float(quadro % kPeriodoPulsoQuadros) / float(kPeriodoPulsoQuadros) * kDoisPi);
    }

} // namespace tempo::luta
