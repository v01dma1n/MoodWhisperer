// whisperer_access_point_manager.h — adds app-specific rows to the portal.
//
// Adds: display brightness, startup-anim toggle, mood source selector,
// fixed-mood slider (as an int for portability — rendered as a plain
// number, user picks between -100 and +100).

#pragma once

#include "ESP32NTPClock.h"
#include "whisperer_preferences.h"

class WhispererAccessPointManager : public BaseAccessPointManager {
public:
    explicit WhispererAccessPointManager(WhispererPreferences& prefs)
        : BaseAccessPointManager(prefs) {}

protected:
    void initializeFormFields() override;
};
