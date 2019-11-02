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

// We should use RTS for default.
// Data frame: ether[0x00:1] == 0x08
//#define PCAP_FILTER_CHAR "ether[0x00:2] == 0x0802 && ether[0x04:1] == 0xff"
// RTS Frame: ether[0x00:1] == 0xB4
#define PCAP_FILTER_CHAR "ether[0x00:2] == 0xB402 && ether[0x04:1] == 0xff"

#define PROGRAM_NAME "rssirx"

// this is where we store a summary of the information from the radiotap header
typedef struct  {
	int m_nChannel;
	int m_nChannelFlags;
	int m_nRate;
	int m_nAntenna;
	int m_nRadiotapFlags;
} __attribute__((packed)) PENUMBRA_RADIOTAP_DATA;

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

int flagHelp = 0;
int param_encrypt_enable = 0;
char * param_encrypt_password = NULL;

wifibroadcast_rx_status_t *rx_status = NULL;
wifibroadcast_rx_status_t_rc *rx_status_rc = NULL;
wifibroadcast_rx_status_t_sysair *rx_status_sysair = NULL;

void usage(void) {
	printf(
	    PROGRAM_NAME" by Rodizio. Based on wifibroadcast rx by Befinitiv. Licensed under GPL2\n\n"
	    "Usage: "PROGRAM_NAME" <config.file>\n\n"
	    "config example:\n"
		"["PROGRAM_NAME"]\n"
		"nic=wlan0\n"
		"encrypt=0\n"
		"mode=0		#0-wifi, 1-udp, 2-both"
		"udp_listen_port=20393 "
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

// input: monitor_interface_t *interface
// output: uint8_t * buf, size_t *len
// receive packet (not decrypted) and save to *buf 
int receive_packet(monitor_interface_t *interface, uint8_t * buf, size_t *len) 
{
	struct pcap_pkthdr * ppcapPacketHeader = NULL;
	struct ieee80211_radiotap_iterator rti;
	PENUMBRA_RADIOTAP_DATA prd;
	u8 payloadBuffer[512];
	u8 *pu8Payload = payloadBuffer;
	int bytes, n, retval, u16HeaderLen;
	struct payloaddata_s payloaddata;
	
	retval = pcap_next_ex(interface->ppcap, &ppcapPacketHeader,(const u_char**)&pu8Payload); 
	if (retval < 0) {
		if (strcmp("The interface went down",pcap_geterr(interface->ppcap)) == 0) {
		    fprintf(stderr, PROGRAM_NAME": The interface went down\n");
		    exit(9);
		} else {
		    fprintf(stderr, PROGRAM_NAME": %s\n", pcap_geterr(interface->ppcap));
		    exit(2);
		}
	}
	if (retval != 1) 
		return 0;

	// fetch radiotap header length from radiotap header 
	u16HeaderLen = (pu8Payload[2] + (pu8Payload[3] << 8));
	if (ppcapPacketHeader->len < (u16HeaderLen + interface->n80211HeaderLength)) 
		exit(1);
	bytes = ppcapPacketHeader->len - (u16HeaderLen + interface->n80211HeaderLength);
	if (bytes < 0) 
		return 0;
	if (ieee80211_radiotap_iterator_init(&rti, (struct ieee80211_radiotap_header *)pu8Payload, 
										ppcapPacketHeader->len) < 0) 
		exit(1);
		
	bzero(&prd, sizeof(prd));
	while ((n = ieee80211_radiotap_iterator_next(&rti)) == 0) {
		switch (rti.this_arg_index) {
		case IEEE80211_RADIOTAP_FLAGS:
			prd.m_nRadiotapFlags = *rti.this_arg;
			break;
		}
	}
	
	pu8Payload += u16HeaderLen + interface->n80211HeaderLength;
	buf = pu8Payload;
	*len = bytes;
	return 0;
}

// input: raw_buffer, raw_length, encrypt_enable, 
// output: decrypted_buffer, decryped_length
int decrypt_payload(uint8_t * raw, size_t * raw_len, int en, char * pwd, 
					uint8_t * buf, size_t * dec_len)
{
	uint8_t * p_dec_data;
	int raw_exp_length, dec_length;		// expected length
	
	if (en == 0) {
		memcpy(buf, raw, raw_len); 
		*dec_len = raw_len;
		return 0;
	}
	
	// expected raw length for decrypt: 
	//	min( 
	//			(smallest value (bigger than sizeof(payloaddata) & divided by 4))+4,
	//			(bytes)
	//		)

	raw_exp_length = ( (sizeof(struct payloaddata_s)/4) +2)*4;
	if (raw_exp_length != raw_len) {
		fprintf(stderr, "Warning: the packet length is not equal to that we need(got %d, expected %d). Maybe rssitx is not the same version?\n", raw_len, raw_exp_length);
	}
	dec_length = (raw_exp_length > raw_len)? raw_len: raw_exp_length;
	
	p_dec_data = xxtea_decrypt(raw, dec_length, pwd, &dec_len);
	memcpy(buf, p_dec_data, dec_len);
	free(p_dec_data);
	
	return 0;
}

// input: decrypted buffer
// output: td
int fill_buf_to_payload(uint8_t * buf, telemetry_data_t *td)
{
	struct payloaddata_s * payload;
	payload = (struct payloaddata_s *)buf;
	
	td->rx_status->adapter[0].current_signal_dbm = payload->signal;
	td->rx_status->lost_packet_cnt = ntohl(payload->lostpackets);
	td->rx_status_rc->adapter[0].current_signal_dbm = payload->signal_rc;
	td->rx_status_rc->lost_packet_cnt = ntohl(payload->lostpackets_rc);
	td->sysair_status->cpuload = payload->cpuload;
	td->sysair_status->temp = payload->temp;
	td->sysair_status->cpuload_wrt = payload->cpuload_wrt;
	td->sysair_status->temp_wrt = payload->temp_wrt;
	td->sysair_status->skipped_fec_cnt = ntohl(payload->skipped_fec_cnt);
	td->sysair_status->injected_block_cnt = ntohl(payload->injected_block_cnt);
	td->sysair_status->injection_fail_cnt = ntohl(payload->injection_fail_cnt);
	td->sysair_status->injection_time_block = be64toh(payload->injection_time_block);
	td->sysair_status->bitrate_kbit = ntohs(payload->bitrate_kbit);
	td->sysair_status->bitrate_measured_kbit = ntohs(payload->bitrate_measured_kbit);
	td->sysair_status->cts = payload->cts;
	td->sysair_status->undervolt = payload->undervolt;
	
	return 0;
}

wifibroadcast_rx_status_t *status_memory_open(void) 
{ 
	char buf[128];
	int fd;
	
	sprintf(buf, "/wifibroadcast_rx_status_uplink");
	fd = shm_open(buf, O_RDWR, S_IRUSR | S_IWUSR);
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
	wifibroadcast_rx_status_t *tretval = (wifibroadcast_rx_status_t*)retval;
	return tretval;
}

wifibroadcast_rx_status_t_rc *status_memory_open_rc(void) 
{
	char buf[128];
	int fd;
	
	sprintf(buf, "/wifibroadcast_rx_status_rc");
	fd = shm_open(buf, O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) { 
		perror("shm_open"); 
		exit(1); 
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_rc), 
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { 
		perror("mmap"); 
		exit(1); 
	}
	wifibroadcast_rx_status_t_rc *tretval = (wifibroadcast_rx_status_t_rc*)retval;
	return tretval;
}

wifibroadcast_rx_status_t_sysair *status_memory_open_sysair(void) 
{
	char buf[128];
	int fd;
	
	sprintf(buf, "/wifibroadcast_rx_status_sysair");
	fd = shm_open(buf, O_RDWR, S_IRUSR | S_IWUSR);
	if (fd < 0) { 
		perror("shm_open"); 
		exit(1); 
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_sysair), 
						PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) { 
		perror("mmap"); 
		exit(1); 
	}
	wifibroadcast_rx_status_t_sysair *tretval = (wifibroadcast_rx_status_t_sysair*)retval;
	return tretval;
}

void telemetry_init(telemetry_data_t *td) 
{
	td->rx_status = status_memory_open();
	td->rx_status_rc = status_memory_open_rc();
	td->sysair_status = status_memory_open_sysair();
}

int main(int argc, char *argv[]) 
{

	setpriority(PRIO_PROCESS, 0, 10);

	monitor_interface_t iface;
	int i;
	telemetry_data_t td;
	
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
	
	telemetry_init(&td);
	td.rx_status->wifi_adapter_cnt = num_interfaces;
	td.rx_status_rc->wifi_adapter_cnt = num_interfaces;

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
