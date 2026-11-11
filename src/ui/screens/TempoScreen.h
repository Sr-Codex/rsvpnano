#pragma once

#include <FS.h>

#include <array>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/Codec.h"
#include "tempo/Cronometro.h"
#include "tempo/FocoLeitor.h"
#include "tempo/Habitos.h"
#include "tempo/Lembretes.h"
#include "tempo/Motor.h"
#include "tempo/Recarga.h"
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

    // Anel estilo Apple generico (Habitos v2): arco em degrade cor0 -> cor1 sobre trilho (22 % de cor0 sobre `fundo`), comeca
    // no topo, sentido horario, pontas redondas; frac > 1 = 2a volta. Cores em RGB565; `fundo` = cor opaca sob o anel.
    void desenharAnelApple(Arduino_GFX& g, int cx, int cy, int raio, int larg, double frac, uint16_t cor0, uint16_t cor1,
                           uint16_t fundo);

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
        // "Sincronizar" (seletor sem projetos): true uma vez depois do toque; o App dispara a sync silenciosa do TickTick.
        bool pegarPedidoSync() { const bool v = pedidoSync_; pedidoSync_ = false; return v; }

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
        // Fim da sync do TickTick (lume.12): relê projetos + habitos.json + check-ins UMA vez, DEPOIS que o job gravou tudo.
        // `versao` = net::ticktick::versao() (marca como vista: a manutencao nao relê de novo).
        void sincronizacaoTerminou(fs::FS* filesystem, uint32_t nowMs, uint32_t versao);
        // Enquanto true o job de fundo esta gravando projetos/habitos/check-ins: a UI NAO le esses arquivos (a releitura
        // fica pendente e acontece quando a sync terminar).
        void definirSyncAtiva(bool ativa) { syncAtiva_ = ativa; }
        // A tela de Habitos abriu: relê na proxima manutencao.
        void pedirRecarga() { recargaPedida_ = true; }
        // Despertar por timer (ADR-009): o lembrete das `minutoAlvo` toca mesmo atrasado.
        void forcarAvisoInicial(int minutoAlvo) { if (carregado_) lembretes_.forcarAvisoInicial(estado_, minutoAlvo); }
        // "Feito agora" na ocorrencia `indice` de estado().dia.ocorrencias: paga, grava o log e enfileira o check-in
        // (enviado ao TickTick na proxima sync com Wi-Fi). Devolve false se ja estava feita ou sem SD.
        // `resultado` (opcional) recebe cr/XP/status para a celebracao. Guarda o instante para o "Desfazer" (8 s).
        bool marcarHabito(uint32_t nowMs, fs::FS* filesystem, size_t indice, tempo::ResultadoOcorrencia* resultado = nullptr);
        // "Desfazer" do ultimo Feito/Pular (Habitos v2): reverte cr/XP/coracoes/eventos do dia, tira o check-in da fila (se ja foi
        // enviado enfileira o valor menor; ver tempo::DestinoDesfazer), anula o evento no log e salva. false = nao da mais.
        bool desfazerHabito(uint32_t nowMs, fs::FS* filesystem);
        bool podeDesfazerHabito() const { return carregado_ && tempo::podeDesfazerHabito(estado_, desfazer_); }
        // true uma vez depois de um check-in novo entrar na fila (o App dispara a sync do TickTick). Depois de um "Feito" da
        // tela, so liberado quando a janela do "Desfazer" fecha (nao envia um check-in que o usuario ainda pode desfazer).
        bool pegarCheckinNovo(uint32_t nowMs) {
            if (!checkinNovo_ || static_cast<int32_t>(nowMs - checkinLiberaEmMs_) < 0) return false;
            checkinNovo_ = false;
            return true;
        }
        // "Pular": sem cr/XP, nao entra na fila, nao conta como esquecido (tempo::pularOcorrencia). Devolve false se ja resolvida/sem SD.
        bool pularHabito(uint32_t nowMs, fs::FS* filesystem, size_t indice);
        // "Ainda nao" (medicacao): o lembrete volta em 15 min (nao grava nada: so a agenda de lembretes em RAM).
        void adiarHabito(uint32_t nowMs, size_t indice);
        // Manutencao dos Habitos, FORA do desenho (App::update, ~1x/s): avanca o relogio do dia, na virada do dia e a cada
        // minuto reconcilia com habitos.json + check-ins do celular (atualizarHabitos). Nao roda durante batalha/reflexao.
        // So relê arquivos quando a sync terminou (versao mudou), na virada do dia ou se a tela de Habitos abriu (tempo::deveReler).
        void manutencaoHabitos(uint32_t nowMs, fs::FS* filesystem, uint32_t versaoSync);
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
            return static_cast<uint8_t>(static_cast<uint8_t>(fase_) * 4 + (cron_.distraido ? 1 : 0) + (overtime_ ? 2 : 0)
                                        + (seletor_ ? 32 + (pagina_ & 1U) * 64 : 0));
        }

    private:
        enum class Fase : uint8_t { Nenhuma, Lutando, Pausada, Descanso, Reflexao };

        void carregar(fs::FS* filesystem, bool recarregarEstado, uint32_t nowMs);
        void comecarSessao(uint32_t nowMs);                    // inicia a batalha (ou o ciclo) com a escolha do Inicio
        void ligarBatalha(uint32_t nowMs);                     // zera contadores e entra em Lutando
        void concluirRodada(uint32_t nowMs, fs::FS* filesystem);   // ciclo: liquida a rodada e entra na pausa
        void salvarPreferencias();
        void escolher(int indice);                              // -1 = Foco livre (a categoria livre fica em categoria_)
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
        double progressoFim(bool apertado, uint32_t nowMs);          // "Fim · segure": 0..1 enchendo, -1 = dedo fora
        void encerrarPeloFim(uint32_t nowMs, Screen& screen);
        Action drawInicio(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawSeletor(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawBatalha(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawVitoria(ui::Context& ui, uint32_t nowMs, Screen& screen);

        bool checkinNovo_ = false;
        uint32_t checkinLiberaEmMs_ = 0;
        tempo::InstanteHabito desfazer_;     // ultimo Feito/Pular (Habitos v2)
        bool carregado_ = false;
        uint32_t ultimoDesenhoMs_ = 0;
        fs::FS* fs_ = nullptr;
        tempo::Estado estado_;
        tempo::ListaProjetos lista_;
        tempo::ListaHabitos habitos_;        // definicoes dos habitos (habitos.json)
        tempo::AgendaLembretes lembretes_;   // estado dos lembretes de habitos (so RAM)
        uint32_t versaoVista_ = 0;           // ultima versao da sync (net::ticktick::versao()) ja aplicada
        bool recargaPedida_ = false;         // tela de Habitos abriu / releitura que ficou pendente (sync ativa)
        bool syncAtiva_ = false;             // job do TickTick gravando arquivos: nao ler
        uint32_t manutencaoRelogioMs_ = 0;
        std::vector<std::string> titulos_;   // titulos limpos (sem emoji), 1:1 com lista_.projetos
        size_t pagina_ = 0;                  // pagina do seletor (0 = categorias do Foco livre + 2 projetos recentes)
        bool pedidoSync_ = false;            // "Sincronizar" tocado no seletor vazio
        bool fimApertado_ = false;           // dedo em "Fim · segure"
        uint32_t fimInicioMs_ = 0;           // quando o dedo apoiou em "Fim · segure"
        uint32_t fimDicaAteMs_ = 0;          // "segure para encerrar" visivel ate (apos toque curto)
        uint64_t pausaInicioMs_ = 0;         // inicio da pausa em curso (Pausar)
        uint32_t sincPedidaMs_ = 0;          // "Sincronizar" tocado ha pouco (0 = nao)
        bool seletor_ = false;               // seletor de foco aberto (Screen::TempoHoje)
        int escolha_ = -1;                   // foco escolhido no Inicio: -1 = Foco livre, senao indice do projeto
        tempo::Categoria categoria_ = tempo::Categoria::Trabalho;   // categoria do FOCO LIVRE (nunca Projetos; com projeto a categoria e unica)
        int duracaoMin_ = 25;                // duracao do anel do Inicio (5..60)
        bool pomodoro_ = false;              // interruptor do ciclo
        tempo::Ciclo ciclo_;                 // ciclo em curso (ativo = false: sessao unica)
        int64_t descansoMs_ = 5 * 60 * 1000;
        tempo::Categoria categoriaBatalha_ = tempo::Categoria::Trabalho;   // da batalha em curso
        std::string nomeBatalha_;            // "Foco livre · Estudo" ou titulo do projeto

        Fase fase_ = Fase::Nenhuma;
        int projeto_ = -1;                   // projeto da batalha em curso (-1 = Foco livre)
        int planejadoMin_ = 25;
        uint64_t ultimoMs_ = 0;
        tempo::Cronometro cron_;             // foco / distracao / pausa (regras das decisoes 18 e 19; src/tempo/Cronometro.h)
        bool overtime_ = false;              // inimigo derrotado, seguindo no bonus
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
            bool subiuNivel = false;     // o XP desta liquidacao subiu o nivel
            bool conquista = false;      // ganhou uma conquista nova
        } resultado_;
        std::array<som::SomId, 4> sons_{};
        size_t nSons_ = 0;
    };

} // namespace screens
