#include <unity.h>

#include <string>

#include "tempo/Codec.h"
#include "tempo/TickTick.h"

using namespace tempo;

void setUp() {}
void tearDown() {}

namespace {

    // Resposta fictícia de POST /open/v1/task/filter (campos extras devem ser ignorados).
    const char* kTarefas = R"([
      {"id":"t1","projectId":"p2","title":"Escrever capítulo 3 📚","priority":0,"status":0,
       "content":"texto longo","desc":"","items":[{"id":"s1","title":"sub","status":0}],
       "tags":["projeto📚","livro"],"dueDate":"2026-12-01T03:00:00.000+0000","timeZone":"America/Sao_Paulo"},
      {"id":"t2","projectId":"p1","title":"Revisar \"Lume\" nano","priority":5,"status":0,"tags":["projeto📚"]},
      {"id":"t3","projectId":"p9","title":"Sem lista conhecida","priority":3,"tags":null,
       "dueDate":"2026-11-20T03:00:00.000+0000"},
      {"id":"t4","projectId":"p1","title":"Mesma prioridade, vence antes","priority":3,"tags":[],
       "dueDate":"2026-11-15T03:00:00.000+0000"}
    ])";

    const char* kListas = R"([
      {"id":"p1","name":"Trabalho","color":"#ff0000","sortOrder":1},
      {"id":"p2","name":"Livros"}
    ])";

} // namespace

void test_variantes_tag() {
    const auto v = ticktick::variantesTag("Projeto\xF0\x9F\x93\x9A");
    TEST_ASSERT_EQUAL(2, v.size());
    TEST_ASSERT_EQUAL_STRING("Projeto\xF0\x9F\x93\x9A", v[0].c_str());
    TEST_ASSERT_EQUAL_STRING("projeto\xF0\x9F\x93\x9A", v[1].c_str());
    TEST_ASSERT_EQUAL(1, ticktick::variantesTag("lume").size());
    TEST_ASSERT_EQUAL(0, ticktick::variantesTag("").size());
}

void test_corpo_filtro() {
    TEST_ASSERT_EQUAL_STRING("{\"tag\":[\"a\\\"b\"],\"status\":[0]}", ticktick::corpoFiltro("a\"b").c_str());
}

void test_contar() {
    TEST_ASSERT_EQUAL(4, ticktick::contarTarefas(kTarefas));
    TEST_ASSERT_EQUAL(0, ticktick::contarTarefas("[]"));
    TEST_ASSERT_EQUAL(-1, ticktick::contarTarefas("{\"errorCode\":\"x\"}"));
}

void test_montar_e_ordenar() {
    const auto json = ticktick::montarProjetos(kTarefas, kListas, "Projeto\xF0\x9F\x93\x9A", "2026-11-10T12:00:00Z");
    TEST_ASSERT_TRUE(json.has_value());
    ListaProjetos lista;
    TEST_ASSERT_TRUE(lerProjetos(*json, lista));  // mesmo esquema que o Tempo le
    TEST_ASSERT_EQUAL_STRING("ticktick", lista.fonte.c_str());
    TEST_ASSERT_EQUAL_STRING("2026-11-10T12:00:00Z", lista.gerado_em.c_str());
    TEST_ASSERT_EQUAL(4, lista.projetos.size());
    // prioridade desc; empate por vencimento; sem vencimento por ultimo
    TEST_ASSERT_EQUAL_STRING("t2", lista.projetos[0].id.c_str());
    TEST_ASSERT_EQUAL_STRING("t4", lista.projetos[1].id.c_str());
    TEST_ASSERT_EQUAL_STRING("t3", lista.projetos[2].id.c_str());
    TEST_ASSERT_EQUAL_STRING("t1", lista.projetos[3].id.c_str());
    TEST_ASSERT_EQUAL_STRING("Trabalho", lista.projetos[0].lista.c_str());
    TEST_ASSERT_EQUAL_STRING("?", lista.projetos[2].lista.c_str());
    TEST_ASSERT_EQUAL_STRING("Livros", lista.projetos[3].lista.c_str());
    TEST_ASSERT_EQUAL_STRING("Revisar \"Lume\" nano", lista.projetos[0].titulo.c_str());
    TEST_ASSERT_FALSE(lista.projetos[0].vence.has_value());
    TEST_ASSERT_TRUE(lista.projetos[3].vence.has_value());
    TEST_ASSERT_EQUAL(2, lista.projetos[3].tags.size());
    TEST_ASSERT_EQUAL_STRING("Escrever capítulo 3", limparTitulo(lista.projetos[3].titulo).c_str());
}

void test_limites_e_erros() {
    TEST_ASSERT_FALSE(ticktick::montarProjetos("{\"errorCode\":1}", kListas, "x", "").has_value());
    TEST_ASSERT_FALSE(ticktick::montarProjetos(kTarefas, "nao-json", "x", "").has_value());
    // lista de projetos vazia ainda gera arquivo valido (tarefas sem nome de lista -> "?")
    TEST_ASSERT_TRUE(ticktick::montarProjetos("[]", "[]", "x", "").has_value());
    // 200 tarefas -> corta em kMaxProjetos e cabe no limite de bytes
    std::string muitas = "[";
    for (int i = 0; i < 200; ++i) {
        if (i) muitas += ',';
        muitas += "{\"id\":\"i" + std::to_string(i) + "\",\"title\":\"Tarefa numero " + std::to_string(i)
            + "\",\"priority\":" + std::to_string(i % 6) + ",\"projectId\":\"p1\"}";
    }
    muitas += "]";
    const auto json = ticktick::montarProjetos(muitas, kListas, "x", "");
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
    RUN_TEST(test_variantes_tag);
    RUN_TEST(test_corpo_filtro);
    RUN_TEST(test_contar);
    RUN_TEST(test_montar_e_ordenar);
    RUN_TEST(test_limites_e_erros);
    RUN_TEST(test_iso8601);
    return UNITY_END();
}
