/*
 * POSIX <poll.h> for %C — curated; host libc at link.
 */
#pragma once

struct pollfd {
	int fd;
	short events;
	short revents;
};

#define POLLIN 0x0001
#define POLLOUT 0x0004
#define POLLERR 0x0008
#define POLLHUP 0x0010

int poll(struct pollfd *fds, size_t nfds, int timeout);
