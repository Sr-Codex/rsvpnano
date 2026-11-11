#pragma once

// App "Habitos" (fase 2): logica PURA da tela (cartao AGORA/PROXIMO, totais do dia, selecao e rolagem da lista).
// Sem Arduino/FS: testavel no host. A tela (src/ui/screens/HabitosScreen.*) so desenha o que estas funcoes decidem.

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/Habitos.h"

namespace tempo {

    inline constexpr size_t kLinhasListaHabitos = 5;   // linhas visiveis da lista (tela deitada)
    inline constexpr int kAdiarMedicacaoMin = 15;      // "Ainda nao": o lembrete da medicacao volta em 15 min

    struct TotaisDia {
        int total = 0;        // todas as ocorrencias do dia
        int feitas = 0;       // registradas (no horario, atrasadas ou tarde demais)
        int puladas = 0;
        int esquecidas = 0;   // fechadas como esquecidas (dia encerrado)
        int pendentes = 0;    // ainda nao resolvidas (futuras, na janela, atrasadas, perdidas)
        double cr = 0.0;      // cr de habitos pagos hoje
    };
    TotaisDia totaisDoDia(const Estado& e);

    enum class ModoCartao : uint8_t {
        SemHabitos,     // nenhum habito configurado hoje
        Proximo,        // na janela, mas antes do horario ("PROXIMO · em N min") ou ainda fora da janela (sem "Feito")
        Agora,          // no horario (ou "qualquer hora")
        Atrasado,       // depois da janela, ainda dentro de +2 h
        Perdido,        // alem de janela + 2 h (ainda pendente ate o fechamento do dia)
        Feito,          // informativo (selecionado na lista)
        Pulado,         // informativo
        Esquecido,      // informativo
        DiaCompleto,    // nada mais a fazer hoje
    };

    struct Cartao {
        ModoCartao modo = ModoCartao::SemHabitos;
        size_t indice = 0;          // ocorrencia mostrada (ignorado em SemHabitos/DiaCompleto)
        FaseOcorrencia fase = FaseOcorrencia::Futura;
        int minutosAte = 0;         // Proximo: minutos ate o horario (> 0)
        int atrasoMin = 0;          // Atrasado/Perdido/Agora apos o horario: minutos desde o horario
        bool podeFeito = false;     // mostra "Feito agora" (ou "Sim, tomei agora")
        bool podePular = false;     // mostra "Pular" (ou "Ainda nao" na medicacao)
        bool confirmar2Toques = false;   // medicacao: Sim, tomei agora / Ainda nao (nunca "Pular")
    };

    // `selecionado` = ocorrencia escolhida na lista (nullopt = a automatica: AGORA/PROXIMO). Indice invalido = automatica.
    Cartao montarCartao(const Estado& e, int minutoAgora, std::optional<size_t> selecionado);

    // Tocar num item seleciona; tocar no ja selecionado volta ao automatico.
    std::optional<size_t> alternarSelecao(std::optional<size_t> atual, size_t tocado);

    // Primeira linha visivel da lista para mostrar `foco` na 3a linha (clampada). n = numero de ocorrencias.
    int rolagemPara(size_t n, size_t foco, size_t visiveis = kLinhasListaHabitos);
    // Rolagem pelo dedo: delta em linhas (+ = para baixo), clampada a [0, n - visiveis].
    int rolar(int atual, int delta, size_t n, size_t visiveis = kLinhasListaHabitos);

    // Minutos ate o proximo horario "HH:MM" > agora entre as ocorrencias pendentes (nullopt = nenhuma). Para o launcher.
    std::optional<int> minutoProximoHabito(const Estado& e, int minutoAgora);

    // ------------------------------------------------------------------ Habitos v2 (cartao com anel do dia, linha do tempo em pe)
    inline constexpr uint32_t kDesfazerMs = 8000;           // quanto tempo o "Desfazer" fica na tela depois de Feito/Pular
    inline constexpr uint32_t kAnimacaoCelebracaoMs = 900;  // anel completa a volta + faiscas
    inline constexpr int kAgrupaMin = 30;                   // em pe: horarios a <= 30 min viram um grupo
    inline constexpr int kAlturaLinhaLista = 56;            // lista do dia / lista do grupo: linha minima (alvo de toque)

    // Icone desenhado do habito (sem emoji). Escolhido pelo tipo e, para "Geral", pelo nome.
    enum class IconeHabito : uint8_t { Haltere, Frasco, Capsula, Talher, Dente, Xicara, Gota, Lotus, Livro, Generico };
    IconeHabito iconeDoHabito(TipoHabito tipo, std::string_view nome);
    // Com o icone escolhido no TickTick (iconRes). Ordem: (a) "habit_*" por palavra; (b) "txt_<emoji>" por emoji comum;
    // (c) tipo + palavras do nome (a funcao de 2 argumentos); (d) Generico. So visual: o TipoHabito nao muda.
    IconeHabito iconeDoHabito(TipoHabito tipo, std::string_view nome, std::string_view iconRes);
    // "#RRGGBB" -> 0xRRGGBB com contraste minimo sobre fundo preto (clareia se escuro); -1 se vazio/invalido.
    int32_t corDoHabito(std::string_view hex);

    // Fracao 0..1 da JANELA do habito (anel interno): Futura/Agora = andamento da janela [horario - antes, horario + janela];
    // Atrasada/Perdida = andamento do atraso (ate janela + 2 h); feita = 1; sem horario = 0.
    double fracaoJanela(const OcorrenciaDia& oc, int minutoAgora);
    // Feitas / (total - puladas), 0 se nada.
    double fracaoDoDia(const TotaisDia& totais);

    // Grupo da linha do tempo em pe: ocorrencias com horario consecutivas a <= kAgrupaMin min uma da outra.
    struct GrupoLinha {
        std::vector<size_t> indices;   // em e.dia.ocorrencias (ordem do dia)
        size_t representante = 0;      // a primeira pendente (ou a primeira, se tudo resolvido)
        int minutoIni = 0, minutoFim = 0;
        bool todasResolvidas = false;  // feitas/puladas/esquecidas
        bool todasFeitas = false;
        bool temFoco = false;          // contem a ocorrencia em foco (cartao automatico)
        bool atrasado = false;         // contem pendente atrasada/perdida
    };
    // So ocorrencias com horario; as sem horario ficam fora (a tela as mostra a parte). `foco` = indice em foco (ou nenhum).
    std::vector<GrupoLinha> agruparLinhaDoTempo(const std::vector<VistaOcorrencia>& vistas, std::optional<size_t> foco);

    // Posicao vertical (px) dos grupos no eixo proporcional: cada grupo vai a `yDoMinuto(minutoIni)`, mas nunca a menos de
    // `minimo` px do anterior; depois um passe de volta garante que o ultimo cabe ate `yMax` (empurra para cima
    // respeitando `minimo`). Devolve um y por grupo.
    std::vector<int> posicoesNoEixo(const std::vector<int>& yIdeal, int minimo, int yMax);

    // Primeiro habito de amanha (nome limpo + horario) para o "amanha: X HH:MM" do dia completo.
    struct HabitoAmanha {
        std::string nome;
        TipoHabito tipo = TipoHabito::Geral;
        int minuto = 0;
    };
    std::optional<HabitoAmanha> primeiroHabitoDeAmanha(const ListaHabitos& lista, const Data& hoje);

    // Animacao da celebracao: ms desde a marcacao -> anel (0..1 da volta interna), disco do visto (0..1), faiscas (0..1).
    struct AnimacaoCelebracao {
        double anel = 0.0, disco = 0.0, faiscas = 0.0;
    };
    AnimacaoCelebracao animacaoCelebracao(uint32_t msDesdeMarcar);
    inline bool desfazerDisponivel(uint32_t agoraMs, uint32_t marcouMs) { return agoraMs - marcouMs < kDesfazerMs; }

    // Rolagem em pixels (lista do dia / lista do grupo): clamp [0, n*altura - visivel].
    int rolarPx(int atualPx, int deltaPx, size_t n, int alturaLinha, int alturaVisivel);
    // Linha tocada (indice) na lista rolada; nullopt se fora. `yRelativo` = y do toque menos o topo da lista.
    std::optional<size_t> linhaNoToque(int rolagemPx, int yRelativo, size_t n, int alturaLinha);
    // Rolagem (px) que deixa a linha `foco` centrada na area visivel (clampada).
    int rolagemPxPara(size_t n, size_t foco, int alturaLinha, int alturaVisivel);

} // namespace tempo
