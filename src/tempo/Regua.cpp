#include "tempo/Regua.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace tempo::regua {

    namespace {
        constexpr int kCurtas = (kTrocaMarchaMin - kMinMin) / kPassoCurtoMin + 1;                       // 12
        constexpr int kLongas = (kMaxMin - kPrimeiraLongaMin) / kPassoLongoMin + 1;                     // 16
        constexpr int kTotal = kCurtas + kLongas;
    }

    int numParadas() { return kTotal; }

    int minutoDaParada(int i) {
        i = std::clamp(i, 0, kTotal - 1);
        return i < kCurtas ? kMinMin + i * kPassoCurtoMin : kPrimeiraLongaMin + (i - kCurtas) * kPassoLongoMin;
    }

    int indiceDaParada(int minutos) {
        if (ehLivre(minutos)) return 0;
        int melhor = 0, dist = 1 << 30;
        for (int i = 0; i < kTotal; ++i) {
            const int d = std::abs(minutoDaParada(i) - minutos);
            if (d < dist) { dist = d; melhor = i; }
        }
        return melhor;
    }

    double posicaoDaParada(int minutos) { return ehLivre(minutos) ? kPosInf : double(indiceDaParada(minutos)); }

    double xDoIndice(int indice, double centroPos, double cx) { return cx + (indice - centroPos) * kPxPorParada; }

    double xDaParada(int minutos, double centroPos, double cx) {
        return cx + (posicaoDaParada(minutos) - centroPos) * kPxPorParada;
    }

    int duracaoDoTimer(int ultimaDaRegua, int ultimaDeTimer) {
        return ehLivre(ultimaDaRegua) ? limitarEncaixado(std::max(ultimaDeTimer, kMinMin)) : limitarEncaixado(ultimaDaRegua);
    }

    double xDoInf(double centroPos) { return kAgulhaX + (kPosInf - centroPos) * kPxPorParada; }
    bool infFixo(double centroPos) { return xDoInf(centroPos) < kInfXMinVisivel; }
    bool toqueNoInf(int x, double centroPos) {
        if (infFixo(centroPos)) return x >= kInfAreaX0 && x <= kInfAreaX1;
        return std::fabs(x - xDoInf(centroPos)) <= 22.0;
    }

    double centroDeX(int minutos, double x, double cx) { return posicaoDaParada(minutos) - (x - cx) / kPxPorParada; }

    double limitar(double pos) { return std::clamp(pos, kPosInf, double(kTotal - 1)); }

    int encaixar(double pos) {
        const double p = limitar(pos);
        if (p < kPosInf / 2.0) return kLivre;   // mais perto do ∞ que do 5
        return minutoDaParada(int(std::lround(std::max(p, 0.0))));
    }

    int limitarEncaixado(int minutos) { return ehLivre(minutos) ? kLivre : minutoDaParada(indiceDaParada(minutos)); }

    float opacidadePonta(float x, float xIni, float xFim, float larg) {
        if (larg <= 0) return (x >= xIni && x <= xFim) ? 1.0f : 0.0f;
        return std::clamp(std::min((x - xIni) / larg, (xFim - x) / larg), 0.0f, 1.0f);
    }

    bool metadeDireita(int x, int areaX, int areaW) { return x >= areaX + areaW / 2; }

    int ajustarPorToque(int atual, bool direita) {
        if (ehLivre(atual)) return direita ? minutoDaParada(0) : kLivre;
        return minutoDaParada(indiceDaParada(atual) + (direita ? 1 : -1));
    }

    int passoDoLado(int atual, bool direita) {
        if (ehLivre(atual)) return direita ? kPassoCurtoMin : 0;
        const int i = indiceDaParada(atual);
        const int j = i + (direita ? 1 : -1);
        if (j < 0 || j >= kTotal) return 0;
        return std::abs(minutoDaParada(j) - minutoDaParada(i));
    }

    bool paradaMaior(int m) { return m <= kTrocaMarchaMin ? m % 10 == 0 : m % 30 == 0; }
    bool paradaHoraCheia(int m) { return m > kTrocaMarchaMin && m % 60 == 0; }

    std::string rotuloDaParada(int m) {
        if (ehLivre(m)) return "∞";
        if (m <= kTrocaMarchaMin) return std::to_string(m);
        const int h = m / 60, r = m % 60;
        if (r == 0) return std::to_string(h) + " h";
        char t[16];
        std::snprintf(t, sizeof t, "%dh%02d", h, r);
        return t;
    }

    std::string textoDuracao(int m) {
        if (ehLivre(m)) return "livre";
        if (m <= kTrocaMarchaMin) return std::to_string(m) + " min";
        const int h = m / 60, r = m % 60;
        if (r == 0) return std::to_string(h) + " h";
        char t[16];
        std::snprintf(t, sizeof t, "%dh%02d", h, r);
        return t;
    }

    std::vector<ParteValor> partesDoValor(int m) {
        if (ehLivre(m)) return {{"livre", false}};
        if (m <= kTrocaMarchaMin) return {{std::to_string(m), true}, {"min", false}};
        const int h = m / 60, r = m % 60;
        std::vector<ParteValor> p{{std::to_string(h), true}, {"h", false}};
        if (r != 0) {
            char t[8];
            std::snprintf(t, sizeof t, "%02d", r);
            p.push_back({t, true});
        }
        return p;
    }

    void Arrasto::comecar(int x, int minutoAtual) {
        ativo = true;
        moveu = false;
        xRef = x;
        posRef = posicaoDaParada(minutoAtual);
        pos = posRef;
    }

    bool Arrasto::mover(int x) {
        if (!ativo) return false;
        if (!moveu) {
            if (std::abs(x - xRef) < kZonaMortaPx) return false;
            moveu = true;
            xRef = x;   // referencia nova: sem salto
            return false;
        }
        const double bruto = posRef - (x - xRef) / kPxPorParada;
        const double novo = limitar(bruto);
        if (novo != bruto) {   // bateu no limite: a referencia acompanha (voltar o dedo volta a fita ja)
            xRef = x;
            posRef = novo;
        }
        const bool mudou = std::fabs(novo - pos) > 1e-9;
        pos = novo;
        return mudou;
    }

    int Arrasto::soltar() {
        const int v = moveu ? encaixar(pos) : encaixar(posRef);
        ativo = false;
        moveu = false;
        return v;
    }

} // namespace tempo::regua
