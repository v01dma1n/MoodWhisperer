// scene_manager.h — cycles through a playlist of DisplayScene entries.
//
// For each scene the manager:
//   1. Formats a string from scene.format_string, using scene.getDataValue()
//      (or strftime() for time-format specifiers).
//   2. Picks an IAnimation based on scene.animation_type.
//   3. Hands the animation to the DisplayManager.
//   4. Advances to the next scene after scene.duration_ms, OR for live
//      scenes (isLiveUpdate) re-runs the formatting/animation every second.
//
// Mirrors ESP32NTPClock::SceneManager.

#pragma once

#include "enc_types.h"

#include <cstdint>

class IBaseClock;

static constexpr int MAX_SCENE_TEXT_LEN = 64;

class SceneManager {
public:
    explicit SceneManager(IBaseClock& clock);

    // Point the manager at an externally-owned array of scenes.
    // The array must remain valid for the lifetime of the manager.
    void setup(const DisplayScene* playlist, int numScenes);

    // Drive one tick. Called from the app's main loop.
    void update();

private:
    void startCurrentScene();
    void renderCurrentSceneText(char* out, size_t out_size);

    IBaseClock& _app;

    const DisplayScene* _scenePlaylist;
    int                 _numScenes;
    int                 _currentSceneIndex;
    uint32_t            _sceneStartMs;
    uint32_t            _lastLiveUpdateMs;
    bool                _wasAnimationRunning;
};
