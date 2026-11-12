#include "tempo/Sessao.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#include "tempo/Parametros.h"
#include "tempo/Regua.h"

namespace tempo {

    int pausaDoCiclo(int rodadaMin) {
        const int arred = static_cast<int>(std::lround(rodadaMin / 25.0)) * 5;   // rodada/5 arredondada a 5
        return std::clamp(arred, kCicloPausaMin, 15);
    }

    int duracaoTotalMin(int rodadaMin, bool ciclo4x) {
        if (!ciclo4x || !ciclo4xPermitido(rodadaMin)) return rodadaMin;
        return kCicloRodadas * rodadaMin + (kCicloRodadas - 1) * pausaDoCiclo(rodadaMin);
    }

    Ciclo iniciarCiclo(int focoMin) {
        Ciclo c;
        c.ativo = true;
        c.focoMin = focoMin;
        c.pausaMin = pausaDoCiclo(focoMin);
        return c;
    }

    Ciclo avancarCiclo(const Ciclo& c) {
        if (!c.ativo) return c;
        Ciclo n = c;
        switch (c.passo) {
        case PassoCiclo::Foco:
            n.passo = c.rodada < kCicloRodadas ? PassoCiclo::PausaCurta : PassoCiclo::PausaLonga;
            break;
        case PassoCiclo::PausaCurta:
            n.passo = PassoCiclo::Foco;
            n.rodada = c.rodada + 1;
            break;
        case PassoCiclo::PausaLonga:
            n.ativo = false;
            break;
        }
        return n;
    }

    int duracaoCicloMin(const Ciclo& c) {
        switch (c.passo) {
        case PassoCiclo::PausaCurta: return c.pausaMin;
        case PassoCiclo::PausaLonga: return kCicloPausaLongaMin;
        default: return c.focoMin;
        }
    }

    namespace {
        constexpr Categoria kChips[kNumChips] = {Categoria::Trabalho, Categoria::Estudo, Categoria::Leitura,
                                                 Categoria::Meditacao, Categoria::Projetos};
    }

    Categoria categoriaDoChip(int chip) {
        return kChips[(chip >= 0 && chip < int(kNumChips)) ? chip : 0];
    }

    int chipDaCategoria(Categoria c) {
        for (int i = 0; i < int(kNumChips); ++i)
            if (kChips[i] == c) return i;
        return 0;
    }

    const char* nomeCategoria(Categoria c) {
        switch (c) {
        case Categoria::Trabalho: return "Trabalho";
        case Categoria::Estudo: return "Estudo";
        case Categoria::Leitura: return "Leitura";
        case Categoria::Meditacao: return "Meditação";
        default: return "Projetos";
        }
    }

    std::string formatarRelogio(long long totalSeg) {
        totalSeg = std::max<long long>(0, totalSeg);
        char t[24];
        if (totalSeg >= 3600)
            std::snprintf(t, sizeof t, "%d:%02d:%02d", int(totalSeg / 3600), int(totalSeg / 60 % 60), int(totalSeg % 60));
        else
            std::snprintf(t, sizeof t, "%02d:%02d", int(totalSeg / 60), int(totalSeg % 60));
        return t;
    }

    std::string nomeFocoLivre(Categoria c) {
        return std::string{nomeCategoria(c)} + " · sem projeto";
    }

    std::string linhaTaxaDoFoco(bool temProjeto, Categoria livre) {
        const Categoria cat = categoriaDoFoco(temProjeto, livre);
        return std::string{temProjeto ? "Projeto" : "sem projeto"} + " · " + textoTaxa(cat);
    }

    std::string textoComecar(int rodadaMin, bool ciclo4x, bool horaValida, int minutoDoDia) {
        if (regua::ehLivre(rodadaMin)) return "livre · sem meta";   // sem hora de fim: conta ate o Concluir
        ciclo4x = ciclo4x && ciclo4xPermitido(rodadaMin);
        std::string t = ciclo4x ? "4 × " + std::to_string(rodadaMin) : regua::textoDuracao(rodadaMin);
        if (horaValida) {
            const int fim = (((minutoDoDia + duracaoTotalMin(rodadaMin, ciclo4x)) % 1440) + 1440) % 1440;
            char hora[16];
            std::snprintf(hora, sizeof hora, " · até %02d:%02d", fim / 60, fim % 60);
            t += hora;
        }
        return t;
    }

    Categoria categoriaDoFoco(bool temProjeto, Categoria livre) {
        return temProjeto ? Categoria::Projetos : categoriaLivreValida(static_cast<int>(livre));
    }

    Categoria categoriaLivreValida(int bruto) {
        if (bruto < 0 || bruto >= int(kNumCategorias)) return Categoria::Trabalho;
        const auto c = static_cast<Categoria>(bruto);
        return c == Categoria::Projetos ? Categoria::Trabalho : c;
    }

    double crPorMinuto(Categoria c) { return param::kTaxas[static_cast<size_t>(c)].crPorMin; }

    std::string textoTaxa(Categoria c) {
        char t[24];
        std::snprintf(t, sizeof t, "%.2f cr/min", crPorMinuto(c));
        for (char* p = t; *p; ++p)
            if (*p == '.') *p = ',';
        return t;
    }

    Categoria categoriaSemProjetoValida(int bruto) {
        const Categoria c = categoriaLivreValida(bruto);
        return c == Categoria::Leitura ? Categoria::Trabalho : c;
    }

    std::string textoTaxaCurta(Categoria c) {
        std::string t = textoTaxa(c);
        const auto espaco = t.find(' ');
        return espaco == std::string::npos ? t : t.substr(0, espaco);
    }

    ResolucaoProjeto resolverProjeto(const std::string& id, const std::vector<std::string>& ids, bool listaLida) {
        ResolucaoProjeto r;
        if (id.empty() || !listaLida) return r;
        const auto it = std::find(ids.begin(), ids.end(), id);
        if (it == ids.end()) r.saiu = true;
        else r.indice = int(it - ids.begin());
        return r;
    }

    size_t paginasDoSeletor(size_t numProjetos) {
        return numProjetos <= kProjetosPorPagina ? 1 : (numProjetos + kProjetosPorPagina - 1) / kProjetosPorPagina;
    }

    std::vector<int> projetosDaPagina(size_t pagina, size_t numProjetos) {
        std::vector<int> saida;
        for (size_t i = pagina * kProjetosPorPagina; i < numProjetos && i < (pagina + 1) * kProjetosPorPagina; ++i) saida.push_back(int(i));
        return saida;
    }

} // namespace tempo
