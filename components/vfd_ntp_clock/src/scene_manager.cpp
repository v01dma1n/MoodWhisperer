#include "scene_manager.h"

#include "i_base_clock.h"
#include "display_manager.h"

#include "anim_static_text.h"
#include "anim_scrolling_text.h"
#include "anim_slot_machine.h"
#include "anim_matrix.h"
#include "anim_time.h"
#include "logging.h"

#include <cstdio>
#include <cstring>
#include <ctime>
#include <memory>

SceneManager::SceneManager(IBaseClock& clock)
    : _app(clock),
      _scenePlaylist(nullptr),
      _numScenes(0),
      _currentSceneIndex(0),
      _sceneStartMs(0),
      _lastLiveUpdateMs(0),
      _wasAnimationRunning(false) {}

void SceneManager::setup(const DisplayScene* playlist, int numScenes) {
    _scenePlaylist     = playlist;
    _numScenes         = numScenes;
    _currentSceneIndex = 0;
    _sceneStartMs      = 0;
    _lastLiveUpdateMs  = 0;
}

void SceneManager::renderCurrentSceneText(char* out, size_t out_size) {
    if (!_scenePlaylist || _numScenes <= 0) { out[0] = '\0'; return; }
    const DisplayScene& scene = _scenePlaylist[_currentSceneIndex];

    // Very light heuristic: if the format string contains '%' followed by
    // a typical strftime specifier letter, use strftime(); otherwise treat
    // it as a plain snprintf format taking the scene's data value.
    const char* fmt = scene.format_string ? scene.format_string : "";
    bool looksLikeStrftime = false;
    for (const char* p = fmt; *p; ++p) {
        if (*p == '%' && p[1]) {
            char c = p[1];
            if (c == 'H' || c == 'M' || c == 'S' || c == 'Y' || c == 'y' ||
                c == 'm' || c == 'd' || c == 'b' || c == 'B' || c == 'a' ||
                c == 'A' || c == 'j' || c == 'p' || c == 'I') {
                looksLikeStrftime = true;
                break;
            }
        }
    }

    if (looksLikeStrftime) {
        time_t now = time(nullptr);
        _app.formatTime(out, out_size, fmt, now);
        return;
    }

    float v = scene.getDataValue ? scene.getDataValue() : 0.0f;
    if (v == UNSET_VALUE) {
        // Data not yet available — show dashes of the right shape.
        std::snprintf(out, out_size, "---");
        return;
    }
    std::snprintf(out, out_size, fmt, v);
}

void SceneManager::startCurrentScene() {
    if (!_scenePlaylist || _numScenes <= 0) return;
    const DisplayScene& scene = _scenePlaylist[_currentSceneIndex];

    char text[MAX_SCENE_TEXT_LEN];
    renderCurrentSceneText(text, sizeof(text));

    LOGDBG("Scene #%d '%s' -> '%s'", _currentSceneIndex,
           scene.scene_name ? scene.scene_name : "?", text);

    std::unique_ptr<IAnimation> anim;
    switch (scene.animation_type) {
        case STATIC_TEXT:
            anim = std::make_unique<StaticTextAnimation>(text);
            break;
        case SCROLLING:
            anim = std::make_unique<ScrollingTextAnimation>(
                text, scene.anim_param_1, scene.dots_with_previous);
            break;
        case MATRIX:
            anim = std::make_unique<MatrixAnimation>(
                text, scene.anim_param_1, scene.anim_param_2,
                scene.dots_with_previous);
            break;
        case SLOT_MACHINE:
        default:
            anim = std::make_unique<SlotMachineAnimation>(
                text, scene.anim_param_1, scene.anim_param_2,
                scene.dots_with_previous);
            break;
    }
    _app.getClock().setAnimation(std::move(anim));
    _sceneStartMs     = app_millis();
    _lastLiveUpdateMs = _sceneStartMs;
}

void SceneManager::update() {
    if (!_app.isOkToRunScenes()) return;
    if (!_scenePlaylist || _numScenes <= 0) return;

    // Always advance the currently-installed animation.
    _app.getClock().update();

    // Lazy first-start: begin playing when update() is called for the
    // first time after setup().
    if (_sceneStartMs == 0) {
        startCurrentScene();
        return;
    }

    uint32_t now = app_millis();
    const DisplayScene& scene = _scenePlaylist[_currentSceneIndex];

    // Live update: for scenes that show seconds, re-render every 1s even
    // while the scene remains the same.
    if (scene.isLiveUpdate && (now - _lastLiveUpdateMs >= 1000)) {
        char text[MAX_SCENE_TEXT_LEN];
        renderCurrentSceneText(text, sizeof(text));

        // A slot-machine on a live scene would look silly; switch to a
        // static-text animation for the refresh.
        _app.getClock().setAnimation(std::make_unique<StaticTextAnimation>(text));
        _lastLiveUpdateMs = now;
    }

    // End-of-scene: advance when our timer expires.
    if (now - _sceneStartMs >= scene.duration_ms) {
        _currentSceneIndex = (_currentSceneIndex + 1) % _numScenes;
        startCurrentScene();
    }
}
