#pragma once

#include <FS.h>

#include <cstddef>
#include <expected>
#include <string>

#include "settings/SettingsModel.h"

// Sincroniza o TickTick direto pela placa (Wi-Fi + HTTPS verificado) e grava /tempo/projetos.json.
// A montagem do JSON e a leitura das respostas ficam em src/tempo/TickTick.* (C++ puro, testado no host).
namespace net::ticktick {

    inline constexpr const char* kApi = "https://api.ticktick.com/open/v1";

    struct Status {
        std::string lastSync;  // AAAA-MM-DDTHH:MM:SSZ da ultima sincronizacao bem-sucedida ("" = nunca)
        std::string error;     // ultimo erro ("" = ultima tentativa deu certo)
    };

    // Copia segura entre tarefas (o job roda em segundo plano; a API le para o GET).
    Status status();

    // Bloqueante (rodar no job de segundo plano). Usa o Wi-Fi ja conectado ou conecta pelas redes salvas e
    // desconecta ao fim se foi ela quem conectou. Devolve a quantidade de projetos gravados; o resultado
    // (sucesso ou erro) tambem fica em status(). O token nunca e registrado em log.
    std::expected<size_t, std::string> sync(const settings::NetworkSettings& network,
                                            const settings::DeviceSecrets& secrets, fs::FS& filesystem);

} // namespace net::ticktick
