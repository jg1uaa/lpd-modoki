// SPDX-License-Identifier: WTFPL

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>

extern char *optarg;

static int fd;
static int port = 515;
static char *queue = NULL;
static char *file = NULL;
static int debug = 0;

#define BUFSIZE 16384
static unsigned char buf[BUFSIZE];

#define send_ack(d)	send_response(d, 0)
#define send_nak(d)	send_response(d, 1)

static int send_response(int d, int nak)
{
	unsigned char rsp = nak;

	return (write(d, &rsp, sizeof(rsp)) >= sizeof(rsp)) ? 0 : -1;
}

static int recv_cmd(int d)
{
	unsigned char cmd;

	return (read(d, &cmd, sizeof(cmd)) >= sizeof(cmd)) ? cmd : -1;
}

static int recv_until_lf(int d)
{
	int i = 0;
	unsigned char c;

	while (1) {
		if (read(d, &c, sizeof(c)) < sizeof(c))
			return -1;
		if (c == 0x0a)
			break;
		if (i < BUFSIZE -1)
			buf[i++] = c;
	}

	buf[i++] = 0;
	return i;
}

static int recv_control_file(int d, long long count)
{
	int len, rv = -1;
	long long c, remain;

	/* discard */
	for (c = 0; c < count; c += len) {
		remain = count - c;
		if (remain > BUFSIZE) remain = BUFSIZE;

		if ((len = read(d, buf, remain)) < 1) {
			fprintf(stderr, "recv_control_file: read\n");
			goto fin0;
		}

		if (debug)
			fprintf(stderr, "%s", buf);
	}

	/* check transfer complete */
	if (recv_cmd(d)) {
		fprintf(stderr, "recv_control_file: recv_cmd\n");
		goto fin0;
	}

	send_ack(d);

	if (debug)
		fprintf(stderr, "%lld bytes received\n", c);

	rv = 0;
fin0:
	return rv;
}

static int recv_data_file(int d, long long count)
{
	int len;
	long long c, remain;
	FILE *fp;

	fp = (file != NULL) ? fopen(file, "w") : stdout;
	if (fp == NULL) {
		fprintf(stderr, "recv_data_file: fopen NULL\n");
		goto fin0;
	}

	/* receive */
	for (c = 0; c < count; c += len) {
		remain = count - c;
		if (remain > BUFSIZE) remain = BUFSIZE;

		if ((len = read(d, buf, remain)) < 1) {
			fprintf(stderr, "recv_data_file: read\n");
			goto fin1;
		}
		fwrite(buf, len, 1, fp);
	}

	/* check transfer complete */
	if (recv_cmd(d)) {
		fprintf(stderr, "recv_control_file: recv_cmd\n");
		goto fin1;
	}

	send_ack(d);

	if (debug)
		fprintf(stderr, "%lld bytes received\n", c);

	/* wait for close from host */
	while (recv_cmd(d) >= 0);

fin1:
	if (file != NULL)
		fclose(fp);
fin0:
	/* quit */
	close(d);
	close(fd);
	exit(0);
}

static int do_command2_loop(int d)
{
	int subcmd, len;
	long long count;

	while (1) {
		if ((subcmd = recv_cmd(d)) < 0)
			goto fin0;

		if ((len = recv_until_lf(d)) < 0)
			goto fin0;

		if (debug) {
			fprintf(stderr,
				"subcmd=%d len=%d arg=%s\n", subcmd, len, buf);
		}

		count = atoll(buf);

		/* process subcommands */
		switch (subcmd) {
		case 0x01:	
			send_ack(d);
			/* do nothing */
			break;
		case 0x02:
			send_ack(d);
			if (recv_control_file(d, count) < 0)
				goto fin0;
			break;
		case 0x03:
			send_ack(d);
			if (recv_data_file(d, count) < 0)
				goto fin0;
			break;
		default:
			send_nak(d);
			break;
		}
	}

fin0:
	return -1;
}

static int is_invalid_queue(void)
{
	char *p;

	if ((p = strchr(buf, ' ')) != NULL)
		*p = 0;

	return (queue == NULL) ? 0 : strcmp(buf, queue);
}

static int do_command_loop(int d)
{
	int cmd, len;

	while (1) {
		if ((cmd = recv_cmd(d)) < 0)
			goto fin0;

		/* ignore command 0x00 (invalid?) */
		if (!cmd)
			continue;

		if ((len = recv_until_lf(d)) < 0)
			goto fin0;

		if (debug) {
			fprintf(stderr,
				"cmd=%d len=%d arg=%s\n", cmd, len, buf);
		}

		if (is_invalid_queue()) {
			send_nak(d);
			continue;
		}

		/* only accept command 02, "Receive a printer job" */
		switch (cmd) {
		case 0x02:
			send_ack(d);
			return do_command2_loop(d);
		default:
			send_nak(d);
			break;
		}
	}

fin0:
	return -1;
}

static int do_main(char *ipstr)
{
	int d, en = 1, rv = -1;
	struct sockaddr_in addr, peer;
	socklen_t peer_len;

	/* create socket */
	if ((fd = socket(AF_INET, SOCK_STREAM, 0)) < 0) {
		fprintf(stderr, "do_main: socket\n");
		goto fin0;
	}
	memset(&addr, 0, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_addr.s_addr = (ipstr == NULL) ? INADDR_ANY : inet_addr(ipstr);
	addr.sin_port = htons(port);

	/* wait for connect */
	setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &en, sizeof(en));
	if (bind(fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
		fprintf(stderr, "do_main: bind\n");
		goto fin1;
	}

	if (listen(fd, 1) < 0) {
		fprintf(stderr, "do_main: listen\n");
		goto fin1;
	}

	peer_len = sizeof(peer);
	if ((d = accept(fd, (struct sockaddr *)&peer, &peer_len)) < 0) {
		fprintf(stderr, "do_main: accept\n");
		goto fin1;
	}

	if (debug) {
		fprintf(stderr, "connected from %s port %d\n",
			inet_ntoa(peer.sin_addr), ntohs(peer.sin_port));
	}

	do_command_loop(d);
	rv = 0;

	close(d);
fin1:
	close(fd);
fin0:
	return rv;
}

int main(int argc, char *argv[])
{
	int ch, help = 0;
	char *ipstr = NULL;
	char *appname = argv[0];

	while ((ch = getopt(argc, argv, "p:a:q:f:dh")) != -1) {
		switch (ch) {
		case 'p':
			port = atoi(optarg);
			break;
		case 'a':
			ipstr = optarg;
			break;
		case 'q':
			queue = optarg;
			break;
		case 'f':
			file = optarg;
			break;
		case 'd':
			debug = 1;
			break;
		case 'h':
		default:
			help = 1;
			break;
		}
	}

	if (help) {
		fprintf(stderr, "usage: %s -a [ip address] -p [portnum] "
			"-q [queue] -f [filename]\n", appname);
		return -1;
	}

	return do_main(ipstr);
}
