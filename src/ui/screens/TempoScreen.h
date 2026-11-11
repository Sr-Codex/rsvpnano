#pragma once

#include <FS.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/FocoLeitor.h"
#include "tempo/Habitos.h"
#include "tempo/Lembretes.h"
#include "tempo/Motor.h"
#include "som/Sintese.h"
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
        // Habitos (launcher): bloco "N/M . proximo HH:MM" e cartao AGORA quando ha habito na hora/atrasado
        int habFeitas = 0;               // registradas + puladas (resolvidas)
        int habTotal = 0;                // ocorrencias de hoje (0 = sem habitos)
        int habProximoMin = -1;          // proximo horario pendente (minuto do dia), -1 = nenhum
        bool habAtrasado = false;        // ha habito atrasado (na janela e passou do horario, ou depois da janela)
        bool habAgora = false;           // ha habito na hora/atrasado para mostrar no cartao AGORA
        std::string habNome;             // habito do cartao AGORA
        std::string habLinha;            // "14:00 . atrasado 20 min"
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

        // Sons (docs/app-tempo/sons.md): os eventos do Tempo/Habitos enfileiram aqui o SomId do momento (um por evento
        // composto: vitoria+moeda+nivel+conquista viram UM som, o de maior valor) e o App drena 1x por quadro.
        // Devolve false quando nao ha mais nenhum.
        bool pegarSom(som::SomId& id);

        // Avanca a batalha (acumula tempo), entra em overtime quando o inimigo zera, conclui no teto de +100 %,
        // cuida da pausa de 5 min e do auto-salvar da reflexao. Devolve true quando o app deve dar um bipe
        // (inimigo derrotado, fim do overtime/pausa, "presenca" a cada 10 min, aviso 2 min antes do fim). Os SONS saem
        // por pegarSom() (o bool continua so como sinal de "algo aconteceu").
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
        // true uma vez depois de um check-in novo entrar na fila (o App dispara a sync do TickTick).
        bool pegarCheckinNovo() { const bool v = checkinNovo_; checkinNovo_ = false; return v; }
        // "Pular": sem cr/XP, nao entra na fila, nao conta como esquecido (tempo::pularOcorrencia). Devolve false se ja resolvida/sem SD.
        bool pularHabito(uint32_t nowMs, fs::FS* filesystem, size_t indice);
        // "Ainda nao" (medicacao): o lembrete volta em 15 min (nao grava nada: so a agenda de lembretes em RAM).
        void adiarHabito(uint32_t nowMs, size_t indice);
        // Manutencao dos Habitos, FORA do desenho (App::update, ~1x/s): avanca o relogio do dia, na virada do dia e a cada
        // minuto reconcilia com habitos.json + check-ins do celular (atualizarHabitos). Nao roda durante batalha/reflexao.
        void manutencaoHabitos(uint32_t nowMs, fs::FS* filesystem);
        // Lembretes (bipe + aviso no topo): devolve o aviso a mostrar agora (o App toca o bipe). `pegouAgora` = pulso do
        // detector de pegada (IMU). Nao pausa leitura nem batalha. Chamar ~1x/s fora do desenho.
        std::optional<tempo::Aviso> lembretesTick(uint32_t nowMs, bool pegouAgora);
        // Depois de despertar por insistencia de medicacao (ADR-009): arma a agenda de lembretes (so RAM) para insistir ja.
        void semearInsistenciaMedicacao(uint32_t nowMs, int insistenciasFeitas);
        const tempo::ListaHabitos& listaHabitos() const { return habitos_; }
        const tempo::Estado& estadoTempo() const { return estado_; }
        bool estadoCarregado() const { return carregado_; }
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
        void finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem, bool tocarDesfecho = true);
        void iniciarDescanso(uint32_t nowMs, bool voltaParaBatalha);
        void encerrarDescanso(uint32_t nowMs);
        void salvarReflexao(fs::FS* filesystem);
        void garantirDia(uint32_t nowMs);
        void registrarHabitoMeditacao(fs::FS* filesystem);     // batalha de Meditacao vencida: marca o habito
        void gravarEventoHabito(fs::FS* filesystem, const tempo::ResultadoOcorrencia& r, std::string_view origem);
        void salvar(fs::FS* filesystem);
        void pedirSom(som::SomId id);
        // A rodada/batalha liquidada ainda ganha uma conquista nova? (resumo.json antes x depois do evento; sem resumo = vazio)
        bool conquistaNova(fs::FS* filesystem, const tempo::EventoLog& evento, int streakAntes) const;
        int64_t restanteMs() const;      // vida restante do inimigo (ms)
        int64_t overtimeMs() const;      // foco alem do necessario para zerar o inimigo (ms)
        int pontosDeVida() const;        // 0..100
        tempo::LiquidacaoBatalha estimar() const;
        Action drawInicio(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawSeletor(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawVitoria(ui::Context& ui, uint32_t nowMs, Screen& screen);

        bool checkinNovo_ = false;
        bool carregado_ = false;
        uint32_t ultimoDesenhoMs_ = 0;
        fs::FS* fs_ = nullptr;
        tempo::Estado estado_;
        tempo::ListaProjetos lista_;
        tempo::ListaHabitos habitos_;        // definicoes dos habitos (habitos.json)
        tempo::AgendaLembretes lembretes_;   // estado dos lembretes de habitos (so RAM)
        uint32_t manutencaoHabitosMs_ = 0;   // ultimo atualizarHabitos periodico (millis)
        uint32_t manutencaoRelogioMs_ = 0;
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
            bool subiuNivel = false;     // o XP desta liquidacao subiu o nivel
            bool conquista = false;      // ganhou uma conquista nova
        } resultado_;
        std::array<som::SomId, 4> sons_{};
        size_t nSons_ = 0;
    };

} // namespace screens
