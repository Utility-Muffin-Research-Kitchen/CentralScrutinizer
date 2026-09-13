#include "cs_i18n.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct { char *key; char *value; } cs_i18n_entry;
static cs_i18n_entry g_entries[512];
static size_t g_count;
static char g_language[16] = "en";
static int valid_lang(const char *s) {
    size_t n;
    if (!s || !*s) return 0;
    n = strlen(s); if (n >= sizeof(g_language)) return 0;
    while (*s) { if (!(('a'<=*s&&*s<='z')||('A'<=*s&&*s<='Z')||('0'<=*s&&*s<='9')||*s=='_'||*s=='-')) return 0; ++s; }
    return 1;
}
static int ascii_lower(char c) {
    return (c >= 'A' && c <= 'Z') ? (c - 'A' + 'a') : c;
}
static int is_language(const char *s, const char *name) {
    size_t i;
    for (i = 0; name[i] != '\0' && s[i] != '\0'; ++i) {
        if (ascii_lower(s[i]) != ascii_lower(name[i])) return 0;
    }
    return name[i] == '\0' && s[i] == '\0';
}
static int is_zh_variant(const char *s) {
    if (!s || ascii_lower(s[0]) != 'z' || ascii_lower(s[1]) != 'h') return 0;
    if (s[2] == '\0') return 1;
    if (s[2] != '_' && s[2] != '-') return 0;
    return s[3] != '\0' && s[4] != '\0'
        && ascii_lower(s[3]) == 'c' && ascii_lower(s[4]) == 'n' && s[5] == '\0';
}
static int fmt_sig(const char *s, char *out, size_t cap) {
    size_t n = 0; int count = 0;
    while (*s) {
        if (*s++ != '%') continue;
        if (*s == '%') { ++s; continue; }
        while (*s && strchr("-+ #0123456789.*'", *s)) ++s;
        while (*s && strchr("hlLqjzt", *s)) { if (n + 1 >= cap) return -1; out[n++] = *s++; }
        if (!*s || n + 1 >= cap) return -1;
        out[n++] = *s++; ++count;
    }
    if (n >= cap) return -1; out[n] = '\0'; return count;
}
static int fmt_compatible(const char *key, const char *value) {
    char a[64], b[64]; int na = fmt_sig(key, a, sizeof(a)), nb = fmt_sig(value, b, sizeof(b));
    return na >= 0 && na == nb && !strcmp(a, b);
}
static void unescape(char *s) { char *r=s; while (*s) { if (s[0]=='\\' && s[1]=='n') { *r++='\n'; s+=2; } else if (s[0]=='\\' && s[1]=='t') { *r++='\t'; s+=2; } else *r++=*s++; } *r='\0'; }
static void load_table(const char *path) {
    FILE *f = fopen(path, "rb"); char line[4096];
    if (!f) return;
    while (fgets(line, sizeof(line), f) && g_count < 512) {
        char *tab, *key, *val, *nl;
        if (line[0]=='#' || line[0]=='\n' || line[0]=='\r') continue;
        tab = strchr(line, '\t'); if (!tab) continue; *tab++='\0';
        nl = strpbrk(tab, "\r\n"); if (nl) *nl='\0';
        key=line; val=tab; unescape(key); unescape(val); if (!*key || !*val || !fmt_compatible(key, val)) continue;
        {
            size_t i;
            int duplicate = 0;
            for (i = 0; i < g_count; ++i) if (!strcmp(g_entries[i].key, key)) { duplicate = 1; break; }
            if (duplicate) continue;
        }
        g_entries[g_count].key=strdup(key); g_entries[g_count].value=strdup(val); ++g_count;
    }
    fclose(f);
}
void cs_i18n_init(const char *res_dir) {
    const char *lang=getenv("UMRK_LANGUAGE"); const char *base=getenv("USERDATA_PATH");
    char path[1024];
    if (!lang || !*lang) lang=getenv("JAWAKA_LANGUAGE");
    g_count=0;
    if (!valid_lang(lang) || is_language(lang, "en")) {
        strcpy(g_language, "en");
    } else if (is_zh_variant(lang)) {
        strcpy(g_language, "zh_CN");
    } else {
        strcpy(g_language, lang);
    }
    if (!strcmp(g_language,"en")) return;
    if (base && *base) { snprintf(path,sizeof(path),"%s/CentralScrutinizer/i18n/%s.tsv",base,g_language); load_table(path); }
    {
        const char *dir = getenv("CENTRAL_SCRUTINIZER_I18N_DIR");
        if (!dir || !*dir) dir = getenv("CS_I18N_DIR");
        if (!g_count && dir && *dir) { snprintf(path,sizeof(path),"%s/%s.tsv",dir,g_language); load_table(path); }
    }
    if (!g_count && res_dir && *res_dir) { snprintf(path,sizeof(path),"%s/../i18n/%s.tsv",res_dir,g_language); load_table(path); }
    if (!g_count) { snprintf(path,sizeof(path),"i18n/%s.tsv",g_language); load_table(path); }
    if (!g_count) strcpy(g_language,"en");
}
const char *cs_i18n_language(void) { return g_language; }
const char *cs_i18n(const char *english) {
    size_t i; if (!english) return "";
    for (i=0;i<g_count;i++) if (!strcmp(g_entries[i].key,english)) return g_entries[i].value;
    return english;
}
