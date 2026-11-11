#pragma once

// Sons do Lume gerados NA PLACA a partir das notas (docs/app-tempo/sons.md). C++ puro (sem Arduino/FreeRTOS):
// testavel no host. Porta fiel do gerador de referencia do usuario (sons/gerador/chip.py e chip2.py): onda quadrada
// com duty, envelope de ataque linear + decaimento (1 - i/n)^decay, vibrato de 6 Hz, glissando e pausas.
// Saida: PCM mono int16 a 16 kHz (taxa do ES8311 no firmware); o chamador duplica para estereo.
// Os 12 sons sao tabelas constexpr de notas: mudar um som = mudar uma linha.

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace som {

    inline constexpr uint32_t kTaxaHz = 16000;

    enum class SomId : uint8_t {
        Bip,
        SobeTres,
        Moeda,
        SobeQuatro,
        DoisBipes,
        DesceAlerta,
        Vitoria,
        FaseCompleta,
        PowerUp,
        Jingle,
        AlertaLongo,
        SuaveDescendo,
        Count,
    };
    inline constexpr size_t kNumSons = static_cast<size_t>(SomId::Count);

    // Duas "geracoes" do gerador de referencia (diferem em volume, ataque e ordem fase/amostra).
    enum class Estilo : uint8_t {
        A,   // chip.py : vol 0,25; ataque 50 amostras @44,1 kHz; fase = t*freq; envelope de fim 1 - (i/n)^2
        B,   // chip2.py: vol 0,22; ataque 60 amostras @44,1 kHz; fase acumulada; envelope (1 - i/n)^decay
    };

    // hz == 0 -> pausa. ms inteiro: n = ms * 16 amostras exatas (a referencia faz int(SR*dur)).
    struct Nota {
        float hz = 0.0f;
        uint16_t ms = 0;
        float duty = 0.5f;
        float decay = 2.0f;      // estilo B: expoente do decaimento (0 = nenhum); o estilo A ignora
        float vib = 0.0f;        // profundidade relativa do vibrato (6 Hz)
        float slideHz = 0.0f;    // != 0: glissando linear de hz ate slideHz
    };

    struct Som {
        const char* nome;
        Estilo estilo;
        std::span<const Nota> notas;
    };

    const Som& definicao(SomId id);
    const char* nome(SomId id);
    // Total de amostras mono (16 kHz) do som, sem silencio de cauda.
    uint32_t totalAmostras(SomId id);
    uint32_t duracaoMs(SomId id);

    // Ganho linear aplicado sobre o nivel de referencia (Medio = 1,0 = exatamente os WAV do usuario).
    enum class Volume : uint8_t { Baixo, Medio, Alto, Count };
    float ganhoDoVolume(Volume v);

    // Sintetizador em blocos (nao aloca o som inteiro): iniciar() e chamar gerar() ate devolver 0.
    class Sintetizador {
    public:
        void iniciar(SomId id, float ganho);
        // Escreve ate `max` amostras MONO em `saida`; devolve quantas escreveu (0 = acabou).
        size_t gerar(int16_t* saida, size_t max);
        bool terminou() const { return terminou_; }
        SomId id() const { return id_; }

    private:
        void abrirNota();

        SomId id_ = SomId::Bip;
        const Som* som_ = nullptr;
        float ganho_ = 1.0f;
        size_t nota_ = 0;
        uint32_t i_ = 0;          // amostra dentro da nota
        uint32_t n_ = 0;          // amostras da nota
        uint32_t ataque_ = 0;
        double fase_ = 0.0;
        bool terminou_ = true;
    };

} // namespace som
