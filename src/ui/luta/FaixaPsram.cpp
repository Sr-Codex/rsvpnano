#include "ui/luta/FaixaPsram.h"

#include <esp_heap_caps.h>

#include <new>

namespace ui::luta {

    namespace {
        void* psram(size_t bytes) { return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT); }
        template<typename T>
        void soltar(T*& p) {
            if (p != nullptr) heap_caps_free(p);
            p = nullptr;
        }
    } // namespace

    bool FaixaPsram::garantir() {
        if (faixa_ != nullptr) return true;
        mem_.rgb = static_cast<uint8_t*>(psram(size_t(kLarg) * kAlt * 3));
        mem_.saida = static_cast<uint16_t*>(psram(size_t(kLarg) * kAlt * sizeof(uint16_t)));
        mem_.pilula = static_cast<uint8_t*>(psram(size_t(kBarraLarg) * kBarraAlt));
        if (!mem_.rgb || !mem_.saida || !mem_.pilula) {
            liberar();
            return false;
        }
        faixa_ = new (armazem_) Faixa(mem_);   // monta o trilho (cobertura do pill)
        return true;
    }

    bool FaixaPsram::garantirPoeira() {
        if (faixa_ == nullptr) return false;
        if (mem_.camada != nullptr) return true;
        mem_.camada = static_cast<uint8_t*>(psram(size_t(kCamadaLarg) * kAlt * 3));
        mem_.particulas = static_cast<tempo::luta::Particula*>(psram(sizeof(tempo::luta::Particula) * kMaxParticulas));
        mem_.blocos = static_cast<uint16_t*>(psram(sizeof(uint16_t) * 2 * kMaxParticulas));
        if (!mem_.camada || !mem_.particulas || !mem_.blocos) {
            soltar(mem_.camada);
            soltar(mem_.particulas);
            soltar(mem_.blocos);
            return false;
        }
        // a Faixa guarda uma copia de Memoria: recria apontando para os mesmos buffers
        faixa_->~Faixa();
        faixa_ = new (armazem_) Faixa(mem_);
        return true;
    }

    void FaixaPsram::liberar() {
        if (faixa_ != nullptr) faixa_->~Faixa();
        faixa_ = nullptr;
        soltar(mem_.rgb);
        soltar(mem_.saida);
        soltar(mem_.pilula);
        soltar(mem_.camada);
        soltar(mem_.particulas);
        soltar(mem_.blocos);
    }

} // namespace ui::luta
