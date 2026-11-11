#pragma once

// Conexões seguras (mbedTLS: sync do TickTick, OTA) alocam os buffers na PSRAM em vez da RAM interna.
//
// Por quê: o sdkconfig do core Arduino fixa CONFIG_MBEDTLS_INTERNAL_MEM_ALLOC, então cada handshake pede ~40 KB
// CONTÍNUOS de RAM interna. Depois de ~30 min de uso a RAM interna fica picotada (strings/JSON da interface < 4 KB vão
// sempre para ela) e a sync passa a falhar com "maior bloco livre 23 KB" (medido na lume.15, 2026-11-11). O mbedTLS
// do core tem a camada de memória ligada (MBEDTLS_PLATFORM_MEMORY sem macros fixas), então dá para trocar o alocador
// em tempo de execução — equivalente ao CONFIG_MBEDTLS_EXTERNAL_MEM_ALLOC oficial do ESP-IDF, sem recompilar o core.
namespace rede {

    // Chamar no setup(), antes de qualquer conexão segura. Devolve false se não há PSRAM (fica como estava).
    bool tlsNaPsram();

} // namespace rede
