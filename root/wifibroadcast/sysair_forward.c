// sysair_forward by libc0607@Github
// send air pi system status to air wrt via udp.
// this program runs on air pi.
// usage: sysair_forward config.ini 
/* 
[sysair_forward]
udp_ip=192.168.0.98
udp_port=35008
udp_bind_port=35009 
*/
// Send to 192.168.0.98:35008 using source port 35009
//
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

int main(int argc, char *argv[]) 
{
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
	fprintf(stderr, "%s Config: Send to %s:%s, source port %s\n", 
			argv[0], iniparser_getstring(ini, "sysair_forward:udp_ip", NULL), 
			iniparser_getstring(ini, "sysair_forward:udp_port", NULL), 
			iniparser_getstring(ini, "sysair_forward:udp_bind_port", NULL));
	
	
	// Init udp
	int16_t port = atoi(iniparser_getstring(ini, "sysair_forward:udp_port", NULL));
	int j = 0;
	int cardcounter = 0;
	struct sockaddr_in si_other_rssi;
	struct sockaddr_in source_addr;	
	int s_rssi, slen_rssi=sizeof(si_other_rssi);
	si_other_rssi.sin_family = AF_INET;
	si_other_rssi.sin_port = htons(port);
	si_other_rssi.sin_addr.s_addr = inet_addr(iniparser_getstring(ini, "sysair_forward:udp_ip", NULL));
	memset(si_other_rssi.sin_zero, '\0', sizeof(si_other_rssi.sin_zero));
	bzero(&source_addr, sizeof(source_addr));
	source_addr.sin_family = AF_INET;
	source_addr.sin_port = htons(atoi(iniparser_getstring(ini, "sysair_forward:udp_bind_port", NULL)));
	source_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if ((s_rssi=socket(PF_INET, SOCK_DGRAM, 0))==-1) 
		printf("ERROR: Could not create UDP socket!");
	if (-1 == bind(s_rssi, (struct sockaddr*)&source_addr, sizeof(source_addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		iniparser_freedict(ini);
		close(s_rssi);
		return 0;
	}
	
	
	FILE *fp;
	long double a[4], b[4];
	struct sysair_payloaddata_s payloaddata;
	int undervolt, temp;
	// main loop
	for(;;) {
	
		// cpu load
		fp = fopen("/proc/stat","r");
		if (fp) {
			fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&a[0],&a[1],&a[2],&a[3]);
			fclose(fp);
		}
		fp = fopen("/proc/stat","r");
		if (fp) {
			fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&b[0],&b[1],&b[2],&b[3]);
			fclose(fp);
		}
		payloaddata.cpuload = (((b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2])) / ((b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3]))) * 100;

		// temperature
		fp = fopen("/tmp/wbc_temp","r");
		if (fp) {
			fscanf(fp,"%d", &temp);
			fclose(fp);
		}
		payloaddata.temp = temp / 1000;
		
		// undervolt
		fp = fopen ("/tmp/undervolt", "r");
		if (fp) {
			fscanf(fp, "%i\n", &undervolt);
			fclose(fp); 
		}
		payloaddata.undervolt = (uint8_t)undervolt;

	
	    if (sendto(s_rssi, &payloaddata, sizeof(payloaddata), 0, (struct sockaddr*)&si_other_rssi, slen_rssi)==-1) 
			printf("ERROR: Could not send sys data!");
	    usleep(100000);
	}
	iniparser_freedict(ini);
	close(s_rssi);
	return 0;
}
