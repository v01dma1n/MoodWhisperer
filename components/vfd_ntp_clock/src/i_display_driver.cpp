#include "i_display_driver.h"

#include <cstring>

// Default implementation of print(): render a nul-terminated string left-aligned,
// optionally folding '.' into the previous cell's decimal point.
// Drivers are free to override, but the default behavior suffices for all
// current hardware targets.
void IDisplayDriver::print(const char* text, bool dotsWithPreviousChar) {
    if (text == nullptr) return;

    clear();

    int cells = getDisplaySize();
    int pos = 0;
    char prev = ' ';

    for (const char* p = text; *p != '\0' && pos < cells; ++p) {
        char c = *p;

        if (dotsWithPreviousChar && c == '.' && pos > 0) {
            // Re-emit the previous character with its dot lit.
            setChar(pos - 1, prev, /*dot=*/true);
            continue;
        }

        setChar(pos, c, /*dot=*/false);
        prev = c;
        ++pos;
    }

    writeDisplay();
}
