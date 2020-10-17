#include <stdlib.h>

// "asm volatile" at file scope is broken in GCC 8.3:
// https://gcc.gnu.org/bugzilla/show_bug.cgi?id=89585 .
asm ( "\n# as-tracer-do-not-instrument" );

void set_up_tracer(void);

void pretzel(int);

int main(int argc, char **argv) {
    set_up_tracer();

    asm volatile (
        "\n# as-tracer-do-instrument"
    );

    if (argc != 2) {
        return 1;
    }

    int i = atoi(argv[1]);
    i += 97;
    pretzel(i);

    return 0;
}
