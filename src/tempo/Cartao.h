#pragma once

// Cartao SD do app Tempo: decide, so pelo que deu para ler, se o estado esta bom, se e a primeira vez (cartao novo) ou se
// esta ILEGIVEL (cartao falhando, arquivo truncado). C++ puro; testes em test/test_cartao_tempo.
//
// Por que existe (revisao do Fable, 2026-11-12): quando nem /tempo/estado.json nem o .bak podiam ser lidos, a placa
// comecava do zero e GRAVAVA esse zero por cima, sem avisar. Agora:
//   estado ou .bak validos ................ Ok        (usa o que leu)
//   os dois ausentes e nenhum outro dado .. Ausente   (cartao novo: comeca do zero e grava normalmente)
//   qualquer outra coisa .................. Ilegivel  (comeca do zero NA MEMORIA, nao grava nada por cima, avisa)
// "Outro dado" = resumo do Perfil ou logs: se eles existem e o estado sumiu, nao e primeira vez.

#include <cstdint>

namespace tempo::cartao {

    enum class Arquivo : uint8_t { Valido, Ausente, Invalido };   // Invalido = existe mas nao deu para ler/entender
    enum class Leitura : uint8_t { Ok, Ausente, Ilegivel };

    Leitura classificar(Arquivo estado, Arquivo backup, bool temOutrosDados);

    // Situacao do cartao para o app (aviso na tela e se pode gravar o estado).
    enum class Situacao : uint8_t { Ok, SemCartao, Ilegivel, FalhaGravar };

    Situacao aposCarregar(bool temCartao, Leitura leitura);
    bool podeGravar(Situacao s);                       // Ilegivel e SemCartao nunca gravam o estado
    Situacao aposGravar(Situacao s, bool gravou);      // falha -> FalhaGravar; gravou de novo -> Ok
    // Texto do aviso (2a linha do cartao do foco, ambar); nullptr = sem aviso.
    const char* aviso(Situacao s);

} // namespace tempo::cartao
