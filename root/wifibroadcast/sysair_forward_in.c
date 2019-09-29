// sysair_forward_in
// Get sysair Data (cpuload, temp. undervolt ..etc)from UDP packet 
//		and write it into shared mem
// this program runs on air wrt, Use with sysair_forward (air pi)
// usage: ./sysair_forward_in conf.ini
// conf.ini:
//	[sysair_forward_in]
//  udp_port=5100
//  mode=tx
//
// Listen on port 5100

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

struct sysair_payloaddata_s {
	uint8_t cpuload;
	uint8_t temp;
	uint8_t undervolt;
}  __attribute__ ((__packed__));

wifibroadcast_rx_status_t_sysair *status_memory_open_sysair() 
{
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_sysair", O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Could not open shared memory /wifibroadcast_rx_status_sysair"); 
		exit(1); 
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_sysair), 
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) {
		perror("mmap"); 
		exit(1); 
	}
	return (wifibroadcast_rx_status_t_sysair*) retval;
}


int main(int argc, char *argv[]) 
{

	if(argc < 2){
        fprintf(stderr, "usage: %s <ini.file>\n", argv[0]);
        return 1;
    }
	
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr, "iniparser: failed to load %s.\n", file);
		exit(1);
	}
	if (strcmp(iniparser_getstring(ini, "sysair_forward_in:mode", NULL), "tx")) {
		fprintf(stderr, "Sleep - not tx (air wrt) mode ...\n");
		while(1) {
			sleep(50000);
		}
	}
	fprintf(stderr, "%s Config: UDP :%s\n", argv[0],
			iniparser_getstring(ini, "sysair_forward_in:udp_port", NULL));
			
	int ret = 0;
	int cardcounter = 0;
	struct sockaddr_in addr;
	int sockfd;
	int slen_rssi = sizeof(addr);
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(iniparser_getstring(ini, "sysair_forward_in:udp_port", NULL)));
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if ( (sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0 ){
		fprintf(stderr, "ERROR: Could not create UDP socket!");
		exit(1);
	}
	if (-1 == bind(sockfd, (struct sockaddr*)&addr, sizeof(addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		iniparser_freedict(ini);
		close(sockfd);
		return 0;
	}

	wifibroadcast_rx_status_t_sysair *t_sysair = status_memory_open_sysair();
	struct sysair_payloaddata_s payloaddata;

	for(;;) {
		usleep(100000);
		ret = recvfrom(sockfd, (char *)&payloaddata, sizeof(payloaddata), 0, 
					(struct sockaddr*)&addr, &slen_rssi);
		if (ret > 0) {
			t_sysair->cpuload = payloaddata.cpuload;
			t_sysair->temp = payloaddata.temp;
			t_sysair->undervolt = payloaddata.undervolt;
		}
	}
	close(sockfd);
	iniparser_freedict(ini);
	return 0;
}
