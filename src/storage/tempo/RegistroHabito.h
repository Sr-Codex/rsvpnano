#pragma once

#include <FS.h>
#include <optional>
#include <string_view>

#include "tempo/Tipos.h"

namespace TempoStorage {
    enum class GravacaoHabito { Recusada, FilaCheia, Pendente, Completa };

    // Chamadas sob o mutex de TempoStorage. O marcador preserva o conteúdo necessário para retomar a gravação.
    namespace registro {
        bool diaPendente(fs::FS& fs, const tempo::Data& dia);
        bool pendente(fs::FS& fs);
        bool recuperar(fs::FS& fs);
        bool lerEstadoPendente(fs::FS& fs, tempo::Estado& estado);
        GravacaoHabito gravar(fs::FS& fs, const tempo::Estado& estado, std::string_view eventos,
                              std::optional<std::string_view> fila);
    }
}
