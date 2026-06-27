#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>
#include <limits.h>
#include "parser/parser.h"
#include "codegen/codegen.h"
#include "diag.h"
#include "check.h"
#include "borrowck.h"
#include "mir/mir.h"

static void print_usage(void) {
    printf("Clean v0.2.0 — native compiler for Clean language\n\n");
    printf("Usage:\n");
    printf("  cl run <file.cl> [args...]               compile and run\n");
    printf("  cl build <file.cl> <output>               compile to native binary\n");
    printf("  cl --help                                 this help\n");
}

static char *read_file(const char *path, size_t *len_out) {
    FILE *f = fopen(path, "rb");
    if (!f) return NULL;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    if (len < 0) { fclose(f); return NULL; }
    fseek(f, 0, SEEK_SET);
    char *source = malloc((size_t)len + 1);
    if (!source) { fclose(f); return NULL; }
    size_t nread = fread(source, 1, (size_t)len, f);
    if (nread != (size_t)len) { free(source); fclose(f); return NULL; }
    source[len] = 0;
    fclose(f);
    *len_out = (size_t)len;
    return source;
}

static const char *get_rt_path(void) {
    static char path[PATH_MAX];
    char exe[PATH_MAX];
    ssize_t len = readlink("/proc/self/exe", exe, sizeof(exe)-1);
    if (len > 0) {
        exe[len] = '\0';
        char *slash = strrchr(exe, '/');
        if (slash) {
            size_t n = (size_t)(slash - exe);
            if (n >= sizeof(path)) n = sizeof(path) - 1;
            memcpy(path, exe, n);
            path[n] = '\0';
            return path;
        }
    }
    const char *home = getenv("HOME");
    if (home) {
        snprintf(path, sizeof(path), "%s/.local/bin", home);
        if (access(path, F_OK) == 0) return path;
    }
    return NULL;
}

static char *concat_source(const char *prefix, size_t prefix_len, const char *main, size_t main_len, size_t *out_len) {
    size_t total = prefix_len + 1 + main_len;
    char *buf = malloc(total + 1);
    if (!buf) return NULL;
    memcpy(buf, prefix, prefix_len);
    buf[prefix_len] = '\n';
    memcpy(buf + prefix_len + 1, main, main_len);
    buf[total] = 0;
    *out_len = total;
    return buf;
}

static int compile(const char *source_file, const char *output_file, int run_it, int prog_argc, char **prog_argv) {
    size_t source_len;
    char *source = read_file(source_file, &source_len);
    if (!source) { fprintf(stderr, "error: cannot open '%s'\n", source_file); return 1; }

    /* load prelude — try CWD, binary-relative, then installed location */
    size_t prelude_len = 0;
    char *prelude_src = read_file("lib/prelude.cl", &prelude_len);
    if (!prelude_src) {
        const char *rt = get_rt_path();
        if (rt) {
            char pp[PATH_MAX + 64];
            snprintf(pp, sizeof(pp), "%s/../lib/prelude.cl", rt);
            prelude_src = read_file(pp, &prelude_len);
            if (!prelude_src) {
                snprintf(pp, sizeof(pp), "%s/../share/clean/prelude.cl", rt);
                prelude_src = read_file(pp, &prelude_len);
            }
        }
    }
    if (prelude_src) {
        size_t combined_len = 0;
        char *combined = concat_source(prelude_src, prelude_len, source, source_len, &combined_len);
        free(prelude_src);
        free(source);
        source = combined;
        source_len = combined_len;
    }
    /* if prelude not found, continue without it */

    Diag diag;
    diag_init(&diag, source_file, source, source_len);

    Parser parser;
    parser_init(&parser, source_file, source, &diag);
    Node *prog = parser_parse(&parser);
    if (diag_has_errors(&diag)) {
        diag_print_all(&diag);
        node_free(prog); free(source); parser_free(&parser); diag_free(&diag);
        return 1;
    }

    check_program(prog, &diag, source);
    if (diag_has_errors(&diag)) {
        diag_print_all(&diag);
        node_free(prog); free(source); parser_free(&parser); diag_free(&diag);
        return 1;
    }
    if (diag_has_any(&diag)) diag_print_all(&diag);

    borrowck_program(prog, &diag, source);
    if (diag_has_errors(&diag)) {
        diag_print_all(&diag);
        node_free(prog); free(source); parser_free(&parser); diag_free(&diag);
        return 1;
    }
    if (diag_has_any(&diag)) diag_print_all(&diag);

    /* optimize: constant folding, dead code elimination, inlining */
    ast_optimize(prog);

    if (diag_has_any(&diag)) diag_print_all(&diag);

    const char *rt_path = get_rt_path();
    if (output_file) {
        codegen_compile(prog, source_file, output_file, &diag, rt_path);
    }
    if (run_it) {
        int run_result = codegen_run(prog, source_file, &diag, rt_path, prog_argc, prog_argv);
        if (diag_has_errors(&diag)) {
            diag_print_all(&diag);
            node_free(prog); free(source); parser_free(&parser); diag_free(&diag);
            return run_result ? run_result : 1;
        }
        node_free(prog);
        free(source);
        parser_free(&parser);
        diag_free(&diag);
        return run_result;
    }

    if (diag_has_errors(&diag)) {
        diag_print_all(&diag);
        node_free(prog); free(source); parser_free(&parser); diag_free(&diag);
        return 1;
    }

    node_free(prog);
    free(source);
    parser_free(&parser);
    diag_free(&diag);
    return 0;
}

int main(int argc, char **argv) {
    if (argc < 2 || !strcmp(argv[1], "--help") || !strcmp(argv[1], "-h")) {
        print_usage();
        return 0;
    }

    const char *cmd = argv[1];

    if (!strcmp(cmd, "run")) {
        if (argc < 3) {
            fprintf(stderr, "error: cl run <file.cl> [args...]\n");
            return 1;
        }
        return compile(argv[2], NULL, 1, argc - 3, argv + 3);
    }

    if (!strcmp(cmd, "build")) {
        if (argc < 4) {
            fprintf(stderr, "error: cl build <file.cl> <output>\n");
            return 1;
        }
        return compile(argv[2], argv[3], 0, 0, NULL);
    }

    fprintf(stderr, "error: '%s'. Use --help.\n", cmd);
    return 1;
}
