#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <curl/curl.h>
#include <pthread.h>

#define POOL_SIZE 16
#define FNAME_MAX 200

/* Compile with gcc -O2 -o scraper.exe scraper.c -lcurl -lpthread in MSYS2 MINGW64 */
 
/* ── memory buffer for curl ────────────────────────────────── */
typedef struct { char *d; size_t len, cap; } Mem;

static size_t on_data(void *p, size_t s, size_t n, void *u) {
    Mem *m = u; size_t sz = s * n;
    if (m->len + sz >= m->cap) {
        while (m->len + sz >= m->cap) m->cap *= 2;
        m->d = realloc(m->d, m->cap);
    }
    memcpy(m->d + m->len, p, sz);
    m->len += sz;
    return sz;
}

/* ── work queue ────────────────────────────────────────────── */
typedef struct {
    char          **urls;
    int             count, next;
    pthread_mutex_t mu;
} Queue;

/* ── url → safe filename ───────────────────────────────────── */
static void url_to_fname(const char *url, char *out, size_t sz) {
    const char *p = strstr(url, "://");
    p = p ? p + 3 : url;
    size_t i = 0;
    for (; *p && i < sz - 5 && i < FNAME_MAX; p++) {
        if (isalnum((unsigned char)*p) || *p == '-' || *p == '.')
            out[i++] = *p;
        else if (i > 0 && out[i - 1] != '_')
            out[i++] = '_';
    }
    while (i > 0 && (out[i - 1] == '_' || out[i - 1] == '.')) i--;
    if (i == 0) { memcpy(out, "page", 4); i = 4; }
    memcpy(out + i, ".txt", 5);
}

/* ── html helpers ──────────────────────────────────────────── */
static int imatch(const char *s, int n, const char *t) {
    int tl = (int)strlen(t);
    if (n < tl) return 0;
    for (int i = 0; i < tl; i++)
        if (tolower((unsigned char)s[i]) != t[i]) return 0;
    return n == tl || s[tl] == ' ' || s[tl] == '/'
                   || s[tl] == '\t'|| s[tl] == '\n';
}

static char decode_ent(const char *e, int n) {
    if (n <= 0) return ' ';
    if (e[0] == '#') {
        int v = (n > 1 && (e[1]=='x'||e[1]=='X'))
              ? (int)strtol(e + 2, NULL, 16) : atoi(e + 1);
        return (v > 0 && v < 128) ? (char)v : ' ';
    }
    if (imatch(e,n,"amp"))  return '&';
    if (imatch(e,n,"lt"))   return '<';
    if (imatch(e,n,"gt"))   return '>';
    if (imatch(e,n,"quot")) return '"';
    if (imatch(e,n,"apos")) return '\'';
    return ' ';
}

/* ── strip html → text file ────────────────────────────────── */
static void strip_write(const char *html, size_t len, const char *fname) {
    FILE *f = fopen(fname, "w");
    if (!f) { perror(fname); return; }

    static const char *blk[] = {
        "p","div","br","li","tr","dt","dd","blockquote",
        "h1","h2","h3","h4","h5","h6",
        "/p","/div","/li","/tr","/dt","/dd","/blockquote",
        "/h1","/h2","/h3","/h4","/h5","/h6", NULL
    };
    int in_tag = 0, skip = 0, in_ent = 0, lsp = 1;
    char tag[64], ent[16];
    int tl = 0, el = 0;

    for (size_t i = 0; i < len; i++) {
        unsigned char ch = html[i];

        if (in_ent) {
            if (ch == ';' || el >= 15) {
                ent[el] = 0;
                if (!skip && !in_tag) {
                    char dc = decode_ent(ent, el);
                    if (isspace(dc)) { if (!lsp) { fputc(' ',f); lsp=1; } }
                    else             { fputc(dc, f); lsp = 0; }
                }
                in_ent = 0;
            } else ent[el++] = ch;
            continue;
        }
        if (ch == '&' && !in_tag) { in_ent = 1; el = 0; continue; }

        if (ch == '<') { in_tag = 1; tl = 0; continue; }
        if (in_tag) {
            if (ch == '>') {
                in_tag = 0; tag[tl] = 0;
                if      (imatch(tag,tl,"script") || imatch(tag,tl,"style"))   skip = 1;
                else if (imatch(tag,tl,"/script")|| imatch(tag,tl,"/style"))  skip = 0;
                if (!skip)
                    for (int j = 0; blk[j]; j++)
                        if (imatch(tag, tl, blk[j]))
                            { fputc('\n', f); lsp = 1; break; }
            } else if (tl < 63) tag[tl++] = ch;
            continue;
        }
        if (skip) continue;

        if (isspace(ch)) { if (!lsp) { fputc(' ', f); lsp = 1; } }
        else             { fputc(ch, f); lsp = 0; }
    }
    fputc('\n', f);
    fclose(f);
}

/* ── worker thread ─────────────────────────────────────────── */
static void *worker(void *a) {
    Queue *q = a;
    for (;;) {
        pthread_mutex_lock(&q->mu);
        if (q->next >= q->count) { pthread_mutex_unlock(&q->mu); break; }
        int idx = q->next++;
        pthread_mutex_unlock(&q->mu);

        char *url = q->urls[idx];
        printf("[%d/%d] %s\n", idx + 1, q->count, url);

        Mem m = { malloc(1 << 16), 0, 1 << 16 };
        CURL *c = curl_easy_init();
        curl_easy_setopt(c, CURLOPT_URL,            url);
        curl_easy_setopt(c, CURLOPT_WRITEFUNCTION,  on_data);
        curl_easy_setopt(c, CURLOPT_WRITEDATA,      &m);
        curl_easy_setopt(c, CURLOPT_FOLLOWLOCATION, 1L);
        curl_easy_setopt(c, CURLOPT_TIMEOUT,        30L);
        curl_easy_setopt(c, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(c, CURLOPT_USERAGENT,
            "Mozilla/5.0 (Windows NT 10.0; Win64; x64)");

        CURLcode r = curl_easy_perform(c);
        curl_easy_cleanup(c);

        if (r != CURLE_OK) {
            fprintf(stderr, "  FAIL [%s]: %s\n", url, curl_easy_strerror(r));
        } else {
            char fname[512];
            url_to_fname(url, fname, sizeof(fname));
            strip_write(m.d, m.len, fname);
            printf("  -> %s (%zu bytes)\n", fname, m.len);
        }
        free(m.d);
    }
    return NULL;
}

/* ── read url list from file ───────────────────────────────── */
static char **read_urls(const char *path, int *count) {
    FILE *f = fopen(path, "r");
    if (!f) { perror(path); exit(1); }

    int cap = 64; *count = 0;
    char **urls = malloc(cap * sizeof(char *));
    char line[2048];

    while (fgets(line, sizeof(line), f)) {
        char *p = line;
        while (*p && isspace((unsigned char)*p)) p++;
        char *e = p + strlen(p);
        while (e > p && isspace((unsigned char)e[-1])) *--e = 0;
        if (*p == 0 || *p == '#') continue;          /* skip empty / comments */

        if (*count >= cap) { cap *= 2; urls = realloc(urls, cap * sizeof(char *)); }
        urls[(*count)++] = strdup(p);
    }
    fclose(f);
    return urls;
}

static int file_exists(const char *s) {
    FILE *f = fopen(s, "r");
    if (f) { fclose(f); return 1; }
    return 0;
}

/* ── main ──────────────────────────────────────────────────── */
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr,
            "Usage:\n"
            "  %s <url>        scrape one page\n"
            "  %s urls.txt     scrape all links in file\n",
            argv[0], argv[0]);
        return 1;
    }

    curl_global_init(CURL_GLOBAL_DEFAULT);

    Queue q = { .next = 0 };
    pthread_mutex_init(&q.mu, NULL);

    int from_file = 0;
    if (strncmp(argv[1], "http://", 7) == 0 ||
        strncmp(argv[1], "https://", 8) == 0) {
        q.urls    = malloc(sizeof(char *));
        q.urls[0] = strdup(argv[1]);
        q.count   = 1;
    } else if (file_exists(argv[1])) {
        q.urls  = read_urls(argv[1], &q.count);
        from_file = 1;
        if (q.count == 0) { puts("No URLs found in file."); return 1; }
    } else {
        fprintf(stderr, "Error: \"%s\" is not a URL or readable file.\n", argv[1]);
        return 1;
    }

    int nt = q.count < POOL_SIZE ? q.count : POOL_SIZE;
    printf("Scraping %d URL(s) with %d thread(s)...\n\n", q.count, nt);

    pthread_t *thr = malloc(nt * sizeof(pthread_t));
    for (int i = 0; i < nt; i++)
        pthread_create(&thr[i], NULL, worker, &q);
    for (int i = 0; i < nt; i++)
        pthread_join(thr[i], NULL);

    printf("\nDone. %d page(s) scraped.\n", q.count);

    for (int i = 0; i < q.count; i++) free(q.urls[i]);
    free(q.urls);
    free(thr);
    pthread_mutex_destroy(&q.mu);
    curl_global_cleanup();
    return 0;
}