#pragma once

#include <cstdint>

// Cola de hardware da confirmacao OTA (ADR-010): le o estado da imagem em execucao e chama
// esp_ota_mark_app_valid_cancel_rollback() quando ota::Confirmador diz que o app esta saudavel.
// No simulador e nas builds sem rollback, tudo isto e no-op.
namespace ota {

    void iniciar();                            // setup(): le o estado da imagem e registra no log
    void setupConcluido();                     // fim do setup()
    void toqueOk(bool ok);
    void sdMontado(bool ok);
    void primeiroQuadro(uint32_t nowMs);       // idempotente: chamar a cada quadro desenhado
    void tick(uint32_t nowMs);                 // a cada volta do laco; confirma quando saudavel
    void confirmarAntesDeReiniciar(const char* motivo);   // deep sleep / desligar / reiniciar de proposito

} // namespace ota
