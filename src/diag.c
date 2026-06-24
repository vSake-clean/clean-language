#include "diag.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

void diag_init(Diag *d, const char *filename, const char *source, size_t source_len) {
    d->filename = filename;
    d->source = source;
    d->source_len = source_len;
    d->error_count = 0;
    d->warn_count = 0;
}

void diag_add(Diag *d, int code, int sev, size_t line, size_t col, size_t len, const char *msg) {
    if (d->error_count >= MAX_ERRORS) return;
    Error *e = &d->errors[d->error_count++];
    e->code = code;
    e->severity = sev;
    e->line = line;
    e->col = col;
    e->len = len;
    e->msg = msg ? strdup(msg) : NULL;
    if (sev == SEV_WARN) d->warn_count++;
}

int diag_has_errors(Diag *d) {
    return d->error_count > d->warn_count;
}

int diag_has_any(Diag *d) {
    return d->error_count > 0;
}

void diag_free(Diag *d) {
    for (int i = 0; i < d->error_count; i++)
        free(d->errors[i].msg);
}

static const char *sev_name(int sev) {
    return sev == SEV_ERROR ? "error" : "warning";
}

static size_t find_line(Diag *d, size_t line, const char **start_out, size_t *len_out) {
    size_t pos = 0;
    size_t cur = 1;
    *start_out = NULL;
    *len_out = 0;
    while (pos < d->source_len) {
        size_t bol = pos;
        while (pos < d->source_len && d->source[pos] != '\n') pos++;
        if (cur == line) {
            *start_out = d->source + bol;
            *len_out = pos - bol;
            return bol;
        }
        if (d->source[pos] == '\n') pos++;
        cur++;
    }
    return 0;
}

void diag_print_all(Diag *d) {
    for (int i = 0; i < d->error_count; i++) {
        Error *e = &d->errors[i];
        fprintf(stderr, "%s[E%04d]: %s\n", sev_name(e->severity), e->code, e->msg);
        fprintf(stderr, "  --> %s:%zu:%zu\n", d->filename, e->line, e->col);
        fprintf(stderr, "   |\n");

        const char *sline = NULL;
        size_t slen = 0;
        find_line(d, e->line, &sline, &slen);
        if (sline) {
            fprintf(stderr, "%4zu | ", e->line);
            for (size_t j = 0; j < slen; j++) {
                if (sline[j] == '\t') fputc(' ', stderr);
                else fputc(sline[j], stderr);
            }
            fputc('\n', stderr);

            fprintf(stderr, "     | ");
            for (size_t j = 1; j < e->col; j++) {
                if (j-1 < slen && sline[j-1] == '\t') fputc('\t', stderr);
                else fputc(' ', stderr);
            }
            size_t carets = e->len > 0 ? e->len : 1;
            if (carets > slen - e->col + 1) carets = 1;
            for (size_t j = 0; j < carets; j++) fputc('^', stderr);
            fputc('\n', stderr);
        }
        fputc('\n', stderr);
    }
}
