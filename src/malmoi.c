#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 200809L
#endif
#include "malmoi.h"
#include "internal/search_backend.h"
#ifdef MALMOI_WITH_E5
#include "internal/e5_encoder.h"
#endif
#include <stdio.h>
#include <stdlib.h>
#ifdef __APPLE__
#include <mach-o/dyld.h>
#elif !defined(_WIN32)
#include <unistd.h>
#endif
#include <string.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <wchar.h>
#endif
struct Malmoi {
    VecDB *db;
    MalmoiSearch *search;
    char *path;
    int semantic;
#ifdef MALMOI_WITH_E5
    MalmoiE5 *encoder;
#endif
};
static char *copy(const char *s) {
    size_t n=strlen(s)+1;
    char *p=(char *)malloc(n);
    if (p) memcpy(p,s,n);
    return p;
}
/* Prefer the full development dataset, then the data shipped beside the app. */
static char *default_database_path(void) {
    char executable[32768];
    char *slash;
    FILE *f=fopen("data/full.mlm","rb");
    if (f) { fclose(f); return copy("data/full.mlm"); }
    executable[0]='\0';
#ifdef _WIN32
    {
        wchar_t wide[32768];
        DWORD n=GetModuleFileNameW(NULL,wide,32768);
        if (n && n<32768)
            if (!WideCharToMultiByte(CP_UTF8,0,wide,-1,executable,sizeof(executable),NULL,NULL))
                executable[0]='\0';
    }
#elif defined(__APPLE__)
    {
        uint32_t size=sizeof(executable);
        if (_NSGetExecutablePath(executable,&size)) executable[0]='\0';
    }
#else
    {
        ssize_t n=readlink("/proc/self/exe",executable,sizeof(executable)-1);
        if (n>0) executable[n]='\0';
        else executable[0]='\0';
    }
#endif
    slash=strrchr(executable,'/');
#ifdef _WIN32
    {
        char *backslash=strrchr(executable,'\\');
        if (backslash && (!slash || backslash>slash)) slash=backslash;
    }                    
#endif
    if (slash && (size_t)(slash-executable)+sizeof("/data/dictionary.mlm")<sizeof(executable)) {
        strcpy(slash,"/data/dictionary.mlm");
        f=fopen(executable,"rb");
        if (f) { fclose(f); return copy(executable); }
    }
    return copy("data/dictionary.mlm");
}
static char *database_path(void) {
#ifdef _WIN32
    const wchar_t *value;
    wchar_t *owned=NULL;
    char *result=NULL;
    int n;
#ifdef _MSC_VER
    size_t length=0;
    if (_wdupenv_s(&owned,&length,L"MALMOI_DATA")!=0) return NULL;
    value=owned;
#else
    value=_wgetenv(L"MALMOI_DATA");
#endif
    if (!value || !value[0]) { free(owned); return default_database_path(); }
    n=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,NULL,0,NULL,NULL);
    if (n) {
        result=(char *)malloc((size_t)n);
        if (result && !WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,value,-1,result,n,NULL,NULL)) {
            free(result);result=NULL;
        }
    }
    free(owned); return result;
#else
    const char *value=getenv("MALMOI_DATA");
    return value && value[0] ? copy(value) : default_database_path();
#endif
}
void malmoi_close(Malmoi *m) {
    if (m) {
        malmoi_search_close(m->search);
#ifdef MALMOI_WITH_E5
        malmoi_e5_close(m->encoder);
#endif
        vecdb_close(m->db);free(m->path);free(m);
    }
}
int malmoi_open(Malmoi **out) {
    Malmoi *m;
    char *path;
    int rc;
    const char *model=getenv("MALMOI_MODEL");
    MalmoiEncoder encoder={0};
    if (!out) return MALMOI_ARGUMENT;
    *out=NULL;
    path=database_path();if (!path) return MALMOI_MEMORY;
    m=(Malmoi *)calloc(1,sizeof(*m));
    if (!m) {free(path);return MALMOI_MEMORY;}
    m->path=path;
    rc=vecdb_open(&m->db,path);
    if (!rc && model && model[0]) {
#ifdef MALMOI_WITH_E5
        rc=malmoi_e5_open(&m->encoder,path,model,vecdb_word_space(m->db),&encoder);
        if (!rc) m->semantic=1;
#else
        rc=MALMOI_CONFIG;
#endif
    }
    if (!rc) rc=malmoi_search_open(&m->search,m->db,m->semantic ? &encoder : NULL);
    if (rc) {malmoi_close(m);return rc;}
    *out=m;return MALMOI_OK;
}
int malmoi_search(const Malmoi *m,const char *text,MalmoiResult *out,size_t k,size_t *count) {
    if (m && m->semantic) return malmoi_search_text(m->search,text,out,k,count);
    return malmoi_search_definitions(m ? m->search : NULL,text,out,k,count);
}
const char *malmoi_data_path(const Malmoi *m) { return m ? m->path : NULL; }
const char *malmoi_search_mode(const Malmoi *m) {
    return m ? (m->semantic ? "e5-semantic" : "lexical") : NULL;
}
int malmoi_similar(const Malmoi *m,const char *word,MalmoiResult *out,size_t k,size_t *count) {
    return malmoi_similar_words(m ? m->search : NULL,word,out,k,count);
}
int malmoi_similarity(const Malmoi *m,const char *a,const char *b,float *score) {
    return malmoi_word_similarity(m ? m->search : NULL,a,b,score);
}
int malmoi_similar_meaning(const Malmoi *m,const char *word,MalmoiResult *out,size_t k,size_t *count) {
    return malmoi_similar_words_meaning(m ? m->search : NULL,word,out,k,count);
}
int malmoi_similarity_meaning(const Malmoi *m,const char *a,const char *b,float *score) {
    return malmoi_word_similarity_meaning(m ? m->search : NULL,a,b,score);
}
const char *malmoi_error_string(int status) {
    return status==MALMOI_CONFIG ? "invalid encoder configuration" : vecdb_status_string(status);
}
