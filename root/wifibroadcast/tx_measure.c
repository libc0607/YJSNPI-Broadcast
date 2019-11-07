/*   tx_measure (c) 2017 Rodizio, based on wifibroadcast tx by Befinitiv
 *
 *   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2.
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   You should have received a copy of the GNU General Public License along
 *   with this program; if not, write to the Free Software Foundation, Inc.,
 *   51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#include "fec.h"
#include "lib.h"
#include "wifibroadcast.h"
#include "xxtea.h"
#include <arpa/inet.h>
#include <fcntl.h>
#include <getopt.h>
#include <iniparser.h>
#include <limits.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define MAX_PACKET_LENGTH 4192
#define MAX_USER_PACKET_LENGTH 2278
#define MAX_DATA_OR_FEC_PACKETS_PER_BLOCK 32

#define MAX_FIFOS 8
#define FILE_NAME "/dev/zero"

int sock = 0;
int socks[5];


static int open_sock (char *ifname) {
    struct sockaddr_ll ll_addr;
    struct ifreq ifr;

//    sock = socket (PF_PACKET, SOCK_RAW, htons(ETH_P_ALL));
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




static u8 u8aRadiotapHeader[] = {
	0x00, 0x00, // <-- radiotap version
	0x0c, 0x00, // <- radiotap header length
	0x04, 0x80, 0x00, 0x00, // <-- radiotap present flags
	0x00, // datarate (will be overwritten later in packet_header_init)
	0x00,
	0x00, 0x00
};

static u8 u8aRadiotapHeader80211n[] = {
	0x00, 0x00, // <-- radiotap version
	0x0d, 0x00, // <- radiotap header length
	0x00, 0x80, 0x08, 0x00, // <-- radiotap present flags (tx flags, mcs)
	0x00, 0x00, 	// tx-flag
	0x07, 			// mcs have: bw, gi, fec: 					 8'b00010111
	0x00,			// mcs: 20MHz bw, long guard interval, ldpc, 8'b00010000
	0x02,			// mcs index 2 (speed level, will be overwritten later)
};

static u8 u8aIeeeHeader_data_short[] = {
        0x08, 0x01, 0x00, 0x00, // frame control field (2bytes), duration (2 bytes)
        0xff // 1st byte of IEEE802.11 RA (mac) must be 0xff or something odd (wifi hardware determines broadcast/multicast through odd/even check)
};

static u8 u8aIeeeHeader_data[] = {
        0x08, 0x02, 0x00, 0x00, // frame control field (2bytes), duration (2 bytes)
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00, // 1st byte of IEEE802.11 RA (mac) must be 0xff or something odd (wifi hardware determines broadcast/multicast through odd/even check)
        0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // mac
        0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // mac
        0x00, 0x00 // IEEE802.11 seqnum, (will be overwritten later by Atheros firmware/wifi chip)
};

static u8 u8aIeeeHeader_rts[] = {
        0xb4, 0x01, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
        0xff, // 1st byte of IEEE802.11 RA (mac) must be 0xff or something odd (wifi hardware determines broadcast/multicast through odd/even check)
};

int flagHelp = 0;

void usage(void) {
	printf(
		"tx_measure (c)2017 by Rodizio. Based on wifibroadcast tx by Befinitiv. Licensed under GPL2\n"
		"Dirty mod by libc0607@Github\n"
		"\n"
		"Usage: tx_measure <config.ini>\n"
		"\n"
		"config.ini example:\n"
		"[tx]\n"
		"port=0\t\t\t\t# Port number 0-255 (default 0)\n"
		"datanum=8\t\t\t# Number of data packets in a block (default 8). Needs to match with rx\n"
		"fecnum=4\t\t\t# Number of FEC packets per block (default 4). Needs to match with rx\n"
		"packetsize=1024\t\t\t# Number of bytes per packet (default %d, max. %d). This is also the FEC block size. Needs to match with rx\n"
		"frametype=0\t\t\t# Frame type to send. 0 = DATA short, 1 = DATA standard, 2 = RTS\n"
		"wifimode=0\t\t\t# Wi-Fi mode. 0=802.11g 1=802.11n\n"
		"ldpc=0\t\t\t\t# 1-Use LDPC encode, 0-default. Experimental. Only valid when wifimode=n and both your Wi-Fi cards support LDPC.\n"
		"stbc=0\t\t\t\t# 0-default, 1-1 STBC stream, 2-2 STBC streams, 3-3 STBC streams. Only valid when wifimode=n and both your Wi-Fi cards support STBC.\n"
		"rate=6\t\t\t\t# When wifimode=g, data rate to send frames with. Choose 1,2,5,6,11,12,18,24,36 Mbit\n"
		"\t\t\t\t# When wifimode=n, mcs index, 0~7\n"
		"mode=0\t\t\t\t# Transmission mode. 0 = send on all interfaces, 1 = send only on interface with best RSSI\n"
		"nic=wlan0\t\t\t# Wi-Fi interface\n"
		"encrypt=1\t\t\t# enable encrypt. Note that when encrypt is enabled, the actual payload length will be reduced by 4\n"
		"password=yarimasune1919810\n"
		, 1024, MAX_USER_PACKET_LENGTH
	);
    exit(1);
}


typedef struct {
	int seq_nr;
	int fd;
	int curr_pb;
	packet_buffer_t *pbl;
} fifo_t;


/* 
packet_header: buffer
type: 0-datashort, 1-data, 2-rts	
mode: 0-802.11g, 1-802.11n
ldpc: 0-disable, 1-802.11n with ldpc
stbc: 0-disable, 1-1 STBC stream, 2- 2 STBC streams, 3 - 3 STBC streams
rate: 1,2,5,6,11,12,18,24,36,48 when mode=0
	  0,1,2,3,4,5,6,7 when mode=1 or 2
port: port
*/
int packet_header_init(uint8_t *packet_header, int type, int mode, int ldpc, int stbc, int rate, int port) 
{
	u8 *pu8 = packet_header;
	int port_encoded = 0;
	
	if (mode == 0) {	// 802.11g
		switch (rate) {
			case 1:  u8aRadiotapHeader[8]=0x02; break;
			case 2:  u8aRadiotapHeader[8]=0x04; break;
			case 5:  u8aRadiotapHeader[8]=0x0b; break;
			case 6:  u8aRadiotapHeader[8]=0x0c; break;
			case 11: u8aRadiotapHeader[8]=0x16; break;
			case 12: u8aRadiotapHeader[8]=0x18; break;
			case 18: u8aRadiotapHeader[8]=0x24; break;
			case 24: u8aRadiotapHeader[8]=0x30; break;
			case 36: u8aRadiotapHeader[8]=0x48; break;
			case 48: u8aRadiotapHeader[8]=0x60; break;
			case 54: u8aRadiotapHeader[8]=0x6C; break;
			default:
				fprintf(stderr, "ERROR: Wrong or no data rate specified (see -d parameter)\n");
				exit(1);
			break;
		}
	} else if (mode == 1) {					// 802.11n
		if (ldpc == 0) {
			u8aRadiotapHeader80211n[10] &= (~0x10);
			u8aRadiotapHeader80211n[11] &= (~0x10);
			u8aRadiotapHeader80211n[12] = (uint8_t)rate;
		} else {
			u8aRadiotapHeader80211n[10] |= 0x10;
			u8aRadiotapHeader80211n[11] |= 0x10;
			u8aRadiotapHeader80211n[12] = (uint8_t)rate;
		}
		switch (stbc) {
		case 1: 
			u8aRadiotapHeader80211n[10] |= 0x20;
			u8aRadiotapHeader80211n[11] |= (0x1 << 5);
		break; 
		case 2:
			u8aRadiotapHeader80211n[10] |= 0x20;
			u8aRadiotapHeader80211n[11] |= (0x2 << 5);
		break;
		case 3: 
			u8aRadiotapHeader80211n[10] |= 0x20;
			u8aRadiotapHeader80211n[11] |= (0x3 << 5);
		break;
		case 0:
		default:
			// clear all bits
			u8aRadiotapHeader80211n[10] &= (~0x20);
			u8aRadiotapHeader80211n[11] &= (~0x60);
		break;
		}
		
	}

	// copy to buf
	if (mode == 0) {
		memcpy(packet_header, u8aRadiotapHeader, sizeof(u8aRadiotapHeader));
		pu8 += sizeof(u8aRadiotapHeader);
	} else if (mode == 1 || mode == 2) {
		memcpy(packet_header, u8aRadiotapHeader80211n, sizeof(u8aRadiotapHeader80211n));
		pu8 += sizeof(u8aRadiotapHeader80211n);
	}

	switch (type) {
	case 0: // short DATA frame (for Ralink video and telemetry)
		fprintf(stderr, "using short DATA frames\n");
		port_encoded = (port * 2) + 1;
		u8aIeeeHeader_data_short[4] = port_encoded; // 1st byte of RA mac is the port
		memcpy(pu8, u8aIeeeHeader_data_short, sizeof (u8aIeeeHeader_data_short)); //copy data short header to pu8
		pu8 += sizeof (u8aIeeeHeader_data_short);
		break;
	case 1: // standard DATA frame (for Atheros video with CTS protection)
		fprintf(stderr, "using standard DATA frames\n");
		port_encoded = (port * 2) + 1;
		u8aIeeeHeader_data[4] = port_encoded; // 1st byte of RA mac is the port
		memcpy(pu8, u8aIeeeHeader_data, sizeof (u8aIeeeHeader_data)); //copy data header to pu8
		pu8 += sizeof (u8aIeeeHeader_data);
		break;
	case 2: // RTS frame
		fprintf(stderr, "using RTS frames\n");
		port_encoded = (port * 2) + 1;
		u8aIeeeHeader_rts[4] = port_encoded; // 1st byte of RA mac is the port
		memcpy(pu8, u8aIeeeHeader_rts, sizeof (u8aIeeeHeader_rts));
		pu8 += sizeof (u8aIeeeHeader_rts);
		break;
	default:
		fprintf(stderr, "ERROR: Wrong or no frame type specified (see -t parameter)\n");
		exit(1);
		break;
	}

	//determine the length of the header
	return pu8 - packet_header;
}

void fifo_init(fifo_t *fifo, int fifo_count, int block_size) {
	int i;

	for(i=0; i<fifo_count; ++i) {
		int j;

		fifo[i].seq_nr = 0;
		fifo[i].fd = -1;
		fifo[i].curr_pb = 0;
		fifo[i].pbl = lib_alloc_packet_buffer_list(block_size, MAX_PACKET_LENGTH);

		//prepare the buffers with headers
		for(j=0; j<block_size; ++j) {
			fifo[i].pbl[j].len = 0;
		}
	}

}

void fifo_open(fifo_t *fifo, int fifo_count) {
	char fn[256];
	sprintf(fn, FILE_NAME);

	if((fifo[0].fd = open(fn, O_RDONLY)) < 0) {
	    fprintf(stderr, "Error opening file \"%s\"\n", fn);
	}
}


void fifo_create_select_set(fifo_t *fifo, int fifo_count, fd_set *fifo_set, 
								int *max_fifo_fd) 
{
	FD_ZERO(fifo_set);
	FD_SET(fifo[0].fd, fifo_set);

	if(fifo[0].fd > *max_fifo_fd) {
		*max_fifo_fd = fifo[0].fd;
	}
}


void pb_transmit_packet(int seq_nr, uint8_t *packet_transmit_buffer, 
						int packet_header_len, const uint8_t *packet_data, 
						int packet_length, int num_interfaces, 
						int param_transmission_mode, int best_adapter) 
{
    int i = 0;

    //add header outside of FEC
    wifi_packet_header_t *wph = (wifi_packet_header_t*)(packet_transmit_buffer + packet_header_len);
    wph->sequence_number = seq_nr;

    //copy data
    memcpy(packet_transmit_buffer + packet_header_len + sizeof(wifi_packet_header_t), packet_data, packet_length);

    int plen = packet_length + packet_header_len + sizeof(wifi_packet_header_t);


    if (best_adapter == 5) {
		for(i=0; i<num_interfaces; ++i) {
			if (write(socks[i], packet_transmit_buffer, plen) < 0 ) 
				fprintf(stderr, "!");
		}
    } else {
		if (write(socks[best_adapter], packet_transmit_buffer, plen) < 0 ) 
			fprintf(stderr, "!");
    }


}


void pb_transmit_block(packet_buffer_t *pbl, int *seq_nr, int port, int packet_length, 
						uint8_t *packet_transmit_buffer, int packet_header_len, 
						int data_packets_per_block, int fec_packets_per_block, 
						int num_interfaces, int param_transmission_mode, 
						telemetry_data_t *td1,
						int enable_encrypt, char * encrypt_password) 
{
	int i;
	uint8_t *data_blocks[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
	uint8_t fec_pool[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK][MAX_USER_PACKET_LENGTH];
	uint8_t *fec_blocks[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
	size_t enc_len;
	
	for (i=0; i<data_packets_per_block; ++i) {
		if (enable_encrypt == 1 && encrypt_password != NULL) {
			data_blocks[i] = xxtea_encrypt(pbl[i].data, packet_length, encrypt_password, &enc_len);
			packet_length += 4;
		} else if (enable_encrypt == 0){
			data_blocks[i] = pbl[i].data;
		}
	}

	if (fec_packets_per_block) {
		for(i=0; i<fec_packets_per_block; ++i) {
			fec_blocks[i] = fec_pool[i];
		}
		fec_encode(packet_length, data_blocks, data_packets_per_block, 
					(unsigned char **)fec_blocks, fec_packets_per_block);
	}

	uint8_t *pb = packet_transmit_buffer;
	pb += packet_header_len;

	//send data and FEC packets interleaved
	int di = 0;
	int fi = 0;
	int seq_nr_tmp = *seq_nr;
	while(di < data_packets_per_block || fi < fec_packets_per_block) {
	    int best_adapter = 0;
	    if (param_transmission_mode == 1) {
    		int i;
    		int ac = td1->rx_status->wifi_adapter_cnt;
    		int best_dbm = -1000;

    		// find out which card has best signal
    		for(i=0; i<ac; ++i) {
    		    if (best_dbm < td1->rx_status->adapter[i].current_signal_dbm) {
					best_dbm = td1->rx_status->adapter[i].current_signal_dbm;
					best_adapter = i;
    		    }
    		}
    		printf ("bestadapter: %d (%d dbm)\n",best_adapter, best_dbm);
	    } else {
			best_adapter = 5; 
			// set to 5 to let transmit packet function know 
			// it shall transmit on all interfaces
	    }

	    if (di < data_packets_per_block) {
			pb_transmit_packet(seq_nr_tmp, packet_transmit_buffer, packet_header_len, 
								data_blocks[di], packet_length, num_interfaces, 
								param_transmission_mode,best_adapter);
			seq_nr_tmp++;
			di++;
	    }

	    if (fi < fec_packets_per_block) {
			pb_transmit_packet(seq_nr_tmp, packet_transmit_buffer, packet_header_len, 
								fec_blocks[fi], packet_length, num_interfaces,
								param_transmission_mode, best_adapter);
			seq_nr_tmp++;
			fi++;
	    }	
	}

	*seq_nr += data_packets_per_block + fec_packets_per_block;

	//reset the length back
	for(i=0; i< data_packets_per_block; ++i) {
			pbl[i].len = 0;
	}
	
	// free the buffer if encrypt is enabled
	if (enable_encrypt == 1) {
		for(i=0; i< data_packets_per_block; ++i) {
				free(data_blocks[i]);
		}	
	}

}


wifibroadcast_rx_status_t *telemetry_wbc_status_memory_open(void) {

    int fd = 0;
	fd = shm_open("/wifibroadcast_rx_status_0", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) {
		fprintf(stderr, "Could not open wifibroadcast rx status ...\n");
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

void telemetry_init(telemetry_data_t *td) {
    td->rx_status = telemetry_wbc_status_memory_open();
}

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    return milliseconds;
}


int smallest(int* values, int count)
{
	int smallest_value = INT_MAX;
	int ii = 0;
	for (; ii < count; ++ii) {
		if (values[ii] < smallest_value) {
			smallest_value = values[ii];
		}
	}
	return smallest_value;
}



int main(int argc, char *argv[]) {

    setpriority(PRIO_PROCESS, 0, -20);

    char fBrokenSocket = 0;
    int pcnt = 0;
    uint8_t packet_transmit_buffer[MAX_PACKET_LENGTH];
    size_t packet_header_length = 0;
    fd_set fifo_set;
    int max_fifo_fd = -1;
    fifo_t fifo[MAX_FIFOS];

    int param_data_packets_per_block = 8;
    int param_fec_packets_per_block = 4;
    int param_packet_length = 1024;
    int param_port = 0;
    int param_min_packet_length = 0;
    int param_fifo_count = 1;
    int param_packet_type = 99;
    int param_data_rate = 99;
    int param_transmission_mode = 0;
	int param_wifi_mode = 0;
	int param_wifi_ldpc = 0;
	int param_wifi_stbc = 0;
	int param_encrypt_enable = 0;
	char * param_encrypt_password = NULL;
	
	if (argc != 2) {
		usage();
	}
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	
	param_fec_packets_per_block = iniparser_getint(ini, "tx:fecnum", 0);
	param_data_packets_per_block = iniparser_getint(ini, "tx:datanum", 0); 
	
	/*
	 * Note: "tx:packetsize" is the length that actually send to air
	 * packetsize mod 4 should be 0 (for xxtea-encrypt compatible)
	 * if encrypt is enabled, the valid data length in each packet decreased by 4 
	*/
	param_packet_length = iniparser_getint(ini, "tx:packetsize", 0);
	param_encrypt_enable = iniparser_getint(ini, "tx:encrypt", 0);
	if (param_encrypt_enable == 1) {
		param_encrypt_password = (char *)iniparser_getstring(ini, "tx:password", NULL);
		param_packet_length -= 4;	// xxtea-c library has a 4 bytes header
	}
		
	param_port = iniparser_getint(ini, "tx:port", 0);
	param_packet_type = iniparser_getint(ini, "tx:frametype", 0);
	param_data_rate = iniparser_getint(ini, "tx:rate", 0);
	param_transmission_mode = iniparser_getint(ini, "tx:mode", 0);
	if (0 == iniparser_getint(ini, "tx:wifimode", 0)) {
		param_wifi_mode = 0;
	} else if (1 == iniparser_getint(ini, "tx:wifimode", 0)) {
		if (iniparser_getint(ini, "tx:ldpc", 0) == 0) {
			param_wifi_ldpc = 0;
		} else if (iniparser_getint(ini, "tx:ldpc", 0) == 1) {
			param_wifi_ldpc = 1;
		}
		param_wifi_stbc = iniparser_getint(ini, "tx:stbc", 0);
		param_wifi_mode = 1;
	} 

	fprintf(stderr, "%s Config: packet %d/%d/%d, port %d, type %d, rate %d, transmode %d, wifimode %d, nic %s, encrypt %d\n",
			argv[0], param_data_packets_per_block, param_fec_packets_per_block, param_packet_length,
			param_port, param_packet_type, param_data_rate, param_transmission_mode, param_wifi_mode, 
			iniparser_getstring(ini, "tx:nic", NULL), param_encrypt_enable);

    if (param_packet_length > MAX_USER_PACKET_LENGTH) {
		fprintf(stderr, "ERROR; Packet length is limited to %d bytes (you requested %d bytes)\n", MAX_USER_PACKET_LENGTH, param_packet_length);
		return (1);
    }
	if ((param_packet_length % 4) != 0) {
		fprintf(stderr, "ERROR: packetsize must be an integer multiple of 4; Use default 1024.");
		param_packet_length = 1024;
	}
    if (param_min_packet_length > param_packet_length) {
		fprintf(stderr, "ERROR; Minimum packet length is higher than maximum packet length (%d > %d)\n", param_min_packet_length, param_packet_length);
		return (1);
    }
    if (param_data_packets_per_block > MAX_DATA_OR_FEC_PACKETS_PER_BLOCK || param_fec_packets_per_block > MAX_DATA_OR_FEC_PACKETS_PER_BLOCK) {
		fprintf(stderr, "ERROR: Data and FEC packets per block are limited to %d (you requested %d data, %d FEC)\n", MAX_DATA_OR_FEC_PACKETS_PER_BLOCK, param_data_packets_per_block, param_fec_packets_per_block);
		return (1);
    }

    packet_header_length = packet_header_init(packet_transmit_buffer, param_packet_type, param_wifi_mode, param_wifi_ldpc, param_wifi_stbc, param_data_rate, param_port);
    fifo_init(fifo, param_fifo_count, param_data_packets_per_block);
    fifo_open(fifo, param_fifo_count);
    fifo_create_select_set(fifo, param_fifo_count, &fifo_set, &max_fifo_fd);

    //initialize forward error correction
    fec_init();

    // initialize telemetry shared mem for rssi based transmission (-y 1)
    telemetry_data_t td;
    telemetry_init(&td);

    //int x = optind;
    int num_interfaces = 0;
/* 
    while(x < argc && num_interfaces < 4) {
		socks[num_interfaces] = open_sock(argv[x]);
        ++num_interfaces;
        ++x;
        usleep(20000); // wait a bit between configuring interfaces to reduce Atheros and Pi USB flakiness
    } 
*/
	// ini supports only support one interface now
	// should be fixed later
	socks[num_interfaces] = open_sock((char *)iniparser_getstring(ini, "tx:nic", NULL));
	num_interfaces = 1;
	usleep(20000);
	
    long long prev_time = 0;
    long long now = 0;

    int pcntnow = 0;
    int pcntprev = 0;
    int bitrate[9];
    int i_bitrate = 0;
    int measure_count = 0;
    int bitrate_avg = 0;
//    int bitrate_smallest = 0;

    while (!fBrokenSocket) {

		packet_buffer_t *pb = fifo[0].pbl + fifo[0].curr_pb;

		//if the buffer is fresh we add a payload header
		if (pb->len == 0) {
			//make space for a length field (will be filled later)
			pb->len += sizeof(payload_header_t); 
		}

		//read the data
		int inl = read(fifo[0].fd, pb->data + pb->len, param_packet_length - pb->len);
		if (inl < 0 || inl > param_packet_length-pb->len) {
			perror("reading stdin");
			return 1;
		}

		if (inl == 0) {
			//EOF
			fprintf(stderr, "Warning: Lost connection to stdin. Please make sure that a data source is connected\n");
			usleep(1e5);
			continue;
		}

		pb->len += inl;

		//check if this packet is finished
		if (pb->len >= param_min_packet_length) {
			payload_header_t *ph = (payload_header_t*)pb->data;
			// write the length into the packet. this is needed since with fec we cannot use the wifi packet lentgh anymore.
			// We could also set the user payload to a fixed size but this would introduce additional latency since tx would need to wait until that amount of data has been received
			ph->data_length = pb->len - sizeof(payload_header_t);
			pcnt++;
			//check if this block is finished
			if(fifo[0].curr_pb == param_data_packets_per_block-1) {
				pb_transmit_block(fifo[0].pbl, &(fifo[0].seq_nr), param_port, 
									param_packet_length, packet_transmit_buffer, 
									packet_header_length, param_data_packets_per_block, 
									param_fec_packets_per_block, num_interfaces, 
									param_transmission_mode, &td,
									param_encrypt_enable, param_encrypt_password);
				fifo[0].curr_pb = 0;
			} else {
				fifo[0].curr_pb++;
			}
		}

		now = current_timestamp();
		pcntnow = pcnt;

		if (now - prev_time > 250) {
			prev_time = current_timestamp();
			bitrate[i_bitrate] = ((pcntnow - pcntprev) * param_packet_length * 8) * 4;
			pcntprev = pcnt;
	//	    fprintf(stderr,"\t\tbitrate[%d]: %d\n", i_bitrate, bitrate[i_bitrate]);
			measure_count++;
			i_bitrate++;
			if (measure_count == 9) { // measure for 2 seconds (1st measurement is instant, thus 9 * 250ms)
		//		bitrate_smallest = bitrate[2];
		 //		for (i = 2; i < 9; i++) {
		//		    if (bitrate[i] < bitrate_smallest) {
		//			bitrate_smallest = bitrate[i];
		//		    }
		//		}
				bitrate_avg = (bitrate[2] + bitrate[3] + bitrate[4] + bitrate[5] + bitrate[6] + bitrate[7] + bitrate[8]) / 7; // do not use 1st and 2nd measurement, these are flawed
				// for some reason, the above measurement yield about 5% too high bitrate, reduce it by 5% here
				bitrate_avg = bitrate_avg * 0.95;
				fprintf(stdout,"%d\n", bitrate_avg);
		//		fprintf(stderr,"average: %d\n", bitrate_avg);
		//		fprintf(stderr,"smallest:%d \n", bitrate_smallest);
				return 0;
			}
		}
    }
    printf("ERROR: Broken socket!\n");
	iniparser_freedict(ini);
    return (0);
}
