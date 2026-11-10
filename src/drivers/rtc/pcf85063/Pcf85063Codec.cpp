#include "drivers/rtc/pcf85063/Pcf85063Codec.h"

namespace BoardDrivers::Pcf85063 {
    namespace {

        constexpr bool leap(int year) {
            return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
        }

        constexpr int daysInMonth(int year, int month) {
            constexpr int kDays[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            return month == 2 && leap(year) ? 29 : kDays[month - 1];
        }

        // Dias desde 1970-01-01 (algoritmo days_from_civil, Howard Hinnant).
        constexpr int64_t daysFromCivil(int y, int m, int d) {
            y -= m <= 2;
            const int64_t era = (y >= 0 ? y : y - 399) / 400;
            const int64_t yoe = y - era * 400;
            const int64_t doy = (153 * (m + (m > 2 ? -3 : 9)) + 2) / 5 + d - 1;
            const int64_t doe = yoe * 365 + yoe / 4 - yoe / 100 + doy;
            return era * 146097 + doe - 719468;
        }

        constexpr void civilFromDays(int64_t z, int& y, int& m, int& d) {
            z += 719468;
            const int64_t era = (z >= 0 ? z : z - 146096) / 146097;
            const int64_t doe = z - era * 146097;
            const int64_t yoe = (doe - doe / 1460 + doe / 36524 - doe / 146096) / 365;
            const int64_t doy = doe - (365 * yoe + yoe / 4 - yoe / 100);
            const int64_t mp = (5 * doy + 2) / 153;
            d = static_cast<int>(doy - (153 * mp + 2) / 5 + 1);
            m = static_cast<int>(mp < 10 ? mp + 3 : mp - 9);
            y = static_cast<int>(yoe + era * 400 + (m <= 2));
        }

        bool validBcd(uint8_t value) {
            return (value >> 4) <= 9 && (value & 0x0F) <= 9;
        }

    } // namespace

    bool validDateTime(const DateTime& v) {
        return v.year >= 2000 && v.year <= 2099 && v.month >= 1 && v.month <= 12 && v.day >= 1
            && v.day <= daysInMonth(v.year, v.month) && v.hour >= 0 && v.hour <= 23 && v.minute >= 0
            && v.minute <= 59 && v.second >= 0 && v.second <= 59;
    }

    bool toEpoch(const DateTime& v, int64_t& epoch) {
        if (!validDateTime(v)) return false;
        epoch = daysFromCivil(v.year, v.month, v.day) * 86400 + v.hour * 3600 + v.minute * 60 + v.second;
        return true;
    }

    bool fromEpoch(int64_t epoch, DateTime& out) {
        constexpr int64_t kMin = 946684800;    // 2000-01-01T00:00:00Z
        constexpr int64_t kMax = 4102444799;   // 2099-12-31T23:59:59Z
        if (epoch < kMin || epoch > kMax) return false;
        const int64_t days = epoch / 86400;
        const int secondOfDay = static_cast<int>(epoch % 86400);
        DateTime result;
        civilFromDays(days, result.year, result.month, result.day);
        result.hour = secondOfDay / 3600;
        result.minute = secondOfDay % 3600 / 60;
        result.second = secondOfDay % 60;
        out = result;
        return true;
    }

    bool encode(const DateTime& v, TimeRegisters& out) {
        if (!validDateTime(v)) return false;
        int64_t epoch = 0;
        toEpoch(v, epoch);
        const int weekday = static_cast<int>(((epoch / 86400) % 7 + 11) % 7);  // 1970-01-01 = quinta (4); 0 = domingo
        out[0] = toBcd(v.second);  // bit OS limpo: hora agora e confiavel
        out[1] = toBcd(v.minute);
        out[2] = toBcd(v.hour);
        out[3] = toBcd(v.day);
        out[4] = static_cast<uint8_t>(weekday);
        out[5] = toBcd(v.month);
        out[6] = toBcd(v.year - 2000);
        return true;
    }

    bool decode(const TimeRegisters& in, DateTime& out, bool& oscillatorStopped) {
        oscillatorStopped = (in[0] & 0x80) != 0;
        const uint8_t seconds = in[0] & 0x7F, minutes = in[1] & 0x7F, hours = in[2] & 0x3F, day = in[3] & 0x3F,
                      month = in[5] & 0x1F, year = in[6];
        if (!validBcd(seconds) || !validBcd(minutes) || !validBcd(hours) || !validBcd(day) || !validBcd(month)
            || !validBcd(year))
            return false;
        DateTime result{2000 + fromBcd(year), fromBcd(month), fromBcd(day), fromBcd(hours), fromBcd(minutes),
                        fromBcd(seconds)};
        if (!validDateTime(result)) return false;
        out = result;
        return true;
    }

} // namespace BoardDrivers::Pcf85063
