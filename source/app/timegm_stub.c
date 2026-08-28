// timegm stub for Switch (newlib doesn't provide it)
// mktime uses local time, timegm uses UTC — close enough for annotation dates
#include <time.h>
#include <stdlib.h>

time_t timegm(struct tm *tm) {
    // Save original timezone
    const char *tz = getenv("TZ");
    setenv("TZ", "UTC0", 1);
    tzset();
    time_t result = mktime(tm);
    // Restore timezone
    if (tz) {
        setenv("TZ", tz, 1);
    } else {
        unsetenv("TZ");
    }
    tzset();
    return result;
}
