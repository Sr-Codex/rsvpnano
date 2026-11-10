#pragma once

#include <FS.h>

#include "tempo/Codec.h"

// Leitura/gravacao do app "Tempo" no cartao SD (JSON). Gravacao atomica: .tmp + rename + .bak,
// como o resto do firmware (StorageFiles::writeFileAtomic).
namespace TempoStorage {

    bool ensureFolders();
    // false = ausente/ilegivel/corrompido (saida intacta).
    bool loadProjects(fs::FS& filesystem, tempo::ListaProjetos& out);
    bool loadState(fs::FS& filesystem, tempo::Estado& out);
    bool saveState(fs::FS& filesystem, const tempo::Estado& state);
    bool appendLog(fs::FS& filesystem, const tempo::Data& date, const tempo::EventoLog& event);

} // namespace TempoStorage
