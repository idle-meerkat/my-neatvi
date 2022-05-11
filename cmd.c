#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include "vi.h"

static int cmd_make(char **argv, int *ifd, int *ofd)
{
	int pid;
	int pipefds0[2];
	int pipefds1[2];
	if (ifd)
		pipe(pipefds0);
	if (ofd)
		pipe(pipefds1);
	if (!(pid = fork())) {
		if (ifd) {		/* setting up stdin */
			close(0);
			dup(pipefds0[0]);
			close(pipefds0[1]);
			close(pipefds0[0]);
		}
		if (ofd) {		/* setting up stdout */
			close(1);
			dup(pipefds1[1]);
			close(pipefds1[0]);
			close(pipefds1[1]);
		}
		execvp(argv[0], argv);
		exit(1);
	}
	if (ifd)
		close(pipefds0[0]);
	if (ofd)
		close(pipefds1[1]);
	if (pid < 0) {
		if (ifd)
			close(pipefds0[1]);
		if (ofd)
			close(pipefds1[0]);
		return -1;
	}
	if (ifd)
		*ifd = pipefds0[1];
	if (ofd)
		*ofd = pipefds1[0];
	return pid;
}

/* execute a command; process input if iproc and process output if oproc */
char *cmd_pipe(char *cmd, char *ibuf, int oproc)
{
	int pid;
	char *argv[] = {"/bin/sh", "-c", cmd, NULL};
	struct pollfd fds[2];
	struct sbuf *sb = NULL;
	char buf[512];
	int ifd = -1, ofd = -1;
	int nw = 0;
	int slen = ibuf ? strlen(ibuf) : 0;

	pid = cmd_make(argv, slen ? &ifd : 0, oproc ? &ofd : 0);
	if (pid <= 0)
		return NULL;
	if (oproc)
		sb = sbuf_make();
	if (!slen) {
		signal(SIGINT, SIG_IGN);
		term_done();
	}
	fcntl(ifd, F_SETFL, fcntl(ifd, F_GETFL, 0) | O_NONBLOCK);
	fds[0].fd = ofd;
	fds[0].events = POLLIN;
	fds[1].fd = ifd;
	fds[1].events = POLLOUT;
	while ((fds[0].fd >= 0 || fds[1].fd >= 0) && poll(fds, 2, 200) >= 0) {
		if (fds[0].revents & POLLIN) {
			int ret = read(fds[0].fd, buf, sizeof(buf));
			if (ret > 0)
				sbuf_mem(sb, buf, ret);
			if (ret <= 0) {
				close(fds[0].fd);
				fds[0].fd = -1;
			}
			continue;
		}
		if (fds[1].revents & POLLOUT) {
			int ret = write(fds[1].fd, ibuf + nw, slen - nw);
			if (ret > 0)
				nw += ret;
			if (ret <= 0 || nw == slen) {
				close(fds[1].fd);
				fds[1].fd = -1;
			}
			continue;
		}
		if (fds[0].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			close(fds[0].fd);
			fds[0].fd = -1;
		}
		if (fds[1].revents & (POLLERR | POLLHUP | POLLNVAL)) {
			close(fds[1].fd);
			fds[1].fd = -1;
		}
	}
	close(fds[0].fd);
	close(fds[1].fd);
	waitpid(pid, NULL, 0);
	if (!slen) {
		term_init();
		signal(SIGINT, SIG_DFL);
	}
	if (oproc)
		return sbuf_done(sb);
	return NULL;
}

int cmd_exec(char *cmd)
{
	cmd_pipe(cmd, NULL, 0);
	return 0;
}
