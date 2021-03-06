/*
* This file is part of the Bus Pirate project (http://code.google.com/p/the-bus-pirate/).
*
* Written and maintained by the Bus Pirate project and http://dangerousprototypes.com
*
* To the extent possible under law, the project has
* waived all copyright and related or neighboring rights to Bus Pirate. This
* work is published from United States.
*
* For details see: http://creativecommons.org/publicdomain/zero/1.0/.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
*/
/*
* OS independent serial interface
*
* Heavily based on Pirate-Loader:
* http://the-bus-pirate.googlecode.com/svn/trunk/bootloader-v4/pirate-loader/source/pirate-loader.c
*
*/

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "blfwk/serial.h"

int serial_setup(int fd, speed_t speed)
{
#if defined(WIN32)
    COMMTIMEOUTS timeouts;
    DCB dcb = { 0 };
    HANDLE hCom = (HANDLE)fd;

    dcb.DCBlength = sizeof(dcb);
    if (!GetCommState(hCom, &dcb))
    {
        return -1;
    }

    dcb.BaudRate = speed;
    dcb.ByteSize = 8;
    dcb.Parity = NOPARITY;
    dcb.StopBits = ONESTOPBIT;

    if (!SetCommState(hCom, &dcb))
    {
        return -1;
    }

    // These timeouts mean:
    // read: return immediately with whatever data is available, if any
    // write: timeouts not used
    // reference: http://www.robbayer.com/files/serial-win.pdf
    timeouts.ReadIntervalTimeout = MAXDWORD;
    timeouts.ReadTotalTimeoutMultiplier = 0;
    timeouts.ReadTotalTimeoutConstant = 0;
    timeouts.WriteTotalTimeoutMultiplier = 0;
    timeouts.WriteTotalTimeoutConstant = 0;

    if (!SetCommTimeouts(hCom, &timeouts))
    {
        return -1;
    }

#elif defined(LINUX)
    struct termios tty;
    int custom_speed = 0;

    memset(&tty, 0, sizeof(tty));
    cfmakeraw(&tty);

    tty.c_cflag &= ~(PARENB | CSTOPB | CSIZE);
    tty.c_cflag |= (CS8 | CLOCAL | CREAD);
    tty.c_oflag = 0;
    tty.c_lflag = 0;

    switch (speed)
    {
        case 9600:
            speed = B9600;
            break;
        case 19200:
            speed = B19200;
            break;
        case 38400:
            speed = B38400;
            break;
        case 57600:
            speed = B57600;
            break;
        case 115200:
            speed = B115200;
            break;
        case 230400:
            speed = B230400;
            break;
        default:
            custom_speed = 1;
            break;
    }

    if (custom_speed)
    {
        if (cfsetospeed_custom(&tty, speed) < 0)
        {
            return -1;
        }
        if (cfsetispeed_custom(&tty, speed) < 0)
        {
            return -1;
        }
    }
    else
    {
        if (cfsetospeed(&tty, speed) < 0)
        {
            return -1;
        }
        if (cfsetispeed(&tty, speed) < 0)
        {
            return -1;
        }
    }

    // Completely non-blocking read
    // VMIN = 0 and VTIME = 0
    // Completely non-blocking read
    // reference: http://www.unixwiz.net/techtips/termios-vmin-vtime.html
    tty.c_cc[VTIME] = 0;
    tty.c_cc[VMIN] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &tty) < 0)
    {
        return -1;
    }

#elif defined(MACOSX)
    struct termios tty;

    memset(&tty, 0, sizeof(tty));

    if (tcgetattr(fd, &tty) < 0)
    {
        return -1;
    }

    tty.c_cflag |= (CLOCAL | CREAD); // Enable local mode and serial data receipt

    if (tcsetattr(fd, TCSAFLUSH, &tty) < 0)
    {
        return -1;
    }

    return ioctl(fd, IOSSIOSPEED, &speed);
#endif // WIN32

    return 0;
}

int serial_set_read_timeout(int fd, uint32_t timeoutMs)
{
#if defined(WIN32)
    COMMTIMEOUTS timeouts;
    HANDLE hCom = (HANDLE)fd;

    // These timeouts mean:
    // read: return if:
    //  1. Inter-character timeout exceeds ReadIntervalTimeout
    //  2. Total timeout exceeds (ReadIntervalTimeout*ReadTotalTimeoutMultiplier*number of characters) +
    //  ReadTotalTimeoutConstant
    // In practice it seems that no matter how many characters you ask for, if no characters appear on the interface
    // then
    // only ReadTotalTimeoutConstant applies.
    // write: timeouts not used
    // reference: http://www.robbayer.com/files/serial-win.pdf
    if (timeoutMs != 0)
    {
        timeouts.ReadIntervalTimeout = 1000;
        timeouts.ReadTotalTimeoutMultiplier = 10;
        timeouts.ReadTotalTimeoutConstant = timeoutMs;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 0;
    }
    else
    {
        // Need a seperate case for timeoutMs == 0
        // setting all these values to 0 results in no timeout
        // so set them to a minimum value, this will return immediately
        // if there is no data available
        timeouts.ReadIntervalTimeout = 1;
        timeouts.ReadTotalTimeoutMultiplier = 1;
        timeouts.ReadTotalTimeoutConstant = 1;
        timeouts.WriteTotalTimeoutMultiplier = 0;
        timeouts.WriteTotalTimeoutConstant = 0;
    }

    if (!SetCommTimeouts(hCom, &timeouts))
    {
        return -1;
    }

#elif defined(LINUX)
    struct termios tty;

    if (tcgetattr(fd, &tty) < 0)
    {
        return -1;
    }

    // Completely non-blocking read
    // VMIN = 0 and VTIME > 0
    // Pure timed read
    // reference: http://www.unixwiz.net/techtips/termios-vmin-vtime.html
    if (timeoutMs && (timeoutMs < 100))
    {
        // since the lowest resolution this handles is .1 seconds we will set it to that for any non zero
        // timeout value less than 100ms
        tty.c_cc[VTIME] = 1;
    }
    else
    {
        tty.c_cc[VTIME] = (timeoutMs / 100); // in 0.1 sec intervals
    }

    tty.c_cc[VMIN] = 0;

    if (tcsetattr(fd, TCSAFLUSH, &tty) < 0)
    {
        return -1;
    }

#elif defined(MAXOSX)
    // Setting tty.c_cc seems to hang up the serial driver on El Capitan,
    // so now using IOSSDATALAT ioctl with timeout set to 1 us.
    uint32_t us = 1UL;
    if (ioctl(fd, IOSSDATALAT, &us) < 0)
    {
        return -1;
    }
#endif // WIN32
    return 0;
}

int serial_write(int fd, char *buf, int size)
{
#ifdef WIN32
    HANDLE hCom = (HANDLE)fd;
    unsigned long bwritten = 0;

    if (!WriteFile(hCom, buf, size, &bwritten, NULL))
    {
        return 0;
    }
    else
    {
        return bwritten;
    }
#else
    return write(fd, buf, size);
#endif
}

int serial_read(int fd, char *buf, int size)
{
#ifdef WIN32
    HANDLE hCom = (HANDLE)fd;
    unsigned long bread = 0;

    if (!ReadFile(hCom, buf, size, &bread, NULL))
    {
        return 0;
    }
    else
    {
        return bread;
    }
#else
    int len = 0;
    int ret = 0;
    int timeout = 0;

    while (len < size)
    {
        ret = read(fd, buf + len, size - len);
        if (ret == -1)
        {
            return -1;
        }

        if (ret == 0)
        {
            timeout++;

            if (timeout >= 10)
            {
                break;
            }

            continue;
        }

        len += ret;
    }

    return len;
#endif
}

int serial_open(char *port)
{
    int fd;
#ifdef WIN32
    static char full_path[32] = { 0 };

    HANDLE hCom = NULL;

    if (port[0] != '\\')
    {
        _snprintf(full_path, sizeof(full_path) - 1, "\\\\.\\%s", port);
        port = full_path;
    }

#pragma warning(suppress : 6053)
    hCom = CreateFileA(port, GENERIC_WRITE | GENERIC_READ, 0, NULL, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);

    if (!hCom || hCom == INVALID_HANDLE_VALUE)
    {
        fd = -1;
    }
    else
    {
        fd = (int)hCom;
    }
#else
    fd = open(port, O_RDWR | O_NOCTTY | O_NDELAY);
    if (fd == -1)
    {
        fprintf(stderr, "Could not open serial port.");
        return -1;
    }
#endif
    return fd;
}

int serial_close(int fd)
{
#ifdef WIN32
    HANDLE hCom = (HANDLE)fd;

    CloseHandle(hCom);
#else
    close(fd);
#endif
    return 0;
}
