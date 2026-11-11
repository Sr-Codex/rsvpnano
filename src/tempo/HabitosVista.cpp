#include "tempo/HabitosVista.h"

#include <algorithm>

#include "tempo/Parametros.h"

namespace tempo {

    TotaisDia totaisDoDia(const Estado& e) {
        TotaisDia t;
        t.total = static_cast<int>(e.dia.ocorrencias.size());
        t.cr = e.dia.habitosCr;
        for (const OcorrenciaDia& oc : e.dia.ocorrencias) {
            if (oc.feita) ++t.feitas;
            else if (oc.status == StatusHabito::Pulado) ++t.puladas;
            else if (oc.status == StatusHabito::Esquecido) ++t.esquecidas;
            else ++t.pendentes;
        }
        return t;
    }

    namespace {
        Cartao cartaoDe(const Estado& e, size_t indice, int minutoAgora) {
            const OcorrenciaDia& oc = e.dia.ocorrencias[indice];
            Cartao c;
            c.indice = indice;
            c.fase = faseDaOcorrencia(oc, minutoAgora);
            c.confirmar2Toques = exigeConfirmacao(oc.tipo);
            const bool med = c.confirmar2Toques;
            switch (c.fase) {
            case FaseOcorrencia::Futura:   // fora da janela: nada de "Feito agora" (cairia como esquecido); pular sim
                c.modo = ModoCartao::Proximo;
                c.minutosAte = oc.minuto >= 0 ? std::max(1, oc.minuto - minutoAgora) : 0;
                c.podePular = !med;
                break;
            case FaseOcorrencia::Agora:
                if (oc.minuto >= 0 && oc.minuto > minutoAgora) {
                    c.modo = ModoCartao::Proximo;
                    c.minutosAte = oc.minuto - minutoAgora;
                } else {
                    c.modo = ModoCartao::Agora;
                    c.atrasoMin = oc.minuto >= 0 ? minutoAgora - oc.minuto : 0;
                }
                c.podeFeito = true;
                c.podePular = true;
                break;
            case FaseOcorrencia::Atrasada:
                c.modo = ModoCartao::Atrasado;
                c.atrasoMin = minutoAgora - oc.minuto;
                c.podeFeito = true;
                c.podePular = true;
                break;
            case FaseOcorrencia::Perdida:
                c.modo = ModoCartao::Perdido;
                c.atrasoMin = minutoAgora - oc.minuto;
                c.podeFeito = med;      // medicacao: ainda vale registrar a hora real; os demais so pulam
                c.podePular = true;
                break;
            case FaseOcorrencia::Feita:
            case FaseOcorrencia::FeitaAtrasada:
                c.modo = ModoCartao::Feito;
                break;
            case FaseOcorrencia::Pulada:
                c.modo = ModoCartao::Pulado;
                break;
            case FaseOcorrencia::Esquecida:
                c.modo = ModoCartao::Esquecido;
                break;
            }
            return c;
        }
    } // namespace

    Cartao montarCartao(const Estado& e, int minutoAgora, std::optional<size_t> selecionado) {
        Cartao c;
        if (e.dia.ocorrencias.empty()) return c;   // SemHabitos
        if (selecionado && *selecionado < e.dia.ocorrencias.size()) return cartaoDe(e, *selecionado, minutoAgora);
        if (const auto proxima = ocorrenciaAgora(e, minutoAgora)) return cartaoDe(e, proxima->indice, minutoAgora);
        c.modo = ModoCartao::DiaCompleto;
        return c;
    }

    std::optional<size_t> alternarSelecao(std::optional<size_t> atual, size_t tocado) {
        if (atual && *atual == tocado) return std::nullopt;
        return tocado;
    }

    int rolagemPara(size_t n, size_t foco, size_t visiveis) {
        if (n <= visiveis) return 0;
        const int maximo = static_cast<int>(n - visiveis);
        return std::clamp(static_cast<int>(foco) - 2, 0, maximo);
    }

    int rolar(int atual, int delta, size_t n, size_t visiveis) {
        const int maximo = n > visiveis ? static_cast<int>(n - visiveis) : 0;
        return std::clamp(atual + delta, 0, maximo);
    }

    std::optional<int> minutoProximoHabito(const Estado& e, int minutoAgora) {
        std::optional<int> melhor;
        for (const OcorrenciaDia& oc : e.dia.ocorrencias) {
            if (oc.feita || oc.status != StatusHabito::Pendente || oc.minuto < 0 || oc.minuto <= minutoAgora) continue;
            if (!melhor || oc.minuto < *melhor) melhor = oc.minuto;
        }
        return melhor;
    }

    // ------------------------------------------------------------------ Habitos v2
    IconeHabito iconeDoHabito(TipoHabito tipo, std::string_view nome) {
        switch (tipo) {
            case TipoHabito::Academia: return IconeHabito::Haltere;
            case TipoHabito::Suplemento: return IconeHabito::Frasco;
            case TipoHabito::Medicacao: return IconeHabito::Capsula;
            case TipoHabito::Almoco: return IconeHabito::Talher;
            case TipoHabito::Dentes: return IconeHabito::Dente;
            case TipoHabito::Descanso: return IconeHabito::Xicara;
            case TipoHabito::Banho: return IconeHabito::Gota;
            case TipoHabito::Meditacao: return IconeHabito::Lotus;
            default: break;
        }
        const std::string n = normalizarNome(nome);
        const auto tem = [&](const char* trecho) { return n.find(trecho) != std::string::npos; };
        if (tem("leitur") || tem("ler ") || n == "ler" || tem("livro")) return IconeHabito::Livro;
        if (tem("jantar") || tem("cafe da") || tem("refeic") || tem("comida") || tem("lanche") || tem("almoc")) return IconeHabito::Talher;
        if (tem("treino") || tem("academia") || tem("muscula")) return IconeHabito::Haltere;
        if (tem("remedio") || tem("medicac") || tem("vitamina")) return IconeHabito::Capsula;
        if (tem("dente") || tem("escov")) return IconeHabito::Dente;
        if (tem("pausa") || tem("descans")) return IconeHabito::Xicara;
        if (tem("banho") || tem("agua")) return IconeHabito::Gota;
        if (tem("medit") || tem("respir")) return IconeHabito::Lotus;
        return IconeHabito::Generico;
    }

    namespace {
        bool temToken(std::string_view s, std::initializer_list<std::string_view> prefixos) {
            size_t i = 0;
            while (i <= s.size()) {
                size_t j = s.find('_', i);
                if (j == std::string_view::npos) j = s.size();
                const std::string_view tok = s.substr(i, j - i);
                for (std::string_view p : prefixos) {
                    // palavras curtas (<= 4 letras) so por igualdade; as longas por prefixo ("meditating" -> "medit")
                    if (p.size() <= 4 ? tok == p : tok.substr(0, p.size()) == p) return true;
                }
                i = j + 1;
            }
            return false;
        }
        bool temEmoji(std::string_view s, std::initializer_list<std::string_view> emojis) {
            for (std::string_view e : emojis)
                if (s.find(e) != std::string_view::npos) return true;
            return false;
        }
    } // namespace

    IconeHabito iconeDoHabito(TipoHabito tipo, std::string_view nome, std::string_view iconRes) {
        if (iconRes.substr(0, 6) == "habit_") {
            std::string r;
            for (char c : iconRes.substr(6)) r += (c >= 'A' && c <= 'Z') ? char(c - 'A' + 'a') : c;
            if (temToken(r, {"reading", "read", "book", "study"})) return IconeHabito::Livro;
            if (temToken(r, {"meditat", "yoga", "mindful"})) return IconeHabito::Lotus;
            if (temToken(r, {"gym", "workout", "exercise", "run", "sport", "fitness", "weight", "walk", "stretch"})) return IconeHabito::Haltere;
            if (temToken(r, {"water", "drink", "shower", "bath"})) return IconeHabito::Gota;
            if (temToken(r, {"vitamin", "supplement"})) return IconeHabito::Frasco;
            if (temToken(r, {"medicine", "pill", "med", "drug"})) return IconeHabito::Capsula;
            if (temToken(r, {"eat", "breakfast", "lunch", "dinner", "meal", "fruit", "food", "diet", "cook"})) return IconeHabito::Talher;
            if (temToken(r, {"teeth", "brush", "floss", "tooth"})) return IconeHabito::Dente;
            if (temToken(r, {"sleep", "rest", "relax", "coffee", "tea", "break"})) return IconeHabito::Xicara;
            // demais habit_* (stay_positive, plan, quit_snacks, daily_check_in...): cai para o nome/tipo e, no fim, Generico
        } else if (iconRes.substr(0, 4) == "txt_") {
            if (temEmoji(iconRes, {"📖", "📚", "📕", "📗", "📘"})) return IconeHabito::Livro;
            if (temEmoji(iconRes, {"🧘"})) return IconeHabito::Lotus;
            if (temEmoji(iconRes, {"🏋", "💪", "🏃", "🚴", "🤸"})) return IconeHabito::Haltere;
            if (temEmoji(iconRes, {"💧", "🚿", "🛁", "🚰"})) return IconeHabito::Gota;
            if (temEmoji(iconRes, {"💊"})) return IconeHabito::Capsula;
            if (temEmoji(iconRes, {"🍽", "🍎", "🥗", "🍴", "🥣"})) return IconeHabito::Talher;
            if (temEmoji(iconRes, {"🦷", "🪥"})) return IconeHabito::Dente;
            if (temEmoji(iconRes, {"☕", "🍵", "😴", "🛌"})) return IconeHabito::Xicara;
        }
        const std::string n = normalizarNome(nome);
        const auto tem = [&](const char* trecho) { return n.find(trecho) != std::string::npos; };
        // palavras do nome que o tipo ainda nao cobre (o tipo continua mandando nos demais casos)
        if (tipo == TipoHabito::Geral) {
            if (tem("estud")) return IconeHabito::Livro;
            if (tem("corrida") || tem("caminhada") || tem("alongamento") || tem("exercicio")) return IconeHabito::Haltere;
            if (tem("hidrat")) return IconeHabito::Gota;
            if (tem("cafe da manha")) return IconeHabito::Talher;
        }
        return iconeDoHabito(tipo, nome);
    }

    int32_t corDoHabito(std::string_view hex) {
        if (hex.size() != 7 || hex[0] != '#') return -1;
        uint32_t v = 0;
        for (size_t i = 1; i < 7; ++i) {
            const char c = hex[i];
            uint32_t d;
            if (c >= '0' && c <= '9') d = uint32_t(c - '0');
            else if (c >= 'a' && c <= 'f') d = uint32_t(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F') d = uint32_t(c - 'A' + 10);
            else return -1;
            v = v * 16 + d;
        }
        const auto lum = [](uint32_t rgb) {
            return (0.2126 * ((rgb >> 16) & 255) + 0.7152 * ((rgb >> 8) & 255) + 0.0722 * (rgb & 255)) / 255.0;
        };
        for (int i = 0; i < 20 && lum(v) < 0.42; ++i) {   // clareia em direcao ao branco ate dar contraste no preto
            const auto up = [](uint32_t c) { return uint32_t(c + (255 - c) * 0.2); };
            v = (up((v >> 16) & 255) << 16) | (up((v >> 8) & 255) << 8) | up(v & 255);
        }
        return int32_t(v);
    }

    double fracaoJanela(const OcorrenciaDia& oc, int minutoAgora) {
        if (oc.feita || oc.status == StatusHabito::Pulado || oc.status == StatusHabito::Esquecido)
            return oc.feita ? 1.0 : 0.0;
        if (oc.minuto < 0) return 0.0;
        const ValorHabito& v = param::kValoresTipoHabito[static_cast<size_t>(oc.tipo)];
        const int antes = oc.tipo == TipoHabito::Descanso ? 15 : v.janelaMin;
        const int ini = oc.minuto - antes, fim = oc.minuto + v.janelaMin;
        if (minutoAgora <= fim) return std::clamp(double(minutoAgora - ini) / double(std::max(1, fim - ini)), 0.0, 1.0);
        return std::clamp(double(minutoAgora - fim) / double(param::kJanelaAtrasoMin), 0.0, 1.0);
    }

    double fracaoDoDia(const TotaisDia& t) {
        const int base = t.total - t.puladas;
        return base > 0 ? std::clamp(double(t.feitas) / base, 0.0, 1.0) : 0.0;
    }

    std::vector<GrupoLinha> agruparLinhaDoTempo(const std::vector<VistaOcorrencia>& vistas, std::optional<size_t> foco) {
        std::vector<GrupoLinha> grupos;
        int ultimo = -9999;
        for (const VistaOcorrencia& v : vistas) {
            if (v.minuto < 0) continue;
            if (grupos.empty() || v.minuto - ultimo > kAgrupaMin) grupos.emplace_back();
            grupos.back().indices.push_back(v.indice);
            ultimo = v.minuto;
        }
        for (GrupoLinha& g : grupos) {
            g.minutoIni = vistas[g.indices.front()].minuto;
            g.minutoFim = vistas[g.indices.back()].minuto;
            g.representante = g.indices.front();
            g.todasResolvidas = true;
            g.todasFeitas = true;
            bool achouPendente = false;
            for (size_t idx : g.indices) {
                const VistaOcorrencia& v = vistas[idx];
                const bool feita = v.fase == FaseOcorrencia::Feita || v.fase == FaseOcorrencia::FeitaAtrasada;
                const bool resolvida = feita || v.fase == FaseOcorrencia::Pulada || v.fase == FaseOcorrencia::Esquecida;
                if (!feita) g.todasFeitas = false;
                if (!resolvida) {
                    g.todasResolvidas = false;
                    if (!achouPendente) g.representante = idx;
                    achouPendente = true;
                    if (v.fase == FaseOcorrencia::Atrasada || v.fase == FaseOcorrencia::Perdida) g.atrasado = true;
                }
                if (foco && *foco == idx) g.temFoco = true;
            }
            if (g.temFoco && foco) g.representante = *foco;
        }
        return grupos;
    }

    std::vector<int> posicoesNoEixo(const std::vector<int>& yIdeal, int minimo, int yMax) {
        std::vector<int> y = yIdeal;
        for (size_t i = 1; i < y.size(); ++i) y[i] = std::max(y[i], y[i - 1] + minimo);
        if (!y.empty() && y.back() > yMax) {
            y.back() = yMax;
            for (size_t i = y.size() - 1; i-- > 0;) y[i] = std::min(y[i], y[i + 1] - minimo);
        }
        return y;
    }

    std::optional<HabitoAmanha> primeiroHabitoDeAmanha(const ListaHabitos& lista, const Data& hoje) {
        const Data amanha = dataDeSerial(diaSerial(hoje) + 1);
        for (const OcorrenciaDia& oc : gerarOcorrencias(lista, amanha))   // ordenadas; "qualquer hora" por ultimo
            if (oc.minuto >= 0) return HabitoAmanha{oc.nome, oc.tipo, oc.minuto};
        return std::nullopt;
    }

    AnimacaoCelebracao animacaoCelebracao(uint32_t ms) {
        const auto saida = [](double t) { return 1.0 - (1.0 - t) * (1.0 - t); };   // ease-out
        AnimacaoCelebracao a;
        a.anel = saida(std::clamp(double(ms) / 550.0, 0.0, 1.0));
        a.disco = saida(std::clamp((double(ms) - 120.0) / 350.0, 0.0, 1.0));
        a.faiscas = std::clamp((double(ms) - 200.0) / double(kAnimacaoCelebracaoMs - 200), 0.0, 1.0);
        return a;
    }

    int rolarPx(int atual, int delta, size_t n, int alturaLinha, int alturaVisivel) {
        const int maximo = std::max(0, int(n) * alturaLinha - alturaVisivel);
        return std::clamp(atual + delta, 0, maximo);
    }

    std::optional<size_t> linhaNoToque(int rolagemPx, int yRelativo, size_t n, int alturaLinha) {
        if (yRelativo < 0 || alturaLinha <= 0) return std::nullopt;
        const size_t i = size_t((yRelativo + rolagemPx) / alturaLinha);
        if (i >= n) return std::nullopt;
        return i;
    }

    int rolagemPxPara(size_t n, size_t foco, int alturaLinha, int alturaVisivel) {
        return rolarPx(0, int(foco) * alturaLinha - (alturaVisivel - alturaLinha) / 2, n, alturaLinha, alturaVisivel);
    }

} // namespace tempo
