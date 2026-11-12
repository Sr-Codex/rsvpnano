// Gerado por tools/generate_emoji_sprites.py — NAO editar a mao.
// Fluent Emoji (Microsoft, MIT) — ver THIRD_PARTY_LICENSES.md e assets/icons/fluent/LICENSE.
#pragma once
#include <cstdint>

namespace icons::emoji {
enum class Id : uint8_t {
    Bullseye,
};
inline constexpr unsigned kNumEmojis = 1;

// Sprite lado x lado: `rgb` = RGB565 (nao pre-multiplicado, pontilhado), `alfa` = 0..255. Linhas contiguas.
struct Sprite {
    Id id;
    uint8_t lado;
    const uint16_t* rgb;
    const uint8_t* alfa;
};
inline constexpr unsigned kNumSprites = 4;
extern const Sprite kSprites[kNumSprites];   // ordenados por (id, lado)

} // namespace icons::emoji
