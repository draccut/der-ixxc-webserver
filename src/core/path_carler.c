#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <limits.h>
#include <errno.h>

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

static void walk_dir_recursive(const char *dir, const char *base_fs, EntryVec *out)
{
    DIR *d = opendir(dir);
    if (!d) {
        fprintf(stderr, "Cannot read directory '%s': %s\n", dir, strerror(errno));
        return;
    }
    struct dirent *entry;
    while ((entry = readdir(d)) != NULL) {
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0) {
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
            size_t ep_len = strlen("GET /") + strlen(rel) + 1;
            char *endpoint = malloc(ep_len);
            if (!endpoint) {
                free(host_path);
                continue;
            }
            snprintf(endpoint, ep_len, "GET /%s", rel);
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

static int calc_max_host_path_len(const EntryVec *v)
{
    size_t max_len = 0;
    for (size_t i = 0; i < v->len; i++) {
        size_t len = strlen(v->data[i].host_path);
        if (len > max_len) {
            max_len = len;
        }
    }
    return (int)(max_len + 5);
}

static void print_entries_formatted(const EntryVec *v)
{
    if (v->len == 0) {
        printf("/* keine Einträge */\n");
        return;
    }
    int width = calc_max_host_path_len(v);
    printf("{\n");
    for (size_t i = 0; i < v->len; i++) {
        printf("    {\"%s\",%*s\"%s\", %d}%s\n",
               v->data[i].host_path,
               width - (int)strlen(v->data[i].host_path), "",
               v->data[i].endpoint,
               v->data[i].flag,
               (i + 1 < v->len) ? "," : "");
    }
    printf("};\n");
}

int main(int argc, char **argv)
{
    const char *dir = (argc > 1) ? argv[1] : "../currency-game";
    EntryVec entries = collect_from_dir(dir);
    /* Optional: den berechneten Wert auch ausgeben */
    int max_w = calc_max_host_path_len(&entries);
    fprintf(stderr, "/* max host_path width (strlen + 5) = %d */\n\n", max_w);

    print_entries_formatted(&entries);

    entry_vec_free(&entries);
    return 0;
}
