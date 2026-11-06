#include "malmoi.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

/* 뜻 기준 유사도 테스트 (build/fixtures/meaning.mlm)
 *   비   : 물방울 [1,0,0], ‘베’의 방언 [0,1,0], 비-(접두사) [0,0,1]
 *   감   : ‘베’의 방언 [0,1,0], 주황색 열매 [0.6,0.8,0]
 *   이슬비: 가늘게 내리는 비 [0.9,0,0.436]
 *   힘껏 : 있는 힘을 다하여 [0,0,1]
 *   벼, 불: ‘베’의 방언 [0,1,0] 하나뿐 */

static int failures = 0;
#define CHECK(cond) do { if (!(cond)) { \
    fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); ++failures; } } while (0)

static int near(float a, float b) { return fabsf(a - b) < 0.001f; }

static float old_score(const Malmoi *m, const char *a, const char *b) {
    float s = -9.0f;
    CHECK(malmoi_similarity(m, a, b, &s) == MALMOI_OK);
    return s;
}

static float new_score(const Malmoi *m, const char *a, const char *b) {
    float s = -9.0f;
    CHECK(malmoi_similarity_meaning(m, a, b, &s) == MALMOI_OK);
    return s;
}

/* 1. 같은 방언 뜻 하나 때문에 1이던 점수가, 의미 뜻끼리 비교한 값(0.6)으로 바뀌어요. */
static void test_shared_dialect(const Malmoi *m) {
    CHECK(near(old_score(m, "비", "감"), 1.0f));
    CHECK(near(new_score(m, "비", "감"), 0.6f));
}

/* 2. 접사 뜻(비-)은 빼요: 힘껏과 1이던 점수가 0이 돼요. */
static void test_affix(const Malmoi *m) {
    CHECK(near(old_score(m, "비", "힘껏"), 1.0f));
    CHECK(near(new_score(m, "비", "힘껏"), 0.0f));
}

/* 3. 진짜 비슷한 단어는 그대로예요. */
static void test_real_meaning_kept(const Malmoi *m) {
    CHECK(near(old_score(m, "비", "이슬비"), 0.9f));
    CHECK(near(new_score(m, "비", "이슬비"), 0.9f));
}

/* 4. 방언 뜻만 있는 단어(벼)는 모든 뜻을 써서 비교해요. 비는 의미 뜻만 써요. */
static void test_reference_only_word(const Malmoi *m) {
    CHECK(near(new_score(m, "비", "벼"), 0.0f));
}

/* 5. 두 단어가 똑같은 뜻풀이뿐이면 정말 같은 뜻이라 1이에요. */
static void test_fully_identical(const Malmoi *m) {
    CHECK(near(new_score(m, "벼", "불"), 1.0f));
}

/* 6. 같은 단어는 1, 없는 단어는 NOT_FOUND, 인자 오류는 ARGUMENT */
static void test_errors(const Malmoi *m) {
    float s = 0.0f;
    CHECK(near(new_score(m, "비", "비"), 1.0f));
    CHECK(malmoi_similarity_meaning(m, "비", "고양이", &s) == MALMOI_NOT_FOUND);
    CHECK(malmoi_similarity_meaning(m, "비", "감", NULL) == MALMOI_ARGUMENT);
    CHECK(malmoi_similarity_meaning(NULL, "비", "감", &s) == MALMOI_ARGUMENT);
}

/* 7. 힌트 목록: 방언만 있는 단어(벼, 불)는 빠지고, 이슬비 > 감 > 힘껏 순서 */
static void test_hints(const Malmoi *m) {
    MalmoiResult r[10];
    size_t n = 0, i;
    CHECK(malmoi_similar_meaning(m, "비", r, 10, &n) == MALMOI_OK);
    CHECK(n == 3);
    if (n == 3) {
        CHECK(strcmp(r[0].key, "이슬비") == 0 && near(r[0].score, 0.9f));
        CHECK(strcmp(r[1].key, "감") == 0 && near(r[1].score, 0.6f));
        CHECK(strcmp(r[2].key, "힘껏") == 0 && near(r[2].score, 0.0f));
    }
    for (i = 0; i < n; ++i) CHECK(r[i].score < 0.999f);
    CHECK(malmoi_similar_meaning(m, "고양이", r, 10, &n) == MALMOI_NOT_FOUND && n == 0);
}

/* 8. 기존 함수는 바뀌지 않았어요: 기존 힌트에는 1.000짜리가 그대로 있어요. */
static void test_old_unchanged(const Malmoi *m) {
    MalmoiResult r[10];
    size_t n = 0;
    CHECK(malmoi_similar(m, "비", r, 10, &n) == MALMOI_OK);
    CHECK(n >= 1 && near(r[0].score, 1.0f));
}

int main(void) {
    Malmoi *m = NULL;
    if (malmoi_open(&m) != MALMOI_OK) {
        fprintf(stderr, "MALMOI_DATA로 meaning.mlm을 지정하세요\n");
        return 1;
    }
    test_shared_dialect(m);
    test_affix(m);
    test_real_meaning_kept(m);
    test_reference_only_word(m);
    test_fully_identical(m);
    test_errors(m);
    test_hints(m);
    test_old_unchanged(m);
    malmoi_close(m);
    if (failures) {
        fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    printf("meaning tests passed (8 tests)\n");
    return 0;
}