// rssitx by Rodizio (c) 2017. Licensed under GPL2
// reads rssi from shared mem and sends it out on wifi interfaces (for R/C and telemetry uplink RSSI)

// Mod by @libc0607

// Usage: ./rssitx config.ini

/*
[rssitx]
mode=0					# 0-send packet to air, 1-send to udp, 2-both
nic=wlan0				# optional, when mode set to 0or2
udp_ip=127.0.0.1		# optional, when mode set to 1or2
udp_port=30302			# optional, when mode set to 1or2
udp_bind_port=30300		# optional, when mode set to 1or2
wifimode=0				# 0-b/g 1-n
rate=6					# Mbit(802.11b/g) / mcs index 0~7(802.11n/ac)
ldpc=0					# 802.11n/ac only
stbc=0					# 0-off, 1/2/3-stbc streams
encrypt=0				# 0-off, 1-on
password=1145141919810	# char
debug=0					# 0-off 1-packet hexdump
rssifreq=3				# 3 new packets per second
*/


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

#define PROGRAM_NAME "rssitx"

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

int open_wifi_sock_by_conf(dictionary *ini)
{
	return open_sock((char *)iniparser_getstring(ini, PROGRAM_NAME":nic", NULL));
}

int open_udp_sock_by_conf(dictionary *ini) 
{
	struct sockaddr_in source_addr;
	int udpfd;
	
	bzero(&source_addr, sizeof(source_addr));
	source_addr.sin_family = AF_INET;
	source_addr.sin_port = htons(atoi(iniparser_getstring(ini, PROGRAM_NAME":udp_bind_port", NULL)));
	source_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	
	if ((udpfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) 
		printf("ERROR: Could not create UDP socket!");
	if (-1 == bind(udpfd, (struct sockaddr*)&source_addr, sizeof(source_addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		exit(0);
	}
	return udpfd;
}

void set_udp_send_addr_by_conf(struct sockaddr_in * addr, dictionary *ini)
{
	bzero(&addr, sizeof(addr));
	addr->sin_family = AF_INET;
	addr->sin_port = htons(atoi(iniparser_getstring(ini, PROGRAM_NAME":udp_port", NULL)));
	addr->sin_addr.s_addr = inet_addr((char *)iniparser_getstring(ini, PROGRAM_NAME":udp_ip", NULL));
}

int get_int_from_file (char * filename) 
{
	FILE * fp;
	int ret;
	
	fp = fopen (filename, "r");
	if (NULL == fp) {
		fprintf(stderr, "ERROR: Could not open %s", filename);
		exit(EXIT_FAILURE);
	}
    fscanf(fp, "%i\n", &ret);
	fclose(fp);
	
	return ret;
}

uint8_t bitrate_to_rtap8 (int bitrate) 
{
	uint8_t ret;
	switch (bitrate) {
		case  1: ret=0x02; break;
		case  2: ret=0x04; break;
		case  5: ret=0x0b; break;
		case  6: ret=0x0c; break;
		case 11: ret=0x16; break;
		case 12: ret=0x18; break;
		case 18: ret=0x24; break;
		case 24: ret=0x30; break;
		case 36: ret=0x48; break;
		case 48: ret=0x60; break;
		case 54: ret=0x6C; break;
		default: fprintf(stderr, "ERROR: Wrong or no data rate specified\n"); exit(1); break;
	}
	return ret;
}

void dump_memory(void* p, int length, char * tag)
{
	int i, j;
	unsigned char *addr = (unsigned char *)p;

	fprintf(stderr, "\n");
	fprintf(stderr, "===== Memory dump at %s, length=%d =====", tag, length);
	fprintf(stderr, "\n");

	for(i = 0; i < 16; i++)
		fprintf(stderr, "%2x ", i);
	fprintf(stderr, "\n");
	for(i = 0; i < 16; i++)
		fprintf(stderr, "---");
	fprintf(stderr, "\n");

	for(i = 0; i < (length/16) + 1; i++) {
		for(j = 0; j < 16; j++) {
			if (i * 16 + j >= length)
				break;
			fprintf(stderr, "%2x ", *(addr + i * 16 + j));
		}
		fprintf(stderr, "\n");
	}
	for(i = 0; i < 16; i++)
		fprintf(stderr, "---");
	fprintf(stderr, "\n\n");
}

void get_cpu_usage(long double *used, long double *all) 
{
	FILE * fp;
	long double a[4];
	
	fp = fopen("/proc/stat", "r");
	if (NULL == fp) {
		perror("ERROR: Could not open /proc/stat");
		exit(EXIT_FAILURE);
	}
	fscanf(fp,"%*s %Lf %Lf %Lf %Lf",&a[0],&a[1],&a[2],&a[3]);
	fclose(fp);
	
	*used = a[0]+a[1]+a[2];
	*all = a[0]+a[1]+a[2]+a[3];

	return;
	
/* 	cpu usage percent =   
	100 * (
		( (b[0]+b[1]+b[2]) - (a[0]+a[1]+a[2]) ) 			used1 - used2
	/ //-------------------------------------------    == -----------------
	( (b[0]+b[1]+b[2]+b[3]) - (a[0]+a[1]+a[2]+a[3]) )		 all1 - all2
	) ; */

}

int encrypt_payload(uint8_t * buf, size_t length, int encrypt_en, char * pwd) 
{
	if (0 == encrypt_en) {
		return length;
	}
	size_t enc_len;
	uint8_t * enc_data = xxtea_encrypt(buf, length, pwd, &enc_len);
	// overwrite buffer
	memcpy(buf, enc_data, enc_len);
	return enc_len;
}

void fill_td_to_rssi_packet(struct framedata_s * framedata, telemetry_data_t *td) 
{
	if (td->rx_status == NULL) 
		return;
	
	framedata->signal = (td->rx_status->adapter[0].signal_good == 1)? td->rx_status->adapter[0].current_signal_dbm: -127;
	framedata->signal_rc = (td->rx_status_rc->adapter[0].signal_good == 1)? td->rx_status_rc->adapter[0].current_signal_dbm: -127;
	framedata->lostpackets = htonl(td->rx_status->lost_packet_cnt);
	framedata->lostpackets_rc = htonl(td->rx_status_rc->lost_packet_cnt);
	framedata->injected_block_cnt = htonl(td->tx_status->injected_block_cnt);
	framedata->skipped_fec_cnt = htonl(td->tx_status->skipped_fec_cnt);
	framedata->injection_fail_cnt = htonl(td->tx_status->injection_fail_cnt);
	framedata->injection_time_block = htobe64(td->tx_status->injection_time_block);
	framedata->temp_wrt = htonl(get_int_from_file("/tmp/wbc_temp") / 1000);
	framedata->undervolt = td->sysair_status->undervolt;
	framedata->cpuload = td->sysair_status->cpuload;
	framedata->temp = td->sysair_status->temp;
	
	return;
}

int cal_cpu_usage_percent(long double used1, long double used2, long double all1, long double all2)
{
	return (100 * (
			(used1 - used2) 			
	/ //-------------------------
			 (all1 - all2)		
	));
}

int send_packet_udp(int fd, uint8_t *buf, size_t len, struct sockaddr *addr) 
{
	int ret, slen = sizeof(addr);
	
	ret = sendto(fd, buf, len, 0, addr, slen);
	if (ret == -1) 
		fprintf(stderr, "?");
	
	return ret;	
}

int send_packet_wifi(int fd, uint8_t *buf, size_t len) 
{
	int ret;
	
	ret = write(fd, buf, len);
	if (ret < 0) 
		fprintf(stderr, "!");
	
	return ret;
}

// return: rtheader_length
int packet_rtheader_init_by_conf (int offset, uint8_t *buf, dictionary *ini) 
{
	int param_bitrate = iniparser_getint(ini, PROGRAM_NAME":rate", 0);
	int param_wifimode = iniparser_getint(ini, PROGRAM_NAME":wifimode", 0);
	int param_ldpc = (param_wifimode == 1)? iniparser_getint(ini, PROGRAM_NAME":ldpc", 0): 0;
	int param_stbc = (param_wifimode == 1)? iniparser_getint(ini, PROGRAM_NAME":stbc", 0): 0;
	uint8_t * p_rtheader = (param_wifimode == 1)? u8aRadiotapHeader80211n: u8aRadiotapHeader;
	size_t rtheader_length = (param_wifimode == 1)? sizeof(u8aRadiotapHeader80211n): sizeof(u8aRadiotapHeader);

	// set args
	if (param_wifimode == 0) {	
		// 802.11g
		u8aRadiotapHeader[8] = bitrate_to_rtap8(param_bitrate);
	} else if (param_wifimode == 1) {					
		// 802.11n
		u8aRadiotapHeader80211n[10] = (param_ldpc)? (u8aRadiotapHeader80211n[10] | 0x10): (u8aRadiotapHeader80211n[10] & (~0x10));
		u8aRadiotapHeader80211n[10] = (param_stbc)? (u8aRadiotapHeader80211n[10] | 0x20): (u8aRadiotapHeader80211n[10] & (~0x20));											
		u8aRadiotapHeader80211n[11] = (param_ldpc)? (u8aRadiotapHeader80211n[11] | 0x10): (u8aRadiotapHeader80211n[11] & (~0x10));												
		u8aRadiotapHeader80211n[11] = (param_stbc)? (u8aRadiotapHeader80211n[11] | (param_stbc << 5)): (u8aRadiotapHeader80211n[11] & (~0x60));
		u8aRadiotapHeader80211n[12] = (uint8_t)param_bitrate;	
	}
	// copy radiotap header
	memcpy(buf+offset, p_rtheader, rtheader_length);
	return rtheader_length;
}

// return: ieeeheader_length
int packet_ieeeheader_init_by_conf (int offset, uint8_t * buf, dictionary *ini)
{
	// default rts frame
	memcpy(buf+offset, u8aIeeeHeader_rts, sizeof(u8aIeeeHeader_rts));
	return sizeof(u8aIeeeHeader_rts);
}

// Note: this is a unsafe function
// the encryped data will be write to buf directly and the data size will be larger
// you should make sure that the buf has at least (length+8bytes) spaces to keep it safe
int encrypt_payload_by_conf_unsafe(uint8_t * buf, size_t length, dictionary *ini) 
{
	int encrypt_en;
	char * pwd;
	size_t enc_len;
	uint8_t * enc_data;
	
	encrypt_en = iniparser_getint(ini, PROGRAM_NAME":encrypt", 0);
	pwd = (encrypt_en == 1)? (char *)iniparser_getstring(ini, PROGRAM_NAME":password", NULL): NULL;
	if (0 == encrypt_en) 
		return length;

	enc_data = xxtea_encrypt(buf, length, pwd, &enc_len);
	// unsafe: overwrite buffer
	memcpy(buf, enc_data, enc_len);
	if (1 == encrypt_en) 
		free(enc_data);
	
	return enc_len;
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

wifibroadcast_rx_status_t_sysair *status_memory_open_sysair() 
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
	td->rx_status = telemetry_wbc_status_memory_open();
	td->rx_status_rc = telemetry_wbc_status_memory_open_rc();
	td->tx_status = telemetry_wbc_status_memory_open_tx();
	td->sysair_status = status_memory_open_sysair();
}

void usage() 
{
	printf(
		PROGRAM_NAME" by Rodizio.\n"
		"Dirty mod by Github @libc0607\n"
        "\n"
        "Usage: "PROGRAM_NAME" <config.file>\n"
		"config example:\n"
		"["PROGRAM_NAME"]\n"
		"mode=0					# 0-send packet to air, 1-send to udp, 2-both\n"
		"nic=wlan0				# optional, when mode set to 0or2\n"
		"udp_ip=127.0.0.1			# optional, when mode set to 1or2\n"
		"udp_port=30302				# optional, when mode set to 1or2\n"
		"udp_bind_port=30300			# optional, when mode set to 1or2\n"
		"wifimode=0				# 0-b/g 1-n\n"
		"rate=6					# Mbit(802.11b/g) / mcs index 0~7(802.11n/ac)\n"
		"ldpc=0					# 802.11n/ac only\n"
		"stbc=0					# 0-off, 1/2/3-stbc streams\n"
		"encrypt=0				# 0-off, 1-on\n"
		"password=1145141919810			# char\n"
		"debug=0					# 0-off 1-packet hexdump\n"
		"rssifreq=3				# send 3 new packets per second\n"
	);
    exit(1);
}

int main (int argc, char *argv[]) 
{
	uint8_t buf[512];
	int encryped_framedata_len, full_header_len, rtheader_length, ieeeheader_length;
	int param_retrans, param_debug, param_mode, param_rssifreq, i;
	struct framedata_s framedata;
	telemetry_data_t td;
	int sockfd, udpfd, cpu_usage;	
	struct sockaddr_in send_addr;
	long double used1, used2, all1, all2; 
	
	setpriority(PRIO_PROCESS, 0, 10);
	if (argc !=2) {
		usage();
	}
	
	// Load config file
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	
	param_mode = iniparser_getint(ini, PROGRAM_NAME":mode", 0);
	param_retrans = iniparser_getint(ini, PROGRAM_NAME":retrans", 0);
	param_debug = iniparser_getint(ini, PROGRAM_NAME":debug", 0); 
	param_rssifreq = iniparser_getint(ini, PROGRAM_NAME":rssifreq", 0); 
	
	// open socket
	// wi-fi
	if (param_mode == 0 || param_mode == 2) {
		sockfd = open_wifi_sock_by_conf(ini);
	}
	// udp
	if (param_mode == 1 || param_mode == 2) {
		udpfd = open_udp_sock_by_conf(ini);
		set_udp_send_addr_by_conf(&send_addr, ini);
	}
	
	telemetry_init(&td);
	bzero(buf, sizeof(buf));
	
	// init radiotap header to buf
	rtheader_length = packet_rtheader_init_by_conf(0, buf, ini);
	// init ieee header
	ieeeheader_length = packet_ieeeheader_init_by_conf(rtheader_length, buf, ini);
	full_header_len = rtheader_length + ieeeheader_length;
	
	// fill framedata (init)
	bzero(&framedata, sizeof(framedata));
	framedata.bitrate_kbit = get_int_from_file("/tmp/bitrate_kbit");
	framedata.bitrate_measured_kbit = get_int_from_file("/tmp/bitrate_measured_kbit");
	framedata.cts = get_int_from_file("/tmp/cts");
	
	while (1) {
		// 0. get cpu usage (1/2)
		get_cpu_usage(&used1, &all1);
		// 1. sleep
		usleep(1000000/param_rssifreq);
		// 2. get cpu usage (2/2)
		get_cpu_usage(&used2, &all2);
		// 3. cal cpu usage 
		cpu_usage = cal_cpu_usage_percent(used1, used2, all1, all2);
		// 4. fill framedata
		fill_td_to_rssi_packet(&framedata, &td);
		framedata.cpuload_wrt = htonl(cpu_usage);
		// 5. copy data to send buffer
		memcpy(buf+full_header_len, (uint8_t *)&framedata, sizeof(framedata));
		// 6. encrypt
		encryped_framedata_len = encrypt_payload_by_conf_unsafe(buf+full_header_len, sizeof(framedata), ini);
		// 7. send
		for (i=0; i<param_retrans; i++) {
			switch (param_mode) {
			case 0:
				// wi-fi only
				send_packet_wifi(sockfd, buf, full_header_len+encryped_framedata_len);
				break;
			case 1:
				// udp only
				send_packet_udp(udpfd, buf+full_header_len, encryped_framedata_len, (struct sockaddr *)&send_addr);
				break;
			case 2: 
				// both
				send_packet_udp(udpfd, buf+full_header_len, encryped_framedata_len, (struct sockaddr *)&send_addr);
				send_packet_wifi(sockfd, buf, full_header_len+encryped_framedata_len);
				break;
			}
			usleep(1500 * (i+1));
		}
		// 8. debug
		if (param_debug) 
			dump_memory(buf, full_header_len+encryped_framedata_len, "Full buffer");
	}
	iniparser_freedict(ini);
	close(sockfd);
	close(udpfd);
	return 0;
}

