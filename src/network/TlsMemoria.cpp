#include "network/TlsMemoria.h"

#include <esp_heap_caps.h>
#include <esp_log.h>
#include <mbedtls/platform.h>

namespace rede {

    namespace {
        void* callocTls(size_t n, size_t tamanho) {
            if (void* p = heap_caps_calloc(n, tamanho, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return p;
            return heap_caps_calloc(n, tamanho, MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT);   // PSRAM cheia: RAM interna
        }

        void freeTls(void* p) {
            heap_caps_free(p);   // serve para os dois heaps (e para o que foi alocado antes da troca)
        }
    } // namespace

    bool tlsNaPsram() {
        if (heap_caps_get_total_size(MALLOC_CAP_SPIRAM) == 0) return false;
        const bool ok = mbedtls_platform_set_calloc_free(callocTls, freeTls) == 0;
        ESP_LOGI("tls", "buffers do mbedTLS na PSRAM: %s", ok ? "sim" : "NAO");
        return ok;
    }

} // namespace rede
