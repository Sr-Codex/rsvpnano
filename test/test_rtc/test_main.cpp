#include <unity.h>

#include <cstdlib>
#include <ctime>

#include "drivers/rtc/pcf85063/Pcf85063Codec.h"
#include "tempo/Relogio.h"

using namespace BoardDrivers::Pcf85063;

void setUp() {}
void tearDown() {}

void test_bcd() {
    TEST_ASSERT_EQUAL_HEX8(0x59, toBcd(59));
    TEST_ASSERT_EQUAL_HEX8(0x07, toBcd(7));
    TEST_ASSERT_EQUAL(23, fromBcd(0x23));
    for (int v = 0; v < 100; ++v) TEST_ASSERT_EQUAL(v, fromBcd(toBcd(v)));
}

void test_epoch_conhecido() {
    int64_t epoch = 0;
    TEST_ASSERT_TRUE(toEpoch({2026, 11, 10, 12, 0, 0}, epoch));
    TEST_ASSERT_EQUAL_INT64(1794312000, epoch);
    TEST_ASSERT_TRUE(toEpoch({2000, 1, 1, 0, 0, 0}, epoch));
    TEST_ASSERT_EQUAL_INT64(946684800, epoch);
    TEST_ASSERT_TRUE(toEpoch({2024, 2, 29, 23, 59, 59}, epoch));  // bissexto
    TEST_ASSERT_EQUAL_INT64(1709251199, epoch);
    TEST_ASSERT_FALSE(toEpoch({2025, 2, 29, 0, 0, 0}, epoch));
    TEST_ASSERT_FALSE(toEpoch({2026, 13, 1, 0, 0, 0}, epoch));
    TEST_ASSERT_FALSE(toEpoch({1999, 12, 31, 0, 0, 0}, epoch));
}

void test_ida_e_volta() {
    for (int64_t epoch = 946684800; epoch < 4102444799LL; epoch += 86400LL * 37 + 12345) {
        DateTime t;
        TEST_ASSERT_TRUE(fromEpoch(epoch, t));
        int64_t volta = 0;
        TEST_ASSERT_TRUE(toEpoch(t, volta));
        TEST_ASSERT_EQUAL_INT64(epoch, volta);
    }
    DateTime t;
    TEST_ASSERT_FALSE(fromEpoch(946684799, t));
    TEST_ASSERT_FALSE(fromEpoch(4102444800LL, t));
}

void test_encode_decode() {
    TimeRegisters regs{};
    TEST_ASSERT_TRUE(encode({2026, 11, 10, 15, 4, 9}, regs));  // terca-feira
    TEST_ASSERT_EQUAL_HEX8(0x09, regs[0]);
    TEST_ASSERT_EQUAL_HEX8(0x04, regs[1]);
    TEST_ASSERT_EQUAL_HEX8(0x15, regs[2]);
    TEST_ASSERT_EQUAL_HEX8(0x10, regs[3]);
    TEST_ASSERT_EQUAL_HEX8(2, regs[4]);   // 0 = domingo
    TEST_ASSERT_EQUAL_HEX8(0x11, regs[5]);
    TEST_ASSERT_EQUAL_HEX8(0x26, regs[6]);
    DateTime t;
    bool osc = true;
    TEST_ASSERT_TRUE(decode(regs, t, osc));
    TEST_ASSERT_FALSE(osc);
    TEST_ASSERT_EQUAL(2026, t.year);
    TEST_ASSERT_EQUAL(11, t.month);
    TEST_ASSERT_EQUAL(10, t.day);
    TEST_ASSERT_EQUAL(15, t.hour);
    TEST_ASSERT_EQUAL(4, t.minute);
    TEST_ASSERT_EQUAL(9, t.second);
}

void test_decode_invalidos() {
    DateTime t;
    bool osc = false;
    TimeRegisters regs{0x80 | 0x30, 0x15, 0x10, 0x20, 0x01, 0x03, 0x26};  // OS ligado
    TEST_ASSERT_TRUE(decode(regs, t, osc));
    TEST_ASSERT_TRUE(osc);
    TEST_ASSERT_EQUAL(30, t.second);
    TimeRegisters ruim{0x6A, 0x15, 0x10, 0x20, 0x01, 0x03, 0x26};  // BCD invalido (0xA)
    TEST_ASSERT_FALSE(decode(ruim, t, osc));
    TimeRegisters mes13{0x00, 0x00, 0x00, 0x01, 0x01, 0x13, 0x26};
    TEST_ASSERT_FALSE(decode(mes13, t, osc));
    TimeRegisters dia31abril{0x00, 0x00, 0x00, 0x31, 0x01, 0x04, 0x26};
    TEST_ASSERT_FALSE(decode(dia31abril, t, osc));
    TimeRegisters seculo{0x00, 0x00, 0x00, 0x01, 0x01, 0x80 | 0x01, 0x26};  // bit de seculo ignorado
    TEST_ASSERT_TRUE(decode(seculo, t, osc));
    TEST_ASSERT_EQUAL(1, t.month);
}

// Fuso do Tempo: UTC-3 fixo, sem horario de verao (verao do hemisferio sul nao se aplica desde 2019).
void test_fuso_sao_paulo() {
    setenv("TZ", "<-03>3", 1);
    tzset();
    auto verao = tempo::deEpoch(1767225600);   // 2026-01-01T00:00:00Z -> 2025-12-31 21:00 local
    TEST_ASSERT_TRUE(verao.valida);
    TEST_ASSERT_EQUAL(2025, verao.data.ano);
    TEST_ASSERT_EQUAL(12, verao.data.mes);
    TEST_ASSERT_EQUAL(31, verao.data.dia);
    TEST_ASSERT_EQUAL(21 * 3600, verao.segundoDoDia);
    auto inverno = tempo::deEpoch(1783339200);  // 2026-07-06T12:00:00Z -> 09:00 local (mesmo deslocamento)
    TEST_ASSERT_EQUAL(9 * 3600, inverno.segundoDoDia);
    // ancorar(): relogio avanca por millis a partir da hora do RTC/rede
    tempo::ancorar(1794312000, 1000);           // 12:00Z = 09:00 local
    auto a = tempo::agora(1000 + 90 * 1000);
    TEST_ASSERT_TRUE(a.valida);
    TEST_ASSERT_EQUAL(9 * 3600 + 90, a.segundoDoDia);
    tempo::ancorar(0, 0);
    unsetenv("TZ");
    tzset();
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_bcd);
    RUN_TEST(test_epoch_conhecido);
    RUN_TEST(test_ida_e_volta);
    RUN_TEST(test_encode_decode);
    RUN_TEST(test_decode_invalidos);
    RUN_TEST(test_fuso_sao_paulo);
    return UNITY_END();
}
