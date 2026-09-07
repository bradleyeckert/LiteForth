#include "termio.h"
#include <stdio.h>
#include <string.h>
#include <stdbool.h>

#ifdef _WIN32
#include <windows.h>
#include <conio.h>
#else
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <termios.h> 

#ifndef strncpy_s
static int strncpy_s(char* dest, size_t dest_sz, const char* src, size_t max_count) {
    if (!dest || dest_sz == 0 || !src) return -1;
    size_t copy_len = strlen(src);
    if (copy_len > max_count) copy_len = max_count;
    if (copy_len >= dest_sz) return -1;

    memcpy(dest, src, copy_len);
    dest[copy_len] = '\0';
    return 0;
}
#endif
#endif

#define BUFFER_SIZE 256
static unsigned char input_buffer[BUFFER_SIZE];
static int head = 0;
static int tail = 0;

static bool is_terminal = false;

#ifdef _WIN32
static HANDLE hComm = INVALID_HANDLE_VALUE;
#else
static int comm_fd = -1;
static pthread_t thread_id;
static pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
static volatile bool run_thread = false;

static speed_t get_posix_baud(int baud) {
    switch (baud) {
    case 50:      return B50;
    case 75:      return B75;
    case 110:     return B110;
    case 134:     return B134;
    case 150:     return B150;
    case 200:     return B200;
    case 300:     return B300;
    case 600:     return B600;
    case 1200:    return B1200;
    case 1800:    return B1800;
    case 2400:    return B2400;
    case 4800:    return B4800;
    case 9600:    return B9600;
    case 19200:   return B19200;
    case 38400:   return B38400;
    case 57600:   return B57600;
    case 115200:  return B115200;
    case 1000000: return B1000000;
    case 2000000: return B2000000;
    case 3000000: return B3000000;
    default:      return B9600;
    }
}

static void* terminal_thread_func(void* arg) {
    (void)arg;
    while (run_thread) {
        int ch = getchar();
        if (ch == EOF) break;

        pthread_mutex_lock(&buffer_mutex);
        int next_head = (head + 1) % BUFFER_SIZE;
        if (next_head != tail) {
            input_buffer[head] = (unsigned char)ch;
            head = next_head;
        }
        pthread_mutex_unlock(&buffer_mutex);
    }
    return NULL;
}
#endif

int kbopen(char* port_name, int baud_rate) {
    if (!port_name) return KB_ERR_INVALID_PARAM;

    if (strcmp(port_name, "TERM") == 0) {
        is_terminal = true;
#ifndef _WIN32
        head = 0;
        tail = 0;
        run_thread = true;
        if (pthread_create(&thread_id, NULL, terminal_thread_func, NULL) != 0) {
            run_thread = false;
            return KB_ERR_THREAD_CREATE;
        }
#endif
        return KB_SUCCESS;
    }

    is_terminal = false;

#ifdef _WIN32
    char full_path[MAX_PATH];
    if (strncmp(port_name, "COM", 3) == 0 || strncmp(port_name, "com", 3) == 0) {
        snprintf(full_path, sizeof(full_path), "\\\\.\\%s", port_name);
    }
    else {
        if (strncpy_s(full_path, sizeof(full_path), port_name, _TRUNCATE) != 0) {
            return KB_ERR_INVALID_PARAM;
        }
    }

    hComm = CreateFileA(full_path, GENERIC_READ | GENERIC_WRITE, 0, NULL, OPEN_EXISTING, 0, NULL);
    if (hComm == INVALID_HANDLE_VALUE) return KB_ERR_OPEN_FAILED;

    DCB dcbSerialParams = { 0 };
    dcbSerialParams.DCBlength = sizeof(dcbSerialParams);
    if (!GetCommState(hComm, &dcbSerialParams)) {
        CloseHandle(hComm);
        hComm = INVALID_HANDLE_VALUE;
        return KB_ERR_CFG_FAILED;
    }

    dcbSerialParams.BaudRate = baud_rate;
    dcbSerialParams.ByteSize = 8;
    dcbSerialParams.StopBits = ONESTOPBIT;
    dcbSerialParams.Parity = NOPARITY;

    if (!SetCommState(hComm, &dcbSerialParams)) {
        CloseHandle(hComm);
        hComm = INVALID_HANDLE_VALUE;
        return KB_ERR_CFG_FAILED;
    }

    COMMTIMEOUTS timeouts = { 0 };
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    SetCommTimeouts(hComm, &timeouts);
    return KB_SUCCESS;
#else
    comm_fd = open(port_name, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (comm_fd < 0) return KB_ERR_OPEN_FAILED;

    struct termios tty;
    if (tcgetattr(comm_fd, &tty) != 0) {
        close(comm_fd);
        comm_fd = -1;
        return KB_ERR_CFG_FAILED;
    }

    speed_t speed = get_posix_baud(baud_rate);
    cfsetospeed(&tty, speed);
    cfsetispeed(&tty, speed);

    tty.c_cflag = (tty.c_cflag & ~CSIZE) | CS8;
    tty.c_iflag &= ~IGNBRK;
    tty.c_lflag = 0;
    tty.c_oflag = 0;
    tty.c_cc[VMIN] = 0;
    tty.c_cc[VTIME] = 0;
    tty.c_cflag |= (CLOCAL | CREAD);

    if (tcsetattr(comm_fd, TCSANOW, &tty) != 0) {
        close(comm_fd);
        comm_fd = -1;
        return KB_ERR_CFG_FAILED;
    }
    return KB_SUCCESS;
#endif
}

int kbfull(void) {
#ifdef _WIN32
    if (is_terminal) {
        return _kbhit() ? -1 : 0;
    }
    if (hComm == INVALID_HANDLE_VALUE) return 0;
    COMSTAT status;
    DWORD errors;
    ClearCommError(hComm, &errors, &status);
    return (status.cbInQue > 0) ? -1 : 0;
#else
    if (is_terminal) {
        pthread_mutex_lock(&buffer_mutex);
        int has_data = (head != tail);
        pthread_mutex_unlock(&buffer_mutex);
        return has_data ? -1 : 0;
    }

    if (comm_fd < 0) return 0;
    fd_set rfds;
    struct timeval tv = { 0, 0 };
    FD_ZERO(&rfds);
    FD_SET(comm_fd, &rfds);
    return (select(comm_fd + 1, &rfds, NULL, NULL, &tv) > 0) ? -1 : 0;
#endif
}

int kbready(void) {
    return -1;
}

int kbgetc(void) {
    if (kbfull() != -1) return -1;

#ifdef _WIN32
    if (is_terminal) {
        return _getch();
    }
    DWORD bytesRead = 0;
    unsigned char ch = 0;
    if (ReadFile(hComm, &ch, 1, &bytesRead, NULL) && bytesRead > 0) {
        return (int)ch;
    }
    return -1;
#else
    if (is_terminal) {
        pthread_mutex_lock(&buffer_mutex);
        int ch = -1;
        if (head != tail) {
            ch = input_buffer[tail];
            tail = (tail + 1) % BUFFER_SIZE;
        }
        pthread_mutex_unlock(&buffer_mutex);
        return ch;
    }

    unsigned char ch;
    if (read(comm_fd, &ch, 1) > 0) {
        return (int)ch;
    }
    return -1;
#endif
}

void kbputc(char c) {
    if (is_terminal) {
        putchar(c);
        fflush(stdout);
        return;
    }

#ifdef _WIN32
    if (hComm != INVALID_HANDLE_VALUE) {
        DWORD bytesWritten = 0;
        WriteFile(hComm, &c, 1, &bytesWritten, NULL);
    }
#else
    if (comm_fd >= 0) {
        ssize_t result = write(comm_fd, &c, 1);
        (void)result;
    }
#endif
}

void kbclose(void) {
#ifdef _WIN32
    if (!is_terminal && hComm != INVALID_HANDLE_VALUE) {
        CloseHandle(hComm);
        hComm = INVALID_HANDLE_VALUE;
    }
#else
    if (is_terminal) {
        if (run_thread) {
            run_thread = false;
            pthread_cancel(thread_id);
            pthread_join(thread_id, NULL);
        }
    }
    else if (comm_fd >= 0) {
        close(comm_fd);
        comm_fd = -1;
    }
#endif
    is_terminal = false;
}
