// anim_utils.h — shared helper used by every animation that renders text.
//
// Given input like "3.14" with dotsWithPrevious=true, we want the dot
// to *not* consume a cell on the display: it lights the decimal point
// of the cell to its left. This helper parses the string once into a
// flat char array + parallel "dot flag" array.

#pragma once

#include <cstdint>
#include <string>
#include <vector>

inline void parseTextAndDots(const std::string& inputText,
                             bool dotsWithPrevious,
                             std::string& outParsedText,
                             std::vector<uint8_t>& outDotStates) {
    outParsedText.clear();
    outDotStates.clear();

    for (char c : inputText) {
        if (c == '.' && dotsWithPrevious) {
            // Attach the dot to the previous glyph (if any).
            if (!outDotStates.empty()) outDotStates.back() = 1;
        } else {
            outParsedText += c;
            outDotStates.push_back(0);
        }
    }
}
