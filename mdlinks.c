#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <pthread.h>
#include <ctype.h>
#include <fcntl.h>
#include <errno.h>
#include <limits.h>

#include <cmark.h>

#define BUFFER_SIZE 1024

typedef struct {
    char filenames[BUFFER_SIZE][PATH_MAX];
    int64_t file_sizes[BUFFER_SIZE];
    int start;
    int end;
    int count;
    int done;
    pthread_mutex_t mutex;
    pthread_mutex_t out_mutex;
    pthread_cond_t not_empty;
    pthread_cond_t not_full;
} ring_buffer_t;

ring_buffer_t ring_buffer;

void ring_buffer_init(ring_buffer_t *rb) {
    rb->start = 0;
    rb->end = 0;
    rb->count = 0;
    rb->done = 0;
    pthread_mutex_init(&rb->mutex, NULL);
    pthread_mutex_init(&rb->out_mutex, NULL);
    pthread_cond_init(&rb->not_empty, NULL);
    pthread_cond_init(&rb->not_full, NULL);
}

void ring_buffer_enqueue(ring_buffer_t *rb, const char *filename, int64_t size) {
    pthread_mutex_lock(&rb->mutex);
    while (rb->count == BUFFER_SIZE) {
        pthread_cond_wait(&rb->not_full, &rb->mutex);
    }
    strncpy(rb->filenames[rb->end], filename, PATH_MAX);
    rb->file_sizes[rb->end] = size;
    rb->end = (rb->end + 1) % BUFFER_SIZE;
    rb->count++;
    pthread_cond_signal(&rb->not_empty);
    pthread_mutex_unlock(&rb->mutex);
}

char *ring_buffer_dequeue(ring_buffer_t *rb, char *filename, int64_t *size) {
    pthread_mutex_lock(&rb->mutex);
    while (rb->count == 0) {
        if (rb->done) {
            pthread_mutex_unlock(&rb->mutex);
            return NULL;
        }
        pthread_cond_wait(&rb->not_empty, &rb->mutex);
    }
    strncpy(filename, rb->filenames[rb->start], PATH_MAX);
    // fprintf(stderr, "Worker thread %ld processing %d file: %s\n", pthread_self(), rb->start, filename);
    *size = rb->file_sizes[rb->start];
    rb->start = (rb->start + 1) % BUFFER_SIZE;
    rb->count--;
    pthread_cond_signal(&rb->not_full);
    pthread_mutex_unlock(&rb->mutex);
    return filename;
}

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

void* process_file(char *arg, int64_t size, ring_buffer_t *rb) {
    const char *file_path = (const char *)arg;
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
                pthread_mutex_lock(&rb->out_mutex);
                puts(url);
                pthread_mutex_unlock(&rb->out_mutex);
            }
        }
    }

    cmark_iter_free(iter);
    cmark_node_free(document);
    free(content);

    return NULL;
}

void *worker_thread(void *arg) {
    char filename[PATH_MAX];
    int64_t size;
    while (1) {
        char *res = ring_buffer_dequeue(&ring_buffer, filename, &size);
        if (res == NULL) {
            break;
        }
        // fprintf(stderr, "Worker thread %ld processing file: %s\n", pthread_self(), filename);
        process_file(filename, size, &ring_buffer);
    }
    return NULL;
}

void list_files(const char *path) {
    struct stat path_stat;
    if (stat(path, &path_stat) == -1) {
        perror("Failed to stat path");
        return;
    }

    // Don't process symlinks
    // if (S_ISLNK(path_stat.st_mode)) {
    //     return;
    // }

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
    } else if (S_ISREG(path_stat.st_mode) && filter_file(path)) {
        // fprintf(stderr, "enqueueing: %s\n", path);
        ring_buffer_enqueue(&ring_buffer, path, path_stat.st_size);
    }
}

int main(int argc, char *argv[]) {
    long num_procs;

    if (argc < 2) {
        fprintf(stderr, "Usage: %s <directory_path>\n", argv[0]);
        exit(EXIT_FAILURE);
    }


    ring_buffer_init(&ring_buffer);

    num_procs = sysconf(_SC_NPROCESSORS_ONLN);
    if (num_procs < 1) {
        num_procs = 1;
    }

    pthread_t workers[num_procs];
    for (int i = 0; i < num_procs; i++) {
        pthread_create(&workers[i], NULL, worker_thread, NULL);
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

    pthread_mutex_lock(&ring_buffer.mutex);
    ring_buffer.done = 1;
    pthread_cond_broadcast(&ring_buffer.not_empty);
    pthread_mutex_unlock(&ring_buffer.mutex);
    for (int i = 0; i < num_procs; i++) {
        pthread_join(workers[i], NULL);
    }

    return 0;
}
