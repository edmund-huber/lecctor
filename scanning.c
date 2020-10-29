#include <string.h>

#include "scanning.h"

int skip_exactly(char *to_skip, char **s) {
    // If *s begins with to_skip, progress *s past to_skip and return 1.
    if (strncmp(*s, to_skip, strlen(to_skip)) == 0) {
        *s += strlen(to_skip);
        return 1;
    }
    // Else return 0.
    return 0;
}

int scan_until_any(char *any_c, char **s, char *scanned, int scanned_sz) {
    char *original_s = *s;
    // Until we've reached the end of *s,
    int scanned_off = 0;
    if (scanned != NULL)
        scanned[0] = '\0';
    for (; **s != '\0'; *s += 1) {
        // .. if we found any of the characters in any_c, then success.
        if (strchr(any_c, **s)) {
            return 1;
        }
        // Store the characters we're skipping in scanned.
        if (scanned != NULL) {
            if (scanned_off + 1 == scanned_sz) {
                return 0;
            }
            scanned[scanned_off] = **s;
            scanned[scanned_off + 1] = '\0';
            scanned_off++;
        }
    }
    // If we did not find any c in *s, then we need to restore *s to where it
    // was at the start.
    *s = original_s;
    return 0;
}
