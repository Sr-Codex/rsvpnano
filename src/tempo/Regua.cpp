#include "tempo/Regua.h"

#include <algorithm>
#include <cmath>

namespace tempo::regua {

    double posicaoDaParada(int minutos) { return minutos; }

    double xDaParada(int minutos, double centroMin, double cx) {
        return cx + (posicaoDaParada(minutos) - centroMin) * kPxPorMin;
    }

    double centroDeX(int minutos, double x, double cx) { return posicaoDaParada(minutos) - (x - cx) / kPxPorMin; }

    double limitar(double minutos) { return std::clamp(minutos, double(kMinMin), double(kMaxMin)); }

    int encaixar(double minutos) {
        const int passo = static_cast<int>(std::lround(limitar(minutos) / kPassoMin)) * kPassoMin;
        return std::clamp(passo, kMinMin, kMaxMin);
    }

    int limitarEncaixado(int minutos) { return encaixar(double(minutos)); }

    float opacidadePonta(float x, float xIni, float xFim, float larg) {
        if (larg <= 0) return (x >= xIni && x <= xFim) ? 1.0f : 0.0f;
        return std::clamp(std::min((x - xIni) / larg, (xFim - x) / larg), 0.0f, 1.0f);
    }

    bool metadeDireita(int x, int areaX, int areaW) { return x >= areaX + areaW / 2; }

    int ajustarPorToque(int atual, bool direita) {
        return std::clamp(limitarEncaixado(atual) + (direita ? kPassoMin : -kPassoMin), kMinMin, kMaxMin);
    }

    void Arrasto::comecar(int x, int minutoAtual) {
        ativo = true;
        moveu = false;
        xRef = x;
        minutoRef = minutoAtual;
        minuto = minutoAtual;
    }

    bool Arrasto::mover(int x) {
        if (!ativo) return false;
        if (!moveu) {
            if (std::abs(x - xRef) < kZonaMortaPx) return false;
            moveu = true;
            xRef = x;   // referencia nova: sem salto
            return false;
        }
        const double bruto = minutoRef - (x - xRef) / kPxPorMin;
        const double novo = limitar(bruto);
        if (novo != bruto) {   // bateu no limite: o dedo "passa" e a referencia acompanha (voltar o dedo volta a fita ja)
            xRef = x;
            minutoRef = novo;
        }
        const bool mudou = std::fabs(novo - minuto) > 1e-9;
        minuto = novo;
        return mudou;
    }

    int Arrasto::soltar() {
        const int v = moveu ? encaixar(minuto) : encaixar(minutoRef);
        ativo = false;
        moveu = false;
        return v;
    }

} // namespace tempo::regua
