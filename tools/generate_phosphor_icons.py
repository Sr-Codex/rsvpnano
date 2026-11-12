#!/usr/bin/env python3
"""Gera src/ui/icons/PhosphorIcons.generated.{h,cpp}: mascaras alfa de 4 bpp dos icones Phosphor (MIT, 2.1.1).

Entrada: assets/icons/phosphor/fill/<nome>.svg (vendorizados; licenca em assets/icons/phosphor/LICENSE).
Saida: uma mascara quadrada por (icone, lado); so o peso FILL. Pixel = 4 bits de alfa (0..15), 2 pixels por byte (o 1o no nibble alto),
linhas contiguas (lado par => sem preenchimento). Sem rede. Rasteriza com resvg-py em 4x e reduz por media (caixa).

Uso:
  .venv/bin/python -I tools/generate_phosphor_icons.py           # escreve os 2 arquivos
  .venv/bin/python -I tools/generate_phosphor_icons.py --check   # sai com 1 se os arquivos gerados estiverem desatualizados
Requer: resvg-py, Pillow (requirements.txt do repo de docs).
"""
from __future__ import annotations

import io
import re
import sys
from pathlib import Path

import resvg_py
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "assets/icons/phosphor"
OUT_H = ROOT / "src/ui/icons/PhosphorIcons.generated.h"
OUT_CPP = ROOT / "src/ui/icons/PhosphorIcons.generated.cpp"

SS = 4                     # supersampling
VIEWBOX = "8 8 240 240"    # corta a margem do quadro 256 (Phosphor deixa ~16 u livres)
LADOS = (16, 20, 24, 32)   # tamanhos que as telas usam (meia largura s = 8..16)
LADOS_EXTRA = {"check": (40,)}  # visto grande do anel de celebracao
PESO = "fill"   # decisao do usuario: somente fill (estado do habito = forma/cor do marcador)

# (identificador C++, arquivo svg)
ICONES = [
    ("Barbell", "barbell"), ("Pill", "pill"), ("JarLabel", "jar-label"), ("ForkKnife", "fork-knife"),
    ("Tooth", "tooth"), ("Coffee", "coffee"), ("Drop", "drop"), ("FlowerLotus", "flower-lotus"),
    ("BookOpen", "book-open"), ("Brain", "brain"), ("CalendarCheck", "calendar-check"), ("Prohibit", "prohibit"),
    ("Sun", "sun"), ("CheckCircle", "check-circle"), ("Fire", "fire"), ("Check", "check"),
    ("SkipForward", "skip-forward"), ("ArrowCounterClockwise", "arrow-counter-clockwise"), ("Star", "star"),
    # Tempo v3 (Inicio/Seletor/Batalha)
    ("Play", "play"), ("Pause", "pause"), ("HandPalm", "hand-palm"), ("FlagCheckered", "flag-checkered"),
    ("Kanban", "kanban"), ("GraduationCap", "graduation-cap"), ("Briefcase", "briefcase"), ("CaretUp", "caret-up"),
    ("CaretDown", "caret-down"), ("ArrowsClockwise", "arrows-clockwise"), ("CloudSlash", "cloud-slash"),
    ("PauseCircle", "pause-circle"), ("StarFour", "star-four"),
    # Tempo etapa b (livre): aviso "ainda focando?", ganho ao vivo, aviso "0:40 nao conta"
    ("BellRinging", "bell-ringing"), ("Coins", "coins"), ("Info", "info"),
]


def mascara(svg_path: Path, lado: int) -> bytes:
    txt = svg_path.read_text(encoding="utf-8")
    txt = re.sub(r'viewBox="[^"]*"', f'viewBox="{VIEWBOX}"', txt, count=1)
    txt = txt.replace("currentColor", "#ffffff")
    n = lado * SS
    png = resvg_py.svg_to_bytes(svg_string=txt, width=n, height=n)
    alfa = Image.open(io.BytesIO(bytes(png))).convert("RGBA").getchannel("A")
    alfa = alfa.reduce(SS)  # media em caixa SSxSS
    q = [min(15, (v * 15 + 127) // 255) for v in alfa.tobytes()]
    return bytes((q[i] << 4) | q[i + 1] for i in range(0, len(q), 2))


def gerar() -> tuple[str, str]:
    entradas = []  # (id, peso, lado, nome_array, dados)
    for idx, (ident, nome) in enumerate(ICONES):
        for lado in sorted(set(LADOS) | set(LADOS_EXTRA.get(nome, ()))):
            dados = mascara(SRC / PESO / f"{nome}.svg", lado)
            entradas.append((idx, lado, f"k{ident}{lado}", dados))
    h = ["// Gerado por tools/generate_phosphor_icons.py — NAO editar a mao.",
         "// Icones Phosphor 2.1.1 (MIT, Copyright (c) 2023 Phosphor Icons) — ver THIRD_PARTY_LICENSES.md.",
         "#pragma once", "#include <cstdint>", "", "namespace icons::phosphor {",
         "enum class Id : uint8_t {"]
    h += [f"    {ident}," for ident, _ in ICONES]
    h += ["};", f"inline constexpr unsigned kNumIcones = {len(ICONES)};", "",
          "// Mascara alfa 4 bpp, lado x lado, linhas contiguas, 2 px por byte (1o px = nibble alto).",
          "struct Mascara {", "    Id id;", "    uint8_t lado;", "    const uint8_t* alfa;", "};",
          f"inline constexpr unsigned kNumMascaras = {len(entradas)};",
          "extern const Mascara kMascaras[kNumMascaras];   // ordenadas por (id, lado)", "",
          "} // namespace icons::phosphor", ""]
    c = ["// Gerado por tools/generate_phosphor_icons.py — NAO editar a mao.",
         '#include "ui/icons/PhosphorIcons.generated.h"', "", "namespace icons::phosphor {", "namespace {"]
    for _, _, nome, dados in entradas:
        linhas = []
        for i in range(0, len(dados), 24):
            linhas.append("    " + ",".join(f"0x{b:02x}" for b in dados[i:i + 24]) + ",")
        c.append(f"const uint8_t {nome}[] = {{")
        c += linhas
        c.append("};")
    c += ["} // namespace", "", "const Mascara kMascaras[kNumMascaras] = {"]
    for idx, lado, nome, _ in entradas:
        c.append(f"    {{Id::{ICONES[idx][0]}, {lado}, {nome}}},")
    c += ["};", "", "} // namespace icons::phosphor", ""]
    return "\n".join(h), "\n".join(c)


def main(argv: list[str]) -> int:
    h, c = gerar()
    if "--check" in argv:
        ok = OUT_H.exists() and OUT_CPP.exists() and OUT_H.read_text() == h and OUT_CPP.read_text() == c
        print("PhosphorIcons.generated.*: atualizado" if ok else "PhosphorIcons.generated.*: DESATUALIZADO (rode tools/generate_phosphor_icons.py)")
        return 0 if ok else 1
    OUT_H.write_text(h)
    OUT_CPP.write_text(c)
    print(f"escrito {OUT_H.name} e {OUT_CPP.name}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
