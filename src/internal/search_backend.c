#include "search_backend.h"
#include "lexical.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    VecDBWord word;
    const float *vector;
    double inv_norm;
    const char *definition; /* 이 뜻의 뜻풀이 (없으면 NULL) */
    int reference;          /* 1이면 참조형 또는 작품명 뜻 */
} Entry;
/* meaning_count: 벡터가 있고 참조형이 아닌 뜻의 수 */
typedef struct { size_t start, count, meaning_count; } Group;
struct MalmoiSearch {
    const VecDB *db;
    size_t n, ng, dimension;
    Entry *entries;
    Group *groups;
    MalmoiEncoder encoder;
    MalmoiGrams *documents;
    size_t document_count;
};

static int valid_text(const char *text) {
    const unsigned char *s = (const unsigned char *)text;
    size_t i = 0, n;
    int content = 0;
    if (!text) return 0;
    n = strlen(text);
    while (i < n) {
        uint32_t cp;
        unsigned need, j;
        unsigned char c = s[i++];
        if (c >= 0x80 || (c != ' ' && c != '\t' && c != '\r' && c != '\n')) content = 1;
        if (c < 0x80) continue;
        if (c >= 0xc2 && c <= 0xdf) { cp = c & 31U; need = 1; }
        else if (c >= 0xe0 && c <= 0xef) { cp = c & 15U; need = 2; }
        else if (c >= 0xf0 && c <= 0xf4) { cp = c & 7U; need = 3; }
        else return 0;
        if (need > n-i) return 0;
        for (j=0; j<need; ++j) { c=s[i++]; if ((c&192U)!=128U) return 0; cp=(cp<<6)|(c&63U); }
        if ((need==1 && cp<128) || (need==2 && cp<2048) || (need==3 && cp<65536) ||
            cp>0x10ffff || (cp>=0xd800 && cp<=0xdfff)) return 0;
    }
    return content;
}
static int entry_order(const void *a, const void *b) {
    const Entry *x=(const Entry *)a, *y=(const Entry *)b;
    int c=strcmp(x->word.key,y->word.key);
    return c ? c : (x->word.id>y->word.id)-(x->word.id<y->word.id);
}

/* ---------- 뜻 기준 유사도: 참조형 뜻과 똑같은 뜻풀이를 빼고 비교 ---------- */

/* 다른 단어를 가리키기만 하는 뜻(방언, 옛말, 북한어)과 작품 제목 뜻을 찾는 문구 */
static const char *const REFERENCE_PATTERNS[] = {
    "’의 방언", "’의 옛말", "’의 북한어",
    "작곡의 ", "감독이 만든", "지은 시", "지은 소설",
    NULL
};

/* 참조형 뜻인가? 뜻풀이에 위 문구가 있거나, 단어가 '-'로 시작하거나 끝나는
 * 접사(예: 비-, -껏)면 참조형으로 봐요. */
static int is_reference(const char *word, const char *definition) {
    size_t i, n;
    if (word) {
        n = strlen(word);
        if (n && (word[0] == '-' || word[n - 1] == '-')) return 1;
    }
    if (!definition) return 0;
    for (i = 0; REFERENCE_PATTERNS[i]; ++i) {
        if (strstr(definition, REFERENCE_PATTERNS[i])) return 1;
    }
    return 0;
}

/* 뜻마다 뜻풀이를 연결하고, 단어(group)마다 의미 뜻의 수를 셉니다. */
static int attach_definitions(MalmoiSearch *s) {
    size_t *where;
    size_t i, g;
    if (!s->n) return VECDB_OK;
    where = (size_t *)malloc(s->n * sizeof(size_t));
    if (!where) return VECDB_MEMORY;
    for (i = 0; i < s->n; ++i) where[i] = SIZE_MAX;
    for (i = 0; i < s->n; ++i) {
        if (s->entries[i].word.id < s->n) where[s->entries[i].word.id] = i;
    }
    for (i = 0; i < s->document_count; ++i) {
        VecDBSense sense;
        Entry *e;
        if (vecdb_sense(s->db, (uint32_t)i, &sense) != VECDB_OK) continue;
        if (sense.word_id >= s->n || where[sense.word_id] == SIZE_MAX) continue;
        e = &s->entries[where[sense.word_id]];
        if (e->definition) continue;
        e->definition = sense.definition;
        e->reference = is_reference(e->word.word, sense.definition);
    }
    free(where);
    for (g = 0; g < s->ng; ++g) {
        Group *group = &s->groups[g];
        group->meaning_count = 0;
        for (i = group->start; i < group->start + group->count; ++i) {
            if (s->entries[i].inv_norm && !s->entries[i].reference) group->meaning_count++;
        }
    }
    return VECDB_OK;
}

void malmoi_search_close(MalmoiSearch *s) {
    if (s) {
        size_t i;
        for (i=0;i<s->document_count;++i) malmoi_grams_free(&s->documents[i]);
        free(s->documents);free(s->entries);free(s->groups);free(s);
    }
}
int malmoi_search_open(MalmoiSearch **out, const VecDB *db, const MalmoiEncoder *encoder) {
    MalmoiSearch *s;
    size_t i,j,n;
    if (!out) return VECDB_ARGUMENT;
    *out=NULL;
    if (!db) return VECDB_ARGUMENT;
    if (!vecdb_word_dimension(db)) return VECDB_UNSUPPORTED;
    if (encoder && (!encoder->encode || !encoder->space || strcmp(encoder->space,vecdb_word_space(db)))) return VECDB_SPACE;
    n=vecdb_word_count(db);
    if (n>SIZE_MAX/sizeof(Entry) || n>SIZE_MAX/sizeof(Group)) return VECDB_LIMIT;
    s=(MalmoiSearch *)calloc(1,sizeof(*s));
    if (!s) return VECDB_MEMORY;
    s->db=db; s->n=n; s->dimension=vecdb_word_dimension(db);
    if (encoder) s->encoder=*encoder;
    if (n) {
        s->entries=(Entry *)calloc(n,sizeof(Entry)); s->groups=(Group *)calloc(n,sizeof(Group));
        if (!s->entries || !s->groups) { malmoi_search_close(s); return VECDB_MEMORY; }
    }
    for (i=0;i<n;++i) {
        MalmoiVector v;
        double norm=0;
        (void)vecdb_word(db,(uint32_t)i,&s->entries[i].word);
        (void)vecdb_word_vector(db,(uint32_t)i,&v);
        s->entries[i].vector=v.data;
        for (j=0;j<v.dimension;++j) norm+=(double)v.data[j]*v.data[j];
        s->entries[i].inv_norm=norm>0 ? 1.0/sqrt(norm) : 0;
    }
    if (n) qsort(s->entries,n,sizeof(Entry),entry_order);
    for (i=0;i<n;++i) {
        if (!i || strcmp(s->entries[i].word.key,s->entries[i-1].word.key)) {
            s->groups[s->ng].start=i; s->groups[s->ng].count=0; ++s->ng;
        }
        ++s->groups[s->ng-1].count;
    }
    s->document_count=vecdb_sense_count(db);
    if (s->document_count>SIZE_MAX/sizeof(MalmoiGrams)) {s->document_count=0;malmoi_search_close(s);return VECDB_LIMIT;}
    if (s->document_count) {
        s->documents=(MalmoiGrams *)calloc(s->document_count,sizeof(MalmoiGrams));
        if (!s->documents) {s->document_count=0;malmoi_search_close(s);return VECDB_MEMORY;}
    }
    for (i=0;i<s->document_count;++i) {
        VecDBSense sense;
        int rc;
        (void)vecdb_sense(db,(uint32_t)i,&sense);
        rc=malmoi_grams(sense.definition,&s->documents[i]);
        if (rc) {malmoi_search_close(s);return rc;}
    }
        {
        int rc=attach_definitions(s);
        if (rc) {malmoi_search_close(s);return rc;}
    }
    *out=s; return VECDB_OK;
}
static float cosine(const float *a, double inv_a, const Entry *b, size_t dim) {
    size_t i;
    double dot=0, score;
    for (i=0;i<dim;++i) dot+=(double)a[i]*b->vector[i];
    score=dot*inv_a*b->inv_norm;
    if (score>1) score=1;
    if (score< -1) score= -1;
    return (float)score;
}
static int locate(const MalmoiSearch *s,const char *word,size_t *found) {
    char *key;
    size_t i,j=0,n,lo=0,hi=s->ng;
    if (!valid_text(word)) return VECDB_ARGUMENT;
    n=strlen(word); key=(char *)malloc(n+1);
    if (!key) return VECDB_MEMORY;
    for (i=0;i<n;++i) if (word[i]!='-' && word[i]!='^') key[j++]=word[i];
    key[j]='\0';
    while (lo<hi) {
        size_t m=lo+(hi-lo)/2;
        int c=strcmp(s->entries[s->groups[m].start].word.key,key);
        if (c<0) lo=m+1; else hi=m;
    }
    if (lo<s->ng && !strcmp(s->entries[s->groups[lo].start].word.key,key)) {
        free(key); *found=lo; return VECDB_OK;
    }
    free(key); return VECDB_NOT_FOUND;
}
static int better(const MalmoiResult *a,const MalmoiResult *b) {
    int c;
    if (a->score!=b->score) return a->score>b->score;
    c=strcmp(a->key,b->key);
    return c ? c<0 : a->word_id<b->word_id;
}
static void insert(MalmoiResult *out,size_t k,size_t *n,MalmoiResult value) {
    size_t p=0,j;
    while (p<*n && !better(&value,&out[p])) ++p;
    if (p>=k) return;
    if (*n<k) ++*n;
    for (j=*n-1;j>p;--j) out[j]=out[j-1];
    out[p]=value;
}
static int search_vectors(const MalmoiSearch *s,const float *q,double inv_q,
                          const Group *sources,size_t excluded,MalmoiResult *out,size_t k,size_t *count) {
    size_t g,j,a;
    for (g=0;g<s->ng;++g) {
        const Group *group=&s->groups[g];
        MalmoiResult best={UINT32_MAX,NULL,NULL,-2};
        if (g==excluded) continue;
        for (j=group->start;j<group->start+group->count;++j) {
            const Entry *e=&s->entries[j];
            float score=-2;
            if (!e->inv_norm) continue;
            if (!sources) score=cosine(q,inv_q,e,s->dimension);
            else for (a=sources->start;a<sources->start+sources->count;++a) {
                const Entry *source=&s->entries[a];
                float v;
                if (!source->inv_norm) continue;
                v=cosine(source->vector,source->inv_norm,e,s->dimension);
                if (v>score) score=v;
            }
            if (score>best.score) {
                best.word_id=e->word.id; best.word=e->word.word; best.key=e->word.key; best.score=score;
            }
        }
        if (best.word_id!=UINT32_MAX) insert(out,k,count,best);
    }
    return VECDB_OK;
}
int malmoi_search_text(const MalmoiSearch *s,const char *text,MalmoiResult *out,size_t k,size_t *count) {
    float *q;
    double norm=0;
    size_t i;
    int rc;
    if (!count) return VECDB_ARGUMENT;
    *count=0;
    if (!s || !valid_text(text) || (k && !out)) return VECDB_ARGUMENT;
    if (strlen(text)>8192) return VECDB_LIMIT;
    if (!k) return VECDB_OK;
    if (!s->encoder.encode) return VECDB_UNSUPPORTED;
    q=(float *)calloc(s->dimension,sizeof(float));
    if (!q) return VECDB_MEMORY;
    rc=s->encoder.encode(s->encoder.userdata,text,q,s->dimension);
    if (!rc) for (i=0;i<s->dimension;++i) {
        if (!isfinite(q[i])) { rc=VECDB_ARGUMENT; break; }
        norm+=(double)q[i]*q[i];
    }
    if (!rc && norm==0) rc=VECDB_ZERO_VECTOR;
    if (!rc) rc=search_vectors(s,q,1.0/sqrt(norm),NULL,SIZE_MAX,out,k,count);
    free(q); return rc;
}
int malmoi_similar_words(const MalmoiSearch *s,const char *word,MalmoiResult *out,size_t k,size_t *count) {
    size_t group,i;
    int rc,nonzero=0;
    if (!count) return VECDB_ARGUMENT;
    *count=0;
    if (!s || (k && !out)) return VECDB_ARGUMENT;
    rc=locate(s,word,&group); if (rc) return rc;
    for (i=s->groups[group].start;i<s->groups[group].start+s->groups[group].count;++i)
        if (s->entries[i].inv_norm) nonzero=1;
    if (!nonzero) return VECDB_ZERO_VECTOR;
    if (!k) return VECDB_OK;
    return search_vectors(s,NULL,0,&s->groups[group],group,out,k,count);
}
int malmoi_word_similarity(const MalmoiSearch *s,const char *a,const char *b,float *score) {
    size_t ga,gb,i,j;
    int rc;
    float best=-2;
    if (!score) return VECDB_ARGUMENT;
    *score=0;
    if (!s) return VECDB_ARGUMENT;
    rc=locate(s,a,&ga); if (rc) return rc;
    rc=locate(s,b,&gb); if (rc) return rc;
    for (i=s->groups[ga].start;i<s->groups[ga].start+s->groups[ga].count;++i)
        for (j=s->groups[gb].start;j<s->groups[gb].start+s->groups[gb].count;++j) {
            const Entry *x=&s->entries[i], *y=&s->entries[j];
            float v;
            if (!x->inv_norm || !y->inv_norm) continue;
            v=cosine(x->vector,x->inv_norm,y,s->dimension);
            if (v>best) best=v;
        }
    if (best< -1) return VECDB_ZERO_VECTOR;
    *score=best; return VECDB_OK;
}
/* 비교에 쓸 뜻인가? 벡터가 있어야 하고, 단어에 의미 뜻이 있으면 참조형은 빼요.
 * 의미 뜻이 하나도 없는 단어(방언만 있는 단어 등)는 모든 뜻을 써요. */
static int usable(const Group *group, const Entry *e) {
    if (!e->inv_norm) return 0;
    return group->meaning_count == 0 || !e->reference;
}

/* 두 단어의 뜻 기준 유사도. 똑같은 뜻풀이를 가진 뜻 짝은 건너뛰고 최댓값을 구해요.
 * 비교할 짝이 하나도 없으면 -2를 돌려줘요. */
static float meaning_pair_score(const MalmoiSearch *s, const Group *a, const Group *b) {
    size_t i, j;
    float best = -2;
    for (i = a->start; i < a->start + a->count; ++i) {
        const Entry *x = &s->entries[i];
        if (!usable(a, x)) continue;
        for (j = b->start; j < b->start + b->count; ++j) {
            const Entry *y = &s->entries[j];
            float v;
            if (!usable(b, y)) continue;
            if (x->definition && y->definition && !strcmp(x->definition, y->definition)) continue;
            v = cosine(x->vector, x->inv_norm, y, s->dimension);
            if (v > best) best = v;
        }
    }
    return best;
}

int malmoi_word_similarity_meaning(const MalmoiSearch *s, const char *a, const char *b, float *score) {
    size_t ga, gb;
    int rc;
    float best;
    if (!score) return VECDB_ARGUMENT;
    *score = 0;
    if (!s) return VECDB_ARGUMENT;
    rc = locate(s, a, &ga); if (rc) return rc;
    rc = locate(s, b, &gb); if (rc) return rc;
    if (ga == gb) return malmoi_word_similarity(s, a, b, score);
    best = meaning_pair_score(s, &s->groups[ga], &s->groups[gb]);
    /* 두 단어의 뜻이 전부 똑같은 뜻풀이뿐이면 정말 같은 뜻이니 기존 값을 써요. */
    if (best < -1) return malmoi_word_similarity(s, a, b, score);
    *score = best;
    return VECDB_OK;
}

int malmoi_similar_words_meaning(const MalmoiSearch *s, const char *word,
                                 MalmoiResult *out, size_t k, size_t *count) {
    size_t source, g;
    int rc;
    if (!count) return VECDB_ARGUMENT;
    *count = 0;
    if (!s || (k && !out)) return VECDB_ARGUMENT;
    rc = locate(s, word, &source); if (rc) return rc;
    if (!s->groups[source].meaning_count) {
        size_t i;
        int nonzero = 0;
        for (i = s->groups[source].start; i < s->groups[source].start + s->groups[source].count; ++i)
            if (s->entries[i].inv_norm) nonzero = 1;
        if (!nonzero) return VECDB_ZERO_VECTOR;
    }
    if (!k) return VECDB_OK;
    for (g = 0; g < s->ng; ++g) {
        float score;
        const Entry *first;
        MalmoiResult value;
        /* 힌트에는 의미 뜻이 있는 단어만 내보내요 (방언만 있는 단어, 작품 제목 등 제외) */
        if (g == source || !s->groups[g].meaning_count) continue;
        score = meaning_pair_score(s, &s->groups[source], &s->groups[g]);
        if (score < -1) continue;
        first = &s->entries[s->groups[g].start];
        value.word_id = first->word.id;
        value.word = first->word.word;
        value.key = first->word.key;
        value.score = score;
        insert(out, k, count, value);
    }
    return VECDB_OK;
}
int malmoi_search_definitions(const MalmoiSearch *s,const char *text,MalmoiResult *out,size_t k,size_t *count) {
    MalmoiGrams query;
    float *scores;
    size_t i,g,j;
    int rc;
    if (!count) return VECDB_ARGUMENT;
    *count=0;
    if (!s || !valid_text(text) || (k && !out)) return VECDB_ARGUMENT;
    if (strlen(text)>8192) return VECDB_LIMIT;
    if (!k) return VECDB_OK;
    rc=malmoi_grams(text,&query);if (rc) return rc;
    if (!query.count) {malmoi_grams_free(&query);return VECDB_ARGUMENT;}
    if (!s->n) {malmoi_grams_free(&query);return VECDB_OK;}
    scores=(float *)calloc(s->n,sizeof(float));
    if (!scores) {malmoi_grams_free(&query);return VECDB_MEMORY;}
    for (i=0;i<s->document_count;++i) {
        VecDBSense sense;
        float score=malmoi_grams_cosine(&query,&s->documents[i]);
        (void)vecdb_sense(s->db,(uint32_t)i,&sense);
        if (score>scores[sense.word_id]) scores[sense.word_id]=score;
    }
    for (g=0;g<s->ng;++g) {
        MalmoiResult best={UINT32_MAX,NULL,NULL,0};
        const Group *group=&s->groups[g];
        for (j=group->start;j<group->start+group->count;++j) {
            const VecDBWord *word=&s->entries[j].word;
            if (scores[word->id]>best.score) {
                best.word_id=word->id;best.word=word->word;best.key=word->key;best.score=scores[word->id];
            }
        }
        if (best.word_id!=UINT32_MAX) insert(out,k,count,best);
    }
    free(scores);malmoi_grams_free(&query);return VECDB_OK;
}
