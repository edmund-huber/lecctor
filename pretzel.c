#include <stdlib.h>

void set_up_tracer(void);

void pretzel(int);

int main(int argc, char **argv) {
    set_up_tracer();

    if (argc != 2) {
        return 1;
    }

    int i = atoi(argv[1]);
    i += 97;
    pretzel(i);

    return 0;
}
