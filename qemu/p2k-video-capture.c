/*
 * Optional FFmpeg video capture for the Pinball 2000 framebuffer.
 *
 * p2k-display.c remains the sole owner of framebuffer extraction.  This
 * module owns only the encoder process and its producer thread.  Packed
 * RGB555 frames travel through an anonymous pipe into FFmpeg; the only file
 * written is the final container selected from the output extension.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
#include "system/runstate.h"
#include "system/system.h"

#include <poll.h>
#include <signal.h>
#include <sys/wait.h>

#include "p2k-internal.h"

#define P2K_VIDEO_WIDTH       640
#define P2K_VIDEO_HEIGHT      240
#define P2K_VIDEO_FPS         60
#define P2K_VIDEO_PERIOD_US   (G_USEC_PER_SEC / P2K_VIDEO_FPS)
#define P2K_VIDEO_EXIT_MS     5000

typedef struct P2KVideoCapture {
    QemuThread worker;
    Notifier shutdown_notifier;
    Notifier exit_notifier;
    char *path;
    int input_fd;
    GPid encoder_pid;
    bool worker_run;
    bool worker_started;
    bool shutdown_complete;
    uint64_t frames;
} P2KVideoCapture;

static P2KVideoCapture s_video = {
    .input_fd = -1,
};

static bool p2k_video_uses_mov_container(const char *path)
{
    const char *extension = strrchr(path, '.');

    return extension &&
        (!g_ascii_strcasecmp(extension, ".mp4") ||
         !g_ascii_strcasecmp(extension, ".mov") ||
         !g_ascii_strcasecmp(extension, ".m4v") ||
         !g_ascii_strcasecmp(extension, ".3gp") ||
         !g_ascii_strcasecmp(extension, ".3g2") ||
         !g_ascii_strcasecmp(extension, ".mj2"));
}

static bool p2k_video_write_frame(const void *buffer, size_t size)
{
    const uint8_t *cursor = buffer;

    while (size && qatomic_read(&s_video.worker_run)) {
        ssize_t written = write(s_video.input_fd, cursor, size);

        if (written > 0) {
            cursor += written;
            size -= written;
            continue;
        }
        if (written < 0 && errno == EINTR) {
            continue;
        }
        if (written < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            struct pollfd poll_fd = {
                .fd = s_video.input_fd,
                .events = POLLOUT,
            };
            int ready = poll(&poll_fd, 1, 100);

            if (ready >= 0) {
                continue;
            }
            if (errno == EINTR) {
                continue;
            }
        }

        warn_report("pinball2000: video encoder pipe failed after %llu "
                    "frames (%s)",
                    (unsigned long long)s_video.frames,
                    strerror(errno));
        return false;
    }
    return size == 0;
}

static void *p2k_video_worker(void *opaque)
{
    (void)opaque;

    uint16_t *frame = g_new(uint16_t,
                            P2K_VIDEO_WIDTH * P2K_VIDEO_HEIGHT);
    int64_t next_frame_us = g_get_monotonic_time();

    while (qatomic_read(&s_video.worker_run)) {
        if (p2k_display_copy_rgb555_frame(
                frame, P2K_VIDEO_WIDTH * P2K_VIDEO_HEIGHT)) {
            if (!p2k_video_write_frame(
                    frame, P2K_VIDEO_WIDTH * P2K_VIDEO_HEIGHT *
                           sizeof(*frame))) {
                qatomic_set(&s_video.worker_run, false);
                break;
            }
            s_video.frames++;
        }

        next_frame_us += P2K_VIDEO_PERIOD_US;
        int64_t now_us = g_get_monotonic_time();
        if (next_frame_us > now_us) {
            g_usleep(next_frame_us - now_us);
        } else if (now_us - next_frame_us > 4 * P2K_VIDEO_PERIOD_US) {
            next_frame_us = now_us;
        }
    }

    g_free(frame);
    close(s_video.input_fd);
    s_video.input_fd = -1;
    return NULL;
}

static int p2k_video_wait_encoder(GPid pid)
{
    int status = 0;

    for (int waited_ms = 0; waited_ms < P2K_VIDEO_EXIT_MS;
         waited_ms += 50) {
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid) {
            return status;
        }
        if (result < 0 && errno != EINTR) {
            return -1;
        }
        g_usleep(50 * 1000);
    }

    warn_report("pinball2000: video encoder did not finish within %d ms; "
                "terminating it", P2K_VIDEO_EXIT_MS);
    kill(pid, SIGTERM);
    for (int waited_ms = 0; waited_ms < 1000; waited_ms += 50) {
        pid_t result = waitpid(pid, &status, WNOHANG);

        if (result == pid) {
            return status;
        }
        if (result < 0 && errno != EINTR) {
            return -1;
        }
        g_usleep(50 * 1000);
    }
    kill(pid, SIGKILL);
    return waitpid(pid, &status, 0) == pid ? status : -1;
}

static void p2k_video_shutdown(Notifier *notifier, void *data)
{
    int status;

    (void)notifier;
    (void)data;

    if (s_video.shutdown_complete) {
        return;
    }
    s_video.shutdown_complete = true;

    /*
     * Shutdown notifiers run newest-first. Capture is installed after
     * display, so stop presentation explicitly before waiting for FFmpeg.
     * The exit notifier calls this again as an idempotent fallback.
     */
    p2k_display_stop_presentation();

    qatomic_set(&s_video.worker_run, false);
    if (s_video.worker_started) {
        qemu_thread_join(&s_video.worker);
        s_video.worker_started = false;
    } else if (s_video.input_fd >= 0) {
        close(s_video.input_fd);
        s_video.input_fd = -1;
    }

    status = p2k_video_wait_encoder(s_video.encoder_pid);
    g_spawn_close_pid(s_video.encoder_pid);
    s_video.encoder_pid = 0;

    if (status >= 0 && WIFEXITED(status) && WEXITSTATUS(status) == 0 &&
        s_video.frames) {
        info_report("pinball2000: video capture finalized at %s "
                    "(%llu frames, %.1f seconds)",
                    s_video.path, (unsigned long long)s_video.frames,
                    (double)s_video.frames / P2K_VIDEO_FPS);
    } else {
        warn_report("pinball2000: video capture did not finalize cleanly "
                    "(path=%s, frames=%llu, status=%d)",
                    s_video.path, (unsigned long long)s_video.frames, status);
    }
    g_clear_pointer(&s_video.path, g_free);
}

void p2k_install_video_capture(void)
{
    const char *path = getenv("P2K_VIDEO_CAPTURE");
    const char *ffmpeg = getenv("P2K_FFMPEG_BIN");
    const char *argv[32];
    GError *error = NULL;
    size_t argc = 0;
    int flags;

    if (!path || !*path) {
        return;
    }
    if (!ffmpeg || !*ffmpeg) {
        ffmpeg = "ffmpeg";
    }

    argv[argc++] = ffmpeg;
    argv[argc++] = "-nostdin";
    argv[argc++] = "-hide_banner";
    argv[argc++] = "-loglevel";
    argv[argc++] = "warning";
    argv[argc++] = "-f";
    argv[argc++] = "rawvideo";
    argv[argc++] = "-pixel_format";
    argv[argc++] = "rgb555le";
    argv[argc++] = "-video_size";
    argv[argc++] = "640x240";
    argv[argc++] = "-framerate";
    argv[argc++] = "60";
    argv[argc++] = "-i";
    argv[argc++] = "pipe:0";
    argv[argc++] = "-an";
    argv[argc++] = "-vf";
    argv[argc++] = "scale=640:480:flags=neighbor";
    if (p2k_video_uses_mov_container(path)) {
        argv[argc++] = "-movflags";
        argv[argc++] = "+faststart";
    }
    argv[argc++] = "-n";
    argv[argc++] = path;
    argv[argc] = NULL;

    if (!g_spawn_async_with_pipes(
            NULL, (char **)argv, NULL,
            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH |
            G_SPAWN_STDOUT_TO_DEV_NULL,
            NULL, NULL, &s_video.encoder_pid, &s_video.input_fd,
            NULL, NULL, &error)) {
        error_report("pinball2000: cannot start video encoder '%s' (%s)",
                     ffmpeg, error ? error->message : "unknown error");
        g_clear_error(&error);
        return;
    }

    flags = fcntl(s_video.input_fd, F_GETFL);
    if (flags >= 0) {
        fcntl(s_video.input_fd, F_SETFL, flags | O_NONBLOCK);
    }

    s_video.path = g_strdup(path);
    s_video.worker_run = true;
    s_video.worker_started = true;
    s_video.shutdown_notifier.notify = p2k_video_shutdown;
    qemu_register_shutdown_notifier(&s_video.shutdown_notifier);
    s_video.exit_notifier.notify = p2k_video_shutdown;
    qemu_add_exit_notifier(&s_video.exit_notifier);
    qemu_thread_create(&s_video.worker, "p2k-video-capture",
                       p2k_video_worker, NULL, QEMU_THREAD_JOINABLE);

    info_report("pinball2000: recording video to %s "
                "(640x480, 60 fps, FFmpeg-selected format, no raw file)",
                path);
}
