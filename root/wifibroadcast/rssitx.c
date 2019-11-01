// rssitx by Rodizio (c) 2017. Licensed under GPL2
// reads rssi from shared mem and sends it out on wifi interfaces (for R/C and telemetry uplink RSSI)

// Mod by @libc0607

// Usage: ./rssitx config.ini
/*

[rssitx]
mode=0	# 0-send packet to air, 1-send to udp, 2-both
nic=wlan0				// optional, when mode set to 0or2
udp_ip=127.0.0.1		// optional, when mode set to 1or2
udp_port=30302			// optional, when mode set to 1or2
udp_bind_port=30300		// optional, when mode set to 1or2
wifimode=0				// 0-b/g 1-n
rate=6					// Mbit(802.11b/g) / mcs index(802.11n/ac)
ldpc=0					// 802.11n/ac only
stbc=0
encrypt=0
password=1145141919810
debug=0
*/
//


#include "lib.h"
#include "xxtea.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <iniparser.h>
#include <inttypes.h>
#include <linux/serial.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <pcap.h>
#include <poll.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stropts.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PROGRAM_NAME rssitx

int sockfd;
int udpfd;

bool no_signal, no_signal_rc;
int param_encrypt_enable = 0;
char * param_encrypt_password = NULL;
struct framedata_s framedata;

static uint8_t u8aRadiotapHeader[] = {
	0x00, 0x00, // <-- radiotap version
	0x0c, 0x00, // <- radiotap header length
	0x04, 0x80, 0x00, 0x00, // <-- radiotap present flags
	0x00, // datarate (will be overwritten later in packet_header_init)
	0x00,
	0x00, 0x00
};

static uint8_t u8aRadiotapHeader80211n[] = {
	0x00, 0x00, // <-- radiotap version
	0x0d, 0x00, // <- radiotap header length
	0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags (tx flags, mcs)
	0x00, 0x00, 	// tx-flag
	0x07, 			// mcs have: bw, gi, fec: 					 8'b00010111
	0x00,			// mcs: 20MHz bw, long guard interval, ldpc, 8'b00010000
	0x02,			// mcs index 2 (speed level, will be overwritten later)
};

static uint8_t u8aIeeeHeader_rts[] = {
        180, 2, 0, 0, // frame control field (2 bytes), duration (2 bytes)
        0xff, // 1st byte of IEEE802.11 RA (mac) must be 0xff or something odd (wifi hardware determines broadcast/multicast through odd/even check)
};

struct framedata_s {
/*     uint8_t rt1;
    uint8_t rt2;
    uint8_t rt3;
    uint8_t rt4;
    uint8_t rt5;
    uint8_t rt6;
    uint8_t rt7;
    uint8_t rt8;

    uint8_t rt9;
    uint8_t rt10;
    uint8_t rt11;
    uint8_t rt12;

    uint8_t fc1;
    uint8_t fc2;
    uint8_t dur1;
    uint8_t dur2;

    uint8_t mac1_1; // Port
    uint8_t mac1_2;
    uint8_t mac1_3;
    uint8_t mac1_4;
    uint8_t mac1_5;
    uint8_t mac1_6;

    uint8_t mac2_1;
    uint8_t mac2_2;
    uint8_t mac2_3;
    uint8_t mac2_4;
    uint8_t mac2_5;
    uint8_t mac2_6;

    uint8_t mac3_1;
    uint8_t mac3_2;
    uint8_t mac3_3;
    uint8_t mac3_4;
    uint8_t mac3_5;
    uint8_t mac3_6;

    uint8_t ieeeseq1;
    uint8_t ieeeseq2;
 */
    int8_t signal;
    uint32_t lostpackets;
    int8_t signal_rc;
    uint32_t lostpackets_rc;
    uint8_t cpuload;
    uint8_t temp;
    uint32_t injected_block_cnt;
    uint32_t skipped_fec_cnt;
    uint32_t injection_fail_cnt;
    long long injection_time_block;
    uint16_t bitrate_kbit;
    uint16_t bitrate_measured_kbit;
    uint8_t cts;
    uint8_t undervolt;
	uint8_t cpuload_wrt;
    uint8_t temp_wrt;
}  __attribute__ ((__packed__));

static int open_sock (char *ifname) 
{
    struct sockaddr_ll ll_addr;
    struct ifreq ifr;
	int sock;
	
    sock = socket (AF_PACKET, SOCK_RAW, 0);
    if (sock == -1) {
		fprintf(stderr, "Error:\tSocket failed\n");
		exit(1);
    }

    ll_addr.sll_family = AF_PACKET;
    ll_addr.sll_protocol = 0;
    ll_addr.sll_halen = ETH_ALEN;

    strncpy(ifr.ifr_name, ifname, IFNAMSIZ);

    if (ioctl(sock, SIOCGIFINDEX, &ifr) < 0) {
		fprintf(stderr, "Error:\tioctl(SIOCGIFINDEX) failed\n");
		exit(1);
    }

    ll_addr.sll_ifindex = ifr.ifr_ifindex;

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
		fprintf(stderr, "Error:\tioctl(SIOCGIFHWADDR) failed\n");
		exit(1);
    }

    memcpy(ll_addr.sll_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    if (bind (sock, (struct sockaddr *)&ll_addr, sizeof(ll_addr)) == -1) {
		fprintf(stderr, "Error:\tbind failed\n");
		close(sock);
		exit(1);
    }

    if (sock == -1 ) {
        fprintf(stderr,
        "Error:\tCannot open socket\n"
        "Info:\tMust be root with an 802.11 card with RFMON enabled\n");
        exit(1);
    }

    return sock;
}

void sendRSSI(int sock, telemetry_data_t *td) 
{
	if (td->rx_status != NULL) {
		long double a[4], b[4];
		FILE *fp;

		int best_dbm = -127;
		int best_dbm_rc = -127;
		int cardcounter = 0;
		//int number_cards = td->rx_status->wifi_adapter_cnt;
		int number_cards_rc = td->rx_status_rc->wifi_adapter_cnt;

		no_signal_rc=true;
		for (cardcounter=0; cardcounter<number_cards_rc; ++cardcounter) {
		    if (td->rx_status_rc->adapter[cardcounter].signal_good == 1) { 
				printf("card[%i] rc signal good\n",cardcounter); 
			}
		    printf("dbm_rc[%i]: %d\n",cardcounter, td->rx_status_rc->adapter[cardcounter].current_signal_dbm);
			if (td->rx_status_rc->adapter[cardcounter].signal_good == 1) {
				if (best_dbm_rc < td->rx_status_rc->adapter[cardcounter].current_signal_dbm) 
					best_dbm_rc = td->rx_status_rc->adapter[cardcounter].current_signal_dbm;
		    }
		    if (td->rx_status_rc->adapter[cardcounter].signal_good == 1) 
				no_signal_rc = false;
			}

		if (no_signal_rc == false) {
			printf("rc signal good   "); 
		}
		printf("best_dbm_rc:%d\n", best_dbm_rc);

		if (no_signal == false) {
		    framedata.signal = best_dbm;
		} else {
		    framedata.signal = -127;
		}
		if (no_signal_rc == false) {
		    framedata.signal_rc = best_dbm_rc;
		} else {
		    framedata.signal_rc = -127;
		}
		framedata.lostpackets = td->rx_status->lost_packet_cnt;
		framedata.lostpackets_rc = td->rx_status_rc->lost_packet_cnt;

		framedata.injected_block_cnt = td->tx_status->injected_block_cnt;
		framedata.skipped_fec_cnt = td->tx_status->skipped_fec_cnt;
		framedata.injection_fail_cnt = td->tx_status->injection_fail_cnt;
		framedata.injection_time_block = td->tx_status->injection_time_block;

		fp = fopen("/proc/stat","r");
		fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&a[0],&a[1],&a[2],&a[3]);
		fclose(fp);
		usleep(333333); // send about 3 times per second
		fp = fopen("/proc/stat","r");
		fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&b[0],&b[1],&b[2],&b[3]);
		fclose(fp);
		framedata.cpuload_wrt = (((b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2])) / ((b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3]))) * 100;

		// Do not send temp on OpenWrt by default		
		int temp_wrt = 0;
		fp = fopen("/tmp/wbc_temp","r");
		if (fp) {
			fscanf(fp,"%d",&temp_wrt);
			fclose(fp);
			fprintf(stderr,"temp: %d\n",temp_wrt/1000);	
		}
		framedata.temp_wrt = temp_wrt / 1000;

	}

//	printf("signal: %d\n", framedata.signal);
//	printf("skipped fec %d\n", td->tx_status->skipped_fec_cnt);
//	printf("injection time: %lld\n", td->tx_status->injection_time_block);
//	printf("injection time: %lld\n", td->tx_status->injection_time_block);

//	fprintf(stdout,"\t\t%d blocks injected, injection time per block %lldus, %d fecs skipped, %d packet injections failed.          \r", td->tx_status->injected_block_cnt,td->tx_status->injection_time_block,td->tx_status->skipped_fec_cnt,td->tx_status->injection_fail_cnt);

	printf("signal_rc: %d\n", framedata.signal_rc);
//	printf("lostpackets: %d\n", framedata.lostpackets);
//	printf("lostpackets_rc: %d\n", framedata.lostpackets_rc);

	// encrypt packet if needed
	uint8_t * p_send_data = NULL;
	size_t send_data_length;
	if (param_encrypt_enable) {
		p_send_data = xxtea_encrypt(&framedata, sizeof(framedata), param_encrypt_password, &send_data_length);
	} else {
		p_send_data = (uint8_t *)&framedata;
		send_data_length = sizeof(framedata);
	}
	
	// send three times with different delay in between to increase robustness against packetloss
	if (write(socks[0], p_send_data, send_data_length) < 0 ) 
		fprintf(stderr, "!");
	usleep(1500);
	if (write(socks[0], p_send_data, send_data_length) < 0 ) 
		fprintf(stderr, "!");
	usleep(2000);
	if (write(socks[0], p_send_data, send_data_length) < 0 ) 
		fprintf(stderr, "!");
	
	// free memory if encrypt enabled
	if (param_encrypt_enable) {
		free(p_send_data);
	}
}

wifibroadcast_rx_status_t *telemetry_wbc_status_memory_open(void) 
{
    int fd = 0;
    fd = shm_open("/wifibroadcast_rx_status_3", O_RDONLY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
		fprintf(stderr, "RSSITX: ERROR: Could not open wifibroadcast rx uplink status!\n");
		exit(1);
    }
    void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), PROT_READ, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) { 
		perror("mmap"); 
		exit(1); 
	}
    return (wifibroadcast_rx_status_t*)retval;
}

wifibroadcast_rx_status_t_rc *telemetry_wbc_status_memory_open_rc(void) 
{
    int fd = 0;
    fd = shm_open("/wifibroadcast_rx_status_rc", O_RDONLY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
		fprintf(stderr, "RSSITX: ERROR: Could not open wifibroadcast R/C status!\n");
		exit(1);
    }
    void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_rc), PROT_READ, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) { 
		perror("mmap"); 
		exit(1); 
	}
    return (wifibroadcast_rx_status_t_rc*)retval;
}

wifibroadcast_tx_status_t *telemetry_wbc_status_memory_open_tx(void) 
{
    int fd = 0;
    fd = shm_open("/wifibroadcast_tx_status_0", O_RDONLY, S_IRUSR | S_IWUSR);
    if (fd < 0) {
		fprintf(stderr, "RSSITX: ERROR: Could not open wifibroadcast tx status!\n");
		exit(1);
    }
    void *retval = mmap(NULL, sizeof(wifibroadcast_tx_status_t), PROT_READ, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) { 
		perror("mmap"); 
		exit(1); 
	}
    return (wifibroadcast_tx_status_t*)retval;
}

wifibroadcast_rx_status_t_sysair 	*status_memory_open_sysair() 
{
	int fd;
	fd = shm_open("/wifibroadcast_rx_status_sysair", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { 
		fprintf(stderr,"ERROR: Could not open wifibroadcast_rx_status_sysair"); 
		exit(1); 
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_sysair), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { 
		perror("mmap"); 
		exit(1); 
	}
	return (wifibroadcast_rx_status_t_sysair*)retval;
}

void telemetry_init(telemetry_data_t *td) 
{
	// init RSSI shared memory
	td->rx_status = telemetry_wbc_status_memory_open();
	td->rx_status_rc = telemetry_wbc_status_memory_open_rc();
	td->tx_status = telemetry_wbc_status_memory_open_tx();
}

void usage(void) {
	printf(
		"rssitx by Rodizio.\n"
		"Dirty mod by Github @libc0607\n"
        "\n"
        "Usage: rssitx <config.file>\n"
		"config example:\n"
		"[rssitx]\n"
		"nic=wlan0\n"
		"encrypt=1\n"
		"password=1919810\n\n"
		"mode=0	# 0-send packet to air, 1-send to udp, 2-both"
		"udp_ip=127.0.0.1		// optional, when mode set to 1or2"
		"udp_port=30302			// optional, when mode set to 1or2"
		"udp_bind_port=30300		// optional, when mode set to 1or2"
		"wifimode=0				// 0-b/g 1-n"
		"rate=6					// Mbit(802.11b/g) / mcs index(802.11n/ac)"
		"ldpc=0					// 802.11n/ac only"
		"stbc=0"
		"debug=0"
		
	);
    exit(1);
}

int get_int_from_file (char * filename) 
{
	FILE * fp;
	int ret;
	
	fp = fopen (filename, "r");
	if (NULL == pFile) {
		perror("ERROR: Could not open %s", filename);
		exit(EXIT_FAILURE);
	}
    fscanf(fp, "%i\n", &ret);
	fclose(pFile);
	
	return ret;
}

int main (int argc, char *argv[]) 
{
	int done = 1, bitrate_kbit, bitrate_measured_kbit, cts;

	setpriority(PRIO_PROCESS, 0, 10);
	if (argc !=2) {
		usage();
	}
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	
	sockfd = open_sock((char *)iniparser_getstring(ini, "PROGRAM_NAME:nic", NULL));
		
	param_encrypt_enable = iniparser_getint(ini, "rssitx:encrypt", 0);
	if (param_encrypt_enable == 1) {
		param_encrypt_password = (char *)iniparser_getstring(ini, "rssitx:password", NULL);
	}
		
	bitrate_kbit = get_int_from_file("/tmp/bitrate_kbit");
	bitrate_measured_kbit = get_int_from_file("/tmp/bitrate_measured_kbit");
	cts = get_int_from_file("/tmp/cts");

	telemetry_data_t td;
	telemetry_init(&td);

	/* framedata.rt1 = 0; // <-- radiotap version
	framedata.rt2 = 0; // <-- radiotap version

	framedata.rt3 = 12; // <- radiotap header length
	framedata.rt4 = 0; // <- radiotap header length

	framedata.rt5 = 4; // <-- radiotap present flags
	framedata.rt6 = 128; // <-- radiotap present flags
	framedata.rt7 = 0; // <-- radiotap present flags
	framedata.rt8 = 0; // <-- radiotap present flags

	framedata.rt9 = 24; // <-- radiotap rate
	framedata.rt10 = 0; // <-- radiotap stuff
	framedata.rt11 = 0; // <-- radiotap stuff
	framedata.rt12 = 0; // <-- radiotap stuff

	framedata.fc1 = 180; // <-- frame control field 0x08 = 8 data frame (180 = rts frame)
	framedata.fc2 = 2; // <-- frame control field 0x02 = 2
	framedata.dur1 = 0; // <-- duration
	framedata.dur2 = 0; // <-- duration

	framedata.mac1_1 = 255 ; // port 127
	framedata.mac1_2 = 0;
	framedata.mac1_3 = 0;
	framedata.mac1_4 = 0;
	framedata.mac1_5 = 0;
	framedata.mac1_6 = 0;

	framedata.mac2_1 = 0;
	framedata.mac2_2 = 0;
	framedata.mac2_3 = 0;
	framedata.mac2_4 = 0;
	framedata.mac2_5 = 0;
	framedata.mac2_6 = 0;

	framedata.mac3_1 = 0;
	framedata.mac3_2 = 0;
	framedata.mac3_3 = 0;
	framedata.mac3_4 = 0;
	framedata.mac3_5 = 0;
	framedata.mac3_6 = 0;

	framedata.ieeeseq1 = 0;
	framedata.ieeeseq2 = 0; */

	bzero(&framedata, sizeof(framedata));


	framedata.bitrate_kbit = bitrate_kbit;
	framedata.bitrate_measured_kbit = bitrate_measured_kbit;
	framedata.cts = cts;

	wifibroadcast_rx_status_t_sysair *t_sysair = status_memory_open_sysair();
	

	while (done) {
		framedata.undervolt = t_sysair->undervolt;
		framedata.cpuload = t_sysair->cpuload;
		framedata.temp = t_sysair->temp;
		sendRSSI(sock, &td);
	}
	iniparser_freedict(ini);
	return 0;
}
