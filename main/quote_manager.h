// quote_manager.h — picks a motivational or demotivational line based on
// a mood value in the range [-1.0, +1.0].
//
// Mood semantics:
//    -1.0 .. -0.33  demotivational / gallows humor
//    -0.33 .. +0.33 neutral / absurdist / neutral-weird
//    +0.33 .. +1.0  motivational
//
// Each quote carries a (min, max) window in that range; pickQuote() picks
// uniformly at random from all quotes whose window overlaps the current
// mood. Duplicates are avoided across a small history window so the same
// line doesn't repeat back-to-back.
//
// The mood value itself is provided by a MoodProvider (abstract base
// class). The current implementations are a stub RandomMoodProvider for
// testing and FixedMoodProvider for deterministic scenes. A real source
// (barometric pressure? calendar? SNTP drift?) can be added later as a
// new subclass without touching the rest of the system.

#pragma once

#include <cstddef>
#include <string>
#include <vector>

// Abstract source of "how is the clock feeling today?".
class MoodProvider {
public:
    virtual ~MoodProvider() = default;

    // Return a value in [-1.0, +1.0]. Implementations may cache;
    // QuoteManager calls this at most once per quote render.
    virtual float currentMood() = 0;
};

// Deterministic: always returns the same value. Useful during bringup,
// or as a manual override while the real mood source is offline.
class FixedMoodProvider : public MoodProvider {
public:
    explicit FixedMoodProvider(float value) : _value(value) {}
    float currentMood() override { return _value; }
private:
    float _value;
};

// Stub: uniform random mood on each call. Swap out for a real source
// once one is defined (the user noted "we'll figure that out later").
class RandomMoodProvider : public MoodProvider {
public:
    RandomMoodProvider();
    float currentMood() override;
private:
    uint32_t _rng;
};

struct Quote {
    const char* text;   // the line as it should appear on the display
    float       minMood;
    float       maxMood;
};

class QuoteManager {
public:
    // Pass nullptr to use the built-in, baked-in quote table (see
    // quote_manager.cpp for the default entries).
    QuoteManager(MoodProvider* mood,
                 const Quote* customQuotes = nullptr,
                 size_t numCustomQuotes = 0,
                 size_t historyLen = 4);

    // Returns the next quote to display. Never returns an empty string;
    // falls back to a neutral quote if none match the current mood.
    const char* pickQuote();

    // Accessor for the mood currently being used, exposed so the scene
    // playlist can log it / show a mood indicator scene.
    float lastMood() const { return _lastMood; }

private:
    bool recentlyShown(size_t idx) const;
    void rememberShown(size_t idx);

    MoodProvider*   _mood;
    const Quote*    _quotes;
    size_t          _numQuotes;

    std::vector<size_t> _history;
    size_t              _historyLen;
    size_t              _historyHead;

    float _lastMood;
};
