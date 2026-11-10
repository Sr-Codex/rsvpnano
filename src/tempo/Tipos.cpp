#include "tempo/Tipos.h"

namespace tempo {

    bool operator==(const Data& a, const Data& b) {
        return a.ano == b.ano && a.mes == b.mes && a.dia == b.dia;
    }

    bool operator<(const Data& a, const Data& b) {
        if (a.ano != b.ano) {
            return a.ano < b.ano;
        }
        if (a.mes != b.mes) {
            return a.mes < b.mes;
        }
        return a.dia < b.dia;
    }

    // Algoritmo de calendario civil (Howard Hinnant), sem dependencia de <chrono>/fuso.
    int64_t diaSerial(const Data& d) {
        int64_t y = d.ano;
        const int64_t m = d.mes;
        y -= m <= 2 ? 1 : 0;
        const int64_t era = (y >= 0 ? y : y - 399) / 400;
        const int64_t yoe = y - era * 400;
        const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d.dia - 1;
        const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
        return era * 146097 + doe - 719468;
    }

    Data dataDeSerial(int64_t z) {
        z += 719468;
        const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
        const int64_t doe = z - era * 146097;
        const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
        const int64_t y = yoe + era * 400;
        const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
        const int64_t mp = (5 * doy + 2) / 153;
        const int64_t d = doy - (153 * mp + 2) / 5 + 1;
        const int64_t m = mp + (mp < 10 ? 3 : -9);
        Data out;
        out.ano = static_cast<int>(y + (m <= 2 ? 1 : 0));
        out.mes = static_cast<int>(m);
        out.dia = static_cast<int>(d);
        return out;
    }

    int diaDaSemana(const Data& d) {
        const int64_t serial = diaSerial(d);
        // 1970-01-01 foi quinta-feira (3 com segunda = 0).
        return static_cast<int>(((serial + 3) % 7 + 7) % 7);
    }

    int64_t segundaDaSemana(const Data& d) {
        return diaSerial(d) - diaDaSemana(d);
    }

    int chaveMes(const Data& d) {
        return d.ano * 12 + (d.mes - 1);
    }

} // namespace tempo
