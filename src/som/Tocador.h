#pragma once

// Tarefa de audio (FreeRTOS): recebe pedidos de som por uma fila e sintetiza em blocos pequenos (nunca aloca o som
// inteiro), para a tela NAO travar durante um som de ate ~1,2 s. A decisao do que toca (prioridade) e do
// som::Arbitro; o hardware (ES8311/I2S ou o simulador) entra por `Saida`. Compartilhado por todas as placas/simulador.

#include <atomic>
#include <cstddef>
#include <cstdint>

#include "som/Regras.h"

namespace som {

    struct Saida {
        // Chamado na thread de QUEM PEDE, so quando nada esta tocando (liga a rampa de audio e o codec, I2C): assim a
        // tarefa de audio nunca mexe no barramento I2C compartilhado. false = sem audio (o pedido e descartado).
        bool (*preparar)(void* contexto) = nullptr;
        // Chamado na tarefa de audio. `amostras` = total de int16 ESTEREO intercalado (L,R,L,R...). Bloqueante.
        bool (*escrever)(void* contexto, const int16_t* estereo, size_t amostras) = nullptr;
        // Opcional, na tarefa de audio: um som comecou (log).
        void (*aoComecar)(void* contexto, SomId id) = nullptr;
        void* contexto = nullptr;
    };

    class Tocador {
    public:
        static constexpr size_t kBlocoFrames = 256;              // 16 ms por bloco a 16 kHz
        static constexpr size_t kProfundidadeFila = 4;
        static constexpr size_t kFadeFrames = 128;               // 8 ms de fade ao interromper um som
        static constexpr size_t kSilencioFinalFrames = 320;      // 20 ms de silencio no fim (evita estalo)
        static constexpr uint32_t kPilhaBytes = 4096;
        static constexpr unsigned kPrioridade = 2;               // acima do loop (1): o audio nao engasga com o desenho

        // Cria fila e tarefa na 1a chamada (idempotente).
        bool iniciar(const Saida& saida);
        // Nunca bloqueia. false se o som foi descartado (fila cheia / sem audio).
        bool tocar(const Pedido& pedido);
        bool ocupado() const { return pendentes_.load() > 0 || tocando_.load(); }
        // Espera a fila esvaziar e o som terminar (ate `timeoutMs`); se estourar, para o som com seguranca.
        void esperarOuParar(uint32_t timeoutMs);

    private:
        static void entrada(void* self);
        void laco();
        bool escreverMono(const int16_t* mono, size_t frames);
        bool escreverSilencio(size_t frames);
        void aplicar(const Pedido& pedido);

        Saida saida_;
        void* fila_ = nullptr;   // QueueHandle_t
        Arbitro arbitro_;
        Sintetizador sintese_;
        std::atomic<int> pendentes_{0};      // pedidos na fila ainda nao lidos pela tarefa
        std::atomic<bool> tocando_{false};
        std::atomic<bool> parar_{false};
        bool iniciada_ = false;
    };

} // namespace som
