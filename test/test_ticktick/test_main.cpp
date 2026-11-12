#include <unity.h>

#include <string>

#include "tempo/Codec.h"
#include "tempo/Habitos.h"
#include "tempo/TickTick.h"

using namespace tempo;

void setUp() {}
void tearDown() {}

namespace {

    // Resposta fictícia de GET /open/v1/project (campos extras ignorados; nomes reais têm emoji).
    const char* kListas = R"JSON([
      {"id":"inbox1","name":"Entrada","color":"#ff0000","sortOrder":1},
      {"id":"op9","name":"⚙️ Operação e rotinas"},
      {"id":"old3","name":"🎯 PROJETOS antigos","closed":true},
      {"id":"proj7","name":"🎯 PROJETOS","viewMode":"kanban"}
    ])JSON";

    // Resposta fictícia de GET /open/v1/project/proj7/data (content/desc/items devem ser ignorados).
    const char* kDados = R"JSON({
      "project":{"id":"proj7","name":"🎯 PROJETOS"},
      "columns":[
        {"id":"c0","name":"📝REVISAO","sortOrder":1},
        {"id":"c1","name":"🚀 AGORA","sortOrder":2},
        {"id":"c2","name":"⏭️ PRÓXIMOS","sortOrder":3}
      ],
      "tasks":[
        {"id":"t1","projectId":"proj7","columnId":"c1","title":"📚Proj - Lançar o conversor HDMI","priority":0,"status":0,
         "content":"texto longo","desc":"","items":[{"id":"s1","title":"sub","status":0}],
         "tags":["projeto📚"],"dueDate":"2026-12-01T03:00:00.000+0000","timeZone":"America/Sao_Paulo"},
        {"id":"t2","projectId":"proj7","columnId":"c1","title":"Revisar \"Lume\" nano","priority":5,"status":0},
        {"id":"t3","projectId":"proj7","columnId":"c2","title":"Em PRÓXIMOS (outra coluna)","priority":5,"status":0},
        {"id":"t4","projectId":"proj7","columnId":"c1","title":"Concluída","priority":3,"status":2},
        {"id":"t5","projectId":"proj7","columnId":"c1","parentId":"t1","title":"Subtarefa","priority":3,"status":0},
        {"id":"t6","projectId":"proj7","columnId":"c1","title":"Mesma prioridade, vence antes","priority":3,"status":0,
         "dueDate":"2026-11-15T03:00:00.000+0000"},
        {"id":"t7","projectId":"proj7","columnId":"c1","title":"Prioridade 3 sem vencimento","priority":3},
        {"id":"t8","projectId":"proj7","title":"Sem coluna","priority":5,"status":0}
      ]
    })JSON";

} // namespace

void test_nome_contem_ignora_emoji_e_caixa() {
    TEST_ASSERT_TRUE(ticktick::nomeContem("🎯 PROJETOS", "PROJETOS"));
    TEST_ASSERT_TRUE(ticktick::nomeContem("🚀 AGORA", "agora"));
    TEST_ASSERT_TRUE(ticktick::nomeContem("⏭️ PRÓXIMOS", "proximos"));   // acento dobrado
    TEST_ASSERT_TRUE(ticktick::nomeContem("  Agora  ", "AGORA"));
    TEST_ASSERT_FALSE(ticktick::nomeContem("🎯 PROJETOS", "AGORA"));
    TEST_ASSERT_FALSE(ticktick::nomeContem("🚀", "AGORA"));
    TEST_ASSERT_FALSE(ticktick::nomeContem("qualquer", ""));      // alvo vazio nunca bate
    TEST_ASSERT_FALSE(ticktick::nomeContem("qualquer", "🚀"));    // alvo so emoji tambem nao
}

void test_achar_lista_por_nome() {
    const auto lista = ticktick::acharLista(kListas, ticktick::kListaPadrao);
    TEST_ASSERT_TRUE(lista.has_value());
    TEST_ASSERT_EQUAL_STRING("proj7", lista->id.c_str());   // a "PROJETOS antigos" esta arquivada (closed): nao conta
    TEST_ASSERT_EQUAL_STRING("🎯 PROJETOS", lista->nome.c_str());
    // caixa/emoji diferentes e nome configuravel
    TEST_ASSERT_EQUAL_STRING("proj7", ticktick::acharLista(kListas, "projetos")->id.c_str());
    TEST_ASSERT_EQUAL_STRING("op9", ticktick::acharLista(kListas, "operacao")->id.c_str());
    // nome igual ganha de "contem"
    const auto duas = ticktick::acharLista(R"([{"id":"a","name":"Meus projetos pessoais"},{"id":"b","name":"📁 Projetos"}])", "PROJETOS");
    TEST_ASSERT_EQUAL_STRING("b", duas->id.c_str());
}

void test_achar_lista_erros_legiveis() {
    const auto sem = ticktick::acharLista("[{\"id\":\"x\",\"name\":\"Outra\"}]", "PROJETOS");
    TEST_ASSERT_FALSE(sem.has_value());
    TEST_ASSERT_EQUAL_STRING("lista PROJETOS não encontrada", sem.error().c_str());
    TEST_ASSERT_EQUAL_STRING("lista PROJETOS não encontrada", ticktick::acharLista("[]", "PROJETOS").error().c_str());
    TEST_ASSERT_EQUAL_STRING("Resposta do TickTick inválida", ticktick::acharLista("{\"errorCode\":1}", "PROJETOS").error().c_str());
    TEST_ASSERT_EQUAL_STRING("Resposta do TickTick inválida", ticktick::acharLista("nao-json", "PROJETOS").error().c_str());
}

void test_caminho_dos_dados_da_lista() {
    TEST_ASSERT_EQUAL_STRING("/project/proj7/data", ticktick::caminhoDadosLista("proj7").c_str());
    TEST_ASSERT_EQUAL_STRING("", ticktick::caminhoDadosLista("").c_str());
    TEST_ASSERT_EQUAL_STRING("", ticktick::caminhoDadosLista("../x").c_str());   // id vai na URL: so [A-Za-z0-9_-]
    TEST_ASSERT_EQUAL_STRING("", ticktick::caminhoDadosLista("a b").c_str());
}

void test_projetos_da_coluna_agora() {
    const auto json = ticktick::montarProjetosDaColuna(kDados, "🎯 PROJETOS", ticktick::kColunaPadrao, "2026-11-12T12:00:00Z");
    TEST_ASSERT_TRUE(json.has_value());
    ListaProjetos lista;
    TEST_ASSERT_TRUE(lerProjetos(*json, lista));   // mesmo esquema que o Tempo le
    TEST_ASSERT_EQUAL_STRING("ticktick", lista.fonte.c_str());
    TEST_ASSERT_EQUAL_STRING("2026-11-12T12:00:00Z", lista.gerado_em.c_str());
    // so AGORA, abertas, sem subtarefa, sem as de outra coluna / sem coluna: t2, t6, t7, t1
    // ordem: prioridade desc; empate por vencimento; sem vencimento por ultimo
    TEST_ASSERT_EQUAL(4, lista.projetos.size());
    TEST_ASSERT_EQUAL_STRING("t2", lista.projetos[0].id.c_str());   // prioridade 5
    TEST_ASSERT_EQUAL_STRING("t6", lista.projetos[1].id.c_str());   // 3, vence 15/11
    TEST_ASSERT_EQUAL_STRING("t7", lista.projetos[2].id.c_str());   // 3, sem vencimento
    TEST_ASSERT_EQUAL_STRING("t1", lista.projetos[3].id.c_str());   // 0
    TEST_ASSERT_EQUAL_STRING("🎯 PROJETOS", lista.projetos[0].lista.c_str());
    TEST_ASSERT_EQUAL_STRING("proj7", lista.projetos[0].projectId.c_str());
    TEST_ASSERT_EQUAL_STRING("Revisar \"Lume\" nano", lista.projetos[0].titulo.c_str());
    TEST_ASSERT_FALSE(lista.projetos[0].vence.has_value());
    TEST_ASSERT_TRUE(lista.projetos[3].vence.has_value());
    TEST_ASSERT_EQUAL_STRING("Lançar o conversor HDMI", tituloDeProjeto(lista.projetos[3].titulo).c_str());
}

void test_coluna_configuravel_e_outra_coluna() {
    const auto json = ticktick::montarProjetosDaColuna(kDados, "PROJETOS", "proximos", "");
    TEST_ASSERT_TRUE(json.has_value());
    ListaProjetos lista;
    TEST_ASSERT_TRUE(lerProjetos(*json, lista));
    TEST_ASSERT_EQUAL(1, lista.projetos.size());
    TEST_ASSERT_EQUAL_STRING("t3", lista.projetos[0].id.c_str());
}

void test_coluna_agora_vazia_e_arquivo_valido() {
    const char* vazia = R"({"columns":[{"id":"c1","name":"🚀 AGORA"}],"tasks":[
      {"id":"t1","columnId":"c9","title":"x","status":0}]})";
    const auto json = ticktick::montarProjetosDaColuna(vazia, "PROJETOS", "AGORA", "");
    TEST_ASSERT_TRUE(json.has_value());   // coluna existe, so esta vazia: arquivo valido com 0 projetos
    ListaProjetos lista;
    TEST_ASSERT_TRUE(lerProjetos(*json, lista));
    TEST_ASSERT_EQUAL(0, lista.projetos.size());
    // sem "tasks" tambem
    TEST_ASSERT_TRUE(ticktick::montarProjetosDaColuna(R"({"columns":[{"id":"c1","name":"AGORA"}]})", "P", "AGORA", "").has_value());
}

void test_erros_de_coluna_e_resposta() {
    const auto sem = ticktick::montarProjetosDaColuna(R"({"columns":[{"id":"c1","name":"FUTURO"}],"tasks":[]})", "PROJETOS", "AGORA", "");
    TEST_ASSERT_FALSE(sem.has_value());
    TEST_ASSERT_EQUAL_STRING("coluna AGORA não encontrada", sem.error().c_str());
    // lista sem colunas (nao e kanban)
    TEST_ASSERT_EQUAL_STRING("coluna AGORA não encontrada",
                             ticktick::montarProjetosDaColuna(R"({"tasks":[]})", "PROJETOS", "AGORA", "").error().c_str());
    TEST_ASSERT_EQUAL_STRING("coluna Fazendo não encontrada",
                             ticktick::montarProjetosDaColuna(kDados, "PROJETOS", "Fazendo", "").error().c_str());
    TEST_ASSERT_EQUAL_STRING("Resposta do TickTick inválida",
                             ticktick::montarProjetosDaColuna("nao-json", "PROJETOS", "AGORA", "").error().c_str());
    TEST_ASSERT_EQUAL_STRING("Resposta do TickTick inválida",
                             ticktick::montarProjetosDaColuna("[]", "PROJETOS", "AGORA", "").error().c_str());
}

void test_limite_de_projetos_e_bytes() {
    // 200 tarefas na coluna (com content grande, ignorado) -> corta em kMaxProjetos e cabe no limite de bytes
    std::string dados = R"({"columns":[{"id":"c1","name":"AGORA"}],"tasks":[)";
    for (int i = 0; i < 200; ++i) {
        if (i) dados += ',';
        dados += "{\"id\":\"i" + std::to_string(i) + "\",\"columnId\":\"c1\",\"status\":0,\"title\":\"Tarefa numero " + std::to_string(i)
            + "\",\"priority\":" + std::to_string(i % 6) + ",\"content\":\"" + std::string(500, 'x') + "\"}";
    }
    dados += "]}";
    const auto json = ticktick::montarProjetosDaColuna(dados, "PROJETOS", "AGORA", "");
    TEST_ASSERT_TRUE(json.has_value());
    TEST_ASSERT_TRUE(json->size() <= kMaxBytesProjetos);
    ListaProjetos lista;
    TEST_ASSERT_TRUE(lerProjetos(*json, lista));
    TEST_ASSERT_EQUAL(kMaxProjetos, lista.projetos.size());
    TEST_ASSERT_EQUAL(5, lista.projetos[0].prioridade);
}

void test_iso8601() {
    TEST_ASSERT_EQUAL_STRING("2026-11-10T12:00:00Z", ticktick::iso8601Utc(1794312000).c_str());
    TEST_ASSERT_EQUAL_STRING("", ticktick::iso8601Utc(0).c_str());
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_nome_contem_ignora_emoji_e_caixa);
    RUN_TEST(test_achar_lista_por_nome);
    RUN_TEST(test_achar_lista_erros_legiveis);
    RUN_TEST(test_caminho_dos_dados_da_lista);
    RUN_TEST(test_projetos_da_coluna_agora);
    RUN_TEST(test_coluna_configuravel_e_outra_coluna);
    RUN_TEST(test_coluna_agora_vazia_e_arquivo_valido);
    RUN_TEST(test_erros_de_coluna_e_resposta);
    RUN_TEST(test_limite_de_projetos_e_bytes);
    RUN_TEST(test_iso8601);
    return UNITY_END();
}
