/*   tx_rawsock (c) 2017 Rodizio, based on wifibroadcast tx by Befinitiv
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
#include <endian.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <iniparser.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <resolv.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#include <utime.h>

#define MAX_PACKET_LENGTH 4192
#define MAX_USER_PACKET_LENGTH 2278
#define MAX_DATA_OR_FEC_PACKETS_PER_BLOCK 32

int sock = 0;
int socks[4];
int udp_send_fd;
int skipfec = 0;
int block_cnt = 0;
int param_port = 0;
long long took_last = 0;
long long took = 0;
long long injection_time_now = 0;
long long injection_time_prev = 0;
long long injection_time = 0;
long long pm_now = 0;
int flagHelp = 0;

static int open_sock (char *ifname) {
    struct sockaddr_ll ll_addr;
    struct ifreq ifr;

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

    struct timeval timeout;
    timeout.tv_sec = 0;
    timeout.tv_usec = 8000;
    if (setsockopt (sock, SOL_SOCKET, SO_SNDTIMEO, (char *)&timeout, sizeof(timeout)) < 0) 
		fprintf(stderr,"setsockopt SO_SNDTIMEO\n");

    int sendbuff = 131072;
    if (setsockopt(sock, SOL_SOCKET, SO_SNDBUF, &sendbuff, sizeof(sendbuff)) < 0) 
		fprintf(stderr,"setsockopt SO_SNDBUF\n");

    return sock;
}

static u8 u8aRadiotapHeader[] = {
	0x00, 0x00, // <-- radiotap version
	0x0c, 0x00, // <- radiotap header length
	0x04, 0x80, 0x00, 0x00, // <-- radiotap present flags (rate + tx flags)
	0x00, // datarate (will be overwritten later in packet_header_init)
	0x00, // ??
	0x00, 0x00 // ??
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
	0xff // port =  1st byte of IEEE802.11 RA (mac) must be something odd (wifi hardware determines broadcast/multicast through odd/even check)
};

static u8 u8aIeeeHeader_data[] = {
	0x08, 0x02, 0x00, 0x00, // frame control field (2bytes), duration (2 bytes)
	0xff, 0x00, 0x00, 0x00, 0x00, 0x00, // port = 1st byte of IEEE802.11 RA (mac) must be something odd (wifi hardware determines broadcast/multicast through odd/even check)
	0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // mac
	0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // mac
	0x00, 0x00 // IEEE802.11 seqnum, (will be overwritten later by Atheros firmware/wifi chip)
};

static u8 u8aIeeeHeader_rts[] = {
	0xb4, 0x01, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
	0xff, //  port = 1st byte of IEEE802.11 RA (mac) must be something odd (wifi hardware determines broadcast/multicast through odd/even check)
};

void usage(void) {
	printf(
		"tx_rawsock Dirty mod by libc0607@Github\n"
		"\n"
		"Usage: tx_rawsock <config.ini>\n"
		"\n"
		"config.ini example:\n"
				"[tx]\n"
		"port=0\t\t\t\t# Port number 0-255 (default 0)\n"
		"datanum=8\t\t\t# Number of data packets in a block (default 8). Needs to match with rx\n"
		"fecnum=4\t\t\t# Number of FEC packets per block (default 4). Needs to match with rx\n"
		"packetsize=1024\t\t\t# Number of bytes per packet (default %d, max. %d). This is also the FEC block size. Needs to match with rx\n"
		"frametype=0\t\t\t# Frame type to send. 0 = DATA short, 1 = DATA standard, 2 = RTS\n"
		"wifimode=0\t\t\t# Wi-Fi mode. 0=802.11g 0=802.11n\n"
		"ldpc=0\t\t\t\t# 1-Use LDPC encode, 0-default. Experimental. Only valid when wifimode=n and both your Wi-Fi cards support LDPC.\n"
		"stbc=0\t\t\t\t# 0-default, 1-1 STBC stream, 2-2 STBC streams, 3-3 STBC streams. Only valid when wifimode=n and both your Wi-Fi cards support STBC.\n"
		"rate=6\t\t\t\t# When wifimode=g, data rate to send frames with. Choose 1,2,5,6,11,12,18,24,36 Mbit\n"
		"\t\t\t\t# When wifimode=n, mcs index, 0~7\n"
		"mode=0\t\t\t\t# Transmission mode. 0 = send on all interfaces, 1 = send only on interface with best RSSI\n"
		"nic=wlan0\t\t\t# Wi-Fi interface\n"
		"encrypt=1\t\t\t# enable encrypt. Note that when encrypt is enabled, the actual payload length will be reduced by 4\n"
		"password=yarimasune1919810\n"
		"udp_send=0	"
		"udp_send_to_ip=1.1.1.1		# send data to udp"
		"udp_send_to_port=1025			# udp send port"
		"udp_send_bind_port=1026			# udp bind port"
		, 1024, MAX_USER_PACKET_LENGTH
	);
    exit(1);
}

typedef struct {
	int seq_nr;
	int fd;
	int curr_pb;
	packet_buffer_t *pbl;
} input_t;

long long current_timestamp() {
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long useconds = te.tv_sec*1000LL + te.tv_usec;
    return useconds;
}

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
	case 0: // short DATA frame
		fprintf(stderr, "using short DATA frames\n");
		port_encoded = (port * 2) + 1;
		u8aIeeeHeader_data_short[4] = port_encoded; // 1st byte of RA mac is the port
		memcpy(pu8, u8aIeeeHeader_data_short, sizeof (u8aIeeeHeader_data_short)); //copy data short header to pu8
		pu8 += sizeof (u8aIeeeHeader_data_short);
		break;
	case 1: // standard DATA frame
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

int pb_transmit_packet(int seq_nr, uint8_t *packet_transmit_buffer, int packet_header_len, 
						const uint8_t *packet_data, int packet_length, int num_interfaces, 
						int param_transmission_mode, int best_adapter, 
						int udp_send, struct sockaddr_in * udp_addr) 
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
				return 1;
		}
    } else {
		if (write(socks[best_adapter], packet_transmit_buffer, plen) < 0 ) 
			return 1;
    }
	if (udp_send == 1) {
		struct sockaddr_in * addr = udp_addr;
		if ( sendto(udp_send_fd, (packet_transmit_buffer + packet_header_len), 
						(packet_length + sizeof(wifi_packet_header_t)), 0, 
						(struct sockaddr*)addr, sizeof(struct sockaddr_in)) == -1) {
			fprintf(stderr, "Error: udp send failed: sendto() returns -1.\n");				
		}
			
	}
	
    return 0;
}

void pb_transmit_block (packet_buffer_t *pbl, int *seq_nr, int port, 
						int packet_length, uint8_t *packet_transmit_buffer, 
						int packet_header_len, int data_packets_per_block, 
						int fec_packets_per_block, int num_interfaces, 
						int param_transmission_mode, telemetry_data_t *td1,
						int enable_encrypt, char * encrypt_password,
						int udp_send, struct sockaddr_in * udp_addr) {
	int i;
	uint8_t *data_blocks[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
	uint8_t fec_pool[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK][MAX_USER_PACKET_LENGTH];
	uint8_t *fec_blocks[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
	size_t enc_len;
	
	for (i=0; i<data_packets_per_block; i++) {
		if (enable_encrypt == 1 && encrypt_password != NULL) {
			data_blocks[i] = xxtea_encrypt(pbl[i].data, packet_length, encrypt_password, &enc_len);
			packet_length += 4;
		} else if (enable_encrypt == 0){
			data_blocks[i] = pbl[i].data;
		}
	} 

	if (fec_packets_per_block) {
		for(i=0; i<fec_packets_per_block; ++i) fec_blocks[i] = fec_pool[i];
		fec_encode(packet_length, data_blocks, data_packets_per_block, 
					(unsigned char **)fec_blocks, fec_packets_per_block);
	}

	uint8_t *pb = packet_transmit_buffer;
	pb += packet_header_len;

	int di = 0;
	int fi = 0;
	int seq_nr_tmp = *seq_nr;
	long long prev_time = current_timestamp();
	int counterfec = 0;

	while(di < data_packets_per_block || fi < fec_packets_per_block) { 
		//send data and FEC packets interleaved
	    int best_adapter = 0;
	    if(param_transmission_mode == 1) {
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
//    		printf ("bestadapter: %d (%d dbm)\n",best_adapter, best_dbm);
	    } else {
			best_adapter = 5; // set to 5 to let transmit packet function know it shall transmit on all interfaces
	    }

	    if(di < data_packets_per_block) {
			if (pb_transmit_packet(seq_nr_tmp, packet_transmit_buffer, packet_header_len, 
									data_blocks[di], packet_length, num_interfaces, 
									param_transmission_mode, best_adapter, 
									udp_send, udp_addr)) 
				td1->tx_status->injection_fail_cnt++;
			seq_nr_tmp++;
			di++;
	    }

	    if(fi < fec_packets_per_block) {
			if (skipfec < 1) {
				if (pb_transmit_packet(seq_nr_tmp, packet_transmit_buffer, 
										packet_header_len, fec_blocks[fi], 
										packet_length, num_interfaces,
										param_transmission_mode, best_adapter, 
										udp_send, udp_addr)) 
					td1->tx_status->injection_fail_cnt++;
			} else {
				if (counterfec % 2 == 0) {
					if (pb_transmit_packet(seq_nr_tmp, packet_transmit_buffer, 
											packet_header_len, fec_blocks[fi], 
											packet_length, num_interfaces,
											param_transmission_mode, best_adapter, 
											udp_send, udp_addr)) 
						td1->tx_status->injection_fail_cnt++;
				} else {
	//			   fprintf(stdout,"not transmitted\n");
				}
				counterfec++;
			}
			seq_nr_tmp++;
			fi++;
	    }
	    skipfec--;
	}

	block_cnt++;
	td1->tx_status->injected_block_cnt++;

	took_last = took;
	took = current_timestamp() - prev_time;

//	if (took > 50) fprintf(stdout,"write took %lldus\n", took);
	if (took > (packet_length * (data_packets_per_block + fec_packets_per_block)) / 1.5 ) { // we simply assume 1us per byte = 1ms per 1024 byte packet (not very exact ...)
//	    fprintf(stdout,"\nwrite took %lldus skipping FEC packets ...\n", took);
	    skipfec=4;
	    td1->tx_status->skipped_fec_cnt = td1->tx_status->skipped_fec_cnt + skipfec;
	}

	if(block_cnt % 50 == 0) {
	    fprintf(stdout,"\t\t%d blocks sent, injection time per block %lldus, %d fecs skipped, %d packet injections failed.          \r", block_cnt,td1->tx_status->injection_time_block,td1->tx_status->skipped_fec_cnt,td1->tx_status->injection_fail_cnt);
	    fflush(stdout);
	}

	if (took < took_last) { // if we have a lower injection_time than last time, ignore
	    took = took_last;
	}

	injection_time_now = current_timestamp();
	if (injection_time_now - injection_time_prev > 220) {
	    injection_time_prev = current_timestamp();
	    td1->tx_status->injection_time_block = took;
	    took=0;
	    took_last=0;
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

void status_memory_init(wifibroadcast_tx_status_t *s) 
{
    s->last_update = 0;
    s->injected_block_cnt = 0;
    s->skipped_fec_cnt = 0;
    s->injection_fail_cnt = 0;
    s->injection_time_block = 0;
}

wifibroadcast_rx_status_t *telemetry_wbc_status_memory_open(void) 
{
    int fd = 0;

	fd = shm_open("/wifibroadcast_rx_status_0", O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) {
		fprintf(stderr, "Could not open wifibroadcast rx status ...\n");
	}
	if (ftruncate(fd, sizeof(wifibroadcast_rx_status_t)) == -1) {
		perror("ftruncate");
		exit(1);
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), 
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

    return (wifibroadcast_rx_status_t*)retval;
}

wifibroadcast_tx_status_t *telemetry_wbc_status_memory_open_tx(void) 
{
	int fd = 0;
	char buf[128];
	int sharedmem = 0;
	// TODO: Clean up rx_status shared memory handling
	while(sharedmem == 0) {
	    sprintf(buf, "/wifibroadcast_tx_status_%d", param_port);
    	    fd = shm_open(buf, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
        	if(fd < 0) {
            	    fprintf(stderr, "Could not open wifibroadcast tx status - retrying ...\n");
        	} else {
            	    sharedmem = 1;
        	}
        	usleep(150000);
	}
        if (ftruncate(fd, sizeof(wifibroadcast_tx_status_t)) == -1) {
                perror("ftruncate");
                exit(1);
        }
        void *retval = mmap(NULL, sizeof(wifibroadcast_tx_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
        if (retval == MAP_FAILED) {
                perror("mmap");
                exit(1);
        }
	wifibroadcast_tx_status_t *tretval = (wifibroadcast_tx_status_t*)retval;
	status_memory_init(tretval);
	return tretval;
}

void telemetry_init(telemetry_data_t *td) 
{
    td->rx_status = telemetry_wbc_status_memory_open();
    td->tx_status = telemetry_wbc_status_memory_open_tx();
}

int main(int argc, char *argv[]) 
{

    setpriority(PRIO_PROCESS, 0, -10);

    char fBrokenSocket = 0;
    int pcnt = 0;
    uint8_t packet_transmit_buffer[MAX_PACKET_LENGTH];
    size_t packet_header_length = 0;
    input_t input;

    int param_data_packets_per_block = 8;
    int param_fec_packets_per_block = 4;
    int param_packet_length = 1024;
    int param_min_packet_length = 24;
    int param_packet_type = 1;
    int param_data_rate = 18;
    int param_transmission_mode = 0;
	int param_wifi_mode = 0;
	int param_wifi_ldpc = 0;
	int param_wifi_stbc = 0;
	int param_encrypt_enable = 0;
	char * param_encrypt_password = NULL;

	struct sockaddr_in udp_addr;
	int udp_sockfd;
	//int udp_slen = sizeof(udp_addr);
	bzero(&udp_addr, sizeof(udp_addr));

    printf("tx_rawsock (c)2017 by Rodizio. Based on wifibroadcast tx by Befinitiv. GPL2 licensed.\n");

	if (argc !=2) {
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
	} 
	
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
		
	
	fprintf(stderr, "%s Config: packet %d/%d/%d, port %d, type %d, rate %d, transmode %d, wifimode %d, nic %s, UDP :%d, encrypt %d, buf %d\n",
		argv[0], param_data_packets_per_block, param_fec_packets_per_block, param_packet_length,
		param_port, param_packet_type, param_data_rate, param_transmission_mode, param_wifi_mode, 
		iniparser_getstring(ini, "tx:nic", NULL), iniparser_getint(ini, "tx:udp_port", 0),
		param_encrypt_enable,
		iniparser_getint(ini, "tx:udp_bufsize", 0));

	udp_addr.sin_family = AF_INET;
	udp_addr.sin_port = htons(iniparser_getint(ini, "tx:udp_port", 0));
	udp_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if ( (udp_sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0 ){
		fprintf(stderr, "ERROR: Could not create UDP socket!");
		exit(1);
	}
/* 	if (-1 == bind(udp_sockfd, (struct sockaddr*)&udp_addr, sizeof(udp_addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		iniparser_freedict(ini);
		close(udp_sockfd);
		return 0;
	} */
	int udp_bufsize = iniparser_getint(ini, "tx:udp_bufsize", 0);
	// We should increase net.core.rmem_max as well (>= udp_bufsize/2)
    setsockopt(udp_sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&udp_bufsize, sizeof(udp_bufsize));
 
	// udp sender option (for LTE)
	int16_t param_udp_send_to_port = 0;
	int16_t param_udp_send_bind_port = 0;
	char * param_udp_send_to_ip = NULL;
	struct sockaddr_in s_udp_addr_send, s_udp_addr_bind;
	bzero(&s_udp_addr_send, sizeof(s_udp_addr_send));
	bzero(&s_udp_addr_bind, sizeof(s_udp_addr_bind));
	int param_udp_send = iniparser_getint(ini, "tx:udp_send", 0);
	if (param_udp_send == 1) {
		param_udp_send_to_port= atoi(iniparser_getstring(ini, "tx:udp_send_to_port", NULL));
		param_udp_send_bind_port= atoi(iniparser_getstring(ini, "tx:udp_send_bind_port", NULL));
		param_udp_send_to_ip = (char *)iniparser_getstring(ini, "tx:udp_send_to_ip", NULL);
		s_udp_addr_send.sin_family = AF_INET;
		s_udp_addr_send.sin_port = htons(param_udp_send_to_port);
		s_udp_addr_send.sin_addr.s_addr = inet_addr(param_udp_send_to_ip);
		s_udp_addr_bind.sin_family = AF_INET;
		s_udp_addr_bind.sin_port = htons(param_udp_send_bind_port);
		s_udp_addr_bind.sin_addr.s_addr = htonl(INADDR_ANY);
			
		if ((udp_send_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) {
			printf("ERROR: Could not create UDP (send) socket!");
		}
		if (-1 == bind(udp_send_fd, (struct sockaddr*)&s_udp_addr_bind, sizeof(s_udp_addr_bind))) {
			fprintf(stderr, "Error: Bind UDP port failed.\n");
			exit(1);
		}
	}
	
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

    input.fd = STDIN_FILENO;	
	//input.fd = udp_sockfd;	// udp mod
    input.seq_nr = 0;
    input.curr_pb = 0;
    input.pbl = lib_alloc_packet_buffer_list(param_data_packets_per_block, MAX_PACKET_LENGTH);

    //prepare the buffers with headers
    int j = 0;
    for (j=0; j<param_data_packets_per_block; ++j) {
    	input.pbl[j].len = 0;
    }

    //initialize forward error correction
    fec_init();

    // initialize telemetry shared mem for rssi based transmission (-y 1)
    telemetry_data_t td;
    telemetry_init(&td);

    //int x = optind;
    int num_interfaces = 0;

/*     while(x < argc && num_interfaces < 4) {
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

    while (!fBrokenSocket) {

		packet_buffer_t *pb = input.pbl + input.curr_pb;

		// if the buffer is fresh we add a payload header
		if(pb->len == 0) {
			pb->len += sizeof(payload_header_t); //make space for a length field (will be filled later)
		}
		
		//read the data
		int inl = read(input.fd, pb->data + pb->len, param_packet_length - pb->len); 
		//int inl = recvfrom(input.fd, pb->data + pb->len, param_packet_length-pb->len, 
		//					0, (struct sockaddr*)&udp_addr, &udp_slen);
		 
		if (inl < 0 || inl > param_packet_length-pb->len) {
			perror("reading input");
			return 1;
		}

		if(inl == 0) { // EOF
			fprintf(stderr, "Warning: Lost connection to stdin. Please make sure that a data source is connected\n");
			usleep(5e5);
			continue;
		}

		pb->len += inl;

		// check if this packet is finished
		if (pb->len >= param_min_packet_length) {
			payload_header_t *ph = (payload_header_t*)pb->data;
			// write the length into the packet. this is needed since with fec we cannot use the wifi packet lentgh anymore.
			// We could also set the user payload to a fixed size but this would introduce additional latency since tx would need to wait until that amount of data has been received
			ph->data_length = pb->len - sizeof(payload_header_t);
			pcnt++;
			// check if this block is finished
			if (input.curr_pb == param_data_packets_per_block-1) {
				pb_transmit_block(input.pbl, &(input.seq_nr), param_port, 
									param_packet_length, packet_transmit_buffer, 
									packet_header_length, param_data_packets_per_block, 
									param_fec_packets_per_block, num_interfaces, 
									param_transmission_mode, &td,
									param_encrypt_enable, param_encrypt_password,
									param_udp_send, &s_udp_addr_send);
				input.curr_pb = 0;
			} else {
				input.curr_pb++;
			}
		}
    }
	close(udp_sockfd);
	iniparser_freedict(ini);
    printf("ERROR: Broken socket!\n");
    return (0);
}

