#include "tempo/Perfil.h"

#include <glaze/json.hpp>

#include <algorithm>
#include <cstdio>

namespace tempo {
    namespace {
        constexpr glz::opts kLeitura{.error_on_unknown_keys = false};

        bool livre(const EventoLog& e) { return e.projetoId.empty(); }

        bool vitoria(const EventoLog& e) { return e.resultado == "vitoria"; }

        bool perfeita(const EventoLog& e) {
            return e.perfeita || (vitoria(e) && e.distracoes == 0 && e.distraidoMin == 0);
        }

        DiaResumo& diaDoResumo(Resumo& r, int64_t serial) {
            auto it = std::lower_bound(r.dias.begin(), r.dias.end(), serial,
                                       [](const DiaResumo& d, int64_t s) { return d.dia < s; });
            if (it == r.dias.end() || it->dia != serial) {
                DiaResumo novo;
                novo.dia = serial;
                it = r.dias.insert(it, novo);
            }
            return *it;
        }
    } // namespace

    bool lerDataTexto(std::string_view texto, Data& saida) {
        if (texto.size() != 10 || texto[4] != '-' || texto[7] != '-') return false;
        int v[3] = {0, 0, 0};
        const int pos[3][2] = {{0, 4}, {5, 2}, {8, 2}};
        for (int i = 0; i < 3; ++i)
            for (int k = 0; k < pos[i][1]; ++k) {
                const char c = texto[pos[i][0] + k];
                if (c < '0' || c > '9') return false;
                v[i] = v[i] * 10 + (c - '0');
            }
        if (v[0] < 1970 || v[1] < 1 || v[1] > 12 || v[2] < 1 || v[2] > 31) return false;
        saida = Data{v[0], v[1], v[2]};
        return true;
    }

    int minutoDeHora(std::string_view hora) {
        if (hora.size() < 5 || hora[2] != ':') return -1;
        for (int i : {0, 1, 3, 4})
            if (hora[i] < '0' || hora[i] > '9') return -1;
        const int h = (hora[0] - '0') * 10 + (hora[1] - '0');
        const int m = (hora[3] - '0') * 10 + (hora[4] - '0');
        return h > 23 || m > 59 ? -1 : h * 60 + m;
    }

    void registrarEvento(Resumo& r, const EventoLog& e, int streak) {
        if (e.tipo != "batalha") return;
        const int foco = std::max(0, e.focadoMin);
        ++r.batalhas;
        if (vitoria(e)) ++r.vitorias;
        r.focoTotalMin += foco;
        r.maxStreak = std::max(r.maxStreak, streak);

        if (livre(e)) {
            r.focoLivreMin += foco;
        } else {
            const std::string nome = e.projeto.empty() ? std::string{"Projeto"} : e.projeto;
            auto it = std::find_if(r.projetos.begin(), r.projetos.end(), [&](const ProjetoResumo& p) { return p.nome == nome; });
            if (it == r.projetos.end()) {
                if (r.projetos.size() >= kMaxProjetosResumo) {   // abre vaga: descarta o de menos horas
                    r.projetos.erase(std::min_element(r.projetos.begin(), r.projetos.end(),
                                                      [](const ProjetoResumo& a, const ProjetoResumo& b) { return a.minutos < b.minutos; }));
                }
                r.projetos.push_back({nome, 0, 0});
                it = r.projetos.end() - 1;
            }
            it->minutos += foco;
            if (vitoria(e)) ++it->vitorias;
        }

        const int fim = minutoDeHora(e.hora);
        bool madrugada = false;
        if (fim >= 0) {
            for (int k = 1; k <= foco; ++k) ++r.minutosPorHora[static_cast<size_t>((((fim - k) % 1440) + 1440) % 1440 / 60)];
            madrugada = foco > 0 && fim - (foco + std::max(0, e.distraidoMin)) < kMadrugadaMin;
        }

        Data data;
        if (lerDataTexto(e.data, data)) {
            auto& dia = diaDoResumo(r, diaSerial(data));
            dia.focoMin += foco;
            if (e.categoria == "Meditação") dia.meditacaoMin += foco;
            ++dia.batalhas;
            if (perfeita(e)) ++dia.perfeitas;
            dia.madrugada = dia.madrugada || madrugada;
            if (r.dias.size() > kMaxDiasResumo) r.dias.erase(r.dias.begin(), r.dias.end() - kMaxDiasResumo);
        }
    }

    Resumo reconstruirResumo(const std::vector<EventoLog>& eventos) {
        Resumo r;
        for (const auto& e : eventos) registrarEvento(r, e, 0);
        return r;
    }

    std::string serializarResumo(const Resumo& resumo) {
        std::string saida;
        if (glz::write_json(resumo, saida)) return {};
        return saida;
    }

    bool lerResumo(std::string_view json, Resumo& saida) {
        if (json.empty() || json.size() > kMaxBytesResumo) return false;
        Resumo lido;
        if (glz::read<kLeitura>(lido, json)) return false;
        saida = std::move(lido);
        return true;
    }

    int chipDoEvento(const EventoLog& e) {
        for (int i = 0; i < int(kNumChips); ++i)
            if (e.categoria == nomeCategoria(categoriaDoChip(i))) return i;
        return e.projetoId.empty() ? 0 : chipDaCategoria(Categoria::Projetos);   // logs antigos sem categoria
    }

    void somarSemana(Semana& s, const EventoLog& e, int64_t segunda) {
        if (e.tipo != "batalha") return;
        Data data;
        if (!lerDataTexto(e.data, data)) return;
        const int64_t indice = diaSerial(data) - segunda;
        if (indice < 0 || indice > 6) return;
        const int chip = chipDoEvento(e);
        const int foco = std::max(0, e.focadoMin);
        s.min[static_cast<size_t>(indice)][static_cast<size_t>(chip)] += foco;
        s.porCategoria[static_cast<size_t>(chip)] += foco;
        s.total += foco;
        if (e.distraidoMin > 2) s.perdidoMin += e.distraidoMin;
        int dia = 0;
        for (int v : s.min[static_cast<size_t>(indice)]) dia += v;
        s.maiorDia = std::max(s.maiorDia, dia);
    }

    int faixaMaisFocada(const Resumo& r) {
        int melhor = -1, soma = 0;
        for (int h = 0; h < 23; ++h) {
            const int s = r.minutosPorHora[static_cast<size_t>(h)] + r.minutosPorHora[static_cast<size_t>(h + 1)];
            if (s > soma) { soma = s; melhor = h; }
        }
        return melhor;
    }

    int mediaFocoPorDia(const Resumo& r) {
        int soma = 0, dias = 0;
        for (const auto& d : r.dias)
            if (d.focoMin > 0) { soma += d.focoMin; ++dias; }
        return dias > 0 ? soma / dias : 0;
    }

    const char* tituloDoNivel(int nivel) {
        if (nivel >= 30) return "Lenda do Foco";
        if (nivel >= 20) return "Campeão do Foco";
        if (nivel >= 15) return "Mestre do Foco";
        if (nivel >= 10) return "Veterano do Foco";
        if (nivel >= 5) return "Guerreiro do Foco";
        if (nivel >= 3) return "Aprendiz do Foco";
        return "Recruta";
    }

    std::array<Conquista, kNumConquistas> avaliarConquistas(const Resumo& r, int streakAtual) {
        int maiorDia = 0, maiorDiaPerfeito = 0, madrugadas = 0, sequenciaMonge = 0, atualMonge = 0;
        int64_t anterior = -100;
        for (const auto& d : r.dias) {
            maiorDia = std::max(maiorDia, d.focoMin);
            if (d.batalhas > 0 && d.perfeitas == d.batalhas) maiorDiaPerfeito = std::max(maiorDiaPerfeito, d.focoMin);
            if (d.madrugada) ++madrugadas;
            if (d.meditacaoMin > 0) {
                atualMonge = d.dia == anterior + 1 ? atualMonge + 1 : 1;
                sequenciaMonge = std::max(sequenciaMonge, atualMonge);
                anterior = d.dia;
            } else {
                atualMonge = 0;
            }
        }
        const int streak = std::max(r.maxStreak, streakAtual);
        const auto c = [](const char* nome, const char* desc, const char* un, int atual, int meta, uint8_t cor) {
            Conquista k{nome, desc, un};
            k.meta = meta;
            k.atual = std::clamp(atual, 0, meta);
            k.ganha = atual >= meta;
            k.cor = cor;
            return k;
        };
        return {{
            c("Primeira vitória", "derrote 1 inimigo", "vitórias", r.vitorias, 1, 0),
            c("10 vitórias", "10 batalhas vencidas", "vitórias", r.vitorias, 10, 0),
            c("100 vitórias", "100 batalhas vencidas", "vitórias", r.vitorias, 100, 1),
            c("Maratona", "4 h de foco num dia", "min no dia", maiorDia, 240, 0),
            c("Streak 7", "7 dias seguidos", "dias de streak", streak, 7, 2),
            c("Monge", "meditar 7 dias seguidos", "dias de meditação", sequenciaMonge, 7, 4),
            c("Madrugador", "foco antes das 7h, 5 dias", "dias antes das 7h", madrugadas, 5, 1),
            c("Dia perfeito", "só batalhas perfeitas, 2 h", "min num dia perfeito", maiorDiaPerfeito, 120, 5),
        }};
    }

    int conquistasGanhas(const std::array<Conquista, kNumConquistas>& lista) {
        return static_cast<int>(std::count_if(lista.begin(), lista.end(), [](const Conquista& c) { return c.ganha; }));
    }

    int proximaConquista(const std::array<Conquista, kNumConquistas>& lista) {
        int melhor = -1;
        double razao = -1.0;
        for (size_t i = 0; i < lista.size(); ++i) {
            if (lista[i].ganha) continue;
            const double r = double(lista[i].atual) / double(lista[i].meta);
            if (r > razao) { razao = r; melhor = static_cast<int>(i); }
        }
        return melhor;
    }

} // namespace tempo
