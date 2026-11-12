#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <vector>

// Medidor de atraso do toque (diagnostico permanente, so em memoria). Para cada soltura tratada pelo App guarda
// atraso = millis() no tratamento - instante em que a amostra foi lida do controlador, e o backlog da volta do laco
// (amostras que sairam da fila naquela volta, tratadas ou juntadas). Exposto em GET /api/v2/device ("touch").
namespace atrasotoque {

    struct ResumoAtraso {
        uint32_t releases = 0;                    // solturas medidas desde o boot
        uint32_t releaseLagMaxMs = 0;             // maior atraso
        uint32_t releaseLagAvgMs = 0;             // media movel exponencial (peso 1/8 para a medicao nova)
        std::vector<uint32_t> releaseLagLastMs;   // ultimas medicoes, da mais antiga para a mais nova
        std::vector<uint32_t> releaseBacklogLast; // backlog da volta de cada uma dessas medicoes (mesma ordem)
        uint32_t backlogMax = 0;                  // maior numero de amostras numa volta do laco
        uint32_t moveSkipped = 0;                 // arrastos juntados sem entregar (total desde o boot)
    };

    class MedidorAtraso {
    public:
        static constexpr size_t kUltimas = 16;

        // Uma soltura tratada nesta volta (o backlog e preenchido em fimDaVolta).
        void soltar(uint32_t atrasoMs) {
            ultimas_[proxima_] = {atrasoMs, 0};
            proxima_ = (proxima_ + 1) % kUltimas;
            guardadas_ = std::min(guardadas_ + 1, kUltimas);
            pendentesNaVolta_ = std::min(pendentesNaVolta_ + 1, kUltimas);
            maxMs_ = std::max(maxMs_, atrasoMs);
            // EMA em x8 (inteiro): media += (x - media) / 8; a 1a medicao inicia a media.
            mediaX8_ = releases_ == 0 ? int64_t(atrasoMs) * 8 : mediaX8_ + int64_t(atrasoMs) - mediaX8_ / 8;
            ++releases_;
        }

        // Fecha uma volta do laco que tirou `amostras` da fila (tratadas + juntadas) e juntou `movesDescartados` arrastos.
        void fimDaVolta(uint32_t amostras, uint32_t movesDescartados) {
            backlogMax_ = std::max(backlogMax_, amostras);
            moveSkipped_ += movesDescartados;
            for (size_t i = 1; i <= pendentesNaVolta_; ++i) ultimas_[(proxima_ + kUltimas - i) % kUltimas].backlog = amostras;
            pendentesNaVolta_ = 0;
        }

        ResumoAtraso resumo() const {
            ResumoAtraso r;
            r.releases = releases_;
            r.releaseLagMaxMs = maxMs_;
            r.releaseLagAvgMs = releases_ == 0 ? 0 : uint32_t((mediaX8_ + 4) / 8);
            r.releaseLagLastMs.reserve(guardadas_);
            r.releaseBacklogLast.reserve(guardadas_);
            for (size_t i = guardadas_; i > 0; --i) {
                const Medida& m = ultimas_[(proxima_ + kUltimas - i) % kUltimas];
                r.releaseLagLastMs.push_back(m.atrasoMs);
                r.releaseBacklogLast.push_back(m.backlog);
            }
            r.backlogMax = backlogMax_;
            r.moveSkipped = moveSkipped_;
            return r;
        }

    private:
        struct Medida {
            uint32_t atrasoMs = 0;
            uint32_t backlog = 0;
        };
        std::array<Medida, kUltimas> ultimas_{};
        size_t proxima_ = 0;
        size_t guardadas_ = 0;
        size_t pendentesNaVolta_ = 0;
        uint32_t releases_ = 0;
        uint32_t maxMs_ = 0;
        int64_t mediaX8_ = 0;
        uint32_t backlogMax_ = 0;
        uint32_t moveSkipped_ = 0;
    };

    // Instancia unica: escrita pelo laco do App, lida pela API do companion (outra task).
    inline std::mutex& medidorMutex() {
        static std::mutex m;
        return m;
    }
    inline MedidorAtraso& medidorGlobal() {
        static MedidorAtraso m;
        return m;
    }
    inline void registrarSoltura(uint32_t atrasoMs) {
        const std::lock_guard lock{medidorMutex()};
        medidorGlobal().soltar(atrasoMs);
    }
    inline void registrarVolta(uint32_t amostras, uint32_t movesDescartados) {
        const std::lock_guard lock{medidorMutex()};
        medidorGlobal().fimDaVolta(amostras, movesDescartados);
    }
    inline ResumoAtraso resumoGlobal() {
        const std::lock_guard lock{medidorMutex()};
        return medidorGlobal().resumo();
    }

} // namespace atrasotoque
