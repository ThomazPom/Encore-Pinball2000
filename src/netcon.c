/*
 * netcon.c — Network console bridges.
 *
 * Two independent TCP listeners, both single-client (a new connection
 * supersedes any previous one):
 *
 *   --serial-tcp PORT    Bidirectional bridge to emulated COM1 UART.
 *                        Bytes the guest writes to the UART THR are sent
 *                        verbatim to the connected client; bytes received
 *                        from the client are pushed into the UART RX path
 *                        (RBR / LSR.DR / IIR.RDA / IRQ4).
 *
 *                        This is exactly what real Pinball 2000 techs did
 *                        with HyperTerminal at 9600 8N1 (per the gerwiki
 *                        XINA documentation): every XINA shell prompt,
 *                        pinevents log line and crash dump comes out here.
 *
 *   --keyboard-tcp PORT  TCP→PS/2 KBC injection (experimental).
 *                        Each ASCII byte is translated to an IBM PC Set 1
 *                        scancode pair (make + break) and queued. The KBC
 *                        read path pulls one code per port-0x60 read and
 *                        raises IRQ1 while bytes are pending.
 *
 * No external deps — POSIX sockets only. Both listeners are non-blocking;
 * netcon_poll() is called from the main exec loop's frame tick (~60Hz).
 */
#include "encore.h"
#include <sys/socket.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <arpa/inet.h>
#include <poll.h>

/* ===== generic single-client listener ===== */
typedef struct {
    int      listen_fd;     /* -1 = disabled */
    int      client_fd;     /* -1 = no client */
    uint16_t port;
    /* RX ring: bytes received from client, awaiting consumption by guest. */
    uint8_t  rx[4096];
    int      rx_head;       /* next byte to read */
    int      rx_tail;       /* next slot to write */
} NetconListener;

#define RX_SIZE (int)(sizeof(((NetconListener *)0)->rx))

static NetconListener s_serial = { -1, -1, 0, {0}, 0, 0 };
static NetconListener s_kbd    = { -1, -1, 0, {0}, 0, 0 };

/* PS/2 Set 1 scancode queue (make/break codes, one byte per slot). */
static uint8_t  s_scan_queue[256];
static int      s_scan_head = 0;
static int      s_scan_tail = 0;

/* ----- ring helpers ----- */
static int  rx_count(NetconListener *l) {
    int n = l->rx_tail - l->rx_head;
    return n < 0 ? n + RX_SIZE : n;
}
static bool rx_pop(NetconListener *l, uint8_t *out) {
    if (l->rx_head == l->rx_tail) return false;
    *out = l->rx[l->rx_head];
    l->rx_head = (l->rx_head + 1) % RX_SIZE;
    return true;
}
static void rx_push(NetconListener *l, uint8_t b) {
    int next = (l->rx_tail + 1) % RX_SIZE;
    if (next == l->rx_head) return;  /* drop on overflow */
    l->rx[l->rx_tail] = b;
    l->rx_tail = next;
}

/* ----- listener bring-up ----- */
static void listener_open(NetconListener *l, uint16_t port, const char *label)
{
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) {
        LOG("netcon", "%s: socket() failed: %s\n", label, strerror(errno));
        return;
    }
    int one = 1;
    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));

    struct sockaddr_in sa;
    memset(&sa, 0, sizeof(sa));
    sa.sin_family = AF_INET;
    sa.sin_addr.s_addr = htonl(INADDR_LOOPBACK);  /* localhost only — safer */
    sa.sin_port = htons(port);
    if (bind(fd, (struct sockaddr *)&sa, sizeof(sa)) < 0) {
        LOG("netcon", "%s: bind(:%u) failed: %s\n", label, port, strerror(errno));
        close(fd);
        return;
    }
    if (listen(fd, 1) < 0) {
        LOG("netcon", "%s: listen failed: %s\n", label, strerror(errno));
        close(fd);
        return;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    fcntl(fd, F_SETFL, flags | O_NONBLOCK);

    l->listen_fd = fd;
    l->port      = port;
    LOG("netcon", "%s listening on 127.0.0.1:%u (try: nc localhost %u)\n",
        label, port, port);
}

void netcon_init(void)
{
    if (g_emu.serial_tcp_port > 0)
        listener_open(&s_serial, (uint16_t)g_emu.serial_tcp_port, "serial-tcp");
    if (g_emu.keyboard_tcp_port > 0)
        listener_open(&s_kbd, (uint16_t)g_emu.keyboard_tcp_port, "keyboard-tcp");
}

/* ----- per-listener poll: accept + drain ----- */
static void listener_poll(NetconListener *l, const char *label)
{
    if (l->listen_fd < 0) return;

    /* Try to accept a new client (replaces any previous). */
    struct sockaddr_in sa;
    socklen_t slen = sizeof(sa);
    int newfd = accept(l->listen_fd, (struct sockaddr *)&sa, &slen);
    if (newfd >= 0) {
        if (l->client_fd >= 0) {
            close(l->client_fd);
            LOG("netcon", "%s: previous client replaced\n", label);
        }
        int flags = fcntl(newfd, F_GETFL, 0);
        fcntl(newfd, F_SETFL, flags | O_NONBLOCK);
        int one = 1;
        setsockopt(newfd, IPPROTO_TCP, TCP_NODELAY, &one, sizeof(one));
        l->client_fd = newfd;
        LOG("netcon", "%s: client connected from %s:%u\n",
            label, inet_ntoa(sa.sin_addr), ntohs(sa.sin_port));
    }

    /* Drain client RX into ring. */
    if (l->client_fd >= 0) {
        uint8_t buf[256];
        ssize_t n;
        while ((n = recv(l->client_fd, buf, sizeof(buf), 0)) > 0) {
            for (ssize_t i = 0; i < n; i++) rx_push(l, buf[i]);
        }
        if (n == 0) {
            LOG("netcon", "%s: client disconnected\n", label);
            close(l->client_fd);
            l->client_fd = -1;
        } else if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
            LOG("netcon", "%s: recv error: %s\n", label, strerror(errno));
            close(l->client_fd);
            l->client_fd = -1;
        }
    }
}

/* ----- ASCII → PS/2 Set 1 scancode (minimal coverage) -----
 * Returns base scancode (0 if no mapping) and sets *shift to true if the
 * character requires Shift. */
static uint8_t ascii_to_set1(uint8_t c, bool *shift)
{
    *shift = false;
    static const uint8_t low[] = {
        /* 0x00..0x1F: control chars — only a few are useful */
        [0x08] = 0x0E, /* BS  */
        [0x09] = 0x0F, /* TAB */
        [0x0A] = 0x1C, /* LF  → ENTER */
        [0x0D] = 0x1C, /* CR  → ENTER */
        [0x1B] = 0x01, /* ESC */
    };
    if (c < sizeof(low) && low[c]) return low[c];
    if (c == ' ') return 0x39;
    if (c >= '1' && c <= '9') return 0x02 + (c - '1');
    if (c == '0') return 0x0B;
    if (c >= 'a' && c <= 'z') {
        static const uint8_t letters[] = {
            0x1E, 0x30, 0x2E, 0x20, 0x12, 0x21, 0x22, 0x23, 0x17, 0x24, /* a-j */
            0x25, 0x26, 0x32, 0x31, 0x18, 0x19, 0x10, 0x13, 0x1F, 0x14, /* k-t */
            0x16, 0x2F, 0x11, 0x2D, 0x15, 0x2C                           /* u-z */
        };
        return letters[c - 'a'];
    }
    if (c >= 'A' && c <= 'Z') {
        *shift = true;
        return ascii_to_set1((uint8_t)(c + 0x20), &(bool){false});
    }
    /* Common punctuation */
    switch (c) {
        case '-': return 0x0C;
        case '=': return 0x0D;
        case '[': return 0x1A;
        case ']': return 0x1B;
        case ';': return 0x27;
        case '\'': return 0x28;
        case '`': return 0x29;
        case '\\': return 0x2B;
        case ',': return 0x33;
        case '.': return 0x34;
        case '/': return 0x35;
        case '?': *shift = true; return 0x35;
        case '!': *shift = true; return 0x02;
        case '@': *shift = true; return 0x03;
        case '*': *shift = true; return 0x09;
        case '(': *shift = true; return 0x0A;
        case ')': *shift = true; return 0x0B;
        case '_': *shift = true; return 0x0C;
        case '+': *shift = true; return 0x0D;
        case ':': *shift = true; return 0x27;
        case '"': *shift = true; return 0x28;
    }
    return 0;
}

static void scan_push(uint8_t code)
{
    int next = (s_scan_tail + 1) % (int)sizeof(s_scan_queue);
    if (next == s_scan_head) return;
    s_scan_queue[s_scan_tail] = code;
    s_scan_tail = next;
}

static void enqueue_keyboard_byte(uint8_t b)
{
    bool shift;
    uint8_t code = ascii_to_set1(b, &shift);
    if (!code) return;
    if (shift) scan_push(0x2A);          /* shift make */
    scan_push(code);                      /* key make */
    scan_push((uint8_t)(code | 0x80));    /* key break */
    if (shift) scan_push((uint8_t)0xAA);  /* shift break */
}

void netcon_poll(void)
{
    listener_poll(&s_serial, "serial-tcp");
    listener_poll(&s_kbd,    "keyboard-tcp");

    /* Promote keyboard RX bytes into the scancode queue. */
    uint8_t b;
    while (rx_pop(&s_kbd, &b)) enqueue_keyboard_byte(b);

    /* IRQ1 management: raise while any scancode awaits CPU pickup;
     * the KBC read path clears OBF naturally and we re-evaluate next tick. */
    if (s_scan_head != s_scan_tail)
        g_emu.pic[0].irr |= 0x02;       /* IRQ1 */
    else
        g_emu.pic[0].irr &= ~0x02;
}

/* ===== UART side ===== */
void netcon_serial_tx(uint8_t b)
{
    if (s_serial.client_fd < 0) return;
    /* Best-effort: drop on EAGAIN rather than block the CPU loop. */
    ssize_t n = send(s_serial.client_fd, &b, 1, MSG_NOSIGNAL);
    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        close(s_serial.client_fd);
        s_serial.client_fd = -1;
    }
}

bool netcon_serial_rx(uint8_t *out)        { return rx_pop(&s_serial, out); }
bool netcon_serial_rx_pending(void)        { return rx_count(&s_serial) > 0; }

/* ===== KBC side ===== */
bool netcon_keyboard_rx(uint8_t *out)
{
    if (s_scan_head == s_scan_tail) return false;
    *out = s_scan_queue[s_scan_head];
    s_scan_head = (s_scan_head + 1) % (int)sizeof(s_scan_queue);
    return true;
}

bool netcon_keyboard_pending(void)         { return s_scan_head != s_scan_tail; }

void netcon_cleanup(void)
{
    if (s_serial.client_fd >= 0) close(s_serial.client_fd);
    if (s_serial.listen_fd >= 0) close(s_serial.listen_fd);
    if (s_kbd.client_fd    >= 0) close(s_kbd.client_fd);
    if (s_kbd.listen_fd    >= 0) close(s_kbd.listen_fd);
}
