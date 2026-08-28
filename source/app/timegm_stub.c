// timegm stub for Switch (newlib doesn't provide it)
// MuPDF expects timegm() to convert UTC struct tm to time_t
// We save/restore TZ around mktime to simulate UTC behavior
#include <time.h>
#include <stdlib.h>

time_t mjx_timegm(struct tm *tm) {
    const char *saved_tz = getenv("TZ");
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t result = mktime(tm);
    if (saved_tz) {
        setenv("TZ", saved_tz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return result;
}
