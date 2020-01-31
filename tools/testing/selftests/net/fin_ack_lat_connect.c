// SPDX-License-Identifier: GPL-2.0

#include <arpa/inet.h>
#include <error.h>
#include <netinet/tcp.h>
#include <stdio.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>

static unsigned long timediff(struct timeval s, struct timeval e)
{
	if (s.tv_sec > e.tv_sec)
		return 0;
	return (e.tv_sec - s.tv_sec) * 1000000 + e.tv_usec - s.tv_usec;
}

int main(int argc, char const *argv[])
{
	int sock = 0;
	struct sockaddr_in addr, laddr;
	socklen_t len = sizeof(laddr);
	struct linger sl;
	int flag = 1;
	int buffer;
	int rc;
	struct timeval start, end;
	unsigned long lat, sum_lat = 0, nr_lat = 0;

	while (1) {
		gettimeofday(&start, NULL);

		sock = socket(AF_INET, SOCK_STREAM, 0);
		if (sock < 0)
			error(-1, -1, "socket creation");

		sl.l_onoff = 1;
		sl.l_linger = 0;
		if (setsockopt(sock, SOL_SOCKET, SO_LINGER, &sl, sizeof(sl)))
			error(-1, -1, "setsockopt(linger)");

		if (setsockopt(sock, IPPROTO_TCP, TCP_NODELAY,
					&flag, sizeof(flag)))
			error(-1, -1, "setsockopt(nodelay)");

		addr.sin_family = AF_INET;
		addr.sin_port = htons(4242);

		rc = inet_pton(AF_INET, "127.0.0.1", &addr.sin_addr);
		if (rc <= 0)
			error(-1, -1, "inet_pton");

		rc = connect(sock, (struct sockaddr *)&addr, sizeof(addr));
		if (rc < 0)
			error(-1, -1, "connect");

		send(sock, &buffer, sizeof(buffer), 0);

		rc = read(sock, &buffer, sizeof(buffer));

		gettimeofday(&end, NULL);
		lat = timediff(start, end);
		sum_lat += lat;
		nr_lat++;
		if (lat > 100000) {
			rc = getsockname(sock, (struct sockaddr *)&laddr, &len);
			if (rc == -1)
				error(-1, -1, "getsockname");
			printf("port: %d, lat: %lu, avg: %lu, nr: %lu\n",
					ntohs(laddr.sin_port), lat,
					sum_lat / nr_lat, nr_lat);
		}

		if (nr_lat % 1000 == 0)
			fflush(stdout);


		close(sock);
	}
	return 0;
}
