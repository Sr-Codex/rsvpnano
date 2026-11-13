#!/usr/bin/env python3
"""Verificação automática do leitor no simulador.

Roda o firmware real sem janela, numa cópia descartável do cartão virtual, com a tela lenta como a
placa, e confere o que nenhum teste nativo alcança (cartão + toque + tempo):

  1. pausar grava a posição no cartão, e ela sobrevive a um corte de energia;
  2. ao religar, a leitura continua de onde parou;
  3. um arquivo de posição interrompido no meio da gravação é recuperado (.tmp / .bak), e um
     arquivo estragado não trava o leitor;
  4. arrastar com a leitura parada anda de frase em frase (para trás e para frente) e grava; com as
     palavras correndo, um deslize rápido para a esquerda volta uma frase e segue lendo, e um arrasto
     lento não faz nada;
  5. desligar pelo botão grava a posição uma vez só.

Uso (da raiz do fork, depois de `pio run -e native_sim` e de existir `sim/state`):

    python3 sim/verificar_leitor.py [--prog BINARIO] [--state PASTA] [--atraso MS]

Sai com código 1 se alguma conferência falhar. O `sim/state` não é alterado.
"""
import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path

FORK = Path(__file__).resolve().parents[1]

# Launcher -> Leitor; menu do leitor -> "Voltar ao livro".
ENTRAR = "tap 545,50 @3000; tap 93,141 @5000"
LINHA = 86  # altura do dedo: no meio da área da palavra


def arrastar(xs, inicio_ms, passo_ms):
    """Dedo apoiado em xs[0], passando por cada x a cada passo_ms, e solto no último."""
    roteiro = [f"down {xs[0]},{LINHA} @{inicio_ms}"]
    roteiro += [f"move {x},{LINHA} @{inicio_ms + passo_ms * i}" for i, x in enumerate(xs[1:], 1)]
    roteiro.append(f"up @{inicio_ms + passo_ms * len(xs)}")
    return "; ".join(roteiro)

NO_CARTAO = re.compile(r"position on card word=(\d+)")
RECUPEROU = re.compile(r"recovered position word=(\d+) from (\S+)")
VOLTOU = re.compile(r"rewind sentence (\d+) -> (\d+) \((playing|paused)\)")
ARRASTOU = re.compile(r"scrub sentences (-?\d+): (\d+) -> (\d+)")
ABRIU = re.compile(r"Opened indexed book (\S.*?\.\w+): (\d+) words")


class Simulador:
    def __init__(self, prog: Path, estado: Path, atraso: int):
        self.prog, self.estado, self.atraso = prog, estado, atraso

    def rodar(self, roteiro: str, fim_ms: int) -> str:
        env = dict(os.environ, LUME_SIM_RENDER_DELAY_MS=str(self.atraso))
        feito = subprocess.run([str(self.prog), "--headless", "--state", str(self.estado), "--script",
                                f"{roteiro}; quit @{fim_ms}"], env=env, capture_output=True, text=True,
                               timeout=fim_ms / 1000 + 60)
        saida = feito.stdout + feito.stderr
        if feito.returncode != 0:
            raise RuntimeError(f"o simulador saiu com código {feito.returncode}:\n{saida[-800:]}")
        return saida

    def arquivo_de_posicao(self, saida: str) -> Path:
        abriu = ABRIU.search(saida)
        if not abriu:
            raise RuntimeError("o simulador não abriu nenhum livro (o sim/state tem livros?)")
        livro = Path(abriu.group(1).lstrip("/"))
        return self.estado / "sd" / livro.with_suffix(".rstate.toml")


def palavra_no_arquivo(arquivo: Path) -> int:
    texto = arquivo.read_text(encoding="utf-8") if arquivo.exists() else ""
    achou = re.search(r"\[reading\]\s*\n(?:.*\n)*?wordIndex = (\d+)", texto)
    return int(achou.group(1)) if achou else -1


def com_palavra(modelo: str, palavra: int) -> str:
    return re.sub(r"(\[reading\]\s*\n(?:.*\n)*?wordIndex = )\d+", rf"\g<1>{palavra}", modelo, count=1)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    ap.add_argument("--prog", type=Path, default=FORK / ".pio/build/native_sim/program")
    ap.add_argument("--state", type=Path, default=FORK / "sim/state")
    ap.add_argument("--atraso", type=int, default=80, help="ms a mais por quadro desenhado (placa lenta)")
    a = ap.parse_args()
    if not a.prog.exists() or not (a.state / "sd").is_dir():
        print(f"Falta o simulador ({a.prog}) ou o cartão virtual ({a.state}). Rode sim/run.sh uma vez.")
        return 2

    resultados: list[tuple[bool, str, str]] = []

    def conferir(ok: bool, nome: str, detalhe: str):
        resultados.append((bool(ok), nome, detalhe))
        print(f"  {'PASSOU' if ok else 'FALHOU'}  {nome}: {detalhe}")

    with tempfile.TemporaryDirectory(prefix="lume-leitor-") as pasta:
        estado = Path(pasta) / "state"
        shutil.copytree(a.state, estado, symlinks=True)
        ajustes = estado / "sd/config/settings.toml"
        if ajustes.exists():  # pausa imediata: as posições ficam exatas, sem esperar o fim da frase
            ajustes.write_text(re.sub(r'pauseMode = "\w+"', 'pauseMode = "instant"',
                                      ajustes.read_text(encoding="utf-8")), encoding="utf-8")
        sim = Simulador(a.prog, estado, a.atraso)

        print("1. Pausar grava a posição no cartão")
        saida = sim.rodar(f"{ENTRAR}; press boot @7000; press boot @15000", 17000)
        arquivo = sim.arquivo_de_posicao(saida)
        if arquivo.exists():  # começa do zero, qualquer que seja o estado do sim/state
            arquivo.write_text(com_palavra(arquivo.read_text(encoding="utf-8"), 0), encoding="utf-8")
            saida = sim.rodar(f"{ENTRAR}; press boot @7000; press boot @15000", 17000)
        gravadas = [int(n) for n in NO_CARTAO.findall(saida)]
        p1 = gravadas[-1] if gravadas else -1
        conferir(p1 > 0, "pausa grava", f"leu 8 s, pausou e a energia foi cortada; o registro diz palavra {p1}")
        conferir(palavra_no_arquivo(arquivo) == p1 and p1 > 0, "arquivo no cartão",
                 f"{arquivo.name} ficou com a palavra {palavra_no_arquivo(arquivo)}")
        sobras = [f.name for f in arquivo.parent.glob(arquivo.name + ".*")]
        conferir(not sobras, "sem sobras", f"arquivos temporários ao lado: {sobras or 'nenhum'}")

        print("2. Religar continua de onde parou")
        saida = sim.rodar(f"{ENTRAR}; press boot @7000; press boot @11000", 13000)
        gravadas = [int(n) for n in NO_CARTAO.findall(saida)]
        p2 = gravadas[-1] if gravadas else -1
        conferir(p2 > p1 > 0, "continua", f"parou na {p1}; depois de religar e ler mais 4 s está na {p2}")

        if not arquivo.exists():
            print("\nO arquivo de posição nem chegou a existir: as outras conferências dependem dele.")
            return 1

        print("3. Gravação interrompida no meio")
        modelo = arquivo.read_text(encoding="utf-8")
        for nome, preparar, esperado in (
            (".tmp completo, principal sumiu", lambda: (arquivo.unlink(),
                                                         Path(str(arquivo) + ".tmp").write_text(com_palavra(modelo, 500)),
                                                         Path(str(arquivo) + ".bak").write_text(com_palavra(modelo, 100))), 500),
            ("principal vazio, .bak antigo", lambda: (arquivo.write_text(""),
                                                       Path(str(arquivo) + ".bak").write_text(com_palavra(modelo, 100))), 100),
            ("principal cortado, sem cópias", lambda: (arquivo.write_text(modelo[: len(modelo) // 3]),), None),
        ):
            for sobra in arquivo.parent.glob(arquivo.name + ".*"):
                sobra.unlink()
            preparar()
            saida = sim.rodar(f"{ENTRAR}; press boot @7000; press boot @9000", 11000)
            recuperou = RECUPEROU.search(saida)
            final = palavra_no_arquivo(arquivo)
            if esperado is None:
                conferir(recuperou is None and 0 < final < 200, nome,
                         f"abriu do começo sem travar e voltou a gravar (palavra {final})")
            else:
                conferir(bool(recuperou) and int(recuperou.group(1)) == esperado and final >= esperado, nome,
                         f"recuperou a palavra {recuperou.group(1) if recuperou else '?'} "
                         f"(esperado {esperado}); depois de ler mais um pouco: {final}")

        print("4. Arrastar por frase (parado) e deslize para voltar (lendo)")
        arquivo.write_text(com_palavra(modelo, 60), encoding="utf-8")
        for sobra in arquivo.parent.glob(arquivo.name + ".*"):
            sobra.unlink()
        # 150 px para a esquerda = 2 frases; depois 70 px para a direita = 1 frase.
        saida = sim.rodar(f"{ENTRAR}; {arrastar([450, 420, 380, 340, 300], 7000, 150)}; "
                          f"{arrastar([300, 330, 370], 9000, 150)}", 10500)
        arrastos = [(int(n), int(de), int(para)) for n, de, para in ARRASTOU.findall(saida)]
        tras = arrastos[0] if arrastos else None
        conferir(bool(tras) and tras[0] == -2 and tras[2] < tras[1], "parado, 2 frases para trás",
                 f"{tras[0]} frases: palavra {tras[1]} → {tras[2]}" if tras else "nenhum arrasto registrado")
        frente = arrastos[1] if len(arrastos) > 1 else None
        conferir(bool(frente) and bool(tras) and frente[0] == 1 and tras[2] < frente[2] <= tras[1],
                 "parado, 1 frase para frente",
                 f"+{frente[0]} frase: palavra {frente[1]} → {frente[2]}" if frente else "nenhum arrasto registrado")
        conferir(bool(frente) and palavra_no_arquivo(arquivo) == frente[2], "parado, gravou",
                 f"o cartão ficou com a palavra {palavra_no_arquivo(arquivo)}")

        arquivo.write_text(com_palavra(modelo, 60), encoding="utf-8")
        # Lendo: deslize rápido (240 ms) e depois um arrasto lento (1,2 s), que não pode fazer nada.
        saida = sim.rodar(f"{ENTRAR}; press boot @7000; {arrastar([450, 400, 350], 10000, 80)}; "
                          f"{arrastar([450, 430, 400, 370], 11500, 300)}; press boot @14000", 16000)
        voltas = [(int(de), int(para), modo) for de, para, modo in VOLTOU.findall(saida)]
        gravadas = [int(n) for n in NO_CARTAO.findall(saida)]
        lendo = len(voltas) == 1 and voltas[0][2] == "playing"
        conferir(lendo and voltas[0][1] < voltas[0][0], "lendo, deslize voltou",
                 f"palavra {voltas[0][0]} → {voltas[0][1]}" if voltas else "nenhum deslize registrado")
        conferir(len(voltas) <= 1 and not ARRASTOU.search(saida), "lendo, arrasto lento não faz nada",
                 f"deslizes registrados: {len(voltas)}; arrastos: {len(ARRASTOU.findall(saida))}")
        conferir(lendo and len(gravadas) == 1 and gravadas[0] > voltas[0][1], "lendo, seguiu lendo",
                 f"gravações no cartão: {gravadas or 'nenhuma'} (só a do BOOT, 4 s depois do deslize; o arrasto lento "
                 f"não pode ter pausado)")

        arquivo.write_text(com_palavra(modelo, 60), encoding="utf-8")
        saida = sim.rodar(f"{ENTRAR}; press boot @7000; tap 320,{LINHA} @10000", 12500)
        gravadas = [int(n) for n in NO_CARTAO.findall(saida)]
        conferir(len(gravadas) == 1 and gravadas[0] > 60 and palavra_no_arquivo(arquivo) == gravadas[0],
                 "lendo, toque simples pausa", f"gravações no cartão: {gravadas or 'nenhuma'}; arquivo na palavra "
                 f"{palavra_no_arquivo(arquivo)}")

        print("5. Desligar pelo botão grava uma vez só")
        arquivo.write_text(com_palavra(modelo, 60), encoding="utf-8")
        saida = sim.rodar(f"{ENTRAR}; press boot @7000; long power 2500 @10000", 16000)
        gravadas = [int(n) for n in NO_CARTAO.findall(saida)]
        conferir(len(gravadas) == 1 and gravadas[0] > 60 and palavra_no_arquivo(arquivo) == gravadas[0],
                 "desligar lendo", f"gravações no cartão: {gravadas or 'nenhuma'}; arquivo na palavra "
                 f"{palavra_no_arquivo(arquivo)}")

    falhas = [nome for ok, nome, _ in resultados if not ok]
    print(f"\n{len(resultados) - len(falhas)} de {len(resultados)} conferências passaram"
          + (f"; FALHARAM: {', '.join(falhas)}" if falhas else "."))
    return 1 if falhas else 0


if __name__ == "__main__":
    sys.exit(main())
