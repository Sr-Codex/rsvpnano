#!/usr/bin/env bash
# Lume Nano simulator: build (incremental) + prepare the virtual microSD + run.
#   sim/run.sh                 → janela 2× com o SD em sim/state/sd
#   sim/run.sh --scale 2       → argumentos extras vão para o simulador (ver sim/README.md)
#   LUME_SIM_STATE=/outro/dir sim/run.sh   → outro "cartão" + NVS
#   sim/run.sh --reset         → apaga sim/state (SD + NVS) e recria do zero
set -euo pipefail
FW="$(cd "$(dirname "$0")/.." && pwd)"          # rsvpnano/
WS="$(cd "$FW/.." && pwd)"                       # LumeNano/ (workspace com .venv, tools, books)
PIO="${PIO:-$WS/.venv/bin/pio}"
PY="${PY:-$WS/.venv/bin/python}"
STATE="${LUME_SIM_STATE:-$FW/sim/state}"
SD="$STATE/sd"

if [ "${1:-}" = "--reset" ]; then rm -rf "$STATE"; shift; fi

# 1) build (SCons recompila só o que mudou)
(cd "$FW" && "$PIO" run -e native_sim -s)

# 2) microSD virtual — só na primeira vez (depois é o "cartão" do usuário, persistente)
if [ ! -d "$SD" ]; then
  echo "[run] criando microSD virtual em $SD"
  mkdir -p "$SD/library/books" "$SD/config"
  unzip -q -o "$FW/locale-packs/pt-BR.zip" -d "$SD"            # /locales/pt-BR
  if compgen -G "$WS/books/rsvp/*.rsvp" > /dev/null; then
    echo "[run] copiando livros de $WS/books/rsvp"
    cp "$WS/books/rsvp/"*.rsvp "$SD/library/books/"
    cp "$WS/books/rsvp/"*.cover.* "$SD/library/books/" 2>/dev/null || true
  else
    echo "[run] sem books/rsvp — gerando livros de teste (Dracula + sample) com tools/epub2rsvp.py"
    "$PY" -I "$WS/tools/epub2rsvp.py" -o "$SD/library/books" \
      "$WS/tools/fixtures/conversion/Dracula-epub.epub" "$WS/tools/fixtures/conversion/sample.epub"
  fi
fi

# 3) app Tempo: se existir a lista de projetos do Mac, copia para o cartão virtual (/tempo/projetos.json)
if [ -f "$WS/data/tempo/projetos.json" ]; then
  mkdir -p "$SD/tempo"
  cp "$WS/data/tempo/projetos.json" "$SD/tempo/projetos.json"
fi

exec "$FW/.pio/build/native_sim/program" --state "$STATE" "$@"
