// DeskBuddy simulator: the board's application in a window. The left mouse button is a finger on the touchscreen;
// the companion connects to 127.0.0.1:47801 (or --port) instead of the USB serial port.
//
//   deskbuddy_sim [--port N] [--scale N] [--stage PERCENT] [--panel-left] [--verify]
//
// The loop is the board's render task: wait for events (at most as long as app_frame() said), hand them to
// app_event(), then app_frame(). Window messages are handled while waiting.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <timeapi.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "app_link.h"
#include "app_port.h"
#include "sim.h"

#define W 1024 // the board's panel (BOARD_LCD_H_RES x BOARD_LCD_V_RES)
#define H 600
#define WM_SIM_LINK (WM_APP + 1)

static HWND s_wnd;
static int s_scale = 1;
static uint16_t s_port = SIM_DEFAULT_PORT;
static bool s_linked;

// The touch as the GT911 would report it: down, moves while held (only when the point changes), up where it was.
static bool s_down;
static int16_t s_tx, s_ty;

static void touch(app_event_type_t type, LPARAM lp)
{
    int x = (short)LOWORD(lp) / s_scale, y = (short)HIWORD(lp) / s_scale;
    x = x < 0 ? 0 : x >= W ? W - 1 : x;
    y = y < 0 ? 0 : y >= H ? H - 1 : y;
    if (type == APP_TOUCH_UP) {
        x = s_tx; // the release is reported at the last touched point
        y = s_ty;
    } else if (type == APP_TOUCH_MOVE && x == s_tx && y == s_ty) {
        return;
    }
    s_tx = (int16_t)x;
    s_ty = (int16_t)y;
    const app_event_t ev = {.type = type, .x = s_tx, .y = s_ty, .at_us = app_port_time_us()};
    sim_queue_push(&ev);
}

static void update_title(void)
{
    wchar_t title[128];
    swprintf(title, sizeof(title) / sizeof(title[0]), L"DeskBuddy simulator \u2013 127.0.0.1:%u \u2013 %ls",
             (unsigned)s_port, s_linked ? L"companion connected" : L"waiting for the companion");
    SetWindowTextW(s_wnd, title);
}

static void paint(HDC dc)
{
    if (!sim_lit()) { // the backlight comes on once both buffers are drawn, as on the board
        RECT r = {0, 0, W * s_scale, H * s_scale};
        FillRect(dc, &r, (HBRUSH)GetStockObject(BLACK_BRUSH));
        return;
    }
    struct {
        BITMAPINFOHEADER h;
        DWORD masks[3];
    } bmi = {{sizeof(BITMAPINFOHEADER), W, -H, 1, 16, BI_BITFIELDS, 0, 0, 0, 0, 0}, {0xF800, 0x07E0, 0x001F}};
    SetStretchBltMode(dc, COLORONCOLOR); // whole-pixel scaling, no smoothing
    StretchDIBits(dc, 0, 0, W * s_scale, H * s_scale, 0, 0, W, H, sim_front_buffer(), (const BITMAPINFO *)&bmi,
                  DIB_RGB_COLORS, SRCCOPY);
}

static LRESULT CALLBACK wnd_proc(HWND wnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_LBUTTONDOWN:
        SetCapture(wnd);
        s_down = true;
        touch(APP_TOUCH_DOWN, lp);
        return 0;
    case WM_MOUSEMOVE:
        if (s_down) {
            touch(APP_TOUCH_MOVE, lp);
        }
        return 0;
    case WM_LBUTTONUP:
        if (s_down) {
            s_down = false;
            touch(APP_TOUCH_UP, lp);
        }
        ReleaseCapture();
        return 0;
    case WM_CAPTURECHANGED: // the finger is lifted when the window loses the mouse
        if (s_down) {
            s_down = false;
            touch(APP_TOUCH_UP, 0);
        }
        return 0;
    case WM_SIM_LINK:
        s_linked = wp != 0;
        update_title();
        return 0;
    case WM_ERASEBKGND:
        return 1;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(wnd, &ps);
        paint(dc);
        EndPaint(wnd, &ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcW(wnd, msg, wp, lp);
    }
}

static void on_present(void)
{
    InvalidateRect(s_wnd, NULL, FALSE);
}

static void on_link(bool connected) // link thread
{
    PostMessageW(s_wnd, WM_SIM_LINK, connected, 0);
}

static int usage(void)
{
    fprintf(stderr, "usage: deskbuddy_sim [--port N] [--scale N] [--stage PERCENT] [--panel-left] [--verify]\n");
    return 2;
}

int main(int argc, char **argv)
{
    // menuconfig's defaults (firmware/deskbuddy/main/Kconfig.projbuild)
    app_config_t config = {.width = W, .height = H, .stage_percent = 65, .perf_log_s = 5};
    for (int i = 1; i < argc; i++) {
        const bool has_value = i + 1 < argc;
        if (!strcmp(argv[i], "--port") && has_value) {
            const int p = atoi(argv[++i]);
            if (p < 1 || p > 65535) {
                return usage();
            }
            s_port = (uint16_t)p;
        } else if (!strcmp(argv[i], "--scale") && has_value) {
            s_scale = atoi(argv[++i]);
            if (s_scale < 1 || s_scale > 4) {
                return usage();
            }
        } else if (!strcmp(argv[i], "--stage") && has_value) {
            config.stage_percent = atoi(argv[++i]);
            if (config.stage_percent < 40 || config.stage_percent > 100) {
                return usage();
            }
        } else if (!strcmp(argv[i], "--panel-left")) {
            config.panel_left = true;
        } else if (!strcmp(argv[i], "--verify")) {
            config.gfx_verify = true;
        } else {
            return usage();
        }
    }

    // Whole screen pixels, not stretched by Windows' display scaling; sleeps to the millisecond (the animations
    // wait for exact times, as on the board's 1 kHz FreeRTOS tick).
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    timeBeginPeriod(1);

    sim_port_init(W, H);
    sim_on_present = on_present;
    sim_on_link = on_link;

    const HINSTANCE inst = GetModuleHandleW(NULL);
    const WNDCLASSW wc = {.lpfnWndProc = wnd_proc,
                          .hInstance = inst,
                          .hCursor = LoadCursorW(NULL, (LPCWSTR)IDC_ARROW),
                          .lpszClassName = L"DeskBuddySim"};
    RegisterClassW(&wc);
    const DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = {0, 0, W * s_scale, H * s_scale};
    AdjustWindowRectEx(&r, style, FALSE, 0);
    s_wnd = CreateWindowExW(0, wc.lpszClassName, L"DeskBuddy simulator", style, CW_USEDEFAULT, CW_USEDEFAULT,
                            r.right - r.left, r.bottom - r.top, NULL, NULL, inst, NULL);
    if (!s_wnd) {
        fprintf(stderr, "cannot create the window\n");
        return 1;
    }
    update_title();
    ShowWindow(s_wnd, SW_SHOW);

    app_link_boot();
    if (!sim_link_start(s_port)) {
        char text[160];
        snprintf(text, sizeof(text), "Port %u on 127.0.0.1 is in use: is another DeskBuddy simulator running?",
                 s_port);
        MessageBoxA(s_wnd, text, "DeskBuddy simulator", MB_ICONERROR);
        return 1;
    }
    if (!app_init(&config)) {
        MessageBoxA(s_wnd, "The application did not start (see the console).", "DeskBuddy simulator", MB_ICONERROR);
        return 1;
    }

    HANDLE signal = sim_queue_signal();
    uint32_t wait_ms = 0;
    for (;;) {
        // Like xQueueReceive with a timeout: events end the wait at once; window messages are handled meanwhile.
        const int64_t until = app_port_time_us() + (int64_t)wait_ms * 1000;
        for (;;) {
            MSG m;
            while (PeekMessageW(&m, NULL, 0, 0, PM_REMOVE)) {
                if (m.message == WM_QUIT) {
                    timeEndPeriod(1);
                    return 0;
                }
                TranslateMessage(&m);
                DispatchMessageW(&m);
            }
            if (WaitForSingleObject(signal, 0) == WAIT_OBJECT_0) {
                break;
            }
            const int64_t left_us = until - app_port_time_us();
            if (left_us <= 0) {
                break;
            }
            MsgWaitForMultipleObjects(1, &signal, FALSE, (DWORD)((left_us + 999) / 1000), QS_ALLINPUT);
        }
        app_event_t ev;
        while (sim_queue_pop(&ev)) {
            app_event(&ev);
        }
        wait_ms = app_frame();
    }
}
