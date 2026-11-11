#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <string>

#include "tempo/Codec.h"
#include "tempo/Cronometro.h"
#include "tempo/FocoLeitor.h"
#include "tempo/Motor.h"
#include "tempo/Parametros.h"
#include "tempo/Perfil.h"
#include "tempo/Progressao.h"
#include "tempo/Relogio.h"
#include "tempo/Sessao.h"

using namespace tempo;

// Penalidades LIGADAS explicitamente: estes testes cobrem as regras de punicao (o padrao do produto e desligado; ver
// param::kPenalidadesAtivas e os testes "penalidades_desligadas_*").
void setUp() { tempo::definirPenalidadesParaTeste(true); }

void tearDown() { tempo::definirPenalidadesParaTeste(tempo::param::kPenalidadesAtivas); }

namespace {

    // Unity foi compilado sem suporte a double: comparacao propria com tolerancia 1e-6.
    void nearImpl(double esperado, double atual, int linha) {
        char msg[96];
        std::snprintf(msg, sizeof msg, "linha %d: esperado %.6f, atual %.6f", linha, esperado, atual);
        TEST_ASSERT_TRUE_MESSAGE(std::fabs(esperado - atual) <= 1e-6, msg);
    }
#define NEAR(esperado, atual) nearImpl((esperado), (atual), __LINE__)

    Data D(int ano, int mes, int dia) {
        Data d;
        d.ano = ano;
        d.mes = mes;
        d.dia = dia;
        return d;
    }

    const Data kSegunda = {2026, 11, 9};

    Estado novoEstado(const Data& data = kSegunda) {
        Estado e;
        abrirDia(e, data);
        return e;
    }

    EntradaBatalha batalha(Categoria c, int plan, int focados, int distraidos = 0, int numDist = 0) {
        EntradaBatalha b;
        b.categoria = c;
        b.minutosPlanejados = plan;
        b.minutosFocados = focados;
        b.minutosDistraidos = distraidos;
        b.numDistracoes = numDist;
        b.maiorDistracaoMin = distraidos;
        return b;
    }

    EntradaBatalha perfeita(Categoria c, int min) {
        return batalha(c, min, min);
    }
    // vitoria com 1 distracao de 2 min (cura 2 HP, sem bonus, sem Perdido)
    EntradaBatalha comDistracao(Categoria c, int min) {
        return batalha(c, min, min + 2, 2, 1);
    }

    void habitoNoHorario(Estado& e, Habito h) {
        registrarHabito(e, h, 600, 600);
    }

    // ---------------------------------------------------------------- datas
    void test_datas() {
        TEST_ASSERT_EQUAL_INT64(0, diaSerial(D(1970, 1, 1)));
        TEST_ASSERT_EQUAL_INT(0, diaDaSemana(kSegunda));  // 2026-11-09 e segunda
        TEST_ASSERT_EQUAL_INT(6, diaDaSemana(D(2026, 11, 15)));
        Data d = dataDeSerial(diaSerial(D(2028, 2, 29)) + 1);
        TEST_ASSERT_EQUAL_INT(3, d.mes);
        TEST_ASSERT_EQUAL_INT(1, d.dia);
        TEST_ASSERT_EQUAL_INT64(diaSerial(kSegunda), segundaDaSemana(D(2026, 11, 13)));
    }

    // ---------------------------------------------------------------- curva de XP
    void test_curva_xp() {
        TEST_ASSERT_EQUAL_INT(60, xpProximo(1));
        TEST_ASSERT_EQUAL_INT(170, xpProximo(2));
        TEST_ASSERT_EQUAL_INT(310, xpProximo(3));
        TEST_ASSERT_EQUAL_INT64(230, xpAcumulado(3));
        TEST_ASSERT_EQUAL_INT64(6660, xpAcumulado(10));
        TEST_ASSERT_EQUAL_INT64(40280, xpAcumulado(20));
        TEST_ASSERT_EQUAL_INT64(51420, xpAcumulado(22));
        TEST_ASSERT_EQUAL_INT64(113410, xpAcumulado(30));
        TEST_ASSERT_EQUAL_INT(1, nivelDeXp(0));
        TEST_ASSERT_EQUAL_INT(1, nivelDeXp(59.9));
        TEST_ASSERT_EQUAL_INT(2, nivelDeXp(60));
        TEST_ASSERT_EQUAL_INT(9, nivelDeXp(6659));
        TEST_ASSERT_EQUAL_INT(10, nivelDeXp(6660));
        TEST_ASSERT_EQUAL_INT(30, nivelDeXp(1e9));
    }

    void test_atributos() {
        TEST_ASSERT_EQUAL_INT(0, nivelAtributo(Atributo::Oficio, 149));
        TEST_ASSERT_EQUAL_INT(1, nivelAtributo(Atributo::Oficio, 150));
        TEST_ASSERT_EQUAL_INT(10, nivelAtributo(Atributo::Oficio, 15000));
        TEST_ASSERT_EQUAL_INT(1, nivelAtributo(Atributo::Mente, 60));
        TEST_ASSERT_EQUAL_INT(1, nivelAtributo(Atributo::Corpo, 30));
        TEST_ASSERT_EQUAL_INT(2, nivelAtributo(Atributo::Energia, 80));
        TEST_ASSERT_EQUAL_INT(20, nivelAtributo(Atributo::Energia, 1e9));  // teto 20
        Estado e;
        TEST_ASSERT_EQUAL_INT(10, toleranciaTransicaoMin(e));
        e.pontosAtributo[static_cast<size_t>(Atributo::Energia)] = 20.0 * 16;  // nivel 4
        TEST_ASSERT_EQUAL_INT(11, toleranciaTransicaoMin(e));
        e.pontosAtributo[static_cast<size_t>(Atributo::Corpo)] = 30.0 * 100;  // nivel 10
        TEST_ASSERT_EQUAL_INT(11, coracoesMaxMeios(e));
    }

    // ------------------------------------------------------------- dias-tipo
    void test_dia_bom() {
        Estado e = novoEstado();
        e.coracoesMeios = 8;  // abaixo do teto para observar a recuperacao
        for (int i = 0; i < 6; ++i) aplicarBatalha(e, perfeita(Categoria::Projetos, 30));
        for (int i = 0; i < 2; ++i) aplicarBatalha(e, comDistracao(Categoria::Projetos, 30));
        aplicarBatalha(e, perfeita(Categoria::Leitura, 20));
        aplicarBatalha(e, perfeita(Categoria::Meditacao, 15));
        for (size_t h = 0; h < kNumHabitos; ++h) habitoNoHorario(e, static_cast<Habito>(h));
        registrarLazer(e, 60);
        registrarLazer(e, 30);
        for (int i = 0; i < 3; ++i) registrarHumor(e);
        ResumoDia r = fecharDia(e, true);
        NEAR(150.75, r.crGanho);
        NEAR(90.0, r.crGasto);
        NEAR(60.75, r.saldoDoDia);
        NEAR(60.75, e.saldo);
        NEAR(466.0, r.xpGanho);
        TEST_ASSERT_TRUE(r.valido);
        TEST_ASSERT_TRUE(r.limpo);
        TEST_ASSERT_EQUAL_INT(2, r.coracoesDeltaMeios);  // +1 coracao (habitos >= 80 % + dia limpo)
        TEST_ASSERT_EQUAL_INT(10, e.coracoesMeios);
        TEST_ASSERT_EQUAL_INT(1, e.streak);
    }

    void test_dia_medio() {
        Estado e = novoEstado();
        e.coracoesMeios = 8;
        aplicarBatalha(e, perfeita(Categoria::Trabalho, 30));
        for (int i = 0; i < 2; ++i) aplicarBatalha(e, comDistracao(Categoria::Trabalho, 30));
        for (int i = 0; i < 2; ++i) aplicarBatalha(e, perfeita(Categoria::Estudo, 30));
        aplicarBatalha(e, perfeita(Categoria::Leitura, 15));
        habitoNoHorario(e, Habito::Suplemento);
        habitoNoHorario(e, Habito::Banho);
        habitoNoHorario(e, Habito::Dentes);
        habitoNoHorario(e, Habito::Descanso);
        habitoNoHorario(e, Habito::Medicacao);
        ResultadoHabito almoco = registrarHabito(e, Habito::Almoco, 720, 720 + 45 + 30);  // atrasado
        TEST_ASSERT_EQUAL_INT(static_cast<int>(StatusHabito::Atrasado), static_cast<int>(almoco.status));
        registrarLazer(e, 60);
        registrarPerdido(e, 15);  // gratis (<= 30 min/dia)
        for (int i = 0; i < 3; ++i) registrarHumor(e);
        ResumoDia r = fecharDia(e, true);
        NEAR(64.25, r.crGanho);   // 53,25 foco + 11 habitos
        NEAR(60.0, r.crGasto);
        NEAR(4.25, e.saldo);
        // economia.md diz 277,6 (214,1 de foco); com a atribuicao de perfeitas que reproduz +4,25 em cr
        // (1 perfeita de Trabalho + 2 de Estudo) o foco rende 215,625 -> total 279,125.
        NEAR(279.125, r.xpGanho);
        TEST_ASSERT_TRUE(r.limpo);
        TEST_ASSERT_FALSE(r.valido == false);  // 5/7 no horario >= 4 e foco >= 50
        TEST_ASSERT_EQUAL_INT(1, r.coracoesDeltaMeios);  // so dia limpo (+1/2); 5/7 < 80 %
    }

    void test_dia_ruim() {
        Estado e = novoEstado();
        for (int i = 0; i < 3; ++i) aplicarBatalha(e, comDistracao(Categoria::Trabalho, 30));
        habitoNoHorario(e, Habito::Banho);
        habitoNoHorario(e, Habito::Dentes);
        registrarHabito(e, Habito::Medicacao, 600, 600 + 30 + 60);  // atrasada: -1 meio
        registrarLazer(e, 120);  // Navegar
        TEST_ASSERT_EQUAL_size_t(1, e.desafiosPendentes.size());  // cruzou -60
        registrarPerdido(e, 20);  // gratis
        registrarHumor(e);
        registrarHumor(e);
        ResumoDia r = fecharDia(e, false);
        NEAR(22.0, r.crGanho);
        NEAR(120.0, r.crGasto);
        NEAR(-98.0, e.saldo);
        NEAR(109.0, r.xpGanho);
        TEST_ASSERT_FALSE(r.limpo);
        TEST_ASSERT_TRUE(r.fechamentoNegativo);
        // medicacao atrasada -1 meio, desafio -2 meios
        TEST_ASSERT_EQUAL_INT(7, e.coracoesMeios);
    }

    // --------------------------------------------------------------- perdido
    void test_perdido() {
        Estado a = novoEstado();
        NEAR(0.0, registrarPerdido(a, 20));
        NEAR(0.0, a.saldo);
        Estado b = novoEstado();
        NEAR(20.0, registrarPerdido(b, 50));
        NEAR(-20.0, b.saldo);
        // incremental: 20 + 30 = 50 -> total -20; mais 10 -> -10 a mais
        Estado c = novoEstado();
        registrarPerdido(c, 20);
        registrarPerdido(c, 30);
        NEAR(-20.0, c.saldo);
        registrarPerdido(c, 10);
        NEAR(-30.0, c.saldo);
    }

    void test_transicao() {
        Estado e = novoEstado(kSegunda);
        abrirDia(e, kSegunda, 20);  // orcamento de descanso 20 min
        ResultadoTransicao t = registrarTransicao(e, 8, std::nullopt);  // dentro da tolerancia
        TEST_ASSERT_EQUAL_INT(0, t.excessoMin);
        t = registrarTransicao(e, 40, ClasseIntervalo::Descanso);  // excesso 30; 20 de descanso, 10 pendentes
        TEST_ASSERT_EQUAL_INT(20, t.descansoMin);
        TEST_ASSERT_EQUAL_INT(10, t.pendenteMin);
        t = classificarTransicaoPendente(e, ClasseIntervalo::Ok, 10);
        TEST_ASSERT_EQUAL_INT(10, t.okMin);
        TEST_ASSERT_EQUAL_INT(0, e.dia.transicaoPendenteMin);
        registrarTransicao(e, 100, std::nullopt);  // 90 pendentes
        ResumoDia r = fecharDia(e, false);
        TEST_ASSERT_EQUAL_INT(90, r.perdidoMin);  // nao classificado vira Perdido
        NEAR(-60.0, e.saldo);  // 90 - 30 gratis
    }

    // ------------------------------------------------------------------ batalha
    void test_batalha_perfeita_vs_distracao() {
        Estado e = novoEstado();
        LiquidacaoBatalha p = liquidarBatalha(e, perfeita(Categoria::Projetos, 30));
        TEST_ASSERT_TRUE(p.vitoria);
        TEST_ASSERT_TRUE(p.perfeita);
        NEAR(15.0, p.cr);
        NEAR(37.5, p.xp);
        LiquidacaoBatalha d = liquidarBatalha(e, comDistracao(Categoria::Projetos, 30));
        TEST_ASSERT_TRUE(d.vitoria);
        TEST_ASSERT_FALSE(d.perfeita);
        TEST_ASSERT_EQUAL_INT(2, d.curaMin);
        TEST_ASSERT_EQUAL_INT(0, d.perdidoMin);  // 2 min nao passa de 2
        NEAR(12.0, d.cr);
        NEAR(30.0, d.xp);
        // sem tempo extra para zerar o HP curado = nao vence
        LiquidacaoBatalha n = liquidarBatalha(e, batalha(Categoria::Projetos, 30, 30, 2, 1));
        TEST_ASSERT_FALSE(n.vitoria);
        TEST_ASSERT_TRUE(n.fuga);
        // distracao > 2 min vira Perdido
        LiquidacaoBatalha g = liquidarBatalha(e, batalha(Categoria::Projetos, 30, 35, 5, 1));
        TEST_ASSERT_TRUE(g.vitoria);
        TEST_ASSERT_EQUAL_INT(5, g.perdidoMin);
    }

    void test_batalha_curta_sem_bonus() {
        Estado e = novoEstado();
        LiquidacaoBatalha b = liquidarBatalha(e, perfeita(Categoria::Projetos, 10));
        TEST_ASSERT_TRUE(b.vitoria);
        TEST_ASSERT_FALSE(b.perfeita);
        NEAR(4.0, b.cr);
        NEAR(10.0, b.xp);
        LiquidacaoBatalha l = liquidarBatalha(e, perfeita(Categoria::Leitura, 14));
        TEST_ASSERT_FALSE(l.perfeita);
        LiquidacaoBatalha ok = liquidarBatalha(e, perfeita(Categoria::Leitura, 15));
        TEST_ASSERT_TRUE(ok.perfeita);
    }

    void test_batalha_overtime_e_fuga() {
        Estado e = novoEstado();
        LiquidacaoBatalha o = liquidarBatalha(e, batalha(Categoria::Projetos, 30, 90));
        TEST_ASSERT_EQUAL_INT(30, o.minutosOvertime);  // limitado a +100 %
        TEST_ASSERT_EQUAL_INT(60, o.minutosPagos);
        NEAR(60 * 0.4 * 1.25, o.cr);  // perfeita (0 distracoes)
        LiquidacaoBatalha o2 = liquidarBatalha(e, batalha(Categoria::Projetos, 30, 45));
        TEST_ASSERT_EQUAL_INT(15, o2.minutosOvertime);
        // fuga: > 3 distracoes -> paga focados, sem bonus
        EntradaBatalha f = batalha(Categoria::Projetos, 30, 30, 4, 4);
        f.maiorDistracaoMin = 1;
        LiquidacaoBatalha lf = liquidarBatalha(e, f);
        TEST_ASSERT_TRUE(lf.fuga);
        TEST_ASSERT_FALSE(lf.perfeita);
        NEAR(30 * 0.4, lf.cr);
        // distracao longa (> 10 min) tambem e fuga; abandono nunca fica negativo
        TEST_ASSERT_TRUE(liquidarBatalha(e, batalha(Categoria::Projetos, 30, 50, 11, 1)).fuga);
        EntradaBatalha ab = batalha(Categoria::Estudo, 30, 12);
        ab.abandonou = true;
        LiquidacaoBatalha la = liquidarBatalha(e, ab);
        TEST_ASSERT_TRUE(la.fuga);
        NEAR(12 * 0.4, la.cr);
        // fuga nao conta para dia valido
        aplicarBatalha(e, ab);
        TEST_ASSERT_EQUAL_INT(0, e.dia.fochoValidoMin);
    }

    void test_batalha_teto_diario() {
        Estado e = novoEstado();
        e.dia.fochoPagoMin = 350;
        LiquidacaoBatalha b = liquidarBatalha(e, batalha(Categoria::Projetos, 30, 32, 2, 1));
        // 10 min cheios + 20 min a 0,5 -> 12 * (20/30) = 8
        NEAR(8.0, b.cr);
        TEST_ASSERT_TRUE(b.tetoCrAplicado);
        TEST_ASSERT_FALSE(b.tetoXpAplicado);
        e.dia.fochoPagoMin = 360;
        LiquidacaoBatalha c = liquidarBatalha(e, batalha(Categoria::Projetos, 30, 32, 2, 1));
        NEAR(6.0, c.cr);
        NEAR(30.0, c.xp);  // XP cheio ate 8 h
        e.dia.fochoPagoMin = 480;
        LiquidacaoBatalha d = liquidarBatalha(e, batalha(Categoria::Projetos, 30, 32, 2, 1));
        NEAR(15.0, d.xp);
        TEST_ASSERT_TRUE(d.tetoXpAplicado);
    }

    void test_batalha_bonus_com_teto_e_meditacao() {
        Estado e = novoEstado();
        e.equip.crExtra[static_cast<size_t>(Categoria::Projetos)] = 0.5;
        LiquidacaoBatalha b = liquidarBatalha(e, perfeita(Categoria::Projetos, 30));
        NEAR(0.40, b.bonusCrPct);  // teto +40 %
        NEAR(12.0 * 1.4, b.cr);
        e.equip.xpExtra[static_cast<size_t>(Categoria::Estudo)] = 0.9;
        LiquidacaoBatalha s = liquidarBatalha(e, perfeita(Categoria::Estudo, 30));
        NEAR(0.50, s.bonusXpPct);  // teto +50 %
        // meditacao: 30 min cheios, resto 0,1 cr e 1,0 XP
        Estado m = novoEstado();
        LiquidacaoBatalha md = liquidarBatalha(m, batalha(Categoria::Meditacao, 40, 40, 0, 1));
        NEAR(30 * 0.2 + 10 * 0.1, md.cr);
        NEAR(30 * 2.0 + 10 * 1.0, md.xp);
        // Leitura por prazer: 0 cr, so XP
        EntradaBatalha lp = batalha(Categoria::Leitura, 20, 20, 0, 1);
        lp.porPrazer = true;
        LiquidacaoBatalha l = liquidarBatalha(m, lp);
        NEAR(0.0, l.cr);
        NEAR(30.0, l.xp);
    }

    // ------------------------------------------------------------------ habitos
    void test_habitos() {
        Estado e = novoEstado();
        ResultadoHabito a = registrarHabito(e, Habito::Academia, 1080, 1080 + 60);  // limite da janela
        TEST_ASSERT_EQUAL_INT(static_cast<int>(StatusHabito::NoHorario), static_cast<int>(a.status));
        NEAR(15.0, a.cr);
        NEAR(40.0, a.xp);
        ResultadoHabito b = registrarHabito(e, Habito::Banho, 480, 480 + 61);  // atrasado
        TEST_ASSERT_EQUAL_INT(static_cast<int>(StatusHabito::Atrasado), static_cast<int>(b.status));
        NEAR(0.0, b.cr);
        NEAR(2.5, b.xp);
        ResultadoHabito c = registrarHabito(e, Habito::Dentes, 480, 480 + 30 + 121);  // esquecido
        TEST_ASSERT_EQUAL_INT(static_cast<int>(StatusHabito::Esquecido), static_cast<int>(c.status));
        NEAR(0.0, c.xp);
        // registrar de novo e no-op
        TEST_ASSERT_FALSE(registrarHabito(e, Habito::Academia, 1080, 1080).registrado);
        NEAR(15.0, e.saldo);
        // descanso: volta ate +5 min
        TEST_ASSERT_EQUAL_INT(static_cast<int>(StatusHabito::NoHorario),
                              static_cast<int>(classificarHabito(Habito::Descanso, 600, 605)));
        TEST_ASSERT_EQUAL_INT(static_cast<int>(StatusHabito::Atrasado),
                              static_cast<int>(classificarHabito(Habito::Descanso, 600, 606)));
        // atributos recebem XP
        NEAR(40.0 + 2.5 + 0.0, e.pontosAtributo[static_cast<size_t>(Atributo::Corpo)]);
    }

    void test_medicacao_coracoes() {
        Estado e = novoEstado();
        registrarHabito(e, Habito::Medicacao, 480, 480 + 31);  // atrasada: -1/2 coracao
        TEST_ASSERT_EQUAL_INT(9, e.coracoesMeios);
        // Ajuste (app Habitos fase 1, decisao 3): "sem habitos configurados o criterio e ignorado". Um dia fechado sem
        // nenhum habito (legado ou do TickTick) NAO esquece mais a medicacao; a penalidade vale quando ha habitos em uso.
        Estado f0 = novoEstado();
        fecharDia(f0, false);
        TEST_ASSERT_EQUAL_INT(10, f0.coracoesMeios);  // sem habitos: nem penalidade, nem bonus
        Estado f = novoEstado();
        registrarHabito(f, Habito::Banho, 600, 600);
        fecharDia(f, false);  // medicacao esquecida: -1 coracao (e dia limpo devolve 1/2)
        TEST_ASSERT_EQUAL_INT(9, f.coracoesMeios);
        Estado g = novoEstado();
        ResultadoHabito r = registrarHabito(g, Habito::Medicacao, 480, 480 + 300);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(StatusHabito::Esquecido), static_cast<int>(r.status));
        TEST_ASSERT_EQUAL_INT(8, g.coracoesMeios);
    }

    void test_humor_nunca_gera_credito() {
        Estado e = novoEstado();
        for (int i = 0; i < 5; ++i) registrarHumor(e);
        NEAR(0.0, e.saldo);
        NEAR(0.0, e.dia.crGanho);
        NEAR(6.0, e.xpTotal);  // max 3 registros x 2 XP
        for (const Evento& ev : e.dia.eventos) NEAR(0.0, ev.cr);
    }

    void test_ticktick_e_fechar_dia() {
        Estado e = novoEstado();
        for (int i = 0; i < 5; ++i) registrarTickTick(e);
        NEAR(90.0, e.xpTotal);  // max 3/dia
        NEAR(0.0, e.saldo);
        fecharDia(e, true);
        NEAR(110.0, e.xpTotal);  // +20 ao fechar
        fecharDia(e, true);  // idempotente
        NEAR(110.0, e.xpTotal);
    }

    // -------------------------------------------------------------- carteira
    void test_teto_carteira_e_cofre() {
        Estado e = novoEstado();
        creditar(e, 590.0);
        creditar(e, 50.0);
        NEAR(600.0, e.saldo);
        NEAR(40.0, e.cofre);
        // lazer so gasta da carteira; o Cofre nao paga lazer
        registrarLazer(e, 100);
        NEAR(500.0, e.saldo);
        NEAR(40.0, e.cofre);
        Estado f = novoEstado();
        f.cofre = 100.0;
        registrarLazer(f, 10);
        NEAR(-10.0, f.saldo);
        NEAR(100.0, f.cofre);
    }

    void test_creditos_pagam_divida_primeiro() {
        Estado e = novoEstado();
        registrarLazer(e, 30);
        NEAR(-30.0, e.saldo);
        creditar(e, 20.0);
        NEAR(-10.0, e.saldo);
        NEAR(0.0, e.cofre);
        creditar(e, 700.0);  // paga 10 e enche a carteira; sobra vai ao Cofre
        NEAR(600.0, e.saldo);
        NEAR(90.0, e.cofre);
    }

    // --------------------------------------------------------------- desafios
    void test_desafio_cruzar_60_e_limite_diario() {
        Estado e = novoEstado();
        e.dia.habitos[static_cast<size_t>(Habito::Medicacao)] = StatusHabito::NoHorario;  // isola coracoes
        registrarLazer(e, 59);
        TEST_ASSERT_EQUAL_size_t(0, e.desafiosPendentes.size());
        registrarLazer(e, 1);  // -60: cruza
        TEST_ASSERT_EQUAL_size_t(1, e.desafiosPendentes.size());
        TEST_ASSERT_EQUAL_INT(1, e.desafiosPendentes[0].nivel);
        TEST_ASSERT_EQUAL_INT(8, e.coracoesMeios);  // -1 coracao
        TEST_ASSERT_TRUE(lojaTravada(e));
        registrarLazer(e, 60);  // -120 no mesmo dia: NAO dispara outro
        TEST_ASSERT_EQUAL_size_t(1, e.desafiosPendentes.size());
        // dia seguinte: dispara (N2)
        abrirDia(e, D(2026, 11, 10));
        TEST_ASSERT_EQUAL_size_t(2, e.desafiosPendentes.size());
        TEST_ASSERT_EQUAL_INT(2, e.desafiosPendentes[1].nivel);
        TEST_ASSERT_EQUAL_INT(6, e.coracoesMeios);
        // cumprir NAO apaga a divida; devolve 1/2 coracao
        TEST_ASSERT_TRUE(cumprirDesafio(e, 1));
        NEAR(-120.0, e.saldo);
        TEST_ASSERT_EQUAL_INT(7, e.coracoesMeios);
        // dia 3: -180 dispara N3; a divida segue
        registrarLazer(e, 60);
        abrirDia(e, D(2026, 11, 11));
        TEST_ASSERT_EQUAL_INT(3, e.desafiosPendentes.back().nivel);
    }

    void test_desafio_dois_fechamentos_negativos() {
        Estado e = novoEstado();
        registrarLazer(e, 10);  // -10, nunca cruza -60
        fecharDia(e, true);
        TEST_ASSERT_EQUAL_size_t(0, e.desafiosPendentes.size());
        abrirDia(e, D(2026, 11, 10));
        TEST_ASSERT_EQUAL_size_t(0, e.desafiosPendentes.size());
        fecharDia(e, true);  // 2o fechamento negativo seguido
        TEST_ASSERT_EQUAL_size_t(1, e.desafiosPendentes.size());
        TEST_ASSERT_EQUAL_INT(1, e.desafiosPendentes[0].nivel);
        // fechamento positivo no meio zera a contagem
        Estado f = novoEstado();
        registrarLazer(f, 10);
        fecharDia(f, true);
        abrirDia(f, D(2026, 11, 10));
        creditar(f, 50.0);
        fecharDia(f, true);
        abrirDia(f, D(2026, 11, 11));
        registrarLazer(f, 100);  // -60: cruza (nao e pelo fechamento)
        fecharDia(f, true);
        TEST_ASSERT_EQUAL_size_t(1, f.desafiosPendentes.size());
    }

    void test_desafio_niveis_e_reset_na_segunda() {
        Estado e = novoEstado(D(2026, 11, 11));  // quarta
        registrarLazer(e, 60);                    // N1
        abrirDia(e, D(2026, 11, 12));
        registrarLazer(e, 60);                    // -120: N2 (hoje ainda nao houve desafio neste dia)
        abrirDia(e, D(2026, 11, 13));
        registrarLazer(e, 60);                    // -180: N3
        TEST_ASSERT_EQUAL_size_t(3, e.desafiosPendentes.size());
        TEST_ASSERT_EQUAL_INT(1, e.desafiosPendentes[0].nivel);
        TEST_ASSERT_EQUAL_INT(2, e.desafiosPendentes[1].nivel);
        TEST_ASSERT_EQUAL_INT(3, e.desafiosPendentes[2].nivel);
        TEST_ASSERT_EQUAL_INT(3, e.desafiosNaSemana);
        // segunda: reseta a escada (saldo positivo para isolar do gatilho de fechamento)
        Estado f = novoEstado(D(2026, 11, 11));
        f.desafiosNaSemana = 2;
        creditar(f, 100.0);
        abrirDia(f, D(2026, 11, 16));  // segunda
        TEST_ASSERT_EQUAL_INT(0, f.desafiosNaSemana);
        registrarLazer(f, 160);        // -60
        TEST_ASSERT_EQUAL_INT(1, f.desafiosPendentes[0].nivel);
        // acima de N3 continua N3
        Estado g = novoEstado();
        g.desafiosNaSemana = 5;
        registrarLazer(g, 60);
        TEST_ASSERT_EQUAL_INT(3, g.desafiosPendentes[0].nivel);
    }

    void test_desafio_prazo_24h() {
        Estado e = novoEstado();
        e.dia.habitos[static_cast<size_t>(Habito::Medicacao)] = StatusHabito::NoHorario;  // isola coracoes
        avancarRelogio(e, 600);
        registrarLazer(e, 60);  // dispara as 10:00 de segunda: -1 coracao
        TEST_ASSERT_EQUAL_INT(8, e.coracoesMeios);
        creditar(e, 100.0);     // saldo +40: sem gatilho de fechamento negativo
        avancarRelogio(e, 1439);  // ainda dentro do prazo
        TEST_ASSERT_EQUAL_INT(8, e.coracoesMeios);
        abrirDia(e, D(2026, 11, 10));  // fecha segunda (dia limpo: +1/2 coracao)
        e.dia.habitos[static_cast<size_t>(Habito::Medicacao)] = StatusHabito::NoHorario;
        TEST_ASSERT_EQUAL_INT(9, e.coracoesMeios);
        avancarRelogio(e, 600);  // exatamente 24 h: ainda no prazo
        TEST_ASSERT_EQUAL_INT(9, e.coracoesMeios);
        avancarRelogio(e, 601);  // venceu: -1 coracao
        TEST_ASSERT_EQUAL_INT(7, e.coracoesMeios);
        abrirDia(e, D(2026, 11, 11));  // fecha terca (dia limpo: +1/2)
        TEST_ASSERT_EQUAL_INT(8, e.coracoesMeios);
        avancarRelogio(e, 700);  // 2o dia de atraso: -1 coracao
        TEST_ASSERT_EQUAL_INT(6, e.coracoesMeios);
        TEST_ASSERT_TRUE(lojaTravada(e));
    }

    void test_heroi_caido_e_resgate() {
        Estado e = novoEstado();
        aplicarCoracoes(e, -20);
        TEST_ASSERT_EQUAL_INT(0, e.coracoesMeios);
        TEST_ASSERT_TRUE(e.heroiCaido);
        TEST_ASSERT_TRUE(lojaTravada(e));
        TEST_ASSERT_EQUAL_INT(0, aplicarCoracoes(e, 2));  // so o resgate devolve
        // 2 dias validos seguidos
        for (int dia = 0; dia < 2; ++dia) {
            aplicarBatalha(e, perfeita(Categoria::Projetos, 60));
            for (size_t h = 0; h < 4; ++h) habitoNoHorario(e, static_cast<Habito>(h));
            fecharDia(e, true);
            abrirDia(e, D(2026, 11, 10 + dia));
        }
        TEST_ASSERT_FALSE(e.heroiCaido);
        TEST_ASSERT_EQUAL_INT(6, e.coracoesMeios);  // 3 coracoes
    }

    void test_coracoes_ganho_max_por_dia() {
        Estado e = novoEstado();
        e.coracoesMeios = 4;
        TEST_ASSERT_EQUAL_INT(2, aplicarCoracoes(e, 3));  // max +1 coracao/dia
        TEST_ASSERT_EQUAL_INT(0, aplicarCoracoes(e, 1));
        TEST_ASSERT_EQUAL_INT(6, e.coracoesMeios);
        abrirDia(e, D(2026, 11, 10));
        TEST_ASSERT_EQUAL_INT(1, aplicarCoracoes(e, 1));
        e.coracoesMeios = coracoesMaxMeios(e);
        TEST_ASSERT_EQUAL_INT(0, aplicarCoracoes(e, 1));  // teto
    }

    // ------------------------------------------------- dia valido, streak, salvaguardas
    void jogarDiaValido(Estado& e) {
        aplicarBatalha(e, perfeita(Categoria::Projetos, 60));
        for (size_t h = 0; h < 4; ++h) habitoNoHorario(e, static_cast<Habito>(h));
    }

    void test_dia_valido_e_limpo() {
        Estado e = novoEstado();
        aplicarBatalha(e, perfeita(Categoria::Projetos, 45));  // 45 < 50
        for (size_t h = 0; h < 4; ++h) habitoNoHorario(e, static_cast<Habito>(h));
        ResumoDia r = fecharDia(e, false);
        TEST_ASSERT_FALSE(r.valido);
        Estado f = novoEstado();
        jogarDiaValido(f);
        registrarPerdido(f, 31);  // 31 min: custa 1 cr mas e "sujo"
        ResumoDia s = fecharDia(f, false);
        TEST_ASSERT_TRUE(s.valido);
        TEST_ASSERT_FALSE(s.limpo);
    }

    // ------------------------------------------------ penalidades (param::kPenalidadesAtivas; achado 6 da revisao)
    void test_penalidades_padrao_do_produto_e_desligado() {
        TEST_ASSERT_FALSE(param::kPenalidadesAtivas);
    }

    void test_penalidades_desligadas_nao_tiram_coracao_nem_disparam_desafio() {
        definirPenalidadesParaTeste(false);
        Estado e = novoEstado();
        const int coracoes0 = e.coracoesMeios;
        // medicacao esquecida (+300 min) e atrasada: sem perda de coracao, e o resultado nao promete perda
        const ResultadoHabito esquecida = registrarHabito(e, Habito::Medicacao, 480, 480 + 300);
        TEST_ASSERT_EQUAL_INT(0, esquecida.coracoesMeios);
        const ResultadoHabito atrasada = registrarHabito(e, Habito::Almoco, 720, 720 + 40);
        TEST_ASSERT_EQUAL_INT(0, atrasada.coracoesMeios);
        TEST_ASSERT_EQUAL_INT(coracoes0, e.coracoesMeios);
        // divida: cruza varios patamares e nada de desafio / loja travada; os creditos continuam valendo (saldo negativo)
        const double saldoAntes = e.saldo;   // (os habitos acima ja pagaram alguns creditos)
        registrarLazer(e, 130);
        NEAR(saldoAntes - 130.0, e.saldo);
        TEST_ASSERT_TRUE(e.saldo < -60.0);   // cruzou patamares de divida
        TEST_ASSERT_EQUAL_size_t(0, e.desafiosPendentes.size());
        TEST_ASSERT_FALSE(lojaTravada(e));
        TEST_ASSERT_FALSE(e.heroiCaido);
        // perdido > 90 min no dia + fechamentos negativos seguidos: nada de -coracao nem desafio
        registrarPerdido(e, 100);
        fecharDia(e, true);
        abrirDia(e, D(2026, 11, 10));
        fecharDia(e, true);
        abrirDia(e, D(2026, 11, 11));
        TEST_ASSERT_EQUAL_size_t(0, e.desafiosPendentes.size());
        TEST_ASSERT_FALSE(e.gatilhoFechamentoPendente);
        TEST_ASSERT_FALSE(e.heroiCaido);
        TEST_ASSERT_TRUE(e.coracoesMeios >= coracoes0);   // so pode ter ganho (habitos bons / dia limpo), nunca perder
    }

    void test_penalidades_desligadas_mantem_xp_nivel_e_streak() {
        definirPenalidadesParaTeste(false);
        Estado e = novoEstado();
        jogarDiaValido(e);
        TEST_ASSERT_TRUE(e.xpTotal > 0.0);
        const double xp = e.xpTotal;
        fecharDia(e, true);   // +20 XP do recibo
        TEST_ASSERT_EQUAL_INT(1, e.streak);
        TEST_ASSERT_TRUE(e.xpTotal > xp);
        abrirDia(e, D(2026, 11, 10));
        fecharDia(e, false);   // dia invalido: a folga/streak funcionam como antes (streak e visivel no Perfil)
        abrirDia(e, D(2026, 11, 11));
        const ResumoDia r = fecharDia(e, false);
        TEST_ASSERT_TRUE(r.streakQuebrado);
        TEST_ASSERT_EQUAL_INT(0, e.streak);
    }

    void test_penalidades_ligadas_voltam_tudo() {
        definirPenalidadesParaTeste(true);
        Estado e = novoEstado();
        e.dia.habitos[static_cast<size_t>(Habito::Medicacao)] = StatusHabito::NoHorario;  // isola coracoes
        registrarLazer(e, 60);   // cruza -60: desafio + -1 coracao
        TEST_ASSERT_EQUAL_size_t(1, e.desafiosPendentes.size());
        TEST_ASSERT_EQUAL_INT(8, e.coracoesMeios);
        TEST_ASSERT_TRUE(lojaTravada(e));
        Estado f = novoEstado();
        const ResultadoHabito esquecida = registrarHabito(f, Habito::Medicacao, 480, 480 + 300);
        TEST_ASSERT_EQUAL_INT(-2, esquecida.coracoesMeios);
        TEST_ASSERT_EQUAL_INT(8, f.coracoesMeios);
    }

    void test_penalidades_alternar_nao_apaga_estado_guardado() {
        Estado e = novoEstado();
        Desafio d;
        d.nivel = 1;
        e.desafiosPendentes.push_back(d);
        e.heroiCaido = true;
        definirPenalidadesParaTeste(false);
        TEST_ASSERT_FALSE(lojaTravada(e));
        TEST_ASSERT_EQUAL_size_t(1, e.desafiosPendentes.size());   // nada apagado
        TEST_ASSERT_TRUE(e.heroiCaido);
        definirPenalidadesParaTeste(true);
        TEST_ASSERT_TRUE(lojaTravada(e));
    }

    void test_streak_com_folga() {
        Estado e = novoEstado();  // segunda
        jogarDiaValido(e);
        fecharDia(e, false);
        TEST_ASSERT_EQUAL_INT(1, e.streak);
        abrirDia(e, D(2026, 11, 10));
        ResumoDia r = fecharDia(e, false);  // dia invalido: gasta a folga
        TEST_ASSERT_TRUE(r.folgaUsada);
        TEST_ASSERT_EQUAL_INT(1, e.streak);
        abrirDia(e, D(2026, 11, 11));
        r = fecharDia(e, false);  // 2o invalido na semana: quebra
        TEST_ASSERT_TRUE(r.streakQuebrado);
        TEST_ASSERT_EQUAL_INT(0, e.streak);
    }

    void test_streak_guarda_brasa_e_folga_reseta_na_segunda() {
        Estado e = novoEstado();
        e.equip.guardaBrasa = true;
        jogarDiaValido(e);
        fecharDia(e, false);
        abrirDia(e, D(2026, 11, 10));
        fecharDia(e, false);  // folga 1
        abrirDia(e, D(2026, 11, 11));
        fecharDia(e, false);  // folga 2 (Guarda-Brasa)
        TEST_ASSERT_EQUAL_INT(1, e.streak);
        abrirDia(e, D(2026, 11, 16));  // segunda: folgas voltam
        TEST_ASSERT_EQUAL_INT(0, e.folgasUsadasSemana);
        // abrirDia fechou 12, 13, 14, 15 (vazios): 12 quebra a streak
        TEST_ASSERT_EQUAL_INT(0, e.streak);
    }

    void test_dia_de_recuperacao() {
        Estado e = novoEstado();
        e.streak = 5;
        avancarRelogio(e, 11 * 60);
        TEST_ASSERT_TRUE(declararRecuperacao(e));
        registrarLazer(e, 100);  // nao cobra
        NEAR(0.0, e.saldo);
        registrarHabito(e, Habito::Medicacao, 480, 480 + 300);  // esquecida, sem perder coracao
        TEST_ASSERT_EQUAL_INT(10, e.coracoesMeios);
        aplicarBatalha(e, perfeita(Categoria::Estudo, 30));  // foco continua dando XP
        TEST_ASSERT_TRUE(e.xpTotal > 0);
        ResumoDia r = fecharDia(e, false);
        TEST_ASSERT_TRUE(r.recuperacao);
        TEST_ASSERT_FALSE(r.folgaUsada);
        TEST_ASSERT_EQUAL_INT(5, e.streak);
        TEST_ASSERT_EQUAL_INT(0, e.folgasUsadasSemana);
        TEST_ASSERT_EQUAL_INT(10, e.coracoesMeios);
        // 2 por mes
        abrirDia(e, D(2026, 11, 10));
        TEST_ASSERT_TRUE(declararRecuperacao(e));
        abrirDia(e, D(2026, 11, 11));
        TEST_ASSERT_FALSE(declararRecuperacao(e));
        abrirDia(e, D(2026, 12, 1));
        TEST_ASSERT_TRUE(declararRecuperacao(e));  // mes novo
        // so ate o meio-dia
        abrirDia(e, D(2026, 12, 2));
        avancarRelogio(e, 12 * 60 + 1);
        TEST_ASSERT_FALSE(declararRecuperacao(e));
    }

    void test_ferias_congelam() {
        Estado e = novoEstado();
        e.streak = 4;
        registrarLazer(e, 100);  // -100: desafio pendente
        const double saldo = e.saldo;
        TEST_ASSERT_TRUE(iniciarFerias(e));  // fecha o dia (medicacao esquecida etc.)
        const int meios = e.coracoesMeios;
        abrirDia(e, D(2026, 11, 20));  // 11 dias passam sem efeito
        registrarLazer(e, 100);        // nada cobra durante as ferias
        NEAR(saldo, e.saldo);
        TEST_ASSERT_EQUAL_INT(meios, e.coracoesMeios);
        TEST_ASSERT_EQUAL_INT(4, e.streak);
        TEST_ASSERT_EQUAL_size_t(1, e.desafiosPendentes.size());
        TEST_ASSERT_TRUE(encerrarFerias(e, D(2026, 11, 20)));
        TEST_ASSERT_EQUAL_INT(meios, e.coracoesMeios);  // prazo do desafio tambem congelou
        avancarRelogio(e, 300);
        TEST_ASSERT_EQUAL_INT(meios, e.coracoesMeios);
        // minimo 2 dias
        Estado f = novoEstado();
        TEST_ASSERT_TRUE(iniciarFerias(f));
        TEST_ASSERT_FALSE(encerrarFerias(f, D(2026, 11, 10)));
        TEST_ASSERT_TRUE(encerrarFerias(f, D(2026, 11, 11)));
    }

    void test_attr_bonus_estavel_no_dia() {
        Estado e = novoEstado();
        // Oficio sobe de nivel dentro do dia, mas o bonus so vale a partir do proximo dia
        for (int i = 0; i < 4; ++i) aplicarBatalha(e, perfeita(Categoria::Projetos, 30));
        TEST_ASSERT_TRUE(nivelAtributo(e, Atributo::Oficio) >= 1);
        LiquidacaoBatalha hoje = liquidarBatalha(e, perfeita(Categoria::Projetos, 30));
        NEAR(0.25, hoje.bonusCrPct);
        abrirDia(e, D(2026, 11, 10));
        LiquidacaoBatalha amanha = liquidarBatalha(e, perfeita(Categoria::Projetos, 30));
        NEAR(0.25 + 0.005, amanha.bonusCrPct);
    }

} // namespace

// ------------------------------------------------------------ persistencia (passo 2)
namespace {
    std::string lerFixture(const char* caminho) {
        std::string conteudo;
        if (FILE* f = std::fopen(caminho, "rb")) {
            char buf[512];
            size_t n;
            while ((n = std::fread(buf, 1, sizeof buf, f)) > 0) conteudo.append(buf, n);
            std::fclose(f);
        }
        return conteudo;
    }
} // namespace

void test_projetos_le_fixture() {
    const std::string json = lerFixture("test/fixtures/tempo/projetos.json");
    TEST_ASSERT_TRUE(!json.empty());
    ListaProjetos lista;
    TEST_ASSERT_TRUE(lerProjetos(json, lista));
    TEST_ASSERT_EQUAL_INT(3, static_cast<int>(lista.projetos.size()));
    TEST_ASSERT_EQUAL_STRING("ticktick", lista.fonte.c_str());
    TEST_ASSERT_EQUAL_STRING("aaaa0001", lista.projetos[0].id.c_str());
    TEST_ASSERT_EQUAL_INT(3, lista.projetos[0].prioridade);
    TEST_ASSERT_FALSE(lista.projetos[0].vence.has_value());
    TEST_ASSERT_TRUE(lista.projetos[1].vence.has_value());
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(lista.projetos[1].tags.size()));
    TEST_ASSERT_EQUAL_INT(0, static_cast<int>(lista.projetos[2].tags.size()));
}

void test_projetos_json_invalido() {
    ListaProjetos lista;
    TEST_ASSERT_FALSE(lerProjetos("", lista));
    TEST_ASSERT_FALSE(lerProjetos("{nao e json", lista));
    TEST_ASSERT_FALSE(lerProjetos("[1,2,3]", lista));
}

void test_limpar_titulo_remove_emoji_e_mantem_acentos() {
    TEST_ASSERT_EQUAL_STRING("Projeto Alfa (Andamento)", limparTitulo("📚💎 Projeto Alfa (Andamento)").c_str());
    TEST_ASSERT_EQUAL_STRING("Escrever relatório de avaliação – versão 2",
                             limparTitulo("🏹 Escrever relatório de avaliação – versão 2").c_str());
    TEST_ASSERT_EQUAL_STRING("Estudar ação e coração", limparTitulo("  Estudar  ação   e coração ").c_str());
    // emoji com variation selector / ZWJ e letras matematicas em negrito somem
    TEST_ASSERT_EQUAL_STRING("Rotinas", limparTitulo("⚙️ Rotinas").c_str());
    TEST_ASSERT_EQUAL_STRING("", limparTitulo("📚💎").c_str());
    // byte invalido nao quebra
    TEST_ASSERT_EQUAL_STRING("ab", limparTitulo(std::string("a\xFF") + "b").c_str());
}

void test_estado_ida_e_volta() {
    Estado e = novoEstado();
    aplicarBatalha(e, perfeita(Categoria::Projetos, 25));
    aplicarBatalha(e, comDistracao(Categoria::Estudo, 15));
    registrarHumor(e);
    e.equip.crExtra[1] = 0.05;
    e.equip.guardaBrasa = true;
    e.streak = 4;
    e.ferias = false;
    e.desafiosPendentes.push_back(Desafio{2, 100, 1540, false, 1});
    e.saldo = -12.5;

    const std::string json = serializarEstado(e);
    TEST_ASSERT_TRUE(!json.empty());
    Estado lido;
    TEST_ASSERT_TRUE(lerEstado(json, lido));
    NEAR(e.saldo, lido.saldo);
    NEAR(e.xpTotal, lido.xpTotal);
    for (size_t i = 0; i < kNumAtributos; ++i) NEAR(e.pontosAtributo[i], lido.pontosAtributo[i]);
    TEST_ASSERT_EQUAL_INT(e.coracoesMeios, lido.coracoesMeios);
    TEST_ASSERT_EQUAL_INT(e.streak, lido.streak);
    TEST_ASSERT_TRUE(lido.equip.guardaBrasa);
    NEAR(0.05, lido.equip.crExtra[1]);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(lido.desafiosPendentes.size()));
    TEST_ASSERT_EQUAL_INT(2, lido.desafiosPendentes[0].nivel);
    TEST_ASSERT_EQUAL_INT(1540, static_cast<int>(lido.desafiosPendentes[0].prazoEmMin));
    TEST_ASSERT_TRUE(lido.dia.data == e.dia.data);
    TEST_ASSERT_TRUE(lido.dia.aberto);
    TEST_ASSERT_EQUAL_INT(e.dia.fochoPagoMin, lido.dia.fochoPagoMin);
    TEST_ASSERT_EQUAL_INT(static_cast<int>(e.dia.eventos.size()), static_cast<int>(lido.dia.eventos.size()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(e.dia.eventos[0].tipo), static_cast<int>(lido.dia.eventos[0].tipo));
    NEAR(e.dia.crGanho, lido.dia.crGanho);
    // 2a ida e volta e identica (estavel)
    TEST_ASSERT_EQUAL_STRING(json.c_str(), serializarEstado(lido).c_str());
}

void test_estado_json_invalido_nao_altera_saida() {
    Estado e;
    e.saldo = 42.0;
    TEST_ASSERT_FALSE(lerEstado("", e));
    TEST_ASSERT_FALSE(lerEstado("{\"saldo\": \"abc\"", e));
    NEAR(42.0, e.saldo);
}

void test_estado_campos_ausentes_usam_padrao() {
    Estado e;
    TEST_ASSERT_TRUE(lerEstado("{\"saldo\": 7.5, \"futuro\": 1}", e));
    NEAR(7.5, e.saldo);
    TEST_ASSERT_EQUAL_INT(10, e.coracoesMeios);
}

void test_log_linha_e_caminho() {
    EventoLog ev;
    ev.data = "2026-01-15";
    ev.hora = "09:30:05";
    ev.tipo = "batalha";
    ev.projeto = "Projeto \"Alfa\"";
    ev.resultado = "vitoria";
    ev.planejadoMin = 25;
    ev.focadoMin = 25;
    ev.cr = 10.0;
    ev.xp = 25.0;
    ev.overtimeMin = 3;
    ev.humor = 4;
    ev.distraiu = 2;
    const std::string linha = linhaLog(ev);
    TEST_ASSERT_TRUE(linha.find("\"overtimeMin\":3") != std::string::npos);
    TEST_ASSERT_TRUE(linha.find("\"humor\":4") != std::string::npos);
    TEST_ASSERT_TRUE(linha.find("\"distraiu\":2") != std::string::npos);
    TEST_ASSERT_TRUE(!linha.empty());
    TEST_ASSERT_EQUAL_CHAR('\n', linha.back());
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(std::count(linha.begin(), linha.end(), '\n')));
    TEST_ASSERT_TRUE(linha.find("\\\"Alfa\\\"") != std::string::npos);
    TEST_ASSERT_EQUAL_STRING("/tempo/log/2026-01-15.jsonl", caminhoLog(D(2026, 1, 15)).c_str());
    TEST_ASSERT_EQUAL_STRING("2026-11-09", textoData(kSegunda).c_str());
}

void test_relogio_epoch_e_validade() {
    // 2026-01-15 12:34:56 UTC
    const Agora a = deEpoch(1768480496, true);
    TEST_ASSERT_TRUE(a.valida);
    TEST_ASSERT_TRUE(a.data == D(2026, 1, 15));
    TEST_ASSERT_EQUAL_INT(12 * 3600 + 34 * 60 + 56, a.segundoDoDia);
    TEST_ASSERT_EQUAL_INT(12 * 60 + 34, a.minutoDoDia);
    // hora de sistema nao ajustada (1970) = data desconhecida
    TEST_ASSERT_FALSE(deEpoch(1000, true).valida);
    TEST_ASSERT_FALSE(deEpoch(1700000000, true).valida);  // 2023 < 2025
}

void test_relogio_escala_e_ancora() {
    definirEscala(60);
    TEST_ASSERT_EQUAL_UINT32(60, escala());
    TEST_ASSERT_EQUAL_UINT64(60000ULL, ms(1000));
    ancorar(1768480496, 0);  // ancora local; comparamos relativo
    const Agora base = agora(0);
    const Agora depois = agora(60000);  // 60 s reais * 60 = 1 h
    TEST_ASSERT_TRUE(base.valida && depois.valida);
    const int delta = (depois.segundoDoDia - base.segundoDoDia + 86400) % 86400;
    TEST_ASSERT_EQUAL_INT(3600, delta);
    definirEscala(1);
    TEST_ASSERT_EQUAL_UINT32(1, escala());
}

// ---- Tela inicial: Foco livre e ciclo pomodoro --------------------------------------------------------------
void test_foco_livre_liquida_pela_categoria() {
    struct Caso { Categoria cat; double cr; double xp; Atributo attr; };
    // 25 min de foco sem distracao = vitoria perfeita (+25 %): taxa * 25 * 1,25
    const Caso casos[] = {
        {Categoria::Trabalho, 6.25, 31.25, Atributo::Oficio},     // 0,20 cr/min, 1,0 XP/min
        {Categoria::Estudo, 12.5, 37.5, Atributo::Mente},         // 0,40 cr/min, 1,2 XP/min
        {Categoria::Leitura, 6.25, 46.875, Atributo::Mente},      // 0,20 cr/min, 1,5 XP/min
        {Categoria::Meditacao, 6.25, 62.5, Atributo::Mente},      // 0,20 cr/min, 2,0 XP/min
        {Categoria::Projetos, 12.5, 31.25, Atributo::Oficio},     // 0,40 cr/min, 1,0 XP/min
    };
    for (const auto& c : casos) {
        Estado e = novoEstado();
        const double antes = e.saldo;
        const LiquidacaoBatalha r = aplicarBatalha(e, perfeita(c.cat, 25));
        TEST_ASSERT_TRUE(r.vitoria);
        NEAR(c.cr, r.cr);
        NEAR(c.xp, r.xp);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(c.attr), static_cast<int>(r.atributo));
        NEAR(antes + c.cr, e.saldo);   // creditos entram na carteira
        NEAR(c.xp, e.xpTotal);
    }
    TEST_ASSERT_EQUAL_STRING("Foco livre · Estudo", nomeFocoLivre(Categoria::Estudo).c_str());
    TEST_ASSERT_EQUAL_STRING("Foco livre · Meditação", nomeFocoLivre(Categoria::Meditacao).c_str());
    // chips <-> categorias
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Categoria::Trabalho), static_cast<int>(categoriaDoChip(0)));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Categoria::Projetos), static_cast<int>(categoriaDoChip(4)));
    for (int i = 0; i < static_cast<int>(kNumChips); ++i) TEST_ASSERT_EQUAL_INT(i, chipDaCategoria(categoriaDoChip(i)));
}

void test_ciclo_pomodoro_sequencia() {
    TEST_ASSERT_FALSE(Ciclo{}.ativo);
    Ciclo c = iniciarCiclo();
    // 4 rodadas: foco 25 -> pausa 5 (3 vezes), foco 25 -> pausa longa 15, fim
    for (int rodada = 1; rodada <= kCicloRodadas; ++rodada) {
        TEST_ASSERT_TRUE(c.ativo);
        TEST_ASSERT_EQUAL_INT(rodada, c.rodada);
        TEST_ASSERT_EQUAL_INT(static_cast<int>(PassoCiclo::Foco), static_cast<int>(c.passo));
        TEST_ASSERT_EQUAL_INT(25, duracaoCicloMin(c));
        c = avancarCiclo(c);   // foco terminou
        TEST_ASSERT_TRUE(c.ativo);
        TEST_ASSERT_EQUAL_INT(rodada, c.rodada);
        if (rodada < kCicloRodadas) {
            TEST_ASSERT_EQUAL_INT(static_cast<int>(PassoCiclo::PausaCurta), static_cast<int>(c.passo));
            TEST_ASSERT_EQUAL_INT(5, duracaoCicloMin(c));
        } else {
            TEST_ASSERT_EQUAL_INT(static_cast<int>(PassoCiclo::PausaLonga), static_cast<int>(c.passo));
            TEST_ASSERT_EQUAL_INT(15, duracaoCicloMin(c));
        }
        c = avancarCiclo(c);   // pausa terminou
    }
    TEST_ASSERT_FALSE(c.ativo);                 // ciclo acabou depois da pausa longa
    TEST_ASSERT_FALSE(avancarCiclo(c).ativo);   // inativo continua inativo
}

void test_ciclo_pomodoro_cada_rodada_liquida_separado() {
    Estado e = novoEstado();
    double soma = 0.0;
    for (int rodada = 1; rodada <= kCicloRodadas; ++rodada) {
        const LiquidacaoBatalha r = aplicarBatalha(e, perfeita(Categoria::Estudo, kCicloFocoMin));
        NEAR(12.5, r.cr);   // cada rodada de 25 min paga igual, sem acumular com a anterior
        soma += r.cr;
    }
    NEAR(50.0, soma);
    NEAR(50.0, e.saldo - novoEstado().saldo);
}

void test_estado_preferencias_da_tela_inicial() {
    Estado e = novoEstado();
    TEST_ASSERT_EQUAL_INT(25, e.ultimaDuracaoMin);
    TEST_ASSERT_FALSE(e.ciclopomodoro);
    e.ultimaCategoria = static_cast<int>(Categoria::Leitura);
    e.ultimaDuracaoMin = 45;
    e.ciclopomodoro = true;
    Estado lido;
    TEST_ASSERT_TRUE(lerEstado(serializarEstado(e), lido));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(Categoria::Leitura), lido.ultimaCategoria);
    TEST_ASSERT_EQUAL_INT(45, lido.ultimaDuracaoMin);
    TEST_ASSERT_TRUE(lido.ciclopomodoro);
    Estado antigo;   // estado.json antigo (sem os campos novos) cai nos padroes
    TEST_ASSERT_TRUE(lerEstado("{\"saldo\": 1}", antigo));
    TEST_ASSERT_EQUAL_INT(25, antigo.ultimaDuracaoMin);
    TEST_ASSERT_FALSE(antigo.ciclopomodoro);
}

void test_log_foco_livre_registra_categoria() {
    EventoLog ev;
    ev.tipo = "batalha";
    ev.projeto = "Foco livre";
    ev.categoria = "Estudo";
    const std::string linha = linhaLog(ev);
    TEST_ASSERT_TRUE(linha.find("\"projeto\":\"Foco livre\"") != std::string::npos);
    TEST_ASSERT_TRUE(linha.find("\"categoria\":\"Estudo\"") != std::string::npos);
}


// ------------------------------------------------------------------ Perfil (lume.5)
static EventoLog bat(const char* data, const char* hora, const char* cat, int foco, const char* projeto = "", int dist = 0,
                     bool vit = true) {
    EventoLog e;
    e.data = data;
    e.hora = hora;
    e.tipo = "batalha";
    e.projeto = projeto[0] ? projeto : "Foco livre";
    e.projetoId = projeto[0] ? "id" : "";
    e.categoria = cat;
    e.resultado = vit ? "vitoria" : "fuga";
    e.planejadoMin = foco;
    e.focadoMin = foco;
    e.distraidoMin = dist;
    e.distracoes = dist > 0 ? 1 : 0;
    return e;
}

void test_perfil_log_ida_e_volta_e_linha_invalida() {
    EventoLog e = bat("2026-11-10", "09:30:00", "Estudo", 25, "Plataforma");
    e.perfeita = true;
    EventoLog lido;
    TEST_ASSERT_TRUE(lerLinhaLog(linhaLog(e), lido));
    TEST_ASSERT_EQUAL_STRING("Plataforma", lido.projeto.c_str());
    TEST_ASSERT_TRUE(lido.perfeita);
    EventoLog intacto = bat("x", "y", "z", 1);
    TEST_ASSERT_FALSE(lerLinhaLog("{nao json", intacto));
    TEST_ASSERT_EQUAL_STRING("x", intacto.data.c_str());
    EventoLog antigo;   // log antigo: sem categoria/perfeita
    TEST_ASSERT_TRUE(lerLinhaLog("{\"data\":\"2026-11-09\",\"tipo\":\"batalha\",\"projetoId\":\"a\",\"focadoMin\":10}", antigo));
    TEST_ASSERT_FALSE(antigo.perfeita);
    TEST_ASSERT_EQUAL_INT(4, chipDoEvento(antigo));   // projeto sem categoria -> Projetos
}

void test_perfil_datas_e_horas() {
    Data d;
    TEST_ASSERT_TRUE(lerDataTexto("2026-11-10", d));
    TEST_ASSERT_EQUAL_INT(10, d.dia);
    TEST_ASSERT_FALSE(lerDataTexto("2026-13-10", d));
    TEST_ASSERT_FALSE(lerDataTexto("26-11-10", d));
    TEST_ASSERT_EQUAL_INT(9 * 60 + 5, minutoDeHora("09:05:33"));
    TEST_ASSERT_EQUAL_INT(-1, minutoDeHora("25:00"));
    TEST_ASSERT_EQUAL_INT(-1, minutoDeHora(""));
}

void test_perfil_resumo_projetos_horas_e_vitorias() {
    Resumo r;
    registrarEvento(r, bat("2026-11-09", "10:00:00", "Projetos", 50, "Alfa"), 1);
    registrarEvento(r, bat("2026-11-09", "11:00:00", "Projetos", 25, "Alfa", 0, false), 1);
    registrarEvento(r, bat("2026-11-10", "09:00:00", "Estudo", 30), 2);
    EventoLog ruido;
    ruido.tipo = "humor";
    registrarEvento(r, ruido, 9);   // ignorado
    TEST_ASSERT_EQUAL_INT(3, r.batalhas);
    TEST_ASSERT_EQUAL_INT(2, r.vitorias);
    TEST_ASSERT_EQUAL_INT(105, r.focoTotalMin);
    TEST_ASSERT_EQUAL_INT(30, r.focoLivreMin);
    TEST_ASSERT_EQUAL_INT(2, r.maxStreak);
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(r.projetos.size()));
    TEST_ASSERT_EQUAL_INT(75, r.projetos[0].minutos);
    TEST_ASSERT_EQUAL_INT(1, r.projetos[0].vitorias);
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(r.dias.size()));
    TEST_ASSERT_TRUE(r.dias[0].dia < r.dias[1].dia);
    TEST_ASSERT_EQUAL_INT(75, r.dias[0].focoMin);
}

void test_perfil_resumo_ida_e_volta_e_reconstrucao() {
    std::vector<EventoLog> evs = {bat("2026-11-09", "10:00:00", "Trabalho", 25), bat("2026-11-10", "10:00:00", "Estudo", 40, "P")};
    const Resumo a = reconstruirResumo(evs);
    Resumo b;
    TEST_ASSERT_TRUE(lerResumo(serializarResumo(a), b));
    TEST_ASSERT_EQUAL_INT(a.focoTotalMin, b.focoTotalMin);
    TEST_ASSERT_EQUAL_INT(2, static_cast<int>(b.dias.size()));
    TEST_ASSERT_EQUAL_INT(1, static_cast<int>(b.projetos.size()));
    TEST_ASSERT_EQUAL_INT(a.minutosPorHora[9], b.minutosPorHora[9]);
    Resumo intacto;
    intacto.vitorias = 7;
    TEST_ASSERT_FALSE(lerResumo("{lixo", intacto));
    TEST_ASSERT_EQUAL_INT(7, intacto.vitorias);
}

void test_perfil_limita_projetos_e_dias() {
    Resumo r;
    for (int i = 0; i < int(kMaxProjetosResumo) + 5; ++i)
        registrarEvento(r, bat("2026-11-10", "10:00:00", "Projetos", 10 + i, (std::string{"P"} + std::to_string(i)).c_str()));
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kMaxProjetosResumo), static_cast<int>(r.projetos.size()));
    Resumo q;
    for (int i = 0; i < int(kMaxDiasResumo) + 10; ++i) {
        const Data d = dataDeSerial(diaSerial(D(2025, 1, 1)) + i);
        registrarEvento(q, bat(textoData(d).c_str(), "10:00:00", "Trabalho", 10));
    }
    TEST_ASSERT_EQUAL_INT(static_cast<int>(kMaxDiasResumo), static_cast<int>(q.dias.size()));
}

void test_perfil_faixa_mais_focada_e_media() {
    Resumo r;
    TEST_ASSERT_EQUAL_INT(-1, faixaMaisFocada(r));
    registrarEvento(r, bat("2026-11-09", "11:00:00", "Trabalho", 60));    // foco 10:00-11:00
    registrarEvento(r, bat("2026-11-10", "12:00:00", "Trabalho", 60));    // foco 11:00-12:00
    registrarEvento(r, bat("2026-11-10", "10:30:00", "Trabalho", 30));    // 10:00-10:30
    TEST_ASSERT_EQUAL_INT(10, faixaMaisFocada(r));    // 10h-12h
    TEST_ASSERT_EQUAL_INT((60 + 90) / 2, mediaFocoPorDia(r));
}

void test_perfil_semana_agrega_por_dia_e_categoria() {
    const int64_t seg = segundaDaSemana(D(2026, 11, 10));
    Semana s;
    somarSemana(s, bat("2026-11-09", "10:00:00", "Trabalho", 60), seg);
    somarSemana(s, bat("2026-11-10", "10:00:00", "Estudo", 30, "", 5), seg);
    somarSemana(s, bat("2026-11-10", "11:00:00", "Estudo", 20, "", 1), seg);
    somarSemana(s, bat("2026-11-08", "10:00:00", "Trabalho", 99), seg);   // semana anterior: ignorado
    somarSemana(s, bat("2026-11-16", "10:00:00", "Trabalho", 99), seg);   // semana seguinte: ignorado
    TEST_ASSERT_EQUAL_INT(110, s.total);
    TEST_ASSERT_EQUAL_INT(60, s.min[0][0]);
    TEST_ASSERT_EQUAL_INT(50, s.min[1][1]);
    TEST_ASSERT_EQUAL_INT(50, s.porCategoria[1]);
    TEST_ASSERT_EQUAL_INT(5, s.perdidoMin);   // so distracao > 2 min conta como perdido
    TEST_ASSERT_EQUAL_INT(60, s.maiorDia);
}

void test_perfil_titulos_por_nivel() {
    TEST_ASSERT_EQUAL_STRING("Recruta", tituloDoNivel(1));
    TEST_ASSERT_EQUAL_STRING("Guerreiro do Foco", tituloDoNivel(7));
    TEST_ASSERT_EQUAL_STRING("Lenda do Foco", tituloDoNivel(30));
}

void test_perfil_conquistas_vitorias_e_maratona() {
    Resumo r;
    auto c = avaliarConquistas(r, 0);
    TEST_ASSERT_EQUAL_INT(0, conquistasGanhas(c));
    for (int i = 0; i < 10; ++i) registrarEvento(r, bat("2026-11-09", "10:00:00", "Trabalho", 25));
    registrarEvento(r, bat("2026-11-10", "10:00:00", "Trabalho", 25));
    c = avaliarConquistas(r, 0);
    TEST_ASSERT_TRUE(c[0].ganha);    // Primeira vitoria
    TEST_ASSERT_TRUE(c[1].ganha);    // 10 vitorias
    TEST_ASSERT_FALSE(c[2].ganha);   // 100 vitorias
    TEST_ASSERT_EQUAL_INT(11, c[2].atual);
    TEST_ASSERT_TRUE(c[3].ganha);    // Maratona: 10 x 25 min = 250 min no dia 09
}

void test_perfil_maratona_4h_num_dia() {
    Resumo r;
    registrarEvento(r, bat("2026-11-09", "12:00:00", "Trabalho", 120));
    registrarEvento(r, bat("2026-11-09", "15:00:00", "Trabalho", 119));
    TEST_ASSERT_EQUAL_INT(239, avaliarConquistas(r, 0)[3].atual);
    TEST_ASSERT_FALSE(avaliarConquistas(r, 0)[3].ganha);
    registrarEvento(r, bat("2026-11-09", "16:00:00", "Trabalho", 1));
    TEST_ASSERT_TRUE(avaliarConquistas(r, 0)[3].ganha);
}

void test_perfil_streak_conquista_usa_maximo() {
    Resumo r;
    registrarEvento(r, bat("2026-11-09", "12:00:00", "Trabalho", 25), 7);
    TEST_ASSERT_TRUE(avaliarConquistas(r, 0)[4].ganha);          // maxStreak do resumo
    Resumo vazio;
    TEST_ASSERT_TRUE(avaliarConquistas(vazio, 7)[4].ganha);      // streak atual do estado
    TEST_ASSERT_EQUAL_INT(3, avaliarConquistas(vazio, 3)[4].atual);
}

void test_perfil_monge_sete_dias_seguidos() {
    Resumo r;
    for (int i = 0; i < 6; ++i) registrarEvento(r, bat(textoData(dataDeSerial(diaSerial(D(2026, 11, 1)) + i)).c_str(), "08:00:00", "Meditação", 10));
    TEST_ASSERT_EQUAL_INT(6, avaliarConquistas(r, 0)[5].atual);
    TEST_ASSERT_FALSE(avaliarConquistas(r, 0)[5].ganha);
    registrarEvento(r, bat("2026-11-07", "08:00:00", "Meditação", 10));
    TEST_ASSERT_TRUE(avaliarConquistas(r, 0)[5].ganha);
    // um buraco zera a sequencia
    Resumo q;
    for (int i : {0, 1, 2, 4, 5, 6}) registrarEvento(q, bat(textoData(dataDeSerial(diaSerial(D(2026, 11, 1)) + i)).c_str(), "08:00:00", "Meditação", 10));
    TEST_ASSERT_EQUAL_INT(3, avaliarConquistas(q, 0)[5].atual);
}

void test_perfil_madrugador_5_dias_antes_das_7h() {
    Resumo r;
    for (int i = 0; i < 4; ++i) registrarEvento(r, bat(textoData(dataDeSerial(diaSerial(D(2026, 11, 1)) + i)).c_str(), "06:30:00", "Trabalho", 25));
    registrarEvento(r, bat("2026-11-05", "09:00:00", "Trabalho", 25));   // comecou 08:35: nao conta
    TEST_ASSERT_EQUAL_INT(4, avaliarConquistas(r, 0)[6].atual);
    registrarEvento(r, bat("2026-11-06", "06:50:00", "Trabalho", 25));   // comecou 06:25
    TEST_ASSERT_TRUE(avaliarConquistas(r, 0)[6].ganha);
    Resumo tarde;   // comecou 07:00 em ponto: nao e madrugada
    registrarEvento(tarde, bat("2026-11-06", "07:25:00", "Trabalho", 25));
    TEST_ASSERT_EQUAL_INT(0, avaliarConquistas(tarde, 0)[6].atual);
}

void test_perfil_dia_perfeito_exige_todas_perfeitas_e_2h() {
    Resumo r;
    registrarEvento(r, bat("2026-11-09", "10:00:00", "Trabalho", 60));
    registrarEvento(r, bat("2026-11-09", "12:00:00", "Trabalho", 59));
    TEST_ASSERT_EQUAL_INT(119, avaliarConquistas(r, 0)[7].atual);
    TEST_ASSERT_FALSE(avaliarConquistas(r, 0)[7].ganha);
    registrarEvento(r, bat("2026-11-09", "13:00:00", "Trabalho", 1));
    TEST_ASSERT_TRUE(avaliarConquistas(r, 0)[7].ganha);
    Resumo q;   // uma batalha com distracao quebra o dia perfeito
    registrarEvento(q, bat("2026-11-09", "10:00:00", "Trabalho", 130, "", 3));
    TEST_ASSERT_FALSE(avaliarConquistas(q, 0)[7].ganha);
    TEST_ASSERT_EQUAL_INT(0, avaliarConquistas(q, 0)[7].atual);
}

void test_perfil_proxima_conquista_e_contagem() {
    Resumo r;
    for (int i = 0; i < 7; ++i) registrarEvento(r, bat("2026-11-09", "10:00:00", "Trabalho", 10, "", 0, i < 6));
    auto c = avaliarConquistas(r, 0);
    // 6 vitorias: "10 vitorias" (60 %) e a mais adiantada entre as nao ganhas
    TEST_ASSERT_EQUAL_INT(1, conquistasGanhas(c));
    TEST_ASSERT_EQUAL_INT(1, proximaConquista(c));
}

// ------------------------------------------------------------------ Foco do leitor -> Tempo (lume.7)
namespace {
    EntradaFocoLeitor EL(FaseLeitor fase, uint32_t ms, uint32_t concluidas = 0, uint32_t palavra = 0, int plan = 25) {
        EntradaFocoLeitor e;
        e.fase = fase;
        e.agoraMs = ms;
        e.concluidas = concluidas;
        e.palavra = palavra;
        e.planejadoMin = plan;
        return e;
    }
    constexpr uint32_t kMin = 60000;

    RodadaLeitor rodadaCheia(int min = 25) {
        RodadaLeitor rod;
        rod.planejadoMin = min;
        rod.focadoMin = min;
        rod.concluida = true;
        rod.palavras = 5000;   // leu de verdade
        return rod;
    }
}

void test_leitor_rodada_concluida_sem_pausa_e_perfeita() {
    RastreadorLeitor r;
    TEST_ASSERT_FALSE(r.observar(EL(FaseLeitor::Foco, 1000, 0, 100)).has_value());
    TEST_ASSERT_FALSE(r.observar(EL(FaseLeitor::Foco, 1000 + 10 * kMin, 0, 2100)).has_value());
    const auto rod = r.observar(EL(FaseLeitor::Fora, 1000 + 25 * kMin, 1, 5100));
    TEST_ASSERT_TRUE(rod.has_value());
    TEST_ASSERT_TRUE(rod->concluida);
    TEST_ASSERT_EQUAL_INT(25, rod->focadoMin);
    TEST_ASSERT_EQUAL_INT(0, rod->pausas);
    TEST_ASSERT_EQUAL_UINT32(5000, rod->palavras);
    TEST_ASSERT_FALSE(r.aberta());

    Estado e;
    abrirDia(e, D(2026, 11, 10));
    const auto ev = liquidarFocoLeitor(e, *rod, false, "Dom Casmurro", "2026-11-10", "09:00:00");
    TEST_ASSERT_TRUE(ev.has_value());
    // 25 min de Leitura perfeita: cr = 25*0,20*1,25 = 6,25 ; XP = 25*1,5*1,25 = 46,875
    NEAR(6.25, ev->cr);
    NEAR(46.875, ev->xp);
    TEST_ASSERT_TRUE(ev->perfeita);
    TEST_ASSERT_EQUAL_STRING("vitoria", ev->resultado.c_str());
    TEST_ASSERT_EQUAL_STRING("Leitura", ev->categoria.c_str());
    TEST_ASSERT_EQUAL_STRING("leitor", ev->origem.c_str());
    TEST_ASSERT_EQUAL_STRING("Dom Casmurro", ev->projeto.c_str());
    TEST_ASSERT_TRUE(ev->projetoId.empty());
    TEST_ASSERT_EQUAL_INT(5000, ev->palavras);
    NEAR(6.25, e.saldo);
    NEAR(46.875, e.xpTotal);
    TEST_ASSERT_EQUAL_INT(25, e.dia.fochoPagoMin);
    TEST_ASSERT_EQUAL_INT(25, e.dia.fochoValidoMin);
}

void test_leitor_pausa_tira_perfeita_mas_nao_vira_fuga() {
    RastreadorLeitor r;
    r.observar(EL(FaseLeitor::Foco, 0));
    r.observar(EL(FaseLeitor::Foco, 10 * kMin, 0, 2000));
    r.observar(EL(FaseLeitor::Pausado, 10 * kMin + 50, 0, 2000));
    r.observar(EL(FaseLeitor::Pausado, 40 * kMin, 0, 2000));          // 30 min parado: nao conta
    r.observar(EL(FaseLeitor::Foco, 40 * kMin + 50, 0, 2000));
    const auto rod = r.observar(EL(FaseLeitor::Fora, 55 * kMin, 1, 5000));
    TEST_ASSERT_TRUE(rod.has_value());
    TEST_ASSERT_EQUAL_INT(1, rod->pausas);
    TEST_ASSERT_TRUE(rod->concluida);
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    const auto ev = liquidarFocoLeitor(e, *rod, false, "Livro", "2026-11-10", "09:00:00");
    TEST_ASSERT_TRUE(ev.has_value());
    TEST_ASSERT_FALSE(ev->perfeita);
    TEST_ASSERT_EQUAL_STRING("vitoria", ev->resultado.c_str());
    NEAR(5.0, ev->cr);      // 25 * 0,20 sem bonus
    NEAR(37.5, ev->xp);     // 25 * 1,5 sem bonus
}

void test_leitor_tempo_pausado_nao_conta_em_parada_parcial() {
    RastreadorLeitor r;
    r.observar(EL(FaseLeitor::Foco, 0));
    r.observar(EL(FaseLeitor::Foco, 5 * kMin));
    r.observar(EL(FaseLeitor::Pausado, 5 * kMin + 10));
    r.observar(EL(FaseLeitor::Pausado, 60 * kMin));
    const auto rod = r.observar(EL(FaseLeitor::Fora, 60 * kMin + 10));
    TEST_ASSERT_TRUE(rod.has_value());
    TEST_ASSERT_EQUAL_INT(5, rod->focadoMin);
    TEST_ASSERT_FALSE(rod->concluida);
}

void test_leitor_parar_no_meio_liquida_minutos_parciais_como_fuga() {
    RastreadorLeitor r;
    r.observar(EL(FaseLeitor::Foco, 0, 0, 10));
    r.observar(EL(FaseLeitor::Foco, 7 * kMin + 30000, 0, 910));
    const auto rod = r.observar(EL(FaseLeitor::Fora, 7 * kMin + 40000, 0, 920));   // stop(): sem conclusao
    TEST_ASSERT_TRUE(rod.has_value());
    TEST_ASSERT_FALSE(rod->concluida);
    TEST_ASSERT_EQUAL_INT(7, rod->focadoMin);
    TEST_ASSERT_EQUAL_UINT32(910, rod->palavras);
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    const auto ev = liquidarFocoLeitor(e, *rod, false, "Livro", "2026-11-10", "09:00:00");
    TEST_ASSERT_TRUE(ev.has_value());
    TEST_ASSERT_EQUAL_STRING("fuga", ev->resultado.c_str());
    NEAR(1.4, ev->cr);     // 7 * 0,20, leitura da credito mesmo na fuga (sem bonus)
    NEAR(10.5, ev->xp);    // 7 * 1,5
    TEST_ASSERT_EQUAL_INT(7, e.dia.fochoPagoMin);
    TEST_ASSERT_EQUAL_INT(0, e.dia.fochoValidoMin);
}

void test_leitor_menos_de_um_minuto_nao_liquida() {
    RastreadorLeitor r;
    r.observar(EL(FaseLeitor::Foco, 0));
    const auto rod = r.observar(EL(FaseLeitor::Fora, 40000));
    TEST_ASSERT_TRUE(rod.has_value());
    TEST_ASSERT_EQUAL_INT(0, rod->focadoMin);
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    TEST_ASSERT_FALSE(liquidarFocoLeitor(e, *rod, false, "Livro", "2026-11-10", "09:00:00").has_value());
    NEAR(0.0, e.xpTotal);
}

void test_leitor_intervalo_nao_abre_rodada_e_cada_rodada_liquida_separado() {
    RastreadorLeitor r;
    TEST_ASSERT_FALSE(r.observar(EL(FaseLeitor::Fora, 0)).has_value());      // esperando foco
    r.observar(EL(FaseLeitor::Foco, 1000));
    TEST_ASSERT_TRUE(r.observar(EL(FaseLeitor::Fora, 1000 + 25 * kMin, 1)).has_value());   // foco acabou -> pausa
    TEST_ASSERT_FALSE(r.observar(EL(FaseLeitor::Fora, 1000 + 30 * kMin, 1)).has_value());  // break: nada
    TEST_ASSERT_FALSE(r.aberta());
    r.observar(EL(FaseLeitor::Foco, 1000 + 31 * kMin, 1));                   // 2a rodada
    const auto seg = r.observar(EL(FaseLeitor::Fora, 1000 + 56 * kMin, 2));
    TEST_ASSERT_TRUE(seg.has_value());
    TEST_ASSERT_EQUAL_INT(25, seg->focadoMin);
}

void test_leitor_conclusao_com_foco_ainda_ativo_fecha_e_reabre() {
    RastreadorLeitor r;
    r.observar(EL(FaseLeitor::Foco, 0));
    TEST_ASSERT_TRUE(r.observar(EL(FaseLeitor::Foco, 25 * kMin, 1)).has_value());
    TEST_ASSERT_FALSE(r.aberta());
    r.observar(EL(FaseLeitor::Foco, 25 * kMin + 10, 1));
    TEST_ASSERT_TRUE(r.aberta());
}

void test_leitor_anti_dupla_contagem_com_batalha_do_tempo_ativa() {
    const RodadaLeitor rod = rodadaCheia();
    TEST_ASSERT_FALSE(podeLiquidarLeitor(true));
    TEST_ASSERT_TRUE(podeLiquidarLeitor(false));
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    TEST_ASSERT_FALSE(liquidarFocoLeitor(e, rod, true, "Livro", "2026-11-10", "09:00:00").has_value());
    NEAR(0.0, e.saldo);
    NEAR(0.0, e.xpTotal);
    TEST_ASSERT_EQUAL_INT(0, e.dia.fochoPagoMin);
}

void test_leitor_leitura_da_creditos_e_nunca_por_prazer() {
    const auto in = paraEntradaBatalha(rodadaCheia());
    TEST_ASSERT_TRUE(in.categoria == Categoria::Leitura);
    TEST_ASSERT_FALSE(in.porPrazer);
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    TEST_ASSERT_TRUE(liquidarBatalha(e, in).cr > 0.0);
}

void test_leitor_titulo_vazio_e_longo() {
    const RodadaLeitor rod = rodadaCheia();
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    TEST_ASSERT_EQUAL_STRING("Leitura", liquidarFocoLeitor(e, rod, false, "", "2026-11-10", "09:00:00")->projeto.c_str());
    const std::string longo(300, 'a');
    TEST_ASSERT_TRUE(liquidarFocoLeitor(e, rod, false, longo, "2026-11-10", "09:00:00")->projeto.size() <= 80);
}

void test_leitor_evento_ida_e_volta_no_log_e_logs_antigos_continuam_legiveis() {
    RodadaLeitor rod = rodadaCheia();
    rod.palavras = 4321;
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    const auto ev = liquidarFocoLeitor(e, rod, false, "Ensaio sobre a Cegueira", "2026-11-10", "09:00:00");
    EventoLog lido;
    TEST_ASSERT_TRUE(lerLinhaLog(linhaLog(*ev), lido));
    TEST_ASSERT_EQUAL_STRING("leitor", lido.origem.c_str());
    TEST_ASSERT_EQUAL_INT(4321, lido.palavras);
    TEST_ASSERT_EQUAL_STRING("Ensaio sobre a Cegueira", lido.projeto.c_str());

    // log antigo (sem origem/palavras/perfeita) continua legivel, com padroes
    EventoLog velho;
    TEST_ASSERT_TRUE(lerLinhaLog(
        R"({"data":"2026-11-01","hora":"08:00:00","tipo":"batalha","projeto":"Foco livre","categoria":"Estudo","resultado":"vitoria","planejadoMin":25,"focadoMin":25,"cr":10,"xp":30})",
        velho));
    TEST_ASSERT_TRUE(velho.origem.empty());
    TEST_ASSERT_EQUAL_INT(0, velho.palavras);
    TEST_ASSERT_EQUAL_INT(25, velho.focadoMin);
}

void test_leitor_so_conta_lendo_de_verdade() {
    // "So timer" (leitor parado): 25 min concluidos, 0 palavras -> nao liquida
    RastreadorLeitor r;
    auto parado = [](FaseLeitor f, uint32_t ms, uint32_t c, uint32_t p) { auto e = EL(f, ms, c, p); e.lendo = false; return e; };
    r.observar(parado(FaseLeitor::Foco, 0, 0, 100));
    const auto rod = r.observar(parado(FaseLeitor::Fora, 25 * kMin, 1, 100));
    TEST_ASSERT_TRUE(rod.has_value());
    TEST_ASSERT_EQUAL_UINT32(0, rod->palavras);
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    TEST_ASSERT_FALSE(liquidarFocoLeitor(e, *rod, false, "Livro", "2026-11-10", "09:00:00").has_value());
    NEAR(0.0, e.saldo);

    // arrastar/pular com o leitor parado nao soma palavras; so o avanco com o leitor rodando conta
    RastreadorLeitor r2;
    r2.observar(EL(FaseLeitor::Foco, 0, 0, 100));
    r2.observar(EL(FaseLeitor::Foco, 1 * kMin, 0, 400));             // leu 300
    r2.observar(parado(FaseLeitor::Foco, 2 * kMin, 0, 400));
    r2.observar(parado(FaseLeitor::Foco, 3 * kMin, 0, 9000));         // pulou/arrastou: nao conta
    r2.observar(EL(FaseLeitor::Foco, 4 * kMin, 0, 9000));
    const auto rod2 = r2.observar(EL(FaseLeitor::Fora, 5 * kMin, 1, 9020));   // leu +20
    TEST_ASSERT_EQUAL_UINT32(320, rod2->palavras);

    // abaixo do minimo de palavras nao conta
    RodadaLeitor pouca = rodadaCheia();
    pouca.palavras = kMinPalavrasLeitor - 1;
    TEST_ASSERT_FALSE(rodadaValida(pouca));
}

void test_leitor_resumo_conta_leitura_como_foco_livre() {
    Estado e;
    abrirDia(e, D(2026, 11, 10));
    const auto ev = liquidarFocoLeitor(e, rodadaCheia(), false, "Livro", "2026-11-10", "09:00:00");
    Resumo res;
    registrarEvento(res, *ev, e.streak);
    TEST_ASSERT_EQUAL_INT(1, res.batalhas);
    TEST_ASSERT_EQUAL_INT(25, res.focoLivreMin);
    TEST_ASSERT_EQUAL_INT(2, chipDoEvento(*ev));   // chip Leitura
}


// ============================================================ Tempo v3: decisoes 18/19, categoria unica, seletor B
namespace {
    EntradaBatalha comSegundos(int distSeg, int numDist, int plan = 25) {
        EntradaBatalha b = batalha(Categoria::Projetos, plan, plan + std::min(distSeg / 60, plan), distSeg / 60, numDist);
        b.segundosDistraidos = distSeg;
        return b;
    }
}

void test_v3_decisao18_uma_distracao_de_ate_1_min_mantem_o_bonus_perfeito() {
    const Estado e = novoEstado();
    TEST_ASSERT_TRUE(liquidarBatalha(e, comSegundos(0, 0)).perfeita);
    const auto curta = liquidarBatalha(e, comSegundos(45, 1));
    TEST_ASSERT_TRUE(curta.vitoria);
    TEST_ASSERT_TRUE(curta.perfeita);
    NEAR(0.25, curta.bonusCrPct);
    TEST_ASSERT_TRUE(liquidarBatalha(e, comSegundos(60, 1)).perfeita);       // exatamente 1 min ainda vale
}

void test_v3_decisao18_segunda_distracao_ou_mais_de_1_min_perde_o_bonus() {
    const Estado e = novoEstado();
    const auto longa = liquidarBatalha(e, comSegundos(61, 1));
    TEST_ASSERT_TRUE(longa.vitoria);
    TEST_ASSERT_FALSE(longa.perfeita);
    TEST_ASSERT_FALSE(liquidarBatalha(e, comSegundos(40, 2)).perfeita);      // 2 distracoes curtas
    TEST_ASSERT_FALSE(liquidarBatalha(e, comSegundos(30, 1, 10)).perfeita);  // plano < 15 min nunca tem bonus
    // quem nao informa os segundos (ex.: foco do leitor) segue a regra antiga: qualquer distracao tira o bonus
    EntradaBatalha antiga = batalha(Categoria::Leitura, 25, 25, 0, 1);
    TEST_ASSERT_FALSE(liquidarBatalha(e, antiga).perfeita);
}

void test_v3_cronometro_pausar_para_o_relogio_e_o_inimigo_nao_se_cura() {
    Cronometro c;
    c.zerar(25);
    c.avancar(10 * 60000, true);
    TEST_ASSERT_EQUAL_INT64(15 * 60000, c.restanteMs());
    c.avancar(5 * 60000, false);                     // pausado 5 min
    TEST_ASSERT_EQUAL_INT64(15 * 60000, c.restanteMs());
    TEST_ASSERT_EQUAL_UINT64(10 * 60000, c.focadoMs);
    TEST_ASSERT_EQUAL_UINT64(0, c.distraidoMs);
    // pausa DURANTE uma distracao: tambem congela (nao cura nem conta distracao)
    c.distrair();
    c.avancar(30000, true);
    const int64_t antes = c.restanteMs();
    c.avancar(5 * 60000, false);
    TEST_ASSERT_EQUAL_INT64(antes, c.restanteMs());
    TEST_ASSERT_EQUAL_UINT64(30000, c.distraidoMs);
}

void test_v3_cronometro_distrai_cura_o_inimigo() {
    Cronometro c;
    c.zerar(25);
    c.avancar(10 * 60000, true);
    c.distrair();
    c.avancar(2 * 60000, true);
    TEST_ASSERT_EQUAL_INT64(17 * 60000, c.restanteMs());   // 15 min + 2 min de cura
    c.voltar();
    c.avancar(60000, true);
    TEST_ASSERT_EQUAL_INT64(16 * 60000, c.restanteMs());
    TEST_ASSERT_EQUAL_INT(1, c.numDistracoes);
}

void test_v3_cronometro_estados_do_bonus() {
    Cronometro c;
    c.zerar(25);
    TEST_ASSERT_TRUE(c.bonus() == BonusPerfeito::Disponivel);
    c.avancar(60000, true);
    c.distrair();
    c.avancar(30000, true);
    TEST_ASSERT_TRUE(c.bonus() == BonusPerfeito::EmRisco);
    c.voltar();
    TEST_ASSERT_TRUE(c.bonus() == BonusPerfeito::Mantido);
    const auto liq = liquidarBatalha(novoEstado(), c.entrada(Categoria::Projetos, false));
    TEST_ASSERT_TRUE(liq.perfeita);                    // a estimativa da tela ja conta o bonus mantido
    c.distrair();                                      // 2a distracao
    TEST_ASSERT_TRUE(c.bonus() == BonusPerfeito::Perdido);
    c.voltar();
    Cronometro longo;
    longo.zerar(25);
    longo.distrair();
    longo.avancar(61000, true);
    TEST_ASSERT_TRUE(longo.bonus() == BonusPerfeito::Perdido);
    Cronometro curto;
    curto.zerar(10);
    TEST_ASSERT_TRUE(curto.bonus() == BonusPerfeito::Indisponivel);
}

void test_v3_categoria_unica_com_projeto_e_livre_sem_projeto() {
    for (int c = 0; c < int(kNumCategorias); ++c) {
        const auto livre = static_cast<Categoria>(c);
        TEST_ASSERT_TRUE(categoriaDoFoco(true, livre) == Categoria::Projetos);     // projeto: sempre Projetos
    }
    TEST_ASSERT_TRUE(categoriaDoFoco(false, Categoria::Meditacao) == Categoria::Meditacao);
    TEST_ASSERT_TRUE(categoriaDoFoco(false, Categoria::Leitura) == Categoria::Leitura);
    TEST_ASSERT_TRUE(categoriaDoFoco(false, Categoria::Projetos) == Categoria::Trabalho);   // livre nunca e "Projetos"
    TEST_ASSERT_TRUE(categoriaLivreValida(-3) == Categoria::Trabalho);
    TEST_ASSERT_TRUE(categoriaLivreValida(99) == Categoria::Trabalho);
    TEST_ASSERT_TRUE(categoriaLivreValida(int(Categoria::Projetos)) == Categoria::Trabalho);
    TEST_ASSERT_EQUAL_STRING("0,40 cr/min", textoTaxa(Categoria::Projetos).c_str());
    TEST_ASSERT_EQUAL_STRING("0,20 cr/min", textoTaxa(Categoria::Leitura).c_str());
}

void test_v3_projetos_recentes_e_paginas_do_seletor() {
    std::vector<std::string> rec;
    registrarProjetoRecente(rec, "c");
    registrarProjetoRecente(rec, "a");
    registrarProjetoRecente(rec, "c");   // repetido: sobe, sem duplicar
    registrarProjetoRecente(rec, "");    // vazio ignorado
    TEST_ASSERT_EQUAL(2, int(rec.size()));
    TEST_ASSERT_EQUAL_STRING("c", rec[0].c_str());
    TEST_ASSERT_EQUAL_STRING("a", rec[1].c_str());
    registrarProjetoRecente(rec, "b");
    TEST_ASSERT_EQUAL(2, int(rec.size()));   // so os 2 ultimos
    TEST_ASSERT_EQUAL_STRING("b", rec[0].c_str());
    TEST_ASSERT_EQUAL_STRING("c", rec[1].c_str());

    const std::vector<std::string> ids = {"a", "b", "c", "d", "e", "f", "g"};
    auto p1 = projetosDaPrimeiraPagina(rec, ids);
    TEST_ASSERT_EQUAL(2, int(p1.size()));
    TEST_ASSERT_EQUAL(1, p1[0]);   // b
    TEST_ASSERT_EQUAL(2, p1[1]);   // c
    // recentes que sumiram da lista: completa com os primeiros
    const std::vector<std::string> sumidos = {"x", "y"};
    p1 = projetosDaPrimeiraPagina(sumidos, ids);
    TEST_ASSERT_EQUAL(2, int(p1.size()));
    TEST_ASSERT_EQUAL(0, p1[0]);
    TEST_ASSERT_EQUAL(1, p1[1]);
    p1 = projetosDaPrimeiraPagina(rec, ids);
    // 7 projetos: pagina 1 (2) + 5 restantes em 2 paginas de 3
    TEST_ASSERT_EQUAL(3, int(paginasDoSeletor(7)));
    const auto p2 = projetosDaPagina(1, p1, 7);
    TEST_ASSERT_EQUAL(3, int(p2.size()));
    TEST_ASSERT_EQUAL(0, p2[0]);   // a
    TEST_ASSERT_EQUAL(3, p2[1]);   // d (b e c ja estao na pagina 1)
    TEST_ASSERT_EQUAL(4, p2[2]);
    const auto p3 = projetosDaPagina(2, p1, 7);
    TEST_ASSERT_EQUAL(2, int(p3.size()));
    TEST_ASSERT_EQUAL(5, p3[0]);
    TEST_ASSERT_EQUAL(6, p3[1]);
    TEST_ASSERT_EQUAL(1, int(paginasDoSeletor(0)));
    TEST_ASSERT_EQUAL(1, int(paginasDoSeletor(2)));
    TEST_ASSERT_EQUAL(2, int(paginasDoSeletor(3)));
    TEST_ASSERT_EQUAL(2, int(paginasDoSeletor(5)));
    TEST_ASSERT_EQUAL(3, int(paginasDoSeletor(6)));
    TEST_ASSERT_EQUAL(0, int(projetosDaPrimeiraPagina({}, {}).size()));
}

void test_v3_estado_guarda_projetos_recentes_no_json() {
    Estado e = novoEstado();
    e.projetosRecentes = {"p2", "p1"};
    Estado lido;
    TEST_ASSERT_TRUE(lerEstado(serializarEstado(e), lido));
    TEST_ASSERT_EQUAL(2, int(lido.projetosRecentes.size()));
    TEST_ASSERT_EQUAL_STRING("p2", lido.projetosRecentes[0].c_str());
    // estado antigo (sem o campo) continua legivel
    Estado antigo;
    TEST_ASSERT_TRUE(lerEstado("{\"saldo\":3.0}", antigo));
    TEST_ASSERT_TRUE(antigo.projetosRecentes.empty());
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_datas);
    RUN_TEST(test_curva_xp);
    RUN_TEST(test_atributos);
    RUN_TEST(test_dia_bom);
    RUN_TEST(test_dia_medio);
    RUN_TEST(test_dia_ruim);
    RUN_TEST(test_perdido);
    RUN_TEST(test_transicao);
    RUN_TEST(test_batalha_perfeita_vs_distracao);
    RUN_TEST(test_batalha_curta_sem_bonus);
    RUN_TEST(test_batalha_overtime_e_fuga);
    RUN_TEST(test_batalha_teto_diario);
    RUN_TEST(test_batalha_bonus_com_teto_e_meditacao);
    RUN_TEST(test_habitos);
    RUN_TEST(test_medicacao_coracoes);
    RUN_TEST(test_humor_nunca_gera_credito);
    RUN_TEST(test_ticktick_e_fechar_dia);
    RUN_TEST(test_teto_carteira_e_cofre);
    RUN_TEST(test_creditos_pagam_divida_primeiro);
    RUN_TEST(test_desafio_cruzar_60_e_limite_diario);
    RUN_TEST(test_desafio_dois_fechamentos_negativos);
    RUN_TEST(test_desafio_niveis_e_reset_na_segunda);
    RUN_TEST(test_desafio_prazo_24h);
    RUN_TEST(test_heroi_caido_e_resgate);
    RUN_TEST(test_coracoes_ganho_max_por_dia);
    RUN_TEST(test_dia_valido_e_limpo);
    RUN_TEST(test_penalidades_padrao_do_produto_e_desligado);
    RUN_TEST(test_penalidades_desligadas_nao_tiram_coracao_nem_disparam_desafio);
    RUN_TEST(test_penalidades_desligadas_mantem_xp_nivel_e_streak);
    RUN_TEST(test_penalidades_ligadas_voltam_tudo);
    RUN_TEST(test_penalidades_alternar_nao_apaga_estado_guardado);
    RUN_TEST(test_streak_com_folga);
    RUN_TEST(test_streak_guarda_brasa_e_folga_reseta_na_segunda);
    RUN_TEST(test_dia_de_recuperacao);
    RUN_TEST(test_ferias_congelam);
    RUN_TEST(test_attr_bonus_estavel_no_dia);
    RUN_TEST(test_projetos_le_fixture);
    RUN_TEST(test_projetos_json_invalido);
    RUN_TEST(test_limpar_titulo_remove_emoji_e_mantem_acentos);
    RUN_TEST(test_estado_ida_e_volta);
    RUN_TEST(test_estado_json_invalido_nao_altera_saida);
    RUN_TEST(test_estado_campos_ausentes_usam_padrao);
    RUN_TEST(test_log_linha_e_caminho);
    RUN_TEST(test_relogio_epoch_e_validade);
    RUN_TEST(test_relogio_escala_e_ancora);
    RUN_TEST(test_foco_livre_liquida_pela_categoria);
    RUN_TEST(test_ciclo_pomodoro_sequencia);
    RUN_TEST(test_ciclo_pomodoro_cada_rodada_liquida_separado);
    RUN_TEST(test_estado_preferencias_da_tela_inicial);
    RUN_TEST(test_log_foco_livre_registra_categoria);
    RUN_TEST(test_perfil_log_ida_e_volta_e_linha_invalida);
    RUN_TEST(test_perfil_datas_e_horas);
    RUN_TEST(test_perfil_resumo_projetos_horas_e_vitorias);
    RUN_TEST(test_perfil_resumo_ida_e_volta_e_reconstrucao);
    RUN_TEST(test_perfil_limita_projetos_e_dias);
    RUN_TEST(test_perfil_faixa_mais_focada_e_media);
    RUN_TEST(test_perfil_semana_agrega_por_dia_e_categoria);
    RUN_TEST(test_perfil_titulos_por_nivel);
    RUN_TEST(test_perfil_conquistas_vitorias_e_maratona);
    RUN_TEST(test_perfil_maratona_4h_num_dia);
    RUN_TEST(test_perfil_streak_conquista_usa_maximo);
    RUN_TEST(test_perfil_monge_sete_dias_seguidos);
    RUN_TEST(test_perfil_madrugador_5_dias_antes_das_7h);
    RUN_TEST(test_perfil_dia_perfeito_exige_todas_perfeitas_e_2h);
    RUN_TEST(test_perfil_proxima_conquista_e_contagem);
    RUN_TEST(test_leitor_rodada_concluida_sem_pausa_e_perfeita);
    RUN_TEST(test_leitor_so_conta_lendo_de_verdade);
    RUN_TEST(test_leitor_pausa_tira_perfeita_mas_nao_vira_fuga);
    RUN_TEST(test_leitor_tempo_pausado_nao_conta_em_parada_parcial);
    RUN_TEST(test_leitor_parar_no_meio_liquida_minutos_parciais_como_fuga);
    RUN_TEST(test_leitor_menos_de_um_minuto_nao_liquida);
    RUN_TEST(test_leitor_intervalo_nao_abre_rodada_e_cada_rodada_liquida_separado);
    RUN_TEST(test_leitor_conclusao_com_foco_ainda_ativo_fecha_e_reabre);
    RUN_TEST(test_leitor_anti_dupla_contagem_com_batalha_do_tempo_ativa);
    RUN_TEST(test_v3_decisao18_uma_distracao_de_ate_1_min_mantem_o_bonus_perfeito);
    RUN_TEST(test_v3_decisao18_segunda_distracao_ou_mais_de_1_min_perde_o_bonus);
    RUN_TEST(test_v3_cronometro_pausar_para_o_relogio_e_o_inimigo_nao_se_cura);
    RUN_TEST(test_v3_cronometro_distrai_cura_o_inimigo);
    RUN_TEST(test_v3_cronometro_estados_do_bonus);
    RUN_TEST(test_v3_categoria_unica_com_projeto_e_livre_sem_projeto);
    RUN_TEST(test_v3_projetos_recentes_e_paginas_do_seletor);
    RUN_TEST(test_v3_estado_guarda_projetos_recentes_no_json);
    RUN_TEST(test_leitor_leitura_da_creditos_e_nunca_por_prazer);
    RUN_TEST(test_leitor_titulo_vazio_e_longo);
    RUN_TEST(test_leitor_evento_ida_e_volta_no_log_e_logs_antigos_continuam_legiveis);
    RUN_TEST(test_leitor_resumo_conta_leitura_como_foco_livre);
    return UNITY_END();
}
