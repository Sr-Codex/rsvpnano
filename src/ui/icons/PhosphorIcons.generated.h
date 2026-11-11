// Gerado por tools/generate_phosphor_icons.py — NAO editar a mao.
// Icones Phosphor 2.1.1 (MIT, Copyright (c) 2023 Phosphor Icons) — ver THIRD_PARTY_LICENSES.md.
#pragma once
#include <cstdint>

namespace icons::phosphor {
enum class Id : uint8_t {
    Barbell,
    Pill,
    JarLabel,
    ForkKnife,
    Tooth,
    Coffee,
    Drop,
    FlowerLotus,
    BookOpen,
    Brain,
    CalendarCheck,
    Prohibit,
    Sun,
    CheckCircle,
    Fire,
    Check,
    SkipForward,
    ArrowCounterClockwise,
    Star,
};
inline constexpr unsigned kNumIcones = 19;

// Mascara alfa 4 bpp, lado x lado, linhas contiguas, 2 px por byte (1o px = nibble alto).
struct Mascara {
    Id id;
    uint8_t lado;
    const uint8_t* alfa;
};
inline constexpr unsigned kNumMascaras = 77;
extern const Mascara kMascaras[kNumMascaras];   // ordenadas por (id, lado)

} // namespace icons::phosphor
