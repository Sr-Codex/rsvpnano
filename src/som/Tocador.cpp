#include "som/Tocador.h"

#include <Arduino.h>
#include <freertos/FreeRTOS.h>
#include <freertos/queue.h>
#include <freertos/task.h>

#include <algorithm>
#include <array>

namespace som {

    bool Tocador::iniciar(const Saida& saida) {
        if (iniciada_) return true;
        saida_ = saida;
        fila_ = xQueueCreate(kProfundidadeFila, sizeof(Pedido));
        if (fila_ == nullptr) return false;
        if (xTaskCreate(&Tocador::entrada, "audio", kPilhaBytes, this, kPrioridade, nullptr) != pdPASS) return false;
        iniciada_ = true;
        return true;
    }

    bool Tocador::tocar(const Pedido& pedido) {
        if (!iniciada_ || saida_.escrever == nullptr) return false;
        if (!ocupado() && saida_.preparar != nullptr && !saida_.preparar(saida_.contexto)) return false;
        pendentes_.fetch_add(1);
        if (xQueueSend(static_cast<QueueHandle_t>(fila_), &pedido, 0) != pdTRUE) {
            pendentes_.fetch_sub(1);
            return false;
        }
        return true;
    }

    void Tocador::esperarOuParar(uint32_t timeoutMs) {
        const uint32_t inicio = millis();
        while (ocupado() && millis() - inicio < timeoutMs) delay(5);
        if (ocupado()) {
            parar_.store(true);
            while (ocupado() && millis() - inicio < timeoutMs + 200) delay(5);
        }
    }

    void Tocador::entrada(void* self) {
        static_cast<Tocador*>(self)->laco();
    }

    bool Tocador::escreverMono(const int16_t* mono, size_t frames) {
        std::array<int16_t, kBlocoFrames * 2> estereo;
        for (size_t i = 0; i < frames; ++i) estereo[2 * i] = estereo[2 * i + 1] = mono[i];
        return saida_.escrever(saida_.contexto, estereo.data(), frames * 2);
    }

    bool Tocador::escreverSilencio(size_t frames) {
        std::array<int16_t, kBlocoFrames> zeros{};
        while (frames > 0) {
            const size_t n = std::min(frames, kBlocoFrames);
            if (!escreverMono(zeros.data(), n)) return false;
            frames -= n;
        }
        return true;
    }

    void Tocador::aplicar(const Pedido& pedido) {
        sintese_.iniciar(pedido.som, pedido.ganho);
        if (saida_.aoComecar != nullptr) saida_.aoComecar(saida_.contexto, pedido.som);
    }

    void Tocador::laco() {
        std::array<int16_t, kBlocoFrames> bloco;
        for (;;) {
            Pedido pedido;
            if (arbitro_.ocupado()) {
                tocando_.store(true);
            } else {
                tocando_.store(false);
                if (xQueueReceive(static_cast<QueueHandle_t>(fila_), &pedido, portMAX_DELAY) != pdTRUE) continue;
                tocando_.store(true);
                pendentes_.fetch_sub(1);
                if (arbitro_.pedir(pedido) == Arbitro::Acao::TocarAgora) aplicar(pedido);
                else continue;
            }
            // toca ate acabar (ou ser interrompido / parado)
            bool falhou = false;
            while (arbitro_.ocupado()) {
                if (parar_.load()) {
                    arbitro_.parar();
                    escreverSilencio(kSilencioFinalFrames);
                    break;
                }
                const size_t n = sintese_.gerar(bloco.data(), bloco.size());
                if (n > 0 && !escreverMono(bloco.data(), n)) {
                    falhou = true;
                    arbitro_.parar();
                    break;
                }
                // pedidos novos: a arbitragem decide (interromper = fade curto do som atual e troca)
                while (xQueueReceive(static_cast<QueueHandle_t>(fila_), &pedido, 0) == pdTRUE) {
                    pendentes_.fetch_sub(1);
                    const auto acao = arbitro_.pedir(pedido);
                    if (acao == Arbitro::Acao::TocarAgora) {
                        if (!sintese_.terminou()) {   // fade-out de 8 ms do som cortado (sem estalo)
                            const size_t m = sintese_.gerar(bloco.data(), kFadeFrames);
                            for (size_t i = 0; i < m; ++i)
                                bloco[i] = static_cast<int16_t>(int32_t(bloco[i]) * int32_t(m - i) / int32_t(m));
                            escreverMono(bloco.data(), m);
                        }
                        aplicar(pedido);
                    }
                }
                if (n == 0) {   // acabou de verdade: silencio de cauda e proximo da fila de espera
                    escreverSilencio(kSilencioFinalFrames);
                    if (const auto proximo = arbitro_.concluir()) aplicar(*proximo);
                }
            }
            if (parar_.load()) {
                // esvazia a fila (o pedido e descartado) e deixa o proximo comecar do zero
                while (xQueueReceive(static_cast<QueueHandle_t>(fila_), &pedido, 0) == pdTRUE) pendentes_.fetch_sub(1);
                parar_.store(false);
            }
            if (falhou) vTaskDelay(pdMS_TO_TICKS(20));
        }
    }

} // namespace som
