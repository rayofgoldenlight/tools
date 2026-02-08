#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <curl/curl.h>
#include <time.h>   // for clock_gettime

//NOTE IF YOU MODIFY: you may compile the program with the following: gcc gfind.c -o gfind.exe -lcurl
//If using Windows, you may need to install MSYS2 MINGW64 and compile in that terminal, or etc. depending on your situation

// Ignore response body
size_t write_callback(void *ptr, size_t size, size_t nmemb, void *userdata) {
    return size * nmemb;
}

typedef struct {
    char* original_url;
    char* final_url;
} UrlJob;

char* make_course_url(int course_num) {
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "https://sites.google.com/view/%d", course_num); //[[[MODIFY]]] -- this can be modified for your link of choice, %d represents the numbers you are testing
    return strdup(buffer); // malloc copy to persist
}

// Normalize by stripping trailing slashes
char* normalize_url(const char* url) {
    size_t len = strlen(url);
    while (len > 0 && url[len - 1] == '/') {
        len--;
    }
    char* norm = malloc(len + 1);
    strncpy(norm, url, len);
    norm[len] = '\0';
    return norm;
}

typedef struct {
    char *data;
    size_t size;
} MemoryChunk;

size_t store_callback(void *contents, size_t size, size_t nmemb, void *userp) {
    size_t real_size = size * nmemb;
    MemoryChunk *mem = (MemoryChunk *) userp;

    char *ptr = realloc(mem->data, mem->size + real_size + 1);
    if (ptr == NULL) return 0; // out of memory

    mem->data = ptr;
    memcpy(&(mem->data[mem->size]), contents, real_size);
    mem->size += real_size;
    mem->data[mem->size] = '\0';

    return real_size;
}

// Portable version of strcasestr
const char* my_strcasestr(const char* haystack, const char* needle) {
    if (!*needle) return haystack;
    size_t needle_len = strlen(needle);

    for (; *haystack; haystack++) {
        if (strncasecmp(haystack, needle, needle_len) == 0) {
            return haystack;
        }
    }
    return NULL;
}

char* extract_title(const char *html) {
    const char *start = my_strcasestr(html, "<title>");
    const char *end   = my_strcasestr(html, "</title>");
    if (start && end && end > start) {
        start += 7; // skip "<title>"
        size_t len = end - start;
        char *title = malloc(len + 1);
        strncpy(title, start, len);
        title[len] = '\0';
        return title;
    }
    return strdup(""); // no title
}

void process_block(int start, int end, int delay_seconds, int show_title) {
    int num_urls = end - start + 1;

    CURLM *multi_handle = curl_multi_init();
    CURL **easy_handles = malloc(sizeof(CURL*) * num_urls);
    UrlJob *jobs = malloc(sizeof(UrlJob) * num_urls);
    MemoryChunk *buffers = malloc(sizeof(MemoryChunk) * num_urls);

    // Set concurrency controls
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_TOTAL_CONNECTIONS, 50L);
    curl_multi_setopt(multi_handle, CURLMOPT_MAX_HOST_CONNECTIONS, 20L);
    curl_multi_setopt(multi_handle, CURLMOPT_PIPELINING, CURLPIPE_MULTIPLEX);

    // Setup easy handles for each URL
    for (int i = 0; i < num_urls; i++) {
        int course_num = start + i;
        jobs[i].original_url = make_course_url(course_num);
        jobs[i].final_url = NULL;

        buffers[i].data = malloc(1);
        buffers[i].size = 0;

        CURL *eh = curl_easy_init();
        curl_easy_setopt(eh, CURLOPT_URL, jobs[i].original_url);
        curl_easy_setopt(eh, CURLOPT_FOLLOWLOCATION, 1L);

        // Timeouts & HTTP/2
        curl_easy_setopt(eh, CURLOPT_HTTP_VERSION, CURL_HTTP_VERSION_2_0);
        curl_easy_setopt(eh, CURLOPT_TIMEOUT, 5L + delay_seconds);
        curl_easy_setopt(eh, CURLOPT_CONNECTTIMEOUT, 3L + delay_seconds);

        // Save body in buffer
        curl_easy_setopt(eh, CURLOPT_WRITEFUNCTION, store_callback);
        curl_easy_setopt(eh, CURLOPT_WRITEDATA, &buffers[i]);

        easy_handles[i] = eh;
        curl_multi_add_handle(multi_handle, eh);
    }

    int still_running = 0;
    curl_multi_perform(multi_handle, &still_running);
    while (still_running) {
        int numfds;
        curl_multi_wait(multi_handle, NULL, 0, 1000, &numfds);
        curl_multi_perform(multi_handle, &still_running);
    }

    printf("Non-redirecting URLs in block %d-%d:\n", start, end);
    for (int i = 0; i < num_urls; i++) {
        char *eff_url = NULL;
        curl_easy_getinfo(easy_handles[i], CURLINFO_EFFECTIVE_URL, &eff_url);
        if (eff_url) jobs[i].final_url = strdup(eff_url);

        long num_redirects = 0;
        curl_easy_getinfo(easy_handles[i], CURLINFO_REDIRECT_COUNT, &num_redirects);

        if (jobs[i].final_url) {
            char* orig_norm = normalize_url(jobs[i].original_url);
            char* final_norm = normalize_url(jobs[i].final_url);
            char *title = extract_title(buffers[i].data);

            if (num_redirects == 0 &&
                strcmp(orig_norm, final_norm) == 0 &&
                title &&
                strcasecmp(title, "Page Not Found") != 0 && // [[[MODIFY]]] "Page Not Found" and "Error 404 (Not Found)!!1" are the <title></title> data found non-redirected site. For google sites, these pages can be discarded as they are all the same. Modify these strings or remove them to help with your case scenario. 
                strcasecmp(title, "Error 404 (Not Found)!!1") != 0) { 

                if (show_title) {
                    printf("  [%s], %s\n", title, jobs[i].original_url);
                } else {
                    printf("  %s\n", jobs[i].original_url);
                }
            }

            free(title);
            free(orig_norm);
            free(final_norm);
        }

        // Cleanup
        curl_multi_remove_handle(multi_handle, easy_handles[i]);
        curl_easy_cleanup(easy_handles[i]);
        free(jobs[i].original_url);
        if (jobs[i].final_url) free(jobs[i].final_url);
        free(buffers[i].data);
    }

    curl_multi_cleanup(multi_handle);
    free(easy_handles);
    free(jobs);
    free(buffers);
}

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <start> <end> [--delay N] [--block N] [--title]\n", argv[0]);
        fprintf(stderr, "--delay # or --d #: delays each link test to ensure whether it redirects or not\n", argv[0]);
        fprintf(stderr, "--block # or --b #: processes links in blocks of N links, 50 is a good number to prevent errors\n", argv[0]);
        fprintf(stderr, "--title   or --t  : displays the title of the site alongside the url\n", argv[0]);
        fprintf(stderr, "\nPurpose: To find public working sites easier on the internet\n", argv[0]);
        return 1;
    }

    int start = atoi(argv[1]);
    int end   = atoi(argv[2]);
    if (end < start) {
        fprintf(stderr, "End must be >= start\n");
        return 1;
    }

    int delay_seconds = 0;
    int block_size = 0;

    int show_title = 0;   // default: don’t print titles

    for (int a = 3; a < argc; a++) {
        if ((strcmp(argv[a], "--delay") == 0 || strcmp(argv[a], "--d") == 0) && a + 1 < argc) {
            delay_seconds = atoi(argv[a+1]);
            a++;
        }
        else if ((strcmp(argv[a], "--block") == 0 || strcmp(argv[a], "--b") == 0) && a + 1 < argc) {
            block_size = atoi(argv[a+1]);
            a++;
        }
        else if ((strcmp(argv[a], "--title") == 0 || strcmp(argv[a], "--t") == 0)) {
            show_title = 1;   // enable title printing
        }
    }
    struct timespec start_time, end_time;
    clock_gettime(CLOCK_MONOTONIC, &start_time);

    curl_global_init(CURL_GLOBAL_ALL);

    int total_urls = end - start + 1;
    int processed = 0;
    while (processed < total_urls) {
        int chunk_start = start + processed;
        int this_block = (block_size > 0 && block_size < (total_urls - processed))
                         ? block_size
                         : (total_urls - processed);
        int chunk_end = chunk_start + this_block - 1;

        printf("\n--- Block %d: %d to %d ---\n",
            (processed / (block_size > 0 ? block_size : total_urls)) + 1,
            chunk_start, chunk_end);

        process_block(chunk_start, chunk_end, delay_seconds, show_title);

        processed += this_block;
    }

    curl_global_cleanup();

    clock_gettime(CLOCK_MONOTONIC, &end_time);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_nsec - start_time.tv_nsec) / 1e9;
    printf("\nElapsed time: %.3f seconds\n", elapsed);
    return 0;
}