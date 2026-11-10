#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdio>

#include <string>

#include "tempo/Codec.h"
#include "tempo/Motor.h"
#include "tempo/Parametros.h"
#include "tempo/Progressao.h"
#include "tempo/Relogio.h"

using namespace tempo;

void setUp() {}

void tearDown() {}

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
        Estado f = novoEstado();
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
    return UNITY_END();
}
