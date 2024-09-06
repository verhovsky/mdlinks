#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <sys/_types/_s_ifmt.h>
#include <sys/stat.h>
#include <unistd.h>
#include <yaml.h>
#include <cmark.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

int filter_file(const char *filename) {
    const char *ext = strrchr(filename, '.');
    return (
        ext && (!strcmp(ext, ".md") || !strcmp(ext, ".markdown"))
    );
}

int is_whitespace(const char *str) {
    while (*str) {
        if (!isspace((unsigned char)*str)) {
            return 0;
        }
        str++;
    }
    return 1;
}

void* process_file(const char *file_path, int64_t size) {
    FILE *file = fopen(file_path, "r");
    if (!file) {
        perror("Failed to open file");
        return NULL;
    }

    char *content = (char *)malloc(size + 1);
    if (!content) {
        perror("Failed to allocate memory");
        fclose(file);
        return NULL;
    }

    fread(content, 1, size, file);
    fclose(file);
    content[size] = '\0';

    // Extract front matter
    char *front_matter = NULL;
    char *start = content;
    if (strncmp(content, "---", 3) == 0) {
        front_matter = content + 3;
        start = strstr(front_matter, "---");
        if (start) {
            *start = '\0';
            start += 3;
            while (*start == '\n' || *start == '\r') start++;
        } else {
            start = content;
        }
    }

    // Parse Markdown content
    cmark_node *document = cmark_parse_document(start, strlen(start), CMARK_OPT_DEFAULT);
    cmark_iter *iter = cmark_iter_new(document);
    cmark_event_type ev_type;
    cmark_node *node;

    while ((ev_type = cmark_iter_next(iter)) != CMARK_EVENT_DONE) {
        node = cmark_iter_get_node(iter);
        if (ev_type == CMARK_EVENT_ENTER && cmark_node_get_type(node) == CMARK_NODE_LINK) {
            const char *url = cmark_node_get_url(node);
            if (url && (strlen(url) == 0 || is_whitespace(url))) {
                fprintf(stderr, "Warning: Empty or whitespace-only URL in file '%s'\n", file_path);
            } else if (url) {
                puts(url);
            }
        }
    }

    cmark_iter_free(iter);
    cmark_node_free(document);
    free(content);

    return NULL;
}

void list_files(const char *path) {
    struct stat path_stat;
    stat(path, &path_stat);

    if (S_ISDIR(path_stat.st_mode)) {
        DIR *dir = opendir(path);
        if (!dir) {
            perror("Failed to open directory");
            return;
        }

        struct dirent *entry;
        while ((entry = readdir(dir)) != NULL) {
            // skip hidden files/folders and current/parent directory
            if (entry->d_name[0] == '.') {
                continue;
            }

            char subpath[PATH_MAX];
            snprintf(subpath, sizeof(subpath), "%s/%s", path, entry->d_name);
            list_files(subpath);
        }

        closedir(dir);
    } else if (path_stat.st_mode & S_IFREG && filter_file(path)) {
        process_file(path, path_stat.st_size);
    }
}

int main(int argc, char *argv[]) {
    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    for (int i = 1; i < argc; i++) {
        char abs[PATH_MAX];
        if (realpath(argv[i], abs) != NULL) {
            list_files(abs);
        } else {
            perror("Error getting absolute path");
            return 1;
        }
    }

    return 0;
}
