#include "platform_time.h"

#ifdef ESP_PLATFORM
#include "esp_timer.h"
#else
#include <sys/time.h>
#endif

uint32_t platform_millis(void)
{
#ifdef ESP_PLATFORM
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
#else
    struct timeval tv;

    gettimeofday(&tv, NULL);
    return (uint32_t)(tv.tv_sec * 1000U + (tv.tv_usec / 1000U));
#endif
}
