#include "malmoi_bridge.h"

#include "malmoi.h"
#include "game.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#define RESULT_LIMIT 10
#define HINT_POOL_SIZE 32
#define CARD_COUNT 3

/* Read one UTF-8 line. An overlong input is discarded, not truncated. */
static int read_line(const char *prompt, char *buffer, size_t capacity) {
    size_t length;
    int ch;

    if (capacity < 2) return 0;
    fputs(prompt, stdout);
    fflush(stdout);
    if (!fgets(buffer, (int)capacity, stdin)) return 0;

    length = strcspn(buffer, "\r\n");
    if (buffer[length] == '\0' && length == capacity - 1) {
        while ((ch = getchar()) != '\n' && ch != EOF) { }
        fputs("입력이 너무 깁니다.\n", stderr);
        buffer[0] = '\0';
        return 1;
    }
    buffer[length] = '\0';
    return 1;
}

static void show_results(const MalmoiResult *results, size_t count) {
    size_t i;
    if (!count) {
        puts("검색 결과가 없습니다.");
        return;
    }
    for (i = 0; i < count; ++i) {
        printf("%zu. %s (점수 %.4f)\n", i + 1, results[i].word,
               (double)results[i].score);
    }
}

static void run_search(const Malmoi *engine, int similar) {
    char input[8194];
    MalmoiResult results[RESULT_LIMIT];
    size_t count = 0;
    int status;

    if (!read_line(similar ? "단어: " : "설명: ", input, sizeof(input))) return;
    if (!input[0]) return;

    if (similar) {
        status = malmoi_similar(engine, input, results, RESULT_LIMIT, &count);
    } else {
        status = malmoi_search(engine, input, results, RESULT_LIMIT, &count);
    }
    if (status != MALMOI_OK) {
        fprintf(stderr, "검색 오류: %s\n", malmoi_error_string(status));
        return;
    }
    show_results(results, count);
}

static void run_game(const Malmoi *engine) {
    const char *answer = getenv("MALLANGI_ANSWER");
    GameState game = {0};
    MalmoiResult hints[HINT_POOL_SIZE];
    size_t hint_count = 0;
    int cards[CARD_COUNT];
    int status;
    int i;

    /* The default answer is a demo setting, not daily-answer selection. */
    if (!answer || !answer[0]) answer = "이슬비";
    if (strlen(answer) >= sizeof(game.answer)) {
        fputs("정답 단어가 너무 깁니다.\n", stderr);
        return;
    }
    status = malmoi_similar_meaning(engine, answer, hints, HINT_POOL_SIZE, &hint_count);
    if (status != MALMOI_OK || hint_count < CARD_COUNT) {
        fprintf(stderr, "게임 준비 실패: %s (후보 %zu개)\n",
                malmoi_error_string(status), hint_count);
        return;
    }

    game.engine = engine;
    game.attempts = MALLANGI_MAX_ATTEMPTS;
    strcpy(game.answer, answer);
    draw_cards(cards, CARD_COUNT, (int)hint_count);

    puts("\n[말랑이] 힌트 단어");
    for (i = 0; i < CARD_COUNT; ++i) {
        const MalmoiResult *hint = &hints[cards[i]];
        printf("%d. %s (유사도 %.4f)\n", i + 1, hint->word,
               (double)hint->score);
    }

    while (!game.finished) {
        char guess[MALLANGI_WORD_BYTES];
        GameResult result;
        printf("남은 횟수: %d\n", game.attempts);
        if (!read_line("추측 단어 (빈 입력으로 나가기): ", guess, sizeof(guess))) return;
        if (!guess[0]) return;
        result = submit_guess(&game, guess);
        switch (result.status) {
            case GUESS_CORRECT: puts("정답입니다!"); break;
            case GUESS_WRONG:
                printf("아닙니다. 유사도: %.4f\n", result.similarity);
                break;
            case GUESS_DUPLICATE: puts("이미 입력한 단어입니다."); break;
            case GUESS_UNKNOWN: puts("사전에 없거나 유효한 벡터가 없는 단어입니다."); break;
            case GUESS_INVALID: puts("입력이 올바르지 않습니다."); break;
            case GUESS_FINISHED: puts("게임이 끝났습니다."); break;
            default:
                fprintf(stderr, "게임 오류: %s\n",
                        malmoi_error_string(result.engineStatus));
                return;
        }
    }
    if (!game.won) printf("정답: %s\n", game.answer);
}

static void run_bridge(const Malmoi *engine) {
    char from[256], to[256];
    MalmoiBridgeOptions options;
    MalmoiStep path[16];
    size_t count = 0, i;
    int status;

    if (!read_line("출발 단어: ", from, sizeof(from)) || !from[0]) return;
    if (!read_line("도착 단어: ", to, sizeof(to)) || !to[0]) return;
    malmoi_bridge_default_options(&options);
    status = malmoi_bridge(engine, from, to, &options, path, 16, &count);
    if (status != MALMOI_OK) {
        fprintf(stderr, "다리 놓기 오류: %s\n", malmoi_error_string(status));
        return;
    }
    if (!count) {
        puts("연결을 찾지 못했습니다.");
        return;
    }
    printf("%s", path[0].word);
    for (i = 1; i < count; ++i)
        printf(" -> %s (%.3f)", path[i].word, (double)path[i].score);
    printf("\n");
}

int main(void) {
    Malmoi *engine = NULL;
    char choice[16];
    int status = malmoi_open(&engine);
    if (status != MALMOI_OK) {
        fprintf(stderr, "사전 열기 실패: %s\n", malmoi_error_string(status));
        if (status == MALMOI_CONFIG)
            fputs("E5 설정을 확인하세요. 의미 검색은 MALMOI_WITH_E5 빌드와 Python/ONNX 모델이 필요합니다.\n", stderr);
        return 1;
    }

    printf("사전: %s\n설명 검색: %s\n", malmoi_data_path(engine),
           strcmp(malmoi_search_mode(engine), "e5-semantic") == 0
               ? "E5 의미 검색 (로컬 ONNX)" : "문자 유사도 (모델 불필요)");

    srand((unsigned int)time(NULL));
    while (1) {
        puts("\nMalmoi\n1. 역방향 사전\n2. 유사어\n3. 말랑이\n4. 단어 사이 다리 놓기\n0. 종료");
        if (!read_line("선택: ", choice, sizeof(choice))) break;
        if (!strcmp(choice, "0")) break;
        if (!strcmp(choice, "1")) run_search(engine, 0);
        else if (!strcmp(choice, "2")) run_search(engine, 1);
        else if (!strcmp(choice, "3")) run_game(engine);
        else if (!strcmp(choice, "4")) run_bridge(engine);
        else puts("0~4 중에서 선택하세요.");
    }
    malmoi_close(engine);
    return 0;
}
