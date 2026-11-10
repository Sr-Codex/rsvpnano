#pragma once

#include <FS.h>

#include <string_view>

#include "tempo/Codec.h"

// Leitura/gravacao do app "Tempo" no cartao SD (JSON). Gravacao atomica: .tmp + rename + .bak,
// como o resto do firmware (StorageFiles::writeFileAtomic).
namespace TempoStorage {

    bool ensureFolders();
    // false = ausente/ilegivel/corrompido (saida intacta).
    bool loadProjects(fs::FS& filesystem, tempo::ListaProjetos& out);
    // Valida (tempo::lerProjetos) e grava /tempo/projetos.json de forma atomica (.tmp + rename + .bak).
    // false = JSON invalido, cartao ausente ou falha de escrita; o arquivo anterior fica intacto.
    bool saveProjects(fs::FS& filesystem, std::string_view json);
    bool loadState(fs::FS& filesystem, tempo::Estado& out);
    bool saveState(fs::FS& filesystem, const tempo::Estado& state);
    bool appendLog(fs::FS& filesystem, const tempo::Data& date, const tempo::EventoLog& event);

} // namespace TempoStorage
