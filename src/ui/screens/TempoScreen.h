#pragma once

#include <FS.h>

#include <string>
#include <string_view>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/FocoLeitor.h"
#include "tempo/Habitos.h"
#include "tempo/Motor.h"
#include "tempo/Sessao.h"
#include "ui/screens/ModernScreens.h"

namespace screens {

    // App "Tempo": Inicio (estilo Session: aneis + foco + categoria + ciclo pomodoro) com seletor de foco, Batalha
    // (foco, overtime, pausa) e Reflexao (tela "C3": dois aneis estilo Apple). Screen::TempoHoje = o Inicio. Textos em pt-BR direto no codigo (fora do pacote de idiomas).
    // Reflexao reaproveita Screen::TempoVitoria (a enumeracao nao muda).
    // Aneis estilo Apple em miniatura (launcher): externo = voce (laranja, azul no bonus), interno = inimigo (carmim).
    // (cx, cy) = centro; raio = raio da linha central do anel externo. Pinta direto no `g` (regiao opaca do chamador).
    void desenharAneisMini(ui::Context& ui, Arduino_GFX& g, int cx, int cy, int raio, double voce, double inimigo,
                           bool bonus = false);

    // Anel de XP estilo Apple (laranja -> ambar) para Home/Perfil. raio = linha central; frac 0..1 (>1 = 2a volta).
    void desenharAnelXp(ui::Context& ui, Arduino_GFX& g, int cx, int cy, int raio, int larg, double frac,
                        ui::themes::ColorRole fundo);

    // Resumo para o launcher (Home). So leitura.
    struct ResumoTempo {
        enum class Estado : uint8_t { Livre, Batalha, Reflexao } estado = Estado::Livre;
        bool pausada = false;
        bool descanso = false;
        bool bonus = false;
        int vocePct = 0;                 // 0..100
        int inimigoPct = 100;            // 0..100
        std::string projeto;             // batalha: foco em curso; livre: foco escolhido no Inicio
        std::string centro;              // mm:ss (ou +mm:ss) / planejado
        std::string saldo;               // "214" (creditos)
        bool temProjetos = false;
        bool livre = true;               // foco escolhido = Foco livre
        int planejadoMin = 25;
        int nivel = 1;                   // nivel de XP (Perfil)
        double xpFrac = 0.0;             // progresso dentro do nivel (0..1)
        int streak = 0;
        int focoHojeMin = 0;             // foco pago hoje
        uint32_t assinatura = 0;         // muda quando o resumo muda
    };

    class TempoScreen {
    public:
        ResumoTempo resumo(fs::FS* filesystem, uint32_t nowMs);   // carrega o SD na 1a chamada
        void abrirBatalha(Screen& screen);                         // "Voltar a batalha" / "Responder"
        bool comecar(Screen& screen);                              // abre o Inicio com o proximo projeto pre-selecionado
        void focoRapido(uint32_t nowMs, Screen& screen, fs::FS* filesystem);   // BOOT no launcher: Foco livre ja
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
        // Foco do leitor (src/focus/): liquida uma rodada como batalha de Foco livre em Leitura (sem reflexao).
        // Chamar fora do desenho. Nao liquida se ha batalha/reflexao do Tempo em curso (sem dupla contagem).
        // Devolve true se liquidou e gravou.
        bool registrarFocoLeitor(uint32_t nowMs, fs::FS* filesystem, const tempo::RodadaLeitor& rodada,
                                 std::string_view tituloLivro);
        // App Habitos (fase 1, sem tela): reconcilia as ocorrencias de hoje com /tempo/habitos.json e aplica os
        // check-ins feitos no celular (/tempo/habitos-checkins.json, baixados pelo sync). Chamar fora do desenho,
        // ex.: ao fim da sync do TickTick. Idempotente.
        void atualizarHabitos(fs::FS* filesystem, uint32_t nowMs);
        // "Feito agora" na ocorrencia `indice` de estado().dia.ocorrencias: paga, grava o log e enfileira o check-in
        // (enviado ao TickTick na proxima sync com Wi-Fi). Devolve false se ja estava feita ou sem SD.
        bool marcarHabito(uint32_t nowMs, fs::FS* filesystem, size_t indice);
        const tempo::Estado& estadoTempo() const { return estado_; }
        bool battleActive() const { return fase_ == Fase::Lutando || fase_ == Fase::Pausada || fase_ == Fase::Descanso; }
        uint8_t surface() const {
            return static_cast<uint8_t>(static_cast<uint8_t>(fase_) * 4 + (distraido_ ? 1 : 0) + (overtime_ ? 2 : 0)
                                        + (seletor_ ? 32 : 0));
        }

    private:
        enum class Fase : uint8_t { Nenhuma, Lutando, Pausada, Descanso, Reflexao };

        void carregar(fs::FS* filesystem, bool recarregarEstado, uint32_t nowMs);
        void comecarSessao(uint32_t nowMs);                    // inicia a batalha (ou o ciclo) com a escolha do Inicio
        void ligarBatalha(uint32_t nowMs);                     // zera contadores e entra em Lutando
        void concluirRodada(uint32_t nowMs, fs::FS* filesystem);   // ciclo: liquida a rodada e entra na pausa
        void salvarPreferencias();
        void escolher(int indice);                              // -1 = Foco livre
        void pausarOuRetomar(uint32_t nowMs);
        void alternarDistracao(uint32_t nowMs);
        void acumular(uint32_t nowMs);
        void finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem);
        void iniciarDescanso(uint32_t nowMs, bool voltaParaBatalha);
        void encerrarDescanso(uint32_t nowMs);
        void salvarReflexao(fs::FS* filesystem);
        void garantirDia(uint32_t nowMs);
        void registrarHabitoMeditacao(fs::FS* filesystem);     // batalha de Meditacao vencida: marca o habito
        void gravarEventoHabito(fs::FS* filesystem, const tempo::ResultadoOcorrencia& r, std::string_view origem);
        void salvar(fs::FS* filesystem);
        int64_t restanteMs() const;      // vida restante do inimigo (ms)
        int64_t overtimeMs() const;      // foco alem do necessario para zerar o inimigo (ms)
        int pontosDeVida() const;        // 0..100
        tempo::LiquidacaoBatalha estimar() const;
        Action drawInicio(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawSeletor(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawVitoria(ui::Context& ui, uint32_t nowMs, Screen& screen);

        bool carregado_ = false;
        uint32_t ultimoDesenhoMs_ = 0;
        fs::FS* fs_ = nullptr;
        tempo::Estado estado_;
        tempo::ListaProjetos lista_;
        tempo::ListaHabitos habitos_;        // definicoes dos habitos (habitos.json)
        std::vector<std::string> titulos_;   // titulos limpos (sem emoji), 1:1 com lista_.projetos
        size_t primeiro_ = 0;                // primeira linha visivel do seletor (rolagem)
        bool seletor_ = false;               // seletor de foco aberto (Screen::TempoHoje)
        int escolha_ = -1;                   // foco escolhido no Inicio: -1 = Foco livre, senao indice do projeto
        tempo::Categoria categoria_ = tempo::Categoria::Trabalho;   // chip selecionado
        int duracaoMin_ = 25;                // duracao do anel do Inicio (5..60)
        bool pomodoro_ = false;              // interruptor do ciclo
        tempo::Ciclo ciclo_;                 // ciclo em curso (ativo = false: sessao unica)
        int64_t descansoMs_ = 5 * 60 * 1000;
        tempo::Categoria categoriaBatalha_ = tempo::Categoria::Trabalho;   // da batalha em curso
        std::string nomeBatalha_;            // "Foco livre · Estudo" ou titulo do projeto
        ui::modern::ListSwipe swipe_;

        Fase fase_ = Fase::Nenhuma;
        int projeto_ = -1;                   // projeto da batalha em curso (-1 = Foco livre)
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
        bool puxando_ = false;               // dedo no puxador da categoria
        int puxadorX_ = 0;
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
