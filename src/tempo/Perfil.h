#pragma once

// Perfil do app "Tempo" (lume.5): resumo acumulado dos logs, agregados da semana, horario mais focado e
// conquistas. C++ puro (sem Arduino/FS): testavel no host. O resumo vive em /tempo/resumo.json e e atualizado a
// cada batalha liquidada; se faltar, e reconstruido a partir dos logs /tempo/log/*.jsonl.

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/Sessao.h"
#include "tempo/Tipos.h"

namespace tempo {

    inline constexpr const char* kCaminhoResumo = "/tempo/resumo.json";
    inline constexpr const char* kCaminhoResumoTmp = "/tempo/resumo.json.tmp";
    inline constexpr const char* kCaminhoResumoBak = "/tempo/resumo.json.bak";
    inline constexpr size_t kMaxBytesResumo = 128 * 1024;
    inline constexpr size_t kMaxProjetosResumo = 40;
    inline constexpr size_t kMaxDiasResumo = 400;
    inline constexpr int kMetaSemanalPadraoMin = 15 * 60;
    inline constexpr int kMadrugadaMin = 7 * 60;   // foco iniciado antes das 07:00

    struct ProjetoResumo {
        std::string nome;
        int minutos = 0;
        int vitorias = 0;
    };

    struct DiaResumo {
        int64_t dia = 0;          // diaSerial
        int focoMin = 0;
        int meditacaoMin = 0;
        int batalhas = 0;
        int perfeitas = 0;
        bool madrugada = false;   // algum foco iniciado antes das 07:00
    };

    struct Resumo {
        int versao = 1;
        int vitorias = 0;
        int batalhas = 0;         // batalhas com inimigo (vitoria ou fuga); a sessao livre NAO entra aqui
        int sessoesLivres = 0;    // sessoes livres (resultado "livre"): tempo e cr/XP contam, mas nao sao vitoria nem derrota
        int focoTotalMin = 0;
        int focoLivreMin = 0;
        int maxStreak = 0;
        std::array<int, 24> minutosPorHora{};   // minutos de foco por hora do relogio
        std::vector<ProjetoResumo> projetos;     // so projetos (sem Foco livre)
        std::vector<DiaResumo> dias;             // ordenado por dia, no maximo kMaxDiasResumo
    };

    bool lerDataTexto(std::string_view texto, Data& saida);   // "AAAA-MM-DD"
    int minutoDeHora(std::string_view hora);                  // "HH:MM[:SS]" -> 0..1439; -1 = invalida

    // Soma uma batalha ao resumo (ignora eventos que nao sao "batalha"). streak = streak atual (maxStreak).
    void registrarEvento(Resumo& resumo, const EventoLog& evento, int streak = 0);
    Resumo reconstruirResumo(const std::vector<EventoLog>& eventos);   // eventos em ordem cronologica
    std::string serializarResumo(const Resumo& resumo);
    bool lerResumo(std::string_view json, Resumo& saida);

    // Indice do chip (0 Trabalho, 1 Estudo, 2 Leitura, 3 Meditacao, 4 Projetos) do evento.
    int chipDoEvento(const EventoLog& evento);

    // ---------------------------------------------------------------- semana
    struct Semana {
        std::array<std::array<int, kNumChips>, 7> min{};   // [dia: seg..dom][chip]
        std::array<int, kNumChips> porCategoria{};
        int total = 0;
        int perdidoMin = 0;                                  // distracoes > 2 min viram tempo perdido
        int maiorDia = 0;
    };
    // segunda = serial da segunda-feira da semana; ignora eventos de outras datas.
    void somarSemana(Semana& semana, const EventoLog& evento, int64_t segunda);

    // Faixa de 2 h consecutivas com mais foco: devolve a hora inicial (0..22) ou -1 sem dados.
    int faixaMaisFocada(const Resumo& resumo);
    int mediaFocoPorDia(const Resumo& resumo);   // minutos por dia com foco

    const char* tituloDoNivel(int nivel);

    // ------------------------------------------------------------- conquistas
    inline constexpr size_t kNumConquistas = 8;
    struct Conquista {
        const char* nome;
        const char* desc;      // como ganhar
        const char* unidade;   // "vitórias", "min", "dias"...
        bool ganha = false;
        int atual = 0;         // ja limitado a meta
        int meta = 1;
        uint8_t cor = 0;       // 0 laranja, 1 ambar, 2 carmim, 3 roxo, 4 agua, 5 verde
    };
    std::array<Conquista, kNumConquistas> avaliarConquistas(const Resumo& resumo, int streakAtual);
    int conquistasGanhas(const std::array<Conquista, kNumConquistas>& lista);
    // Indice da proxima conquista a ganhar (a nao ganha com maior progresso relativo); -1 se todas ganhas.
    int proximaConquista(const std::array<Conquista, kNumConquistas>& lista);

} // namespace tempo
