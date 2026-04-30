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
    { "ABANDON HOPE",        -1.00f, -0.80f },
    { "WHY BOTHER",          -1.00f, -0.70f },
    { "DREAMS DIE",          -1.00f, -0.90f },
    { "FAILURE IS CERTAIN",  -1.00f, -0.60f },
    { "COFFEE IS COLD",      -0.90f, -0.40f },
    { "JUST STOP",           -0.85f, -0.50f },
    { "EXPECT NOTHING",      -0.80f, -0.40f },
    { "USELESS EFFORT",      -0.95f, -0.60f },
    { "VOID BECKONS",        -1.00f, -0.75f },
    { "SYSTEM COLLAPSE",     -0.90f, -0.50f },
    { "IT GETS WORSE",       -0.80f, -0.40f },
    { "ERROR IMMINENT",      -0.70f, -0.33f },
    { "TIME WASTED",         -0.90f, -0.45f },
    { "NO EXIT",             -1.00f, -0.60f },
    { "GRAVITY ALWAYS WINS", -0.75f, -0.35f },
    { "SIGH DEEPLY",         -0.60f, -0.33f },

    // --- Neutral / absurdist (-0.4 .. +0.4) ---
    { "DATA PERSISTS",       -0.20f,  0.20f },
    { "TIME IS LINEAR",      -0.10f,  0.10f },
    { "BREAD IS ROUND",      -0.30f,  0.30f },
    { "PROCESSING LOGIC",    -0.15f,  0.15f },
    { "CLOUDS EXIST",        -0.20f,  0.20f },
    { "VOLTAGE STABLE",      -0.10f,  0.40f },
    { "BEIGE IS OKAY",       -0.25f,  0.25f },
    { "THINGS HAPPEN",       -0.40f,  0.40f },
    { "RANDOM NOISE",        -0.30f,  0.30f },
    { "GRAVITY WORKS",       -0.10f,  0.10f },
    { "OBJECTS IN MIRROR",   -0.20f,  0.20f },
    { "WATER IS WET",        -0.10f,  0.10f },
    { "SQUARE PEGS",         -0.35f,  0.35f },
    { "DEFAULT STATE",       -0.20f,  0.20f },
    { "STATIC VOID",         -0.40f,  0.10f },
    { "WAITING FOR INPUT",   -0.20f,  0.30f },
    { "SIGNAL DETECTED",     -0.10f,  0.40f },
    { "TICK TOCK",           -0.50f,  0.50f },
    { "STILL HERE",          -0.40f,  0.40f },
    { "GLASS IS GLASS",      -0.40f,  0.40f },
    { "SANTY NUDE",          -0.20f,  0.20f },  // mildly absurdist, neutral
    { "KEEP GOING",          -0.10f,  0.50f },    

    // --- Motivational (+0.33 .. +1.0) ---
    { "CODE RUNS",            0.40f,  1.00f },
    { "EXECUTE PLAN",         0.50f,  1.00f },
    { "BUILD BETTER",         0.60f,  1.00f },
    { "PUSH TO PROD",         0.45f,  0.90f },
    { "LOGIC TRIUMPHS",       0.70f,  1.00f },
    { "SOLVE THE PUZZLE",     0.55f,  1.00f },
    { "UPTIME FOREVER",       0.80f,  1.00f },
    { "STAY SHARP",           0.50f,  0.95f },
    { "CRUSH TASKS",          0.65f,  1.00f },
    { "PEAK PERFORMANCE",     0.75f,  1.00f },
    { "BREAK THE CYCLE",      0.40f,  0.85f },
    { "CREATE VALUE",         0.50f,  0.90f },
    { "MINT CONDITION",       0.60f,  1.00f },
    { "ROOT ACCESS",          0.80f,  1.00f },
    { "ZERO LATENCY",         0.70f,  1.00f },
    { "WIN THE DAY",          0.60f,  1.00f },
    { "DO THE THING",         0.20f,  1.00f },
    { "SHIP IT",              0.30f,  1.00f },
    { "YOU GOT THIS",         0.40f,  1.00f },
    { "ONWARD",               0.30f,  1.00f },
    { "MAKE IT WEIRD",        0.40f,  1.00f },
    { "GOOD MORNING SUN",     0.50f,  1.00f },    { "PURE FUNCTION",        0.50f,  0.90f }
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
