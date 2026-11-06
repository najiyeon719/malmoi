#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "game.h"

#define CARD_COUNT 3
#define HINT_POOL_SIZE 32

int main(void){
    // 환경 변수에서 오늘의 정답 가져오기
    const char *answer = getenv("MALLANGI_ANSWER");

    if(answer == NULL || answer[0] == '\0' ||
       strlen(answer) >= MALLANGI_WORD_BYTES){
        fprintf(stderr,
                "MALLANGI_ANSWER 환경 변수에 정답 단어를 설정하세요.\n");
        return 1;
    }

    // 1. 실제 데이터셋 엔진 초기화
    Malmoi *engine = NULL;

    int status = malmoi_open(&engine);

    if(status != MALMOI_OK){
        fprintf(stderr,
                "데이터셋 열기 실패: %s\n",
                malmoi_error_string(status));
        return 1;
    }

    // 2. 게임 상태 초기화
    GameState game = {
        .attempts = MALLANGI_MAX_ATTEMPTS,
        .engine = engine
    };

    strcpy(game.answer, answer);

    // 3. 실제 엔진에서 유사 단어 가져오기
    MalmoiResult hints[HINT_POOL_SIZE];
    size_t hintCount = 0;

    status = malmoi_similar_meaning(
        engine,
        game.answer,
        hints,
        HINT_POOL_SIZE,
        &hintCount
    );

    if(status != MALMOI_OK || hintCount < CARD_COUNT){
        fprintf(stderr,
                "단어 카드 준비 실패: %s (카드 후보 %zu개)\n",
                malmoi_error_string(status),
                hintCount);

        malmoi_close(engine);
        return 1;
    }

    // 4. 단어 카드 3장 추첨
    srand((unsigned int)time(NULL));

    int cards[CARD_COUNT];

    draw_cards(cards, CARD_COUNT, (int)hintCount);

    // 5. 게임 시작
    printf("===== MALLANGI =====\n");
    printf("오늘의 비밀 단어를 맞혀보세요!\n\n");

    printf("[내 단어 카드]\n");

    for(int i = 0; i < CARD_COUNT; i++){
        const MalmoiResult *hint = &hints[cards[i]];

        printf("%d번 카드: %s\n", i + 1, hint->word);
        printf("유사도: %.2f\n\n", hint->score);
    }

    // 6. 정답 제출
    char guess[MALLANGI_WORD_BYTES];

    while(!game.finished && game.attempts > 0){
        printf("\n남은 정답 제출 횟수: %d\n",
               game.attempts);

        printf("정답을 입력하세요: ");

        if(scanf("%63s", guess) != 1){
            printf("입력을 종료합니다.\n");
            break;
        }

        GameResult result = submit_guess(&game, guess);

        if(result.status == GUESS_CORRECT){
            printf("\n정답입니다!\n");
            break;
        }
        else if(result.status == GUESS_WRONG){
            printf("틀렸습니다!\n");
            printf("정답과의 유사도: %.2f\n",
                   result.similarity);
        }
        else if(result.status == GUESS_DUPLICATE){
            printf("이미 제출한 단어입니다.\n");
        }
        else if(result.status == GUESS_UNKNOWN){
            printf("등록되지 않았거나 유효한 벡터가 없는 단어입니다.\n");
        }
        else if(result.status == GUESS_INVALID){
            printf("잘못된 입력입니다.\n");
        }
        else{
            fprintf(stderr,
                    "게임 오류: %s\n",
                    malmoi_error_string(result.engineStatus));

            malmoi_close(engine);
            return 1;
        }
    }

    // 7. 실패 시 정답 공개
    if(!game.won && game.attempts == 0){
        printf("\n모든 기회를 사용했습니다.\n");
        printf("정답은 %s입니다.\n", game.answer);
    }

    // 8. 엔진 종료
    malmoi_close(engine);

    return 0;
}
