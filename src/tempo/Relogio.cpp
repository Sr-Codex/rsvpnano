#include "tempo/Relogio.h"

#include <ctime>

namespace tempo {
    namespace {
        uint32_t gEscala = 1;
        bool gAncorado = false;
        int64_t gEpochBase = 0;
        uint64_t gMsBase = 0;
    } // namespace

    void definirEscala(uint32_t valor) {
        gEscala = valor == 0 ? 1 : valor;
        gAncorado = false;  // a hora de parede precisa reancorar com a nova escala
    }

    uint32_t escala() {
        return gEscala;
    }

    uint64_t ms(uint32_t nowMs) {
        return static_cast<uint64_t>(nowMs) * gEscala;
    }

    Agora deEpoch(int64_t epoch, bool utc) {
        Agora r;
        const std::time_t t = static_cast<std::time_t>(epoch);
        std::tm tm{};
        if ((utc ? gmtime_r(&t, &tm) : localtime_r(&t, &tm)) == nullptr) return r;
        const int ano = tm.tm_year + 1900;
        if (ano < kAnoMinimoValido) return r;
        r.valida = true;
        r.data.ano = ano;
        r.data.mes = tm.tm_mon + 1;
        r.data.dia = tm.tm_mday;
        r.segundoDoDia = tm.tm_hour * 3600 + tm.tm_min * 60 + tm.tm_sec;
        r.minutoDoDia = r.segundoDoDia / 60;
        return r;
    }

    void ancorar(int64_t epoch, uint32_t nowMs) {
        gAncorado = epoch > 0;
        gEpochBase = epoch;
        gMsBase = ms(nowMs);
    }

    Agora agora(uint32_t nowMs) {
        if (!gAncorado) {
            const int64_t epoch = static_cast<int64_t>(std::time(nullptr));
            if (!deEpoch(epoch).valida) return {};
            ancorar(epoch, nowMs);
        }
        const int64_t decorridoS = static_cast<int64_t>((ms(nowMs) - gMsBase) / 1000);
        return deEpoch(gEpochBase + decorridoS);
    }

} // namespace tempo
