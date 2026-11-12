#include "tempo/Motor.h"

#include <algorithm>
#include <cmath>

#include "tempo/Parametros.h"
#include "tempo/Progressao.h"

namespace tempo {

    namespace {

        constexpr double kEps = 1e-9;

        bool congelado(const Estado& e) {
            return e.ferias;
        }

        int64_t agoraAbs(const Estado& e) {
            return diaSerial(e.dia.data) * 1440 + e.dia.minutoAtual;
        }

        void logEvento(Estado& e, TipoEvento tipo, double cr, double xp, int meios, int detalhe) {
            Evento ev;
            ev.tipo = tipo;
            ev.minutoDoDia = e.dia.minutoAtual;
            ev.cr = cr;
            ev.xp = xp;
            ev.coracoesMeios = meios;
            ev.detalhe = detalhe;
            e.dia.eventos.push_back(ev);
        }

        void addXp(Estado& e, double xp, std::optional<Atributo> atributo) {
            if (xp <= 0.0) {
                return;
            }
            e.xpTotal += xp;
            e.dia.xpGanho += xp;
            if (atributo) {
                e.pontosAtributo[static_cast<size_t>(*atributo)] += xp;
            }
        }

        bool ehTrabalhoOuProjetos(Categoria c) {
            return c == Categoria::Trabalho || c == Categoria::Projetos;
        }

        void resetSemanaSeNecessario(Estado& e) {
            const int64_t segunda = segundaDaSemana(e.dia.data);
            if (segunda != e.semanaRef) {
                e.semanaRef = segunda;
                e.folgasUsadasSemana = 0;
                e.desafiosNaSemana = 0;
            }
        }

        void resetDia(Estado& e, const Data& data, int descansoOrcamentoMin) {
            Dia novo;
            novo.data = data;
            novo.aberto = true;
            novo.descansoOrcamentoMin = descansoOrcamentoMin;
            for (size_t i = 0; i < kNumAtributos; ++i) {
                novo.nivelAtributoInicio[i] = nivelAtributo(static_cast<Atributo>(i), e.pontosAtributo[i]);
            }
            e.dia = std::move(novo);
            resetSemanaSeNecessario(e);
        }

        void dispararDesafio(Estado& e) {
            resetSemanaSeNecessario(e);
            Desafio d;
            d.nivel = std::min(e.desafiosNaSemana + 1, param::kNivelMaxDesafio);
            d.criadoEmMin = agoraAbs(e);
            d.prazoEmMin = d.criadoEmMin + param::kPrazoDesafioMin;
            ++e.desafiosNaSemana;
            e.desafiosPendentes.push_back(d);
            e.dia.desafioHoje = true;
            const int aplicado = aplicarCoracoes(e, -2);
            logEvento(e, TipoEvento::DesafioDisparado, 0.0, 0.0, aplicado, d.nivel);
        }

        void atualizarDesafiosVencidos(Estado& e) {
            if (congelado(e) || !penalidadesAtivas()) {
                return;
            }
            const int64_t agora = agoraAbs(e);
            for (Desafio& d : e.desafiosPendentes) {
                if (agora <= d.prazoEmMin) {
                    continue;
                }
                const int diasAtraso = static_cast<int>((agora - d.prazoEmMin) / 1440) + 1;
                const int novos = diasAtraso - d.diasPenalizados;
                if (novos > 0) {
                    d.diasPenalizados = diasAtraso;
                    const int aplicado = aplicarCoracoes(e, -2 * novos);
                    logEvento(e, TipoEvento::DesafioVencido, 0.0, 0.0, aplicado, d.nivel);
                }
            }
        }

        // Distribui o excesso de uma transicao na classe; devolve o que sobrou sem classe.
        int aplicarClasse(Estado& e, int excesso, ClasseIntervalo classe, ResultadoTransicao& r) {
            switch (classe) {
                case ClasseIntervalo::Descanso: {
                    const int restante = std::max(0, e.dia.descansoOrcamentoMin - e.dia.descansoUsadoMin);
                    const int usa = std::min(excesso, restante);
                    e.dia.descansoUsadoMin += usa;
                    r.descansoMin += usa;
                    return excesso - usa;
                }
                case ClasseIntervalo::Ok: {
                    const int restante = std::max(0, param::kOkOrcamentoMin - e.dia.okUsadoMin);
                    const int usa = std::min(excesso, restante);
                    e.dia.okUsadoMin += usa;
                    r.okMin += usa;
                    return excesso - usa;
                }
                case ClasseIntervalo::Perdido: {
                    r.custoCr += registrarPerdido(e, excesso);
                    r.perdidoMin += excesso;
                    return 0;
                }
            }
            return excesso;
        }

    } // namespace

    namespace {
        bool gPenalidades = param::kPenalidadesAtivas;
    }
    bool penalidadesAtivas() { return gPenalidades; }
    void definirPenalidadesParaTeste(bool ativas) { gPenalidades = ativas; }

    // ------------------------------------------------------------------ carteira
    int nivelDivida(double saldo) {
        if (saldo >= 0.0) {
            return 0;
        }
        return static_cast<int>(std::floor(-saldo / param::kPatamarDivida + kEps));
    }

    void creditar(Estado& e, double cr) {
        if (cr <= 0.0 || congelado(e)) {
            return;
        }
        e.saldo += cr;
        e.dia.crGanho += cr;
        if (e.saldo > param::kTetoCarteira) {
            e.cofre += e.saldo - param::kTetoCarteira;
            e.saldo = param::kTetoCarteira;
        }
        e.patamarDisparado = std::min(e.patamarDisparado, nivelDivida(e.saldo));
        logEvento(e, TipoEvento::Credito, cr, 0.0, 0, 0);
    }

    void debitar(Estado& e, double cr) {
        if (cr <= 0.0 || congelado(e)) {
            return;
        }
        e.saldo -= cr;
        e.dia.crGasto += cr;
        avaliarGatilhos(e);
    }

    // ----------------------------------------------------------------- corações
    int aplicarCoracoes(Estado& e, int deltaMeios) {
        if (deltaMeios == 0 || congelado(e)) {
            return 0;
        }
        int aplicado = 0;
        if (deltaMeios < 0) {
            if (e.dia.recuperacao || !penalidadesAtivas()) {
                return 0;   // penalidades desligadas ate existir o Recibo (Parametros.h)
            }
            const int novo = std::max(0, e.coracoesMeios + deltaMeios);
            aplicado = novo - e.coracoesMeios;
            e.coracoesMeios = novo;
            if (novo == 0 && aplicado < 0) {
                e.heroiCaido = true;
                e.resgateDiasValidos = 0;
            }
        } else {
            if (e.heroiCaido) {
                return 0;  // so a missao de resgate devolve coracoes
            }
            const int folgaDia = param::kGanhoMaxDiaMeios - e.dia.ganhoCoracoesMeios;
            const int folgaMax = coracoesMaxMeios(e) - e.coracoesMeios;
            aplicado = std::max(0, std::min({deltaMeios, folgaDia, folgaMax}));
            e.coracoesMeios += aplicado;
            e.dia.ganhoCoracoesMeios += aplicado;
        }
        e.dia.coracoesDeltaMeios += aplicado;
        return aplicado;
    }

    // ------------------------------------------------------------------- desafios
    bool avaliarGatilhos(Estado& e) {
        if (congelado(e) || !e.dia.aberto) {
            return false;
        }
        const int k = nivelDivida(e.saldo);
        if (k < e.patamarDisparado) {
            e.patamarDisparado = k;  // dívida diminuiu: rearma
        }
        if (!penalidadesAtivas()) {
            // Sem punicao: acompanha o patamar em silencio (divida feita agora nao dispara desafio retroativo ao religar).
            e.patamarDisparado = std::max(e.patamarDisparado, k);
            e.gatilhoFechamentoPendente = false;
            return false;
        }
        if (e.dia.desafioHoje || e.dia.recuperacao) {
            return false;
        }
        if (k > e.patamarDisparado) {
            e.patamarDisparado = k;
            dispararDesafio(e);
            return true;
        }
        if (e.gatilhoFechamentoPendente) {
            e.gatilhoFechamentoPendente = false;
            dispararDesafio(e);
            return true;
        }
        return false;
    }

    bool cumprirDesafio(Estado& e, size_t indice) {
        if (indice >= e.desafiosPendentes.size()) {
            return false;
        }
        const int nivel = e.desafiosPendentes[indice].nivel;
        e.desafiosPendentes.erase(e.desafiosPendentes.begin() + static_cast<std::ptrdiff_t>(indice));
        const int aplicado = aplicarCoracoes(e, 1);
        logEvento(e, TipoEvento::DesafioCumprido, 0.0, 0.0, aplicado, nivel);
        return true;  // NAO apaga a divida
    }

    bool lojaTravada(const Estado& e) {
        if (!penalidadesAtivas()) return false;   // sem punicao nao ha o que travar (desafio/heroi caido ficam guardados)
        return !e.desafiosPendentes.empty() || e.heroiCaido;
    }

    // ----------------------------------------------------------------------- batalha
    LiquidacaoBatalha liquidarBatalha(const Estado& e, const EntradaBatalha& in) {
        LiquidacaoBatalha r;
        const TaxaCategoria& taxa = param::kTaxas[static_cast<size_t>(in.categoria)];
        r.atributo = taxa.atributo;

        const int plan = std::max(0, in.minutosPlanejados);
        const int focados = std::max(0, in.minutosFocados);
        const int distraidos = std::max(0, in.minutosDistraidos);

        if (in.livre) {   // modo livre (decisao 23): so taxa x minutos focados; nada de vitoria, bonus perfeito ou fuga
            r.minutosPagos = focados;
            r.crBruto = focados * taxa.crPorMin;
            double crBase = r.crBruto, xpBase = focados * taxa.xpPorMin;
            if (in.categoria == Categoria::Meditacao) {
                const int cheios = std::clamp(param::kMeditacaoTetoMin - e.dia.meditacaoMin, 0, focados);
                crBase = cheios * taxa.crPorMin + (focados - cheios) * param::kMeditacaoCrExcesso;
                xpBase = cheios * taxa.xpPorMin + (focados - cheios) * param::kMeditacaoXpExcesso;
                r.crBruto = crBase;
            }
            double bonusCr = 0.0, bonusXp = 0.0;   // sem perfeito; bonus de atributo/equipamento valem como sempre
            const size_t ci = static_cast<size_t>(in.categoria);
            if (ehTrabalhoOuProjetos(in.categoria)) {
                bonusCr += std::min(e.dia.nivelAtributoInicio[static_cast<size_t>(Atributo::Oficio)] * param::kOficioPorNivel,
                                    param::kTetoAtributoBonus) + e.equip.refinoCrExtra;
            }
            if (taxa.atributo == Atributo::Mente) {
                bonusXp += std::min(e.dia.nivelAtributoInicio[static_cast<size_t>(Atributo::Mente)] * param::kMentePorNivel,
                                    param::kTetoAtributoBonus);
            }
            bonusCr += e.equip.crExtra[ci];
            bonusXp += e.equip.xpExtra[ci];
            r.bonusCrPct = std::min(bonusCr, param::kTetoBonusCr);
            r.bonusXpPct = std::min(bonusXp, param::kTetoBonusXp);
            r.crBonus = crBase * r.bonusCrPct;
            double fatorCr = 1.0, fatorXp = 1.0;
            if (focados > 0) {
                const int antes = e.dia.fochoPagoMin;
                const int abaixoCr = std::clamp(param::kTetoFocoCrMin - antes, 0, focados);
                const int abaixoXp = std::clamp(param::kTetoFocoXpMin - antes, 0, focados);
                fatorCr = (abaixoCr + param::kFatorAposTeto * (focados - abaixoCr)) / focados;
                fatorXp = (abaixoXp + param::kFatorAposTeto * (focados - abaixoXp)) / focados;
                r.tetoCrAplicado = abaixoCr < focados;
                r.tetoXpAplicado = abaixoXp < focados;
            }
            r.cr = (crBase + r.crBonus) * fatorCr;
            r.xp = xpBase * (1.0 + r.bonusXpPct) * fatorXp;
            return r;
        }

        r.curaMin = std::min(distraidos, plan);
        bool fuga = in.abandonou || in.numDistracoes > param::kMaxDistracoes ||
                    in.maiorDistracaoMin > param::kDistracaoLongaMin;
        const bool hpZero = focados >= plan + r.curaMin;
        if (!hpZero) {
            fuga = true;  // terminou sem zerar o HP = abandono
        }
        r.fuga = fuga;
        r.vitoria = !fuga;
        if (distraidos > param::kDistracaoPerdidoMin) {
            r.perdidoMin = distraidos;
        }

        if (r.vitoria) {
            r.minutosOvertime = std::max(0, focados - (plan + r.curaMin));   // hora extra sem teto (decisao 24)
            r.minutosPagos = plan + r.minutosOvertime;
        } else {
            r.minutosPagos = focados;
        }
        // Decisao 18: 1 distracao de ate 1 min NAO perde o bonus (so quando o chamador informa os segundos).
        const bool semDistracao = in.numDistracoes == 0 && distraidos == 0;
        const bool distracaoCurta = in.segundosDistraidos >= 0 && in.numDistracoes == 1 &&
                                    in.segundosDistraidos <= param::kDistracaoToleradaSeg;
        r.perfeita = r.vitoria && (semDistracao || distracaoCurta) && plan >= param::kDuracaoMinBonusPerfeita;

        // Minutos base: meditacao tem teto diario de 30 min com taxa cheia.
        const int pagos = r.minutosPagos;
        double crBase = 0.0;
        double xpBase = 0.0;
        if (in.categoria == Categoria::Meditacao) {
            const int cheios = std::clamp(param::kMeditacaoTetoMin - e.dia.meditacaoMin, 0, pagos);
            const int resto = pagos - cheios;
            crBase = cheios * taxa.crPorMin + resto * param::kMeditacaoCrExcesso;
            xpBase = cheios * taxa.xpPorMin + resto * param::kMeditacaoXpExcesso;
        } else {
            crBase = pagos * taxa.crPorMin;
            xpBase = pagos * taxa.xpPorMin;
        }
        if (in.porPrazer) {
            crBase = 0.0;
        }
        r.crBruto = crBase;

        // Bonus aditivos com teto (sem bonus na fuga).
        if (!r.fuga) {
            double bonusCr = r.perfeita ? param::kBonusPerfeita : 0.0;
            double bonusXp = bonusCr;
            const size_t ci = static_cast<size_t>(in.categoria);
            if (ehTrabalhoOuProjetos(in.categoria)) {
                const double oficio = std::min(
                    e.dia.nivelAtributoInicio[static_cast<size_t>(Atributo::Oficio)] * param::kOficioPorNivel, param::kTetoAtributoBonus);
                bonusCr += oficio + e.equip.refinoCrExtra;
            }
            if (taxa.atributo == Atributo::Mente) {
                bonusXp += std::min(
                    e.dia.nivelAtributoInicio[static_cast<size_t>(Atributo::Mente)] * param::kMentePorNivel, param::kTetoAtributoBonus);
            }
            bonusCr += e.equip.crExtra[ci];
            bonusXp += e.equip.xpExtra[ci];
            r.bonusCrPct = std::min(bonusCr, param::kTetoBonusCr);
            r.bonusXpPct = std::min(bonusXp, param::kTetoBonusXp);
        }
        // Decisao 24: o bonus perfeito (+25 %) vale so para a parte PLANEJADA; os minutos de hora extra ficam sem ele.
        double pctCr = r.bonusCrPct, pctXp = r.bonusXpPct;
        if (r.perfeita && pagos > 0 && r.minutosOvertime > 0) {
            const double fracExtra = double(r.minutosOvertime) / pagos;
            pctCr -= std::min(param::kBonusPerfeita, r.bonusCrPct) * fracExtra;
            pctXp -= std::min(param::kBonusPerfeita, r.bonusXpPct) * fracExtra;
        }
        r.crBonus = crBase * pctCr;

        // Tetos diarios (6 h -> cr x0,5; 8 h -> XP x0,5), proporcional aos minutos acima do teto.
        double fatorCr = 1.0;
        double fatorXp = 1.0;
        if (pagos > 0) {
            const int antes = e.dia.fochoPagoMin;
            const int abaixoCr = std::clamp(param::kTetoFocoCrMin - antes, 0, pagos);
            const int abaixoXp = std::clamp(param::kTetoFocoXpMin - antes, 0, pagos);
            fatorCr = (abaixoCr + param::kFatorAposTeto * (pagos - abaixoCr)) / pagos;
            fatorXp = (abaixoXp + param::kFatorAposTeto * (pagos - abaixoXp)) / pagos;
            r.tetoCrAplicado = abaixoCr < pagos;
            r.tetoXpAplicado = abaixoXp < pagos;
        }
        r.cr = (crBase + r.crBonus) * fatorCr;
        r.xp = xpBase * (1.0 + pctXp) * fatorXp;
        return r;
    }

    LiquidacaoBatalha aplicarBatalha(Estado& e, const EntradaBatalha& in) {
        LiquidacaoBatalha r = liquidarBatalha(e, in);
        e.dia.fochoPagoMin += r.minutosPagos;
        if (r.vitoria || (in.livre && r.minutosPagos >= param::kLivreDiaValidoMin)) {
            e.dia.fochoValidoMin += r.minutosPagos;   // livre: nao e vitoria, mas >= 15 min conta para o dia valido
        }
        if (in.categoria == Categoria::Meditacao) {
            e.dia.meditacaoMin += r.minutosPagos;
        }
        creditar(e, r.cr);
        addXp(e, r.xp, r.atributo);
        logEvento(e, TipoEvento::Batalha, r.cr, r.xp, 0, static_cast<int>(in.categoria));
        if (r.perdidoMin > 0) {
            registrarPerdido(e, r.perdidoMin);
        }
        return r;
    }

    // ---------------------------------------------------------------------- habitos
    StatusHabito classificarHabito(Habito habito, int minutoPlanejado, int minutoReal) {
        const int janela = param::kHabitos[static_cast<size_t>(habito)].janelaMin;
        const int delta = minutoReal - minutoPlanejado;
        const int distancia = habito == Habito::Descanso ? delta : std::abs(delta);
        if (distancia <= janela) {
            return StatusHabito::NoHorario;
        }
        if (distancia <= janela + param::kJanelaAtrasoMin) {
            return StatusHabito::Atrasado;
        }
        return StatusHabito::Esquecido;
    }

    ResultadoHabito registrarHabito(Estado& e, Habito habito, int minutoPlanejado, int minutoReal) {
        ResultadoHabito r;
        StatusHabito& slot = e.dia.habitos[static_cast<size_t>(habito)];
        if (slot != StatusHabito::Pendente) {
            r.status = slot;
            return r;
        }
        const ValorHabito& v = param::kHabitos[static_cast<size_t>(habito)];
        r.registrado = true;
        r.status = classificarHabito(habito, minutoPlanejado, minutoReal);
        slot = r.status;
        switch (r.status) {
            case StatusHabito::NoHorario:
                r.cr = v.cr;
                r.xp = v.xp;
                break;
            case StatusHabito::Atrasado:
                r.xp = v.xp * param::kXpAtrasadoFator;
                if (habito == Habito::Medicacao) {
                    r.coracoesMeios = -1;
                }
                break;
            default:
                if (habito == Habito::Medicacao) {
                    r.coracoesMeios = -2;
                }
                break;
        }
        if (!penalidadesAtivas() && r.coracoesMeios < 0) r.coracoesMeios = 0;   // sem punicao (Parametros.h)
        e.dia.habitosCr += r.cr;
        creditar(e, r.cr);
        addXp(e, r.xp, v.atributo);
        const int aplicado = aplicarCoracoes(e, r.coracoesMeios);
        logEvento(e, TipoEvento::Habito, r.cr, r.xp, aplicado, static_cast<int>(habito));
        return r;
    }

    StatusHabito classificarOcorrencia(TipoHabito tipo, int minutoPlanejado, int minutoReal) {
        if (minutoPlanejado < 0) {
            return StatusHabito::NoHorario;
        }
        const int janela = param::kValoresTipoHabito[static_cast<size_t>(tipo)].janelaMin;
        const int delta = minutoReal - minutoPlanejado;
        const int distancia = tipo == TipoHabito::Descanso ? delta : std::abs(delta);
        if (distancia <= janela) {
            return StatusHabito::NoHorario;
        }
        if (distancia <= janela + param::kJanelaAtrasoMin) {
            return StatusHabito::Atrasado;
        }
        return StatusHabito::Esquecido;
    }

    ResultadoOcorrencia registrarOcorrencia(Estado& e, size_t indice, int minutoReal, bool remota) {
        ResultadoOcorrencia r;
        r.indice = indice;
        if (indice >= e.dia.ocorrencias.size()) {
            return r;
        }
        OcorrenciaDia& oc = e.dia.ocorrencias[indice];
        if (oc.feita) {
            r.status = oc.status;
            return r;
        }
        const ValorHabito& v = param::kValoresTipoHabito[static_cast<size_t>(oc.tipo)];
        r.registrado = true;
        r.status = classificarOcorrencia(oc.tipo, oc.minuto, minutoReal);
        oc.status = r.status;
        oc.feita = true;
        oc.minutoReal = minutoReal;
        oc.remota = remota;
        const bool medicacao = oc.tipo == TipoHabito::Medicacao;
        switch (r.status) {
            case StatusHabito::NoHorario: {
                const double restante = std::max(0.0, param::kTetoHabitosCrDia - e.dia.habitosCr);
                r.cr = std::min(v.cr, restante);
                r.tetoAplicado = r.cr + kEps < v.cr;
                r.xp = v.xp;
                break;
            }
            case StatusHabito::Atrasado:
                r.xp = v.xp * param::kXpAtrasadoFator;
                if (medicacao) {
                    r.coracoesMeios = -1;
                }
                break;
            default:
                if (medicacao) {
                    r.coracoesMeios = -2;
                }
                break;
        }
        if (!penalidadesAtivas() && r.coracoesMeios < 0) r.coracoesMeios = 0;   // sem punicao (Parametros.h)
        e.dia.habitosCr += r.cr;
        creditar(e, r.cr);
        addXp(e, r.xp, v.atributo);
        const int aplicado = aplicarCoracoes(e, r.coracoesMeios);
        logEvento(e, TipoEvento::Habito, r.cr, r.xp, aplicado, 1000 + static_cast<int>(indice));
        return r;
    }

    ResultadoOcorrencia pularOcorrencia(Estado& e, size_t indice) {
        ResultadoOcorrencia r;
        r.indice = indice;
        if (indice >= e.dia.ocorrencias.size()) {
            return r;
        }
        OcorrenciaDia& oc = e.dia.ocorrencias[indice];
        if (oc.feita || oc.status != StatusHabito::Pendente) {
            r.status = oc.status;
            return r;
        }
        oc.status = StatusHabito::Pulado;   // feita continua false: nao entra na fila, nao paga, nao vira esquecida
        oc.minutoReal = e.dia.minutoAtual;
        r.registrado = true;
        r.status = StatusHabito::Pulado;
        return r;
    }

    InstanteHabito capturarHabito(const Estado& e, size_t indice) {
        InstanteHabito s;
        if (indice >= e.dia.ocorrencias.size()) return s;
        s.valido = true;
        s.indice = indice;
        s.dia = e.dia.data;
        s.antes = e.dia.ocorrencias[indice];
        s.saldo = e.saldo;
        s.cofre = e.cofre;
        s.xpTotal = e.xpTotal;
        s.pontosAtributo = e.pontosAtributo;
        s.coracoesMeios = e.coracoesMeios;
        s.heroiCaido = e.heroiCaido;
        s.resgateDiasValidos = e.resgateDiasValidos;
        s.patamarDisparado = e.patamarDisparado;
        s.habitosCr = e.dia.habitosCr;
        s.crGanho = e.dia.crGanho;
        s.xpGanho = e.dia.xpGanho;
        s.ganhoCoracoesMeios = e.dia.ganhoCoracoesMeios;
        s.coracoesDeltaMeios = e.dia.coracoesDeltaMeios;
        s.eventosAntes = e.dia.eventos.size();
        return s;
    }

    void fecharCapturaHabito(InstanteHabito& s, const Estado& e) {
        if (!s.valido || s.indice >= e.dia.ocorrencias.size()) {
            s.valido = false;
            return;
        }
        s.eventosDepois = e.dia.eventos.size();
        s.statusDepois = e.dia.ocorrencias[s.indice].status;
        s.feitaDepois = e.dia.ocorrencias[s.indice].feita;
    }

    bool podeDesfazerHabito(const Estado& e, const InstanteHabito& s) {
        if (!s.valido || s.indice >= e.dia.ocorrencias.size()) return false;
        if (e.dia.fechado || !(e.dia.data == s.dia)) return false;
        const OcorrenciaDia& oc = e.dia.ocorrencias[s.indice];
        if (oc.habitoId != s.antes.habitoId || oc.indice != s.antes.indice) return false;
        if (oc.status == StatusHabito::Pendente) return false;          // nada a reverter
        if (oc.status != s.statusDepois || oc.feita != s.feitaDepois) return false;   // mudou desde a marcacao
        return e.dia.eventos.size() == s.eventosDepois;                  // algo mais aconteceu: nao arrisca
    }

    bool desfazerOcorrencia(Estado& e, const InstanteHabito& s) {
        if (!podeDesfazerHabito(e, s)) return false;
        e.dia.ocorrencias[s.indice] = s.antes;
        e.saldo = s.saldo;
        e.cofre = s.cofre;
        e.xpTotal = s.xpTotal;
        e.pontosAtributo = s.pontosAtributo;
        e.coracoesMeios = s.coracoesMeios;
        e.heroiCaido = s.heroiCaido;
        e.resgateDiasValidos = s.resgateDiasValidos;
        e.patamarDisparado = s.patamarDisparado;
        e.dia.habitosCr = s.habitosCr;
        e.dia.crGanho = s.crGanho;
        e.dia.xpGanho = s.xpGanho;
        e.dia.ganhoCoracoesMeios = s.ganhoCoracoesMeios;
        e.dia.coracoesDeltaMeios = s.coracoesDeltaMeios;
        e.dia.eventos.resize(std::min(e.dia.eventos.size(), s.eventosAntes));
        return true;
    }

    double registrarHumor(Estado& e) {
        if (e.dia.humorRegistros >= param::kHumorMaxDia) {
            return 0.0;
        }
        ++e.dia.humorRegistros;
        addXp(e, param::kXpHumor, Atributo::Energia);  // 0 cr, sempre
        logEvento(e, TipoEvento::Humor, 0.0, param::kXpHumor, 0, 0);
        return param::kXpHumor;
    }

    double registrarTickTick(Estado& e) {
        if (e.dia.tickTick >= param::kTickTickMaxDia) {
            return 0.0;
        }
        ++e.dia.tickTick;
        addXp(e, param::kXpTickTick, Atributo::Oficio);
        logEvento(e, TipoEvento::TickTick, 0.0, param::kXpTickTick, 0, 0);
        return param::kXpTickTick;
    }

    // -------------------------------------------------------- transicao / perdido / lazer
    double registrarPerdido(Estado& e, int minutos) {
        if (minutos <= 0) {
            return 0.0;
        }
        const int antes = e.dia.perdidoMin;
        const int depois = antes + minutos;
        e.dia.perdidoMin = depois;
        const int pagosAntes = std::max(0, antes - param::kPerdidoGratisMin);
        const int pagosDepois = std::max(0, depois - param::kPerdidoGratisMin);
        double custo = (pagosDepois - pagosAntes) * param::kPrecoPerdido;
        if (e.dia.recuperacao || congelado(e)) {
            custo = 0.0;
        }
        logEvento(e, TipoEvento::Perdido, -custo, 0.0, 0, minutos);
        debitar(e, custo);
        return custo;
    }

    double registrarLazer(Estado& e, int minutos) {
        if (minutos <= 0) {
            return 0.0;
        }
        double custo = minutos * param::kPrecoLazer;
        if (e.dia.recuperacao || congelado(e)) {
            custo = 0.0;
        }
        logEvento(e, TipoEvento::Lazer, -custo, 0.0, 0, minutos);
        debitar(e, custo);  // so a carteira paga lazer; o Cofre nao
        return custo;
    }

    ResultadoTransicao registrarTransicao(Estado& e, int minutosIntervalo,
                                          std::optional<ClasseIntervalo> classe) {
        ResultadoTransicao r;
        r.excessoMin = std::max(0, minutosIntervalo - toleranciaTransicaoMin(e));
        if (r.excessoMin == 0) {
            return r;
        }
        int sobra = r.excessoMin;
        if (classe) {
            sobra = aplicarClasse(e, sobra, *classe, r);
        }
        e.dia.transicaoPendenteMin += sobra;
        r.pendenteMin = sobra;
        logEvento(e, TipoEvento::Transicao, -r.custoCr, 0.0, 0, r.excessoMin);
        return r;
    }

    ResultadoTransicao classificarTransicaoPendente(Estado& e, ClasseIntervalo classe, int minutos) {
        ResultadoTransicao r;
        const int usa = std::clamp(minutos, 0, e.dia.transicaoPendenteMin);
        r.excessoMin = usa;
        if (usa == 0) {
            return r;
        }
        e.dia.transicaoPendenteMin -= usa;
        const int sobra = aplicarClasse(e, usa, classe, r);
        e.dia.transicaoPendenteMin += sobra;
        r.pendenteMin = sobra;
        return r;
    }

    // -------------------------------------------------------------------------- dia
    void avancarRelogio(Estado& e, int minutoDoDia) {
        e.dia.minutoAtual = std::clamp(minutoDoDia, 0, 1439);
        atualizarDesafiosVencidos(e);
    }

    void abrirDia(Estado& e, const Data& data, int descansoOrcamentoMin) {
        if (!e.dia.aberto) {
            resetDia(e, data, descansoOrcamentoMin);
        } else {
            while (e.dia.data < data) {
                if (!congelado(e) && !e.dia.fechado) {
                    fecharDia(e, false);
                }
                const Data proximo = dataDeSerial(diaSerial(e.dia.data) + 1);
                resetDia(e, proximo, e.dia.descansoOrcamentoMin);
            }
            e.dia.descansoOrcamentoMin = descansoOrcamentoMin;
        }
        e.dia.minutoAtual = 0;
        if (!congelado(e)) {
            atualizarDesafiosVencidos(e);
            avaliarGatilhos(e);
        }
    }

    ResumoDia fecharDia(Estado& e, bool reciboVisto) {
        ResumoDia resumo;
        Dia& d = e.dia;
        resumo.data = d.data;
        if (!d.aberto || d.fechado) {
            return resumo;
        }

        // 1. transicao nao classificada vira Perdido
        if (d.transicaoPendenteMin > 0) {
            const int pend = d.transicaoPendenteMin;
            d.transicaoPendenteMin = 0;
            registrarPerdido(e, pend);
        }

        // 2. habitos pendentes viram esquecidos (medicacao esquecida = -1 coracao).
        //    Modo "ocorrencias" (habitos do TickTick): criterio por FRACAO das ocorrencias do dia.
        //    Modo legado (enum de 7 fixos): so vale se algum habito legado foi registrado hoje.
        //    Sem habitos configurados nem registrados: o criterio de habitos e ignorado (nao bloqueia o dia valido
        //    nem rende coracao).
        int noHorario = 0;
        bool criterioHabitos = true;      // dia valido
        bool habitosBons = false;         // >= 80 % -> +1/2 coracao
        if (!d.ocorrencias.empty()) {
            size_t pulados = 0;
            for (OcorrenciaDia& oc : d.ocorrencias) {
                if (!oc.feita && oc.status == StatusHabito::Pendente) {
                    oc.status = StatusHabito::Esquecido;
                    if (oc.tipo == TipoHabito::Medicacao) {
                        aplicarCoracoes(e, -2);
                    }
                }
                if (oc.status == StatusHabito::NoHorario) {
                    ++noHorario;
                }
                if (oc.status == StatusHabito::Pulado) {
                    ++pulados;
                }
            }
            // "Pular" tira a ocorrencia do denominador (nao conta como esquecida). So puladas = criterio ignorado.
            const size_t base = d.ocorrencias.size() - pulados;
            if (base > 0) {
                const double fracao = static_cast<double>(noHorario) / static_cast<double>(base);
                criterioHabitos = fracao >= static_cast<double>(param::kDiaValidoHabitos) / kNumHabitos - kEps;
                habitosBons = fracao >= param::kHabitosBomFracao - kEps;
            }
        } else {
            bool legadoUsado = false;
            for (size_t i = 0; i < kNumHabitos; ++i) {
                legadoUsado = legadoUsado || d.habitos[i] != StatusHabito::Pendente;
            }
            if (legadoUsado) {
                for (size_t i = 0; i < kNumHabitos; ++i) {
                    if (d.habitos[i] == StatusHabito::Pendente) {
                        d.habitos[i] = StatusHabito::Esquecido;
                        if (static_cast<Habito>(i) == Habito::Medicacao) {
                            aplicarCoracoes(e, -2);
                        }
                    }
                    if (d.habitos[i] == StatusHabito::NoHorario) {
                        ++noHorario;
                    }
                }
                criterioHabitos = noHorario >= param::kDiaValidoHabitos;
                habitosBons = static_cast<double>(noHorario) / kNumHabitos >= param::kHabitosBomFracao - kEps;
            }
        }
        resumo.habitosNoHorario = noHorario;

        const bool recuperacao = d.recuperacao;
        resumo.recuperacao = recuperacao;
        resumo.valido = d.fochoValidoMin >= param::kDiaValidoFocoMin && criterioHabitos;
        resumo.limpo = e.saldo >= 0.0 && d.perdidoMin <= param::kDiaLimpoPerdidoMax;
        resumo.fechamentoNegativo = e.saldo < 0.0 && !recuperacao;

        // 3. corações
        if (d.perdidoMin > param::kPerdidoCoracaoMin) {
            aplicarCoracoes(e, -1);
        }
        if (habitosBons) {
            aplicarCoracoes(e, 1);
        }
        if (resumo.limpo) {
            aplicarCoracoes(e, 1);
        }
        if (e.heroiCaido && !recuperacao) {
            if (resumo.valido) {
                if (++e.resgateDiasValidos >= param::kResgateDiasValidos) {
                    e.heroiCaido = false;
                    e.resgateDiasValidos = 0;
                    e.coracoesMeios = std::min(param::kResgateMeios, coracoesMaxMeios(e));
                    d.coracoesDeltaMeios += e.coracoesMeios;
                }
            } else {
                e.resgateDiasValidos = 0;
            }
        }

        // 4. fechamentos negativos seguidos
        if (!recuperacao && penalidadesAtivas()) {
            if (e.saldo < 0.0) {
                if (++e.fechamentosNegativosSeguidos >= param::kFechamentosNegativos) {
                    e.fechamentosNegativosSeguidos = 0;
                    e.gatilhoFechamentoPendente = true;
                    avaliarGatilhos(e);  // dispara hoje se ainda nao houve desafio; senao amanha
                }
            } else {
                e.fechamentosNegativosSeguidos = 0;
            }
        }

        // 5. streak (1 folga/semana; Guarda-Brasa +1). Recuperacao e neutra.
        if (!recuperacao) {
            if (resumo.valido) {
                ++e.streak;
            } else if (e.folgasUsadasSemana < folgasStreakMax(e)) {
                ++e.folgasUsadasSemana;
                resumo.folgaUsada = true;
            } else {
                resumo.streakQuebrado = e.streak > 0;
                e.streak = 0;
            }
        }

        // 6. XP de fechar o dia
        if (reciboVisto) {
            addXp(e, param::kXpFecharDia, std::nullopt);
            logEvento(e, TipoEvento::FecharDia, 0.0, param::kXpFecharDia, 0, 0);
        }

        d.fechado = true;
        resumo.crGanho = d.crGanho;
        resumo.crGasto = d.crGasto;
        resumo.saldoDoDia = d.crGanho - d.crGasto;
        resumo.saldoFinal = e.saldo;
        resumo.xpGanho = d.xpGanho;
        resumo.perdidoMin = d.perdidoMin;
        resumo.coracoesDeltaMeios = d.coracoesDeltaMeios;
        return resumo;
    }

    // -------------------------------------------------------------- salvaguardas
    bool declararRecuperacao(Estado& e) {
        if (!e.dia.aberto || e.dia.fechado || e.dia.recuperacao || congelado(e)) {
            return false;
        }
        if (e.dia.minutoAtual > param::kRecuperacaoLimiteMinutoDia) {
            return false;
        }
        const int chave = chaveMes(e.dia.data);
        if (chave != e.recuperacoesMesChave) {
            e.recuperacoesMesChave = chave;
            e.recuperacoesUsadasMes = 0;
        }
        if (e.recuperacoesUsadasMes >= param::kRecuperacoesPorMes) {
            return false;
        }
        ++e.recuperacoesUsadasMes;
        e.dia.recuperacao = true;
        return true;
    }

    bool iniciarFerias(Estado& e) {
        if (e.ferias || !e.dia.aberto) {
            return false;
        }
        if (!e.dia.fechado) {
            fecharDia(e, false);
        }
        e.ferias = true;
        e.feriasInicioSerial = diaSerial(e.dia.data);
        return true;
    }

    bool encerrarFerias(Estado& e, const Data& hoje) {
        if (!e.ferias) {
            return false;
        }
        const int64_t dias = diaSerial(hoje) - e.feriasInicioSerial;
        if (dias < param::kFeriasMinDias) {
            return false;
        }
        for (Desafio& d : e.desafiosPendentes) {  // o prazo tambem congela
            d.prazoEmMin += dias * 1440;
            d.criadoEmMin += dias * 1440;
        }
        e.ferias = false;
        resetDia(e, hoje, e.dia.descansoOrcamentoMin);
        return true;
    }

} // namespace tempo
