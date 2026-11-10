#include "tempo/FocoLeitor.h"

#include <algorithm>

#include "tempo/Motor.h"
#include "tempo/Sessao.h"

namespace tempo {

    std::optional<RodadaLeitor> RastreadorLeitor::observar(const EntradaFocoLeitor& e) {
        if (!aberta_) {
            if (e.fase == FaseLeitor::Fora) return std::nullopt;
            aberta_ = true;
            anterior_ = e.fase;
            ultimoMs_ = e.agoraMs;
            focoMs_ = 0;
            pausas_ = 0;
            planejadoMin_ = e.planejadoMin;
            concluidasInicio_ = e.concluidas;
            palavraFim_ = e.palavra;
            palavras_ = 0;
            lendoAnterior_ = e.lendo;
            return std::nullopt;
        }

        const uint32_t delta = e.agoraMs - ultimoMs_;   // aritmetica modular: segura contra overflow de millis()
        if (anterior_ == FaseLeitor::Foco) focoMs_ += delta;
        if (anterior_ == FaseLeitor::Foco && e.fase == FaseLeitor::Pausado) ++pausas_;
        if (lendoAnterior_ && e.lendo && e.palavra > palavraFim_) palavras_ += e.palavra - palavraFim_;
        palavraFim_ = e.palavra;
        lendoAnterior_ = e.lendo;
        ultimoMs_ = e.agoraMs;
        anterior_ = e.fase;

        const bool concluida = e.concluidas != concluidasInicio_;
        if (e.fase != FaseLeitor::Fora && !concluida) return std::nullopt;

        RodadaLeitor r;
        r.planejadoMin = planejadoMin_;
        r.concluida = concluida;
        r.pausas = pausas_;
        const int64_t minutos = static_cast<int64_t>(focoMs_ / 60000U);
        r.focadoMin = concluida ? planejadoMin_ : static_cast<int>(std::min<int64_t>(minutos, planejadoMin_));
        r.palavras = palavras_;
        aberta_ = false;
        return r;
    }

    bool rodadaValida(const RodadaLeitor& rodada) {
        return rodada.planejadoMin > 0 && rodada.palavras >= kMinPalavrasLeitor
               && (rodada.concluida || rodada.focadoMin >= 1);
    }

    bool podeLiquidarLeitor(bool batalhaTempoAtiva) {
        return !batalhaTempoAtiva;
    }

    EntradaBatalha paraEntradaBatalha(const RodadaLeitor& rodada) {
        EntradaBatalha e;
        e.categoria = Categoria::Leitura;
        e.minutosPlanejados = rodada.planejadoMin;
        e.minutosFocados = rodada.focadoMin;
        e.numDistracoes = rodada.pausas > 0 ? 1 : 0;
        e.abandonou = !rodada.concluida;
        e.porPrazer = false;
        return e;
    }

    std::optional<EventoLog> liquidarFocoLeitor(Estado& estado, const RodadaLeitor& rodada, bool batalhaTempoAtiva,
                                                std::string_view tituloLivro, std::string_view dataTexto,
                                                std::string_view horaTexto) {
        if (!podeLiquidarLeitor(batalhaTempoAtiva) || !rodadaValida(rodada)) return std::nullopt;
        const EntradaBatalha entrada = paraEntradaBatalha(rodada);
        const LiquidacaoBatalha liq = aplicarBatalha(estado, entrada);

        EventoLog ev;
        ev.data = std::string{dataTexto};
        ev.hora = std::string{horaTexto};
        ev.tipo = "batalha";
        std::string titulo = limparTitulo(tituloLivro);
        if (titulo.size() > 80) {   // corta em fronteira UTF-8
            size_t corte = 80;
            while (corte > 0 && (static_cast<unsigned char>(titulo[corte]) & 0xC0) == 0x80) --corte;
            titulo.resize(corte);
        }
        ev.projeto = titulo.empty() ? std::string{"Leitura"} : titulo;
        ev.categoria = nomeCategoria(Categoria::Leitura);
        ev.resultado = liq.vitoria ? "vitoria" : "fuga";
        ev.planejadoMin = rodada.planejadoMin;
        ev.focadoMin = rodada.focadoMin;
        ev.overtimeMin = liq.minutosOvertime;
        ev.cr = liq.cr;
        ev.xp = liq.xp;
        ev.perfeita = liq.perfeita;
        ev.origem = kOrigemLeitor;
        ev.palavras = static_cast<int>(std::min<uint32_t>(rodada.palavras, 1000000U));
        return ev;
    }

} // namespace tempo
