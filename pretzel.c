#include <stdlib.h>

void pretzel(int);

int main(int argc, char **argv) {
    if (argc != 2) {
        return 1;
    }

    int i = atoi(argv[1]);
    i += 97;
    pretzel(i);

    return 0;
}
