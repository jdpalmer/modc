/*
 * POSIX <sys/ioctl.h> for %C — curated subset for tty (TIOCGWINSZ).
 */
#pragma once

#include <stddef.h>

struct winsize {
	unsigned short ws_row;
	unsigned short ws_col;
	unsigned short ws_xpixel;
	unsigned short ws_ypixel;
};

#ifdef __APPLE__
#define TIOCGWINSZ 1074295912
#else
/* Linux: _IOR('T', 0x13, struct winsize) */
#define TIOCGWINSZ 21523
#endif

int ioctl(int fd, unsigned long req, ...);
