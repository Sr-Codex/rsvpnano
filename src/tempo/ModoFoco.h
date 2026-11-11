#pragma once

// Modo Foco — etapa 0 (docs/app-tempo/revisao-fable-modo-foco-2026-11-11.md, secoes 3.1-3.8 e 4.5): sem cena, a tela da
// batalha do Tempo ESCURECE depois de 10 s sem toque e o 1o toque so acorda. C++ puro, sem hardware: o App monta um
// Contexto por volta do laco e pede o BRILHO EFETIVO; so chama Board::Display::setBrightness quando o valor muda.
// Assim toda saida (fim da batalha, saque/Reflexao, trocar de app, standby, OTA, aviso de habito, placa em pe...) volta
// ao brilho do usuario por construcao: fora das condicoes de escurecer o alvo e sempre o brilho escolhido em Ajustes.
// O valor salvo do usuario nunca e alterado aqui (so lido).
//
//   Normal --10 s sem toque--> Escurecendo (1,5 s) --> Escuro
//     ^                            | toque (antes da metade: cancela, NAO consome)      | 1o toque: acorda e e CONSUMIDO;
//     |                            | toque (depois da metade: como Escuro)              | os botoes armam 300 ms depois
//     +------- Acordando (300 ms) <-+-------------------------------------------------- +
//   Fora das condicoes (pausado, distraido, Fim segurando, saque/Reflexao, fora da batalha, aviso, em pe, Desligado):
//   Acordando -> Normal, e os 10 s so comecam a contar quando as condicoes voltam.
//
// Ajuste Cena (etapa 1, cenas QG): a MESMA maquina; o App desenha a cena em tela cheia no lugar da batalha quando
// cenaNaTela() (Escuro, ou rampa passada da metade = o mesmo ponto em que o toque passa a ser consumido). So em focando e
// hora extra (usaCena); a pausa do ciclo continua so escurecendo. Brilho com a cena ("mergulho" na troca de imagem):
//   Escurecendo 0..1/2: batalha  usuario -> escurecido (fatores da etapa 0)      | troca para a cena na metade
//   Escurecendo 1/2..1: cena     escurecido -> brilho da cena (kFatorCena*)
//   Escuro: cena no brilho da cena; na vitoria (Contexto::cenaVitoria) volta ao brilho do usuario; trocas suaves
//   Acordando: do brilho que estava na tela ate o do usuario (300 ms)

#include <cstdint>

namespace tempo::modofoco {

    // Ajuste "Modo Foco" (Ajustes > Sons e foco): Desligado / Escurecer / Cena (cena QG em tela cheia; ver acima).
    enum class Ajuste : uint8_t { Desligado, Escurecer, Cena };

    // ---- Constantes (chute inicial; calibrar na placa de dia e de noite — revisao, secao 3.8)
    inline constexpr uint32_t kOciosoMs = 10000;         // sem toque na batalha ate comecar a escurecer
    inline constexpr uint32_t kDescerMs = 1500;          // rampa de escurecer
    inline constexpr uint32_t kSubirMs = 300;            // rampa de acordar (toque, aviso, qualquer saida)
    inline constexpr uint32_t kArmarMs = 300;            // depois de acordar, toques que COMECAM antes disso sao consumidos
    inline constexpr uint8_t kFatorDiaPct = 40;          // escurecido de dia = 40 % do brilho escolhido
    inline constexpr uint8_t kFatorNoitePct = 20;        // escurecido de noite = 20 % do brilho escolhido
    inline constexpr int kNoiteInicioMin = 22 * 60;      // noite = 22:00 ..
    inline constexpr int kNoiteFimMin = 6 * 60;          //          .. 06:00 (hora local do app; sem hora = dia)
    // Piso absoluto: 5 % = o menor brilho que o proprio usuario consegue escolher em Ajustes (BoundedValue 5..100). Na
    // placa 3.49 a escala logica 1..100 % ja e mapeada para a faixa util do PWM (duty minimo 102/255, BoardDisplay.cpp),
    // entao 5 % fica aceso e legivel; nunca escurecemos alem do que o dono ja viu e aprovou como "mais escuro".
    // Obs.: como o mapeamento e linear no duty a partir de 102, "40 % do brilho" logico reduz menos a luz real
    // (ex.: 70 % -> 28 % logico = duty ~82 % -> ~56 %). Os fatores acima sao para calibrar olhando a placa.
    inline constexpr uint8_t kPisoPct = 5;
    // Cena (as cenas QG sao escuras por natureza): brilho proprio, num lugar so. Chute inicial; calibrar na placa.
    inline constexpr uint8_t kFatorCenaDiaPct = 75;      // cena de dia = 75 % do brilho escolhido
    inline constexpr uint8_t kFatorCenaNoitePct = 45;    // cena das 22:00 as 06:00 = 45 %
    inline constexpr uint32_t kTrocaBrilhoCenaMs = 1500; // vitoria <-> amanhecer: o brilho anda no maximo 100 % em 1,5 s
    // Toque durante a rampa de escurecer: ate esta fracao a tela ainda esta clara -> o toque so cancela (vai para o botao
    // normalmente); depois dela a tela ja parece apagada -> o toque e tratado como o "1o toque" (consumido).
    inline constexpr float kRampaConsomeAPartirDe = 0.5f;

    // Fase da batalha vista pelo Modo Foco (o App traduz o estado do TempoScreen).
    enum class Fase : uint8_t {
        Nenhuma,      // sem batalha (Inicio, seletor...)
        Focando,      // Lutando, sem distracao, antes de zerar o inimigo
        HoraExtra,    // Lutando em overtime
        PausaCiclo,   // pausa do ciclo 4x (5/15 min)
        Pausado,      // "Pausar"
        Distraido,    // "Distrai"
        Fechamento,   // saque/Desfazer ou Reflexao
    };

    struct Contexto {
        Ajuste ajuste = Ajuste::Escurecer;
        bool telaBatalha = false;     // a tela da batalha do Tempo esta na frente
        Fase fase = Fase::Nenhuma;
        bool segurandoFim = false;    // "Fim · segure" em andamento
        bool dedoNaTela = false;      // ha um dedo apoiado (nao escurece com a mao na tela)
        bool avisoHabito = false;     // aviso de lembrete no topo: acorda a tela
        bool emPe = false;            // placa em pe (IMU): so escurece deitada
        uint8_t brilhoUsuario = 70;   // brilho escolhido em Ajustes (1..100)
        int minutoDoDia = -1;         // hora local do app (0..1439); -1 = desconhecida (usa o fator de dia)
        bool cenaVitoria = false;     // cena na tela mostrando a vitoria (~10 s): brilho do usuario
    };

    enum class Estado : uint8_t { Normal, Escurecendo, Escuro, Acordando };

    // Todas as condicoes para escurecer (ou continuar escuro) valem agora?
    bool podeEscurecer(const Contexto& c);
    bool noite(int minutoDoDia);
    // Brilho alvo escurecido (1..100) para o brilho do usuario e a hora: fator dia/noite, piso kPisoPct, nunca acima do usuario.
    uint8_t brilhoEscurecido(uint8_t brilhoUsuario, int minutoDoDia);
    // Brilho da cena (1..100): fator kFatorCena* dia/noite, piso kPisoPct, nunca acima do usuario.
    uint8_t brilhoCena(uint8_t brilhoUsuario, int minutoDoDia);
    // Ajuste Cena e a fase tem cena (focando ou hora extra)? A pausa do ciclo so escurece.
    bool usaCena(const Contexto& c);

    class Controlador {
    public:
        // Um toque COMECOU (TouchStart). Conta como atividade (zera os 10 s). Devolve true se o gesto inteiro deve ser
        // CONSUMIDO (nao chega a nenhum botao): tela escura (ou rampa passada da metade) = este toque so acorda; ou o toque
        // comecou nos kArmarMs seguintes a um despertar.
        bool toque(uint32_t nowMs);
        // BOOT/PWR: acorda e zera os 10 s, mas o botao mantem a funcao dele (nunca consumido).
        void botao(uint32_t nowMs);
        // Avanca a maquina com o estado desta volta do laco e devolve o brilho efetivo (1..100).
        uint8_t atualizar(const Contexto& c, uint32_t nowMs);

        Estado estado() const { return estado_; }
        // 0 = brilho normal, 1 = totalmente escurecido (posicao na rampa), no instante da ultima atualizacao.
        float nivel() const { return nivel_; }
        // A tela esta "escurecida" (Escuro, ou rampa passada da metade): com o ajuste Cena e o momento de mostrar a cena.
        // Coincide com o ponto em que um toque passa a ser consumido (so acorda).
        bool cenaNaTela() const {
            return estado_ == Estado::Escuro || (estado_ == Estado::Escurecendo && nivel_ >= kRampaConsomeAPartirDe);
        }
        // Desenhar a cena agora? (ajuste Cena, focando/hora extra, e a tela escurecida)
        bool mostrarCena(const Contexto& c) const { return usaCena(c) && cenaNaTela(); }

    private:
        float nivelEm(uint32_t nowMs) const;
        void acordar(uint32_t nowMs);

        Estado estado_ = Estado::Normal;
        uint32_t ultimaAtividadeMs_ = 0;
        bool temAtividade_ = false;   // antes da 1a volta: conta a partir da 1a atualizacao
        uint32_t rampaInicioMs_ = 0;
        float rampaNivelInicial_ = 0.0f;   // Acordando: de onde a subida comecou (0..1)
        float nivel_ = 0.0f;
        uint32_t acordouMs_ = 0;
        bool armando_ = false;        // janela de kArmarMs depois de um despertar por toque/botao
        // Ajuste Cena: ultimo brilho entregue (a subida e a troca vitoria/amanhecer partem dele, sem saltos)
        float ultimoBrilho_ = 0.0f;
        bool ultimoUsaCena_ = false;
        uint32_t ultimoMs_ = 0;
        float subidaDe_ = 0.0f;       // brilho no instante do despertar (Acordando vindo da cena)
        bool subidaDaCena_ = false;
        float brilhoCenaEm(const Contexto& c, uint32_t nowMs);
    };

} // namespace tempo::modofoco
