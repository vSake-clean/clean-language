#include <stdlib.h>
#include <stdio.h>

extern int main(int argc, char **argv);

extern char _clean_rodata_start;
extern char _clean_rodata_end;
extern unsigned char _clean_rodata_key;

extern void _clean_decrypt_rodata(void *start, void *end, uint8_t key);

__attribute__((section(".text._start")))
void _start(int argc, char **argv) {
    _clean_decrypt_rodata(&_clean_rodata_start, &_clean_rodata_end, _clean_rodata_key);
    int code = main(argc, argv);
    exit(code);
}

void __clean_panic(const char *msg) {
    fprintf(stderr, "panic: %s\n", msg);
    abort();
}
