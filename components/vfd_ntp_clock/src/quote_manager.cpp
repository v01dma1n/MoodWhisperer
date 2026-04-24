// quote_manager.cpp — mood-banded quote picker.

#include "quote_manager.h"
#include "logging.h"

#include "esp_random.h"

#include <algorithm>
#include <cstdint>

// ---------------------------------------------------------------------------
// Default quote table. Hand-curated, deliberately short so the whole line
// fits the 10-cell VFD after scrolling is over. Longer lines are fine too
// — ScrollingTextAnimation handles them.
//
// Ranges are inclusive. Quotes with overlapping ranges are fine; the
// picker chooses uniformly among eligible entries.
// ---------------------------------------------------------------------------

static const Quote s_defaultQuotes[] = {
    // --- Demotivational (-1.0 .. -0.33) ---
    { "ENTROPY WINS",        -1.00f, -0.50f },
    { "NOTHING MATTERS",     -1.00f, -0.50f },
    { "BRACE YOURSELF",      -1.00f, -0.40f },
    { "THIS IS FINE",        -1.00f, -0.20f },
    { "TOMORROW BREAKS",     -1.00f, -0.40f },
    { "NOT GREAT",           -1.00f, -0.30f },

    // --- Neutral / absurdist (-0.4 .. +0.4) ---
    { "TICK TOCK",           -0.50f,  0.50f },
    { "STILL HERE",          -0.40f,  0.40f },
    { "GLASS IS GLASS",      -0.40f,  0.40f },
    { "SANTY NUDE",          -0.20f,  0.20f },  // mildly absurdist, neutral
    { "KEEP GOING",          -0.10f,  0.50f },

    // --- Motivational (+0.33 .. +1.0) ---
    { "DO THE THING",         0.20f,  1.00f },
    { "SHIP IT",              0.30f,  1.00f },
    { "YOU GOT THIS",         0.40f,  1.00f },
    { "ONWARD",               0.30f,  1.00f },
    { "MAKE IT WEIRD",        0.40f,  1.00f },
    { "GOOD MORNING SUN",     0.50f,  1.00f },
};
static constexpr size_t s_defaultQuotesLen =
    sizeof(s_defaultQuotes) / sizeof(s_defaultQuotes[0]);

// ---------------------------------------------------------------------------

RandomMoodProvider::RandomMoodProvider() : _rng(esp_random() | 1u) {}

float RandomMoodProvider::currentMood() {
    uint32_t x = _rng;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    _rng = x;

    // Map uint32 to (-1, +1) uniformly.
    return (static_cast<float>(x) / static_cast<float>(UINT32_MAX)) * 2.0f - 1.0f;
}

// ---------------------------------------------------------------------------

QuoteManager::QuoteManager(MoodProvider* mood,
                           const Quote* customQuotes,
                           size_t numCustomQuotes,
                           size_t historyLen)
    : _mood(mood),
      _quotes(customQuotes ? customQuotes : s_defaultQuotes),
      _numQuotes(customQuotes ? numCustomQuotes : s_defaultQuotesLen),
      _history(historyLen, static_cast<size_t>(-1)),
      _historyLen(historyLen),
      _historyHead(0),
      _lastMood(0.0f) {}

bool QuoteManager::recentlyShown(size_t idx) const {
    for (size_t v : _history) if (v == idx) return true;
    return false;
}

void QuoteManager::rememberShown(size_t idx) {
    _history[_historyHead] = idx;
    _historyHead = (_historyHead + 1) % _historyLen;
}

const char* QuoteManager::pickQuote() {
    _lastMood = _mood ? _mood->currentMood() : 0.0f;
    if (_lastMood < -1.0f) _lastMood = -1.0f;
    if (_lastMood >  1.0f) _lastMood =  1.0f;

    // Build a candidate list.
    std::vector<size_t> candidates;
    candidates.reserve(_numQuotes);
    for (size_t i = 0; i < _numQuotes; ++i) {
        if (_lastMood >= _quotes[i].minMood &&
            _lastMood <= _quotes[i].maxMood &&
            !recentlyShown(i)) {
            candidates.push_back(i);
        }
    }

    // If history filter left us empty, relax it.
    if (candidates.empty()) {
        for (size_t i = 0; i < _numQuotes; ++i) {
            if (_lastMood >= _quotes[i].minMood &&
                _lastMood <= _quotes[i].maxMood) {
                candidates.push_back(i);
            }
        }
    }

    // Absolute fallback: any quote at all.
    if (candidates.empty()) {
        for (size_t i = 0; i < _numQuotes; ++i) candidates.push_back(i);
    }
    if (candidates.empty()) return "";  // pathological: empty table

    size_t pick = candidates[esp_random() % candidates.size()];
    rememberShown(pick);

    LOGDBG("QuoteManager: mood=%.2f picked='%s'",
           _lastMood, _quotes[pick].text);
    return _quotes[pick].text;
}
