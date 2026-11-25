# Lume Nano fork (read first)

This is the `Sr-Codex/rsvpnano` fork. The product line is **`feat/app-tempo`** (Lume Nano firmware, releases `v0.2.0-rev2.1-lume.N`,
installed by OTA). Publishing a release needs the owner's explicit approval. This repository is public: no personal data, purchased
assets, tokens or Wi-Fi names. Run `pio test -e native_test` and `sim/verificar_leitor.py` (reader changes) before calling work done.
Never edit `*.generated.*` by hand. The section below comes unchanged from upstream and applies when proposing work **to upstream**.

---

# RSVP Nano UI and dictionary release

Read `docs/releases/ui-dictionary-preview.md` for scope and release requirements.
The founder approved this scoped implementation and GitHub source publication.
The complete ecosystem prototype is preserved separately; do not add the new
Android app, notes, recording, Bluetooth phone sync or private service data here.

Keep Classic available and the default stable release. New UI is an explicitly
selected preview for LCD 3.49 Rev1; this branch also prepares a local Rev2 candidate.
The founder requires review before ANY push or publication of this Rev2 work.
Read `docs/releases/lcd349-rev2-candidate.md`. Use unique release tags and separate merged
browser and application-only OTA artifacts. Never move published tags, overwrite
assets or change flash partitions. Preserve SD books, progress and deferred data.

Generate review images from production renderers with synthetic hardware/data
fixtures. Preserve screen IDs and exported comments across board regeneration.
Host builds and captures do not establish physical touch, OTA or reading speed.
Public firmware publication follows candidate review and actual device acceptance.
Keep installer changes separable from firmware changes to the stable branch.

The founder approved publishing English dictionary data on GitHub independently
of firmware. The data release must not become Latest or change the firmware update
feeds. English only for now; the UI should say more languages are planned.
