#include "tempo/Habitos.h"

#include <glaze/json.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "tempo/Parametros.h"
#include "tempo/Perfil.h"

namespace tempo {
    namespace {

        constexpr glz::opts kLeitura{.error_on_unknown_keys = false};

        // Decodifica um codepoint UTF-8; em byte invalido devolve U+FFFD e avanca 1 byte.
        uint32_t proximoCp(std::string_view& texto) {
            const auto b0 = static_cast<uint8_t>(texto.front());
            size_t n = 1;
            uint32_t cp = b0;
            if (b0 >= 0xF0 && b0 < 0xF8) {
                n = 4;
                cp = b0 & 0x07U;
            } else if (b0 >= 0xE0 && b0 < 0xF0) {
                n = 3;
                cp = b0 & 0x0FU;
            } else if (b0 >= 0xC0 && b0 < 0xE0) {
                n = 2;
                cp = b0 & 0x1FU;
            } else if (b0 >= 0x80) {
                texto.remove_prefix(1);
                return 0xFFFD;
            }
            if (texto.size() < n) {
                texto.remove_prefix(1);
                return 0xFFFD;
            }
            for (size_t i = 1; i < n; ++i) {
                const auto b = static_cast<uint8_t>(texto[i]);
                if ((b & 0xC0U) != 0x80U) {
                    texto.remove_prefix(1);
                    return 0xFFFD;
                }
                cp = (cp << 6) | (b & 0x3FU);
            }
            texto.remove_prefix(n);
            return cp;
        }

        // Latin-1 acentuado -> ASCII minusculo (0 = sem equivalente).
        char dobrarLatin1(uint32_t cp) {
            if (cp >= 0xC0 && cp <= 0xDE && cp != 0xD7) cp += 0x20;   // maiuscula -> minuscula
            if (cp >= 0xE0 && cp <= 0xE5) return 'a';
            if (cp == 0xE7) return 'c';
            if (cp >= 0xE8 && cp <= 0xEB) return 'e';
            if (cp >= 0xEC && cp <= 0xEF) return 'i';
            if (cp == 0xF1) return 'n';
            if (cp >= 0xF2 && cp <= 0xF6) return 'o';
            if (cp >= 0xF9 && cp <= 0xFC) return 'u';
            if (cp == 0xFD || cp == 0xFF) return 'y';
            return 0;
        }

        bool contem(std::string_view texto, std::string_view trecho) {
            return texto.find(trecho) != std::string_view::npos;
        }

        bool contemEmoji(std::string_view bruto, const char* utf8) {
            return bruto.find(utf8) != std::string_view::npos;
        }

        constexpr const char* kEmojiMeditacao = "\xF0\x9F\xA7\x98";   // U+1F9D8
        constexpr const char* kEmojiLeitura = "\xF0\x9F\x93\x96";     // U+1F4D6
        constexpr const char* kEmojiFoco = "\xF0\x9F\xA7\xA0";        // U+1F9E0

        int diasNoMes(int ano, int mes) {
            static constexpr int dias[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
            if (mes == 2 && ((ano % 4 == 0 && ano % 100 != 0) || ano % 400 == 0)) return 29;
            return dias[std::clamp(mes, 1, 12) - 1];
        }

        bool lerInteiro(std::string_view texto, int& saida) {
            if (texto.empty() || texto.size() > 9) return false;
            int valor = 0;
            for (char c : texto) {
                if (c < '0' || c > '9') return false;
                valor = valor * 10 + (c - '0');
            }
            saida = valor;
            return true;
        }

        bool ordenarOcorrencia(const OcorrenciaDia& a, const OcorrenciaDia& b) {
            const bool aSem = a.minuto < 0;
            const bool bSem = b.minuto < 0;
            if (aSem != bSem) return !aSem;
            if (a.minuto != b.minuto) return a.minuto < b.minuto;
            if (a.nome != b.nome) return a.nome < b.nome;
            if (a.habitoId != b.habitoId) return a.habitoId < b.habitoId;
            return a.indice < b.indice;
        }

        bool igual(const OcorrenciaDia& a, const OcorrenciaDia& b) {
            return a.habitoId == b.habitoId && a.nome == b.nome && a.tipo == b.tipo && a.indice == b.indice
                && a.total == b.total && a.minuto == b.minuto && a.status == b.status && a.feita == b.feita
                && a.minutoReal == b.minutoReal && a.remota == b.remota;
        }

        std::string horaTexto(int minuto) {
            char buf[16];
            std::snprintf(buf, sizeof buf, "%02d:%02d:00", std::clamp(minuto, 0, 1439) / 60,
                          std::clamp(minuto, 0, 1439) % 60);
            return buf;
        }

        int piso(int64_t a, int64_t b) {   // a/b com piso (b > 0)
            return static_cast<int>(a >= 0 ? a / b : -((-a + b - 1) / b));
        }

    } // namespace

    // ------------------------------------------------------------------ definicoes
    bool lerHabitos(std::string_view json, ListaHabitos& saida) {
        if (json.empty() || json.size() > kMaxBytesHabitos) return false;
        ListaHabitos lida;
        if (glz::read<kLeitura>(lida, json)) return false;
        if (lida.habitos.size() > kMaxHabitos) lida.habitos.resize(kMaxHabitos);
        saida = std::move(lida);
        return true;
    }

    bool habitoArquivado(std::string_view archivedTime) {
        if (archivedTime.size() < 4) return false;
        int ano = 0;
        if (!lerInteiro(archivedTime.substr(0, 4), ano)) return false;
        return ano > 2001;
    }

    // ------------------------------------------------------------------ nomes e tipos
    std::string normalizarNome(std::string_view bruto) {
        std::string saida;
        while (!bruto.empty()) {
            const uint32_t cp = proximoCp(bruto);
            char c = 0;
            if (cp >= 'A' && cp <= 'Z') {
                c = static_cast<char>(cp - 'A' + 'a');
            } else if ((cp >= 'a' && cp <= 'z') || (cp >= '0' && cp <= '9')) {
                c = static_cast<char>(cp);
            } else if (cp >= 0xC0 && cp <= 0xFF) {
                c = dobrarLatin1(cp);
            }
            if (c == 0) c = ' ';
            if (c == ' ' && (saida.empty() || saida.back() == ' ')) continue;
            saida += c;
        }
        while (!saida.empty() && saida.back() == ' ') saida.pop_back();
        return saida;
    }

    std::string nomeParaTela(std::string_view bruto) {
        std::string nome = limparTitulo(bruto);
        if (!nome.empty()) return nome;
        if (contemEmoji(bruto, kEmojiMeditacao)) return "Medita\xC3\xA7\xC3\xA3o";
        if (contemEmoji(bruto, kEmojiLeitura)) return "Leitura";
        if (contemEmoji(bruto, kEmojiFoco)) return "Foco";
        return "H\xC3\xA1" "bito";
    }

    TipoHabito detectarTipo(std::string_view bruto) {
        const std::string n = normalizarNome(bruto);
        if (contem(n, "academia") || contem(n, "treino")) return TipoHabito::Academia;
        if (contem(n, "medicac") || contem(n, "remedio")) return TipoHabito::Medicacao;
        if (contem(n, "medita") || contemEmoji(bruto, kEmojiMeditacao)) return TipoHabito::Meditacao;
        if (contem(n, "suplement")) return TipoHabito::Suplemento;
        if (contem(n, "almoco")) return TipoHabito::Almoco;
        if (contem(n, "banho")) return TipoHabito::Banho;
        if (contem(n, "escovar") || contem(n, "dente")) return TipoHabito::Dentes;
        if (contem(n, "descanso")) return TipoHabito::Descanso;
        return TipoHabito::Geral;
    }

    const char* nomeTipo(TipoHabito tipo) {
        switch (tipo) {
            case TipoHabito::Academia: return "Academia";
            case TipoHabito::Medicacao: return "Medicação";
            case TipoHabito::Meditacao: return "Meditação";
            case TipoHabito::Suplemento: return "Suplemento";
            case TipoHabito::Almoco: return "Almoço";
            case TipoHabito::Banho: return "Banho";
            case TipoHabito::Dentes: return "Dentes";
            case TipoHabito::Descanso: return "Descanso";
            default: return "Geral";
        }
    }

    bool exigeConfirmacao(TipoHabito tipo) {
        return tipo == TipoHabito::Medicacao;
    }

    // ------------------------------------------------------------------ RRULE
    RegraRepeticao lerRegra(std::string_view regra) {
        RegraRepeticao r;
        if (regra.substr(0, 6) == "RRULE:") regra.remove_prefix(6);
        if (regra.empty()) return r;   // Vazia = todo dia

        bool temFreq = false;
        bool bydayInvalido = false;
        bool chaveDesconhecida = false;
        bool temBymonthday = false;
        while (!regra.empty()) {
            const size_t fim = regra.find(';');
            const std::string_view par = regra.substr(0, fim);
            regra = fim == std::string_view::npos ? std::string_view{} : regra.substr(fim + 1);
            const size_t eq = par.find('=');
            if (eq == std::string_view::npos) continue;
            const std::string_view chave = par.substr(0, eq);
            const std::string_view valor = par.substr(eq + 1);
            if (chave == "FREQ") {
                temFreq = true;
                if (valor == "DAILY") r.freq = RegraRepeticao::Freq::Diaria;
                else if (valor == "WEEKLY") r.freq = RegraRepeticao::Freq::Semanal;
                else if (valor == "MONTHLY") r.freq = RegraRepeticao::Freq::Mensal;
                else if (valor == "YEARLY") r.freq = RegraRepeticao::Freq::Anual;
                else r.freq = RegraRepeticao::Freq::NaoSuportada;
            } else if (chave == "INTERVAL") {
                int n = 0;
                r.intervalo = lerInteiro(valor, n) && n >= 1 ? n : 1;
            } else if (chave == "BYDAY") {
                std::string_view dias = valor;
                while (!dias.empty()) {
                    const size_t v = dias.find(',');
                    const std::string_view d = dias.substr(0, v);
                    dias = v == std::string_view::npos ? std::string_view{} : dias.substr(v + 1);
                    static constexpr std::string_view kNomes[7] = {"MO", "TU", "WE", "TH", "FR", "SA", "SU"};
                    bool achou = false;
                    for (int i = 0; i < 7; ++i) {
                        if (d == kNomes[i]) {
                            r.dias = static_cast<uint8_t>(r.dias | (1U << i));
                            achou = true;
                        }
                    }
                    if (!achou) bydayInvalido = true;   // "1MO", "-1FR"...: ordinal nao suportado
                }
            } else if (chave == "BYMONTHDAY") {
                int n = 0;
                if (lerInteiro(valor, n) && n >= 1 && n <= 31) {
                    r.diaDoMes = n;
                    temBymonthday = true;
                } else {
                    chaveDesconhecida = true;
                }
            } else if (chave == "UNTIL") {
                int ymd = 0;
                if (valor.size() >= 8 && lerInteiro(valor.substr(0, 8), ymd)) {
                    r.temAte = true;
                    r.ate = dataDeStamp(ymd);
                }
            } else if (chave == "COUNT" || chave == "WKST") {
                // ignorados
            } else {
                chaveDesconhecida = true;   // BYSETPOS, BYMONTH, BYHOUR...
            }
        }
        if (!temFreq || chaveDesconhecida || bydayInvalido) r.freq = RegraRepeticao::Freq::NaoSuportada;
        if (r.freq == RegraRepeticao::Freq::Diaria && r.dias != 0) r.freq = RegraRepeticao::Freq::NaoSuportada;
        if ((r.freq == RegraRepeticao::Freq::Semanal) && temBymonthday) r.freq = RegraRepeticao::Freq::NaoSuportada;
        if ((r.freq == RegraRepeticao::Freq::Mensal || r.freq == RegraRepeticao::Freq::Anual) && r.dias != 0)
            r.freq = RegraRepeticao::Freq::NaoSuportada;
        return r;
    }

    bool valeNoDia(const RegraRepeticao& regra, const Data& dia, int inicioStamp) {
        using F = RegraRepeticao::Freq;
        if (regra.freq == F::NaoSuportada) return false;
        if (regra.temAte && regra.ate < dia) return false;
        const bool temInicio = inicioStamp >= 10000101;
        const Data inicio = temInicio ? dataDeStamp(inicioStamp) : Data{};
        if (temInicio && dia < inicio) return false;
        const int n = std::max(1, regra.intervalo);
        switch (regra.freq) {
            case F::Vazia:
                return true;
            case F::Diaria:
                if (n == 1 || !temInicio) return true;
                return (diaSerial(dia) - diaSerial(inicio)) % n == 0;
            case F::Semanal: {
                uint8_t dias = regra.dias;
                if (dias == 0) {
                    if (!temInicio) return true;
                    dias = static_cast<uint8_t>(1U << diaDaSemana(inicio));   // sem BYDAY: o dia da semana do inicio
                }
                if ((dias & (1U << diaDaSemana(dia))) == 0) return false;
                if (n == 1 || !temInicio) return true;
                const int64_t semanas = (segundaDaSemana(dia) - segundaDaSemana(inicio)) / 7;
                return semanas % n == 0;
            }
            case F::Mensal: {
                int alvo = regra.diaDoMes;
                if (alvo == 0) {
                    if (!temInicio) return false;
                    alvo = inicio.dia;
                }
                if (dia.dia != std::min(alvo, diasNoMes(dia.ano, dia.mes))) return false;
                if (n == 1 || !temInicio) return true;
                return (chaveMes(dia) - chaveMes(inicio)) % n == 0;
            }
            case F::Anual:
                if (!temInicio) return false;
                if (dia.mes != inicio.mes || dia.dia != inicio.dia) return false;
                return (dia.ano - inicio.ano) % n == 0;
            default:
                return false;
        }
    }

    // ------------------------------------------------------------------ ocorrencias
    std::vector<int> minutosDosLembretes(const std::vector<std::string>& reminders) {
        std::vector<int> minutos;
        for (const std::string& r : reminders) {
            const int m = minutoDeHora(r);
            if (m >= 0) minutos.push_back(m);
        }
        std::sort(minutos.begin(), minutos.end());
        minutos.erase(std::unique(minutos.begin(), minutos.end()), minutos.end());
        return minutos;
    }

    std::vector<OcorrenciaDia> gerarOcorrencias(const ListaHabitos& lista, const Data& dia) {
        std::vector<OcorrenciaDia> saida;
        for (const DefHabito& def : lista.habitos) {
            if (def.id.empty()) continue;
            if (!valeNoDia(lerRegra(def.repeatRule), dia, def.targetStartDate)) continue;
            std::vector<int> minutos = minutosDosLembretes(def.reminders);
            if (minutos.size() > param::kMaxOcorrenciasPorHabito) minutos.resize(param::kMaxOcorrenciasPorHabito);
            const size_t porMeta = def.goal >= 2.0 ? static_cast<size_t>(std::lround(def.goal)) : 1;
            const size_t total = std::clamp<size_t>(std::max<size_t>(minutos.size(), porMeta), 1,
                                                    param::kMaxOcorrenciasPorHabito);
            const std::string nome = nomeParaTela(def.name);
            const TipoHabito tipo = detectarTipo(def.name);
            for (size_t i = 0; i < total; ++i) {
                OcorrenciaDia oc;
                oc.habitoId = def.id;
                oc.nome = nome;
                oc.tipo = tipo;
                oc.indice = static_cast<int>(i);
                oc.total = static_cast<int>(total);
                oc.minuto = i < minutos.size() ? minutos[i] : -1;
                saida.push_back(std::move(oc));
            }
        }
        std::stable_sort(saida.begin(), saida.end(), ordenarOcorrencia);
        if (saida.size() > param::kMaxOcorrenciasDia) saida.resize(param::kMaxOcorrenciasDia);
        return saida;
    }

    bool reconciliarDia(Estado& e, const ListaHabitos& lista) {
        const std::vector<OcorrenciaDia> antes = e.dia.ocorrencias;
        std::vector<OcorrenciaDia> geradas = gerarOcorrencias(lista, e.dia.data);
        std::vector<OcorrenciaDia> novas;
        std::vector<bool> usada(antes.size(), false);
        for (OcorrenciaDia& g : geradas) {
            bool achou = false;
            for (size_t i = 0; i < antes.size(); ++i) {
                if (usada[i] || antes[i].habitoId != g.habitoId || antes[i].indice != g.indice) continue;
                usada[i] = true;
                achou = true;
                OcorrenciaDia oc = antes[i];
                if (!oc.feita && oc.status != StatusHabito::Pulado) {   // pendente: acompanha a definicao atual; feita/pulada: congelada
                    oc.nome = g.nome;
                    oc.tipo = g.tipo;
                    oc.total = g.total;
                    oc.minuto = g.minuto;
                }
                novas.push_back(std::move(oc));
                break;
            }
            if (!achou) novas.push_back(std::move(g));
        }
        for (size_t i = 0; i < antes.size(); ++i) {   // feitas (e puladas) de habitos que sumiram ficam
            if (!usada[i] && (antes[i].feita || antes[i].status == StatusHabito::Pulado)) novas.push_back(antes[i]);
        }
        std::stable_sort(novas.begin(), novas.end(), ordenarOcorrencia);
        if (novas.size() > param::kMaxOcorrenciasDia) novas.resize(param::kMaxOcorrenciasDia);
        bool mudou = novas.size() != antes.size();
        for (size_t i = 0; !mudou && i < novas.size(); ++i) mudou = !igual(novas[i], antes[i]);
        if (mudou) e.dia.ocorrencias = std::move(novas);
        return mudou;
    }

    // ------------------------------------------------------------------ visao
    FaseOcorrencia faseDaOcorrencia(const OcorrenciaDia& oc, int minutoAgora) {
        if (oc.feita) {
            switch (oc.status) {
                case StatusHabito::NoHorario: return FaseOcorrencia::Feita;
                case StatusHabito::Atrasado: return FaseOcorrencia::FeitaAtrasada;
                default: return FaseOcorrencia::Esquecida;
            }
        }
        if (oc.status == StatusHabito::Pulado) return FaseOcorrencia::Pulada;
        if (oc.status == StatusHabito::Esquecido) return FaseOcorrencia::Esquecida;
        if (oc.minuto < 0) return FaseOcorrencia::Agora;
        const int janela = param::kValoresTipoHabito[static_cast<size_t>(oc.tipo)].janelaMin;
        const int antes = oc.tipo == TipoHabito::Descanso ? 15 : janela;
        if (minutoAgora < oc.minuto - antes) return FaseOcorrencia::Futura;
        if (minutoAgora <= oc.minuto + janela) return FaseOcorrencia::Agora;
        if (minutoAgora <= oc.minuto + janela + param::kJanelaAtrasoMin) return FaseOcorrencia::Atrasada;
        return FaseOcorrencia::Perdida;
    }

    std::vector<VistaOcorrencia> ocorrenciasDoDia(const Estado& e, int minutoAgora) {
        std::vector<VistaOcorrencia> saida;
        for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i) {
            const OcorrenciaDia& oc = e.dia.ocorrencias[i];
            const ValorHabito& v = param::kValoresTipoHabito[static_cast<size_t>(oc.tipo)];
            VistaOcorrencia vista;
            vista.indice = i;
            vista.nome = oc.nome;
            vista.tipo = oc.tipo;
            vista.minuto = oc.minuto;
            vista.fase = faseDaOcorrencia(oc, minutoAgora);
            vista.confirmar2Toques = exigeConfirmacao(oc.tipo);
            vista.cr = v.cr;
            vista.xp = v.xp;
            saida.push_back(std::move(vista));
        }
        return saida;
    }

    std::optional<ProximaOcorrencia> ocorrenciaAgora(const Estado& e, int minutoAgora) {
        std::optional<size_t> agora, atrasada, futura, semHora;
        for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i) {
            const OcorrenciaDia& oc = e.dia.ocorrencias[i];
            if (oc.feita || oc.status != StatusHabito::Pendente) continue;
            const FaseOcorrencia fase = faseDaOcorrencia(oc, minutoAgora);
            if (oc.minuto < 0) {
                if (!semHora) semHora = i;
                continue;
            }
            if (fase == FaseOcorrencia::Agora && !agora) agora = i;
            else if (fase == FaseOcorrencia::Atrasada && !atrasada) atrasada = i;
            else if (fase == FaseOcorrencia::Futura && !futura) futura = i;
        }
        if (agora) return ProximaOcorrencia{*agora, FaseOcorrencia::Agora};
        if (atrasada) return ProximaOcorrencia{*atrasada, FaseOcorrencia::Atrasada};
        if (futura) return ProximaOcorrencia{*futura, FaseOcorrencia::Futura};
        if (semHora) return ProximaOcorrencia{*semHora, FaseOcorrencia::Agora};
        return std::nullopt;
    }

    std::optional<int> proximoLembrete(const Estado& e, int minutoAgora) {
        std::optional<int> melhor;
        for (const OcorrenciaDia& oc : e.dia.ocorrencias) {
            if (oc.feita || oc.status != StatusHabito::Pendente || oc.minuto < 0 || oc.minuto < minutoAgora) continue;
            if (!melhor || oc.minuto < *melhor) melhor = oc.minuto;
        }
        return melhor;
    }

    std::optional<size_t> proximaPendenteDoTipo(const Estado& e, TipoHabito tipo) {
        for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i) {   // ja ordenadas: com horario por minuto, sem horario no fim
            const OcorrenciaDia& oc = e.dia.ocorrencias[i];
            if (oc.tipo == tipo && !oc.feita && oc.status == StatusHabito::Pendente) return i;
        }
        return std::nullopt;
    }

    int ocorrenciasFeitas(const Estado& e, std::string_view habitoId) {
        int n = 0;
        for (const OcorrenciaDia& oc : e.dia.ocorrencias)
            if (oc.feita && oc.habitoId == habitoId) ++n;
        return n;
    }

    // ------------------------------------------------------------------ datas / tempo
    int stampDe(const Data& data) {
        return data.ano * 10000 + data.mes * 100 + data.dia;
    }

    Data dataDeStamp(int stamp) {
        Data d;
        const int ano = stamp / 10000;
        const int mes = stamp / 100 % 100;
        const int dia = stamp % 100;
        if (ano < 1970 || mes < 1 || mes > 12 || dia < 1 || dia > 31) return d;
        d.ano = ano;
        d.mes = mes;
        d.dia = dia;
        return d;
    }

    std::string tempoCheckinUtc(const Data& local, int minuto, int fusoMin) {
        const int64_t utcMin = diaSerial(local) * 1440 + std::clamp(minuto, 0, 1439) - fusoMin;
        const Data d = dataDeSerial(piso(utcMin, 1440));
        const int m = static_cast<int>(utcMin - static_cast<int64_t>(diaSerial(d)) * 1440);
        char buf[32];
        std::snprintf(buf, sizeof buf, "%04d-%02d-%02dT%02d:%02d:00+0000", d.ano, d.mes, d.dia, m / 60, m % 60);
        return buf;
    }

    std::optional<MomentoLocal> interpretarTempo(std::string_view iso, int fusoMin) {
        // AAAA-MM-DDTHH:MM:SS[.fff](Z|+HHMM|+HH:MM|-HHMM)
        if (iso.size() < 19 || iso[4] != '-' || iso[7] != '-' || (iso[10] != 'T' && iso[10] != ' ') || iso[13] != ':')
            return std::nullopt;
        int a = 0, mo = 0, d = 0, h = 0, mi = 0;
        if (!lerInteiro(iso.substr(0, 4), a) || !lerInteiro(iso.substr(5, 2), mo) || !lerInteiro(iso.substr(8, 2), d)
            || !lerInteiro(iso.substr(11, 2), h) || !lerInteiro(iso.substr(14, 2), mi))
            return std::nullopt;
        if (a < 1970 || mo < 1 || mo > 12 || d < 1 || d > 31 || h > 23 || mi > 59) return std::nullopt;
        std::string_view resto = iso.substr(19);
        if (!resto.empty() && resto.front() == '.') {
            resto.remove_prefix(1);
            while (!resto.empty() && resto.front() >= '0' && resto.front() <= '9') resto.remove_prefix(1);
        }
        int offsetMin = 0;   // deslocamento do texto em relacao a UTC
        if (!resto.empty() && (resto.front() == '+' || resto.front() == '-')) {
            const int sinal = resto.front() == '-' ? -1 : 1;
            resto.remove_prefix(1);
            int hh = 0, mm = 0;
            if (resto.size() >= 4 && resto[2] == ':') {
                if (!lerInteiro(resto.substr(0, 2), hh) || !lerInteiro(resto.substr(3, 2), mm)) return std::nullopt;
            } else if (resto.size() >= 4) {
                if (!lerInteiro(resto.substr(0, 2), hh) || !lerInteiro(resto.substr(2, 2), mm)) return std::nullopt;
            } else {
                return std::nullopt;
            }
            offsetMin = sinal * (hh * 60 + mm);
        }
        Data base;
        base.ano = a;
        base.mes = mo;
        base.dia = d;
        const int64_t utcMin = diaSerial(base) * 1440 + h * 60 + mi - offsetMin;
        const int64_t localMin = utcMin + fusoMin;
        MomentoLocal saida;
        saida.data = dataDeSerial(piso(localMin, 1440));
        saida.minuto = static_cast<int>(localMin - diaSerial(saida.data) * 1440);
        return saida;
    }

    // ------------------------------------------------------------------ fila
    std::string linhaFila(const ItemFila& item) {
        std::string saida;
        if (glz::write_json(item, saida)) return {};
        saida += '\n';
        return saida;
    }

    std::string serializarFila(const std::vector<ItemFila>& fila) {
        std::string saida;
        for (const ItemFila& item : fila) saida += linhaFila(item);
        return saida;
    }

    std::vector<ItemFila> lerFila(std::string_view jsonl) {
        std::vector<ItemFila> fila;
        while (!jsonl.empty()) {
            const size_t nl = jsonl.find('\n');
            const std::string_view linha = jsonl.substr(0, nl);
            jsonl = nl == std::string_view::npos ? std::string_view{} : jsonl.substr(nl + 1);
            if (linha.empty() || linha.size() > 1024) continue;
            ItemFila item;
            if (!glz::read<kLeitura>(item, linha) && !item.id.empty() && item.stamp > 0) fila.push_back(std::move(item));
        }
        return fila;
    }

    void enfileirar(std::vector<ItemFila>& fila, ItemFila novo) {
        for (ItemFila& item : fila) {
            if (item.id != novo.id || item.stamp != novo.stamp) continue;
            if (novo.valor > item.valor + 1e-9) {   // mais uma ocorrencia feita: substitui e zera o contador de erro
                novo.tentativas = 0;
                novo.ultimoHttp = 0;
                item = std::move(novo);
            }
            return;   // mesmo valor (ou menor): nada a fazer, nunca duplica
        }
        fila.push_back(std::move(novo));
        while (fila.size() > kMaxFila) fila.erase(fila.begin());
    }

    void aplicarEnvio(std::vector<ItemFila>& fila, const ItemFila& enviado, int http) {
        for (auto it = fila.begin(); it != fila.end(); ++it) {
            if (it->id != enviado.id || it->stamp != enviado.stamp) continue;
            if (http == 200 || http == 201) {
                if (it->valor <= enviado.valor + 1e-9) {
                    fila.erase(it);
                } else {
                    it->tentativas = 0;   // entrou valor maior depois do envio: continua pendente
                    it->ultimoHttp = http;
                }
            } else {
                if (http > 0) ++it->tentativas;
                it->ultimoHttp = http;
            }
            return;
        }
    }

    // ------------------------------------------------------------------ check-ins remotos
    bool lerCheckins(std::string_view json, ArquivoCheckins& saida) {
        if (json.empty() || json.size() > kMaxBytesCheckins) return false;
        ArquivoCheckins lido;
        if (glz::read<kLeitura>(lido, json)) return false;
        saida = std::move(lido);
        return true;
    }

    std::vector<ResultadoOcorrencia> aplicarCheckinsRemotos(Estado& e, const std::vector<CheckinRemoto>& checkins,
                                                            int fusoMin) {
        std::vector<ResultadoOcorrencia> resultados;
        const int hoje = stampDe(e.dia.data);
        std::vector<std::string> vistos;
        for (const CheckinRemoto& c : checkins) {
            if (c.stamp != hoje || c.value <= 0.0) continue;
            if (std::find(vistos.begin(), vistos.end(), c.habitId) != vistos.end()) continue;   // 1 por habito
            vistos.push_back(c.habitId);
            // maior valor entre os check-ins do mesmo habito e dia
            const CheckinRemoto* melhor = &c;
            for (const CheckinRemoto& o : checkins)
                if (o.habitId == c.habitId && o.stamp == hoje && o.value > melhor->value) melhor = &o;

            int total = 0;
            for (const OcorrenciaDia& oc : e.dia.ocorrencias)
                if (oc.habitoId == c.habitId) ++total;
            if (total == 0) continue;
            const int alvo = std::min(total, static_cast<int>(std::lround(melhor->value)));
            int faltam = alvo - ocorrenciasFeitas(e, c.habitId);

            int minutoCheckin = -1;   // so vale se o instante cai no dia de hoje (check-in retroativo nao tem hora util)
            if (const auto m = interpretarTempo(melhor->time, fusoMin); m && m->data == e.dia.data) minutoCheckin = m->minuto;

            bool primeira = true;
            while (faltam-- > 0) {
                std::optional<size_t> escolhida;
                int melhorDist = 0;
                for (size_t i = 0; i < e.dia.ocorrencias.size(); ++i) {
                    const OcorrenciaDia& oc = e.dia.ocorrencias[i];
                    if (oc.habitoId != c.habitId || oc.feita || oc.status != StatusHabito::Pendente) continue;
                    if (primeira && minutoCheckin >= 0) {
                        // a mais proxima da hora do check-in; "qualquer hora" so se nao ha outra
                        const int dist = oc.minuto < 0 ? 100000 : std::abs(oc.minuto - minutoCheckin);
                        if (!escolhida || dist < melhorDist) {
                            escolhida = i;
                            melhorDist = dist;
                        }
                    } else if (!escolhida) {
                        escolhida = i;
                    }
                }
                if (!escolhida) break;
                const OcorrenciaDia& oc = e.dia.ocorrencias[*escolhida];
                int real = oc.minuto >= 0 ? oc.minuto : e.dia.minutoAtual;
                if (primeira && minutoCheckin >= 0) real = minutoCheckin;
                primeira = false;
                const ResultadoOcorrencia r = registrarOcorrencia(e, *escolhida, real, true);
                if (r.registrado) resultados.push_back(r);
            }
        }
        return resultados;
    }

    // ------------------------------------------------------------------ marcar na placa
    ItemFila itemFilaDoHabito(const Estado& e, std::string_view habitoId) {
        ItemFila item;
        item.id = std::string{habitoId};
        item.stamp = stampDe(e.dia.data);
        item.valor = ocorrenciasFeitas(e, habitoId);
        int total = 1;
        for (const OcorrenciaDia& oc : e.dia.ocorrencias)
            if (oc.habitoId == habitoId) total = oc.total;
        item.meta = total;
        item.time = tempoCheckinUtc(e.dia.data, e.dia.minutoAtual, param::kFusoMinutos);
        return item;
    }

    ResultadoOcorrencia marcarFeita(Estado& e, size_t indice, std::vector<ItemFila>& fila) {
        const ResultadoOcorrencia r = registrarOcorrencia(e, indice, e.dia.minutoAtual, false);
        if (r.registrado) enfileirar(fila, itemFilaDoHabito(e, e.dia.ocorrencias[indice].habitoId));
        return r;
    }

    ResultadoOcorrencia marcarMeditacaoPorTimer(Estado& e, std::vector<ItemFila>& fila) {
        const auto idx = proximaPendenteDoTipo(e, TipoHabito::Meditacao);
        if (!idx) return {};
        const OcorrenciaDia& oc = e.dia.ocorrencias[*idx];
        const int noHorario = oc.minuto >= 0 ? oc.minuto : e.dia.minutoAtual;   // independente da hora real
        const ResultadoOcorrencia r = registrarOcorrencia(e, *idx, noHorario, false);
        if (r.registrado) enfileirar(fila, itemFilaDoHabito(e, e.dia.ocorrencias[*idx].habitoId));
        return r;
    }

    EventoLog eventoHabito(const Estado& e, const ResultadoOcorrencia& r, std::string_view origem) {
        EventoLog ev;
        if (r.indice >= e.dia.ocorrencias.size()) return ev;
        const OcorrenciaDia& oc = e.dia.ocorrencias[r.indice];
        ev.data = textoData(e.dia.data);
        ev.hora = horaTexto(e.dia.minutoAtual);
        ev.tipo = "habito";
        ev.projeto = oc.nome;
        ev.projetoId = oc.habitoId;
        ev.categoria = nomeTipo(oc.tipo);
        ev.resultado = r.status == StatusHabito::NoHorario ? "no_horario"
                     : r.status == StatusHabito::Atrasado  ? "atrasado"
                     : r.status == StatusHabito::Pulado    ? "pulado" : "esquecido";
        ev.planejadoMin = oc.minuto;
        ev.cr = r.cr;
        ev.xp = r.xp;
        ev.origem = std::string{origem};
        return ev;
    }

} // namespace tempo
