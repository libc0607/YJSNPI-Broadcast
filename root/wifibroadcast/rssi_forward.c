// rssi_forward by Rodizio (c) 2017. Licensed under GP2.
// reads video rssi from shared mem and sends it out via UDP (for FPV_VR 2018 app)
//
// modified by libc0607@Github: specified udp source port 
//
// usage: rssi_forward config.ini 
/* 
[rssirx]
nic=wlan0
udp_ip=192.168.0.98
udp_port=35003
udp_bind_port=35004 
*/
// Send to 192.168.0.98:35003 using source port 35004
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


typedef struct {
    uint32_t received_packet_cnt;
	uint32_t wrong_crc_cnt;			// add
    int8_t current_signal_dbm;
    int8_t type; // 0 = Atheros, 1 = Ralink
	int8_t signal_good;	// add
} __attribute__((packed)) wifi_adapter_rx_status_forward_t;

typedef struct {
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


wifibroadcast_rx_status_t 			*status_memory_open() {
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_0", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { fprintf(stderr,"ERROR: Could not open wifibroadcast_rx_status_0"); exit(1); }
	//if (ftruncate(fd, sizeof(wifibroadcast_rx_status_t)) == -1) { perror("ftruncate"); exit(1); }
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
	return (wifibroadcast_rx_status_t*)retval;
}

wifibroadcast_rx_status_t 			*status_memory_open_tdown() {
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_1", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { fprintf(stderr,"ERROR: Could not open wifibroadcast_rx_status_1"); exit(1);}
	//if (ftruncate(fd, sizeof(wifibroadcast_rx_status_t_1)) == -1) { perror("ftruncate"); exit(1); }
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
	return (wifibroadcast_rx_status_t*)retval;
}

wifibroadcast_rx_status_t_sysair 	*status_memory_open_sysair() {
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_sysair", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { fprintf(stderr,"ERROR: Could not open wifibroadcast_rx_status_sysair"); exit(1); }
	//if (ftruncate(fd, sizeof(wifibroadcast_rx_status_t_sysair)) == -1) { perror("ftruncate"); exit(1); }
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_sysair), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
	return (wifibroadcast_rx_status_t_sysair*)retval;
}

wifibroadcast_rx_status_t_rc 		*status_memory_open_rc() {
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_rc", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { fprintf(stderr,"ERROR: Could not open wifibroadcast_rx_status_rc"); exit(1); }
	//if (ftruncate(fd, sizeof(wifibroadcast_rx_status_t_rc)) == -1) { perror("ftruncate"); exit(1); }
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_rc), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
	return (wifibroadcast_rx_status_t_rc*)retval;
}

wifibroadcast_rx_status_t 			*status_memory_open_uplink() {
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_uplink", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { fprintf(stderr,"ERROR: Could not open wifibroadcast_rx_status_uplink"); exit(1); }
	//if (ftruncate(fd, sizeof(wifibroadcast_rx_status_t_rc)) == -1) { perror("ftruncate"); exit(1); }
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
	return (wifibroadcast_rx_status_t*)retval;
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
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	fprintf(stderr, "%s Config: Send to %s:%s, source port %s\n", 
			argv[0], iniparser_getstring(ini, "rssirx:udp_ip", NULL), 
			iniparser_getstring(ini, "rssirx:udp_port", NULL), 
			iniparser_getstring(ini, "rssirx:udp_bind_port", NULL));
	
	
	int16_t port = atoi(iniparser_getstring(ini, "rssirx:udp_port", NULL));
	//int j = 0;
	int cardcounter = 0;
	struct sockaddr_in si_other_rssi;
	struct sockaddr_in source_addr;	
	int s_rssi, slen_rssi=sizeof(si_other_rssi);

	si_other_rssi.sin_family = AF_INET;
	si_other_rssi.sin_port = htons(port);
	si_other_rssi.sin_addr.s_addr = inet_addr(iniparser_getstring(ini, "rssirx:udp_ip", NULL));
	memset(si_other_rssi.sin_zero, '\0', sizeof(si_other_rssi.sin_zero));
	
	bzero(&source_addr, sizeof(source_addr));
	source_addr.sin_family = AF_INET;
	source_addr.sin_port = htons(atoi(iniparser_getstring(ini, "rssirx:udp_bind_port", NULL)));
	source_addr.sin_addr.s_addr = htonl(INADDR_ANY);

	wifibroadcast_rx_status_t *t = status_memory_open();
	wifibroadcast_rx_status_t *t_tdown = status_memory_open_tdown();
	wifibroadcast_rx_status_t_sysair *t_sysair = status_memory_open_sysair();
	wifibroadcast_rx_status_t_rc *t_rc = status_memory_open_rc();
	wifibroadcast_rx_status_t *t_uplink = status_memory_open_uplink();
	
	wifibroadcast_rx_status_forward_t wbcdata;

	//int number_cards = t->wifi_adapter_cnt;

	bzero(&wbcdata, sizeof(wbcdata));
	
	if ((s_rssi=socket(PF_INET, SOCK_DGRAM, 0))==-1) 
		printf("ERROR: Could not create UDP socket!");
	
	// always bind on the same source port to avoid UDP "connection" fail
	// see https://unix.stackexchange.com/questions/420570/udp-port-unreachable-although-process-is-listening
	if (-1 == bind(s_rssi, (struct sockaddr*)&source_addr, sizeof(source_addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		iniparser_freedict(ini);
		close(s_rssi);
		return 0;
	}
	
	long double a[4], b[4];
	for(;;) {
		
		// 1. /wifibroadcast_rx_status_0 (video rx)
	    wbcdata.damaged_block_cnt = htonl(t->damaged_block_cnt);
	    wbcdata.lost_packet_cnt = htonl(t->lost_packet_cnt);
	    wbcdata.skipped_packet_cnt = htonl(t_sysair->skipped_fec_cnt);
	    wbcdata.received_packet_cnt = htonl(t->received_packet_cnt);
	    wbcdata.kbitrate = htonl(t->kbitrate);
		wbcdata.wifi_adapter_cnt = htonl(t->wifi_adapter_cnt);
		wbcdata.rx0_lost_per_block_cnt = htonl(t->lost_per_block_cnt);
		wbcdata.rx0_received_block_cnt = htonl(t->received_block_cnt);
		wbcdata.rx0_tx_restart_cnt = htonl(t->tx_restart_cnt);
		for (cardcounter=0; cardcounter<6; ++cardcounter) {
			wbcdata.adapter[cardcounter].current_signal_dbm = t->adapter[cardcounter].current_signal_dbm;
			wbcdata.adapter[cardcounter].received_packet_cnt = htonl(t->adapter[cardcounter].received_packet_cnt);
			wbcdata.adapter[cardcounter].type = t->adapter[cardcounter].type;
			wbcdata.adapter[cardcounter].signal_good = t->adapter[cardcounter].signal_good;
			wbcdata.adapter[cardcounter].wrong_crc_cnt = htonl(t->adapter[cardcounter].wrong_crc_cnt);
	    }
		
		// 2. /wifibroadcast_rx_status_sysair (sys status)
	    wbcdata.kbitrate_measured = htonl(t_sysair->bitrate_kbit);
	    wbcdata.kbitrate_set = htonl(t_sysair->bitrate_measured_kbit);
		wbcdata.cpuload_air = t_sysair->cpuload;
	    wbcdata.temp_air = t_sysair->temp;
		wbcdata.undervolt = t_sysair->undervolt;
		wbcdata.sysair_cts = t_sysair->cts;
		wbcdata.sysair_injection_fail_cnt = htonl(t_sysair->injection_fail_cnt);
		wbcdata.sysair_injection_time_block = htonl(t_sysair->injection_time_block);
		wbcdata.sysair_injected_block_cnt = htonl(t_sysair->injected_block_cnt);

		// 3. /wifibroadcast_rx_status_1 (telemetry rx)
		wbcdata.lost_packet_cnt_telemetry_down = htonl(t_tdown->lost_packet_cnt);
		
		// 4. /wifibroadcast_rx_status_rc (rc on air pi)
	    wbcdata.lost_packet_cnt_rc = htonl(t_rc->lost_packet_cnt);
	    wbcdata.current_signal_air = t_rc->adapter[0].current_signal_dbm;
		
		// 5. /wifibroadcast_rx_status_uplink
		wbcdata.current_signal_uplink = t_uplink->adapter[0].current_signal_dbm;
		wbcdata.lost_packet_cnt_uplink = htonl(t_uplink->lost_packet_cnt);
		
		// 6. wrt status	// rssitx/rssirx mod
		wbcdata.cpuload_airwrt = t_sysair->cpuload_wrt;
		wbcdata.temp_airwrt = t_sysair->temp_wrt;
		
		FILE *fp;
		fp = fopen("/proc/stat","r");
		fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&a[0],&a[1],&a[2],&a[3]);
		fclose(fp);
		fp = fopen("/proc/stat","r");
		fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&b[0],&b[1],&b[2],&b[3]);
		fclose(fp);
		wbcdata.cpuload_gndwrt = (((b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2])) / ((b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3]))) * 100;		
		int temp_wrt = 0;
		fp = fopen("/tmp/wbc_temp","r");
		if (fp) {
			fscanf(fp,"%d",&temp_wrt);
			fclose(fp);
			fprintf(stderr,"temp: %d\n",temp_wrt/1000);	
		}
		wbcdata.temp_gndwrt = temp_wrt / 1000;
		
		// Unused
		wbcdata.lost_packet_cnt_telemetry_up = htonl(0);
	    wbcdata.lost_packet_cnt_msp_up = htonl(0);
	    wbcdata.lost_packet_cnt_msp_down = htonl(0);
	    wbcdata.joystick_connected = 0;
	    wbcdata.cpuload_gnd = 0;
	    wbcdata.temp_gnd = 0;




	    if (sendto(s_rssi, &wbcdata, sizeof(wbcdata), 0, (struct sockaddr*)&si_other_rssi, slen_rssi)==-1) 
			printf("ERROR: Could not send RSSI data!");
	    usleep(100000);
	}
	iniparser_freedict(ini);
	close(s_rssi);
	return 0;
}
