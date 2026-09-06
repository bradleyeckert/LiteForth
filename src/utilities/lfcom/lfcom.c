/** 

UART-terminal bridge console app "lfcom" compiles with:
- Linux using standard C11 toolchains with gcc or clang
- Windows using standard Win32 SDK allocations

MacOS is not supported.

**/

// Part 1: Global Platform Structs, Contexts, and Header Guard Definitions

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <signal.h>

#define BAUDRATE 115200

#ifdef _WIN32
    #include <windows.h>
    #define THREAD_RETURN DWORD WINAPI
    typedef HANDLE thread_t;
    typedef HANDLE serial_t;
    #define INVALID_SERIAL INVALID_HANDLE_VALUE
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <sys/ioctl.h>
    #include <asm/termbits.h> // Modern Linux direct-baud ioctl structure (struct termios2)
    #include <pthread.h>
    #include <glob.h>
//    #include <termios.h>
    #define THREAD_RETURN void*
    typedef pthread_t thread_t;
    typedef int serial_t;
    #define INVALID_SERIAL (-1)
#endif

#define SEQ_ENTER_RAW    "\033[?999h"  
#define SEQ_ENTER_COOKED "\033[?999l"  

typedef enum {
    STATE_GROUND,
    STATE_ESC,
    STATE_CSI,
    STATE_MATCHING_RAW
} BridgeFsmState;

// Shared configuration context
bool is_raw_mode = false;
bool loopback_mode = false;
serial_t serial_fd = INVALID_SERIAL;

#ifdef _WIN32
    struct termios_backup { DWORD stdin_mode; DWORD stdout_mode; };
#else
    // Save backup using regular system termios to restore raw/cooked console nicely
    struct termios_backup { struct termios2 stdin_orig; };
#endif
struct termios_backup orig_console_settings;

// --- Thread-Safe Loopback Queue Context ---
#define LOOPBACK_BUF_SIZE 4096
static char loopback_buffer[LOOPBACK_BUF_SIZE];
static volatile int loop_head = 0;
static volatile int loop_tail = 0;

#ifdef _WIN32
    static CRITICAL_SECTION loop_cs;
    #define LOCK_LOOP() EnterCriticalSection(&loop_cs)
    #define UNLOCK_LOOP() LeaveCriticalSection(&loop_cs)
#else
    static pthread_mutex_t loop_mutex = PTHREAD_MUTEX_INITIALIZER;
    #define LOCK_LOOP() pthread_mutex_lock(&loop_mutex)
    #define UNLOCK_LOOP() pthread_mutex_unlock(&loop_mutex)
#endif

// Part 2: IO Buffer Drivers, Loopback Framework, and Hardware Port Discovery

void loopback_write_char(char c) {
    LOCK_LOOP();
    int next_head = (loop_head + 1) % LOOPBACK_BUF_SIZE;
    if (next_head != loop_tail) {
        loopback_buffer[loop_head] = c;
        loop_head = next_head;
    }
    UNLOCK_LOOP();
}

int loopback_read_char(void) {
    int res = -1;
    LOCK_LOOP();
    if (loop_head != loop_tail) {
        res = (unsigned char)loopback_buffer[loop_tail];
        loop_tail = (loop_tail + 1) % LOOPBACK_BUF_SIZE;
    }
    UNLOCK_LOOP();
    return res;
}

void emit_string(const char *str) {
    while (*str) {
        putchar(*str++);
    }
    fflush(stdout);
}

// Global intermediate stream block-buffer infrastructure
#define IO_WRITE_BUF_SIZE 1024
static char io_write_buffer[IO_WRITE_BUF_SIZE];
static int io_write_index = 0;

void io_buf_flush(void) {
    if (io_write_index > 0 && serial_fd != INVALID_SERIAL) {
        if (loopback_mode) {
            for (int i = 0; i < io_write_index; i++) {
                loopback_write_char(io_write_buffer[i]);
            }
        }
        else {
#ifdef _WIN32
            DWORD written = 0;
            OVERLAPPED osWrite = { 0 };

            // Create a local manual-reset event for tracing this asynchronous block write
            osWrite.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL);
            if (osWrite.hEvent != NULL) {
                // Issue the overlapped write operation using our buffer index length
                if (!WriteFile(serial_fd, io_write_buffer, io_write_index, &written, &osWrite)) {
                    if (GetLastError() == ERROR_IO_PENDING) {
                        // Operation is pending; wait synchronously until the driver consumes the block
                        GetOverlappedResult(serial_fd, &osWrite, &written, TRUE);
                    }
                }
                CloseHandle(osWrite.hEvent);
            }
#else
            int res = write(serial_fd, io_write_buffer, io_write_index);
            (void)res;
#endif
        }
        io_write_index = 0;
    }
}

void io_buf_putc(char c) {
    if (io_write_index >= IO_WRITE_BUF_SIZE) {
        io_buf_flush();
    }
    io_write_buffer[io_write_index++] = c;
}

void io_buf_puts(const char *str) {
    while (*str) {
        io_buf_putc(*str++);
    }
}

void list_available_ports(void) {
    printf("Scanning for available serial ports...\n");
#ifdef _WIN32
    HKEY hKey;
    if (RegOpenKeyExA(HKEY_LOCAL_MACHINE, "HARDWARE\\DEVICEMAP\\SERIALCOMM", 0, KEY_READ, &hKey) == ERROR_SUCCESS) {
        // Zero-initialize both array structures to satisfy strict MSVC linter checks
        char valueName[256] = { 0 };
        BYTE portName[256] = { 0 };
        DWORD i = 0, nameLen, portLen, type;

        while (1) {
            nameLen = sizeof(valueName);
            portLen = sizeof(portName);

            if (RegEnumValueA(hKey, i, valueName, &nameLen, NULL, &type, portName, &portLen) == ERROR_SUCCESS) {
                printf("  -> %s (%s)\n", (char*)portName, valueName);
                i++;
            }
            else {
                break;
            }
        }
        RegCloseKey(hKey);
    }
#else
    // ... POSIX/Linux Glob Implementation remains exactly the same
    glob_t glob_results;
    int r1 = glob("/dev/ttyUSB*", 0, NULL, &glob_results);
    int r2 = glob("/dev/ttyACM*", GLOB_APPEND, NULL, &glob_results);
    int r3 = glob("/dev/ttyS*", GLOB_APPEND, NULL, &glob_results);
    if (r1 == 0 || r2 == 0 || r3 == 0) {
        for (size_t i = 0; i < glob_results.gl_pathc; i++) {
            int test_fd = open(glob_results.gl_pathv[i], O_RDWR | O_NOCTTY | O_NONBLOCK);
            if (test_fd != -1) {
                printf("  -> %s\n", glob_results.gl_pathv[i]);
                close(test_fd);
            }
        }
        globfree(&glob_results);
    }
#endif
}

// Part 3: Linux-Specific High-Speed Outbound Event Stream Translation FSM

#ifndef _WIN32
typedef enum {
    XTERM_STATE_GROUND,
    XTERM_STATE_ESC,
    XTERM_STATE_CSI,
    XTERM_STATE_MOUSE_SGR,
    XTERM_STATE_O
} XtermFsmState;

#define XTERM_PARAM_BUF_SIZE 64
static char xterm_param_buf[XTERM_PARAM_BUF_SIZE];
static int xterm_param_len = 0;
static XtermFsmState xterm_current_state = XTERM_STATE_GROUND;

static void handle_outbound_csi_sequence(char final_char) {
    xterm_param_buf[xterm_param_len] = '\0';
    char transport_buffer[128];

    if (final_char == '~') {
        int code = atoi(xterm_param_buf);
        switch (code) {
            case 11: io_buf_puts("\033OP"); io_buf_flush(); return;
            case 12: io_buf_puts("\033OQ"); io_buf_flush(); return;
            case 13: io_buf_puts("\033OR"); io_buf_flush(); return;
            case 14: io_buf_puts("\033OS"); io_buf_flush(); return;
            case 15: io_buf_puts("\033[15~"); io_buf_flush(); return;
            case 17: io_buf_puts("\033[17~"); io_buf_flush(); return;
            case 18: io_buf_puts("\033[18~"); io_buf_flush(); return;
            case 19: io_buf_puts("\033[19~"); io_buf_flush(); return;
            case 20: io_buf_puts("\033[20~"); io_buf_flush(); return;
            case 21: io_buf_puts("\033[21~"); io_buf_flush(); return;
            case 23: io_buf_puts("\033[23~"); io_buf_flush(); return;
            case 24: io_buf_puts("\033[24~"); io_buf_flush(); return;
            default: break;
        }
    }

    if (final_char >= 'A' && final_char <= 'D') {
        snprintf(transport_buffer, sizeof(transport_buffer), "\033[%s%c", xterm_param_buf, final_char);
        io_buf_puts(transport_buffer); io_buf_flush(); return;
    }

    if (final_char == 't') {
        snprintf(transport_buffer, sizeof(transport_buffer), "\033[%st", xterm_param_buf);
        io_buf_puts(transport_buffer); io_buf_flush(); return;
    }

    snprintf(transport_buffer, sizeof(transport_buffer), "\033[%s%c", xterm_param_buf, final_char);
    io_buf_puts(transport_buffer); io_buf_flush();
}

void fsm_send(char c) {
    char seq_out[128];
    switch (xterm_current_state) {
        case XTERM_STATE_GROUND:
            if (c == '\033') xterm_current_state = XTERM_STATE_ESC;
            else { io_buf_putc(c); io_buf_flush(); }
            break;
        case XTERM_STATE_ESC:
            if (c == '[') { xterm_current_state = XTERM_STATE_CSI; xterm_param_len = 0; }
            else if (c == 'O') xterm_current_state = XTERM_STATE_O;
            else { io_buf_putc('\033'); io_buf_putc(c); io_buf_flush(); xterm_current_state = XTERM_STATE_GROUND; }
            break;
        case XTERM_STATE_O:
            if (c >= 'P' && c <= 'S') { snprintf(seq_out, sizeof(seq_out), "\033O%c", c); io_buf_puts(seq_out); io_buf_flush(); }
            else { io_buf_puts("\033O"); io_buf_putc(c); io_buf_flush(); }
            xterm_current_state = XTERM_STATE_GROUND;
            break;
        case XTERM_STATE_CSI:
            if (c == '<') { xterm_current_state = XTERM_STATE_MOUSE_SGR; xterm_param_buf[xterm_param_len++] = c; }
            else if ((c >= '0' && c <= '9') || c == ';' || c == '?') {
                if (xterm_param_len < XTERM_PARAM_BUF_SIZE - 1) xterm_param_buf[xterm_param_len++] = c;
            } else if (c >= 0x40 && c <= 0x7E) { handle_outbound_csi_sequence(c); xterm_current_state = XTERM_STATE_GROUND; }
            else xterm_current_state = XTERM_STATE_GROUND;
            break;
        case XTERM_STATE_MOUSE_SGR:
            if ((c >= '0' && c <= '9') || c == ';') {
                if (xterm_param_len < XTERM_PARAM_BUF_SIZE - 1) xterm_param_buf[xterm_param_len++] = c;
            } else if (c == 'M' || c == 'm') {
                xterm_param_buf[xterm_param_len] = '\0';
                snprintf(seq_out, sizeof(seq_out), "\033[%s%c", xterm_param_buf, c);
                io_buf_puts(seq_out); io_buf_flush(); xterm_current_state = XTERM_STATE_GROUND;
            } else xterm_current_state = XTERM_STATE_GROUND;
            break;
    }
}
#endif

// Part 4: High-Speed Serial Drivers, Terminal Modes, and Shared Input Loop Execution
void set_terminal_modes(bool raw) {
    if (loopback_mode) return;
    if (raw == is_raw_mode) return;
#ifdef _WIN32
    HANDLE hIn = GetStdHandle(STD_INPUT_HANDLE); HANDLE hOut = GetStdHandle(STD_OUTPUT_HANDLE);
    if (raw) {
        GetConsoleMode(hIn, &orig_console_settings.stdin_mode); GetConsoleMode(hOut, &orig_console_settings.stdout_mode);
        SetConsoleMode(hIn, (orig_console_settings.stdin_mode & ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT)) | ENABLE_VIRTUAL_TERMINAL_INPUT);
        SetConsoleMode(hOut, orig_console_settings.stdout_mode | ENABLE_VIRTUAL_TERMINAL_PROCESSING);
        emit_string("\033[?1002h\033[?1006h");
    } else {
        emit_string("\033[?1006l\033[?1002l");
        SetConsoleMode(hIn, orig_console_settings.stdin_mode); SetConsoleMode(hOut, orig_console_settings.stdout_mode);
    }
#else
    if (raw) {
        struct termios2 raw_term;
        if (ioctl(STDIN_FILENO, TCGETS2, &orig_console_settings.stdin_orig) < 0) {
            perror("ioctl TCGETS2 failed A");
            exit (1);
        }
    
//        tcgetattr(STDIN_FILENO, &orig_console_settings.stdin_orig); 
        raw_term = orig_console_settings.stdin_orig;
        raw_term.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG); 
        raw_term.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw_term.c_cflag &= ~(CSIZE | PARENB); raw_term.c_cflag |= CS8; raw_term.c_oflag &= ~(OPOST);
        raw_term.c_cc[VMIN] = 1; 
        raw_term.c_cc[VTIME] = 0; 
        if (ioctl(STDIN_FILENO, TCSETSF2, &raw_term) < 0) {
            perror("ioctl TCGETS2 failed B");
            exit (1);
        }
        emit_string("\033[?1002h\033[?1006h");
    } else {
        emit_string("\033[?1006l\033[?1002l"); 
        if (ioctl(STDIN_FILENO, TCSETSF2, &orig_console_settings.stdin_orig)) {
            perror("ioctl TCGETS2 failed C");
            exit (1);
        }
    }
#endif
    is_raw_mode = raw;
}

serial_t open_serial(const char *port_name, int baud, bool hw_flow) {
    if (loopback_mode) return (serial_t)1;
#ifdef _WIN32
    // FIX: Open file with FILE_FLAG_OVERLAPPED to support asynchronous kernel event triggers
    HANDLE hComm = CreateFileA(port_name, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, FILE_FLAG_OVERLAPPED, NULL);
    if (hComm == INVALID_HANDLE_VALUE) return INVALID_SERIAL;
    
    DCB dcb = {0}; dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hComm, &dcb)) { CloseHandle(hComm); return INVALID_SERIAL; }
    dcb.BaudRate = baud; dcb.ByteSize = 8; dcb.StopBits = ONESTOPBIT; dcb.Parity = NOPARITY;
    dcb.fOutxCtsFlow = hw_flow ? TRUE : FALSE; dcb.fRtsControl = hw_flow ? RTS_CONTROL_HANDSHAKE : RTS_CONTROL_ENABLE;
    if (!SetCommState(hComm, &dcb)) { CloseHandle(hComm); return INVALID_SERIAL; }

    // Maintain non-blocking fallbacks for the write stream configurations
    COMMTIMEOUTS timeouts = {0};
    timeouts.ReadIntervalTimeout         = MAXDWORD; 
    timeouts.ReadTotalTimeoutMultiplier  = 0;
    timeouts.ReadTotalTimeoutConstant    = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant   = 0;
    if (!SetCommTimeouts(hComm, &timeouts)) { CloseHandle(hComm); return INVALID_SERIAL; }

    // Register interest specifically in the RX character arrival event mask
    if (!SetCommMask(hComm, EV_RXCHAR)) { CloseHandle(hComm); return INVALID_SERIAL; }

    return hComm;
#else
    int fd = open(port_name, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1) return INVALID_SERIAL;
    
    struct termios2 tio;
    if (ioctl(fd, TCGETS2, &tio) < 0) { close(fd); return INVALID_SERIAL; }
    tio.c_cflag &= ~CBAUD; tio.c_cflag |= BOTHER;
    tio.c_ispeed = baud; tio.c_ospeed = baud;
    tio.c_cflag |= (CLOCAL | CREAD); tio.c_cflag &= ~PARENB; tio.c_cflag &= ~CSTOPB;
    tio.c_cflag &= ~CSIZE; tio.c_cflag |= CS8; tio.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tio.c_oflag &= ~OPOST;
    if (hw_flow) tio.c_cflag |= CRTSCTS; else tio.c_cflag &= ~CRTSCTS;
    
    if (ioctl(fd, TCSETS2, &tio) < 0) { close(fd); return INVALID_SERIAL; }
    return fd;
#endif
}

void transmit_char(char c) {
#ifdef _WIN32
    io_buf_putc(c);
#else
    fsm_send(c);
#endif
}

void parse_bridge_stream(char c) {
    static BridgeFsmState state = STATE_GROUND; static char buffer[16]; static int idx = 0;
    switch (state) {
        case STATE_GROUND:
            if (c == '\033') { state = STATE_ESC; buffer[idx = 0] = c; }
            else { transmit_char(c); io_buf_flush(); }
            break;
        case STATE_ESC:
            if (idx < 15) buffer[++idx] = c;
            if (c == '[') state = STATE_CSI;
            else { for (int i=0; i<=idx; i++) transmit_char(buffer[i]); io_buf_flush(); state = STATE_GROUND; }
            break;
        case STATE_CSI:
            if (idx < 15) buffer[++idx] = c;
            if (c == '?') state = STATE_CSI;
            else if (c == '9') state = STATE_MATCHING_RAW;
            else { for (int i=0; i<=idx; i++) transmit_char(buffer[i]); io_buf_flush(); state = STATE_GROUND; }
            break;
        case STATE_MATCHING_RAW:
            if (idx < 15) buffer[++idx] = c;
            if (idx == 5 && memcmp(buffer, SEQ_ENTER_RAW, 6) == 0) { set_terminal_modes(true); state = STATE_GROUND; }
            else if (idx == 5 && memcmp(buffer, SEQ_ENTER_COOKED, 6) == 0) { set_terminal_modes(false); state = STATE_GROUND; }
            else if (idx >= 6) { for (int i=0; i<=idx; i++) transmit_char(buffer[i]); io_buf_flush(); state = STATE_GROUND; }
            break;
    }
}

THREAD_RETURN uart_to_stdout_thread(void *arg) {
    (void)arg; char c;
    if (loopback_mode) {
        while (1) {
            int ch = loopback_read_char();
            if (ch != -1) { putchar(ch); fflush(stdout); }
            else {
#ifdef _WIN32
                Sleep(2);
#else
                usleep(2000);
#endif
            }
        }
        return 0;
    }
#ifdef _WIN32
    OVERLAPPED osStatus = {0};
    osStatus.hEvent = CreateEvent(NULL, TRUE, FALSE, NULL); // Manual-reset, unsignaled event
    if (osStatus.hEvent == NULL) return 0;

    DWORD dwCommEvent = 0;
    
    while (1) {
        // Initiate asynchronous tracking wait
        if (!WaitCommEvent(serial_fd, &dwCommEvent, &osStatus)) {
            if (GetLastError() == ERROR_IO_PENDING) {
                // Thread suspends and waits here with ZERO CPU overhead until com0com delivers characters
                DWORD dwWait = WaitForSingleObject(osStatus.hEvent, INFINITE);
                if (dwWait != WAIT_OBJECT_0) {
                    break;
                }
            } else {
                // Handle port crash or disconnect events safely
                break;
            }
        }

        // Once signaled, continuously consume all characters currently waiting in the buffer
        DWORD br;
        while (ReadFile(serial_fd, &c, 1, &br, &osStatus)) {
            if (br > 0) {
                putchar(c);
                fflush(stdout);
            } else {
                // Buffer is drained; go back to kernel sleep
                break;
            }
        }
        
        // Alternative handle condition verification for overlapped ReadFile calls
        if (GetLastError() == ERROR_IO_PENDING) {
            WaitForSingleObject(osStatus.hEvent, INFINITE);
            if (GetOverlappedResult(serial_fd, &osStatus, &br, FALSE) && br > 0) {
                putchar(c);
                fflush(stdout);
            }
        }
        
        // Reset the event status indicator explicitly before restarting the Wait loop
        ResetEvent(osStatus.hEvent);
    }
    
    CloseHandle(osStatus.hEvent);
#else
    while (read(serial_fd, &c, 1) > 0) { putchar(c); fflush(stdout); }
#endif
    return 0;
}

// Part 5: Shutdown Management, Help Text, and Main Runtime Loop

void handle_signal_shutdown(int signum) {
    set_terminal_modes(false); io_buf_flush();
    if (serial_fd != INVALID_SERIAL && !loopback_mode) {
#ifdef _WIN32
        // Cancel outstanding asynchronous IO operations before closing the hardware handle
        PurgeComm(serial_fd, PURGE_TXABORT | PURGE_RXABORT | PURGE_TXCLEAR | PURGE_RXCLEAR);
        CloseHandle(serial_fd); 
        DeleteCriticalSection(&loop_cs);
#else
        close(serial_fd);
#endif
    }
    exit(signum);
}

void print_usage(const char *p) {
    fprintf(stderr, "Error: Invalid parameters.\n\nUsage:\n  %s -l                  List active hardware ports\n", p);
    fprintf(stderr, "  %s -k [options]        Run virtual loopback simulator test\n", p);
    fprintf(stderr, "  %s <Port> [options]    Connect to physical hardware\n\n", p);
    fprintf(stderr, "Options:\n  -b <baud>  Set target BPS (e.g. 115200, 1000000, 3000000)\n");
    fprintf(stderr, "  -r         Force active raw mode + mouse tracking immediately on start\n");
    fprintf(stderr, "  -f         Engage strict hardware RTS/CTS flow control handshaking lines\n");
}

int main(int argc, char *argv[]) {
    if (argc < 2) { print_usage(argv[0]); return 1; }
    if (strcmp(argv[1], "-l") == 0) { list_available_ports(); return 0; }
    
    char *port_name = NULL;
    if (strcmp(argv[1], "-k") == 0) loopback_mode = true;
    else { if (argv[1][0] == '-') { print_usage(argv[0]); return 1; } port_name = argv[1]; }

    int baud_rate = BAUDRATE; 
    bool start_in_raw = false; 
    bool hw_flow = false;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-b") == 0 && i + 1 < argc) {
            baud_rate = atoi(argv[++i]);
            if (baud_rate <= 0) { fprintf(stderr, "Error: Invalid speed rate configuration.\n"); return 1; }
        } else if (strcmp(argv[i], "-r") == 0) start_in_raw = true;
          else if (strcmp(argv[i], "-f") == 0) hw_flow = true;
          else { print_usage(argv[0]); return 1; }
    }

#ifdef _WIN32
    InitializeCriticalSection(&loop_cs);
#endif
    signal(SIGINT, handle_signal_shutdown); signal(SIGTERM, handle_signal_shutdown);
#ifndef _WIN32
    signal(SIGHUP, handle_signal_shutdown);
#endif

    serial_fd = open_serial(port_name, baud_rate, hw_flow);
    if (serial_fd == INVALID_SERIAL) { fprintf(stderr, "Error: Port setup failed for %s.\n", port_name ? port_name : "loopback"); return 1; }
    if (start_in_raw) set_terminal_modes(true);

    thread_t rx;
#ifdef _WIN32
    rx = CreateThread(NULL, 0, uart_to_stdout_thread, NULL, 0, NULL);
#else
    pthread_create(&rx, NULL, uart_to_stdout_thread, NULL);
#endif

    char in_char;
#ifdef _WIN32
    DWORD rl; 
    while (ReadFile(GetStdHandle(STD_INPUT_HANDLE), &in_char, 1, &rl, NULL) && rl > 0) {
        // In raw mode, Ctrl+C arrives as literal ASCII byte value 0x03
        if (is_raw_mode && in_char == 0x03) {
            handle_signal_shutdown(SIGINT);
        }
        parse_bridge_stream(in_char);
    }
#else
    while (read(STDIN_FILENO, &in_char, 1) > 0) {
        // In raw mode, Ctrl+C arrives as literal ASCII byte value 0x03
        if (is_raw_mode && in_char == 0x03) {
            handle_signal_shutdown(SIGINT);
        }
        parse_bridge_stream(in_char);
    }
#endif    

    if (loopback_mode) {
#ifdef _WIN32
        Sleep(1);
#else
        usleep(1000);
#endif
    }
    set_terminal_modes(false); io_buf_flush();
    return 0;
}
