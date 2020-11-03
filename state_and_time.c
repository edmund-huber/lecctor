#include <stdio.h>
#include <stdlib.h>
#include <limits.h>
#include <time.h>

void set_up_tracer(void);

int int_cmp(int *p1, int *p2) {
    return *p1 > *p2;
}

int main(int argc, char **argv) {
    set_up_tracer();

    // Parse out arguments.
    if (argc != 3) {
        fprintf(stderr, "usage: state_and_time <seed> <# seconds>\n");
        return 1;
    }
    int seed = atoi(argv[1]);
    int seconds = atoi(argv[2]);

    // Do a bunch of calculations that compound on previous results, such that
    // it's very hard to tell what the behavior of the program will be given
    // any given seed input.
    time_t start_time = time(NULL);
    enum {
        HAPPY = 0,
        SAD
    } emotion = seed % 2;
    enum {
        BANANA = 0,
        APPLE
    } fruit = (seed + 1) % 2;
    enum {
        RED = 0,
        GREEN
    } color = RED;
    while (time(NULL) - start_time < seconds) {
        if ((fruit == BANANA) && (color == RED))
            emotion = HAPPY;
        else if ((fruit == BANANA) && (color == GREEN))
            emotion = HAPPY;
        else if ((fruit == APPLE) && (color == RED))
            emotion = HAPPY;
        else if ((fruit == APPLE) && (color == GREEN))
            emotion = SAD;

        if ((color == RED) && (emotion == HAPPY))
            fruit = BANANA;
        else if ((color == RED) && (emotion == SAD))
            fruit = APPLE;
        else if ((color == GREEN) && (emotion == HAPPY)) 
            fruit = BANANA;
        else if ((color == GREEN) && (emotion == SAD))
            fruit = APPLE;

        if ((emotion == HAPPY) && (fruit == BANANA))
            color = RED;
        else if ((emotion == HAPPY) && (fruit == APPLE))
            color = GREEN;
        else if ((emotion == SAD) && (fruit == BANANA))
            color = GREEN;
        else if ((emotion == SAD) && (fruit == APPLE))
            color = RED;
    }

    return 0;
}
