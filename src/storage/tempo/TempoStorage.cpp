#include "storage/tempo/TempoStorage.h"

#include <algorithm>
#include <mutex>
#include <system_error>

#include "storage/fs/StorageFiles.h"

namespace TempoStorage {
    namespace {
        std::mutex gRegistroMutex;

        std::optional<std::vector<tempo::ItemFila>> readQueue(fs::FS& fs) {
            const char* path = tempo::kCaminhoFilaHabitos;
            if (!fs.exists(path)) path = tempo::kCaminhoFilaHabitosBak;
            if (!fs.exists(path)) return std::vector<tempo::ItemFila>{};
            File f = fs.open(path, FILE_READ);
            if (!f || f.isDirectory()) return std::nullopt;
            if (f.size() == 0) return std::vector<tempo::ItemFila>{};
            f.close();
            auto texto = StorageFiles::readTextFile(fs, path, tempo::kMaxBytesFila);
            if (!texto) return std::nullopt;
            auto fila = tempo::lerFila(*texto);
            size_t linhas = 0;
            for (std::string_view resto = *texto; !resto.empty();) {
                const auto fim = resto.find('\n');
                if (!resto.substr(0, fim).empty()) ++linhas;
                if (fim == resto.npos) break;
                resto.remove_prefix(fim + 1);
            }
            if (linhas != fila.size()) return std::nullopt;
            return fila;
        }

        bool writeQueue(fs::FS& filesystem, const std::vector<tempo::ItemFila>& queue) {
            if (!TempoStorage::ensureFolders()) return false;
            const auto texto = tempo::serializarFila(queue);
            if (!StorageFiles::writeFileAtomic(filesystem, tempo::kCaminhoFilaHabitos, tempo::kCaminhoFilaHabitosTmp,
                                               tempo::kCaminhoFilaHabitosBak, texto)) return false;
            if (!filesystem.exists(tempo::kCaminhoFilaHabitos)) return false;
            const auto salvo = readQueue(filesystem);
            return salvo && tempo::serializarFila(*salvo) == texto;
        }
    } // namespace

    bool ensureFolders() {
        return StorageFiles::ensureDirectory(tempo::kPastaTempo) && StorageFiles::ensureDirectory(tempo::kPastaLog);
    }

    bool recuperarRegistroHabito(fs::FS& fs) {
        const std::lock_guard lock{gRegistroMutex};
        return registro::recuperar(fs);
    }

    GravacaoHabito registrarHabito(fs::FS& fs, const tempo::Estado& novo, const std::vector<tempo::EventoLog>& eventos,
                                   const tempo::ItemFila* item, bool desfazer) {
        const std::lock_guard lock{gRegistroMutex};
        if (!registro::recuperar(fs) || !ensureFolders()) return GravacaoHabito::Recusada;
        std::string linhas;
        for (const auto& evento : eventos) {
            const auto linha = tempo::linhaLog(evento);
            if (linha.empty()) return GravacaoHabito::Recusada;
            linhas += linha;
        }
        std::string jsonFila;
        if (item) {
            auto fila = readQueue(fs);
            if (!fila) return GravacaoHabito::Recusada;
            const bool existe = std::any_of(fila->begin(), fila->end(), [&](const auto& i) {
                return i.id == item->id && i.stamp == item->stamp;
            });
            if (!existe && fila->size() >= tempo::kMaxFila) return GravacaoHabito::FilaCheia;
            if (desfazer) tempo::desfazerNaFila(*fila, *item);
            else tempo::enfileirar(*fila, *item);
            jsonFila = tempo::serializarFila(*fila);
        }
        return registro::gravar(fs, novo, linhas, item ? std::optional<std::string_view>{jsonFila} : std::nullopt);
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
        const std::lock_guard lock{gRegistroMutex};
        if (!registro::recuperar(filesystem)) {
            return registro::lerEstadoPendente(filesystem, out);
        }
        auto text = StorageFiles::readTextFile(filesystem, tempo::kCaminhoEstado, tempo::kMaxBytesEstado);
        if (text && tempo::lerEstado(*text, out)) return true;
        // gravacao interrompida entre os renames: o .bak guarda a ultima versao boa
        auto backup = StorageFiles::readTextFile(filesystem, tempo::kCaminhoEstadoBak, tempo::kMaxBytesEstado);
        return backup && tempo::lerEstado(*backup, out);
    }

    tempo::cartao::Leitura loadStateChecked(fs::FS& filesystem, tempo::Estado& out) {
        const std::lock_guard lock{gRegistroMutex};
        if (!registro::recuperar(filesystem)) {
            return registro::lerEstadoPendente(filesystem, out) ? tempo::cartao::Leitura::Ok : tempo::cartao::Leitura::Ilegivel;
        }
        using tempo::cartao::Arquivo;
        const auto ler = [&](const char* caminho, tempo::Estado& destino) {
            auto text = StorageFiles::readTextFile(filesystem, caminho, tempo::kMaxBytesEstado);
            if (text) return tempo::lerEstado(*text, destino) ? Arquivo::Valido : Arquivo::Invalido;
            return text.error() == std::make_error_code(std::errc::no_such_file_or_directory) ? Arquivo::Ausente
                                                                                                : Arquivo::Invalido;
        };
        tempo::Estado lido;
        const Arquivo estado = ler(tempo::kCaminhoEstado, lido);
        if (estado == Arquivo::Valido) {
            out = lido;
            return tempo::cartao::Leitura::Ok;
        }
        lido = tempo::Estado{};
        const Arquivo backup = ler(tempo::kCaminhoEstadoBak, lido);
        if (backup == Arquivo::Valido) {
            out = lido;
            return tempo::cartao::Leitura::Ok;
        }
        // estado sumiu: se o Perfil (resumo) ou o .bak dele existem, nao e cartao novo
        const bool outros = filesystem.exists(tempo::kCaminhoResumo) || filesystem.exists(tempo::kCaminhoResumoBak);
        return tempo::cartao::classificar(estado, backup, outros);
    }

    bool saveState(fs::FS& filesystem, const tempo::Estado& state) {
        const std::lock_guard lock{gRegistroMutex};
        if (!registro::recuperar(filesystem)) return false;
        const std::string json = tempo::serializarEstado(state);
        if (json.empty() || !ensureFolders()) return false;
        return StorageFiles::writeFileAtomic(filesystem, tempo::kCaminhoEstado, tempo::kCaminhoEstadoTmp,
                                             tempo::kCaminhoEstadoBak, json)
            .has_value();
    }

    bool loadLogDay(fs::FS& filesystem, const tempo::Data& date, std::vector<tempo::EventoLog>& out) {
        const std::lock_guard lock{gRegistroMutex};
        if (registro::diaPendente(filesystem, date)) return false;
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

    bool loadHabits(fs::FS& filesystem, tempo::ListaHabitos& out) {
        auto text = StorageFiles::readTextFile(filesystem, tempo::kCaminhoHabitos, tempo::kMaxBytesHabitos);
        if (text && tempo::lerHabitos(*text, out)) return true;
        auto backup = StorageFiles::readTextFile(filesystem, tempo::kCaminhoHabitosBak, tempo::kMaxBytesHabitos);
        return backup && tempo::lerHabitos(*backup, out);
    }

    bool saveHabits(fs::FS& filesystem, std::string_view json) {
        tempo::ListaHabitos check;
        if (!tempo::lerHabitos(json, check) || !ensureFolders()) return false;
        return StorageFiles::writeFileAtomic(filesystem, tempo::kCaminhoHabitos, tempo::kCaminhoHabitosTmp,
                                             tempo::kCaminhoHabitosBak, json)
            .has_value();
    }

    bool loadHabitCheckins(fs::FS& filesystem, tempo::ArquivoCheckins& out) {
        auto text = StorageFiles::readTextFile(filesystem, tempo::kCaminhoCheckinsHoje, tempo::kMaxBytesCheckins);
        if (text && tempo::lerCheckins(*text, out)) return true;
        auto backup = StorageFiles::readTextFile(filesystem, tempo::kCaminhoCheckinsHojeBak, tempo::kMaxBytesCheckins);
        return backup && tempo::lerCheckins(*backup, out);
    }

    bool saveHabitCheckins(fs::FS& filesystem, std::string_view json) {
        tempo::ArquivoCheckins check;
        if (!tempo::lerCheckins(json, check) || !ensureFolders()) return false;
        return StorageFiles::writeFileAtomic(filesystem, tempo::kCaminhoCheckinsHoje, tempo::kCaminhoCheckinsHojeTmp,
                                             tempo::kCaminhoCheckinsHojeBak, json)
            .has_value();
    }

    std::optional<std::vector<tempo::ItemFila>> loadQueue(fs::FS& filesystem) {
        const std::lock_guard lock{gRegistroMutex};
        if (!registro::recuperar(filesystem)) return std::nullopt;
        return readQueue(filesystem);
    }

    std::optional<std::vector<tempo::ItemFila>> filaParaEnvio(fs::FS& filesystem) {
        const std::lock_guard lock{gRegistroMutex};
        if (!registro::recuperar(filesystem)) return std::nullopt;
        auto fila = readQueue(filesystem);
        if (!fila) return std::nullopt;
        std::vector<tempo::ItemFila> lote;
        bool mudou = false;
        for (auto& i : *fila) {
            if (!tempo::devePreEnviar(i)) continue;
            if (!i.despachado) mudou = true;
            i.despachado = true;
            lote.push_back(i);
        }
        // Gravar antes de expor o lote ao HTTP. Uma resposta perdida ou um reinício conserva o Desfazer.
        if (mudou && !writeQueue(filesystem, *fila)) return std::nullopt;
        return lote;
    }

    bool applyQueueResults(fs::FS& filesystem, const std::vector<std::pair<tempo::ItemFila, int>>& results) {
        const std::lock_guard lock{gRegistroMutex};
        if (!registro::recuperar(filesystem)) return false;
        auto queue = readQueue(filesystem);
        if (!queue) return false;
        for (const auto& [item, http] : results) tempo::aplicarEnvio(*queue, item, http);
        return writeQueue(filesystem, *queue);
    }

    bool appendLog(fs::FS& filesystem, const tempo::Data& date, const tempo::EventoLog& event) {
        const std::lock_guard lock{gRegistroMutex};
        if (!registro::recuperar(filesystem)) return false;
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
