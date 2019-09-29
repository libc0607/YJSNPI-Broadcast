/* 
	suftees - clone from stdin to one udp stream and many named pipe 
	(c) racic@stackoverflow
	
	modified by libc0607@github
	WTFPL Licence 

 */
/*
*	Usage:
*		suftees sourceport ip port FIFO1 (... ipn portn FIFOn) 
*		(n up to 16
* 
*	e.g. suftees 20000 192.168.1.1 30001 FIFO1 FIFO2
*
*	Forward stdin to 192.168.1.1:30001, FIFO1, FIFO2
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
	int n = 0;
	int j = 0;
	struct stat status;
	
	char *fifonam[16];
	int writefd[16] = {0};
	ssize_t bytes_r, bytes_w[16];
	
	int readfd;
	char buffer[BUFSIZ];
	
	struct sockaddr_in addr;
	struct sockaddr_in source_addr;	
	int sockfd;
	int slen = sizeof(addr);

	signal(SIGPIPE, SIG_IGN);

	if (argc < 2 || argc > 20) {
		printf("Usage:\n\t%s sourceport ip port FIFO1 FIFO2 ... FIFOn \n\t\tFIFOn - path to a"
			" named pipe, required argument\n\t\tn up to 16\n", argv[0]);
		exit(EXIT_FAILURE);
	}
	
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(argv[3]));
	addr.sin_addr.s_addr = inet_addr(argv[2]); 
	
	bzero(&source_addr, sizeof(source_addr));
	source_addr.sin_family = AF_INET;
	source_addr.sin_port = htons(atoi(argv[1]));
	source_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	if ( (sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0 ){
		fprintf(stderr, "ERROR: Could not create UDP socket!");
		exit(1);
	}
	
	// always bind on the same source port to avoid UDP "connection" fail
	// see https://unix.stackexchange.com/questions/420570/udp-port-unreachable-although-process-is-listening
	bind(sockfd, (struct sockaddr*)&source_addr, sizeof(source_addr));

	n = argc - 4;
	for (j=0; j<n; j++) {
		fifonam[j] = argv[j+4];	// fifonam[0] = argv[4] and so on...
		readfd = open(fifonam[j], O_RDONLY | O_NONBLOCK);
		if (-1 == readfd) {
			perror("suftees: readfd: open()");
			exit(EXIT_FAILURE);
		}

		if (-1 == fstat(readfd, &status)) {
			perror("suftees: fstat");
			close(readfd);
			exit(EXIT_FAILURE);
		}

		if(!S_ISFIFO(status.st_mode)) {
			fprintf(stderr, "suftees: %s is a file\n", fifonam[j]);
		} else {
			fprintf(stderr, "suftees: %s is a fifo\n", fifonam[j]);
		}
		
		writefd[j] = open(fifonam[j], O_WRONLY | O_NONBLOCK);
		if (-1 == writefd[j]) {
			perror("suftees: writefd: open()");
			close(readfd);
			exit(EXIT_FAILURE);
		}
		close(readfd);
	}
	
	while (1) {
		
		bytes_r = read(STDIN_FILENO, buffer, sizeof(buffer));
		
		if (bytes_r < 0 && errno == EINTR)
			continue;
		
		if (bytes_r == 0)	
			usleep(1000);	// +1ms, excited!
		
		if (sendto(sockfd, (char *)&buffer, bytes_r, 0, (struct sockaddr*)&addr, slen) == -1) 
			perror("suftees: send udp");
		
		for (j=0; j<n; j++) {
			bytes_w[j] = write(writefd[j], buffer, bytes_r);
			if (-1 == bytes_w[j])
				perror("suftees: writing to fifo");
		}
	}
	for (j=0; j<n; j++) {
		close(writefd[j]); 
	}
	close(sockfd);
	return(0);
}