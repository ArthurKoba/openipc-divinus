#include "watchdog.h"

static int fd = -1;

void watchdog_reset(void) {
    if (fd < 0) return;
    if (write(fd, "", 1) != 1)
        HAL_WARNING("watchdog", "Failed to reset watchdog!\n");
}

int watchdog_start(int timeout) {
    const char* paths[] = {"/dev/watchdog0", "/dev/watchdog", NULL};
    const char **path;

    if (fd >= 0) return EXIT_SUCCESS;

    for (path = paths; *path; ++path) {
        if (access(*path, F_OK)) continue;
        fd = open(*path, O_WRONLY);
        if (fd < 0)
            HAL_ERROR("watchdog", "%s could not be opened!\n", *path);
        break;
    }
    if (fd < 0)
        HAL_ERROR("watchdog", "No matching device has been found!\n");

    if (ioctl(fd, WDIOC_SETTIMEOUT, &timeout) < 0) {
        close(fd);
        fd = -1;
        HAL_ERROR("watchdog", "Failed to set watchdog timeout!\n");
    }

    HAL_INFO("watchdog", "Watchdog started with timeout %d s!\n", timeout);
    return EXIT_SUCCESS;
}

void watchdog_stop(void) {
    int options = WDIOS_DISABLECARD;

    if (fd < 0) return;
    if (ioctl(fd, WDIOC_SETOPTIONS, &options) < 0)
        HAL_WARNING("watchdog", "Failed to disable watchdog via ioctl!\n");
    if (write(fd, "V", 1) != 1)
        HAL_WARNING("watchdog", "Failed to disarm watchdog cleanly!\n");
    close(fd);
    fd = -1;

    HAL_INFO("watchdog", "Watchdog stopped!\n");
}
