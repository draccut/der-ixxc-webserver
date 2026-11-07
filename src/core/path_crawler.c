#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

#if __has_include("build/example/endpoints.h")
    #define CRAWLING_INPUT_PATH "build/example/crawling"
    #define CRAWLING_BASE_ENDPOINT "GET /crawling/"
#else
    #define CRAWLING_INPUT_PATH "../currency_game"
    #define CRAWLING_BASE_ENDPOINT "GET /currency_game/"
#endif

typedef struct {
    char *host_path;
    char *endpoint;
    int   flag;
} Entry;

typedef struct {
    Entry *data;
    size_t len;
    size_t capacity;
} EntryVec;

static void entry_vec_init(EntryVec *v)
{
    v->data = NULL;
    v->len = 0;
    v->capacity = 0;
}

static int entry_vec_push(EntryVec *v, char *host_path, char *endpoint)
{
    if (v->len >= v->capacity) {
        size_t new_cap = v->capacity ? v->capacity * 2 : 32;
        Entry *tmp = realloc(v->data, new_cap * sizeof(Entry));
        if (!tmp) return -1;
        v->data = tmp;
        v->capacity = new_cap;
    }
    v->data[v->len].host_path = host_path;
    v->data[v->len].endpoint  = endpoint;
    v->data[v->len].flag      = 0;
    v->len++;
    return 0;
}

static void entry_vec_free(EntryVec *v)
{
    for (size_t i = 0; i < v->len; i++) {
        free(v->data[i].host_path);
        free(v->data[i].endpoint);
    }
    free(v->data);
    v->data = NULL;
    v->len = v->capacity = 0;
}

static int contains_substring(const char *str, const char *sub)
{
    if (!*sub) return 1;
    for (int i = 0; str[i] != '\0'; i++) {
        int j = 0;
        while (str[i + j] != '\0' && sub[j] != '\0' && str[i + j] == sub[j]) {
            j++;
        }
        if (sub[j] == '\0') {
            return 1;
        }
    }
    return 0;
}

static void walk_dir_recursive(const char *dir, const char *base_fs, EntryVec *out)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "Cannot read directory '%s': %s\n", dir, strerror(errno));
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0 || contains_substring(entry->d_name, ".git") == 1) {
            continue;
        }
        char path[PATH_MAX];
        snprintf(path, sizeof(path), "%s/%s", dir, entry->d_name);
        struct stat st;
        if (stat(path, &st) != 0) {
            continue;
        }
        if (S_ISDIR(st.st_mode)) {
            walk_dir_recursive(path, base_fs, out);
        } else if (S_ISREG(st.st_mode)) {
            char *host_path = strdup(path);
            if (!host_path) {
                continue;
            }
            size_t base_len = strlen(base_fs);
            const char *rel = path;
            if (strncmp(path, base_fs, base_len) == 0) {
                rel = path + base_len;
                while (*rel == '/') {
                    rel++;
                }
            }
            size_t ep_len = strlen(rel) + 1;
            char *endpoint = malloc(ep_len);
            if (!endpoint) {
                free(host_path);
                continue;
            }
            snprintf(endpoint, ep_len, "%s", rel);
            if (entry_vec_push(out, host_path, endpoint) != 0) {
                free(host_path);
                free(endpoint);
            }
        }
    }
    closedir(d);
}

static EntryVec collect_from_dir(const char *fs_path)
{
    EntryVec entries;
    entry_vec_init(&entries);
    if (!fs_path) {
        fprintf(stderr, "fs_path is NULL\n");
        return entries;
    }
    struct stat st;
    if (stat(fs_path, &st) != 0 || !S_ISDIR(st.st_mode)) {
        fprintf(stderr, "Path '%s' is not a directory  -  skipping.\n", fs_path);
        return entries;
    }
    walk_dir_recursive(fs_path, fs_path, &entries);
    return entries;
}

static void write_to_output_file(char **strings, size_t count)
{
    const char *output_file = "build/generated/path_crawled.h";
    FILE *fp = fopen(output_file, "w");
    for (size_t i = 0; i < count; i++) {
        if (strings[i] != NULL) {
            if (fprintf(fp, "%s", strings[i]) < 0) {
                fclose(fp);
            }
        }
    }
}

static void print_entries_formatted(const EntryVec *v, const char *base_url)
{
    char **buf = malloc(sizeof(char *) * v->len);
    for (size_t i = 0; i < v->len; i++) {
        buf[i] = malloc(0x1000);
        snprintf(buf[i], 0x1000, "{\"%s\", \"%s%s\", 0},\n",
               v->data[i].host_path,
               base_url,
               v->data[i].endpoint);
    }
    write_to_output_file(buf, v->len);
    for (size_t i = 0; i < v->len; i++) {
        free(buf[i]);
    };
    free(buf);
}

static void build_crawled_paths(const char *to_crawl, const char *base_url)
{
    EntryVec entries = collect_from_dir(to_crawl);
    print_entries_formatted(&entries, base_url);
    entry_vec_free(&entries);
}

int main(void)
{
    build_crawled_paths(CRAWLING_INPUT_PATH, CRAWLING_BASE_ENDPOINT);
    return 0;
}
