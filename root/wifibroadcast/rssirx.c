// rssirx by Rodizio. Based on wifibroadcast rx by Befinitiv. Licensed under GPL2
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
#include "radiotap.h"
#include "xxtea.h"
#include <iniparser.h>
#include <time.h>
#include <sys/resource.h>

// Data frame: ether[0x00:1] == 0x08
//#define PCAP_FILTER_CHAR "ether[0x00:2] == 0x0802 && ether[0x04:1] == 0xff"
// RTS Frame: ether[0x00:1] == 0xB4
#define PCAP_FILTER_CHAR "ether[0x00:2] == 0xB402 && ether[0x04:1] == 0xff"
// We should use RTS for default.

#define PROGRAM_NAME "rssirx"

// this is where we store a summary of the information from the radiotap header
typedef struct  {
	int m_nChannel;
	int m_nChannelFlags;
	int m_nRate;
	int m_nAntenna;
	int m_nRadiotapFlags;
} __attribute__((packed)) PENUMBRA_RADIOTAP_DATA;


int flagHelp = 0;
int param_encrypt_enable = 0;
char * param_encrypt_password = NULL;

wifibroadcast_rx_status_t *rx_status = NULL;
wifibroadcast_rx_status_t_rc *rx_status_rc = NULL;
wifibroadcast_rx_status_t_sysair *rx_status_sysair = NULL;

void usage(void) {
	printf(
	    "rssirx by Rodizio. Based on wifibroadcast rx by Befinitiv. Licensed under GPL2\n\n"
	    "Usage: rssirx <config.file>\n\n"
	    "config example:\n"
		"[rssirx]\n"
		"nic=wlan0\n"
		"encrypt=1\n"
		"password=1919810\n\n");
	exit(1);
}

typedef struct {
	pcap_t *ppcap;
	int selectable_fd;
	int n80211HeaderLength;
} monitor_interface_t;


void open_and_configure_interface(const char *name, monitor_interface_t *interface) 
{
	struct bpf_program bpfprogram;
	char szProgram[512];
	char szErrbuf[PCAP_ERRBUF_SIZE];

	// open the interface in pcap
	szErrbuf[0] = '\0';

	interface->ppcap = pcap_open_live(name, 120, 0, -1, szErrbuf);
	if (interface->ppcap == NULL) {
		fprintf(stderr, "Unable to open %s: %s\n", name, szErrbuf);
		exit(1);
	}
	
	if(pcap_setnonblock(interface->ppcap, 1, szErrbuf) < 0) {
		fprintf(stderr, "Error setting %s to nonblocking mode: %s\n", name, szErrbuf);
	}

	int nLinkEncap = pcap_datalink(interface->ppcap);

	if (nLinkEncap == DLT_IEEE802_11_RADIO) {
		interface->n80211HeaderLength = 0x18; // 24 bytes
		sprintf(szProgram, PCAP_FILTER_CHAR); // match on frametype, 1st byte of mac (ff) and portnumber (255 = 127 for rssi)
	} else {
		fprintf(stderr, "ERROR: unknown encapsulation on %s! check if monitor mode is supported and enabled\n", name);
		exit(1);
	}

	if (pcap_compile(interface->ppcap, &bpfprogram, szProgram, 1, 0) == -1) {
		puts(szProgram);
		puts(pcap_geterr(interface->ppcap));
		exit(1);
	} else {
		if (pcap_setfilter(interface->ppcap, &bpfprogram) == -1) {
			fprintf(stderr, "%s\n", szProgram);
			fprintf(stderr, "%s\n", pcap_geterr(interface->ppcap));
		} else {
		}
		pcap_freecode(&bpfprogram);
	}

	interface->selectable_fd = pcap_get_selectable_fd(interface->ppcap);
}

uint8_t process_packet(monitor_interface_t *interface, int adapter_no) {
	struct pcap_pkthdr * ppcapPacketHeader = NULL;
	struct ieee80211_radiotap_iterator rti;
	PENUMBRA_RADIOTAP_DATA prd;
	u8 payloadBuffer[100];
	u8 *pu8Payload = payloadBuffer;
	int bytes, n, retval, u16HeaderLen;

	bzero(&prd, sizeof(prd));
	
	struct payloaddata_s {
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

	struct payloaddata_s payloaddata;

	retval = pcap_next_ex(interface->ppcap, &ppcapPacketHeader,(const u_char**)&pu8Payload); // receive

	if (retval < 0) {
		if (strcmp("The interface went down",pcap_geterr(interface->ppcap)) == 0) {
		    fprintf(stderr, "rx: The interface went down\n");
		    exit(9);
		} else {
		    fprintf(stderr, "rx: %s\n", pcap_geterr(interface->ppcap));
		    exit(2);
		}
	}

	if (retval != 1) 
		return 0;

	// fetch radiotap header length from radiotap header (seems to be 36 for Atheros and 18 for Ralink)
	u16HeaderLen = (pu8Payload[2] + (pu8Payload[3] << 8));
//	fprintf(stderr, "u16headerlen: %d\n", u16HeaderLen);
//	fprintf(stderr, "ppcapPacketHeader->len: %d\n", ppcapPacketHeader->len);
	if (ppcapPacketHeader->len < (u16HeaderLen + interface->n80211HeaderLength)) 
		exit(1);

	bytes = ppcapPacketHeader->len - (u16HeaderLen + interface->n80211HeaderLength);

	if (bytes < 0) 
		return(0);
	if (ieee80211_radiotap_iterator_init(&rti, (struct ieee80211_radiotap_header *)pu8Payload, 
										ppcapPacketHeader->len) < 0) 
		exit(1);

	while ((n = ieee80211_radiotap_iterator_next(&rti)) == 0) {
		switch (rti.this_arg_index) {
		case IEEE80211_RADIOTAP_FLAGS:
			prd.m_nRadiotapFlags = *rti.this_arg;
			break;
//		case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
//			rx_status->adapter[adapter_no].current_signal_dbm = (int8_t)(*rti.this_arg);
//			break;
		}
	}
	pu8Payload += u16HeaderLen + interface->n80211HeaderLength;
	
	// decrypt if needed
	uint8_t * p_write_data;
	size_t write_data_length;
	// decrypt length: min( (smallest value (bigger than sizeof(payloaddata) & divided by 4))+4,
	//								bytes)
	if (param_encrypt_enable) {
		int decrypt_length = ( (sizeof(payloaddata)/4) +2)*4;
		if (decrypt_length != bytes) {
			fprintf(stderr, "Warning: the packet length is not equal to that we need(got %d, expected %d). Maybe rssitx is not the same version?\n", bytes, decrypt_length);
		}
		decrypt_length = (decrypt_length > bytes)? bytes: decrypt_length;
		p_write_data = xxtea_decrypt(pu8Payload, decrypt_length, 
								param_encrypt_password, &write_data_length);
	} else {
		p_write_data = pu8Payload;
		write_data_length = sizeof(payloaddata);
	}
	// copy payloaddata (signal, lost packets count, cpuload, temp, ....) to struct
	memcpy(&payloaddata, p_write_data, write_data_length); 
	// free memory
	if (param_encrypt_enable) {
		free(p_write_data);
	}

//	printf ("signal:%d\n",payloaddata.signal);
//	printf ("lostpackets:%d\n",payloaddata.lostpackets);
//	printf ("signal_rc:%d\n",payloaddata.signal_rc);
//	printf ("lostpackets_rc:%d\n",payloaddata.lostpackets_rc);
//	printf ("cpuload:%d\n",payloaddata.cpuload);
//	printf ("temp:%d\n",payloaddata.temp);
//	printf ("injected_blocl_cnt:%d\n",payloaddata.injected_block_cnt);

//	printf ("bitrate_kbit:%d\n",payloaddata.bitrate_kbit);
//	printf ("bitrate_measured_kbit:%d\n",payloaddata.bitrate_measured_kbit);

//	printf ("cts:%d\n",payloaddata.cts);
//	printf ("undervolt:%d\n",payloaddata.undervolt);

	rx_status->adapter[0].current_signal_dbm = payloaddata.signal;
	rx_status->lost_packet_cnt = payloaddata.lostpackets;
	rx_status_rc->adapter[0].current_signal_dbm = payloaddata.signal_rc;
	rx_status_rc->lost_packet_cnt = payloaddata.lostpackets_rc;
	rx_status_sysair->cpuload = payloaddata.cpuload;
	rx_status_sysair->temp = payloaddata.temp;
	rx_status_sysair->cpuload_wrt = payloaddata.cpuload_wrt;
	rx_status_sysair->temp_wrt = payloaddata.temp_wrt;

	rx_status_sysair->skipped_fec_cnt = payloaddata.skipped_fec_cnt;
	rx_status_sysair->injected_block_cnt = payloaddata.injected_block_cnt;
	rx_status_sysair->injection_fail_cnt = payloaddata.injection_fail_cnt;
	rx_status_sysair->injection_time_block = payloaddata.injection_time_block;

	rx_status_sysair->bitrate_kbit = payloaddata.bitrate_kbit;
	rx_status_sysair->bitrate_measured_kbit = payloaddata.bitrate_measured_kbit;

	rx_status_sysair->cts = payloaddata.cts;
	rx_status_sysair->undervolt = payloaddata.undervolt;

//	write(STDOUT_FILENO, pu8Payload, 18);
	return(0);
}

void status_memory_init(wifibroadcast_rx_status_t *s) {
	s->received_block_cnt = 0;
	s->damaged_block_cnt = 0;
	s->received_packet_cnt = 0;
	s->lost_packet_cnt = 0;
	s->tx_restart_cnt = 0;
	s->wifi_adapter_cnt = 0;

	int i;
	for(i=0; i<8; ++i) {
		s->adapter[i].received_packet_cnt = 0;
		s->adapter[i].wrong_crc_cnt = 0;
		s->adapter[i].current_signal_dbm = -126;
	}
}

void status_memory_init_rc(wifibroadcast_rx_status_t_rc *s) {
	s->received_block_cnt = 0;
	s->damaged_block_cnt = 0;
	s->received_packet_cnt = 0;
	s->lost_packet_cnt = 0;
	s->tx_restart_cnt = 0;
	s->wifi_adapter_cnt = 0;

	int i;
	for(i=0; i<8; ++i) {
		s->adapter[i].received_packet_cnt = 0;
		s->adapter[i].wrong_crc_cnt = 0;
		s->adapter[i].current_signal_dbm = -126;
	}
}

void status_memory_init_sysair(wifibroadcast_rx_status_t_sysair *s) {
	s->cpuload = 0;
	s->temp = 0;
	s->skipped_fec_cnt = 0;
	s->injected_block_cnt = 0;
	s->injection_fail_cnt = 0;
	s->injection_time_block = 0;
	s->bitrate_kbit = 0;
	s->bitrate_measured_kbit = 0;
	s->cts = 0;
	s->undervolt = 0;
}

wifibroadcast_rx_status_t *status_memory_open(void) {
	char buf[128];
	int fd;
	sprintf(buf, "/wifibroadcast_rx_status_uplink");
	fd = shm_open(buf, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { perror("shm_open"); exit(1); }
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
	wifibroadcast_rx_status_t *tretval = (wifibroadcast_rx_status_t*)retval;
	status_memory_init(tretval);
	return tretval;
}

wifibroadcast_rx_status_t_rc *status_memory_open_rc(void) {
	char buf[128];
	int fd;
	sprintf(buf, "/wifibroadcast_rx_status_rc");
	fd = shm_open(buf, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { perror("shm_open"); exit(1); }
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_rc), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
	wifibroadcast_rx_status_t_rc *tretval = (wifibroadcast_rx_status_t_rc*)retval;
	status_memory_init_rc(tretval);
	return tretval;
}

wifibroadcast_rx_status_t_sysair *status_memory_open_sysair(void) {
	char buf[128];
	int fd;
	sprintf(buf, "/wifibroadcast_rx_status_sysair");
	fd = shm_open(buf, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) { perror("shm_open"); exit(1); }
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_sysair), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { perror("mmap"); exit(1); }
	wifibroadcast_rx_status_t_sysair *tretval = (wifibroadcast_rx_status_t_sysair*)retval;
	status_memory_init_sysair(tretval);
	return tretval;
}

int main(int argc, char *argv[]) 
{
	printf("RSSI RX started\n");
	setpriority(PRIO_PROCESS, 0, 10);

	monitor_interface_t interfaces[8];
	int num_interfaces = 0;
	int i;
	//int result;
	
	if (argc !=2) {
		usage();
	}
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	
	// support only one wi-fi interface; will be fixed later (¹¾¹¾¹¾)
	open_and_configure_interface((char *)iniparser_getstring(ini, "rssirx:nic", NULL), 
									interfaces + num_interfaces);
	++num_interfaces;
	usleep(10000); // wait a bit between configuring interfaces to reduce Atheros and Pi USB flakiness
	
	param_encrypt_enable = iniparser_getint(ini, "rssirx:encrypt", 0);
	if (param_encrypt_enable == 1) {
		param_encrypt_password = (char *)iniparser_getstring(ini, "rssirx:password", NULL);
	}
	
	rx_status = status_memory_open();
	rx_status->wifi_adapter_cnt = num_interfaces;
	rx_status_rc = status_memory_open_rc();
	rx_status_rc->wifi_adapter_cnt = num_interfaces;
	rx_status_sysair = status_memory_open_sysair();
	
	int fd_sum = 0;
	for (;;) {
		fd_set readset;
		struct timeval to;

		to.tv_sec = 0;
		to.tv_usec = 1e5;
	
		FD_ZERO(&readset);
		for (i=0; i<num_interfaces; ++i) {
			FD_SET(interfaces[i].selectable_fd, &readset);
			fd_sum += interfaces[i].selectable_fd;
		}

		int n = select(fd_sum+1, &readset, NULL, NULL, &to);

		for (i=0; i<num_interfaces; ++i) {
			if (n == 0) {
//			    printf("n == 0\n");
		    	    //break;
			}
			if (FD_ISSET(interfaces[i].selectable_fd, &readset)) {
			    //result = process_packet(interfaces + i, i);
				process_packet(interfaces + i, i);
			}
		}
	}

	return (0);
}
