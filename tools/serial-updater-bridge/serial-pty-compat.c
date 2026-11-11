#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE

#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdbool.h>
#include <stdarg.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

/*
 * Compatibility shim for legacy GUI software which discovers serial ports
 * through /proc/tty/drivers and applies TIOCEXCL to the selected device.
 *
 * The bridge exposes one harmless alias.  Opens of that alias are redirected
 * to a private PTY.  TIOCEXCL is acknowledged only for that PTY because the
 * bridge process must keep the PTY master open; applying the kernel lock would
 * otherwise make every later reconnect fail with EBUSY.
 */

typedef FILE *(*fopen_fn)(const char *, const char *);
typedef int (*open_fn)(const char *, int, ...);
typedef int (*openat_fn)(int, const char *, int, ...);
typedef int (*ioctl_fn)(int, unsigned long, ...);
typedef ssize_t (*write_fn)(int, const void *, size_t);
typedef ssize_t (*read_fn)(int, void *, size_t);

#define UPDATE_COMMAND_CAPACITY 128

static unsigned char pending_command[UPDATE_COMMAND_CAPACITY];
static size_t pending_command_size;
static atomic_bool command_pending;

static const char *configured(const char *name)
{
    const char *value = getenv(name);

    return value && value[0] ? value : NULL;
}

static const char *redirect_path(const char *path)
{
    const char *drivers = configured("ENCORE_SERIAL_DRIVERS_FILE");
    const char *alias = configured("ENCORE_SERIAL_UPDATER_ALIAS");
    const char *device = configured("ENCORE_SERIAL_UPDATER_DEVICE");

    if (path && drivers && strcmp(path, "/proc/tty/drivers") == 0) {
        return drivers;
    }
    if (!alias) {
        alias = "/dev/full";
    }
    if (path && device && strcmp(path, alias) == 0) {
        return device;
    }
    return path;
}

static int mode_argument_required(int flags)
{
    if (flags & O_CREAT) {
        return 1;
    }
#ifdef O_TMPFILE
    if ((flags & O_TMPFILE) == O_TMPFILE) {
        return 1;
    }
#endif
    return 0;
}

static FILE *redirected_fopen(const char *path, const char *mode,
                              const char *symbol)
{
    fopen_fn real_fopen = (fopen_fn)dlsym(RTLD_NEXT, symbol);

    if (!real_fopen) {
        errno = ENOSYS;
        return NULL;
    }
    return real_fopen(redirect_path(path), mode);
}

FILE *fopen(const char *path, const char *mode)
{
    return redirected_fopen(path, mode, "fopen");
}

FILE *fopen64(const char *path, const char *mode)
{
    return redirected_fopen(path, mode, "fopen64");
}

static int redirected_open(const char *path, int flags, mode_t mode,
                           int has_mode, const char *symbol)
{
    open_fn real_open = (open_fn)dlsym(RTLD_NEXT, symbol);
    int result;

    if (!real_open) {
        errno = ENOSYS;
        return -1;
    }
    path = redirect_path(path);
    result = has_mode ? real_open(path, flags, mode) : real_open(path, flags);
    return result;
}

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int has_mode = mode_argument_required(flags);

    if (has_mode) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, mode_t);
        va_end(arguments);
    }
    return redirected_open(path, flags, mode, has_mode, "open");
}

int open64(const char *path, int flags, ...)
{
    mode_t mode = 0;
    int has_mode = mode_argument_required(flags);

    if (has_mode) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, mode_t);
        va_end(arguments);
    }
    return redirected_open(path, flags, mode, has_mode, "open64");
}

static int redirected_openat(int dirfd, const char *path, int flags,
                             mode_t mode, int has_mode, const char *symbol)
{
    openat_fn real_openat = (openat_fn)dlsym(RTLD_NEXT, symbol);
    int result;

    if (!real_openat) {
        errno = ENOSYS;
        return -1;
    }
    path = redirect_path(path);
    result = has_mode ? real_openat(dirfd, path, flags, mode)
                      : real_openat(dirfd, path, flags);
    return result;
}

int openat(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    int has_mode = mode_argument_required(flags);

    if (has_mode) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, mode_t);
        va_end(arguments);
    }
    return redirected_openat(dirfd, path, flags, mode, has_mode, "openat");
}

int openat64(int dirfd, const char *path, int flags, ...)
{
    mode_t mode = 0;
    int has_mode = mode_argument_required(flags);

    if (has_mode) {
        va_list arguments;
        va_start(arguments, flags);
        mode = va_arg(arguments, mode_t);
        va_end(arguments);
    }
    return redirected_openat(dirfd, path, flags, mode, has_mode, "openat64");
}

static int fd_is_updater_pty(int fd)
{
    const char *device = configured("ENCORE_SERIAL_UPDATER_DEVICE");
    struct stat fd_stat;
    struct stat device_stat;

    if (!device || fstat(fd, &fd_stat) != 0 || stat(device, &device_stat) != 0) {
        return 0;
    }
    return S_ISCHR(fd_stat.st_mode) && S_ISCHR(device_stat.st_mode) &&
           fd_stat.st_rdev == device_stat.st_rdev;
}

static int is_lf_terminated_update_command(const void *buffer, size_t count)
{
    static const char prefix[] = "fupdate load com1 ";
    const unsigned char *bytes = buffer;

    return configured("ENCORE_SERIAL_UPDATER_CRLF") && buffer &&
           count > sizeof(prefix) &&
           memcmp(bytes, prefix, sizeof(prefix) - 1) == 0 &&
           bytes[count - 1] == '\n' && bytes[count - 2] != '\r';
}

static int write_complete(write_fn real_write, int fd,
                          const unsigned char *buffer, size_t count)
{
    size_t offset = 0;

    while (offset < count) {
        ssize_t result = real_write(fd, buffer + offset, count - offset);

        if (result > 0) {
            offset += (size_t)result;
            continue;
        }
        if (result < 0 && errno == EINTR) {
            continue;
        }
        return -1;
    }
    return 0;
}

static void drain_command_echo(int fd, read_fn real_read)
{
    static const unsigned char prefix[] = "fupdate load com1 ";
    unsigned char observed[512];
    size_t used = 0;

    while (used < sizeof(observed)) {
        struct pollfd descriptor = {
            .fd = fd,
            .events = POLLIN,
        };
        int ready;

        do {
            ready = poll(&descriptor, 1, 250);
        } while (ready < 0 && errno == EINTR);
        if (ready <= 0 || !(descriptor.revents & (POLLIN | POLLHUP))) {
            return;
        }

        ssize_t count = real_read(fd, observed + used,
                                  sizeof(observed) - used);
        if (count < 0 && (errno == EINTR || errno == EAGAIN)) {
            continue;
        }
        if (count <= 0) {
            return;
        }
        used += (size_t)count;

        unsigned char *start = memmem(observed, used, prefix,
                                      sizeof(prefix) - 1);
        if (start && memchr(start, '\n',
                           used - (size_t)(start - observed))) {
            return;
        }
    }
}

static int deliver_pending_command(write_fn real_write, int fd)
{
    read_fn real_read;
    size_t count;
    struct timespec settle = {
        .tv_nsec = 50 * 1000 * 1000,
    };

    if (!atomic_exchange_explicit(&command_pending, false,
                                  memory_order_acq_rel)) {
        return 0;
    }
    count = pending_command_size;
    tcflush(fd, TCIFLUSH);
    if (write_complete(real_write, fd, pending_command, count) != 0) {
        return -1;
    }
    real_read = (read_fn)dlsym(RTLD_NEXT, "read");
    if (real_read) {
        drain_command_echo(fd, real_read);
    }
    while (nanosleep(&settle, &settle) != 0 && errno == EINTR) {
    }
    return 0;
}

/*
 * The legacy updater emits its one XINA shell command with a Unix LF.  A
 * physical tty's output processing supplied CRLF.  Hold only that command
 * across the application's slow close/reopen handoff.  The first binary write
 * delivers it, consumes its echo, then proceeds byte-transparent.
 */
ssize_t write(int fd, const void *buffer, size_t count)
{
    write_fn real_write = (write_fn)dlsym(RTLD_NEXT, "write");
    unsigned char translated[UPDATE_COMMAND_CAPACITY];

    if (!real_write) {
        errno = ENOSYS;
        return -1;
    }
    if (!fd_is_updater_pty(fd) ||
        !is_lf_terminated_update_command(buffer, count)) {
        if (fd_is_updater_pty(fd) &&
            deliver_pending_command(real_write, fd) != 0) {
            return -1;
        }
        return real_write(fd, buffer, count);
    }

    if (count + 1 > sizeof(translated)) {
        errno = E2BIG;
        return -1;
    }
    memcpy(translated, buffer, count - 1);
    translated[count - 1] = '\r';
    translated[count] = '\n';
    memcpy(pending_command, translated, count + 1);
    pending_command_size = count + 1;
    atomic_store_explicit(&command_pending, true, memory_order_release);
    return (ssize_t)count;
}

int ioctl(int fd, unsigned long request, ...)
{
    ioctl_fn real_ioctl = (ioctl_fn)dlsym(RTLD_NEXT, "ioctl");
    void *argument;
    va_list arguments;

    if (!real_ioctl) {
        errno = ENOSYS;
        return -1;
    }
    if (request == TIOCEXCL) {
        if (fd_is_updater_pty(fd)) {
            return 0;
        }
        return real_ioctl(fd, request);
    }
#ifdef TIOCNXCL
    if (request == TIOCNXCL) {
        return real_ioctl(fd, request);
    }
#endif
    va_start(arguments, request);
    argument = va_arg(arguments, void *);
    va_end(arguments);
    return real_ioctl(fd, request, argument);
}
