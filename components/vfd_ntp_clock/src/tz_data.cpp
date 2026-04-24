// tz_data.cpp — POSIX timezone options for the captive-portal dropdown.
//
// A subset that covers the populous tz regions. Values are either full
// POSIX TZ strings (preferred; no newlib tzdata required) or IANA names
// (only useful if the build includes tzdata — newlib's implementation
// falls back to UTC for unknown names). The POSIX-string entries are
// safe on a bare IDF build.

#include "tz_data.h"

const PrefSelectOption timezones[] = {
    { "(UTC) Coordinated Universal Time",                         "UTC0"                          },
    { "(GMT-10:00) Hawaii",                                       "HST10"                         },
    { "(GMT-09:00) Alaska",                                       "AKST9AKDT,M3.2.0,M11.1.0"      },
    { "(GMT-08:00) Pacific Time (US & Canada)",                   "PST8PDT,M3.2.0,M11.1.0"        },
    { "(GMT-07:00) Mountain Time (US & Canada)",                  "MST7MDT,M3.2.0,M11.1.0"        },
    { "(GMT-07:00) Arizona (no DST)",                             "MST7"                          },
    { "(GMT-06:00) Central Time (US & Canada)",                   "CST6CDT,M3.2.0,M11.1.0"        },
    { "(GMT-05:00) Eastern Time (US & Canada)",                   "EST5EDT,M3.2.0,M11.1.0"        },
    { "(GMT-04:00) Atlantic Time (Canada)",                       "AST4ADT,M3.2.0,M11.1.0"        },
    { "(GMT-03:00) Buenos Aires",                                 "ART3"                          },
    { "(GMT+00:00) London, Dublin",                               "GMT0BST,M3.5.0/1,M10.5.0"      },
    { "(GMT+01:00) Berlin, Warsaw, Paris, Rome",                  "CET-1CEST,M3.5.0,M10.5.0/3"    },
    { "(GMT+02:00) Athens, Helsinki, Kyiv",                       "EET-2EEST,M3.5.0/3,M10.5.0/4"  },
    { "(GMT+03:00) Moscow, Istanbul",                             "MSK-3"                         },
    { "(GMT+05:30) India Standard Time",                          "IST-5:30"                      },
    { "(GMT+08:00) Beijing, Singapore, Hong Kong",                "CST-8"                         },
    { "(GMT+09:00) Tokyo, Seoul",                                 "JST-9"                         },
    { "(GMT+10:00) Sydney",                                       "AEST-10AEDT,M10.1.0,M4.1.0/3"  },
    { "(GMT+12:00) Auckland",                                     "NZST-12NZDT,M9.5.0,M4.1.0/3"   },
};

const int num_timezones = sizeof(timezones) / sizeof(PrefSelectOption);
