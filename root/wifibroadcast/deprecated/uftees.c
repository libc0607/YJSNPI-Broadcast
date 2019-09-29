/* uftees - clone from udp stream to many named pipe 
(c) racic@stackoverflow
modified by libc0607@github
WTFPL Licence */

/*
*	Usage:
*		uftees listen-port FIFO1 (... FIFOn) 
*		(n up to 16
*	e.g. uftees 30001 FIFO1 FIFO2 FIFO3
*	Listen UDP port 30001 and write it to FIFOs
*/
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <errno.h>
#include <resolv.h>
#include <string.h>
#include <utime.h>
#include <unistd.h>
#include <getopt.h>
#include <endian.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include "lib.h"

int main(int argc, char *argv[])
{
	int readfd;
	int writefd[16] = {0};
	int n = 0;
	int j = 0;
	struct stat status;
	char *fifonam[16];
	char buffer[BUFSIZ];
	ssize_t bytes_r, bytes_w[16];
	struct sockaddr_in addr;
	int sockfd;
	int slen = sizeof(addr);

	signal(SIGPIPE, SIG_IGN);

	if (argc < 2 || argc > 18) {
		printf("Usage:\n\t%s port FIFO1 FIFO2 ... FIFOn \n\t\tFIFOn - path to a"
			" named pipe, required argument\n\t\tn up to 16\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[1]));
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if ( (sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0 ){
		fprintf(stderr, "ERROR: Could not create UDP socket!");
		exit(1);
	}
	bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
	
	n = argc - 2;
	for (j=0; j<n; j++) {
		fifonam[j] = argv[j+2];	// fifonam[0] = argv[2] and so on...
		readfd = open(fifonam[j], O_RDONLY | O_NONBLOCK);
		if (-1 == readfd) {
			perror("uftees: readfd: open()");
			exit(EXIT_FAILURE);
		}

		if (-1 == fstat(readfd, &status)) {
			perror("uftees: fstat");
			close(readfd);
			exit(EXIT_FAILURE);
		}

		if(!S_ISFIFO(status.st_mode)) {
			printf("uftees: %s is not a fifo!\n", fifonam[j]);
			close(readfd);
			exit(EXIT_FAILURE);
		}
		writefd[j] = open(fifonam[j], O_WRONLY | O_NONBLOCK);
		if (-1 == writefd[j]) {
			perror("uftees: writefd: open()");
			close(readfd);
			exit(EXIT_FAILURE);
		}
		close(readfd);
	}
	
	while (1) {
		
		bytes_r = recvfrom(sockfd, (char *)&buffer, sizeof(buffer), 0, 
					(struct sockaddr*)&addr, &slen);
		//bytes_r = read(STDIN_FILENO, buffer, sizeof(buffer));
		
		if (bytes_r < 0 && errno == EINTR)
			continue;
		
		if (bytes_r == 0)	
			usleep(1000);	// +1ms, excited!
		
		for (j=0; j<n; j++) {
			bytes_w[j] = write(writefd[j], buffer, bytes_r);
			if (-1 == bytes_w[j])
				perror("uftees: writing to fifo");
		}
	}
	for (j=0; j<n; j++) {
		close(writefd[j]); 
	}
	close(sockfd);
	return(0);
}