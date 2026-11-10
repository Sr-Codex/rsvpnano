#pragma once

#include <FS.h>

#include <string_view>
#include <utility>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/Habitos.h"
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
    // Habitos (app Habitos, fase 1): definicoes vindas do sync (/tempo/habitos.json), check-ins de hoje baixados do
    // TickTick (/tempo/habitos-checkins.json) e fila de check-ins a enviar (/tempo/habitos-fila.jsonl).
    bool loadHabits(fs::FS& filesystem, tempo::ListaHabitos& out);
    bool saveHabits(fs::FS& filesystem, std::string_view json);          // valida (tempo::lerHabitos) e grava atomico
    bool loadHabitCheckins(fs::FS& filesystem, tempo::ArquivoCheckins& out);
    bool saveHabitCheckins(fs::FS& filesystem, std::string_view json);   // valida e grava atomico
    // Fila: arquivo ausente = fila vazia. As operacoes abaixo sao serializadas por um mutex (a UI e o job de sync
    // do TickTick mexem na fila de tarefas diferentes) e releem o arquivo antes de gravar.
    std::vector<tempo::ItemFila> loadQueue(fs::FS& filesystem);
    bool enqueueCheckin(fs::FS& filesystem, const tempo::ItemFila& item);
    // Aplica os resultados dos envios (item enviado, status HTTP; 0 = sem rede) a fila atual do SD.
    bool applyQueueResults(fs::FS& filesystem, const std::vector<std::pair<tempo::ItemFila, int>>& results);
    bool appendLog(fs::FS& filesystem, const tempo::Data& date, const tempo::EventoLog& event);

} // namespace TempoStorage
