#pragma once

#include <FS.h>

#include <algorithm>
#include <array>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "tempo/Cartao.h"
#include "tempo/CenaViva.h"
#include "tempo/Codec.h"
#include "tempo/Cronometro.h"
#include "tempo/Fechamento.h"
#include "tempo/FocoLeitor.h"
#include "tempo/Habitos.h"
#include "tempo/Livre.h"
#include "tempo/Lembretes.h"
#include "tempo/Luta.h"
#include "tempo/ModoFoco.h"
#include "tempo/Motor.h"
#include "tempo/Recarga.h"
#include "tempo/Regua.h"
#include "tempo/Relogio.h"
#include "som/Sintese.h"
#include "tempo/Sessao.h"
#include "ui/luta/FaixaPsram.h"
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

    // Anel suavizado com trilho de cor fixa (ex.: cinza do Habitos no launcher; contorno dos rostos = frac 1, cor unica).
    // raio/larg em pixels (podem ser fracionarios). Cores em RGB565.
    void desenharAnelSuave(Arduino_GFX& g, int cx, int cy, float raio, float larg, double frac, uint16_t cor0,
                           uint16_t cor1, uint16_t fundo, uint16_t trilho);

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
        // Erro da ultima sincronizacao do TickTick ("" = deu certo), ex.: "lista PROJETOS não encontrada"; aparece no "Trocar ›" sem projetos.
        void definirErroSync(std::string erro) { erroSync_ = std::move(erro); }

        // Atividade do usuario (toque na tela ou botao fisico, inclusive o que so acorda o Modo Foco): zera os 90 min do aviso
        // "ainda focando?" de qualquer sessao rodando (Timer, hora extra, livre). Com o aviso aberto, o toque na tela e
        // resolvido pelo desenho (para nao apertar o botao que estava sob o dedo); o botao fisico, por boot()/back().
        void atividade(uint32_t nowMs, bool toque, int x = -1, int y = -1);
        // Sessao livre (parada ∞): conta para cima, sem meta. Aviso de 90 min sem toque aberto.
        bool sessaoLivre() const { return livre_; }
        bool avisoOciosoAberto() const { return ocio_.ativo(); }
        // Avanca a batalha (acumula tempo), entra em overtime quando o inimigo zera (sem teto: segue ate o Concluir),
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
            return static_cast<uint8_t>((static_cast<uint8_t>(fase_) + (saqueAtivo_ ? 5 : 0)) * 4 + (cron_.distraido ? 1 : 0)
                                        + (overtime_ ? 2 : 0) + (distracaoAberta_ ? 160 : 0)
                                        + (seletor_ ? 32 + (pagina_ & 1U) * 64 : 0)) ^ (livre_ ? 0x15U : 0U) ^ (ocio_.ativo() ? 0x2AU : 0U)
                   ^ (saqueLivre_ ? 0x51U : 0U);
        }
        // Reflexao aberta ou saque com a janela do "Desfazer" ainda aberta (nada gravado no log/resumo): o estado nao pode
        // mudar por fora (sync de habitos, manutencao) nem comecar outra batalha.
        bool emFechamento() const { return fase_ == Fase::Reflexao || (saqueAtivo_ && !confirmado_); }
        // Modo Foco (etapa 0): a fase da batalha vista pelo escurecimento (tempo::modofoco) e o "Fim · segure" em andamento.
        tempo::modofoco::Fase faseModoFoco() const {
            using F = tempo::modofoco::Fase;
            if (saqueAtivo_ || fase_ == Fase::Reflexao || ocio_.ativo()) return F::Fechamento;   // aviso 90 min: brilho normal
            switch (fase_) {
            case Fase::Lutando: return overtime_ ? F::HoraExtra : cron_.distraido ? F::Distraido : F::Focando;
            case Fase::Pausada: return F::Pausado;
            case Fase::Descanso: return F::PausaCiclo;
            default: return F::Nenhuma;
            }
        }
        bool segurandoFim() const { return fimApertado_; }
        // Batalha em cabo de guerra (etapa c): o App diz se a animacao pode correr (tela clara, batalha na frente, placa deitada;
        // tempo::luta::deveAnimar) e devolve a PSRAM da faixa quando a tela sai da batalha.
        void definirAnimacao(bool animar) { animarLuta_ = animar; }
        void liberarAnimacao() { luta_.liberar(); }
        // Modo Foco, cenas QG (tempo::cenafoco): progresso do foco (0..1, sem contar o overtime), duracao planejada e foco
        // pago HOJE antes desta sessao (as sessoes anteriores do dia; a batalha em curso so entra ao liquidar). Data do
        // dia diferente de hoje (ex.: a batalha passou da meia-noite) = 0.
        // Livre: a cena trata como 60 min (12 nos de 5 min, ate o maximo de 12; sem vitoria porque nao ha inimigo).
        double progressoFoco() const {
            const double plano = double(planejadoMin()) * 60000.0;
            return plano > 0 ? std::min(1.0, double(cron_.focadoMs) / plano) : 0.0;
        }
        int planejadoMin() const { return livre_ ? 60 : planejadoMin_; }
        int focoPagoHojeMin(uint32_t nowMs) const {
            const auto agora = tempo::agora(nowMs);
            if (!carregado_ || (agora.valida && !(estado_.dia.data == agora.data))) return 0;
            return std::max(0, estado_.dia.fochoPagoMin);
        }
        // Modo Foco v2: dados do relogio da cena (tempo::cenaviva::relogio). Vitoria = o App (fundo da cena).
        tempo::cenaviva::EntradaRelogio entradaRelogioCena() const {
            tempo::cenaviva::EntradaRelogio e;
            e.livre = livre_;
            e.horaExtra = overtime_;
            e.restanteMs = restanteMs();
            e.overtimeMs = overtimeMs();
            e.focadoMs = cron_.focadoMs;
            e.planoMs = int64_t(planejadoMin()) * 60000;
            return e;
        }

    private:
        enum class Fase : uint8_t { Nenhuma, Lutando, Pausada, Descanso, Reflexao };

        void carregar(fs::FS* filesystem, bool recarregarEstado, uint32_t nowMs);
        void comecarSessao(uint32_t nowMs);                    // inicia a batalha (ou o ciclo) com a escolha do Inicio
        void ligarBatalha(uint32_t nowMs, bool livre = false); // zera contadores e entra em Lutando (livre = parada ∞)
        // ---- Livre (decisao 23) e aviso de 90 min (decisoes 23/24)
        void finalizarLivre(uint32_t nowMs, fs::FS* filesystem, bool tocarDesfecho = true);   // < 1 min: nao registra; senao liquida e abre o saque
        void concluirPorOcio(uint32_t nowMs, fs::FS* filesystem);   // 5 min sem resposta: conclui pagando so ate a hora do aviso
        void continuarOcio(uint32_t nowMs);
        Action drawLivre(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawAvisoOcio(ui::Context& ui, uint32_t nowMs, Screen& screen);
        void concluirRodada(uint32_t nowMs, fs::FS* filesystem);   // ciclo: liquida a rodada e entra na pausa
        void salvarPreferencias();
        void escolher(int indice);                              // projeto do TickTick (indice em lista_); lembra o id
        void escolherCategoria(tempo::Categoria categoria);     // "sem projeto" nessa categoria; esquece o projeto lembrado
        void pausarOuRetomar(uint32_t nowMs);
        void alternarDistracao(uint32_t nowMs);
        void acumular(uint32_t nowMs);
        void finalizar(uint32_t nowMs, bool abandonou, fs::FS* filesystem, bool tocarDesfecho = true);
        void iniciarDescanso(uint32_t nowMs, bool voltaParaBatalha);
        void encerrarDescanso(uint32_t nowMs);
        // ---- Fechamento (Tempo v3 etapa 2): liquida -> [Reflexao] -> saque (+ Desfazer 8 s) -> proximo passo
        void liquidar(uint32_t nowMs, fs::FS* filesystem, bool tocarDesfecho);   // aplica a batalha e guarda o instante do Desfazer
        void fecharReflexao(uint32_t nowMs, fs::FS* filesystem, bool mostrarSaque, bool deReflexao);   // humor_/distraiu_ ja decididos
        void confirmarSaque(fs::FS* filesystem);                // grava log do dia + resumo.json + check-in da Meditacao (ponto sem volta)
        void iniciarProximoPasso(uint32_t nowMs);               // pausa do ciclo (relogio desde o fim da rodada) ou nada
        bool desfazerBatalha(uint32_t nowMs, fs::FS* filesystem, Screen& screen);
        bool podeDesfazerBatalha(uint32_t nowMs) const;
        void acaoPrimariaSaque(uint32_t nowMs, Screen& screen, fs::FS* filesystem);   // BOOT / botao primario do saque
        void sairDoSaque(uint32_t nowMs, fs::FS* filesystem);  // confirma e segue o proximo passo (pausa) sem mudar de tela
        void garantirDia(uint32_t nowMs);
        void registrarHabitoMeditacao(fs::FS* filesystem);     // batalha de Meditacao vencida: marca o habito
        void gravarEventoHabito(fs::FS* filesystem, const tempo::ResultadoOcorrencia& r, std::string_view origem);
        // Grava o estado, a menos que o cartao esteja sem ler (tempo/Cartao.h): nunca grava estado zerado por cima.
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
        Action drawVitoria(ui::Context& ui, uint32_t nowMs, Screen& screen);   // Reflexao ou Saque
        Action drawReflexao(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawDistracoes(ui::Context& ui, uint32_t nowMs, Screen& screen);
        Action drawSaque(ui::Context& ui, uint32_t nowMs, Screen& screen);

        // livre / aviso de 90 min
        bool livre_ = false;                   // a sessao em curso e livre (conta para cima, sem meta)
        bool saqueLivre_ = false;              // o saque no ar e de uma sessao livre (Desfazer volta a contar)
        tempo::Cronometro cronLivreSalvo_;     // cronometro no instante do Concluir (o Desfazer volta daqui)
        tempo::livre::AvisoOcioso ocio_;       // 90 min sem toque -> "ainda focando?"; 5 min -> conclui
        uint64_t focadoNoAvisoMs_ = 0;         // foco no instante do aviso (paga so ate aqui se ninguem responder)
        int inicioMinutoDia_ = -1;             // hora de parede do inicio da sessao ("DESDE 14:20"), -1 = sem relogio
        int avisoMinutoDia_ = -1;              // hora de parede do aviso (minuto do dia), -1 = sem relogio
        uint32_t avisoDesenhadoMs_ = 0;        // ultimo quadro com o aviso na tela (0 = nunca)
        bool toqueNovo_ = false;               // toque comecou com o aviso aberto (resolvido no desenho)
        int toqueX_ = -1, toqueY_ = -1;        // onde o toque novo comecou
        int evOcio_ = 0;                       // evento do aviso pendente do acumular(): 0 nada, 1 avisar, 2 concluir
        uint32_t naoContaAteMs_ = 0;           // "0:40 não conta" visivel no Inicio ate (0 = nao)
        std::string naoContaTxt_;
        int duracaoTimerMin_ = 25;             // ultima duracao de TIMER (o BOOT do launcher sempre comeca um Timer)
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
        size_t pagina_ = 0;                  // pagina do "Trocar ›" (2 projetos por pagina; setas so com mais de 2)
        std::string erroSync_;               // erro da ultima sync do TickTick (aparece no "Trocar ›" sem projetos)
        // Cartao SD (revisao do Fable): sem cartao / estado ilegivel (nao grava por cima) / falha ao gravar. Aviso ambar no
        // cartao do foco do Inicio enquanto nao estiver Ok.
        tempo::cartao::Situacao cartao_ = tempo::cartao::Situacao::Ok;
        bool projetoSaiu_ = false;           // o projeto lembrado saiu da coluna AGORA (aviso ambar no cartao ate a proxima escolha/sessao)
        std::string projetoBatalhaId_;       // id do TickTick do projeto da batalha em curso ("" = sem projeto)
        bool pedidoSync_ = false;            // "Sincronizar" tocado no seletor vazio
        bool fimApertado_ = false;           // dedo em "Fim · segure"
        uint32_t fimInicioMs_ = 0;           // quando o dedo apoiou em "Fim · segure"
        uint32_t fimEncerraEmMs_ = 0;        // 100 % em "Fim · segure": instante em que encerra de fato (0 = nao agendado)
        uint32_t fimDicaAteMs_ = 0;          // "segure para encerrar" visivel ate (apos toque curto)
        uint64_t pausaInicioMs_ = 0;         // inicio da pausa em curso (Pausar)
        uint32_t sincPedidaMs_ = 0;          // "Sincronizar" tocado ha pouco (0 = nao)
        bool seletor_ = false;               // seletor de foco aberto (Screen::TempoHoje)
        int escolha_ = -1;                   // foco escolhido no Inicio: -1 = sem projeto, senao indice do projeto (re-resolvido pelo id em Estado::projetoId a cada recarga)
        tempo::Categoria categoria_ = tempo::Categoria::Trabalho;   // categoria do FOCO LIVRE (nunca Projetos; com projeto a categoria e unica)
        int duracaoMin_ = 25;                // duracao da regua do Inicio (5..90, multiplo de 5)
        bool pomodoro_ = false;              // "4x rodadas": repete a rodada escolhida 4 vezes (pausa = tempo::pausaDoCiclo)
        tempo::regua::Arrasto reguaArrasto_; // dedo sobre a regua do Inicio (fita 1:1 + encaixe de 5 em 5)
        tempo::Ciclo ciclo_;                 // ciclo em curso (ativo = false: sessao unica)
        int64_t descansoMs_ = 5 * 60 * 1000;
        tempo::Categoria categoriaBatalha_ = tempo::Categoria::Trabalho;   // da batalha em curso
        std::string nomeBatalha_;            // "Estudo · sem projeto" ou titulo do projeto

        Fase fase_ = Fase::Nenhuma;
        int projeto_ = -1;                   // projeto da batalha em curso (-1 = Foco livre)
        int planejadoMin_ = 25;
        uint64_t ultimoMs_ = 0;
        tempo::Cronometro cron_;             // foco / distracao / pausa (regras das decisoes 18 e 19; src/tempo/Cronometro.h)
        bool overtime_ = false;              // inimigo derrotado, seguindo no bonus
        bool golpeVisivel_ = false;          // golpe final em curso/recente (inicio em golpeDesdeMs_, relogio do laco)
        uint32_t golpeDesdeMs_ = 0;          // nowMs em que o inimigo zerou (tick)
        bool poeiraPronta_ = false;          // particulas do golpe final geradas
        bool animarLuta_ = true;             // vem do App (tempo::luta::deveAnimar)
        ui::luta::FaixaPsram luta_;          // memoria (PSRAM) da faixa animada da batalha
        int ultimoBipeFocoMin_ = 0;          // ultimo multiplo de 10 min de foco ja avisado
        bool avisoFimDado_ = false;
        bool arrastando_ = false;            // dedo no anel do preparo
        uint64_t descansoInicioMs_ = 0;
        bool descansoVolta_ = false;         // pausa veio do overtime: volta para a batalha (pausada)

        // reflexao
        uint32_t reflexaoInicioMs_ = 0;
        int humor_ = -1;                     // 0..4 (Pessimo..Otimo), -1 = sem resposta
        int distraiu_ = -1;                  // correcao opcional no chip "distracoes": 0 Nao, 1 Um pouco, 2 Muito, -1 = nao corrigida
        bool distracaoAberta_ = false;       // sub-tela Nao / Um pouco / Muito
        tempo::EntradaBatalha entradaPend_;     // entrada da batalha em fechamento (para re-liquidar apos o Desfazer)
        bool liquidado_ = false;             // a economia da batalha em fechamento esta aplicada (false apos o Desfazer)
        bool saqueAtivo_ = false;            // tela de saque no ar (Screen::TempoVitoria)
        bool confirmado_ = true;             // log/resumo/check-in ja gravados (sem volta)
        bool saqueDeReflexao_ = false;       // o Desfazer volta a Reflexao (senao: ao Inicio, ciclo encerrado)
        uint32_t saqueInicioMs_ = 0;         // inicio da janela de 8 s do Desfazer (e ancora da pausa do ciclo)
        tempo::ProximoPasso proximo_ = tempo::ProximoPasso::VoltarInicio;
        bool emCiclo_ = false;               // a batalha em fechamento veio de um ciclo 4x
        int cicloRodadas_ = 0;               // rodadas do ciclo ja liquidadas (para "ciclo de N rodadas")
        int cicloFocadoMin_ = 0;
        int distracoesPend_ = 0;             // distracoes marcadas na hora (chip)
        tempo::InstanteBatalha instante_;    // estado antes/depois da liquidacao
        std::vector<tempo::ItemFila> filaMed_;           // check-in da Meditacao por timer, enviado so ao confirmar
        std::optional<tempo::EventoLog> eventoMedPend_;  // evento de habito da Meditacao, gravado so ao confirmar
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
