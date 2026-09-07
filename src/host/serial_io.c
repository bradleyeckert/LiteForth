#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "serial_io.h"

#if defined(_WIN32) || defined(_WIN64)
    #include <windows.h>
    #include <io.h>
    #define TARGET_ISATTY() _isatty(0)
#else
    #include <unistd.h>
    #include <fcntl.h>
    #include <termios.h>
    #include <sys/select.h>
    #define TARGET_ISATTY() isatty(STDIN_FILENO)
#endif

// Global internal handlers
static int is_terminal_mode = 1;

#if defined(_WIN32) || defined(_WIN64)
    static HANDLE hCommPort = INVALID_HANDLE_VALUE;
#else
    static int comm_fd = -1;
#endif

int set_terminal_mode(int enable) {
    static int is_initialized = 0;

    if (!TARGET_ISATTY()) {
        return ERR_NOT_A_TTY;
    }

#if defined(_WIN32) || defined(_WIN64)
    static DWORD orig_input_mode;
    HANDLE hInput = GetStdHandle(STD_INPUT_HANDLE);
    
    if (hInput == INVALID_HANDLE_VALUE) {
        return ERR_INVALID_HANDLE;
    }

    if (!is_initialized) {
        if (!GetConsoleMode(hInput, &orig_input_mode)) {
            return ERR_GET_STATE_FAILED;
        }
        is_initialized = 1;
    }

    if (enable) {
        DWORD raw_mode = orig_input_mode;
        raw_mode &= ~(ENABLE_LINE_INPUT | ENABLE_ECHO_INPUT | ENABLE_PROCESSED_INPUT);
        if (!SetConsoleMode(hInput, raw_mode)) {
            return ERR_SET_STATE_FAILED;
        }
    } else {
        if (!SetConsoleMode(hInput, orig_input_mode)) {
            return ERR_SET_STATE_FAILED;
        }
    }
#else
    static struct termios orig_termios;
    
    if (!is_initialized) {
        if (tcgetattr(STDIN_FILENO, &orig_termios) < 0) {
            return ERR_GET_STATE_FAILED;
        }
        is_initialized = 1;
    }

    if (enable) {
        struct termios raw = orig_termios;
        raw.c_iflag &= ~(BRKINT | ICRNL | INPCK | ISTRIP | IXON);
        raw.c_oflag &= ~(OPOST);
        raw.c_cflag |= (CS8);
        raw.c_lflag &= ~(ECHO | ICANON | IEXTEN | ISIG);
        
        raw.c_cc[VMIN] = 1;
        raw.c_cc[VTIME] = 0;

        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw) < 0) {
            return ERR_SET_STATE_FAILED;
        }
    } else {
        if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &orig_termios) < 0) {
            return ERR_SET_STATE_FAILED;
        }
    }
#endif

    return SERIAL_SUCCESS;
}

int serial_open(char* name, int baudrate) {
    if (strcmp(name, "TERM") == 0) {
        is_terminal_mode = 1;
        return SERIAL_SUCCESS;
    }

    is_terminal_mode = 0;

#if defined(_WIN32) || defined(_WIN64)
    char port_path[MAX_PATH];
    snprintf(port_path, sizeof(port_path), "\\\\.\\%s", name);

    hCommPort = CreateFileA(port_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hCommPort == INVALID_HANDLE_VALUE) {
        return ERR_PORT_OPEN_FAILED;
    }

    DCB dcb;
    SecureZeroMemory(&dcb, sizeof(DCB));
    dcb.DCBlength = sizeof(DCB);

    if (!GetCommState(hCommPort, &dcb)) {
        CloseHandle(hCommPort);
        hCommPort = INVALID_HANDLE_VALUE;
        return ERR_GET_STATE_FAILED;
    }

    dcb.BaudRate = (DWORD)baudrate;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    if (!SetCommState(hCommPort, &dcb)) {
        CloseHandle(hCommPort);
        hCommPort = INVALID_HANDLE_VALUE;
        return ERR_SET_STATE_FAILED;
    }

    COMMTIMEOUTS timeouts = { MAXDWORD, 0, 0, 0, 0 };
    SetCommTimeouts(hCommPort, &timeouts);
#else
    comm_fd = open(name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (comm_fd < 0) {
        return ERR_PORT_OPEN_FAILED;
    }

    struct termios tty;
    if (tcgetattr(comm_fd, &tty) < 0) {
        close(comm_fd);
        comm_fd = -1;
        return ERR_GET_STATE_FAILED;
    }

    speed_t speed;
    switch (baudrate) {
        case 9600:    speed = B9600;    break;
        case 115200:  speed = B115200;  break;
#ifdef B1000000
        case 1000000: speed = B1000000; break;
#endif
#ifdef B2000000
        case 2000000: speed = B2000000; break;
#endif
#ifdef B3000000
        case 3000000: speed = B3000000; break;
#endif
        default:      speed = B9600;    break;
    }

    cfsetispeed(&tty, speed);
    cfsetospeed(&tty, speed);

    tty.c_cflag |= (CLOCAL | CREAD | CS8);
    tty.c_cflag &= ~(PARENB | CSTOPB | CRTSCTS);
    tty.c_lflag &= ~(ICANON | ECHO | ECHOE | ISIG);
    tty.c_iflag &= ~(IXON | IXOFF | IXANY | ICRNL);
    tty.c_oflag &= ~OPOST;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;

    if (tcsetattr(comm_fd, TCSANOW, &tty) < 0) {
        close(comm_fd);
        comm_fd = -1;
        return ERR_SET_STATE_FAILED;
    }
#endif

    return SERIAL_SUCCESS;
}

void serial_close(void) {
#if defined(_WIN32) || defined(_WIN64)
    if (hCommPort != INVALID_HANDLE_VALUE) {
        CloseHandle(hCommPort);
        hCommPort = INVALID_HANDLE_VALUE;
    }
#else
    if (comm_fd >= 0) {
        close(comm_fd);
        comm_fd = -1;
    }
#endif
    is_terminal_mode = 1;
}

int serial_ready(void) {
    if (is_terminal_mode) {
        if (!TARGET_ISATTY()) return ERR_NOT_A_TTY;
#if defined(_WIN32) || defined(_WIN64)
        return _kbhit() ? 1 : 0;
#else
        fd_set read_fds;
        struct timeval timeout = {0, 0};
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        return select(STDIN_FILENO + 1, &read_fds, NULL, NULL, &timeout) > 0 ? 1 : 0;
#endif
    }

#if defined(_WIN32) || defined(_WIN64)
    if (hCommPort == INVALID_HANDLE_VALUE) return ERR_INVALID_HANDLE;
    DWORD errors;
    COMSTAT stat;
    if (ClearCommError(hCommPort, &errors, &stat)) {
        return (stat.cbInQue > 0) ? 1 : 0;
    }
    return ERR_IO_CHECK_FAILED;
#else
    if (comm_fd < 0) return ERR_INVALID_HANDLE;
    fd_set read_fds;
    struct timeval timeout = {0, 0};
    FD_ZERO(&read_fds);
    FD_SET(comm_fd, &read_fds);
    int res = select(comm_fd + 1, &read_fds, NULL, NULL, &timeout);
    if (res < 0) return ERR_IO_CHECK_FAILED;
    return (res > 0) ? 1 : 0;
#endif
}

int serial_busy(void) {
    if (is_terminal_mode) {
        return 0; 
    }

#if defined(_WIN32) || defined(_WIN64)
    if (hCommPort == INVALID_HANDLE_VALUE) return ERR_INVALID_HANDLE;
    DWORD errors;
    COMSTAT stat;
    if (ClearCommError(hCommPort, &errors, &stat)) {
        return 0; 
    }
    return ERR_IO_CHECK_FAILED;
#else
    if (comm_fd < 0) return ERR_INVALID_HANDLE;
    fd_set write_fds;
    struct timeval timeout = {0, 0};
    FD_ZERO(&write_fds);
    FD_SET(comm_fd, &write_fds);
    
    int res = select(comm_fd + 1, NULL, &write_fds, NULL, &timeout);
    if (res < 0) return ERR_IO_CHECK_FAILED;
    
    return (res > 0) ? 0 : 1;
#endif
}

int serial_getc(void) {
    if (is_terminal_mode) {
        return fgetc(stdin);
    }

#if defined(_WIN32) || defined(_WIN64)
    if (hCommPort == INVALID_HANDLE_VALUE) return EOF;
    unsigned char ch;
    DWORD bytes_read = 0;
    if (ReadFile(hCommPort, &ch, 1, &bytes_read, NULL) && bytes_read == 1) {
        return (int)ch;
    }
    return EOF;
#else
    if (comm_fd < 0) return EOF;
    unsigned char ch;
    ssize_t result = read(comm_fd, &ch, 1);
    if (result == 1) {
        return (int)ch;
    }
    return EOF;
#endif
}

int serial_putc(int c) {
    if (is_terminal_mode) {
        int res = fputc(c, stdout);
        fflush(stdout);
        return res;
    }

#if defined(_WIN32) || defined(_WIN64)
    if (hCommPort == INVALID_HANDLE_VALUE) return EOF;
    unsigned char ch = (unsigned char)c;
    DWORD bytes_written = 0;
    if (WriteFile(hCommPort, &ch, 1, &bytes_written, NULL) && bytes_written == 1) {
        return c;
    }
    return EOF;
#else
    if (comm_fd < 0) return EOF;
    unsigned char ch = (unsigned char)c;
    ssize_t result = write(comm_fd, &ch, 1);
    if (result == 1) {
        return c;
    }
    return EOF;
#endif
}
