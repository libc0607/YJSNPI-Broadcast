// tx_telemetry (c)2017 by Rodizio. Based on wifibroadcast tx by Befinitiv. GPL2 licensed.
/*
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
#include "lib.h"
#include "mavlink/common/mavlink.h"
#include "xxtea.h"
#include <fcntl.h>
#include <getopt.h>
#include <iniparser.h>
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
//#include <arpa/inet.h>

int sock = 0;
int socks[5];
int type[5];

//struct timeval time;

mavlink_status_t status;
mavlink_message_t msg;


uint8_t headers_atheros[64]; // header buffer for atheros
uint8_t headers_ralink[64]; // header buffer for ralink
int headers_atheros_len = 0;
int headers_ralink_len = 0;

uint8_t packet_buffer_ath[512]; // wifi packet to be sent (263 + len and seq + radiotap and ieee headers)
uint8_t packet_buffer_ral[512]; // wifi packet to be sent (263 + len and seq + radiotap and ieee headers)

// telemetry frame header consisting of seqnr and payload length
struct header_s {
    uint32_t seqnumber;
    uint16_t length;
} __attribute__ ((__packed__)); // TODO: check if packed works as intended
struct header_s header;

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

    return sock;
}

static u8 u8aRadiotapHeader[] = {
        0x00, 0x00, // <-- radiotap version
        0x0c, 0x00, // <- radiotap header length
        0x04, 0x80, 0x00, 0x00, // <-- radiotap present flags
        0x00, // datarate (will be overwritten later)
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

static u8 u8aIeeeHeader_data[] = {
        0x08, 0x02, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
        0xff, 0x00, 0x00, 0x00, 0x00, 0x00,// 1st byte of MAC will be overwritten with encoded port
        0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // mac
        0x13, 0x22, 0x33, 0x44, 0x55, 0x66, // mac
        0x00, 0x00 // IEEE802.11 seqnum, (will be overwritten later by Atheros firmware/wifi chip)
};

static u8 u8aIeeeHeader_data_short[] = {
        0x08, 0x01, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
        0xff // 1st byte of MAC will be overwritten with encoded port
};

static u8 u8aIeeeHeader_rts[] = {
        0xb4, 0x01, 0x00, 0x00, // frame control field (2 bytes), duration (2 bytes)
        0xff // 1st byte of MAC will be overwritten with encoded port
};

static u8 dummydata[] = {
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd, 0xdd, 0xdd, 0xdd, 0xdd,
        0xdd, 0xdd, 0xdd, 0xdd
};

int flagHelp = 0;

void usage(void) 
{
	printf(
	"tx_telemetry Dirty mod by libc0607@Github\n"
	"\n"
	"Usage: tx_telemetry <config.ini>\n"
	"\n"
	"config.ini example:\n"
	"\n"
	"[tx_telemetry]\n"
	"port=1             	# Port number 0-127 (default 1)\n"
	"cts_protection=0		# CTS protection disabled / CTS protection enabled (Atheros only)\n"
	"retrans_count=2       	# Retransmission count. 1 = send each frame once, 2 = twice, 3 = three times ... Default = 1\n"
	"tele_protocol=1   		# Telemetry protocol to be used. 0 = Mavlink. 1 = generic (for all others)\n"
	"wifimode=0		    	# Wi-Fi mode. 0=802.11g 1=802.11n"
	"ldpc=0		        	# 1-Use LDPC encode, 0-default. Experimental. Only valid when wifimode=n and both your Wi-Fi cards support LDPC."
	"stbc=0					# 0-default, 1-1 STBC stream, 2-2 STBC streams, 3-3 STBC streams. Only valid when wifimode=n and both your Wi-Fi cards support STBC.\n"
	"rate=6             	# Data rate to send frames with. Currently only supported with Ralink cards. Choose 6,12,18,24,36 Mbit\n"
	"mode=0             	# Transmission mode, not used. 0 = send on all interfaces, 1 = send only on interface with best RSSI\n"
	"nic=wlan0          	# Wi-Fi interface\n"
	"encrypt=1				# enable encrypt. Note that when encrypt is enabled, the actual payload length will be reduced by 4\n"
	"password=yarimasune1919810\n"
	""
	);
    exit(1);
}

wifibroadcast_rx_status_t *telemetry_wbc_status_memory_open(void) 
{
    int fd = 0;

    fd = shm_open("/wifibroadcast_rx_status_0", O_RDONLY, S_IRUSR | S_IWUSR);
    if(fd < 0) {
		fprintf(stderr, "ERROR: Could not open wifibroadcast rx status!\n");
		exit(1);
    }

    void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), PROT_READ, MAP_SHARED, fd, 0);
    if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
    return (wifibroadcast_rx_status_t*)retval;
}

void telemetry_init(telemetry_data_t *td) 
{
    td->rx_status = telemetry_wbc_status_memory_open();
}

void sendpacket(uint32_t seqno, uint16_t len, telemetry_data_t *td, 
				int transmission_mode, int num_int, uint8_t data[512],// ???
				int encrypt_enable, char * encrypt_password) 	
{
	header.seqnumber = seqno;
	header.length = len;
//	fprintf(stderr,"seqno: %d",seqno);
	int padlen = 0;
	int best_adapter = 0;
	
	// encrypt data
	uint8_t * p_send_data;
	size_t send_data_length;
	if (encrypt_enable) { 
		p_send_data = xxtea_encrypt(data, len, encrypt_password, &send_data_length);
	} else {
		p_send_data = data;
		send_data_length = len;
	}
	
	if (transmission_mode == 1) {
	    int i;
	    int best_dbm = -1000;
	    // find out which card has best signal
	    for (i=0; i<num_int; ++i) {
	    	if (best_dbm < td->rx_status->adapter[i].current_signal_dbm) {
				best_dbm = td->rx_status->adapter[i].current_signal_dbm;
				best_adapter = i;
			}
	    }
//	    printf ("bestadapter: %d (%d dbm)\n",best_adapter, best_dbm);
	    if (type[best_adapter] == 0) { 
			// Atheros
			// telemetry header (seqno and len)
			memcpy(packet_buffer_ath + headers_atheros_len, &header, sizeof(header));
			// telemetry data
			memcpy(packet_buffer_ath + headers_atheros_len + sizeof(header), p_send_data, send_data_length);
			if (send_data_length < 5) { 
				// if telemetry payload is too short, pad to minimum length
				padlen = 5 - send_data_length;
//			    fprintf(stderr, "padlen: %d ",padlen);
				memcpy(packet_buffer_ath + headers_atheros_len + sizeof(header) + send_data_length, dummydata, padlen);
			}
	        if (write(socks[best_adapter], &packet_buffer_ath, 
						headers_atheros_len + sizeof(header) + send_data_length + padlen) < 0 ) {
				fprintf(stderr, ".");
			}	
	    } else { // Ralink
			// telemetry header (seqno and len)
			memcpy(packet_buffer_ral + headers_ralink_len, &header, sizeof(header));
			// telemetry data
			memcpy(packet_buffer_ral + headers_ralink_len + sizeof(header), p_send_data, send_data_length);
			if (send_data_length < 18) { 
				// if telemetry payload is too short, pad to minimum length
				padlen = 18 - send_data_length;
//			    fprintf(stderr, "padlen: %d ",padlen);
				memcpy(packet_buffer_ral + headers_ralink_len + sizeof(header) + send_data_length, dummydata, padlen);
			}
			if (write(socks[best_adapter], &packet_buffer_ral, 
						headers_ralink_len + sizeof(header) + send_data_length + padlen) < 0 ) {
				fprintf(stderr, ".");
			}	
		}
	} else { 
		// transmit on all interfaces
	    int i;
	    for(i=0; i<num_int; ++i) {
			// Atheros
			if (type[i] == 0) { 
//		    	fprintf(stderr,"type: Atheros");
				// telemetry header (seqno and len)
				memcpy(packet_buffer_ath + headers_atheros_len, &header, 6);
				// telemetry data
				memcpy(packet_buffer_ath + headers_atheros_len + 6, p_send_data, send_data_length);
//			    fprintf(stderr," lendata:%d ",len);

				// if telemetry payload is too short, pad to minimum length
				if (send_data_length < 5) { 
					padlen = 5 - send_data_length;
//					fprintf(stderr, "padlen: %d ",padlen);
					memcpy(packet_buffer_ath + headers_atheros_len + 6 + send_data_length, dummydata, padlen);
				}
				
//			    int x = 0;
//			    int dumplen = 100;
//			    fprintf(stderr,"buf:");
//			    for (x=12;x < dumplen; x++) {
//				fprintf(stderr,"0x%02x ", packet_buffer[x]);
//			    }
//			    fprintf(stderr,"\n");
//			    fprintf(stderr," headers_atheros_len:%d ",headers_atheros_len);
//			    fprintf(stderr," writelen:%d ",headers_atheros_len + 4 + send_data_length);
				if (write(socks[i], &packet_buffer_ath, headers_atheros_len + 6 + send_data_length + padlen) < 0 ) 
					fprintf(stderr, ".");
			} else { 
				// Ralink
//			    fprintf(stderr,"type: Ralink");
				// telemetry header (seqno and len)
				memcpy(packet_buffer_ral + headers_ralink_len, &header, 6);
				// telemetry data
				memcpy(packet_buffer_ral + headers_ralink_len + 6, p_send_data, send_data_length);
				if (send_data_length < 18) { 
					// pad to minimum length
					padlen = 18 - send_data_length;
//					fprintf(stderr, "padlen: %d ",padlen);
					memcpy(packet_buffer_ral + headers_ralink_len + 6 + send_data_length, dummydata, padlen);
				}
				if (write(socks[i], &packet_buffer_ral, 
							headers_ralink_len + 6 + send_data_length + padlen) < 0 ) {
					fprintf(stderr, ".");
				}
					
			}
	    }
	}
	if (encrypt_enable) {
		free(p_send_data);
	}
}

long long current_timestamp() {
    struct timeval te;
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    return milliseconds;
}

void print_mavlink_debug_info() {
	switch (msg.msgid){
	case MAVLINK_MSG_ID_SYS_STATUS:
		fprintf(stderr, "SYS_STATUS ");
		break;
	case MAVLINK_MSG_ID_HEARTBEAT:
		fprintf(stderr, "HEARTBEAT ");
		break;
	case MAVLINK_MSG_ID_COMMAND_ACK:
		fprintf(stderr, "COMMAND_ACK:%d ",
					mavlink_msg_command_ack_get_command(&msg));
		break;
	case MAVLINK_MSG_ID_COMMAND_INT:
		fprintf(stderr, "COMMAND_INT:%d ",
					mavlink_msg_command_int_get_command(&msg));
		break;
	case MAVLINK_MSG_ID_EXTENDED_SYS_STATE:
		fprintf(stderr, "EXTENDED_SYS_STATE: vtol_state:%d, landed_state:%d",
					mavlink_msg_extended_sys_state_get_vtol_state(&msg),
					mavlink_msg_extended_sys_state_get_landed_state(&msg));
		break;
	case MAVLINK_MSG_ID_LOCAL_POSITION_NED:
		fprintf(stderr, "LOCAL_POSITION_NED ");
		break;
	case MAVLINK_MSG_ID_POSITION_TARGET_LOCAL_NED:
		fprintf(stderr, "POSITION_TARGET_LOCAL_NED ");
		break;
	case MAVLINK_MSG_ID_POSITION_TARGET_GLOBAL_INT:
		fprintf(stderr, "POSITION_TARGET_GLOBAL_INT ");
		break;
	case MAVLINK_MSG_ID_ESTIMATOR_STATUS:
		fprintf(stderr, "ESTIMATOR_STATUS ");
		break;
	case MAVLINK_MSG_ID_HOME_POSITION:
		fprintf(stderr, "HIGHRES_IMU ");
		break;
	case MAVLINK_MSG_ID_NAMED_VALUE_FLOAT:
		fprintf(stderr, "NAMED_VALUE_FLOAT ");
		break;
	case MAVLINK_MSG_ID_NAMED_VALUE_INT:
		fprintf(stderr, "NAMED_VALUE_INT ");
		break;
	case MAVLINK_MSG_ID_PARAM_VALUE:
		fprintf(stderr, "PARAM_VALUE ");
		break;
	case MAVLINK_MSG_ID_PARAM_SET:
		fprintf(stderr, "PARAM_SET ");
		break;
	case MAVLINK_MSG_ID_PARAM_REQUEST_READ:
		fprintf(stderr, "PARAM_REQUEST_READ ");
		break;
	case MAVLINK_MSG_ID_PARAM_REQUEST_LIST:
		fprintf(stderr, "PARAM_REQUEST_LIST ");
		break;
	case MAVLINK_MSG_ID_RC_CHANNELS_OVERRIDE:
		fprintf(stderr, "RC_CHANNELS_OVERRIDE ");
		break;
	case MAVLINK_MSG_ID_RC_CHANNELS:
		fprintf(stderr, "RC_CHANNELS ");
		break;
	case MAVLINK_MSG_ID_MANUAL_CONTROL:
		fprintf(stderr, "MANUAL_CONTROL ");
		break;
	case MAVLINK_MSG_ID_COMMAND_LONG:
		fprintf(stderr, "COMMAND_LONG:%d ",
					mavlink_msg_command_long_get_command(&msg));
		break;
	case MAVLINK_MSG_ID_STATUSTEXT:
		fprintf(stderr, "STATUSTEXT: severity:%d ",
					mavlink_msg_statustext_get_severity(&msg));
		break;
	case MAVLINK_MSG_ID_SYSTEM_TIME:
		fprintf(stderr, "SYSTEM_TIME ");
		break;
	case MAVLINK_MSG_ID_PING:
		fprintf(stderr, "PING ");
		break;
	case MAVLINK_MSG_ID_CHANGE_OPERATOR_CONTROL:
		fprintf(stderr, "CHANGE_OPERATOR_CONTROL ");
		break;
	case MAVLINK_MSG_ID_CHANGE_OPERATOR_CONTROL_ACK:
		fprintf(stderr, "CHANGE_OPERATOR_CONTROL_ACK ");
		break;
	case MAVLINK_MSG_ID_MISSION_WRITE_PARTIAL_LIST:
		fprintf(stderr, "MISSION_WRITE_PARTIAL_LIST ");
		break;
	case MAVLINK_MSG_ID_MISSION_ITEM:
		fprintf(stderr, "MISSION_ITEM ");
		break;
	case MAVLINK_MSG_ID_MISSION_REQUEST:
		fprintf(stderr, "MISSION_REQUEST ");
		break;
	case MAVLINK_MSG_ID_MISSION_SET_CURRENT:
		fprintf(stderr, "MISSION_SET_CURRENT ");
		break;
	case MAVLINK_MSG_ID_MISSION_CURRENT:
		fprintf(stderr, "MISSION_CURRENT ");
		break;
	case MAVLINK_MSG_ID_MISSION_REQUEST_LIST:
		fprintf(stderr, "MISSION_REQUEST_LIST ");
		break;
	case MAVLINK_MSG_ID_MISSION_COUNT:
		fprintf(stderr, "MISSION_COUNT ");
		break;
	case MAVLINK_MSG_ID_MISSION_CLEAR_ALL:
		fprintf(stderr, "MISSION_CLEAR_ALL ");
		break;
	case MAVLINK_MSG_ID_MISSION_ACK:
		fprintf(stderr, "MISSION_ACK ");
		break;
	case MAVLINK_MSG_ID_MISSION_ITEM_INT:
		fprintf(stderr, "MISSION_ITEM_INT ");
		break;
	case MAVLINK_MSG_ID_MISSION_REQUEST_INT:
		fprintf(stderr, "MISSION_REQUEST_INT ");
		break;
	case MAVLINK_MSG_ID_SET_MODE:
		fprintf(stderr, "SET_MODE ");
		break;
	case MAVLINK_MSG_ID_REQUEST_DATA_STREAM:
		fprintf(stderr, "REQUEST_DATA_STREAM ");
		break;
	case MAVLINK_MSG_ID_DATA_STREAM:
		fprintf(stderr, "DATA_STREAM ");
		break;
	default:
		fprintf(stderr, "OTHER MESSAGE ID:%d ",msg.msgid);
		break;
	}
	fprintf(stderr, "\n");
}

int main(int argc, char *argv[]) 
{
    char fBrokenSocket = 0;
    char line[128], path[128];
    FILE* procfile;

    int pcnt = 0;
    int port_encoded = 0;
    int param_cts = 0;
    int param_port = 1;
    int param_retransmissions = 1;
    int param_telemetry_protocol = 0;
    int param_data_rate = 6;
    int param_transmission_mode = 0;
    int param_debug = 0;
	int param_wifimode = 0;
	int param_ldpc = 0;
	int param_stbc = 0;
	int param_encrypt_enable = 0;
	char * param_encrypt_password = NULL;

    uint8_t buf[512]; // data read from stdin
    uint8_t mavlink_message[512];

    uint16_t len_msg = 0;
    uint32_t seqno = 0;

    fprintf(stdout,"tx_telemetry (c)2017 by Rodizio. Based on wifibroadcast tx by Befinitiv. GPL2 licensed.\n");
	fprintf(stdout, "Dirty mod by @libc0607\n");

	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	if (argc !=2) {
		usage();
	}
	param_cts = iniparser_getint(ini, "tx_telemetry:cts_protection", 0);
	param_port = iniparser_getint(ini, "tx_telemetry:port", 0);
	param_retransmissions = iniparser_getint(ini, "tx_telemetry:retrans_count", 0);
	param_telemetry_protocol = iniparser_getint(ini, "tx_telemetry:tele_protocol", 0);
	param_data_rate = iniparser_getint(ini, "tx_telemetry:rate", 0);
	param_transmission_mode = iniparser_getint(ini, "tx_telemetry:mode", 0);
	param_wifimode = (0 == iniparser_getint(ini, "tx:wifimode", 0))? 0: 1;
	param_ldpc = iniparser_getint(ini, "tx_telemetry:ldpc", 0);
	param_stbc = iniparser_getint(ini, "tx_telemetry:stbc", 0);
	
	param_encrypt_enable = iniparser_getint(ini, "tx_telemetry:encrypt", 0);
	if (param_encrypt_enable == 1) {
		param_encrypt_password = (char *)iniparser_getstring(ini, "tx_telemetry:password", NULL);
	}
	fprintf(stderr, "%s Config: cts %d, port %d, retrans %d, proto %d, rate %d, mode %d, wifimode %d, ldpc %d, encrypt %d, nic %s\n",
			argv[0], param_cts, param_port, param_retransmissions, param_telemetry_protocol,
			param_data_rate, param_transmission_mode, param_wifimode, param_ldpc,
			param_encrypt_enable,
			iniparser_getstring(ini, "tx_telemetry:nic", NULL)
	);
    int x = optind;
    int num_interfaces = 0;

	
	// ini supports only support one interface now
	// should be fixed later
	snprintf(path, 45, "/sys/class/net/%s/device/uevent", 
					iniparser_getstring(ini, "tx_telemetry:nic", NULL));
	procfile = fopen(path, "r");
	if (!procfile) {
		fprintf(stderr,"ERROR: opening %s failed!\n", path); 
		return 0;
	}
	
	// read the first line
	fgets(line, 100, procfile); 
	if (strncmp(line, "DRIVER=ath9k", 12) == 0) { 
		// it's an atheros card
		fprintf(stderr, "tx_telemetry: Atheros card detected\n");
		type[num_interfaces] = 0;
	} else { 
		// ralink
		fprintf(stderr, "tx_telemetry: Ralink (or other) card detected\n");
		type[num_interfaces] = 1;
	}
	socks[num_interfaces] = open_sock((char *)iniparser_getstring(ini, "tx_telemetry:nic", NULL));
	++num_interfaces;
	++x;
	fclose(procfile);
	usleep(10000); 


    // initialize telemetry shared mem for rssi based transmission (-y 1)
    telemetry_data_t td;
    telemetry_init(&td);
	
	// Set bitrate
	if (param_wifimode == 0) {
		switch (param_data_rate) {
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
			default: fprintf(stderr, "tx_telemetry: ERROR: Wrong or no data rate specified\n"); 
				exit(1); break;
		}
	} else if (param_wifimode == 1) {
		u8aRadiotapHeader80211n[12] = (uint8_t) param_data_rate;
	}

	// Set port
    port_encoded = (param_port * 2) + 1;
    u8aIeeeHeader_rts[4] = port_encoded;
    u8aIeeeHeader_data[4] = port_encoded;
    u8aIeeeHeader_data_short[4] = port_encoded;

    // for Atheros use data frames if CTS protection enabled or rts if disabled
    // CTS protection causes R/C transmission to stop for some reason, 
	// always use rts frames (i.e. no cts protection)
	
	int rtheader_length = 0;
	if (param_wifimode == 0) {	// 802.11g
		memcpy(headers_atheros, u8aRadiotapHeader, sizeof(u8aRadiotapHeader)); // radiotap header
		memcpy(headers_ralink, u8aRadiotapHeader, sizeof(u8aRadiotapHeader));// radiotap header
		rtheader_length = sizeof(u8aRadiotapHeader);
	} else if (param_wifimode == 1) {	// 802.11n
		if (param_ldpc == 0) {
			u8aRadiotapHeader80211n[10] &= (~0x10);
			u8aRadiotapHeader80211n[11] &= (~0x10);
		} else if (param_ldpc == 1){
			u8aRadiotapHeader80211n[10] |= 0x10;
			u8aRadiotapHeader80211n[11] |= 0x10;
		}
		switch (param_stbc) {
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
			u8aRadiotapHeader80211n[10] &= (~0x20);
			u8aRadiotapHeader80211n[11] &= (~0x60);
		break;
		}
		memcpy(headers_atheros, u8aRadiotapHeader80211n, sizeof(u8aRadiotapHeader80211n)); // radiotap header
		memcpy(headers_ralink, u8aRadiotapHeader80211n, sizeof(u8aRadiotapHeader80211n));// radiotap header
		rtheader_length = sizeof(u8aRadiotapHeader80211n);
	}
	
    if (param_cts == 1) { // use data frames
		memcpy(headers_atheros + rtheader_length, 
				u8aIeeeHeader_data, sizeof(u8aIeeeHeader_data)); // ieee header
		headers_atheros_len = rtheader_length + sizeof(u8aIeeeHeader_data);
    } else { // use rts frames
		memcpy(headers_atheros + rtheader_length, 
				u8aIeeeHeader_rts, sizeof(u8aIeeeHeader_rts)); // ieee header
		headers_atheros_len = rtheader_length + sizeof(u8aIeeeHeader_rts);
    }

    // for Ralink always use data short
    memcpy(headers_ralink+sizeof(u8aRadiotapHeader), 
			u8aIeeeHeader_data_short, sizeof(u8aIeeeHeader_data_short));// ieee header
    headers_ralink_len = sizeof(u8aRadiotapHeader) + sizeof(u8aIeeeHeader_data_short);

    // radiotap and ieee headers
    memcpy(packet_buffer_ath, headers_atheros, headers_atheros_len);
    memcpy(packet_buffer_ral, headers_ralink, headers_ralink_len);

    long long prev_time = current_timestamp();
    long long prev_time_read = current_timestamp();

    while (!fBrokenSocket) {
		int inl = read(STDIN_FILENO, buf, 350); // read the data
		if (param_debug == 1) {
			long long took_read = current_timestamp() - prev_time_read;
			prev_time_read = current_timestamp();
			fprintf(stderr, "read delta:%lldms bytes:%d ",took_read,inl);
		}
		if (inl < 0) { 
			fprintf(stderr,"tx_telemetry: ERROR: reading stdin"); 
			return 1; 
		} else if (inl > 350) { 
			fprintf(stderr,"tx_telemetry: Warning: Input data > 300 bytes"); 
			continue; 
		} else if (inl == 0) { 
			fprintf(stderr, "tx_telemetry: Warning: Lost connection to stdin\n"); 
			usleep(1e5); 
			continue;
		} // EOF

		if (param_telemetry_protocol == 0) { 
			// parse Mavlink
			for(int i=0; i<inl; i++) {
				uint8_t c = buf[i];
				if (!mavlink_parse_char(0, c, &msg, &status)) {
					continue;
				}
				if (param_debug == 1) {
					long long took = current_timestamp() - prev_time;
					prev_time = current_timestamp();
					fprintf(stderr, "Msg delta:%lldms seq:%d  sysid:%d, compid:%d  ",
												took, msg.seq, msg.sysid, msg.compid);
					print_mavlink_debug_info();
				}
				len_msg = mavlink_msg_to_send_buffer(mavlink_message, &msg);
				for (int k=0; k<param_retransmissions; k++) {
					sendpacket(seqno, len_msg, &td, param_transmission_mode, 
											num_interfaces, mavlink_message,
											param_encrypt_enable, param_encrypt_password);
					usleep(300*(k+1)*1.4);
				}
				pcnt++;
				seqno++;
			}
		} else { 
			// generic telemetry handling
			for (int k=0; k<param_retransmissions; k++) {
				sendpacket(seqno, inl, &td, param_transmission_mode, num_interfaces, buf,
										param_encrypt_enable, param_encrypt_password);
				usleep(300*(k+1)*1.4);
			}
			pcnt++;
			seqno++;
		}

		if(pcnt % 128 == 0) {
			printf("\t\t%d packets sent\r", pcnt);
			fflush(stdout);
		}
    }

    printf("TX_TELEMETRY ERROR: Broken socket!\n");
	iniparser_freedict(ini);
    return (0);
}
