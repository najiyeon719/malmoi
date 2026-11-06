#include "similarity.h"

int game_similarity(
    const Malmoi *engine,
    const char *answer,
    const char *guess,
    float *score
){
    if(engine == NULL || answer == NULL ||
       guess == NULL || score == NULL){
        return MALMOI_ARGUMENT;
    }

    return malmoi_similarity_meaning(engine, answer, guess, score);
}