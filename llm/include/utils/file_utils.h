#pragma once

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

std::string toHexString(int value);
void print_bar(int length);

// 辅助函数：打印美观的表格行
void print_row(const std::string &label, int value);

FILE *fopen_check(const char *path, const char *mode, const char *file,
                  int line);
#define fopenCheck(path, mode) fopen_check(path, mode, __FILE__, __LINE__)

void fread_check(void *ptr, size_t size, size_t nmemb, FILE *stream,
                 const char *file, int line);
#define freadCheck(ptr, size, nmemb, stream)                                   \
    fread_check(ptr, size, nmemb, stream, __FILE__, __LINE__)

void fclose_check(FILE *fp, const char *file, int line);
#define fcloseCheck(fp) fclose_check(fp, __FILE__, __LINE__)

void sclose_check(int sockfd, const char *file, int line);
#define scloseCheck(sockfd) sclose_check(sockfd, __FILE__, __LINE__)

#ifdef _WIN32
extern void closesocket_check(int sockfd, const char *file, int line);
#define closesocketCheck(sockfd) closesocket_check(sockfd, __FILE__, __LINE__)
#endif

void fseek_check(FILE *fp, long off, int whence, const char *file, int line);
#define fseekCheck(fp, off, whence)                                            \
    fseek_check(fp, off, whence, __FILE__, __LINE__)

void fwrite_check(void *ptr, size_t size, size_t nmemb, FILE *stream,
                  const char *file, int line);
#define fwriteCheck(ptr, size, nmemb, stream)                                  \
    fwrite_check(ptr, size, nmemb, stream, __FILE__, __LINE__)

// ----------------------------------------------------------------------------
// malloc error-handling wrapper util
void *malloc_check(size_t size, const char *file, int line);
#define mallocCheck(size) malloc_check(size, __FILE__, __LINE__)

// ----------------------------------------------------------------------------
// check that all tokens are within range
void token_check(const int *tokens, int token_count, int vocab_size,
                 const char *file, int line);
#define tokenCheck(tokens, count, vocab)                                       \
    token_check(tokens, count, vocab, __FILE__, __LINE__)

// ----------------------------------------------------------------------------
// I/O ops
void create_dir_if_not_exists(const char *dir);
extern int find_max_step(const char *output_log_dir);
extern int ends_with_bin(const char *str);