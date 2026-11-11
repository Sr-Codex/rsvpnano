#include <unity.h>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <vector>

#include "som/Regras.h"
#include "som/Sintese.h"

using namespace som;

void setUp() {}
void tearDown() {}

namespace {

    // Renderiza o som inteiro em blocos de `bloco` amostras (testa a emenda entre blocos).
    std::vector<int16_t> renderizar(SomId id, float ganho = 1.0f, size_t bloco = 100) {
        Sintetizador s;
        s.iniciar(id, ganho);
        std::vector<int16_t> out, buf(bloco);
        for (;;) {
            const size_t n = s.gerar(buf.data(), buf.size());
            if (n == 0) break;
            out.insert(out.end(), buf.begin(), buf.begin() + n);
        }
        return out;
    }

    int pico(const std::vector<int16_t>& v) {
        int p = 0;
        for (int16_t x : v) p = std::max(p, std::abs(int(x)));
        return p;
    }

    // Cruzamentos de zero de um trecho [a, b)
    int cruzamentos(const std::vector<int16_t>& v, size_t a, size_t b) {
        int c = 0;
        for (size_t i = a + 1; i < b; ++i)
            if ((v[i - 1] > 0) != (v[i] > 0) && v[i - 1] != 0 && v[i] != 0) ++c;
        return c;
    }

    // Referencia independente em double: copia literal das formulas do chip.py / chip2.py, a 16 kHz.
    std::vector<int16_t> referencia(SomId id) {
        const Som& def = definicao(id);
        const double SR = 16000.0;
        std::vector<int16_t> out;
        for (const Nota& nota : def.notas) {
            const int n = int(nota.ms) * 16;
            if (nota.hz <= 0) {
                out.insert(out.end(), size_t(n), 0);
                continue;
            }
            const double vol = def.estilo == Estilo::A ? 0.25 : 0.22;
            const int ataque = def.estilo == Estilo::A ? 18 : 22;   // 50 / 60 amostras @44,1 kHz -> 16 kHz
            double ph = 0.0;
            for (int i = 0; i < n; ++i) {
                const double t = i / SR;
                double f = nota.hz;
                if (nota.slideHz != 0.0f) f = nota.hz + (nota.slideHz - nota.hz) * (double(i) / n);
                if (nota.vib != 0.0f) f *= 1 + nota.vib * std::sin(2 * M_PI * 6 * t);
                double fase;
                if (def.estilo == Estilo::A) {
                    fase = std::fmod(t * f, 1.0);
                } else {
                    ph = std::fmod(ph + f / SR, 1.0);
                    fase = ph;
                }
                const double s = fase < nota.duty ? 1.0 : -1.0;
                double env = def.estilo == Estilo::A ? 1 - std::pow(double(i) / n, 2.0)
                           : nota.decay > 0 ? std::pow(1 - double(i) / n, nota.decay) : 1.0;
                if (i < ataque) env *= double(i) / ataque;
                const double v = std::max(-1.0, std::min(1.0, s * vol * env));
                out.push_back(static_cast<int16_t>(v * 32767));
            }
        }
        return out;
    }

} // namespace

// ------------------------------------------------------------------ sintese
void test_duracao_e_amostras_de_cada_som() {
    constexpr uint32_t esperadoMs[kNumSons] = {90, 260, 280, 290, 180, 300, 970, 940, 800, 1220, 940, 1200};
    for (size_t i = 0; i < kNumSons; ++i) {
        const SomId id = static_cast<SomId>(i);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(esperadoMs[i], duracaoMs(id), nome(id));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(esperadoMs[i] * 16, totalAmostras(id), nome(id));
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(totalAmostras(id), renderizar(id).size(), nome(id));          // blocos de 100
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(totalAmostras(id), renderizar(id, 1.0f, 777).size(), nome(id));
    }
}

void test_nomes_dos_12_sons() {
    const char* nomes[] = {"Bip", "SobeTres", "Moeda", "SobeQuatro", "DoisBipes", "DesceAlerta",
                           "Vitoria", "FaseCompleta", "PowerUp", "Jingle", "AlertaLongo", "SuaveDescendo"};
    TEST_ASSERT_EQUAL(12, kNumSons);
    for (size_t i = 0; i < kNumSons; ++i) TEST_ASSERT_EQUAL_STRING(nomes[i], nome(static_cast<SomId>(i)));
}

void test_envelope_sem_clique_no_inicio() {
    for (size_t i = 0; i < kNumSons; ++i) {
        const SomId id = static_cast<SomId>(i);
        const auto v = renderizar(id);
        TEST_ASSERT_EQUAL_INT_MESSAGE(0, v[0], nome(id));   // comeca em zero
        // rampa de ataque: |amostra| nao decresce nas primeiras 16 amostras e o 1o passo e pequeno
        for (int k = 1; k < 16; ++k) TEST_ASSERT_TRUE_MESSAGE(std::abs(v[k]) >= std::abs(v[k - 1]), nome(id));
        TEST_ASSERT_TRUE_MESSAGE(std::abs(v[1]) < pico(v) / 8, nome(id));
    }
}

void test_decaimento_termina_perto_de_zero() {
    for (size_t i = 0; i < kNumSons; ++i) {
        const SomId id = static_cast<SomId>(i);
        const auto v = renderizar(id);
        TEST_ASSERT_TRUE_MESSAGE(std::abs(v.back()) * 50 <= pico(v), nome(id));   // a ultima nota decai ate ~0 (<= 2 % do pico)
    }
}

void test_volume_e_ganho() {
    const int base = pico(renderizar(SomId::Bip, ganhoDoVolume(Volume::Medio)));
    TEST_ASSERT_INT_WITHIN(250, 8191, base);   // 0,25 do fundo de escala (menos o decaimento ate o fim do ataque)
    const int baixo = pico(renderizar(SomId::Bip, ganhoDoVolume(Volume::Baixo)));
    const int alto = pico(renderizar(SomId::Bip, ganhoDoVolume(Volume::Alto)));
    TEST_ASSERT_TRUE(baixo < base && base < alto);
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 0.4f, float(baixo) / float(base));
    TEST_ASSERT_FLOAT_WITHIN(0.03f, 1.6f, float(alto) / float(base));
    TEST_ASSERT_TRUE(alto < 32767);   // sem clipping no volume alto
    for (size_t i = 0; i < kNumSons; ++i)   // nenhum som estoura no alto
        TEST_ASSERT_TRUE_MESSAGE(pico(renderizar(static_cast<SomId>(i), ganhoDoVolume(Volume::Alto))) < 32000,
                                 nome(static_cast<SomId>(i)));
}

void test_moeda_nova_sol5_para_do6() {
    const Som& moeda = definicao(SomId::Moeda);
    TEST_ASSERT_EQUAL(2, moeda.notas.size());
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 783.99f, moeda.notas[0].hz);    // sol5
    TEST_ASSERT_EQUAL(60, moeda.notas[0].ms);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 1046.5f, moeda.notas[1].hz);    // do6 (nao mais o mi6 1318,5)
    TEST_ASSERT_EQUAL(220, moeda.notas[1].ms);
    const auto v = renderizar(SomId::Moeda);
    // frequencia medida pelos cruzamentos de zero: 783,99 Hz x 60 ms ~ 47 ciclos; 1046,5 Hz x 220 ms ~ 230 ciclos
    TEST_ASSERT_INT_WITHIN(4, 2 * 47, cruzamentos(v, 0, 960));
    TEST_ASSERT_INT_WITHIN(6, 2 * 230, cruzamentos(v, 960, v.size()));
}

void test_pausas_sao_silencio_exato() {
    const auto v = renderizar(SomId::DoisBipes);   // 70 ms, pausa 40 ms, 70 ms
    for (size_t i = 70 * 16; i < 110 * 16; ++i) TEST_ASSERT_EQUAL_INT16(0, v[i]);
    TEST_ASSERT_TRUE(pico(std::vector<int16_t>(v.begin(), v.begin() + 70 * 16)) > 0);
    TEST_ASSERT_TRUE(pico(std::vector<int16_t>(v.begin() + 110 * 16, v.end())) > 0);
}

void test_power_up_glissando_sobe() {
    const auto v = renderizar(SomId::PowerUp);
    const int inicio = cruzamentos(v, 800, 1600), fim = cruzamentos(v, 8000, 8800);   // 50 ms no comeco e perto do fim do glissando
    TEST_ASSERT_TRUE(fim > inicio * 2);
}

void test_confere_com_referencia_double() {
    // Mesma formula em double (copia do chip.py/chip2.py a 16 kHz): so pode divergir por arredondamento de fase
    // (amostras exatamente na borda da quadrada: ex. A5 a 16 kHz cai em fase 0,500000 a cada 100 amostras e a
    // ordem de arredondamento decide o sinal), nunca em forma/duracao. Tolerancia: 1 % das amostras.
    for (size_t i = 0; i < kNumSons; ++i) {
        const SomId id = static_cast<SomId>(i);
        const auto a = renderizar(id), b = referencia(id);
        TEST_ASSERT_EQUAL_UINT32_MESSAGE(b.size(), a.size(), nome(id));
        size_t diferentes = 0;
        for (size_t k = 0; k < a.size(); ++k)
            if (std::abs(int(a[k]) - int(b[k])) > 2) ++diferentes;
        TEST_ASSERT_TRUE_MESSAGE(diferentes * 100 <= a.size(), nome(id));
    }
}

// ------------------------------------------------------------------ regras
void test_classes_dos_sons() {
    TEST_ASSERT_EQUAL(Classe::Interface, classeDe(SomId::Bip));
    TEST_ASSERT_EQUAL(Classe::Lembrete, classeDe(SomId::DoisBipes));
    TEST_ASSERT_EQUAL(Classe::Medicacao, classeDe(SomId::AlertaLongo));
    for (SomId id : {SomId::SobeTres, SomId::Moeda, SomId::SobeQuatro, SomId::DesceAlerta, SomId::Vitoria,
                     SomId::FaseCompleta, SomId::PowerUp, SomId::Jingle, SomId::SuaveDescendo})
        TEST_ASSERT_EQUAL(Classe::Jogo, classeDe(id));
}

void test_silencio_janela_que_cruza_a_meia_noite() {
    Config c;
    c.silencioAtivo = true;   // 23:00-07:00
    TEST_ASSERT_TRUE(emSilencio(c, 23 * 60));
    TEST_ASSERT_TRUE(emSilencio(c, 23 * 60 + 59));
    TEST_ASSERT_TRUE(emSilencio(c, 0));
    TEST_ASSERT_TRUE(emSilencio(c, 3 * 60));
    TEST_ASSERT_TRUE(emSilencio(c, 6 * 60 + 59));
    TEST_ASSERT_FALSE(emSilencio(c, 7 * 60));
    TEST_ASSERT_FALSE(emSilencio(c, 12 * 60));
    TEST_ASSERT_FALSE(emSilencio(c, 22 * 60 + 59));
    c.silencioAtivo = false;
    TEST_ASSERT_FALSE(emSilencio(c, 3 * 60));
}

void test_silencio_janela_no_mesmo_dia_e_casos_limite() {
    Config c;
    c.silencioAtivo = true;
    c.silencioInicioMin = 13 * 60;
    c.silencioFimMin = 15 * 60;
    TEST_ASSERT_TRUE(emSilencio(c, 13 * 60));
    TEST_ASSERT_TRUE(emSilencio(c, 14 * 60 + 59));
    TEST_ASSERT_FALSE(emSilencio(c, 15 * 60));
    TEST_ASSERT_FALSE(emSilencio(c, 12 * 60));
    c.silencioFimMin = c.silencioInicioMin;   // janela vazia: nunca em silencio
    TEST_ASSERT_FALSE(emSilencio(c, 13 * 60));
    c.silencioInicioMin = 23 * 60;
    c.silencioFimMin = 7 * 60;
    TEST_ASSERT_FALSE(emSilencio(c, -1));   // relogio invalido: toca
}

void test_padroes_da_config() {
    const Config c;
    TEST_ASSERT_EQUAL(Volume::Medio, c.volume);
    TEST_ASSERT_TRUE(c.sonsJogo);
    TEST_ASSERT_FALSE(c.silencioAtivo);
    TEST_ASSERT_EQUAL(23 * 60, c.silencioInicioMin);
    TEST_ASSERT_EQUAL(7 * 60, c.silencioFimMin);
}

void test_sons_de_jogo_desligados_so_cortam_jogo() {
    Config c;
    c.sonsJogo = false;
    for (SomId id : {SomId::SobeTres, SomId::Moeda, SomId::Vitoria, SomId::PowerUp, SomId::Jingle, SomId::FaseCompleta,
                     SomId::SobeQuatro, SomId::DesceAlerta, SomId::SuaveDescendo}) {
        const auto d = decidir(id, c, 12 * 60);
        TEST_ASSERT_FALSE_MESSAGE(d.tocar, nome(id));
        TEST_ASSERT_EQUAL(Motivo::SonsDeJogoDesligados, d.motivo);
    }
    TEST_ASSERT_TRUE(decidir(SomId::DoisBipes, c, 12 * 60).tocar);    // lembretes sempre tocam
    TEST_ASSERT_TRUE(decidir(SomId::AlertaLongo, c, 12 * 60).tocar);
    TEST_ASSERT_TRUE(decidir(SomId::Bip, c, 12 * 60).tocar);          // bip de interface nao e "jogo"
}

void test_silencio_lembrete_comum_respeita_medicacao_nao() {
    Config c;
    c.silencioAtivo = true;
    const int noite = 23 * 60 + 30;
    TEST_ASSERT_FALSE(decidir(SomId::DoisBipes, c, noite).tocar);
    TEST_ASSERT_EQUAL(Motivo::Silencio, decidir(SomId::DoisBipes, c, noite).motivo);
    TEST_ASSERT_FALSE(decidir(SomId::Vitoria, c, noite).tocar);
    TEST_ASSERT_FALSE(decidir(SomId::Bip, c, noite).tocar);
    TEST_ASSERT_TRUE(decidir(SomId::AlertaLongo, c, noite).tocar);   // medicacao toca mesmo no silencio
    c.sonsJogo = false;
    TEST_ASSERT_TRUE(decidir(SomId::AlertaLongo, c, noite).tocar);   // ... e mesmo com jogo desligado
    TEST_ASSERT_TRUE(decidir(SomId::DoisBipes, c, 12 * 60).tocar);   // fora da janela, o lembrete comum toca
}

void test_volume_vai_no_ganho_da_decisao() {
    Config c;
    c.volume = Volume::Baixo;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, decidir(SomId::Bip, c, 12 * 60).ganho);
    c.volume = Volume::Alto;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.6f, decidir(SomId::Bip, c, 12 * 60).ganho);
    c.volume = Volume::Medio;
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 1.0f, decidir(SomId::Bip, c, 12 * 60).ganho);
}

void test_escolher_um_som_por_evento_composto() {
    TEST_ASSERT_EQUAL(SomId::Jingle, *escolher({SomId::Moeda, SomId::PowerUp, SomId::Jingle}));
    TEST_ASSERT_EQUAL(SomId::PowerUp, *escolher({SomId::Vitoria, SomId::PowerUp}));
    TEST_ASSERT_EQUAL(SomId::FaseCompleta, *escolher({SomId::SobeTres, SomId::FaseCompleta}));
    TEST_ASSERT_EQUAL(SomId::PowerUp, *escolher({SomId::SobeTres, SomId::FaseCompleta, SomId::PowerUp}));
    TEST_ASSERT_EQUAL(SomId::Moeda, *escolher({SomId::Moeda, SomId::Bip}));
    TEST_ASSERT_EQUAL(SomId::Moeda, *escolher({SomId::DesceAlerta, SomId::Moeda}));
    TEST_ASSERT_EQUAL(SomId::SuaveDescendo, *escolher({SomId::SuaveDescendo, SomId::Bip}));
    TEST_ASSERT_FALSE(escolher({}).has_value());
}

// ------------------------------------------------------------------ arbitro (fila)
namespace {
    Pedido pedido(SomId id) { return {id, 1.0f, classeDe(id)}; }
}

void test_arbitro_sem_nada_tocando_toca() {
    Arbitro a;
    TEST_ASSERT_EQUAL(Arbitro::Acao::TocarAgora, a.pedir(pedido(SomId::Vitoria)));
    TEST_ASSERT_EQUAL(SomId::Vitoria, a.atual()->som);
}

void test_arbitro_lembrete_interrompe_jogo() {
    Arbitro a;
    a.pedir(pedido(SomId::Vitoria));
    TEST_ASSERT_EQUAL(Arbitro::Acao::TocarAgora, a.pedir(pedido(SomId::DoisBipes)));
    TEST_ASSERT_EQUAL(SomId::DoisBipes, a.atual()->som);
    TEST_ASSERT_FALSE(a.aguardando().has_value());   // o jogo interrompido nao volta
}

void test_arbitro_medicacao_interrompe_tudo_e_lembrete_interrompido_aguarda() {
    Arbitro a;
    a.pedir(pedido(SomId::DoisBipes));
    TEST_ASSERT_EQUAL(Arbitro::Acao::TocarAgora, a.pedir(pedido(SomId::AlertaLongo)));
    TEST_ASSERT_EQUAL(SomId::AlertaLongo, a.atual()->som);
    TEST_ASSERT_TRUE(a.aguardando().has_value());
    TEST_ASSERT_EQUAL(SomId::DoisBipes, a.aguardando()->som);
    const auto proximo = a.concluir();   // acabou o alerta longo: o lembrete comum interrompido toca
    TEST_ASSERT_TRUE(proximo.has_value());
    TEST_ASSERT_EQUAL(SomId::DoisBipes, proximo->som);
    TEST_ASSERT_FALSE(a.concluir().has_value());
    TEST_ASSERT_FALSE(a.ocupado());
}

void test_arbitro_jogo_sobre_jogo_substitui() {
    Arbitro a;
    a.pedir(pedido(SomId::SobeTres));
    TEST_ASSERT_EQUAL(Arbitro::Acao::TocarAgora, a.pedir(pedido(SomId::Moeda)));
    TEST_ASSERT_EQUAL(SomId::Moeda, a.atual()->som);
    TEST_ASSERT_FALSE(a.aguardando().has_value());
}

void test_arbitro_jogo_e_bip_nao_interrompem_lembrete() {
    Arbitro a;
    a.pedir(pedido(SomId::AlertaLongo));
    TEST_ASSERT_EQUAL(Arbitro::Acao::Descartar, a.pedir(pedido(SomId::Vitoria)));
    TEST_ASSERT_EQUAL(Arbitro::Acao::Descartar, a.pedir(pedido(SomId::Bip)));
    TEST_ASSERT_EQUAL(SomId::AlertaLongo, a.atual()->som);
    Arbitro b;
    b.pedir(pedido(SomId::DoisBipes));
    TEST_ASSERT_EQUAL(Arbitro::Acao::Descartar, b.pedir(pedido(SomId::PowerUp)));
}

void test_arbitro_bip_nao_interrompe_jogo_nem_bip() {
    Arbitro a;
    a.pedir(pedido(SomId::Vitoria));
    TEST_ASSERT_EQUAL(Arbitro::Acao::Descartar, a.pedir(pedido(SomId::Bip)));
    Arbitro b;
    b.pedir(pedido(SomId::Bip));
    TEST_ASSERT_EQUAL(Arbitro::Acao::Descartar, b.pedir(pedido(SomId::Bip)));
    TEST_ASSERT_EQUAL(Arbitro::Acao::TocarAgora, b.pedir(pedido(SomId::Vitoria)));   // jogo interrompe o bip
}

void test_arbitro_lembrete_durante_medicacao_aguarda_e_mesmo_som_e_descartado() {
    Arbitro a;
    a.pedir(pedido(SomId::AlertaLongo));
    TEST_ASSERT_EQUAL(Arbitro::Acao::Aguardar, a.pedir(pedido(SomId::DoisBipes)));
    TEST_ASSERT_EQUAL(Arbitro::Acao::Descartar, a.pedir(pedido(SomId::DoisBipes)));   // ja aguardando
    TEST_ASSERT_EQUAL(Arbitro::Acao::Descartar, a.pedir(pedido(SomId::AlertaLongo)));  // mesma insistencia
    TEST_ASSERT_EQUAL(SomId::DoisBipes, a.concluir()->som);
}

void test_arbitro_parar_limpa_tudo() {
    Arbitro a;
    a.pedir(pedido(SomId::AlertaLongo));
    a.pedir(pedido(SomId::DoisBipes));
    a.parar();
    TEST_ASSERT_FALSE(a.ocupado());
    TEST_ASSERT_FALSE(a.aguardando().has_value());
    TEST_ASSERT_EQUAL(Arbitro::Acao::TocarAgora, a.pedir(pedido(SomId::Bip)));
}

int main(int, char**) {
    UNITY_BEGIN();
    RUN_TEST(test_duracao_e_amostras_de_cada_som);
    RUN_TEST(test_nomes_dos_12_sons);
    RUN_TEST(test_envelope_sem_clique_no_inicio);
    RUN_TEST(test_decaimento_termina_perto_de_zero);
    RUN_TEST(test_volume_e_ganho);
    RUN_TEST(test_moeda_nova_sol5_para_do6);
    RUN_TEST(test_pausas_sao_silencio_exato);
    RUN_TEST(test_power_up_glissando_sobe);
    RUN_TEST(test_confere_com_referencia_double);
    RUN_TEST(test_classes_dos_sons);
    RUN_TEST(test_silencio_janela_que_cruza_a_meia_noite);
    RUN_TEST(test_silencio_janela_no_mesmo_dia_e_casos_limite);
    RUN_TEST(test_padroes_da_config);
    RUN_TEST(test_sons_de_jogo_desligados_so_cortam_jogo);
    RUN_TEST(test_silencio_lembrete_comum_respeita_medicacao_nao);
    RUN_TEST(test_volume_vai_no_ganho_da_decisao);
    RUN_TEST(test_escolher_um_som_por_evento_composto);
    RUN_TEST(test_arbitro_sem_nada_tocando_toca);
    RUN_TEST(test_arbitro_lembrete_interrompe_jogo);
    RUN_TEST(test_arbitro_medicacao_interrompe_tudo_e_lembrete_interrompido_aguarda);
    RUN_TEST(test_arbitro_jogo_sobre_jogo_substitui);
    RUN_TEST(test_arbitro_jogo_e_bip_nao_interrompem_lembrete);
    RUN_TEST(test_arbitro_bip_nao_interrompe_jogo_nem_bip);
    RUN_TEST(test_arbitro_lembrete_durante_medicacao_aguarda_e_mesmo_som_e_descartado);
    RUN_TEST(test_arbitro_parar_limpa_tudo);
    return UNITY_END();
}
