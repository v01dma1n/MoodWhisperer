// tz_data.h — dropdown-backing list of POSIX timezones.

#pragma once

#include "enc_types.h"

// Exported table used by BaseAccessPointManager to render the timezone
// dropdown. Each option maps a human-friendly label to a full POSIX TZ
// string that gets fed into setenv("TZ", ...).
extern const PrefSelectOption timezones[];
extern const int num_timezones;
