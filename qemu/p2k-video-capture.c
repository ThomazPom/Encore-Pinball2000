/*
 * Optional H.264 video capture for the Pinball 2000 framebuffer.
 *
 * p2k-display.c remains the sole owner of framebuffer extraction.  This
 * module owns only the encoder process and its producer thread.  Packed
 * RGB555 frames travel through an anonymous pipe into FFmpeg; the only file
 * written is the final H.264 MP4.
 */

#include "qemu/osdep.h"
#include "qemu/error-report.h"
#include "qemu/notify.h"
#include "qemu/thread.h"
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
    Notifier exit_notifier;
    char *path;
    int input_fd;
    GPid encoder_pid;
    bool worker_run;
    bool worker_started;
    uint64_t frames;
} P2KVideoCapture;

static P2KVideoCapture s_video = {
    .input_fd = -1,
};

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

        warn_report("pinball2000: H.264 encoder pipe failed after %llu "
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

    warn_report("pinball2000: H.264 encoder did not finish within %d ms; "
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
        info_report("pinball2000: H.264 capture finalized at %s "
                    "(%llu frames, %.1f seconds)",
                    s_video.path, (unsigned long long)s_video.frames,
                    (double)s_video.frames / P2K_VIDEO_FPS);
    } else {
        warn_report("pinball2000: H.264 capture did not finalize cleanly "
                    "(path=%s, frames=%llu, status=%d)",
                    s_video.path, (unsigned long long)s_video.frames, status);
    }
    g_clear_pointer(&s_video.path, g_free);
}

void p2k_install_video_capture(void)
{
    const char *path = getenv("P2K_VIDEO_CAPTURE");
    const char *ffmpeg = getenv("P2K_FFMPEG_BIN");
    GError *error = NULL;
    int flags;

    if (!path || !*path) {
        return;
    }
    if (!ffmpeg || !*ffmpeg) {
        ffmpeg = "ffmpeg";
    }

    const char *argv[] = {
        ffmpeg,
        "-nostdin",
        "-hide_banner",
        "-loglevel", "warning",
        "-f", "rawvideo",
        "-pixel_format", "rgb555le",
        "-video_size", "640x240",
        "-framerate", "60",
        "-i", "pipe:0",
        "-an",
        "-vf", "scale=640:480:flags=neighbor,format=yuv420p",
        "-c:v", "libx264",
        "-preset", "veryfast",
        "-crf", "20",
        "-threads", "2",
        "-movflags", "+faststart",
        "-f", "mp4",
        "-n",
        path,
        NULL,
    };

    if (!g_spawn_async_with_pipes(
            NULL, (char **)argv, NULL,
            G_SPAWN_DO_NOT_REAP_CHILD | G_SPAWN_SEARCH_PATH |
            G_SPAWN_STDOUT_TO_DEV_NULL,
            NULL, NULL, &s_video.encoder_pid, &s_video.input_fd,
            NULL, NULL, &error)) {
        error_report("pinball2000: cannot start H.264 encoder '%s' (%s)",
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
    s_video.exit_notifier.notify = p2k_video_shutdown;
    qemu_add_exit_notifier(&s_video.exit_notifier);
    qemu_thread_create(&s_video.worker, "p2k-h264-capture",
                       p2k_video_worker, NULL, QEMU_THREAD_JOINABLE);

    info_report("pinball2000: recording H.264 video to %s "
                "(640x480, 60 fps, CRF 20, no raw file)", path);
}
