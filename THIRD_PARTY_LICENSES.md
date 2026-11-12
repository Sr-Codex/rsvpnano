# Third-party licenses (assets vendored in this repository)

Fonts and libraries keep their own licences next to their files (see `third_party/` and `fonts/`).

## Phosphor Icons 2.1.1 (MIT)

`assets/icons/phosphor/fill/*.svg` are a subset of `@phosphor-icons/core` 2.1.1 (https://phosphoricons.com), fill weight only.
`src/ui/icons/PhosphorIcons.generated.*` are alpha masks rasterized from them by `tools/generate_phosphor_icons.py`.
Full licence text: `assets/icons/phosphor/LICENSE`.

> MIT License — Copyright (c) 2023 Phosphor Icons

## Fluent Emoji (MIT)

`assets/icons/fluent/bullseye_color.svg` is the "Bullseye" emoji (Color style) from https://github.com/microsoft/fluentui-emoji,
unmodified. `src/ui/icons/EmojiSprites.generated.*` are RGB565 + alpha sprites rasterized from it by
`tools/generate_emoji_sprites.py`. Full licence text: `assets/icons/fluent/LICENSE`.

> MIT License — Copyright (c) Microsoft Corporation.
