#include "tempo/CenaViva.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tempo::cenaviva {

    namespace {
        constexpr double kDoisPi = 6.283185307179586;

        // fase dos mockups com precisao: so a parte dentro do ciclo `periodoMs` (evita perder casas com ms grande)
        double faseEm(uint32_t ms, uint32_t periodoMs) { return static_cast<double>(ms % periodoMs) / kFaseMs; }

        double modPositivo(double v, double m) {
            const double r = std::fmod(v, m);
            return r < 0 ? r + m : r;
        }
    } // namespace

    uint32_t quadro(uint32_t ms) { return ms / kQuadroMs; }

    uint32_t misturar(uint32_t a, uint32_t b, uint32_t c) {
        uint32_t h = a * 0x9E3779B1u ^ (b + 0x7F4A7C15u) * 0x85EBCA77u ^ (c + 0x165667B1u) * 0xC2B2AE3Du;
        h ^= h >> 16;
        h *= 0x7FEB352Du;
        h ^= h >> 15;
        h *= 0x846CA68Bu;
        h ^= h >> 16;
        return h;
    }

    float aleatorio(uint32_t a, uint32_t b, uint32_t c) {
        return static_cast<float>(misturar(a, b, c) >> 8) / static_cast<float>(1u << 24);
    }

    float gotaY(float u, float vel, uint32_t ms, float alt) {
        if (!(alt > 0.0f)) return 0.0f;
        // vel * fase com o ms inteiro (double): continua, sem salto quando ms da a volta no ciclo dos mockups
        const double y = static_cast<double>(u) + static_cast<double>(vel) * (static_cast<double>(ms) / kFaseMs);
        return static_cast<float>(modPositivo(y, alt));
    }

    int quadroSprite(uint32_t ms) { return static_cast<int>(quadro(ms) % kQuadrosSprite); }

    // ---------------------------------------------------------------- QG Neon
    bool neonTreme(uint32_t q) {
        // blocos de 32 quadros (4 s): 3 em cada 4 blocos tem uma falha num quadro sorteado (as vezes dupla, 2 quadros depois)
        const uint32_t bloco = q / 32, dentro = q % 32;
        const uint32_t h = misturar(bloco, 0x7E0u);
        if ((h & 3u) == 0) return false;
        const uint32_t k = 3 + (h >> 4) % 26;
        return dentro == k || ((h & 0x100u) != 0 && dentro == k + 2);
    }

    bool gatoPisca(uint32_t q) {
        // blocos de 40 quadros (5 s): uma piscada de 2 quadros (250 ms); as vezes duas seguidas
        const uint32_t bloco = q / 40, dentro = q % 40;
        const uint32_t h = misturar(bloco, 0xCA7u);
        const uint32_t k = 4 + (h >> 3) % 30;
        if (dentro == k || dentro == k + 1) return true;
        return (h & 0x40u) != 0 && (dentro == k + 4 || dentro == k + 5);
    }

    bool roteadorAceso(int led, uint32_t ms) {
        const uint64_t passo = static_cast<uint64_t>(ms) * 8 / kFaseMs;   // int(fase * 8) do mockup
        return (passo + static_cast<uint64_t>(std::max(0, led))) % 4 != 0;
    }

    bool rackAceso(int fila, int led, uint32_t ms, float probAceso) {
        const uint32_t passo = ms / (kFaseMs / 6);   // int(fase * 6): 500 ms
        return aleatorio(static_cast<uint32_t>(fila) * 13u + static_cast<uint32_t>(led), passo, 0xACCu) < probAceso;
    }

    float teclaMistura(int tecla, uint32_t ms) {
        return static_cast<float>((std::sin(tecla * 0.3 + faseEm(ms, kFaseMs) * kDoisPi) + 1.0) / 2.0);
    }

    float faixaMistura(int led, uint32_t ms) {
        return static_cast<float>((std::sin(led * 0.5 + faseEm(ms, kFaseMs) * kDoisPi) + 1.0) / 2.0);
    }

    bool janelaAcesa(int janela, uint32_t ms) {
        const uint32_t i = static_cast<uint32_t>(janela);
        const uint32_t periodo = 2500 + misturar(i, 1) % 3500;    // cada janela no seu ritmo (2,5..6 s)
        const uint32_t desloc = misturar(i, 2) % periodo;
        const uint32_t ciclo = static_cast<uint32_t>((static_cast<uint64_t>(ms) + desloc) / periodo);
        return aleatorio(i, ciclo, 3) < 0.25f;
    }

    Drone drone(uint32_t ms, float x0, float x1, float yBase, float amp, float vao, float entra) {
        Drone d;
        const double f = static_cast<double>(ms % kDronePeriodoMs) / kFaseMs;   // 0..4: so a 1a fase e a passagem
        if (f >= 1.0) return d;
        d.x = static_cast<float>(x0 + (x1 - x0 + vao) * f - entra);
        d.y = static_cast<float>(yBase + amp * std::sin(f * kDoisPi));
        d.visivel = x0 < d.x && d.x < x1;
        d.vermelhoAceso = static_cast<int>(f * 24) % 2 != 0;   // int(fase * 24) % 2 do mockup
        return d;
    }

    // ---------------------------------------------------------------- QG Cidade
    bool antenaAcesa(int camada, uint32_t ms) {
        const uint64_t passo = static_cast<uint64_t>(ms) * 4 / kFaseMs;   // int(fase * 4) do mockup
        return (passo + static_cast<uint64_t>(std::max(0, camada))) % 2 == 0;
    }

    float letreiroForca(int letreiro, uint32_t ms) {
        const uint32_t i = static_cast<uint32_t>(letreiro);
        // respira de leve (0,90..1,00), cada letreiro no seu ritmo ...
        const double periodo = 2400.0 + 370.0 * (i % 7);
        const double resp = 0.95 + 0.05 * std::sin(kDoisPi * std::fmod(static_cast<double>(ms), periodo) / periodo + i);
        // ... e as vezes falha (2 quadros a 70 %): blocos de 48 quadros (6 s), 1 em 3 com falha
        const uint32_t q = quadro(ms), bloco = q / 48, dentro = q % 48;
        const uint32_t h = misturar(i, bloco, 0x1E7u);
        if (h % 3 == 0) {
            const uint32_t k = 2 + (h >> 4) % 44;
            if (dentro == k || dentro == k + 1) return static_cast<float>(resp * 0.7);
        }
        return static_cast<float>(resp);
    }

    float carroVivaX(float fase0, int sentido, uint32_t ms, float x0, float x1) {
        const double u = modPositivo(faseEm(ms, kFaseMs) + fase0, 1.0);
        return static_cast<float>(x0 + (x1 - x0) * (sentido > 0 ? u : 1.0 - u));
    }

    float carroLongeX(float fase0, float vel, uint32_t ms, float largura) {
        const double v = static_cast<double>(fase0) + static_cast<double>(vel) * (static_cast<double>(ms) / kFaseMs);
        return static_cast<float>(modPositivo(v, largura + 40.0) - 20.0);
    }

    bool doisPontosAcesos(uint32_t ms) { return ms % 1000 < 500; }

    // ---------------------------------------------------------------- relogio
    void formatar(int64_t segundos, bool mais, std::array<char, 12>& saida) {
        const int64_t s = std::max<int64_t>(0, segundos);
        const int64_t h = s / 3600, m = (s / 60) % 60, ss = s % 60;
        const char* sinal = mais ? "+" : "";
        if (h > 0)
            std::snprintf(saida.data(), saida.size(), "%s%d:%02d:%02d", sinal, static_cast<int>(std::min<int64_t>(h, 99)),
                          static_cast<int>(m), static_cast<int>(ss));
        else if (mais)
            std::snprintf(saida.data(), saida.size(), "+%d:%02d", static_cast<int>(m), static_cast<int>(ss));
        else
            std::snprintf(saida.data(), saida.size(), "%02d:%02d", static_cast<int>(m), static_cast<int>(ss));
    }

    Relogio relogio(const EntradaRelogio& e) {
        Relogio r;
        if (e.livre) {
            r.estado = EstadoRelogio::Livre;
            formatar(static_cast<int64_t>(e.focadoMs / 1000), false, r.texto);
            r.fracao = static_cast<float>(static_cast<double>(e.focadoMs % 3600000ULL) / 3600000.0);
        } else if (e.horaExtra && e.vitoria) {
            r.estado = EstadoRelogio::Vitoria;
            formatar(0, false, r.texto);
            r.fracao = 1.0f;
        } else if (e.horaExtra) {
            r.estado = EstadoRelogio::HoraExtra;
            formatar(std::max<int64_t>(0, e.overtimeMs) / 1000, true, r.texto);
            r.fracao = 1.0f;
        } else {
            r.estado = EstadoRelogio::Foco;
            formatar((std::max<int64_t>(0, e.restanteMs) + 999) / 1000, false, r.texto);   // como a batalha: arredonda para cima
            r.fracao = e.planoMs > 0 ? static_cast<float>(std::clamp(static_cast<double>(e.focadoMs) / static_cast<double>(e.planoMs), 0.0, 1.0))
                                     : 0.0f;
        }
        return r;
    }

    uint8_t segmentos(char c) {
        static constexpr uint8_t kMapa[10] = {0x3F, 0x06, 0x5B, 0x4F, 0x66, 0x6D, 0x7D, 0x07, 0x7F, 0x6F};
        return c >= '0' && c <= '9' ? kMapa[c - '0'] : 0;
    }

} // namespace tempo::cenaviva
