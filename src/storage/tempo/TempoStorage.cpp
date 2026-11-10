#include "storage/tempo/TempoStorage.h"

#include <algorithm>

#include "storage/fs/StorageFiles.h"

namespace TempoStorage {

    bool ensureFolders() {
        return StorageFiles::ensureDirectory(tempo::kPastaTempo) && StorageFiles::ensureDirectory(tempo::kPastaLog);
    }

    bool loadProjects(fs::FS& filesystem, tempo::ListaProjetos& out) {
        auto text = StorageFiles::readTextFile(filesystem, tempo::kCaminhoProjetos, tempo::kMaxBytesProjetos);
        if (text && tempo::lerProjetos(*text, out)) return true;
        // gravacao interrompida entre os renames: o .bak guarda a ultima versao boa
        auto backup = StorageFiles::readTextFile(filesystem, "/tempo/projetos.json.bak", tempo::kMaxBytesProjetos);
        return backup && tempo::lerProjetos(*backup, out);
    }

    bool saveProjects(fs::FS& filesystem, std::string_view json) {
        tempo::ListaProjetos check;
        if (!tempo::lerProjetos(json, check) || !ensureFolders()) return false;
        return StorageFiles::writeFileAtomic(filesystem, tempo::kCaminhoProjetos, "/tempo/projetos.json.tmp",
                                             "/tempo/projetos.json.bak", json)
            .has_value();
    }

    bool loadState(fs::FS& filesystem, tempo::Estado& out) {
        auto text = StorageFiles::readTextFile(filesystem, tempo::kCaminhoEstado, tempo::kMaxBytesEstado);
        if (text && tempo::lerEstado(*text, out)) return true;
        // gravacao interrompida entre os renames: o .bak guarda a ultima versao boa
        auto backup = StorageFiles::readTextFile(filesystem, tempo::kCaminhoEstadoBak, tempo::kMaxBytesEstado);
        return backup && tempo::lerEstado(*backup, out);
    }

    bool saveState(fs::FS& filesystem, const tempo::Estado& state) {
        const std::string json = tempo::serializarEstado(state);
        if (json.empty() || !ensureFolders()) return false;
        return StorageFiles::writeFileAtomic(filesystem, tempo::kCaminhoEstado, tempo::kCaminhoEstadoTmp,
                                             tempo::kCaminhoEstadoBak, json)
            .has_value();
    }

    bool loadLogDay(fs::FS& filesystem, const tempo::Data& date, std::vector<tempo::EventoLog>& out) {
        const std::string path = tempo::caminhoLog(date);
        if (!filesystem.exists(path.c_str())) return false;
        auto text = StorageFiles::readTextFile(filesystem, path.c_str(), 256 * 1024);
        if (!text) return false;
        std::string_view rest = *text;
        while (!rest.empty()) {
            const size_t nl = rest.find('\n');
            const std::string_view line = rest.substr(0, nl);
            tempo::EventoLog event;
            if (tempo::lerLinhaLog(line, event)) out.push_back(std::move(event));
            rest = nl == std::string_view::npos ? std::string_view{} : rest.substr(nl + 1);
        }
        return true;
    }

    bool loadSummary(fs::FS& filesystem, tempo::Resumo& out) {
        auto text = StorageFiles::readTextFile(filesystem, tempo::kCaminhoResumo, tempo::kMaxBytesResumo);
        if (text && tempo::lerResumo(*text, out)) return true;
        auto backup = StorageFiles::readTextFile(filesystem, tempo::kCaminhoResumoBak, tempo::kMaxBytesResumo);
        return backup && tempo::lerResumo(*backup, out);
    }

    bool saveSummary(fs::FS& filesystem, const tempo::Resumo& summary) {
        const std::string json = tempo::serializarResumo(summary);
        if (json.empty() || !ensureFolders()) return false;
        return StorageFiles::writeFileAtomic(filesystem, tempo::kCaminhoResumo, tempo::kCaminhoResumoTmp,
                                             tempo::kCaminhoResumoBak, json)
            .has_value();
    }

    tempo::Resumo rebuildSummary(fs::FS& filesystem, const tempo::Data& today) {
        std::vector<tempo::EventoLog> events;
        const int64_t last = tempo::diaSerial(today);
        for (int64_t day = last - int64_t(tempo::kMaxDiasResumo) + 1; day <= last; ++day)
            loadLogDay(filesystem, tempo::dataDeSerial(day), events);
        return tempo::reconstruirResumo(events);
    }

    bool recordBattle(fs::FS& filesystem, const tempo::EventoLog& event, int streak) {
        tempo::Resumo summary;
        if (loadSummary(filesystem, summary)) {
            tempo::registrarEvento(summary, event, streak);
        } else {
            tempo::Data day;
            if (!tempo::lerDataTexto(event.data, day)) return false;
            summary = rebuildSummary(filesystem, day);   // o log ja contem esta batalha
            summary.maxStreak = std::max(summary.maxStreak, streak);
        }
        return saveSummary(filesystem, summary);
    }

    tempo::Resumo loadOrRebuildSummary(fs::FS& filesystem, const tempo::Data& today) {
        tempo::Resumo summary;
        if (loadSummary(filesystem, summary)) return summary;
        summary = rebuildSummary(filesystem, today);
        if (summary.batalhas > 0) saveSummary(filesystem, summary);
        return summary;
    }

    bool appendLog(fs::FS& filesystem, const tempo::Data& date, const tempo::EventoLog& event) {
        const std::string line = tempo::linhaLog(event);
        if (line.empty() || !ensureFolders()) return false;
        File file = filesystem.open(tempo::caminhoLog(date).c_str(), FILE_APPEND);
        if (!file) return false;
        const size_t written = file.write(reinterpret_cast<const uint8_t*>(line.data()), line.size());
        file.flush();
        file.close();
        return written == line.size();
    }

} // namespace TempoStorage
