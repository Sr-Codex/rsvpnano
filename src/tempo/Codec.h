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
        std::string categoria;   // Trabalho/Estudo/Leitura/Meditação/Projetos ("Foco livre" = projeto vazio de id)
        std::string resultado;   // "vitoria" | "fuga"
        int planejadoMin = 0;
        int focadoMin = 0;
        int distraidoMin = 0;
        int distracoes = 0;
        int overtimeMin = 0;     // minutos de bonus (apos derrotar o inimigo)
        int humor = 0;           // reflexao: 0 = sem resposta, 1..5 = Pessimo..Otimo (nao rende cr/XP)
        int distraiu = 0;        // reflexao: 0 = sem resposta, 1 = Nao, 2 = Um pouco, 3 = Muito
        double cr = 0.0;
        double xp = 0.0;
        bool perfeita = false;   // vitoria sem nenhuma distracao (lume.5; logs antigos nao tem)
        std::string origem;      // "" = batalha do Tempo; "leitor" = foco do leitor (logs antigos nao tem)
        int palavras = 0;        // origem "leitor": palavras avancadas no periodo (logs antigos nao tem)
    };

    std::string linhaLog(const EventoLog& evento);   // 1 linha JSON terminada em '\n'
    bool lerLinhaLog(std::string_view linha, EventoLog& saida);   // false = linha invalida (saida intacta)
    std::string caminhoLog(const Data& data);         // /tempo/log/AAAA-MM-DD.jsonl
    std::string textoData(const Data& data);          // AAAA-MM-DD

} // namespace tempo
