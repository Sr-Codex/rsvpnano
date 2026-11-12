#!/usr/bin/env python3
"""Gera src/ui/icons/EmojiSprites.generated.{h,cpp}: sprites coloridos (RGB565 + alfa de 8 bits) de emojis Fluent (MIT).

Entrada: assets/icons/fluent/<nome>.svg (vendorizados; licenca em assets/icons/fluent/LICENSE).
Saida: um sprite quadrado por (emoji, lado): `uint16_t` RGB565 (cor SEM pre-multiplicar, com pontilhado ordenado Bayer 4x4 antes
de quantizar, para o degrade do icone nao virar faixas nos 65 mil cores) + `uint8_t` alfa (0..255). O desenho (ui/screens/HabitosIcones)
mistura com o fundo pixel a pixel, entao as bordas saem suavizadas. Sem rede. Rasteriza com resvg-py em 8x e reduz por media
em caixa com alfa PRE-MULTIPLICADO (sem halo escuro nas bordas).

Uso (a partir de rsvpnano/):
  ../.venv/bin/python -I tools/generate_emoji_sprites.py           # escreve os 2 arquivos
  ../.venv/bin/python -I tools/generate_emoji_sprites.py --check   # sai com 1 se os arquivos gerados estiverem desatualizados
Requer: resvg-py, Pillow (requirements.txt do repo de docs).
"""
from __future__ import annotations

import io
import sys
from pathlib import Path

import resvg_py
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "assets/icons/fluent"
OUT_H = ROOT / "src/ui/icons/EmojiSprites.generated.h"
OUT_CPP = ROOT / "src/ui/icons/EmojiSprites.generated.cpp"

SS = 8                       # supersampling
LADOS = (16, 24, 28, 34)     # tamanhos usados: Perfil (16), painel vazio (24), linha do seletor (28), cartao do foco (34)

# (identificador C++, arquivo svg)
EMOJIS = [("Bullseye", "bullseye_color")]

BAYER4 = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def viewbox_do_conteudo(txt: str) -> str:
    """O SVG Fluent tem margem em volta do desenho; corta num quadrado justo (+0,5 unidade) em volta do conteudo visivel,
    para o sprite usar o lado inteiro (como no mockup)."""
    import re
    vb = [float(v) for v in re.search(r'viewBox="([^"]*)"', txt).group(1).split()]
    n = 512
    png = resvg_py.svg_to_bytes(svg_string=txt, width=n, height=n)
    caixa = Image.open(io.BytesIO(bytes(png))).convert("RGBA").getchannel("A").point(lambda v: 255 if v > 8 else 0).getbbox()
    k = vb[2] / n
    x0, y0, x1, y1 = (caixa[0] * k + vb[0], caixa[1] * k + vb[1], caixa[2] * k + vb[0], caixa[3] * k + vb[1])
    lado = max(x1 - x0, y1 - y0) + 1.0
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    return re.sub(r'viewBox="[^"]*"', f'viewBox="{cx - lado / 2:.3f} {cy - lado / 2:.3f} {lado:.3f} {lado:.3f}"', txt, count=1)


def sprite(svg: Path, lado: int) -> tuple[list[int], list[int]]:
    n = lado * SS
    txt = viewbox_do_conteudo(svg.read_text(encoding="utf-8"))
    png = resvg_py.svg_to_bytes(svg_string=txt, width=n, height=n)
    img = Image.open(io.BytesIO(bytes(png))).convert("RGBA").convert("RGBa")   # pre-multiplicado
    img = img.reduce(SS).convert("RGBA")                                          # media em caixa e volta a nao pre-multiplicado
    px = img.load()
    rgb, alfa = [], []
    for y in range(lado):
        for x in range(lado):
            r, g, b, a = px[x, y]
            d = (BAYER4[y % 4][x % 4] + 0.5) / 16.0 - 0.5   # -0.5..+0.5 passo de quantizacao
            def q(v: int, bits: int) -> int:
                passo = 255.0 / ((1 << bits) - 1)
                return max(0, min((1 << bits) - 1, int(round((v + d * passo) / passo))))
            rgb.append((q(r, 5) << 11) | (q(g, 6) << 5) | q(b, 5))
            alfa.append(a)
    return rgb, alfa


def gerar() -> tuple[str, str]:
    entradas = []  # (idx, lado, nome, rgb, alfa)
    for idx, (ident, nome) in enumerate(EMOJIS):
        for lado in LADOS:
            rgb, alfa = sprite(SRC / f"{nome}.svg", lado)
            entradas.append((idx, lado, f"k{ident}{lado}", rgb, alfa))
    h = ["// Gerado por tools/generate_emoji_sprites.py — NAO editar a mao.",
         "// Fluent Emoji (Microsoft, MIT) — ver THIRD_PARTY_LICENSES.md e assets/icons/fluent/LICENSE.",
         "#pragma once", "#include <cstdint>", "", "namespace icons::emoji {", "enum class Id : uint8_t {"]
    h += [f"    {ident}," for ident, _ in EMOJIS]
    h += ["};", f"inline constexpr unsigned kNumEmojis = {len(EMOJIS)};", "",
          "// Sprite lado x lado: `rgb` = RGB565 (nao pre-multiplicado, pontilhado), `alfa` = 0..255. Linhas contiguas.",
          "struct Sprite {", "    Id id;", "    uint8_t lado;", "    const uint16_t* rgb;", "    const uint8_t* alfa;", "};",
          f"inline constexpr unsigned kNumSprites = {len(entradas)};",
          "extern const Sprite kSprites[kNumSprites];   // ordenados por (id, lado)", "",
          "} // namespace icons::emoji", ""]
    c = ["// Gerado por tools/generate_emoji_sprites.py — NAO editar a mao.",
         '#include "ui/icons/EmojiSprites.generated.h"', "", "namespace icons::emoji {", "namespace {"]
    for _, _, nome, rgb, alfa in entradas:
        c.append(f"const uint16_t {nome}Rgb[] = {{")
        for i in range(0, len(rgb), 16):
            c.append("    " + ",".join(f"0x{v:04x}" for v in rgb[i:i + 16]) + ",")
        c.append("};")
        c.append(f"const uint8_t {nome}Alfa[] = {{")
        for i in range(0, len(alfa), 24):
            c.append("    " + ",".join(f"0x{v:02x}" for v in alfa[i:i + 24]) + ",")
        c.append("};")
    c += ["} // namespace", "", "const Sprite kSprites[kNumSprites] = {"]
    for idx, lado, nome, _, _ in entradas:
        c.append(f"    {{Id::{EMOJIS[idx][0]}, {lado}, {nome}Rgb, {nome}Alfa}},")
    c += ["};", "", "} // namespace icons::emoji", ""]
    return "\n".join(h), "\n".join(c)


def main(argv: list[str]) -> int:
    h, c = gerar()
    if "--check" in argv:
        ok = OUT_H.exists() and OUT_CPP.exists() and OUT_H.read_text() == h and OUT_CPP.read_text() == c
        print("EmojiSprites.generated.*: atualizado" if ok else "EmojiSprites.generated.*: DESATUALIZADO (rode tools/generate_emoji_sprites.py)")
        return 0 if ok else 1
    OUT_H.write_text(h)
    OUT_CPP.write_text(c)
    print(f"escrito {OUT_H.name} e {OUT_CPP.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
