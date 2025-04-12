#include "utils/file_utils.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#ifndef _WIN32
#include <arpa/inet.h>
#include <dirent.h>
#endif
#include <iomanip>
#include <iostream>

std::string toHexString(int value) {
    std::ostringstream stream;
    stream << std::setw(3) << std::setfill('0') << value; // 设置宽度为3，并用'0'填充
    return stream.str();                                  // 返回结果字符串
}

void print_bar(int length) {
    for (int i = 0; i < length; ++i) {
        std::cout << "=";
    }
    std::cout << "\n";
}

// 辅助函数：打印美观的表格行
void print_row(const std::string &label, int value) { std::cout << "| " << std::left << std::setw(20) << label << "| " << std::right << std::setw(15) << value << " |\n"; }
// ----------------------------------------------------------------------------
// fread convenience utils, with nice handling of error checking using macros
// simple replace fopen, fread, fclose, fseek
// with fopenCheck, freadCheck, fcloseCheck, fseekCheck

FILE *fopen_check(const char *path, const char *mode, const char *file, int line) {
    FILE *fp = fopen(path, mode);
    if (fp == NULL) {
        fprintf(stderr, "Error: Failed to open file '%s' at %s:%d\n", path, file, line);
        fprintf(stderr, "Error details:\n");
        fprintf(stderr, "  File: %s\n", file);
        fprintf(stderr, "  Line: %d\n", line);
        fprintf(stderr, "  Path: %s\n", path);
        fprintf(stderr, "  Mode: %s\n", mode);
        fprintf(stderr, "---> HINT 1: dataset files/code have moved to dev/data "
                        "recently (May 20, 2024). You may have to mv them from the "
                        "legacy data/ dir to dev/data/(dataset), or re-run the data "
                        "preprocessing script. Refer back to the main README\n");
        fprintf(stderr, "---> HINT 2: possibly try to re-run `python train_gpt2.py`\n");
        exit(EXIT_FAILURE);
    }
    return fp;
}

void fread_check(void *ptr, size_t size, size_t nmemb, FILE *stream, const char *file, int line) {
    size_t result = fread(ptr, size, nmemb, stream);
    if (result != nmemb) {
        if (feof(stream)) {
            fprintf(stderr, "Error: Unexpected end of file at %s:%d\n", file, line);
        } else if (ferror(stream)) {
            fprintf(stderr, "Error: File read error at %s:%d\n", file, line);
        } else {
            fprintf(stderr,
                    "Error: Partial read at %s:%d. Expected %zu elements, read "
                    "%zu\n",
                    file, line, nmemb, result);
        }
        fprintf(stderr, "Error details:\n");
        fprintf(stderr, "  File: %s\n", file);
        fprintf(stderr, "  Line: %d\n", line);
        fprintf(stderr, "  Expected elements: %zu\n", nmemb);
        fprintf(stderr, "  Read elements: %zu\n", result);
        exit(EXIT_FAILURE);
    }
}

void fclose_check(FILE *fp, const char *file, int line) {
    if (fclose(fp) != 0) {
        fprintf(stderr, "Error: Failed to close file at %s:%d\n", file, line);
        fprintf(stderr, "Error details:\n");
        fprintf(stderr, "  File: %s\n", file);
        fprintf(stderr, "  Line: %d\n", line);
        exit(EXIT_FAILURE);
    }
}

void sclose_check(int sockfd, const char *file, int line) {
    if (close(sockfd) != 0) {
        fprintf(stderr, "Error: Failed to close socket at %s:%d\n", file, line);
        fprintf(stderr, "Error details:\n");
        fprintf(stderr, "  File: %s\n", file);
        fprintf(stderr, "  Line: %d\n", line);
        exit(EXIT_FAILURE);
    }
}

#ifdef _WIN32
extern void closesocket_check(int sockfd, const char *file, int line) {
    if (closesocket(sockfd) != 0) {
        fprintf(stderr, "Error: Failed to close socket at %s:%d\n", file, line);
        fprintf(stderr, "Error details:\n");
        fprintf(stderr, "  File: %s\n", file);
        fprintf(stderr, "  Line: %d\n", line);
        exit(EXIT_FAILURE);
    }
}
#endif

void fseek_check(FILE *fp, long off, int whence, const char *file, int line) {
    if (fseek(fp, off, whence) != 0) {
        fprintf(stderr, "Error: Failed to seek in file at %s:%d\n", file, line);
        fprintf(stderr, "Error details:\n");
        fprintf(stderr, "  Offset: %ld\n", off);
        fprintf(stderr, "  Whence: %d\n", whence);
        fprintf(stderr, "  File:   %s\n", file);
        fprintf(stderr, "  Line:   %d\n", line);
        exit(EXIT_FAILURE);
    }
}

void fwrite_check(void *ptr, size_t size, size_t nmemb, FILE *stream, const char *file, int line) {
    size_t result = fwrite(ptr, size, nmemb, stream);
    if (result != nmemb) {
        if (feof(stream)) {
            fprintf(stderr, "Error: Unexpected end of file at %s:%d\n", file, line);
        } else if (ferror(stream)) {
            fprintf(stderr, "Error: File write error at %s:%d\n", file, line);
        } else {
            fprintf(stderr,
                    "Error: Partial write at %s:%d. Expected %zu elements, "
                    "wrote %zu\n",
                    file, line, nmemb, result);
        }
        fprintf(stderr, "Error details:\n");
        fprintf(stderr, "  File: %s\n", file);
        fprintf(stderr, "  Line: %d\n", line);
        fprintf(stderr, "  Expected elements: %zu\n", nmemb);
        fprintf(stderr, "  Written elements: %zu\n", result);
        exit(EXIT_FAILURE);
    }
}

// ----------------------------------------------------------------------------
// malloc error-handling wrapper util

void *malloc_check(size_t size, const char *file, int line) {
    void *ptr = malloc(size);
    if (ptr == NULL) {
        fprintf(stderr, "Error: Memory allocation failed at %s:%d\n", file, line);
        fprintf(stderr, "Error details:\n");
        fprintf(stderr, "  File: %s\n", file);
        fprintf(stderr, "  Line: %d\n", line);
        fprintf(stderr, "  Size: %zu bytes\n", size);
        exit(EXIT_FAILURE);
    }
    return ptr;
}

// ----------------------------------------------------------------------------
// check that all tokens are within range
void token_check(const int *tokens, int token_count, int vocab_size, const char *file, int line) {
    for (int i = 0; i < token_count; i++) {
        if (!(0 <= tokens[i] && tokens[i] < vocab_size)) {
            fprintf(stderr, "Error: Token out of vocabulary at %s:%d\n", file, line);
            fprintf(stderr, "Error details:\n");
            fprintf(stderr, "  File: %s\n", file);
            fprintf(stderr, "  Line: %d\n", line);
            fprintf(stderr, "  Token: %d\n", tokens[i]);
            fprintf(stderr, "  Position: %d\n", i);
            fprintf(stderr, "  Vocab: %d\n", vocab_size);
            exit(EXIT_FAILURE);
        }
    }
}

// ----------------------------------------------------------------------------
// I/O ops

void create_dir_if_not_exists(const char *dir) {
    if (dir == NULL) {
        return;
    }
    struct stat st = {0};
    if (stat(dir, &st) == -1) {
        if (mkdir(dir, 0700) == -1) {
            printf("ERROR: could not create directory: %s\n", dir);
            exit(EXIT_FAILURE);
        }
        printf("created directory: %s\n", dir);
    }
}

int find_max_step(const char *output_log_dir) {
    // find the DONE file in the log dir with highest step count
    if (output_log_dir == NULL) {
        return -1;
    }
    DIR *dir;
    struct dirent *entry;
    int max_step = -1;
    dir = opendir(output_log_dir);
    if (dir == NULL) {
        return -1;
    }
    while ((entry = readdir(dir)) != NULL) {
        if (strncmp(entry->d_name, "DONE_", 5) == 0) {
            int step = atoi(entry->d_name + 5);
            if (step > max_step) {
                max_step = step;
            }
        }
    }
    closedir(dir);
    return max_step;
}

int ends_with_bin(const char *str) {
    // checks if str ends with ".bin". could be generalized in the future.
    if (str == NULL) {
        return 0;
    }
    size_t len = strlen(str);
    const char *suffix = ".bin";
    size_t suffix_len = strlen(suffix);
    if (len < suffix_len) {
        return 0;
    }
    int suffix_matches = strncmp(str + len - suffix_len, suffix, suffix_len) == 0;
    return suffix_matches;
}
