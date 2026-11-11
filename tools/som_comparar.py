#!/usr/bin/env python3
"""Compara os WAV de referencia do usuario (44,1 kHz) com a previa gerada pelo sintetizador C++ (16 kHz).

Uso: python3 tools/som_comparar.py <pasta-referencia> <pasta-previa>

Metricas por som:
  dur  : duracao de cada um (ms) e a diferenca; tolerancia = 1 bloco de 256 amostras (16 ms a 16 kHz).
  corr : correlacao de Pearson da FORMA DE ONDA, com o nosso (16 kHz) segurado em degrau ate 44,1 kHz (uma quadrada
         e constante por trechos; o desvio restante e so o erro de ate 1/16000 s na posicao de cada borda).
  env  : correlacao do envelope RMS em janelas de 10 ms (forma do ataque/decaimento/pausas).
  freq : erro relativo mediano da frequencia (cruzamentos de zero em janelas de 20 ms com sinal).
"""
import math
import struct
import sys
import wave
from pathlib import Path


def ler(path):
    with wave.open(str(path)) as w:
        assert w.getnchannels() == 1 and w.getsampwidth() == 2
        raw = w.readframes(w.getnframes())
        return list(struct.unpack("<%dh" % (len(raw) // 2), raw)), w.getframerate()


def pearson(a, b):
    n = min(len(a), len(b))
    a, b = a[:n], b[:n]
    ma, mb = sum(a) / n, sum(b) / n
    sa = math.sqrt(sum((x - ma) ** 2 for x in a))
    sb = math.sqrt(sum((x - mb) ** 2 for x in b))
    if sa == 0 or sb == 0:
        return 1.0 if sa == sb else 0.0
    return sum((x - ma) * (y - mb) for x, y in zip(a, b)) / (sa * sb)


def degrau(x, taxa_de, taxa_para, n):
    return [x[min(len(x) - 1, int(i * taxa_de / taxa_para))] for i in range(n)]


def envelope(x, taxa, janela_ms=10):
    j = int(taxa * janela_ms / 1000)
    return [math.sqrt(sum(v * v for v in x[i:i + j]) / j) for i in range(0, len(x) - j + 1, j)]


def freq_janelas(x, taxa, janela_ms=20):
    j = int(taxa * janela_ms / 1000)
    out = []
    for i in range(0, len(x) - j + 1, j):
        seg = x[i:i + j]
        if max(abs(v) for v in seg) < 400:
            out.append(None)
            continue
        cruz = sum(1 for k in range(1, j) if seg[k - 1] != 0 and (seg[k - 1] > 0) != (seg[k] > 0))
        out.append(cruz / 2 / (j / taxa))
    return out


def main():
    ref_dir, prev_dir = Path(sys.argv[1]), Path(sys.argv[2])
    print(f"{'som':20} {'ref ms':>8} {'nosso ms':>9} {'dif ms':>7} {'corr':>6} {'env':>6} {'freq err':>9}")
    for ref_path in sorted(ref_dir.glob("*.wav"), key=lambda p: int(p.name.split("-")[0])):
        prev_path = prev_dir / ref_path.name
        if not prev_path.exists():
            print(f"{ref_path.stem:20} (sem previa)")
            continue
        ref, tr = ler(ref_path)
        nos, tn = ler(prev_path)
        dur_r, dur_n = len(ref) * 1000 / tr, len(nos) * 1000 / tn
        n = min(len(ref), int(len(nos) * tr / tn))
        corr = pearson(ref[:n], degrau(nos, tn, tr, n))
        e1, e2 = envelope(ref, tr), envelope(nos, tn)
        env = pearson(e1, e2)
        f1, f2 = freq_janelas(ref, tr), freq_janelas(nos, tn)
        erros = sorted(abs(a - b) / a for a, b in zip(f1, f2) if a and b)
        ferr = erros[len(erros) // 2] if erros else float("nan")
        print(f"{ref_path.stem:20} {dur_r:8.2f} {dur_n:9.2f} {dur_n - dur_r:7.2f} {corr:6.3f} {env:6.3f} {ferr:9.4f}")


if __name__ == "__main__":
    main()
