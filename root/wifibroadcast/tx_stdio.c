/*
stdin -> wi-fi packet injection
raw data; no fec & retransmission
just a low-level packet sender
*/

#include "lib.h"
#include <fcntl.h>
#include <getopt.h>
#include <iniparser.h>
#include <inttypes.h>
#include <malloc.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <pcap.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <termios.h>
#include <unistd.h>

#define PROGRAM_NAME tx_stdio

static uint8_t rtheader_11g[] = {
	0x00, 0x00, 
	0x0C, 0x00, 	// length
	0x04, 0x80, 0x00, 0x00,	// present flag 00000000 00000000 10000000 00001000
	0x30, 0x00, 			// // 24mbps + padding
	0x00, 0x00, 	
};	

static uint8_t rtheader_11n[] = {
	0x00, 0x00, 
	0x0D, 0x00, 	// length
	0x00, 0x80, 0x08, 0x00,	// present flag 00000000 00001000 10000000 00000000
	0x00, 0x00, 
	0x17, 			// mask: bw, gi, fec: 					8'b00010111
	0x10,			// 20MHz bw, long guard interval, ldpc, 8'b00010000
	0x02,			// mcs index 2 (speed level)
};

static uint8_t ieeeheader_rts[] = {
    0xb4, 0x01, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
    0xff, // 1st byte of IEEE802.11 RA (mac) must be 0xff or something odd (wifi hardware determines broadcast/multicast through odd/even check)
};

static int open_sock (char *ifname, int * mtu) 
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
		fprintf(stderr, "Error:\tioctl(SIOCGIFINDEX) failed.\n");
		exit(1);
    }

    ll_addr.sll_ifindex = ifr.ifr_ifindex;

    if (ioctl(sock, SIOCGIFHWADDR, &ifr) < 0) {
		fprintf(stderr, "Error:\tioctl(SIOCGIFHWADDR) failed.\n");
		exit(1);
    }

    memcpy(ll_addr.sll_addr, ifr.ifr_hwaddr.sa_data, ETH_ALEN);

    if (bind (sock, (struct sockaddr *)&ll_addr, sizeof(ll_addr)) == -1) {
		fprintf(stderr, "Error:\tbind failed.\n");
		close(sock);
		exit(1);
    }

    if (sock == -1 ) {
        fprintf(stderr,
        "Error:\tCannot open socket\n"
        "Info:\tMust be root with an 802.11 card with RFMON enabled.\n");
        exit(1);
    }

	// get mtu
	if (ioctl(sock, SIOCGIFMTU, &ifr)) {
        fprintf(stderr, "Error:\tCan't get MTU.\n");
		close(sock);
		exit(1);
    }
    *mtu = (int)ifr.ifr_mtu;
	
    return sock;
}

void usage(void) 
{
	printf(
		"PROGRAM_NAME by Github @libc0607\n"
        "\n"
        "Usage: PROGRAM_NAME <config.file>\n"
		"config example:\n"
		"[PROGRAM_NAME]\n"
		"nic=wlan0\n"
		"packetsize=1024\n"
		"wifimode=0\n"
		"ldpc=0\n"
		"stbc=2\n"
		"rate=6\n"
	);
    exit(1);
}

void packet_header_init(uint8_t * header, int mode, int rate, int ldpc, int stbc) 
{
	if (mode == 0) {	// 802.11g
		switch (rate) {
			case 1:  header[8]=0x02; break;
			case 2:  header[8]=0x04; break;
			case 5:  header[8]=0x0b; break;
			case 6:  header[8]=0x0c; break;
			case 11: header[8]=0x16; break;
			case 12: header[8]=0x18; break;
			case 18: header[8]=0x24; break;
			case 24: header[8]=0x30; break;
			case 36: header[8]=0x48; break;
			case 48: header[8]=0x60; break;
			default:
				fprintf(stderr, "ERROR: Wrong or no data rate specified (see -d parameter)\n");
				exit(1);
			break;
		}
	} else {					// 802.11n
		if (ldpc == 1) {
			header[10] |= 0x10;
			header[11] |= 0x10;
			header[12] = (uint8_t)rate;
		} else {
			header[10] &= (~0x10);
			header[11] &= (~0x10);
			header[12] = (uint8_t)rate;
		}
		switch (stbc) {
		case 1: 
			header[10] |= 0x20;
			header[11] |= (0x1 << 5);
		break; 
		case 2:
			header[10] |= 0x20;
			header[11] |= (0x2 << 5);
		break;
		case 3: 
			header[10] |= 0x20;
			header[11] |= (0x3 << 5);
		break;
		case 0:
		default:
			header[10] &= (~0x20);
			header[11] &= (~0x60);
		break;
		}
		
	}
	return;
}

int main (int argc, char *argv[]) 
{
	int sockfd, rtheader_len, ieeeheader_len, data_len, packet_len;
    uint8_t *p_rtheader, *p_ieeeheader, *p_data;
	uint8_t *p_packet_buf;		// packet_buf = rtheader + ieeeheader + data
	int max_mtu, rlen, wlen, debug, cnt;
	
	setpriority(PRIO_PROCESS, 0, 10);

	if (argc != 2) {
		usage();
	}
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr, "iniparser: failed to load %s.\n", file);
		exit(1);
	}
	
	sockfd = open_sock((char *)iniparser_getstring(ini, "PROGRAM_NAME:nic", NULL), &max_mtu);
	p_rtheader = (0 == iniparser_getint(ini, "PROGRAM_NAME:wifimode", 0))? rtheader_11g: rtheader_11n;
	rtheader_len = (0 == iniparser_getint(ini, "PROGRAM_NAME:wifimode", 0))? sizeof(rtheader_11g): sizeof(rtheader_11n);
	p_ieeeheader = (uint8_t *)ieeeheader_rts;
	ieeeheader_len = sizeof(ieeeheader_rts);
	data_len = iniparser_getint(ini, "PROGRAM_NAME:packetsize", 0);
	debug = iniparser_getint(ini, "PROGRAM_NAME:debug", 0);
	
	if (data_len + rtheader_len + ieeeheader_len > max_mtu) {
		fprintf(stderr, "Error: packetsize too big.\n");
		exit(1);
	}
	
	packet_header_init(p_rtheader, iniparser_getint(ini, "PROGRAM_NAME:wifimode", 0), iniparser_getint(ini, "PROGRAM_NAME:rate", 0), iniparser_getint(ini, "PROGRAM_NAME:ldpc", 0), iniparser_getint(ini, "PROGRAM_NAME:stbc", 0));
	
	p_packet_buf = (uint8_t *)malloc(max_mtu);
	if (NULL == p_packet_buf) {
		fprintf(stderr, "Error: malloc() failed.\n");
		exit(1);
	}
	
	memcpy(p_packet_buf, p_rtheader, rtheader_len);
	memcpy(p_packet_buf + rtheader_len, p_ieeeheader, ieeeheader_len);
	p_data = p_packet_buf + rtheader_len + ieeeheader_len;

	cnt = 0;
	while (1) {
		// 1. get data from stdin
		rlen = read(STDIN_FILENO, p_data, data_len);
		if (rlen < 0) {
			if (debug) {
				fprintf(stderr, "Error: read() got %d.\n", rlen);
			}
			continue;
		}
		if (rlen == 0) {
			if (debug) {
				fprintf(stderr, "Warning: lost connection to stdin.\n");
			}
			usleep(50000);
			continue;
		}
	
		// 2. send data to air
		packet_len = rtheader_len + ieeeheader_len + rlen;
		wlen = write(sockfd, p_packet_buf, packet_len);
		if (wlen <= 0) {
			if (debug) {
				fprintf(stderr, "Error: write() got %d.\n", wlen);
			}
			if (wlen == 0){
				usleep(50000);
			}
			continue;
		}
		cnt++;
		if (debug) {
			fprintf(stderr, "r%d,", rlen);
			fprintf(stderr, "w%d,", wlen);
			if (cnt % 128 == 0) {
				fprintf(stderr, "cnt%d,\n", cnt);
			}
		}	
	}
	
	// clean up
	iniparser_freedict(ini);
	close(sockfd);
	free(p_packet_buf);
	return 0;
}
