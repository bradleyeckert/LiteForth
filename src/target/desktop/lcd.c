/*
 * lcd.c: a simulated graphic LCD module (see lcd.h).
 *
 * The controller model works on its own frame memory; a window thread
 * shows the panel about 30 times a second when it has changed. lcd_tx and
 * the window thread share the model under a lock.
 *
 * Window backends: Win32 GDI (_WIN32, needs -lgdi32), X11 (LCD_X11, needs
 * -lX11), none (LCD_HEADLESS: the model only, lcd_init always works), or
 * unavailable (lcd_init reports ERR_UNSUPPORTED_OPERATION).
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lcd.h"
#include "errcodes.h"

#if defined(LCD_HEADLESS)
    #define LCD_BACKEND_NONE
#elif defined(_WIN32) || defined(_WIN64)
    #define LCD_BACKEND_WIN32
    #include <windows.h>
#elif defined(LCD_X11)
    #define LCD_BACKEND_X11
    #include <pthread.h>
    #include <sys/select.h>
    #include <unistd.h>
    #include <X11/Xlib.h>
    #include <X11/Xutil.h>
#else
    #define LCD_BACKEND_UNAVAILABLE
#endif

/* ---- the ST7789 model ------------------------------------------------- */

#define ST_COLS 240                     // frame memory size
#define ST_ROWS 320

// MADCTL bits
#define MAD_MY  0x80                    // row address order
#define MAD_MX  0x40                    // column address order
#define MAD_MV  0x20                    // row/column exchange
#define MAD_BGR 0x08                    // BGR order

typedef struct {
    uint32_t* gram;                     // ST_ROWS x ST_COLS, 18-bit RRRRRRGGGGGGBBBBBB
    int width, height;                  // the panel
    int in_reset;                       // reset is held (bit 15)
    uint8_t cmd;                        // the command being received
    uint8_t param[8];
    int nparam;
    int writing;                        // RAMWR or RAMWRC data
    uint8_t pix[3];                     // bytes of the pixel(s) being assembled
    int npix;
    int sleeping, display_on, inverted, idle, scrolling;
    uint8_t madctl, colmod;
    int xs, xe, ys, ye;                 // CASET and RASET windows
    int cx, cy;                         // the write position in that window
    int tfa, vsa, vsp;                  // vertical scrolling
} st7789_t;

static st7789_t lcd;
static int lcd_open;                    // lcd_init succeeded
static volatile int lcd_dirty;          // the panel may have changed

/* Reset state, from hardware or software reset. Frame memory is kept. */
static void st_reset(void) {
    lcd.cmd = 0;  lcd.nparam = 0;  lcd.writing = 0;  lcd.npix = 0;
    lcd.sleeping = 1;  lcd.display_on = 0;  lcd.inverted = 0;
    lcd.idle = 0;  lcd.scrolling = 0;
    lcd.madctl = 0;  lcd.colmod = 0x66;
    lcd.xs = 0;  lcd.xe = ST_COLS - 1;  lcd.ys = 0;  lcd.ye = ST_ROWS - 1;
    lcd.cx = 0;  lcd.cy = 0;
    lcd.tfa = 0;  lcd.vsa = ST_ROWS;  lcd.vsp = 0;
}

/* Stores one pixel at the write position and advances it. */
static void st_pixel(uint32_t r6, uint32_t g6, uint32_t b6) {
    if (lcd.madctl & MAD_BGR) { uint32_t t = r6; r6 = b6; b6 = t; }
    int x = lcd.cx, y = lcd.cy;
    if (lcd.madctl & MAD_MV) { int t = x; x = y; y = t; }
    if (x < ST_COLS && y < ST_ROWS) {
        if (lcd.madctl & MAD_MX) x = ST_COLS - 1 - x;
        if (lcd.madctl & MAD_MY) y = ST_ROWS - 1 - y;
        lcd.gram[y * ST_COLS + x] = (r6 << 12) | (g6 << 6) | b6;
    }
    if (++lcd.cx > lcd.xe) {
        lcd.cx = lcd.xs;
        if (++lcd.cy > lcd.ye) lcd.cy = lcd.ys;
    }
}

/* A byte of pixel data, in the format COLMOD selects. */
static void st_pixel_byte(uint8_t b) {
    lcd.pix[lcd.npix++] = b;
    switch (lcd.colmod & 7) {
    case 5:                             // 16 bits: RRRRRGGG GGGBBBBB
        if (lcd.npix == 2) {
            uint32_t r = lcd.pix[0] >> 3;
            uint32_t g = ((lcd.pix[0] & 7u) << 3) | (lcd.pix[1] >> 5);
            uint32_t bl = lcd.pix[1] & 31u;
            st_pixel((r << 1) | (r >> 4), g, (bl << 1) | (bl >> 4));
            lcd.npix = 0;
        }
        break;
    case 3:                             // 12 bits: two pixels in 3 bytes
        if (lcd.npix == 3) {
            uint32_t v[6] = { lcd.pix[0] >> 4u, lcd.pix[0] & 15u, lcd.pix[1] >> 4u,
                              lcd.pix[1] & 15u, lcd.pix[2] >> 4u, lcd.pix[2] & 15u };
            for (int i = 0; i < 6; i += 3) {
                st_pixel((v[i] << 2) | (v[i] >> 2), (v[i + 1] << 2) | (v[i + 1] >> 2),
                         (v[i + 2] << 2) | (v[i + 2] >> 2));
            }
            lcd.npix = 0;
        }
        break;
    default:                            // 18 bits (and 24, truncated): 3 bytes
        if (lcd.npix == 3) {
            st_pixel(lcd.pix[0] >> 2, lcd.pix[1] >> 2, lcd.pix[2] >> 2);
            lcd.npix = 0;
        }
        break;
    }
}

/* Commands that take effect when they arrive. */
static void st_command(uint8_t c) {
    lcd.cmd = c;  lcd.nparam = 0;  lcd.writing = 0;  lcd.npix = 0;
    switch (c) {
    case 0x01: st_reset(); break;                       // SWRESET
    case 0x10: lcd.sleeping = 1; break;                 // SLPIN
    case 0x11: lcd.sleeping = 0; break;                 // SLPOUT
    case 0x12: lcd.scrolling = 0; break;                // PTLON (partial: not shown)
    case 0x13: lcd.scrolling = 0; break;                // NORON
    case 0x20: lcd.inverted = 0; break;                 // INVOFF
    case 0x21: lcd.inverted = 1; break;                 // INVON
    case 0x28: lcd.display_on = 0; break;               // DISPOFF
    case 0x29: lcd.display_on = 1; break;               // DISPON
    case 0x38: lcd.idle = 0; break;                     // IDMOFF
    case 0x39: lcd.idle = 1; break;                     // IDMON
    case 0x2C: lcd.cx = lcd.xs; lcd.cy = lcd.ys;        // RAMWR
               lcd.writing = 1; break;
    case 0x3C: lcd.writing = 1; break;                  // RAMWRC: from where it was
    default: break;                                     // others wait for parameters
    }
}

/* Parameters, used when the command has all it needs. */
static void st_parameter(uint8_t b) {
    if (lcd.nparam < (int)sizeof lcd.param) lcd.param[lcd.nparam] = b;
    lcd.nparam++;
    const uint8_t* p = lcd.param;
    switch (lcd.cmd) {
    case 0x2A:                                          // CASET XS XE
        if (lcd.nparam == 4) { lcd.xs = (p[0] << 8) | p[1];  lcd.xe = (p[2] << 8) | p[3]; }
        break;
    case 0x2B:                                          // RASET YS YE
        if (lcd.nparam == 4) { lcd.ys = (p[0] << 8) | p[1];  lcd.ye = (p[2] << 8) | p[3]; }
        break;
    case 0x33:                                          // VSCRDEF TFA VSA BFA
        if (lcd.nparam == 6) { lcd.tfa = (p[0] << 8) | p[1];  lcd.vsa = (p[2] << 8) | p[3]; }
        break;
    case 0x36:                                          // MADCTL
        if (lcd.nparam == 1) lcd.madctl = p[0];
        break;
    case 0x37:                                          // VSCSAD VSP
        if (lcd.nparam == 2) { lcd.vsp = (p[0] << 8) | p[1];  lcd.scrolling = 1; }
        break;
    case 0x3A:                                          // COLMOD
        if (lcd.nparam == 1) lcd.colmod = p[0];
        break;
    default: break;
    }
}

/* The color the panel shows at (x, y), as 0xRRGGBB. */
static uint32_t st_shown(int x, int y) {
    if (lcd.sleeping || !lcd.display_on) return 0;
    int gy = y;
    if (lcd.scrolling && lcd.vsa > 0 && y >= lcd.tfa && y < lcd.tfa + lcd.vsa) {
        gy = y - lcd.tfa + lcd.vsp;
        while (gy >= lcd.tfa + lcd.vsa) gy -= lcd.vsa;
    }
    if (gy < 0 || gy >= ST_ROWS) return 0;
    uint32_t c = lcd.gram[gy * ST_COLS + x];
    if (lcd.idle) c &= 0x20820;                         // 8 colors: the top bits
    if (lcd.inverted) c ^= 0x3FFFF;
    uint32_t r = (c >> 12) & 63, g = (c >> 6) & 63, b = c & 63;
    if (lcd.idle) { r = r ? 63 : 0;  g = g ? 63 : 0;  b = b ? 63 : 0; }
    return (((r << 2) | (r >> 4)) << 16) | (((g << 2) | (g >> 4)) << 8) | ((b << 2) | (b >> 4));
}

#if defined(LCD_BACKEND_WIN32) || defined(LCD_BACKEND_X11)
/* Fills out (width*zoom x height*zoom, 0x00RRGGBB) from the panel. */
static void st_compose(uint32_t* out, int zoom) {
    int w = lcd.width * zoom;
    for (int y = 0; y < lcd.height; y++) {
        uint32_t* row = out + (size_t)y * zoom * w;
        for (int x = 0; x < lcd.width; x++) {
            uint32_t c = st_shown(x, y);
            for (int k = 0; k < zoom; k++) row[x * zoom + k] = c;
        }
        for (int k = 1; k < zoom; k++) memcpy(row + (size_t)k * w, row, (size_t)w * 4);
    }
}
#endif

/* ---- locking ----------------------------------------------------------- */

#if defined(LCD_BACKEND_WIN32)
static CRITICAL_SECTION lcd_cs;
static int lcd_cs_ready;
#define LCD_LOCK()   EnterCriticalSection(&lcd_cs)
#define LCD_UNLOCK() LeaveCriticalSection(&lcd_cs)
#elif defined(LCD_BACKEND_X11)
static pthread_mutex_t lcd_mutex = PTHREAD_MUTEX_INITIALIZER;
#define LCD_LOCK()   pthread_mutex_lock(&lcd_mutex)
#define LCD_UNLOCK() pthread_mutex_unlock(&lcd_mutex)
#else
#define LCD_LOCK()   ((void)0)
#define LCD_UNLOCK() ((void)0)
#endif

/* ---- windows ----------------------------------------------------------- */

static uint32_t* win_pixels;            // the zoomed image the window shows

#if defined(LCD_BACKEND_WIN32)

static HANDLE win_thread;
static HANDLE win_ready;
static HWND win_hwnd;
static volatile int win_ok;

static LRESULT CALLBACK win_proc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_TIMER:
        if (lcd_dirty) InvalidateRect(hwnd, NULL, FALSE);
        return 0;
    case WM_PAINT: {
        PAINTSTRUCT ps;
        HDC dc = BeginPaint(hwnd, &ps);
        int w = lcd.width * LCD_ZOOM, h = lcd.height * LCD_ZOOM;
        LCD_LOCK();
        lcd_dirty = 0;
        st_compose(win_pixels, LCD_ZOOM);
        LCD_UNLOCK();
        BITMAPINFO bmi;
        memset(&bmi, 0, sizeof bmi);
        bmi.bmiHeader.biSize = sizeof bmi.bmiHeader;
        bmi.bmiHeader.biWidth = w;
        bmi.bmiHeader.biHeight = -h;                    // top-down
        bmi.bmiHeader.biPlanes = 1;
        bmi.bmiHeader.biBitCount = 32;
        bmi.bmiHeader.biCompression = BI_RGB;
        SetDIBitsToDevice(dc, 0, 0, (DWORD)w, (DWORD)h, 0, 0, 0, (UINT)h,
                          win_pixels, &bmi, DIB_RGB_COLORS);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CLOSE:
        DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        KillTimer(hwnd, 1);
        PostQuitMessage(0);
        return 0;
    default:
        return DefWindowProcA(hwnd, msg, wp, lp);
    }
}

static DWORD WINAPI win_main(LPVOID arg) {
    (void)arg;
    static int registered;
    HINSTANCE inst = GetModuleHandleA(NULL);
    if (!registered) {
        WNDCLASSA wc;
        memset(&wc, 0, sizeof wc);
        wc.lpfnWndProc = win_proc;
        wc.hInstance = inst;
        wc.hCursor = LoadCursor(NULL, IDC_ARROW);
        wc.lpszClassName = "LiteForthLCD";
        registered = (RegisterClassA(&wc) != 0);
    }
    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
    RECT r = { 0, 0, lcd.width * LCD_ZOOM, lcd.height * LCD_ZOOM };
    AdjustWindowRect(&r, style, FALSE);
    char title[64];
    snprintf(title, sizeof title, "LiteForth LCD %dx%d ST7789", lcd.width, lcd.height);
    win_hwnd = registered ? CreateWindowA("LiteForthLCD", title, style, CW_USEDEFAULT,
                                          CW_USEDEFAULT, r.right - r.left, r.bottom - r.top,
                                          NULL, NULL, inst, NULL) : NULL;
    win_ok = (win_hwnd != NULL);
    if (win_ok) {
        ShowWindow(win_hwnd, SW_SHOWNOACTIVATE);
        SetTimer(win_hwnd, 1, 33, NULL);
    }
    SetEvent(win_ready);
    if (!win_ok) return 0;
    MSG msg;
    while (GetMessageA(&msg, NULL, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageA(&msg);
    }
    win_hwnd = NULL;
    return 0;
}

static int win_open(void) {
    win_ready = CreateEventA(NULL, TRUE, FALSE, NULL);
    win_thread = CreateThread(NULL, 0, win_main, NULL, 0, NULL);
    if (!win_ready || !win_thread) return ERR_UNSUPPORTED_OPERATION;
    WaitForSingleObject(win_ready, INFINITE);
    CloseHandle(win_ready);
    if (!win_ok) {
        WaitForSingleObject(win_thread, INFINITE);
        CloseHandle(win_thread);
        win_thread = NULL;
        return ERR_UNSUPPORTED_OPERATION;
    }
    return 0;
}

static void win_close(void) {
    if (!win_thread) return;
    HWND hwnd = win_hwnd;
    if (hwnd) PostMessageA(hwnd, WM_CLOSE, 0, 0);       // unless the user closed it
    WaitForSingleObject(win_thread, INFINITE);
    CloseHandle(win_thread);
    win_thread = NULL;
}

#elif defined(LCD_BACKEND_X11)

static Display* x_dpy;
static Window x_win;
static GC x_gc;
static XImage* x_img;
static Atom x_wm_delete;
static pthread_t x_thread;
static int x_running;
static int x_stop[2] = { -1, -1 };      // a pipe that tells the thread to stop

static void x_draw(void) {
    LCD_LOCK();
    lcd_dirty = 0;
    st_compose(win_pixels, LCD_ZOOM);
    LCD_UNLOCK();
    XPutImage(x_dpy, x_win, x_gc, x_img, 0, 0, 0, 0,
              (unsigned)(lcd.width * LCD_ZOOM), (unsigned)(lcd.height * LCD_ZOOM));
    XFlush(x_dpy);
}

static void* x_main(void* arg) {
    (void)arg;
    int fd = ConnectionNumber(x_dpy);
    int open = 1;
    while (open) {
        int expose = 0;
        while (XPending(x_dpy)) {
            XEvent ev;
            XNextEvent(x_dpy, &ev);
            if (ev.type == Expose) expose = 1;
            if (ev.type == ClientMessage && (Atom)ev.xclient.data.l[0] == x_wm_delete) {
                XDestroyWindow(x_dpy, x_win);           // the user closed it
                XFlush(x_dpy);
                open = 0;
            }
        }
        if (!open) break;
        if (expose || lcd_dirty) x_draw();
        fd_set fds;
        FD_ZERO(&fds);
        FD_SET(fd, &fds);
        FD_SET(x_stop[0], &fds);
        struct timeval tv = { 0, 33000 };
        int maxfd = (fd > x_stop[0]) ? fd : x_stop[0];
        if (select(maxfd + 1, &fds, NULL, NULL, &tv) > 0 && FD_ISSET(x_stop[0], &fds)) {
            XDestroyWindow(x_dpy, x_win);
            open = 0;
        }
    }
    x_img->data = NULL;                 // win_pixels isn't Xlib's to free
    XDestroyImage(x_img);
    XFreeGC(x_dpy, x_gc);
    XCloseDisplay(x_dpy);
    x_dpy = NULL;
    return NULL;
}

static int win_open(void) {
    x_dpy = XOpenDisplay(NULL);
    if (!x_dpy) return ERR_UNSUPPORTED_OPERATION;
    int scr = DefaultScreen(x_dpy);
    Visual* vis = DefaultVisual(x_dpy, scr);
    int depth = DefaultDepth(x_dpy, scr);
    if (depth < 24 || vis->red_mask != 0xFF0000 || vis->green_mask != 0xFF00
        || vis->blue_mask != 0xFF) {
        XCloseDisplay(x_dpy);  x_dpy = NULL;
        return ERR_UNSUPPORTED_OPERATION;
    }
    unsigned w = (unsigned)(lcd.width * LCD_ZOOM), h = (unsigned)(lcd.height * LCD_ZOOM);
    x_win = XCreateSimpleWindow(x_dpy, RootWindow(x_dpy, scr), 0, 0, w, h, 0,
                                BlackPixel(x_dpy, scr), BlackPixel(x_dpy, scr));
    char title[64];
    snprintf(title, sizeof title, "LiteForth LCD %dx%d ST7789", lcd.width, lcd.height);
    XStoreName(x_dpy, x_win, title);
    XSizeHints* hints = XAllocSizeHints();
    if (hints) {                        // a fixed size
        hints->flags = PMinSize | PMaxSize;
        hints->min_width = hints->max_width = (int)w;
        hints->min_height = hints->max_height = (int)h;
        XSetWMNormalHints(x_dpy, x_win, hints);
        XFree(hints);
    }
    x_wm_delete = XInternAtom(x_dpy, "WM_DELETE_WINDOW", False);
    XSetWMProtocols(x_dpy, x_win, &x_wm_delete, 1);
    XSelectInput(x_dpy, x_win, ExposureMask);
    x_gc = XCreateGC(x_dpy, x_win, 0, NULL);
    x_img = XCreateImage(x_dpy, vis, (unsigned)depth, ZPixmap, 0, (char*)win_pixels,
                         w, h, 32, 0);
    if (!x_img || pipe(x_stop) != 0) {
        if (x_img) { x_img->data = NULL;  XDestroyImage(x_img); }
        XCloseDisplay(x_dpy);  x_dpy = NULL;
        return ERR_UNSUPPORTED_OPERATION;
    }
    const uint32_t one = 1;
    x_img->byte_order = (*(const uint8_t*)&one) ? LSBFirst : MSBFirst;
    XMapWindow(x_dpy, x_win);
    XFlush(x_dpy);
    if (pthread_create(&x_thread, NULL, x_main, NULL) != 0) {
        x_img->data = NULL;  XDestroyImage(x_img);
        XCloseDisplay(x_dpy);  x_dpy = NULL;
        close(x_stop[0]);  close(x_stop[1]);
        return ERR_UNSUPPORTED_OPERATION;
    }
    x_running = 1;
    return 0;
}

static void win_close(void) {
    if (!x_running) return;
    ssize_t n = write(x_stop[1], "x", 1);
    (void)n;
    pthread_join(x_thread, NULL);
    close(x_stop[0]);  close(x_stop[1]);
    x_running = 0;
}

#elif defined(LCD_BACKEND_NONE)

static int win_open(void) { return 0; }
static void win_close(void) { }

#else

static int win_open(void) { return ERR_UNSUPPORTED_OPERATION; }
static void win_close(void) { }

#endif

/* ---- the interface ----------------------------------------------------- */

int lcd_init(int width, int height, int controller) {
    lcd_close();
    if (controller != LCD_ST7789 || width < 1 || height < 1
        || width > ST_COLS || height > ST_ROWS) {
        return ERR_UNSUPPORTED_OPERATION;
    }
#if defined(LCD_BACKEND_WIN32)
    if (!lcd_cs_ready) { InitializeCriticalSection(&lcd_cs);  lcd_cs_ready = 1; }
#endif
    lcd.gram = malloc((size_t)ST_ROWS * ST_COLS * sizeof *lcd.gram);
    win_pixels = malloc((size_t)width * height * LCD_ZOOM * LCD_ZOOM * 4);
    if (!lcd.gram || !win_pixels) {
        free(lcd.gram);  free(win_pixels);
        lcd.gram = NULL;  win_pixels = NULL;
        return ERR_UNSUPPORTED_OPERATION;
    }
    uint32_t seed = 0x1234567u;         // power-on garbage
    for (int i = 0; i < ST_ROWS * ST_COLS; i++) {
        seed = seed * 1664525u + 1013904223u;
        lcd.gram[i] = (seed >> 8) & 0x3FFFF;
    }
    lcd.width = width;  lcd.height = height;
    lcd.in_reset = 0;
    st_reset();
    int ior = win_open();
    if (ior) {
        free(lcd.gram);  free(win_pixels);
        lcd.gram = NULL;  win_pixels = NULL;
        return ior;
    }
    lcd_open = 1;
    lcd_dirty = 1;
    return 0;
}

int lcd_tx(uint16_t data) {
    if (!lcd_open) return ERR_UNSUPPORTED_OPERATION;
    LCD_LOCK();
    if (data & LCD_RESET) {                             // held in reset
        if (!lcd.in_reset) st_reset();
        lcd.in_reset = 1;
    } else {
        lcd.in_reset = 0;
        if (!(data & LCD_CSX)) {
            uint8_t b = (uint8_t)(data & LCD_DATA);
            if (!(data & LCD_DCX)) st_command(b);
            else if (lcd.writing) st_pixel_byte(b);
            else st_parameter(b);
        }
    }
    lcd_dirty = 1;
    LCD_UNLOCK();
    return 0;
}

int32_t lcd_pixel(int x, int y) {
    if (!lcd_open || x < 0 || y < 0 || x >= lcd.width || y >= lcd.height) return -1;
    LCD_LOCK();
    uint32_t c = st_shown(x, y);
    LCD_UNLOCK();
    return (int32_t)c;
}

int lcd_save(const char* filename) {
    if (!lcd_open) return ERR_UNSUPPORTED_OPERATION;
    FILE* f = fopen(filename, "wb");
    if (!f) return ERR_CREATE_FILE_FAILED;
    fprintf(f, "P6\n%d %d\n255\n", lcd.width, lcd.height);
    LCD_LOCK();
    for (int y = 0; y < lcd.height; y++) {
        for (int x = 0; x < lcd.width; x++) {
            uint32_t c = st_shown(x, y);
            fputc((int)(c >> 16), f);  fputc((int)((c >> 8) & 255), f);  fputc((int)(c & 255), f);
        }
    }
    LCD_UNLOCK();
    int bad = ferror(f);
    if (fclose(f) != 0) bad = 1;
    return bad ? ERR_CREATE_FILE_FAILED : 0;
}

void lcd_close(void) {
    if (!lcd_open) return;
    win_close();
    lcd_open = 0;
    free(lcd.gram);  free(win_pixels);
    lcd.gram = NULL;  win_pixels = NULL;
}
