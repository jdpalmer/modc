/*
 * POSIX <termios.h> for %C — curated layouts matching host libc ABI.
 * Used by tty. Flag macros needed for open-coding raw if desired;
 * prefer cfmakeraw + TCSANOW.
 */
#pragma once

#include <stddef.h>

#ifdef __APPLE__
typedef unsigned long tcflag_t;
typedef unsigned long speed_t;
typedef unsigned char cc_t;
#define NCCS 20
struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};
#else
/* Linux glibc / musl x86_64 & aarch64. */
typedef unsigned int tcflag_t;
typedef unsigned int speed_t;
typedef unsigned char cc_t;
#define NCCS 32
struct termios {
	tcflag_t c_iflag;
	tcflag_t c_oflag;
	tcflag_t c_cflag;
	tcflag_t c_lflag;
	cc_t c_line;
	cc_t c_cc[NCCS];
	speed_t c_ispeed;
	speed_t c_ospeed;
};
#endif

#define TCSANOW 0

int tcgetattr(int fd, struct termios *t);
int tcsetattr(int fd, int optional_actions, const struct termios *t);
void cfmakeraw(struct termios *t);
