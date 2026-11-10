#pragma once

#include <FS.h>

#include <string_view>

#include <vector>

#include "tempo/Codec.h"
#include "tempo/Perfil.h"

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
    // Perfil (lume.5): eventos de um dia (acrescenta em `out`), resumo acumulado /tempo/resumo.json.
    bool loadLogDay(fs::FS& filesystem, const tempo::Data& date, std::vector<tempo::EventoLog>& out);
    bool loadSummary(fs::FS& filesystem, tempo::Resumo& out);
    bool saveSummary(fs::FS& filesystem, const tempo::Resumo& summary);
    // Reconstroi o resumo a partir dos logs dos ultimos 400 dias ate `today` (sem gravar).
    tempo::Resumo rebuildSummary(fs::FS& filesystem, const tempo::Data& today);
    // Chamar DEPOIS de appendLog: soma a batalha ao resumo (ou o reconstroi, ja incluindo a batalha, se faltar).
    bool recordBattle(fs::FS& filesystem, const tempo::EventoLog& event, int streak);
    // Carrega o resumo; se faltar/estiver ilegivel, reconstroi dos logs e grava.
    tempo::Resumo loadOrRebuildSummary(fs::FS& filesystem, const tempo::Data& today);
    bool appendLog(fs::FS& filesystem, const tempo::Data& date, const tempo::EventoLog& event);

} // namespace TempoStorage
