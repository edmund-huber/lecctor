#ifndef __ASSERT_H__
#define __ASSERT_H__

#include <stdlib.h>
#include <stdio.h>

#define ASSERT(cond) \
    { \
        if (!(cond)) { \
            printf("ASSERT(%s) failed at %s L%i\n", #cond, __FILE__, __LINE__); \
            exit(1); \
        } \
    }

#define xstr(s) str(s)
#define str(s) #s

#endif
