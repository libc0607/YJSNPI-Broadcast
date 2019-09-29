// rssi_forward_in
// Get RSSI Data from UDP packet and write it into shared mem
// Use with rssi_forward
// usage: ./rssi_forward_in conf.ini
// conf.ini:
//	[rssi_in]
//  udp_port=5100
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

#define DEBUG

typedef struct {
    uint32_t received_packet_cnt;
	uint32_t wrong_crc_cnt;			// add
    int8_t current_signal_dbm;
    int8_t type; // 0 = Atheros, 1 = Ralink
	int8_t signal_good;	// add
} __attribute__((packed)) wifi_adapter_rx_status_forward_t;

typedef struct 
{
    uint32_t damaged_block_cnt; // number bad blocks video downstream
    uint32_t lost_packet_cnt; // lost packets video downstream
    uint32_t skipped_packet_cnt; // skipped packets video downstream
    uint32_t received_packet_cnt; // packets received video downstream
    uint32_t kbitrate; // live video kilobitrate per second video downstream
    uint32_t kbitrate_measured; // max measured kbitrate during tx startup
    uint32_t kbitrate_set; // set kilobitrate (measured * bitrate_percent) during tx startup
    uint32_t lost_packet_cnt_telemetry_up; // lost packets telemetry uplink
    uint32_t lost_packet_cnt_telemetry_down; // lost packets telemetry downlink
    uint32_t lost_packet_cnt_msp_up; // lost packets msp uplink (not used at the moment)
    uint32_t lost_packet_cnt_msp_down; // lost packets msp downlink (not used at the moment)
    uint32_t lost_packet_cnt_rc; // lost packets rc link
    int8_t current_signal_air; // signal strength in dbm at air pi (telemetry upstream and rc link)
    int8_t joystick_connected; // 0 = no joystick connected, 1 = joystick connected
    uint8_t cpuload_gnd; // CPU load Ground Pi
    uint8_t temp_gnd; // CPU temperature Ground Pi
    uint8_t cpuload_air; // CPU load Air Pi
    uint8_t temp_air; // CPU temperature Air Pi
    uint32_t wifi_adapter_cnt; // number of wifi adapters
    wifi_adapter_rx_status_forward_t adapter[6]; // same struct as in wifibroadcast lib.h
	
	// OpenWrt mod
	// openwrt system status
	uint8_t cpuload_airwrt; // CPU load Air WRT
	uint8_t temp_airwrt; // CPU temperature Air WRT		// but.. not all wrts have a temp sensor
	uint8_t cpuload_gndwrt; // CPU load Ground WRT
	uint8_t temp_gndwrt; // CPU temperature Ground WRT
	
	// uplink
	int8_t current_signal_uplink;
	uint32_t lost_packet_cnt_uplink;
	
	// Air Pi undervolt
	uint8_t undervolt;
	
		
	// some other variables in /wifibroadcast_rx_status_0	
	uint32_t rx0_lost_per_block_cnt;
	uint32_t rx0_received_block_cnt;
	uint32_t rx0_tx_restart_cnt;
	
	// other in shmem sysair
	uint8_t sysair_cts;
	uint32_t sysair_injected_block_cnt;
	uint32_t sysair_injection_fail_cnt;
	long long sysair_injection_time_block;
	
} __attribute__((packed)) wifibroadcast_rx_status_forward_t;


wifibroadcast_rx_status_t *status_memory_open() 
{
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_0", O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Could not open shared memory /wifibroadcast_rx_status_0"); 
		exit(1); 
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), 
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) {
		perror("mmap"); 
		exit(1); 
	}
	return (wifibroadcast_rx_status_t*) retval;
}

wifibroadcast_rx_status_t *status_memory_open_tdown() 
{
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_1", O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		fprintf(stderr, "ERROR: Could not open shared memory /wifibroadcast_rx_status_1"); 
		exit(1);
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), 
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) {
		perror("mmap"); 
		exit(1); 
	}
	return (wifibroadcast_rx_status_t*) retval;
}

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

wifibroadcast_rx_status_t_rc *status_memory_open_rc() 
{
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_rc", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { 
		fprintf(stderr,"ERROR: Could not open shared memory /wifibroadcast_rx_status_rc"); 
		exit(1); 
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_rc), 
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { 
		perror("mmap"); 
		exit(1); 
	}
	return (wifibroadcast_rx_status_t_rc*) retval;
}


wifibroadcast_rx_status_t *status_memory_open_uplink(void) 
{
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_uplink", O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) {
		perror("shm_open"); 
		exit(1); 
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), 
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { 
		perror("mmap"); 
		exit(1); 
	}
	return (wifibroadcast_rx_status_t *) retval;
}



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
	// 一行16个
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
#ifdef DEBUG
	fprintf(stderr, "rssi_forward_in started\n");
#endif	
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
	if (strcmp(iniparser_getstring(ini, "rssi_in:mode", NULL), "rx")) {
		fprintf(stderr, "Sleep - not rx mode ...\n");
		while(1) {
			sleep(50000);
		}
	}
	fprintf(stderr, "%s Config: UDP :%s\n", argv[0],
			iniparser_getstring(ini, "rssi_in:udp_port", NULL));
			
	int ret = 0;
	int cardcounter = 0;
	struct sockaddr_in addr;
	int sockfd;
	int slen_rssi = sizeof(addr);
	
	bzero(&addr, sizeof(addr));
	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(iniparser_getstring(ini, "rssi_in:udp_port", NULL)));
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

	wifibroadcast_rx_status_t *t = status_memory_open();
	wifibroadcast_rx_status_t *t_tdown = status_memory_open_tdown();
	wifibroadcast_rx_status_t_sysair *t_sysair = status_memory_open_sysair();
	wifibroadcast_rx_status_t_rc *t_rc = status_memory_open_rc();
	wifibroadcast_rx_status_t *t_uplink = status_memory_open_uplink();
	wifibroadcast_rx_status_forward_t wbcdata;
	bzero(&wbcdata, sizeof(wbcdata));
	//int number_cards = t->wifi_adapter_cnt;

	for(;;) {
		usleep(100000);
		ret = recvfrom(sockfd, (char *)&wbcdata, 94, 0, 
					(struct sockaddr*)&addr, (socklen_t *)&slen_rssi);
#ifdef DEBUG
		printf("============Loop=================\n");			
		printf("recvlen = %d, err = %d\n", ret, errno);
		dump_memory(&wbcdata, 94, "wbcdata");
#endif		
		if (ret > 0) {
#ifdef DEBUG
			printf("Get data from %s, %d Bytes\n", inet_ntoa(addr.sin_addr), ret);
#endif
			// /wifibroadcast_rx_status_0	
			t->damaged_block_cnt = ntohl(wbcdata.damaged_block_cnt);
			t->lost_packet_cnt = ntohl(wbcdata.lost_packet_cnt);
			t->received_packet_cnt = ntohl(wbcdata.received_packet_cnt);
			t->kbitrate = ntohl(wbcdata.kbitrate);
			t->wifi_adapter_cnt = ntohl(wbcdata.wifi_adapter_cnt);
			t->lost_per_block_cnt = ntohl(wbcdata.rx0_lost_per_block_cnt);
			t->received_block_cnt = ntohl(wbcdata.rx0_received_block_cnt);
			t->tx_restart_cnt = ntohl(wbcdata.rx0_tx_restart_cnt);
			for (cardcounter=0; cardcounter<6; ++cardcounter) {
				t->adapter[cardcounter].current_signal_dbm = 
							wbcdata.adapter[cardcounter].current_signal_dbm;
				t->adapter[cardcounter].received_packet_cnt = 
							ntohl(wbcdata.adapter[cardcounter].received_packet_cnt);
				t->adapter[cardcounter].type = wbcdata.adapter[cardcounter].type;
				t->adapter[cardcounter].signal_good = wbcdata.adapter[cardcounter].signal_good;
				t->adapter[cardcounter].wrong_crc_cnt = wbcdata.adapter[cardcounter].wrong_crc_cnt;
			}
			
			// /wifibroadcast_rx_status_sysair	
			t_sysair->skipped_fec_cnt = ntohl(wbcdata.skipped_packet_cnt);
			t_sysair->bitrate_kbit = ntohl(wbcdata.kbitrate_measured);
			t_sysair->bitrate_measured_kbit = ntohl(wbcdata.kbitrate_set);
			t_sysair->cpuload = wbcdata.cpuload_air;
			t_sysair->temp = wbcdata.temp_air;
			t_sysair->cpuload_wrt = wbcdata.cpuload_airwrt;
			t_sysair->temp_wrt = wbcdata.temp_airwrt;
			t_sysair->undervolt = wbcdata.undervolt;
			t_sysair->cts = wbcdata.sysair_cts;
			t_sysair->injection_fail_cnt = wbcdata.sysair_injection_fail_cnt;
			t_sysair->injection_time_block = wbcdata.sysair_injection_time_block;
			t_sysair->injected_block_cnt = wbcdata.sysair_injected_block_cnt;
			
			// /wifibroadcast_rx_status_uplink	
			t_uplink->adapter[0].current_signal_dbm = wbcdata.current_signal_uplink;
			t_uplink->lost_packet_cnt = ntohl(wbcdata.lost_packet_cnt_uplink);
			
			// /wifibroadcast_rx_status_1		
			t_tdown->lost_packet_cnt = ntohl(wbcdata.lost_packet_cnt_telemetry_down);
			
			// /wifibroadcast_rx_status_rc	
			t_rc->lost_packet_cnt = ntohl(wbcdata.lost_packet_cnt_rc);
			t_rc->adapter[0].current_signal_dbm = wbcdata.current_signal_air;
			
		}
	}
	close(sockfd);
	iniparser_freedict(ini);
	return 0;
}
