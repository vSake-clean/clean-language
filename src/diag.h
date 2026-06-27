#ifndef CLEAN_DIAG_H
#define CLEAN_DIAG_H

#include <stddef.h>

#define MAX_ERRORS 256

#define SEV_ERROR 0
#define SEV_WARN  1

typedef struct {
    int code;
    int severity;
    size_t line;
    size_t col;
    size_t len;
    char *msg;
} Error;

typedef struct {
    const char *filename;
    const char *source;
    size_t source_len;
    Error errors[MAX_ERRORS];
    int error_count;
    int warn_count;
    int seen_error;
} Diag;

void diag_init(Diag *d, const char *filename, const char *source, size_t source_len);
void diag_add(Diag *d, int code, int sev, size_t line, size_t col, size_t len, const char *msg);
int  diag_has_errors(Diag *d);
int  diag_has_any(Diag *d);
void diag_print_all(Diag *d);
void diag_free(Diag *d);

#endif
