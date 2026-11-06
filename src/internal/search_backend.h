#ifndef MALMOI_SEARCH_H
#define MALMOI_SEARCH_H
#include "malmoi.h"
#include "vecdb.h"
#ifdef __cplusplus
extern "C" {
#endif

typedef struct MalmoiSearch MalmoiSearch;
typedef struct {
    /* Fill dimension floats in the declared space, return VECDB_OK on success.
     * The context owns neither userdata nor the encoder. */
    int (*encode)(void *userdata, const char *utf8_text, float *out, size_t dimension);
    void *userdata;
    const char *space;
} MalmoiEncoder;

/* DB must outlive the search context and returned string views.
 * encoder may be NULL for word-only search. Its space must match the DB.
 * No global state; concurrent calls require a thread-safe encoder. */
VECDB_API int malmoi_search_open(MalmoiSearch **out, const VecDB *db, const MalmoiEncoder *encoder);
VECDB_API void malmoi_search_close(MalmoiSearch *search);

/* 1. Any nonempty UTF-8 string -> unique word list + cosine scores.
 * Requires an explicitly supplied internal encoder.
 * Sort: score descending, key ascending, ID ascending. k=0 is a no-op.
 * Caller allocates out[k]; count is cleared on failure. Strings belong to DB. */
VECDB_API int malmoi_search_text(const MalmoiSearch *search, const char *utf8_text,
                               MalmoiResult *out, size_t k, size_t *count);

/* Existing word -> similar words; excludes all entries with the same key.
 * NFC input expected; ASCII '-' and '^' separators are removed.
 * Homonyms/polysemy: maximum cosine over sense-entry pairs, one result/key.
 * Zero vectors are skipped; unknown words return NOT_FOUND. */
VECDB_API int malmoi_similar_words(const MalmoiSearch *search, const char *utf8_word,
                                 MalmoiResult *out, size_t k, size_t *count);

/* 2. Two word strings -> maximum sense-pair cosine (same key => 1).
 * Unknown words/zero-only groups return explicit errors, never a fake 0. */
VECDB_API int malmoi_word_similarity(const MalmoiSearch *search, const char *utf8_a,
                                   const char *utf8_b, float *score);
/* Meaning-based variants: skip reference senses (dialect, archaic, North Korean
 * forms, work titles) when a word has other senses, and skip sense pairs whose
 * definitions are identical. Falls back to the plain score when nothing is left. */
VECDB_API int malmoi_word_similarity_meaning(const MalmoiSearch *search, const char *utf8_a,
                                           const char *utf8_b, float *score);
VECDB_API int malmoi_similar_words_meaning(const MalmoiSearch *search, const char *utf8_word,
                                         MalmoiResult *out, size_t k, size_t *count);
                                   /* Native definition character n-gram search; independent of model vectors. */
int malmoi_search_definitions(const MalmoiSearch *search,const char *text,
                               MalmoiResult *out,size_t k,size_t *count);
#ifdef __cplusplus
}
#endif
#endif
