#pragma once

#include "tempo/Tipos.h"

namespace tempo::param {

    // Taxas por categoria (economia.md 2.1 + decisao 6: Trabalho 0,20 cr/min).
    constexpr std::array<TaxaCategoria, kNumCategorias> kTaxas = {{
        {0.20, 1.0, Atributo::Oficio},   // Trabalho
        {0.40, 1.0, Atributo::Oficio},   // Projetos
        {0.40, 1.2, Atributo::Mente},    // Estudo
        {0.20, 1.5, Atributo::Mente},    // Leitura
        {0.20, 2.0, Atributo::Mente},    // Meditacao
    }};

    // Meditacao: acima de 30 min/dia vale 0,1 cr/min e 1,0 XP/min.
    constexpr int kMeditacaoTetoMin = 30;
    constexpr double kMeditacaoCrExcesso = 0.1;
    constexpr double kMeditacaoXpExcesso = 1.0;

    // Batalha
    constexpr double kBonusPerfeita = 0.25;
    constexpr int kDuracaoMinBonusPerfeita = 15;
    constexpr int kDistracaoPerdidoMin = 2;       // distraidos > 2 min viram Perdido
    constexpr int kMaxDistracoes = 3;             // > 3 => fuga
    constexpr int kDistracaoLongaMin = 10;        // 1 distracao > 10 min => fuga
    constexpr double kOvertimeMax = 1.0;          // +100 % do planejado
    constexpr int kTetoFocoCrMin = 360;           // 6 h
    constexpr int kTetoFocoXpMin = 480;           // 8 h
    constexpr double kFatorAposTeto = 0.5;
    constexpr double kTetoBonusCr = 0.40;
    constexpr double kTetoBonusXp = 0.50;
    constexpr double kOficioPorNivel = 0.005;     // ate 0,10
    constexpr double kMentePorNivel = 0.005;      // ate 0,10
    constexpr double kTetoAtributoBonus = 0.10;

    // Habitos (economia.md 3)
    constexpr std::array<ValorHabito, kNumHabitos> kHabitos = {{
        {15.0, 40.0, Atributo::Corpo, 60},    // Academia
        {2.0, 5.0, Atributo::Corpo, 30},      // Suplemento
        {2.0, 5.0, Atributo::Corpo, 45},      // Almoco
        {2.0, 5.0, Atributo::Corpo, 60},      // Banho
        {2.0, 5.0, Atributo::Corpo, 30},      // Dentes
        {2.0, 10.0, Atributo::Energia, 5},    // Descanso (volta ate +5 min)
        {3.0, 10.0, Atributo::Energia, 30},   // Medicacao
    }};
    constexpr int kJanelaAtrasoMin = 120;
    constexpr double kXpAtrasadoFator = 0.5;
    constexpr double kXpHumor = 2.0;
    constexpr int kHumorMaxDia = 3;
    constexpr double kXpFecharDia = 20.0;
    constexpr double kXpTickTick = 30.0;
    constexpr int kTickTickMaxDia = 3;

    // Transicao / perdido / lazer
    constexpr int kToleranciaTransicaoMin = 10;
    constexpr int kOkOrcamentoMin = 45;
    constexpr int kPerdidoGratisMin = 30;
    constexpr double kPrecoLazer = 1.0;
    constexpr double kPrecoPerdido = 1.0;
    constexpr int kPerdidoCoracaoMin = 90;

    // Carteira / divida / desafio
    constexpr double kTetoCarteira = 600.0;
    constexpr double kPatamarDivida = 60.0;
    constexpr int kFechamentosNegativos = 2;
    constexpr int kPrazoDesafioMin = 1440;
    constexpr int kNivelMaxDesafio = 3;

    // Corações (em meios)
    constexpr int kCoracoesBaseMeios = 10;
    constexpr int kCoracoesMaxMeios = 14;
    constexpr int kGanhoMaxDiaMeios = 2;
    constexpr int kResgateMeios = 6;
    constexpr int kResgateDiasValidos = 2;

    // Dia valido / streak / salvaguardas
    constexpr int kDiaValidoFocoMin = 50;
    constexpr int kDiaValidoHabitos = 4;
    constexpr double kHabitosBomFracao = 0.8;
    constexpr int kDiaLimpoPerdidoMax = 30;
    constexpr int kFolgasStreakBase = 1;
    constexpr int kRecuperacoesPorMes = 2;
    constexpr int kRecuperacaoLimiteMinutoDia = 12 * 60;
    constexpr int kFeriasMinDias = 2;

    // Progressao
    constexpr int kNivelMax = 30;
    constexpr int kNivelAtributoMax = 20;
    constexpr std::array<double, kNumAtributos> kK = {150.0, 60.0, 30.0, 20.0};  // Oficio, Mente, Corpo, Energia

} // namespace tempo::param
