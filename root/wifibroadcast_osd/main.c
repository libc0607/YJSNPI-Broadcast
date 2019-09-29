/*
Copyright (c) 2015, befinitiv
Copyright (c) 2012, Broadcom Europe Ltd
modified by Samuel Brucksch https://github.com/SamuelBrucksch/wifibroadcast_osd
modified by Rodizio
modified by libc0607@Github: Get telemetry data from UDP,
e.g. '/tmp/osd 30000' listen on port 30000

All rights reserved.
Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
* Redistributions of source code must retain the above copyright
notice, this list of conditions and the following disclaimer.
* Redistributions in binary form must reproduce the above copyright
notice, this list of conditions and the following disclaimer in the
documentation and/or other materials provided with the distribution.
* Neither the name of the copyright holder nor the
names of its contributors may be used to endorse or promote products
derived from this software without specific prior written permission.
THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/
#include <stdio.h>
#include <stdint.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/time.h>
#include <sys/resource.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/select.h>
#include <locale.h>
#include <endian.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <arpa/inet.h>
#include <errno.h>
#include <resolv.h>
#include <utime.h>
#include <iniparser.h>

#include "render.h"
#include "telemetry.h"

#include "ltm.h"		//0
#include "mavlink.h"	//1
#include "frsky.h"		//2
#include "smartport.h"	//3

//#define DEBUG
//#define DUMP_PACKET

extern telemetry_type_t i_telemetry_type;

long long current_timestamp() {
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    return milliseconds;
}

fd_set set;

struct timeval timeout;

void dump_memory(void* p, int length, char * tag)
{
	int i, j;
	unsigned char *addr = (unsigned char *)p;

	printf("\n");
	printf("===== Memory dump at %s, length=%d =====", tag, length);
	printf("\n");

	for(i = 0; i < 16; i++)
		printf("%2x ", i);
	printf("\n");
	for(i = 0; i < 16; i++)
		printf("---");
	printf("\n");
	for(i = 0; i < (length/16) + 1; i++) {
		for(j = 0; j < 16; j++) {
			if (i * 16 + j >= length)
				break;
			printf("%2x ", *(addr + i * 16 + j));
		}
		printf("\n");
	}
	for(i = 0; i < 16; i++)
		printf("---");
	printf("\n\n");
}


int main(int argc, char *argv[]) 
{
	uint8_t buf[263]; // Mavlink maximum packet length
	size_t n;
	long long fpscount_ts = 0;
	long long fpscount_ts_last = 0;
	int fpscount = 0;
	int fpscount_last = 0;
	int fps = 0;
	int do_render = 0;
	int counter = 0;
	struct stat fdstatus;
	telemetry_data_t td;
	long long prev_time = current_timestamp();
	long long prev_time2 = current_timestamp();
	long long prev_cpu_time = current_timestamp();
	long long delta = 0;
	int cpuload_gnd = 0;
	int temp_gnd = 0;
	int undervolt_gnd = 0;
	FILE *fp;
	FILE *fp2;
	FILE *fp3;
	long double a[4], b[4];
	struct sockaddr_in addr;
	int sockfd;
	int slen = sizeof(addr);
	char *udp_listen_port_char;
	char *file = argv[1];
	frsky_state_t fs;
	
	setpriority(PRIO_PROCESS, 0, 10);
	setlocale(LC_ALL, "en_GB.UTF-8");
	signal(SIGPIPE, SIG_IGN);
	
    fprintf(stderr,"OSD started\n=====================================\n\n");
	fprintf(stderr, "Using config file: %s\n", file);
	
	
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(EXIT_FAILURE);
	}
	load_ini(ini);

	
    fprintf(stderr,"OSD: Initializing sharedmem ...\n");
    telemetry_init(&td);
    fprintf(stderr,"OSD: Sharedmem init done\n");

	fprintf(stderr,"OSD: Initializing render engine ...\n");
    render_init(iniparser_getstring(ini, "osd:font", NULL));
	fprintf(stderr,"OSD: Render init done\n");

	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	udp_listen_port_char = iniparser_getstring(ini, "osd:udp_port", NULL);
	addr.sin_port = htons(atoi(udp_listen_port_char));
	fprintf(stderr, "UDP Listen port %s\n", udp_listen_port_char);
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if ( (sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0 ){
		fprintf(stderr, "ERROR: Could not create UDP socket!\n");
		exit(1);
	}
	if (-1 == bind(sockfd, (struct sockaddr*)&addr, sizeof(addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		iniparser_freedict(ini);
		close(sockfd);
		return 0;
	}
	
    fp3 = fopen("/tmp/undervolt","r");
    if(NULL == fp3) {
        perror("ERROR: Could not open /tmp/undervolt");
        exit(EXIT_FAILURE);
    }
    fscanf(fp3, "%d", &undervolt_gnd);
    fclose(fp3);
	fprintf(stderr,"undervolt:%d\n",undervolt_gnd);

	// main loop
    while(1) {
#ifdef DEBUG
		fprintf(stderr," start while ");		
#endif
		prev_time = current_timestamp();
	    FD_ZERO(&set);
	    FD_SET(sockfd, &set);
	    timeout.tv_sec = 0;
	    timeout.tv_usec = 50 * 1000;
	    // look for data 50ms, then timeout
	    n = select(sockfd + 1, &set, NULL, NULL, &timeout);
	    if(n > 0) { 
			// if data there, read it and parse it
			n = recvfrom(sockfd, (char *)&buf, sizeof(buf), 0, 
					(struct sockaddr*)&addr, &slen);
#ifdef DEBUG
			fprintf(stderr, "Get data from %s, %d Bytes\n", inet_ntoa(addr.sin_addr), n);
#ifdef DUMP_PACKET
			dump_memory(&buf, n, "buf");
#endif
#endif
	        if(n == 0) 
				continue;			// EOF
			if (n < 0) {
				perror("OSD: recvfrom");
				exit(-1);
			}
			
			switch (i_telemetry_type) {
			case LTM:
				do_render = ltm_read(&td, buf, n);
				break;
			case MAVLINK:
				do_render = mavlink_read(&td, buf, n);
				break;
			case FRSKY:
				frsky_parse_buffer(&fs, &td, buf, n);
				break;
			case SMARTPORT:
				smartport_read(&td, buf, n);
				break;	
			}
	    }
	    counter++;
#ifdef DEBUG
	    fprintf(stderr,"OSD: counter: %d\n",counter);
#endif
	    // render only if we have data that needs to be processed as quick as possible (attitude)
	    // or if three iterations (~150ms) passed without rendering
	    if ((do_render == 1) || (counter == 3)) {
#ifdef DEBUG			
			fprintf(stderr," rendering! ");
#endif
			prev_time = current_timestamp();
			fpscount++;
			render(&td, cpuload_gnd, temp_gnd/1000, undervolt_gnd, fps);
			long long took = current_timestamp() - prev_time;
#ifdef DEBUG
			fprintf(stderr,"Render took %lldms\n", took);
#endif
			do_render = 0;
			counter = 0;
	    }

	    delta = current_timestamp() - prev_cpu_time;
	    if (delta > 1000) {
			prev_cpu_time = current_timestamp();
#ifdef DEBUG
	//		fprintf(stderr,"delta > 10000\n");
#endif
			fp2 = fopen("/sys/class/thermal/thermal_zone0/temp","r");
			fscanf(fp2,"%d",&temp_gnd);
			fclose(fp2);
#ifdef DEBUG
	//		fprintf(stderr,"temp gnd:%d\n",temp_gnd/1000);
#endif
			fp = fopen("/proc/stat","r");
			fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&a[0],&a[1],&a[2],&a[3]);
			fclose(fp);

			cpuload_gnd = (((b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2])) / ((b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3]))) * 100;
#ifdef DEBUG
	//		fprintf(stderr,"cpuload gnd:%d\n",cpuload_gnd);
#endif
			fp = fopen("/proc/stat","r");
			fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&b[0],&b[1],&b[2],&b[3]);
			fclose(fp);
	    }
		long long took = current_timestamp() - prev_time;
#ifdef DEBUG
		fprintf(stderr,"while took %lldms\n", took);
#endif
		long long fpscount_timer = current_timestamp() - fpscount_ts_last;
		if (fpscount_timer > 2000) {
		    fpscount_ts_last = current_timestamp();
		    fps = (fpscount - fpscount_last) / 2;
		    fpscount_last = fpscount;
#ifdef DEBUG
		    fprintf(stderr,"OSD FPS: %d\n", fps);
#endif
		}
    }
	iniparser_freedict(ini);
	close(sockfd);
    return 0;
}
