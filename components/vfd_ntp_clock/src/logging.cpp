#include "logging.h"

// The app can change this at any time; defaults to INFO so boot-time logs
// are not lost before preferences are restored.
AppLogLevel g_appLogLevel = APP_LOG_INFO;
