#include <errno.h>
#include <stdio.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <netinet/in.h>
#include <sys/socket.h>
#include <sys/un.h>

#include "say.h"

static int systemd_fd = -1;
static const char *sd_unix_path = NULL;
/*
 * Open connection with systemd daemon (using unix socket located in
 * "NOTIFY_SOCKET" environmnent variable)
 *
 * @return  1 on non-systemd plaformts
 * @return -1 on errors
 * @return  1 on sucess
 */
int systemd_init() {
	sd_unix_path = getenv("NOTIFY_SOCKET");
	if (sd_unix_path == NULL) {
		say_info("systemd: NOTIFY_SOCKET variable is empty, skipping");
		return 1;
	}
	struct sockaddr_un sa = { 0 };
	if (strlen(sd_unix_path) >= sizeof(sa.sun_path)) {
		say_error("systemd: NOTIFY_SOCKET is longer that MAX_UNIX_PATH");
		return -1;
	}
	strncpy(sa.sun_path, sd_unix_path, sizeof(sa.sun_path));
	if ((systemd_fd = socket(AF_UNIX, SOCK_DGRAM | SOCK_CLOEXEC, 0)) == -1) {
		say_syserror("systemd: failed to create unix socket");
		return -1;
	}
	/*
	if (connect(systemd_fd, (struct sockaddr *)&sa,
		    strlen(sa.sun_path) + sizeof(sa.sun_family)) == -1) {
		say_syserror("systemd: failed to connect to '%s'", sd_unix_path);
		close(systemd_fd);
		return -1;
	}
	*/
	return 0;
}

/* Close connection with systemd daemon */
void systemd_shutdown() {
	if (systemd_fd > 0)
		close(systemd_fd);
}

/*
 * @return  0 on non-systemd platforms
 * @return -1 on errors (more information in errno)
 * @return >0 on ok
 */
int systemd_notify(const char *state) {
	if (systemd_fd == -1 || sd_unix_path == NULL)
		return 0;

	struct sockaddr_un sa = {
		.sun_family = AF_UNIX,
	};
	struct iovec vec = {
		.iov_base = (char *)state,
		.iov_len  = (size_t )strlen(state)
	};
	struct msghdr msg = {
		.msg_iov = &vec,
		.msg_iovlen = 1,
		.msg_name = &sa,
	};

	strncpy(sa.sun_path, sd_unix_path, sizeof(sa.sun_path));
	if (sa.sun_path[0] == '@')
		sa.sun_path[0] = '\0';

	msg.msg_namelen = sizeof(sa.sun_family) + strlen(sa.sun_path);

	ssize_t sent = sendmsg(systemd_fd, &msg, MSG_NOSIGNAL);
	if (sent == -1) {
		say_syserror("systemd: failed to send '%s'", state);
		return -1;
	}
	return sent;
}

/*
 * @return  0 on non-systemd platforms
 * @return -1 on errors (more information in errno)
 * @return >0 on ok
 */
int systemd_vsnotify(const char *format, va_list ap) {
	if (systemd_fd == -1 || sd_unix_path == NULL)
		return 0;
	char *buf = NULL;
	int rv = vasprintf(&buf, format, ap);
	if (rv < 0 || buf == NULL) {
		errno = ENOMEM;
		return -1;
	}
	rv = systemd_notify(buf);
	free(buf);
	return rv;
}

/*
 * @return  0 on non-systemd platforms
 * @return -1 on errors (more information in errno)
 * @return >0 on ok
 */
__attribute__ ((format (printf, 1, 2))) int
systemd_snotify(const char *format, ...) {
	if (systemd_fd == -1 || sd_unix_path == NULL)
		return 0;
	va_list args;
	va_start(args, format);
	size_t res = systemd_vsnotify(format, args);
	va_end(args);
	return res;
}
