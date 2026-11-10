#include "storage/tempo/TempoStorage.h"

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
