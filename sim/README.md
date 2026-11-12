# Simulador do Lume Nano (macOS/Linux, SDL2)

Roda o **firmware real** (`src/app/App.cpp` e todas as telas, leitor, biblioteca, foco, ajustes) no
computador, numa placa virtual **Waveshare ESP32-S3-Touch-LCD-3.49 Rev2**. Você clica e testa sem
compilar para a placa nem gravar por OTA. Decisão: `docs/adr/008-simulador-sdl-firmware-inteiro.md`
(repo `lume-nano`).

## Usar

```bash
sim/run.sh                 # compila (incremental), prepara o microSD virtual e abre a janela no tamanho real (1×)
sim/run.sh --scale 2       # janela 2× (padrão = 1×, tamanho real 640×172)
sim/run.sh --reset         # apaga o cartão + NVS virtuais e começa do zero (onboarding de novo)
LUME_SIM_STATE=~/outro sim/run.sh     # outro "cartão" + NVS
```

Pré-requisitos: `brew install sdl2` (Linux: `libsdl2-dev`) e o `.venv` do workspace
(`../scripts/setup.sh` em `lume-nano`). O ambiente PlatformIO é `native_sim`
(`pio run -e native_sim`; binário em `.pio/build/native_sim/program`).

**Estado persistente** em `sim/state/` (ignorado pelo git):
- `sim/state/sd/` = **microSD** (mesma estrutura do cartão: `/library/books`, `/config/settings.toml`,
  `/locales`, `/fonts`, `/themes`…). Copie livros `.rsvp` + `Livro.cover.jpg` para `sd/library/books/`.
  Como na placa, o cartão só é lido no boot: reinicie o simulador depois de mexer.
- `sim/state/nvs/` = NVS (Preferences). `esp_restart()` reinicia o simulador mantendo os dois.

Na 1ª execução o `run.sh` monta o cartão com o pacote **pt-BR** (`locale-packs/pt-BR.zip`) e os livros de
`../books/rsvp/` (se existir) ou, senão, gera **Dracula** + **Letter** com `tools/epub2rsvp.py`.

## Controles

| Entrada | Placa |
| --- | --- |
| Clique / arrastar com o mouse | Toque (tocar, segurar ≥ 600 ms, deslizar) |
| **`Z`** (ou `B`/`Espaço`) | Botão **BOOT** (clique = selecionar/play-pause; segurar = descanso) |
| **`X`** (ou `P`/`Esc`) | Botão **PWR** (clique = menu/voltar; segurar = desligar → fecha o simulador) |
| **`C`** | Botão **RESET** (atrás da placa): reinicia o firmware mantendo cartão e NVS — como o reset real, relê o SD |
| `1` | IMU: deitada, tela para cima (padrão) → `Flat` |
| `2` | IMU: deitada, tela para baixo → `Flat` |
| `3` | IMU: em pé, **ponta A** para cima → `ShortA` |
| `4` | IMU: em pé, **ponta B** para cima → `ShortB` |
| `H` | IMU: chacoalhar / pegou na mão (pulso de ~0,7 s, até 1,6 g) |
| `R` | Girar a placa 90° (rotação manual da janela; o toque acompanha) |
| `[` / `]` | Bateria −/+ 10 % |
| `U` | Liga/desliga alimentação USB (ícone de carregando) |
| `S` | Captura PNG da tela em `sim/state/screenshots/` |
| `Q` | Sair |

**Orientação:** a janela segue a orientação que o firmware usa (`ui::Context` → `gfx.setRotation`).
Se a interface for para retrato (172×640) ou girar 180° (modo canhoto), a janela gira junto e o
mapeamento mouse→toque também. `R` soma giros manuais por cima disso.

**IMU:** o QMI8658 é emulado por registrador (WHO_AM_I `0x05`, reset `0x60`/`0x4D`, `CTRL7` liga o
acelerômetro, dados em `0x35…0x3A`, ±4 g). É o que `focus::OrientationReader` lê de verdade.

## Testes sem janela (roteiro)

```bash
.pio/build/native_sim/program --headless --state sim/state \
  --script "tap 597,74 @2000; tap 417,20 @3000; shot /tmp/menu.png @4500; quit @5000"
```

Verbos (`@ms` = tempo desde o início): `tap X,Y` · `hold X,Y MS` · `drag X1,Y1 X2,Y2 MS` ·
`down X,Y` / `move X,Y` / `up` (dedo apoiado, para capturar no meio de um arrasto) · `press boot|power` · `long boot|power MS` · `pose up|down|a|b` · `shake` · `turn` · `shot ARQ.png` ·
`imucheck` (autoteste: o `OrientationReader` real classifica cada pose) · `quit`.
Coordenadas = as da janela (640×172 deitada). Log de depuração: `LUME_SIM_DEBUG=1`.
As capturas aplicam o brilho do painel (0,35 + 0,65 × %); `LUME_SIM_SHOT_RAW=1` grava os pixels exatamente como o
firmware os escreveu (sem brilho), para comparar com os mockups.

**Render lento (como a placa):** `LUME_SIM_RENDER_DELAY_MS=80` faz cada quadro desenhado levar 80 ms a mais (o Mac desenha
em ~1 ms; na placa a batalha do Tempo passa dos 30 ms entre leituras do toque). Serve para reproduzir defeitos que dependem da
fila de toques acumulando (ex.: "Fim · segure" que encerrava depois de soltar, lume.21). Roteiros em
`docs/simulador/toque-atraso/` (repo `lume-nano`). Só o simulador lê a variável (`sim/platform/SimBoard.cpp`).

**Escala de tempo (app Tempo):** `LUME_SIM_TIME_SCALE=60 sim/run.sh` faz o relógio do app Tempo (batalha,
data/hora) andar 60× mais rápido: 1 min real = 1 h, então uma batalha de 25 min termina em 25 s. Só o
simulador lê a variável (`sim/main.cpp`); o roteiro `@ms` continua em tempo real e o resto do firmware
(toque, standby, leitor) não é acelerado. Sem a variável, escala 1.

**Relógio de parede do app Tempo:** `LUME_SIM_CLOCK=2026-11-10T14:20 sim/run.sh` ancora a hora do app (Tempo/Hábitos, hora local do Mac)
nesse instante e ela segue andando (combine com `LUME_SIM_TIME_SCALE`). Serve para capturar os Hábitos em horários específicos; só o simulador lê a variável.
**Deep sleep com despertador (ADR-009):** sem sono real; `enter()` registra `[sim] deep sleep ate HH:MM` e espera o PWR (boot normal).
`LUME_SIM_DEEPSLEEP_WAKE=1` religa na hora como se o timer disparasse (boot de lembrete, relógio no instante do despertar);
`LUME_SIM_BOOT_CAUSE=timer|button` + `LUME_SIM_RTC=...` sobem direto um boot por timer/botão. `LUME_SIM_CLOCK` aceita segundos (`...T19:59:55`).

**Projetos do app Tempo:** `sim/run.sh` copia `$WS/data/tempo/projetos.json` (se existir) para
`sim/state/sd/tempo/projetos.json` a cada execução (arquivo do Mac, nunca vai para o repositório). Estado e
log gravados pelo app ficam em `sim/state/sd/tempo/estado.json` e `sim/state/sd/tempo/log/AAAA-MM-DD.jsonl`.

## O que é e o que não é simulado

| Igual à placa (mesmo código) | Só na placa |
| --- | --- |
| Telas, layout, fontes, cores, temas, textos pt-BR | Velocidade real (o Mac é muito mais rápido) |
| Navegação, toque, gestos, botões BOOT/PWR | Memória: RAM/PSRAM, falta de memória, stack das tasks |
| Leitor RSVP, capítulos, progresso, biblioteca, capas (JPEGDEC real) | Painel físico (brilho/cor real, QSPI) e precisão do toque |
| Foco/pomodoro, ajustes (TOML ⇄ NVS), idiomas (SHA-256 real) | Wi-Fi, OTA, companion HTTP/USB, pen drive (MSC), RSS, dicionário online → **"Indisponível no simulador"** |
| Tarefas FreeRTOS (threads reais), filas, tempo real | Áudio (beep), sono leve real, consumo de bateria |
| IMU (pose por tecla), bateria/USB (valores por tecla) | Escalonamento exato do FreeRTOS em 2 núcleos |

Implementação: `sim/support/` (Arduino, FS, Preferences, FreeRTOS, ESP-IDF mínimos), `sim/platform/`
(placa virtual: tela, toque, botões, energia, SD, IMU), `sim/stubs/` (rede/USB/OTA indisponíveis),
`sim/main.cpp` (janela SDL, roteiro, PNG), `sim/pio_sim.py` (integração PlatformIO). O firmware da placa
não muda: nenhum arquivo de `src/` foi alterado para o simulador.

**Funcionalidade nova que usar hardware** (sensor, rádio, áudio…) precisa de um equivalente em
`sim/platform/` ou `sim/stubs/` — senão o `native_sim` não linka.
