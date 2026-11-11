// Previa no Mac dos sons do Lume: gera os 12 sons pelo MESMO sintetizador C++ que roda na placa (src/som/Sintese.*)
// e grava WAV mono 16 kHz. Uso (a partir da raiz do fork):
//   g++ -std=gnu++23 -O1 -Isrc tools/som_preview.cpp src/som/Sintese.cpp -o /tmp/som_preview
//   /tmp/som_preview ../docs/app-tempo/sons-preview [volume: baixo|medio|alto]
// Depois: python3 tools/som_comparar.py ../sons ../docs/app-tempo/sons-preview
#include <cstdio>
#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

#include "som/Sintese.h"

namespace {
    struct Arquivo { som::SomId id; const char* nome; };
    // Mesmos nomes dos WAV de referencia do usuario (sons/*.wav).
    constexpr Arquivo kArquivos[] = {
        {som::SomId::SobeTres, "1-sobe-3-notas"},   {som::SomId::Moeda, "2-moeda"},
        {som::SomId::SobeQuatro, "3-sobe-4-notas"}, {som::SomId::DoisBipes, "4-dois-bipes"},
        {som::SomId::DesceAlerta, "5-desce-alerta"}, {som::SomId::Bip, "6-bip-unico"},
        {som::SomId::Vitoria, "7-vitoria"},         {som::SomId::FaseCompleta, "8-fase-completa"},
        {som::SomId::PowerUp, "9-power-up"},        {som::SomId::Jingle, "10-jingle"},
        {som::SomId::AlertaLongo, "11-alerta-longo"}, {som::SomId::SuaveDescendo, "12-suave-descendo"},
    };

    void u32(std::FILE* f, uint32_t v) { std::fwrite(&v, 4, 1, f); }
    void u16(std::FILE* f, uint16_t v) { std::fwrite(&v, 2, 1, f); }

    bool gravarWav(const std::string& caminho, const std::vector<int16_t>& pcm) {
        std::FILE* f = std::fopen(caminho.c_str(), "wb");
        if (!f) return false;
        const uint32_t bytes = uint32_t(pcm.size() * 2);
        std::fwrite("RIFF", 1, 4, f); u32(f, 36 + bytes); std::fwrite("WAVEfmt ", 1, 8, f);
        u32(f, 16); u16(f, 1); u16(f, 1); u32(f, som::kTaxaHz); u32(f, som::kTaxaHz * 2); u16(f, 2); u16(f, 16);
        std::fwrite("data", 1, 4, f); u32(f, bytes); std::fwrite(pcm.data(), 2, pcm.size(), f);
        std::fclose(f);
        return true;
    }
}

int main(int argc, char** argv) {
    if (argc < 2) { std::fprintf(stderr, "uso: %s <pasta-de-saida> [baixo|medio|alto]\n", argv[0]); return 2; }
    som::Volume volume = som::Volume::Medio;   // Medio = ganho 1,0 = nivel exato dos WAV de referencia
    if (argc > 2 && !std::strcmp(argv[2], "baixo")) volume = som::Volume::Baixo;
    if (argc > 2 && !std::strcmp(argv[2], "alto")) volume = som::Volume::Alto;
    for (const Arquivo& a : kArquivos) {
        som::Sintetizador s;
        s.iniciar(a.id, som::ganhoDoVolume(volume));
        std::vector<int16_t> pcm, bloco(256);   // em blocos, como a tarefa de audio da placa
        for (;;) {
            const size_t n = s.gerar(bloco.data(), bloco.size());
            if (n == 0) break;
            pcm.insert(pcm.end(), bloco.begin(), bloco.begin() + n);
        }
        const std::string caminho = std::string(argv[1]) + "/" + a.nome + ".wav";
        if (!gravarWav(caminho, pcm)) { std::fprintf(stderr, "falhou: %s\n", caminho.c_str()); return 1; }
        std::printf("%s  %s  %zu amostras (%.0f ms)\n", caminho.c_str(), som::nome(a.id), pcm.size(), pcm.size() * 1000.0 / som::kTaxaHz);
    }
    return 0;
}
