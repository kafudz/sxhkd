/* Copyright (c) 2013, Bastien Dejean
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this
 *    list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright notice,
 *    this list of conditions and the following disclaimer in the documentation
 *    and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
 * SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <errno.h>
#include <signal.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include "sxhkd.h"

void warn(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
}

__attribute__((noreturn))
void err(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	vfprintf(stderr, fmt, ap);
	va_end(ap);
	exit(EXIT_FAILURE);
}

void *emalloc(size_t size)
{
	void *p = malloc(size);
	if (p == NULL)
		err("Out of memory.\n");
	return p;
}

void *ecalloc(size_t nmemb, size_t size)
{
	void *p = calloc(nmemb, size);
	if (p == NULL)
		err("Out of memory.\n");
	return p;
}

void run(char *command, bool sync)
{
	char *cmd[] = {shell, "-c", command, NULL};
	spawn(cmd, sync);
}

void spawn(char *cmd[], bool sync)
{
	pid_t pid = fork();
	if (pid == -1) {
		warn("Failed to fork: %s.\n", strerror(errno));
		return;
	}
	if (pid == 0) {
		/* The daemon blocks its signals while it is not waiting for
		 * events; the spawned command must not inherit that mask. */
		sigprocmask(SIG_SETMASK, &orig_sigmask, NULL);
		if (dpy != NULL)
			close(xcb_get_file_descriptor(dpy));
		if (sync) {
			execute(cmd);
		} else {
			pid_t pid2 = fork();
			if (pid2 == 0)
				execute(cmd);
			/* _exit: don't flush stdio buffers inherited from the daemon */
			_exit(pid2 == -1 ? EXIT_FAILURE : EXIT_SUCCESS);
		}
	}
	sigset_t saved;
	/* The daemon blocks the handled signals outside of pselect(); unblock
	 * them while waiting so a synchronous command can be interrupted by a
	 * termination request instead of delaying it until the command ends. */
	sigprocmask(SIG_SETMASK, &orig_sigmask, &saved);
	while (waitpid(pid, NULL, 0) == -1 && errno == EINTR && running)
		;
	sigprocmask(SIG_SETMASK, &saved, NULL);
}

void execute(char *cmd[])
{
	setsid();
	if (redir_fd != -1) {
		dup2(redir_fd, STDOUT_FILENO);
		dup2(redir_fd, STDERR_FILENO);
		close(redir_fd);
	}
	execvp(cmd[0], cmd);
	warn("Spawning '%s' failed: %s.\n", cmd[0], strerror(errno));
	_exit(EXIT_FAILURE);
}

/* Locale-independent replacement for isgraph(): bytes >= 0x80 (UTF-8) count
 * as graphic characters, whereas isgraph() rejects them in the "C" locale and
 * would make the trimming below eat trailing non-ASCII characters. */
bool is_graphic(char c)
{
	unsigned char u = (unsigned char) c;
	return u > ' ' && u != 0x7f;
}

char *lgraph(char *s)
{
	size_t len = strlen(s);
	size_t i = 0;
	while (i < len && !is_graphic(s[i]))
		i++;
	if (i < len)
		return (s + i);
	else
		return NULL;
}

char *rgraph(char *s)
{
	size_t len = strlen(s);
	while (len > 0 && !is_graphic(s[len - 1]))
		len--;
	if (len > 0)
		return (s + len - 1);
	else
		return NULL;
}
