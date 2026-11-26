#include "storage/tempo/RegistroHabito.h"

#include <algorithm>
#include <cstring>
#include <array>
#include <glaze/json.hpp>

#include "hash/Fnv1a.h"
#include "storage/fs/StorageFiles.h"
#include "tempo/Habitos.h"

namespace TempoStorage::registro {
    struct Plano {
        int dia = 0;
        size_t posicao = 0;
        std::string eventos;
        std::string estado;
        std::optional<std::string> fila;
        uint32_t integridade = 0;
    };

    namespace {
        constexpr char marcador[] = "/tempo/registro-habito.json";
        constexpr size_t limiteLog = 256 * 1024;
        constexpr size_t limitePlano = 2 * (tempo::kMaxBytesEstado + tempo::kMaxBytesFila + limiteLog);

        uint32_t assinatura(const Plano& p) {
            uint32_t hash = Fnv1a::hash(std::to_string(p.dia) + "/" + std::to_string(p.posicao));
            hash = Fnv1a::append(hash, p.eventos);
            hash = Fnv1a::append(hash, p.estado);
            return p.fila ? Fnv1a::append(Fnv1a::append(hash, "/fila/"), *p.fila) : hash;
        }

        bool lerPlano(fs::FS& fs, Plano& p) {
            const auto dados = StorageFiles::readTextFile(fs, marcador, limitePlano);
            return dados && !glz::read_json(p, *dados) && p.dia >= 20250101 && !p.eventos.empty() &&
                   !p.estado.empty() && p.estado.size() <= tempo::kMaxBytesEstado &&
                   (!p.fila || p.fila->size() <= tempo::kMaxBytesFila) && p.posicao <= limiteLog &&
                   p.eventos.size() <= limiteLog - p.posicao && p.integridade == assinatura(p);
        }

        // O marcador contém os bytes definitivos dos dois arquivos, inclusive da fila vazia.
        // Uma queda entre os renames pode ser recuperada mesmo se só restou o destino antigo.
        bool trechoIgual(File& f, std::string_view esperado) {
            std::array<uint8_t, 512> buffer;
            while (!esperado.empty()) {
                const size_t n = std::min(buffer.size(), esperado.size());
                if (f.read(buffer.data(), n) != n || std::memcmp(buffer.data(), esperado.data(), n)) return false;
                esperado.remove_prefix(n);
            }
            return true;
        }

        bool arquivoIgual(fs::FS& fs, const char* path, std::string_view esperado) {
            File f = fs.open(path, FILE_READ);
            return f && !f.isDirectory() && f.size() == esperado.size() && trechoIgual(f, esperado);
        }

        bool publicar(fs::FS& fs, const char* final, const char* tmp, const char* bak, std::string_view dados) {
            if (arquivoIgual(fs, final, dados)) return true;
            return StorageFiles::writeFileAtomic(fs, final, tmp, bak, dados) && arquivoIgual(fs, final, dados);
        }

        // Nenhum outro escritor pode anexar enquanto houver marcador. Se faltar energia durante um
        // append, conferir o prefixo na posição original e completar somente os bytes faltantes.
        bool completarLog(fs::FS& fs, const Plano& plano) {
            const auto path = tempo::caminhoLog(tempo::dataDeStamp(plano.dia));
            size_t jaGravado = 0;
            if (fs.exists(path.c_str())) {
                File f = fs.open(path.c_str(), FILE_READ);
                if (!f || f.isDirectory() || f.size() < plano.posicao) return false;
                jaGravado = f.size() - plano.posicao;
                if (jaGravado > plano.eventos.size() || (jaGravado && !f.seek(plano.posicao))) return false;
                if (!trechoIgual(f, std::string_view(plano.eventos).substr(0, jaGravado))) return false;
            } else if (plano.posicao) return false;
            if (jaGravado == plano.eventos.size()) return true;
            File f = fs.open(path.c_str(), FILE_APPEND);
            if (!f || f.isDirectory()) return false;
            const auto falta = std::string_view(plano.eventos).substr(jaGravado);
            const size_t escrito = f.write(reinterpret_cast<const uint8_t*>(falta.data()), falta.size());
            f.flush();
            f.close();
            if (escrito != falta.size()) return false;
            File leitura = fs.open(path.c_str(), FILE_READ);
            return leitura && !leitura.isDirectory() && leitura.size() == plano.posicao + plano.eventos.size() &&
                   leitura.seek(plano.posicao) && trechoIgual(leitura, plano.eventos);
        }

        bool concluir(fs::FS& fs, const Plano& p) {
            if (!publicar(fs, tempo::kCaminhoEstado, tempo::kCaminhoEstadoTmp, tempo::kCaminhoEstadoBak, p.estado)) return false;
            if (!completarLog(fs, p)) return false;
            if (p.fila && !publicar(fs, tempo::kCaminhoFilaHabitos, tempo::kCaminhoFilaHabitosTmp,
                                       tempo::kCaminhoFilaHabitosBak, *p.fila)) return false;
            return fs.remove(marcador);
        }
    }

    bool pendente(fs::FS& fs) { return fs.exists(marcador); }

    bool recuperar(fs::FS& fs) {
        if (!pendente(fs)) return true;
        Plano p;
        return lerPlano(fs, p) && concluir(fs, p);
    }

    bool diaPendente(fs::FS& fs, const tempo::Data& dia) {
        if (!pendente(fs)) return false;
        Plano p;
        return lerPlano(fs, p) && p.dia == tempo::stampDe(dia);
    }

    bool lerEstadoPendente(fs::FS& fs, tempo::Estado& estado) {
        Plano p;
        return lerPlano(fs, p) && tempo::lerEstado(p.estado, estado);
    }

    GravacaoHabito gravar(fs::FS& fs, const tempo::Estado& estado, std::string_view eventos,
                          std::optional<std::string_view> fila) {
        Plano p;
        p.dia = tempo::stampDe(estado.dia.data);
        p.eventos = eventos;
        p.estado = tempo::serializarEstado(estado);
        if (fila) p.fila.emplace(*fila);
        const auto path = tempo::caminhoLog(estado.dia.data);
        if (fs.exists(path.c_str())) {
            File f = fs.open(path.c_str(), FILE_READ);
            if (!f || f.isDirectory()) return GravacaoHabito::Recusada;
            p.posicao = f.size();
            if (p.posicao) {
                if (p.posicao > limiteLog || !f.seek(p.posicao - 1)) return GravacaoHabito::Recusada;
                const int ultimo = f.read();
                if (ultimo < 0) return GravacaoHabito::Recusada;
                if (ultimo != '\n') p.eventos.insert(0, 1, '\n');
            }
        }
        p.integridade = assinatura(p);
        std::string json;
        if (p.estado.empty() || p.estado.size() > tempo::kMaxBytesEstado ||
            (fila && fila->size() > tempo::kMaxBytesFila) || eventos.empty() || p.eventos.size() > limiteLog - p.posicao ||
            glz::write_json(p, json) || json.size() > limitePlano) return GravacaoHabito::Recusada;
        if (!StorageFiles::writeFileAtomic(fs, marcador, "/tempo/registro-habito.tmp", "/tempo/registro-habito.bak", json))
            return GravacaoHabito::Recusada;
        if (!arquivoIgual(fs, marcador, json)) return GravacaoHabito::Pendente;
        return concluir(fs, p) ? GravacaoHabito::Completa : GravacaoHabito::Pendente;
    }
}
