#pragma once

// App "Habitos" (fase 2): logica PURA da tela (cartao AGORA/PROXIMO, totais do dia, selecao e rolagem da lista).
// Sem Arduino/FS: testavel no host. A tela (src/ui/screens/HabitosScreen.*) so desenha o que estas funcoes decidem.

#include <cstddef>
#include <optional>
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

} // namespace tempo
