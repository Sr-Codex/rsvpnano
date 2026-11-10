#include <unity.h>
#include <glaze/toml.hpp>
#include <string>
#include <vector>

#include "reader/ReadingLoop.h"
#include "reader/ReadingMarks.h"
#include "reader/ReadingSession.h"
#include "settings/SettingsGlaze.h"

using namespace reading;

void setUp() {}
void tearDown() {}

static constexpr uint32_t kWords = 10000;

void test_jump_saves_current_position_as_resume() {
    ResumePoint r;
    ResumeTracker t;
    TEST_ASSERT_TRUE(noteJump(r, t, 500, 4000, 3, kWords));
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_TRUE(r.pending);
    TEST_ASSERT_EQUAL_UINT32(500, r.wordIndex);
    TEST_ASSERT_EQUAL_UINT32(3, r.chapter);
    TEST_ASSERT_EQUAL_UINT32(5, r.percent);
    TEST_ASSERT_EQUAL_UINT32(4000, t.lastWord);
}

void test_jump_to_same_word_is_not_a_jump() {
    ResumePoint r;
    ResumeTracker t;
    TEST_ASSERT_FALSE(noteJump(r, t, 500, 500, 0, kWords));
    TEST_ASSERT_FALSE(r.valid);
}

void test_chained_jumps_keep_the_last_really_read_place() {
    ResumePoint r;
    ResumeTracker t;
    noteJump(r, t, 500, 4000, 1, kWords); // A -> B
    noteJump(r, t, 4000, 7000, 2, kWords); // B -> C without reading B
    TEST_ASSERT_EQUAL_UINT32(500, r.wordIndex);
}

void test_jump_back_to_resume_clears_it() {
    ResumePoint r;
    ResumeTracker t;
    noteJump(r, t, 500, 4000, 1, kWords);
    TEST_ASSERT_TRUE(noteJump(r, t, 4000, 500, 1, kWords));
    TEST_ASSERT_FALSE(r.valid);
}

void test_reading_enough_words_settles_the_new_place() {
    ResumePoint r;
    ResumeTracker t;
    noteJump(r, t, 500, 4000, 1, kWords);
    uint32_t word = 4000;
    bool settled = false;
    for (int i = 0; i < 500 && !settled; ++i) settled = tick(r, t, ++word, 200, true);
    TEST_ASSERT_TRUE(settled);
    TEST_ASSERT_FALSE(r.valid);
    // The next jump saves the live position again.
    noteJump(r, t, word, 10, 0, kWords);
    TEST_ASSERT_EQUAL_UINT32(word, r.wordIndex);
}

void test_reading_for_two_minutes_settles() {
    ResumePoint r;
    ResumeTracker t;
    noteJump(r, t, 500, 4000, 1, kWords);
    bool settled = false;
    for (int i = 0; i < 125 && !settled; ++i) settled = tick(r, t, 4000, 1000, true); // stuck word, time only
    TEST_ASSERT_TRUE(settled);
}

void test_paused_time_and_big_jumps_do_not_settle() {
    ResumePoint r;
    ResumeTracker t;
    noteJump(r, t, 500, 4000, 1, kWords);
    for (int i = 0; i < 1000; ++i) TEST_ASSERT_FALSE(tick(r, t, 4000, 1000, false));
    uint32_t word = 4000;
    for (int i = 0; i < 100; ++i) TEST_ASSERT_FALSE(tick(r, t, word += 100, 10, false)); // scrub-like leaps
    TEST_ASSERT_TRUE(r.valid);
}

void test_backward_steps_never_touch_resume() {
    ResumePoint r;
    ResumeTracker t;
    noteJump(r, t, 500, 4000, 1, kWords);
    for (uint32_t w = 3990; w > 3900; --w) tick(r, t, w, 100, false);
    TEST_ASSERT_TRUE(r.valid);
    TEST_ASSERT_EQUAL_UINT32(500, r.wordIndex);
    TEST_ASSERT_EQUAL_UINT32(0, t.wordsRead);
}

void test_bookmarks_add_sort_dedupe_and_limit() {
    std::vector<Bookmark> list;
    TEST_ASSERT_EQUAL((int) AddResult::Added, (int) addBookmark(list, 900, kWords, "b"));
    TEST_ASSERT_EQUAL((int) AddResult::Added, (int) addBookmark(list, 100, kWords, "a"));
    TEST_ASSERT_EQUAL((int) AddResult::AlreadyMarked, (int) addBookmark(list, 100, kWords, "x"));
    TEST_ASSERT_EQUAL_UINT32(100, list[0].wordIndex);
    TEST_ASSERT_EQUAL_UINT32(1, list[0].percent);
    for (uint32_t i = 0; list.size() < kMaxBookmarks; ++i) addBookmark(list, 2000 + i, kWords, "f");
    TEST_ASSERT_EQUAL((int) AddResult::Full, (int) addBookmark(list, 9000, kWords, "z"));
    TEST_ASSERT_TRUE(removeBookmarkAt(list, 900));
    TEST_ASSERT_FALSE(removeBookmarkAt(list, 900));
    TEST_ASSERT_TRUE(removeBookmark(list, 0));
    TEST_ASSERT_FALSE(removeBookmark(list, 99));
}

void test_snippet_is_utf8_safe_and_single_line() {
    TEST_ASSERT_EQUAL_STRING("a b c", makeSnippet("  a\n\tb   c ").c_str());
    const std::string cut = makeSnippet("ééééééééééééééééééééééééé", 9); // 2-byte chars
    TEST_ASSERT_EQUAL_STRING("éééé...", cut.c_str());
}

void test_bookmarks_round_trip_and_identity_check() {
    const BookIdentity id{1234, 99, kWords};
    std::vector<Bookmark> list;
    addBookmark(list, 10, kWords, "primeiro\ttrecho");
    addBookmark(list, 5000, kWords, "\"A recompensa...\"");
    const std::string text = serializeBookmarks(id, list);
    const auto parsed = parseBookmarks(text, id);
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_EQUAL_UINT32(2, parsed->size());
    TEST_ASSERT_EQUAL_UINT32(5000, (*parsed)[1].wordIndex);
    TEST_ASSERT_EQUAL_UINT32(50, (*parsed)[1].percent);
    TEST_ASSERT_EQUAL_STRING("\"A recompensa...\"", (*parsed)[1].snippet.c_str());
    TEST_ASSERT_EQUAL_STRING("primeiro trecho", (*parsed)[0].snippet.c_str());
    BookIdentity other = id;
    other.sourceFingerprint = 7;
    TEST_ASSERT_FALSE(parseBookmarks(text, other).has_value());
    TEST_ASSERT_FALSE(parseBookmarks("lixo", id).has_value());
}

void test_bookmarks_parser_skips_bad_lines() {
    const BookIdentity id{1, 2, 100};
    const std::string text = serializeBookmarks(id, {}) + "x\ty\tz\n5\t5\tok\n500\t5\tfora do livro\n5\t9\tdup\n";
    const auto parsed = parseBookmarks(text, id);
    TEST_ASSERT_TRUE(parsed.has_value());
    TEST_ASSERT_EQUAL_UINT32(1, parsed->size());
    TEST_ASSERT_EQUAL_STRING("ok", (*parsed)[0].snippet.c_str());
}

void test_old_sidecar_without_resume_still_loads() {
    const std::string old = "[identity]\nsourceSize = 10\nsourceFingerprint = 3\nwordCount = 50\n\n"
                            "[reading]\nwordIndex = 7\n\n[reading.overrides]\nlanguageFonts = []\n";
    StoredState state;
    const glz::error_ctx error = glz::read<glz::opts{.format = glz::TOML, .error_on_unknown_keys = false}>(state, old);
    TEST_ASSERT_FALSE(static_cast<bool>(error));
    TEST_ASSERT_EQUAL_UINT32(7, state.reading.wordIndex);
    TEST_ASSERT_FALSE(state.reading.resume.valid);
}

void test_resume_round_trips_through_sidecar_toml() {
    StoredState state;
    state.identity = {10, 3, 50};
    state.reading.wordIndex = 20;
    state.reading.resume = {7, 4, 14, true, true};
    std::string toml;
    TEST_ASSERT_FALSE(glz::write_toml(state, toml));
    StoredState decoded;
    TEST_ASSERT_FALSE(glz::read_toml(decoded, toml));
    TEST_ASSERT_TRUE(decoded == state);
}

void test_scrub_math() {
    TEST_ASSERT_FALSE(isScrubMove(8, 0));
    TEST_ASSERT_TRUE(isScrubMove(9, 2));
    TEST_ASSERT_FALSE(isScrubMove(20, 30)); // vertical wins
    TEST_ASSERT_EQUAL_INT(0, scrubSteps(11));
    TEST_ASSERT_EQUAL_INT(1, scrubSteps(12));
    TEST_ASSERT_EQUAL_INT(-3, scrubSteps(-40));
    TEST_ASSERT_EQUAL_INT(8, scrubSteps(96));
    TEST_ASSERT_TRUE(scrubSteps(300) > 8 + 60); // accelerated past the linear zone
    TEST_ASSERT_EQUAL_UINT32(116, scrubTarget(100, 120, 1000)); // 8 linear + 8 accelerated
    TEST_ASSERT_EQUAL_UINT32(0, scrubTarget(5, -600, 1000));
    TEST_ASSERT_EQUAL_UINT32(999, scrubTarget(990, 600, 1000));
    TEST_ASSERT_EQUAL_UINT32(0, scrubTarget(0, 50, 0));
}

void test_backward_math() {
    TEST_ASSERT_EQUAL_UINT32(9, backwardTarget(10, 1));
    TEST_ASSERT_EQUAL_UINT32(7, backwardTarget(10, 3));
    TEST_ASSERT_EQUAL_UINT32(0, backwardTarget(2, 3));
    TEST_ASSERT_EQUAL_UINT32(0, backwardTarget(0, 1));
    TEST_ASSERT_EQUAL_UINT32(100, backwardSpeedPercent(0));
    TEST_ASSERT_EQUAL_UINT32(200, backwardSpeedPercent(2000));
    TEST_ASSERT_EQUAL_UINT32(300, backwardSpeedPercent(4500));
}

static std::vector<std::string> gWords;
static ReadingSession makeSession(size_t count) {
    gWords.clear();
    for (size_t i = 0; i < count; ++i) gWords.push_back("w" + std::to_string(i));
    ReadingSession s;
    ReadingLoop::setWords(s, gWords, 0);
    return s;
}

void test_update_backward_walks_back_at_wpm_and_stops_at_zero() {
    settings::ReadingSettings cfg;
    cfg.wpm = 600; // 100 ms / word
    ReadingSession s = makeSession(50);
    ReadingLoop::seekTo(s, 10);
    s.lastAdvanceMs = 1000;
    TEST_ASSERT_FALSE(ReadingLoop::updateBackward(s, cfg, 1010, 100)); // too early for one word
    TEST_ASSERT_TRUE(ReadingLoop::updateBackward(s, cfg, 1250, 100));
    TEST_ASSERT_TRUE(s.state.wordIndex < 10 && s.state.wordIndex >= 6); // 1..4 words (bounded catch-up)
    TEST_ASSERT_EQUAL_STRING(gWords[s.state.wordIndex].c_str(), s.currentWord.c_str());
    // 200% speed: more steps in the same time than 100%
    ReadingSession a = makeSession(50), b = makeSession(50);
    ReadingLoop::seekTo(a, 40);
    ReadingLoop::seekTo(b, 40);
    a.lastAdvanceMs = b.lastAdvanceMs = 0;
    ReadingLoop::updateBackward(a, cfg, 220, 100);
    ReadingLoop::updateBackward(b, cfg, 220, 200);
    TEST_ASSERT_TRUE(b.state.wordIndex < a.state.wordIndex);
    uint32_t now = 1250;
    for (int i = 0; i < 100; ++i) ReadingLoop::updateBackward(s, cfg, now += 100, 100);
    TEST_ASSERT_EQUAL_UINT32(0, s.state.wordIndex);
}

void test_jump_to_records_resume_but_rewind_does_not() {
    settings::ReadingSettings cfg;
    cfg.wpm = 600;
    ReadingSession s = makeSession(50);
    ReadingLoop::seekTo(s, 30);
    ReadingLoop::jumpTo(s, 5);
    TEST_ASSERT_EQUAL_UINT32(5, s.state.wordIndex);
    TEST_ASSERT_TRUE(s.state.resume.valid);
    TEST_ASSERT_EQUAL_UINT32(30, s.state.resume.wordIndex);
    TEST_ASSERT_TRUE(s.resumeDirty);
    s.lastAdvanceMs = 0;
    ReadingLoop::updateBackward(s, cfg, 500, 100);
    TEST_ASSERT_EQUAL_UINT32(30, s.state.resume.wordIndex); // rewinding below the new place changes nothing
    ReadingLoop::jumpTo(s, 30); // back to the exact saved word
    TEST_ASSERT_EQUAL_UINT32(30, s.state.wordIndex);
    TEST_ASSERT_FALSE(s.state.resume.valid);
}

int main() {
    UNITY_BEGIN();
    RUN_TEST(test_jump_saves_current_position_as_resume);
    RUN_TEST(test_jump_to_same_word_is_not_a_jump);
    RUN_TEST(test_chained_jumps_keep_the_last_really_read_place);
    RUN_TEST(test_jump_back_to_resume_clears_it);
    RUN_TEST(test_reading_enough_words_settles_the_new_place);
    RUN_TEST(test_reading_for_two_minutes_settles);
    RUN_TEST(test_paused_time_and_big_jumps_do_not_settle);
    RUN_TEST(test_backward_steps_never_touch_resume);
    RUN_TEST(test_bookmarks_add_sort_dedupe_and_limit);
    RUN_TEST(test_snippet_is_utf8_safe_and_single_line);
    RUN_TEST(test_bookmarks_round_trip_and_identity_check);
    RUN_TEST(test_bookmarks_parser_skips_bad_lines);
    RUN_TEST(test_old_sidecar_without_resume_still_loads);
    RUN_TEST(test_resume_round_trips_through_sidecar_toml);
    RUN_TEST(test_scrub_math);
    RUN_TEST(test_backward_math);
    RUN_TEST(test_update_backward_walks_back_at_wpm_and_stops_at_zero);
    RUN_TEST(test_jump_to_records_resume_but_rewind_does_not);
    return UNITY_END();
}
