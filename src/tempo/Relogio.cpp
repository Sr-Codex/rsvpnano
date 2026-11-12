#include "tempo/Relogio.h"

#include <cstdio>
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

    int64_t epochAgora(uint32_t nowMs) {
        if (!agora(nowMs).valida) return 0;   // ancora se preciso
        return gEpochBase + static_cast<int64_t>((ms(nowMs) - gMsBase) / 1000);
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

    int64_t epochDeIso(std::string_view iso) {
        if (iso.size() < 19) return 0;
        const auto num = [&](size_t pos, size_t n, int& out) {
            out = 0;
            for (size_t i = pos; i < pos + n; ++i) {
                if (iso[i] < '0' || iso[i] > '9') return false;
                out = out * 10 + (iso[i] - '0');
            }
            return true;
        };
        int a, mo, d, h, mi, se;
        if (!num(0, 4, a) || !num(5, 2, mo) || !num(8, 2, d) || !num(11, 2, h) || !num(14, 2, mi) || !num(17, 2, se)) return 0;
        if (iso[4] != '-' || iso[7] != '-' || (iso[10] != 'T' && iso[10] != ' ') || mo < 1 || mo > 12 || d < 1 || d > 31
            || h > 23 || mi > 59 || se > 60 || a < kAnoMinimoValido)
            return 0;
        return diaSerial(Data{a, mo, d}) * 86400 + h * 3600 + mi * 60 + se;
    }

    std::string textoHoraSincronizacao(std::string_view iso, int64_t agoraEpoch, int fusoMin) {
        const int64_t epoch = epochDeIso(iso);
        if (epoch <= 0) return "nunca";
        const Agora quando = deEpoch(epoch + int64_t(fusoMin) * 60, true);
        if (!quando.valida) return "nunca";
        char hora[8];
        std::snprintf(hora, sizeof hora, "%02d:%02d", quando.minutoDoDia / 60, quando.minutoDoDia % 60);
        if (agoraEpoch > 0) {
            const Agora agora = deEpoch(agoraEpoch + int64_t(fusoMin) * 60, true);
            if (agora.valida) {
                const int64_t dif = diaSerial(agora.data) - diaSerial(quando.data);
                if (dif == 0) return std::string{"hoje "} + hora;
                if (dif == 1) return std::string{"ontem "} + hora;
            }
        }
        char dia[8];
        std::snprintf(dia, sizeof dia, "%02d/%02d ", quando.data.dia, quando.data.mes);
        return std::string{dia} + hora;
    }

} // namespace tempo
