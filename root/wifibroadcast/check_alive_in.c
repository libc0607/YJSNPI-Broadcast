// check_alive by Rodizio. Checks for incoming wifibroadcast packets. GPL2 licensed.
// modified by @libc0607: get status from UDP
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <resolv.h>
#include <string.h>
#include <utime.h>
#include <unistd.h>
#include <getopt.h>
#include <endian.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include "lib.h"
#include <iniparser.h> 

wifibroadcast_rx_status_t *status_memory_open(void) {
	int fd;

	for(;;) {
		fd = shm_open("/wifibroadcast_rx_status_0", O_RDWR, S_IRUSR | S_IWUSR);
		if(fd > 0) { break; }
		usleep(100000);
	}

	if (ftruncate(fd, sizeof(wifibroadcast_rx_status_t)) == -1) {
		perror("ftruncate");
		exit(1);
	}

	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	
	return (wifibroadcast_rx_status_t*)retval;
}


int main(int argc, char *argv[]) {
	int status_output = 0;
	int ret;
	if(argc < 2){
        fprintf(stderr, "usage: %s <ini.file>\n", argv[0]);
        return 1;
    }
	
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	fprintf(stderr, "%s Config: Listen port %s\n", 
			argv[0], iniparser_getstring(ini, "alive_in:udp_port", NULL));
	
	
	int16_t port = atoi(iniparser_getstring(ini, "alive_in:udp_port", NULL));
	char *param_exec = (char *)iniparser_getstring(ini, "alive_in:exec", NULL);
	struct sockaddr_in send_addr;
	struct sockaddr_in source_addr;	
	int sockfd, slen = sizeof(send_addr);

	bzero(&source_addr, sizeof(source_addr));
	source_addr.sin_family = AF_INET;
	source_addr.sin_port = htons(port);
	source_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	if ((sockfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) 
		printf("ERROR: Could not create UDP socket!");
	if (-1 == bind(sockfd, (struct sockaddr*)&source_addr, sizeof(source_addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		iniparser_freedict(ini);
		close(sockfd);
		return 0;
	}
	
    //wifibroadcast_rx_status_t *t = status_memory_open();
	
	while (1) {
		ret = recvfrom(sockfd, &status_output, sizeof(status_output), 0, 
					(struct sockaddr*)&source_addr, (socklen_t *)&slen);
		if (ret > 0) {
			status_output = ntohl(status_output);
			if (!status_output) {
				fprintf(stderr, "No new packets, exec %s ...", param_exec);
				popen(param_exec, "r");
				sleep(5);
			}
		}
		
	}
	iniparser_freedict(ini);
	close(sockfd);
    return 0;
}
