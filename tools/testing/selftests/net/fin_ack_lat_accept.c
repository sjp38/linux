// SPDX-License-Identifier: GPL-2.0

#include <error.h>
#include <netinet/in.h>
#include <stdio.h>
#include <sys/socket.h>
#include <unistd.h>

int main(int argc, char const *argv[])
{
	int sock, new_sock;
	int opt = 1;
	struct sockaddr_in address;
	int addrlen = sizeof(address);
	int buffer;
	int rc;

	sock = socket(AF_INET, SOCK_STREAM, 0);
	if (!sock)
		error(-1, -1, "socket");

	rc = setsockopt(sock, SOL_SOCKET, SO_REUSEADDR | SO_REUSEPORT,
			&opt, sizeof(opt));
	if (rc == -1)
		error(-1, -1, "setsockopt");

	address.sin_family = AF_INET;
	address.sin_addr.s_addr = INADDR_ANY;
	address.sin_port = htons(4242);

	rc = bind(sock, (struct sockaddr *)&address, sizeof(address));
	if (rc < 0)
		error(-1, -1, "bind");

	rc = listen(sock, 3);
	if (rc < 0)
		error(-1, -1, "listen");

	while (1) {
		new_sock = accept(sock, (struct sockaddr *)&address,
				(socklen_t *)&addrlen);
		if (new_sock < 0)
			error(-1, -1, "accept");

		rc = read(new_sock, &buffer, sizeof(buffer));
		close(new_sock);
	}
	return 0;
}
