#pragma once

// Persistencia do app "Tempo" em JSON (glaze). C++ puro: sem Arduino/FS, testavel no host.
// Arquivos no cartao SD: /tempo/projetos.json (Mac -> placa, so leitura), /tempo/estado.json
// (placa, leitura+gravacao) e /tempo/log/AAAA-MM-DD.jsonl (1 evento por linha).

#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/Tipos.h"

namespace tempo {

    inline constexpr const char* kPastaTempo = "/tempo";
    inline constexpr const char* kPastaLog = "/tempo/log";
    inline constexpr const char* kCaminhoProjetos = "/tempo/projetos.json";
    inline constexpr const char* kCaminhoEstado = "/tempo/estado.json";
    inline constexpr const char* kCaminhoEstadoTmp = "/tempo/estado.json.tmp";
    inline constexpr const char* kCaminhoEstadoBak = "/tempo/estado.json.bak";
    inline constexpr size_t kMaxBytesProjetos = 96 * 1024;
    inline constexpr size_t kMaxBytesEstado = 128 * 1024;
    inline constexpr size_t kMaxProjetos = 64;

    // ----------------------------------------------------------------- projetos
    struct Projeto {
        std::string id;
        std::string titulo;  // bruto (UTF-8, pode ter emoji); use limparTitulo() para exibir
        std::string lista;
        std::string projectId;
        int prioridade = 0;
        std::optional<std::string> vence;
        std::vector<std::string> tags;
    };

    struct ListaProjetos {
        std::string fonte;
        std::string tag;
        std::string gerado_em;
        std::vector<Projeto> projetos;
    };

    bool lerProjetos(std::string_view json, ListaProjetos& saida);

    // Remove glyphs que a fonte da UI nao tem (emoji, simbolos, matematicos, ZWJ/VS16...) e
    // normaliza espacos. Mantem Latin (com acentos), pontuacao geral e simbolos comuns.
    std::string limparTitulo(std::string_view titulo);

    // ------------------------------------------------------------------- estado
    std::string serializarEstado(const Estado& estado);
    bool lerEstado(std::string_view json, Estado& saida);

    // ---------------------------------------------------------------------- log
    struct EventoLog {
        std::string data;        // AAAA-MM-DD ("" = desconhecida)
        std::string hora;        // HH:MM:SS ("" = desconhecida)
        std::string tipo;        // "batalha", ...
        std::string projeto;     // titulo exibido
        std::string projetoId;
        std::string resultado;   // "vitoria" | "fuga"
        int planejadoMin = 0;
        int focadoMin = 0;
        int distraidoMin = 0;
        int distracoes = 0;
        double cr = 0.0;
        double xp = 0.0;
    };

    std::string linhaLog(const EventoLog& evento);   // 1 linha JSON terminada em '\n'
    std::string caminhoLog(const Data& data);         // /tempo/log/AAAA-MM-DD.jsonl
    std::string textoData(const Data& data);          // AAAA-MM-DD

} // namespace tempo
