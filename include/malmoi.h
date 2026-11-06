#ifndef MALMOI_H
#define MALMOI_H
#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(VECDB_BUILD_DLL)
#define MALMOI_API __declspec(dllexport)
#else
#define MALMOI_API
#endif
#ifdef __cplusplus
extern "C" {
#endif

typedef struct Malmoi Malmoi;
typedef struct {
    uint32_t word_id;  /* valid for this engine's dataset only */
    const char *word;  /* original spelling, read-only */
    const char *key;   /* normalized word */
    float score;       /* lexical search [0,1]; embedding search/similarity [-1,1] */
} MalmoiResult;

enum MalmoiStatus {
    MALMOI_OK=0, MALMOI_ARGUMENT, MALMOI_IO, MALMOI_FORMAT, MALMOI_MEMORY,
    MALMOI_RANGE, MALMOI_NOT_FOUND, MALMOI_AMBIGUOUS, MALMOI_SPACE,
    MALMOI_ZERO_VECTOR, MALMOI_LIMIT, MALMOI_UNSUPPORTED, MALMOI_CONFIG
};

/* Default mode is native C only. Finds data/full.mlm, executable-adjacent
 * data/dictionary.mlm, then working-directory data/dictionary.mlm. MALMOI_DATA may
 * select another UTF-8 path. No model, Python, ONNX or network is needed.
 * Optional E5 build: MALMOI_MODEL selects matching offline model assets and
 * enables semantic description search using embedded Python/ONNX Runtime.
 * Explicit model configuration errors never silently fall back to lexical.
 * Open once and reuse; close after all calls. Failure clears *out.
 * Dataset replacement takes effect on the next open, without caller changes.
 * Concurrent read calls are allowed; close must not race with calls. */
MALMOI_API int malmoi_open(Malmoi **out);
MALMOI_API void malmoi_close(Malmoi *engine);
/* Read-only strings owned by engine; NULL for a NULL engine. The data path
 * is the configured path (may be relative); mode is lexical or e5-semantic. */
MALMOI_API const char *malmoi_data_path(const Malmoi *engine);
MALMOI_API const char *malmoi_search_mode(const Malmoi *engine);

/* NFC UTF-8 description -> words, ranked by the configured search mode.
 * Default: definition character overlap cosine [0,1]. E5 mode: [-1,1].
 * Caller allocates out[k]. count is actual results, cleared on failure.
 * Returned strings belong to engine, valid until malmoi_close.
 * k=0/out=NULL is allowed. Text is limited to 8192 UTF-8 bytes. */
MALMOI_API int malmoi_search(const Malmoi *engine,const char *text,
                            MalmoiResult *out,size_t k,size_t *count);
/* Existing NFC word -> similar words, excluding its own normalized key. */
MALMOI_API int malmoi_similar(const Malmoi *engine,const char *word,
                             MalmoiResult *out,size_t k,size_t *count);
/* Two existing words -> max sense-pair cosine. Unknown words are errors. */
MALMOI_API int malmoi_similarity(const Malmoi *engine,const char *a,const char *b,float *score);
/* 뜻 기준 유사도 (게임용). 다른 단어를 가리키기만 하는 뜻(방언, 옛말, 북한어)과
 * 작품 제목 뜻은 그 단어에 다른 뜻이 있으면 빼고, 뜻풀이가 똑같은 뜻 짝도 빼고 비교해요.
 * 그래서 '비'와 '감'처럼 같은 방언 뜻 하나 때문에 1이 되는 일이 없어요.
 * 사용법과 오류 규칙은 malmoi_similar, malmoi_similarity와 같아요. */
MALMOI_API int malmoi_similar_meaning(const Malmoi *engine,const char *word,
                                     MalmoiResult *out,size_t k,size_t *count);
MALMOI_API int malmoi_similarity_meaning(const Malmoi *engine,const char *a,const char *b,float *score);
MALMOI_API const char *malmoi_error_string(int status);

#ifdef __cplusplus
}
#endif
#endif
