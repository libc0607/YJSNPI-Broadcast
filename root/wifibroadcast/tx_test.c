// tx_test: send custom strings to air
// Github @libc0607
//
// Usage: ./tx_test  <iface with monitor mode enabled> 
//			<g/n/n_ldpc> \
//			<ratembps-1,2,5,6,12,18,24,36,48,54/mcsindex-0~7> \
//			<payload less than 1kbytes>
//
// e.g. ./tx_test wlan0 g 24 rts yjsnpiyjsnpi
// e.g. ./tx_test wlan0 n 2 data yjsnpiyjsnpi
// e.g. ./tx_test wlan0 n_ldpc 7 datashort yjsnpiyjsnpi
//
// This is a test program - maybe bugs


#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

static const uint8_t framedata_rtheader_80211g[] = {
	0x00, 0x00, 
	0x0C, 0x00, 	// length
	0x04, 0x80, 0x00, 0x00,	// present flag 00000000 00000000 10000000 00001000
	0x30, 0x00, 			// // 24mbps + padding
	0x00, 0x00, 	
};	

static const uint8_t framedata_rtheader_80211n[] = {
	0x00, 0x00, 
	0x0D, 0x00, 	// length
	0x00, 0x80, 0x08, 0x00,	// present flag 00000000 00001000 10000000 00000000
	0x00, 0x00, 
	0x17, 			// mask: bw, gi, fec: 					8'b00010111
	0x10,			// 20MHz bw, long guard interval, ldpc, 8'b00010000
	0x02,			// mcs index 2 (speed level)
};

static const uint8_t framedata_ieeeheader_rts[] = {
        0xb4, 0x01, 0x00, 0x00, 
		0xff, 0x00, 0x00, 0x00, 0x00, 0x00,
		0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t framedata_ieeeheader_data[] = {
        0x08, 0x02, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00,// 1st byte of MAC will be overwritten with encoded port
        0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // mac
        0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // mac
        0x00, 0x00 // IEEE802.11 seqnum, (will be overwritten later by Atheros firmware/wifi chip)
};

static const uint8_t framedata_ieeeheader_datashort[] = {
        0x08, 0x01, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
        0xff // 1st byte of MAC will be overwritten with encoded port
};


int open_sock (char *ifname) 
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

void usage() 
{
	printf(
		"Usage: \n"
		"\ttx_test  <iface with monitor mode enabled> <g/n/n_ldpc> \n"
		"\t	<ratembps-1,2,5,6,12,18,24,36,48,54/mcsindex-0~7> \n"
		"\t	<payload less than 1kbytes>\n"
		"\t	\n"
		"\t	e.g. ./tx_test.c wlan0 g 24 rts yjsnpiyjsnpi\n"
		"\t	e.g. ./tx_test.c wlan0 n 2 data yjsnpiyjsnpi\n"
		"\t	e.g. ./tx_test.c wlan0 n_ldpc 7 datashort yjsnpiyjsnpi\n"
		"\t	\n"
		"This is a test program - maybe bugs\n"
	);
}

int main (int argc, char *argv[]) 
{
	//./tx_test.c wlan0 g 24 rts yjsnpiyjsnpi
	//		0		1	2 3	 4		5
	uint8_t * rt_header, ieee_header, payload;
	int rt_length, ieee_length, payload_length, total_length, len_ret;
	uint8_t framedata_buf[2048];
	int sockfd;
	
	if (argc == 1) {
		usage();
		exit(0);
	}
	
	// open wi-fi iface
	sockfd = open_sock(argv[1]);
	usleep(20000);

	// debug output
	fprintf(stderr, "%s Init complete\n", argv[1]);
	fprintf(stderr, "tx_test, send 802.11%s packet, ", argv[2]);
	if (0 == strcmp(argv[2], "g")) {
		fprintf(stderr, "rate %sMbps, ", argv[3]);
	} else if (0 == strcmp(argv[2], "n")) {
		fprintf(stderr, "mcs index %s, ", argv[3]);
	}
	fprintf(stderr, "frametype %s, ", argv[4]);
	fprintf(stderr, "payload %s\n ", argv[5]);
	
	// init memory
	bzero(framedata_buf, sizeof(framedata_buf));
	total_length = 0;
	
	// copy radiotap header to buf
	if (0 == strcmp(argv[2], "g")) {
		memcpy(framedata_buf, framedata_rtheader_80211g, sizeof(framedata_rtheader_80211g));
		framedata_buf[8] = (uint8_t)(atoi(argv[3]) *2);
		total_length += sizeof(framedata_rtheader_80211g);
	} else if (0 == strcmp(argv[2], "n")) {
		memcpy(framedata_buf, framedata_rtheader_80211n, sizeof(framedata_rtheader_80211n));
		framedata_buf[12] = (uint8_t)(atoi(argv[3]));
		framedata_buf[10] = 0x07;
		framedata_buf[11] = 0x00;
		total_length += sizeof(framedata_rtheader_80211n);
	} else if (0 == strcmp(argv[2], "n_ldpc")) {
		memcpy(framedata_buf, framedata_rtheader_80211n, sizeof(framedata_rtheader_80211n));
		framedata_buf[12] = (uint8_t)(atoi(argv[3]));
		framedata_buf[10] = 0x17;
		framedata_buf[11] = 0x10;
		total_length += sizeof(framedata_rtheader_80211n);
	}
	
	// copy ieee header to buf
	if (0 == strcmp(argv[4], "rts")) {
		memcpy(framedata_buf+total_length, framedata_ieeeheader_rts, sizeof(framedata_ieeeheader_rts));
		total_length += sizeof(framedata_ieeeheader_rts);
	} else if (0 == strcmp(argv[4], "data")) {
		memcpy(framedata_buf+total_length, framedata_ieeeheader_data, sizeof(framedata_ieeeheader_data));
		total_length += sizeof(framedata_ieeeheader_data);
	} else if (0 == strcmp(argv[4], "datashort")) {
		memcpy(framedata_buf+total_length, framedata_ieeeheader_datashort, sizeof(framedata_ieeeheader_datashort));
		total_length += sizeof(framedata_ieeeheader_datashort);
	}
	
	// copy custom payload to buf
	strcpy(framedata_buf+total_length, argv[5]);
	total_length += strlen(argv[5]);

	// Send frame to air
	len_ret = write(sockfd, &framedata_buf, total_length);
	fprintf(stderr, "write() returns %d.\n", len_ret);
	if (len_ret < 0) {
		fprintf(stderr, "inject failed.\n");	
	}
	
	usleep(10000); 

	fprintf(stderr, "end\n");
	close(sockfd);
	return EXIT_SUCCESS;
}

