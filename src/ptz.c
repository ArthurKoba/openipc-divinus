#define _POSIX_C_SOURCE 200809L
#include "ptz.h"

#include <errno.h>
#include <fcntl.h>
#include <ctype.h>
#include <pthread.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

#define PTZ_HELPER_DEFAULT "/usr/sbin/fh8626-ptz"
#define PTZ_PRESETS_DEFAULT "/etc/openipc/ptz.presets"

extern char **environ;

static pthread_mutex_t presets_mutex = PTHREAD_MUTEX_INITIALIZER;

static const char *ptz_helper(void) {
    const char *path = getenv("DIVINUS_PTZ_HELPER");
    return path && *path ? path : PTZ_HELPER_DEFAULT;
}

static const char *ptz_presets_path(void) {
    const char *path = getenv("DIVINUS_PTZ_PRESETS");
    return path && *path ? path : PTZ_PRESETS_DEFAULT;
}

static int wait_result(pid_t pid) {
    int status;

    while (waitpid(pid, &status, 0) < 0) {
        if (errno != EINTR) return -errno;
    }
    if (!WIFEXITED(status)) return -EIO;
    if (WEXITSTATUS(status) == 0) return 0;
    if (WEXITSTATUS(status) == 2) return -EBUSY;
    return -EIO;
}

static int ptz_exec(const char *command, const char *first, const char *second) {
    const char *path = ptz_helper();
    posix_spawn_file_actions_t actions;
    char *arguments[5];
    int action_rc, spawn_rc;
    pid_t pid;

    if (access(path, X_OK)) return -errno;
    arguments[0] = (char *)path;
    arguments[1] = (char *)command;
    arguments[2] = (char *)first;
    arguments[3] = (char *)second;
    arguments[4] = NULL;
    if (!first || !second) arguments[2] = NULL;
    action_rc = posix_spawn_file_actions_init(&actions);
    if (action_rc) return -action_rc;
    action_rc = posix_spawn_file_actions_addopen(&actions, STDOUT_FILENO,
        "/dev/null", O_WRONLY, 0);
    if (!action_rc)
        action_rc = posix_spawn_file_actions_addopen(&actions, STDERR_FILENO,
            "/dev/null", O_WRONLY, 0);
    if (action_rc) {
        posix_spawn_file_actions_destroy(&actions);
        return -action_rc;
    }
    spawn_rc = posix_spawn(&pid, path, &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_rc) return -spawn_rc;
    return wait_result(pid);
}

static int ptz_move(const char *command, int pan, int tilt) {
    char pan_text[24], tilt_text[24];

    snprintf(pan_text, sizeof(pan_text), "%d", pan);
    snprintf(tilt_text, sizeof(tilt_text), "%d", tilt);
    return ptz_exec(command, pan_text, tilt_text);
}

int ptz_status_read(struct ptz_status *status) {
    const char *path = ptz_helper();
    char buffer[512];
    size_t used = 0;
    posix_spawn_file_actions_t actions;
    char *arguments[3];
    int action_rc, spawn_rc;
    int pipefd[2], child_status = 0;
    int waited;
    pid_t pid;

    if (!status) return -EINVAL;
    memset(status, 0, sizeof(*status));
    if (access(path, X_OK)) return -errno;
    if (pipe(pipefd)) return -errno;
    action_rc = posix_spawn_file_actions_init(&actions);
    if (action_rc) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -action_rc;
    }
    action_rc = posix_spawn_file_actions_addclose(&actions, pipefd[0]);
    if (!action_rc)
        action_rc = posix_spawn_file_actions_adddup2(&actions, pipefd[1],
            STDOUT_FILENO);
    if (!action_rc)
        action_rc = posix_spawn_file_actions_addclose(&actions, pipefd[1]);
    if (action_rc) {
        posix_spawn_file_actions_destroy(&actions);
        close(pipefd[0]);
        close(pipefd[1]);
        return -action_rc;
    }
    arguments[0] = (char *)path;
    arguments[1] = "status";
    arguments[2] = NULL;
    spawn_rc = posix_spawn(&pid, path, &actions, NULL, arguments, environ);
    posix_spawn_file_actions_destroy(&actions);
    if (spawn_rc) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -spawn_rc;
    }
    close(pipefd[1]);
    while (used + 1 < sizeof(buffer)) {
        ssize_t count = read(pipefd[0], buffer + used, sizeof(buffer) - used - 1);
        if (count > 0) used += (size_t)count;
        else if (!count) break;
        else if (errno != EINTR) break;
    }
    close(pipefd[0]);
    do {
        waited = waitpid(pid, &child_status, 0);
    } while (waited < 0 && errno == EINTR);
    buffer[used] = '\0';
    if (waited < 0) return -errno;
    if (!WIFEXITED(child_status) ||
        (WEXITSTATUS(child_status) != 0 && WEXITSTATUS(child_status) != 2))
        return -EIO;
    if (sscanf(buffer,
            "busy=%d calibrated=%d pan=%d range=%d,%d,%d tilt=%d range=%d,%d,%d",
            &status->busy, &status->calibrated, &status->pan,
            &status->pan_min, &status->pan_max, &status->pan_home,
            &status->tilt, &status->tilt_min, &status->tilt_max,
            &status->tilt_home) != 10)
        return -EPROTO;
    status->available = 1;
    return 0;
}

int ptz_status_json(char *buffer, size_t size) {
    struct ptz_status status;
    int rc, length;

    if (!buffer || !size) return -EINVAL;
    rc = ptz_status_read(&status);
    if (rc)
        length = snprintf(buffer, size,
            "{\"available\":false,\"error\":%d}", -rc);
    else
        length = snprintf(buffer, size,
            "{\"available\":true,\"busy\":%s,\"calibrated\":%s,"
            "\"pan\":%d,\"pan_range\":[%d,%d],\"pan_home\":%d,"
            "\"tilt\":%d,\"tilt_range\":[%d,%d],\"tilt_home\":%d}",
            status.busy ? "true" : "false",
            status.calibrated ? "true" : "false",
            status.pan, status.pan_min, status.pan_max, status.pan_home,
            status.tilt, status.tilt_min, status.tilt_max, status.tilt_home);
    return length < 0 || (size_t)length >= size ? -EOVERFLOW : length;
}

int ptz_move_relative(int pan, int tilt) {
    return ptz_move("move", pan, tilt);
}

int ptz_move_absolute(int pan, int tilt) {
    return ptz_move("goto", pan, tilt);
}

int ptz_home(void) {
    return ptz_exec("home", NULL, NULL);
}

static int ptz_token_valid(const char *token) {
    size_t length;

    if (!token || !(length = strlen(token)) || length >= sizeof(((struct ptz_preset *)0)->token))
        return 0;
    for (size_t index = 0; index < length; ++index)
        if (!isalnum((unsigned char)token[index]) && token[index] != '_' &&
            token[index] != '-' && token[index] != '.') return 0;
    return 1;
}

static int ptz_presets_read_unlocked(struct ptz_preset *presets, size_t capacity) {
    FILE *file;
    size_t count = 0;

    if (!presets || !capacity) return -EINVAL;
    file = fopen(ptz_presets_path(), "r");
    if (!file) return errno == ENOENT ? 0 : -errno;
    while (count < capacity && fscanf(file, "%31s %d %d",
            presets[count].token, &presets[count].pan, &presets[count].tilt) == 3) {
        if (!ptz_token_valid(presets[count].token)) {
            fclose(file);
            return -EPROTO;
        }
        count++;
    }
    if (ferror(file)) {
        fclose(file);
        return -EIO;
    }
    fclose(file);
    return (int)count;
}

int ptz_presets_read(struct ptz_preset *presets, size_t capacity) {
    int result;

    pthread_mutex_lock(&presets_mutex);
    result = ptz_presets_read_unlocked(presets, capacity);
    pthread_mutex_unlock(&presets_mutex);
    return result;
}

int ptz_presets_json(char *buffer, size_t size) {
    struct ptz_preset presets[PTZ_MAX_PRESETS];
    size_t used = 0;
    int count, length;

    if (!buffer || !size) return -EINVAL;
    count = ptz_presets_read(presets, PTZ_MAX_PRESETS);
    if (count < 0) return count;
    length = snprintf(buffer, size, "{\"presets\":[");
    if (length < 0 || (size_t)length >= size) return -EOVERFLOW;
    used = (size_t)length;
    for (int index = 0; index < count; ++index) {
        length = snprintf(buffer + used, size - used,
            "%s{\"token\":\"%s\",\"pan\":%d,\"tilt\":%d}",
            index ? "," : "", presets[index].token,
            presets[index].pan, presets[index].tilt);
        if (length < 0 || (size_t)length >= size - used) return -EOVERFLOW;
        used += (size_t)length;
    }
    length = snprintf(buffer + used, size - used, "]}");
    if (length < 0 || (size_t)length >= size - used) return -EOVERFLOW;
    return (int)(used + (size_t)length);
}

static int ptz_presets_write(const struct ptz_preset *presets, size_t count) {
    const char *path = ptz_presets_path();
    char temporary[320];
    FILE *file;

    if (snprintf(temporary, sizeof(temporary), "%s.new", path) >= (int)sizeof(temporary))
        return -ENAMETOOLONG;
    file = fopen(temporary, "w");
    if (!file) return -errno;
    for (size_t index = 0; index < count; ++index)
        if (fprintf(file, "%s %d %d\n", presets[index].token,
                presets[index].pan, presets[index].tilt) < 0) {
            fclose(file);
            unlink(temporary);
            return -EIO;
        }
    int error = 0;
    if (fflush(file)) error = errno ? -errno : -EIO;
    if (!error && fsync(fileno(file))) error = -errno;
    if (fclose(file) && !error) error = errno ? -errno : -EIO;
    if (error) {
        unlink(temporary);
        return error;
    }
    if (rename(temporary, path)) {
        int error = -errno;
        unlink(temporary);
        return error;
    }
    return 0;
}

int ptz_preset_set(const char *requested_token, char *token, size_t token_size) {
    struct ptz_preset presets[PTZ_MAX_PRESETS];
    struct ptz_status status;
    char generated[32];
    const char *selected = requested_token;
    int count, slot = -1, rc;

    if (!token || !token_size) return -EINVAL;
    if (selected && *selected && !ptz_token_valid(selected)) return -EINVAL;
    pthread_mutex_lock(&presets_mutex);
    count = ptz_presets_read_unlocked(presets, PTZ_MAX_PRESETS);
    if (count < 0) goto out;
    if (!selected || !*selected) {
        for (int number = 1; number <= PTZ_MAX_PRESETS; ++number) {
            int used = 0;
            snprintf(generated, sizeof(generated), "preset%d", number);
            for (int index = 0; index < count; ++index)
                if (!strcmp(generated, presets[index].token)) used = 1;
            if (!used) { selected = generated; break; }
        }
        if (!selected || !*selected) { count = -ENOSPC; goto out; }
    }
    for (int index = 0; index < count; ++index)
        if (!strcmp(selected, presets[index].token)) slot = index;
    if (slot < 0) {
        if (count >= PTZ_MAX_PRESETS) { count = -ENOSPC; goto out; }
        slot = count++;
    }
    rc = ptz_status_read(&status);
    if (rc) { count = rc; goto out; }
    if (status.busy || !status.calibrated) { count = -EBUSY; goto out; }
    snprintf(presets[slot].token, sizeof(presets[slot].token), "%s", selected);
    presets[slot].pan = status.pan;
    presets[slot].tilt = status.tilt;
    rc = ptz_presets_write(presets, (size_t)count);
    if (rc) { count = rc; goto out; }
    if (snprintf(token, token_size, "%s", selected) >= (int)token_size)
        count = -EOVERFLOW;
    else count = 0;
out:
    pthread_mutex_unlock(&presets_mutex);
    return count;
}

int ptz_preset_goto(const char *token) {
    struct ptz_preset presets[PTZ_MAX_PRESETS];
    int count;

    if (!ptz_token_valid(token)) return -EINVAL;
    pthread_mutex_lock(&presets_mutex);
    count = ptz_presets_read_unlocked(presets, PTZ_MAX_PRESETS);
    if (count < 0) {
        pthread_mutex_unlock(&presets_mutex);
        return count;
    }
    for (int index = 0; index < count; ++index)
        if (!strcmp(token, presets[index].token)) {
            int pan = presets[index].pan, tilt = presets[index].tilt;
            pthread_mutex_unlock(&presets_mutex);
            return ptz_move_absolute(pan, tilt);
        }
    pthread_mutex_unlock(&presets_mutex);
    return -ENOENT;
}

int ptz_preset_remove(const char *token) {
    struct ptz_preset presets[PTZ_MAX_PRESETS];
    int count, slot = -1;

    if (!ptz_token_valid(token)) return -EINVAL;
    pthread_mutex_lock(&presets_mutex);
    count = ptz_presets_read_unlocked(presets, PTZ_MAX_PRESETS);
    if (count < 0) goto out;
    for (int index = 0; index < count; ++index)
        if (!strcmp(token, presets[index].token)) slot = index;
    if (slot < 0) { count = -ENOENT; goto out; }
    for (int index = slot; index + 1 < count; ++index)
        presets[index] = presets[index + 1];
    count = ptz_presets_write(presets, (size_t)(count - 1));
out:
    pthread_mutex_unlock(&presets_mutex);
    return count;
}
