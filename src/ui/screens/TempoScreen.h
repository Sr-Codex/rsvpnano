#pragma once

#include <FS.h>

#include <string>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/Motor.h"
#include "ui/screens/ModernScreens.h"

namespace screens {

    // App "Tempo" v1: Hoje, Batalha (preparo, foco, overtime, pausa) e Reflexao (tela "C3": dois aneis
    // estilo Apple). Textos em pt-BR direto no codigo (fora do pacote de idiomas).
    // Reflexao reaproveita Screen::TempoVitoria (a enumeracao nao muda).
    // Aneis estilo Apple em miniatura (launcher): externo = voce (laranja, azul no bonus), interno = inimigo (carmim).
    // (cx, cy) = centro; raio = raio da linha central do anel externo. Pinta direto no `g` (regiao opaca do chamador).
    void desenharAneisMini(ui::Context& ui, Arduino_GFX& g, int cx, int cy, int raio, double voce, double inimigo,
                           bool bonus = false);

    // Resumo para o launcher (Home). So leitura.
    struct ResumoTempo {
        enum class Estado : uint8_t { Livre, Batalha, Reflexao } estado = Estado::Livre;
        bool pausada = false;
        bool descanso = false;
        bool bonus = false;
        int vocePct = 0;                 // 0..100
        int inimigoPct = 100;            // 0..100
        std::string projeto;             // batalha: projeto em curso; livre: primeiro da lista
        std::string centro;              // mm:ss (ou +mm:ss) / planejado
        std::string saldo;               // "214" (creditos)
        bool temProjetos = false;
        int planejadoMin = 25;
        uint32_t assinatura = 0;         // muda quando o resumo muda
    };

    class TempoScreen {
    public:
        ResumoTempo resumo(fs::FS* filesystem, uint32_t nowMs);   // carrega o SD na 1a chamada
        void abrirBatalha(Screen& screen);                         // "Voltar a batalha" / "Responder"
        bool comecar(Screen& screen);                              // abre o preparo do 1o projeto
        static bool owns(Screen screen) {
            return screen == Screen::TempoHoje || screen == Screen::TempoBatalha || screen == Screen::TempoVitoria;
        }

        // Avanca a batalha (acumula tempo), entra em overtime quando o inimigo zera, conclui no teto de +100 %,
        // cuida da pausa de 5 min e do auto-salvar da reflexao. Devolve true quando o app deve dar um bipe
        // (inimigo derrotado, fim do overtime/pausa, "presenca" a cada 10 min, aviso 2 min antes do fim).
        bool tick(uint32_t nowMs, fs::FS* filesystem);
        Action draw(ui::Context& ui, uint32_t nowMs, Screen& screen, fs::FS* filesystem);
        void boot(uint32_t nowMs, Screen& screen, fs::FS* filesystem);   // BOOT curto
        void back(uint32_t nowMs, Screen& screen, fs::FS* filesystem);   // PWR curto
        // Recarrega so a lista de projetos do SD (apos a sync do TickTick). Ignorado durante preparo/batalha/reflexao.
        void recarregarProjetos(fs::FS* filesystem);
        bool battleActive() const { return fase_ == Fase::Lutando || fase_ == Fase::Pausada || fase_ == Fase::Descanso; }
        uint8_t surface() const {
            return static_cast<uint8_t>(static_cast<uint8_t>(fase_) * 4 + (distraido_ ? 1 : 0) + (overtime_ ? 2 : 0));
        }

    private:
        enum class Fase : uint8_t { Nenhuma, Preparo, Lutando, Pausada, Descanso, Reflexao };

        void carregar(fs::FS* filesystem, bool recarregarEstado, uint32_t nowMs);
        void abrirPreparo(size_t indiceProjeto);
        void iniciar(uint32_t nowMs);
        void pausarOuRetomar(uint32_t nowMs);
        void alternarDistracao(uint32_t nowMs);
        void acumular(uint32_t nowMs);
        void finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem);
        void iniciarDescanso(uint32_t nowMs, bool voltaParaBatalha);
        void encerrarDescanso();
        void salvarReflexao(fs::FS* filesystem);
        void garantirDia(uint32_t nowMs);
        void salvar(fs::FS* filesystem);
        int64_t restanteMs() const;      // vida restante do inimigo (ms)
        int64_t overtimeMs() const;      // foco alem do necessario para zerar o inimigo (ms)
        int pontosDeVida() const;        // 0..100
        tempo::LiquidacaoBatalha estimar(bool preparo) const;
        Action drawHoje(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawVitoria(ui::Context& ui, uint32_t nowMs, Screen& screen);

        bool carregado_ = false;
        uint32_t ultimoDesenhoMs_ = 0;
        fs::FS* fs_ = nullptr;
        tempo::Estado estado_;
        tempo::ListaProjetos lista_;
        std::vector<std::string> titulos_;   // titulos limpos (sem emoji), 1:1 com lista_.projetos
        size_t primeiro_ = 0;                // primeiro projeto visivel (rolagem)
        ui::modern::ListSwipe swipe_;

        Fase fase_ = Fase::Nenhuma;
        size_t projeto_ = 0;
        int planejadoMin_ = 25;
        uint64_t ultimoMs_ = 0;
        uint64_t focadoMs_ = 0;
        uint64_t distraidoMs_ = 0;
        uint64_t distracaoAtualMs_ = 0;
        uint64_t comboMs_ = 0;               // foco continuo sem distracao
        bool distraido_ = false;
        bool overtime_ = false;              // inimigo derrotado, seguindo no bonus
        int numDistracoes_ = 0;
        int maiorDistracaoMin_ = 0;
        int ultimoBipeFocoMin_ = 0;          // ultimo multiplo de 10 min de foco ja avisado
        bool avisoFimDado_ = false;
        bool arrastando_ = false;            // dedo no anel do preparo
        uint64_t descansoInicioMs_ = 0;
        bool descansoVolta_ = false;         // pausa veio do overtime: volta para a batalha (pausada)

        // reflexao
        uint32_t reflexaoInicioMs_ = 0;
        int humor_ = -1;                     // 0..4 (Pessimo..Otimo), -1 = sem resposta
        int distraiu_ = -1;                  // 0 Nao, 1 Um pouco, 2 Muito, -1 = sem resposta
        tempo::EventoLog eventoPendente_;
        tempo::Data dataPendente_;

        struct Resultado {
            tempo::LiquidacaoBatalha liq;
            std::string projeto;
            int planejadoMin = 0;
            int focadoMin = 0;
            int nivelAtributo = 1;
            int streak = 0;
            bool abandonou = false;
        } resultado_;
    };

} // namespace screens
