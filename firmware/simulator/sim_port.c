// app_port.h in the simulator: the performance counter as the board's timer, a wall clock that starts at 1970 like
// the board's (no RTC; the companion sets it), malloc for both kinds of memory, two frame buffers swapped on
// present, and a TCP connection on 127.0.0.1 in place of the console UART (frames and log lines, as on the board).
#define _CRT_RAND_S
#define WIN32_LEAN_AND_MEAN
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_link.h"
#include "app_port.h"
#include "sim.h"

#ifndef DESKBUDDY_SIM_VERSION
#define DESKBUDDY_SIM_VERSION "unknown"
#endif

void (*sim_on_present)(void);
void (*sim_on_link)(bool connected);

static LARGE_INTEGER s_freq, s_start;
static CRITICAL_SECTION s_out; // stdout and the companion's socket: one line at a time
static SOCKET s_client = INVALID_SOCKET;

// ---- time ----

int64_t app_port_time_us(void)
{
    LARGE_INTEGER now;
    QueryPerformanceCounter(&now);
    const int64_t ticks = now.QuadPart - s_start.QuadPart;
    return ticks / s_freq.QuadPart * 1000000 + ticks % s_freq.QuadPart * 1000000 / s_freq.QuadPart;
}

uint32_t app_port_random(void)
{
    unsigned int r = 0;
    rand_s(&r);
    return r;
}

void app_port_random_fill(void *buf, size_t len)
{
    uint8_t *p = buf;
    for (size_t i = 0; i < len; i++) {
        unsigned int r = 0;
        rand_s(&r); // the system's cryptographic generator
        p[i] = (uint8_t)r;
    }
}

// Wall clock = the time it was set to plus the time since; from 1970 at start-up, as on the board.
static int64_t s_wall_base_us; // wall-clock microseconds at app_port_time_us() == s_wall_at_us
static int64_t s_wall_at_us;

void app_port_wall_get(int64_t *sec, uint32_t *usec)
{
    const int64_t us = s_wall_base_us + (app_port_time_us() - s_wall_at_us);
    *sec = us / 1000000;
    *usec = (uint32_t)(us % 1000000);
}

void app_port_wall_set(int64_t sec, uint32_t usec)
{
    s_wall_at_us = app_port_time_us();
    s_wall_base_us = sec * 1000000 + usec;
}

const char *app_port_version(void)
{
    return DESKBUDDY_SIM_VERSION;
}

// "sim-" and 12 hex digits of a hash of the computer's name: the same every run on this machine, another on the next.
const char *app_port_device_id(void)
{
    static char id[17];
    if (!id[0]) {
        char name[MAX_COMPUTERNAME_LENGTH + 1] = "";
        DWORD len = sizeof(name);
        GetComputerNameA(name, &len);
        uint64_t h = 0xcbf29ce484222325ull; // FNV-1a
        for (const char *p = name; *p; p++) {
            h = (h ^ (uint8_t)*p) * 0x100000001b3ull;
        }
        snprintf(id, sizeof(id), "sim-%012llx", (unsigned long long)(h & 0xffffffffffffull));
    }
    return id;
}

void *app_port_alloc(size_t size, app_mem_t kind)
{
    (void)kind;
    return malloc(size);
}

// ---- output: the console window and the companion ----

// Writes one line to the console and, if the companion is connected, to it.
static void out_line(const char *line, size_t len)
{
    EnterCriticalSection(&s_out);
    fwrite(line, 1, len, stdout);
    fflush(stdout);
    if (s_client != INVALID_SOCKET) {
        send(s_client, line, (int)len, 0); // a failed send shows up in the link thread's recv
    }
    LeaveCriticalSection(&s_out);
}

void app_port_send(const char *line, size_t len)
{
    out_line(line, len);
}

// ESP-IDF's format: "I (12345) tag: text".
void app_port_log(char level, const char *tag, const char *fmt, ...)
{
    char line[1024];
    int n = snprintf(line, sizeof(line), "%c (%lld) %s: ", level, (long long)(app_port_time_us() / 1000), tag);
    va_list ap;
    va_start(ap, fmt);
    const int m = vsnprintf(line + n, sizeof(line) - (size_t)n - 1, fmt, ap);
    va_end(ap);
    n = m < 0 ? n : (n + m < (int)sizeof(line) - 1 ? n + m : (int)sizeof(line) - 2);
    line[n++] = '\n';
    out_line(line, (size_t)n);
}

// ---- display ----

static int s_w, s_h;
static uint16_t *s_fb[2];
static int s_back = 1; // buffer 0 is shown first (black), as on the board
static bool s_lit;
static uint32_t s_presents;

gfx_color_t *app_port_back_buffer(void)
{
    return s_fb[s_back];
}

void app_port_flush(const gfx_color_t *px, int count)
{
    (void)px; // nothing between the renderer and the window
    (void)count;
}

void app_port_present(void)
{
    s_back ^= 1; // the window shows the other buffer from now on; nothing reads the old one any more
    s_presents++;
    if (sim_on_present) {
        sim_on_present();
    }
}

const uint16_t *sim_front_buffer(void)
{
    return s_fb[s_back ^ 1];
}

bool app_port_backlight_on(void)
{
    s_lit = true;
    if (sim_on_present) {
        sim_on_present();
    }
    return true;
}

bool sim_lit(void)
{
    return s_lit;
}

void app_port_scanout_stats(app_scanout_stats_t *out)
{
    *out = (app_scanout_stats_t){.presents = s_presents}; // no panel scanout to measure
    s_presents = 0;
}

// ---- the event queue ----

#define QUEUE_LEN 64

static CRITICAL_SECTION s_qlock;
static HANDLE s_qsignal;
static app_event_t s_queue[QUEUE_LEN];
static int s_qhead, s_qcount;

bool sim_queue_push(const app_event_t *ev)
{
    bool ok = true;
    EnterCriticalSection(&s_qlock);
    if (s_qcount == QUEUE_LEN) {
        if (ev->type == APP_TOUCH_MOVE || ev->type == APP_COMMAND) {
            ok = false; // newer moves follow; a command gets "err busy"
        } else {
            s_qhead = (s_qhead + 1) % QUEUE_LEN; // down/up must arrive: the oldest goes
            s_qcount--;
        }
    }
    if (ok) {
        s_queue[(s_qhead + s_qcount) % QUEUE_LEN] = *ev;
        s_qcount++;
        SetEvent(s_qsignal);
    }
    LeaveCriticalSection(&s_qlock);
    return ok;
}

bool sim_queue_pop(app_event_t *ev)
{
    EnterCriticalSection(&s_qlock);
    const bool got = s_qcount > 0;
    if (got) {
        *ev = s_queue[s_qhead];
        s_qhead = (s_qhead + 1) % QUEUE_LEN;
        s_qcount--;
    }
    if (s_qcount == 0) {
        ResetEvent(s_qsignal);
    }
    LeaveCriticalSection(&s_qlock);
    return got;
}

void *sim_queue_signal(void)
{
    return s_qsignal;
}

void sim_port_init(int width, int height)
{
    QueryPerformanceFrequency(&s_freq);
    QueryPerformanceCounter(&s_start);
    InitializeCriticalSection(&s_out);
    InitializeCriticalSection(&s_qlock);
    s_qsignal = CreateEventW(NULL, TRUE, FALSE, NULL);
    s_w = width;
    s_h = height;
    for (int i = 0; i < 2; i++) {
        s_fb[i] = calloc((size_t)width * height, sizeof(uint16_t));
        if (!s_fb[i]) {
            fprintf(stderr, "no memory for the frame buffers\n");
            exit(1);
        }
    }
}

// ---- the link to the companion ----

static SOCKET s_listen = INVALID_SOCKET;

static bool deliver(const app_event_t *ev, void *ctx)
{
    (void)ctx;
    return sim_queue_push(ev);
}

static void set_client(SOCKET c)
{
    EnterCriticalSection(&s_out);
    s_client = c;
    LeaveCriticalSection(&s_out);
}

// One companion at a time: another connection is accepted and closed at once.
static DWORD WINAPI link_thread(void *arg)
{
    (void)arg;
    static app_link_rx_t rx;
    SOCKET client = INVALID_SOCKET;
    for (;;) {
        fd_set ready;
        FD_ZERO(&ready);
        FD_SET(s_listen, &ready);
        if (client != INVALID_SOCKET) {
            FD_SET(client, &ready);
        }
        if (select(0, &ready, NULL, NULL, NULL) == SOCKET_ERROR) {
            app_port_log('E', "link", "select failed (%d)", WSAGetLastError());
            return 1;
        }
        if (FD_ISSET(s_listen, &ready)) {
            const SOCKET s = accept(s_listen, NULL, NULL);
            if (s != INVALID_SOCKET && client != INVALID_SOCKET) {
                closesocket(s); // the companion that is connected keeps the link
                app_port_log('W', "link", "a second connection was refused (one companion at a time)");
            } else if (s != INVALID_SOCKET) {
                const BOOL on = TRUE;
                setsockopt(s, IPPROTO_TCP, TCP_NODELAY, (const char *)&on, sizeof(on));
                client = s;
                app_link_rx_reset(&rx);
                set_client(client);
                app_port_log('I', "link", "companion connected");
                if (sim_on_link) {
                    sim_on_link(true);
                }
            }
        }
        if (client != INVALID_SOCKET && FD_ISSET(client, &ready)) {
            uint8_t buf[512];
            const int n = recv(client, (char *)buf, sizeof(buf), 0);
            if (n <= 0) {
                set_client(INVALID_SOCKET);
                closesocket(client);
                client = INVALID_SOCKET;
                app_port_log('I', "link", "companion disconnected");
                if (sim_on_link) {
                    sim_on_link(false);
                }
                continue;
            }
            app_link_feed(&rx, buf, (size_t)n, deliver, NULL);
        }
    }
}

bool sim_link_start(uint16_t port)
{
    WSADATA wsa;
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        return false;
    }
    s_listen = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (s_listen == INVALID_SOCKET) {
        return false;
    }
    const BOOL on = TRUE; // a second simulator must not share the port
    setsockopt(s_listen, SOL_SOCKET, SO_EXCLUSIVEADDRUSE, (const char *)&on, sizeof(on));
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_port = htons(port);
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK); // this PC only
    if (bind(s_listen, (struct sockaddr *)&addr, sizeof(addr)) == SOCKET_ERROR || listen(s_listen, 2) == SOCKET_ERROR) {
        closesocket(s_listen);
        s_listen = INVALID_SOCKET;
        return false;
    }
    const HANDLE t = CreateThread(NULL, 0, link_thread, NULL, 0, NULL);
    if (!t) {
        return false;
    }
    CloseHandle(t);
    return true;
}
