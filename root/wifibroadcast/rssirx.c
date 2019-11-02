// rssirx by Rodizio. Based on wifibroadcast rx by Befinitiv. Licensed under GPL2
// Mod by Github @libc0607
//
// Usage:
//	rssirx ./config.ini
//
// config.ini:
// [rssirx]
// nic=wlan0
// mode=0		#0-wifi, 1-udp, 2-both
// udp_listen_port=20393
// encrypt=0
// password=1919810
// debug=0		# won't show in usage() but it should work

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
	// 0
	int8_t signal;
	int8_t signal_rc;
	uint8_t cpuload;
	uint8_t temp;	
	uint8_t cts;
	uint8_t undervolt;
	uint8_t cpuload_wrt;
	uint8_t temp_wrt;
	// 8
	uint16_t bitrate_kbit;
	uint16_t bitrate_measured_kbit;
	uint32_t lostpackets;
	// 16
	uint32_t lostpackets_rc;
	uint32_t injected_block_cnt;
	uint32_t skipped_fec_cnt;
	uint32_t injection_fail_cnt;
	// 32
	uint64_t injection_time_block;
	uint64_t seqno;
	// 48
}  __attribute__ ((__packed__));

void usage(void) {
	printf("\n"
	    PROGRAM_NAME" by Rodizio. Based on wifibroadcast rx by Befinitiv. Licensed under GPL2\n"
		"Mod by Github @libc0607\n\n"
	    "Usage: "PROGRAM_NAME" <config.file>\n\n"
	    "config example:\n"
		"["PROGRAM_NAME"]\n"
		"nic=wlan0\n"
		"mode=0					# 0-wifi, 1-udp, 2-both\n"
		"udp_bind_port=20393\n"
		"encrypt=0\n"
		"password=1919810\n\n"
	);
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

// input: monitor_interface_t *interface
// output: uint8_t * buf, size_t *len
// receive packet (not decrypted) and save to buf 
int receive_packet(monitor_interface_t *interface, uint8_t * buf, size_t *len) 
{
	struct pcap_pkthdr * ppcapPacketHeader = NULL;
	struct ieee80211_radiotap_iterator rti;
	PENUMBRA_RADIOTAP_DATA prd;
	u8 payloadBuffer[512];
	u8 *pu8Payload = payloadBuffer;
	int bytes, n, retval, u16HeaderLen;
	
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
		
	bzero(&prd, sizeof(PENUMBRA_RADIOTAP_DATA));
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

// input: udpfd, port, recv_len
// output: uint8_t * buf, size_t *len
// return: =recvfrom()
// receive packet (not decrypted) and save to buf 
int receive_packet_udp(int udpfd, struct sockaddr * addr, int recv_len, uint8_t * buf, size_t *len)
{
	int ret, slen = sizeof(addr);

	ret = recvfrom(udpfd, buf, recv_len, 0, addr, (socklen_t *)&slen);
	
	return ret;
}

// input: raw_buffer, raw_length, encrypt_enable, 
// output: decrypted_buffer, decryped_length
int decrypt_payload(uint8_t * raw, size_t raw_len, int en, char * pwd, 
					uint8_t * buf, size_t * dec_len)
{
	uint8_t * p_dec_data;
	int raw_exp_length, dec_length;		// expected length
	
	if (en == 0 || pwd == NULL) {
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
		fprintf(stderr, "Warning: the packet length is not equal to that we need(got %d, expected %d). Maybe rssitx is not the same version?\n", (int)raw_len, (int)raw_exp_length);
	}
	dec_length = (raw_exp_length > raw_len)? raw_len: raw_exp_length;
	
	p_dec_data = xxtea_decrypt(raw, dec_length, pwd, dec_len);
	memcpy(buf, p_dec_data, *dec_len);
	free(p_dec_data);
	
	return 0;
}

// input: decrypted buffer, seqno (current)
// output: seqno(new), td
int fill_buf_to_payload(uint8_t * buf, uint64_t * seqno, telemetry_data_t *td)
{
	struct payloaddata_s * payload;
	uint64_t seqno_get;
	
	payload = (struct payloaddata_s *)buf;
	seqno_get = be64toh(payload->seqno);
	
	if (seqno_get < *seqno)
		return 0;
	*seqno = seqno_get;
	
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

// input: ini
// output: addr
int open_udp_binded_sock_by_conf(dictionary *ini, struct sockaddr_in * addr) 
{
	int udpfd;
	
	bzero(addr, sizeof(struct sockaddr_in));
	addr->sin_family = AF_INET;
	addr->sin_port = htons(atoi(iniparser_getstring(ini, PROGRAM_NAME":udp_bind_port", NULL)));
	addr->sin_addr.s_addr = htonl(INADDR_ANY);
	
	if ((udpfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) 
		printf("ERROR: Could not create UDP socket!");
	if (-1 == bind(udpfd, (struct sockaddr*)addr, sizeof(addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		exit(0);
	}
	
	return udpfd;
}

int main(int argc, char *argv[]) 
{
	int udpfd = 0, param_enc, param_mode, param_dbg;
	char *param_pwd;
	char *param_nic;
	struct timeval to;
	uint8_t raw[256], buf[256];
	size_t raw_len, dec_len;
	fd_set readset;
	telemetry_data_t td;
	monitor_interface_t iface;
	struct sockaddr_in addr;
	uint64_t seqno;
	
	setpriority(PRIO_PROCESS, 0, 10);
	
	bzero(raw, sizeof(raw));
	bzero(buf, sizeof(buf));
	bzero(&td, sizeof(telemetry_data_t));
	bzero(&iface, sizeof(monitor_interface_t));

	if (argc != 2) {
		usage();
	}
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	
	param_mode = iniparser_getint(ini, PROGRAM_NAME":mode", 0);
	if (param_mode == 0 || param_mode == 2) {
		param_nic = (char *)iniparser_getstring(ini, PROGRAM_NAME":nic", NULL);
		open_and_configure_interface(param_nic, &iface);
		usleep(10000); // wait a bit 
	}
	if (param_mode == 1 || param_mode == 2) {
		udpfd = open_udp_binded_sock_by_conf(ini, &addr);
	}
	
	param_enc = iniparser_getint(ini, PROGRAM_NAME":encrypt", 0);
	param_pwd = (param_enc == 1)? (char *)iniparser_getstring(ini, PROGRAM_NAME":password", NULL): NULL;
	param_dbg = iniparser_getint(ini, PROGRAM_NAME":debug", 0);
	
	telemetry_init(&td);
	td.rx_status->wifi_adapter_cnt = 1;
	td.rx_status_rc->wifi_adapter_cnt = 1;
	seqno = 0;
	
	for (;;) {
		to.tv_sec = 0;
		to.tv_usec = 1e5;
		FD_ZERO(&readset);
		FD_SET(iface.selectable_fd, &readset);
		FD_SET(udpfd, &readset);
		
		select(iface.selectable_fd+udpfd+1, &readset, NULL, NULL, &to);
		if (FD_ISSET(iface.selectable_fd, &readset)) {
			receive_packet(&iface, raw, &raw_len);
			decrypt_payload(raw, raw_len, param_enc, param_pwd, buf, &dec_len);
			fill_buf_to_payload(buf, &seqno, &td);
			fprintf(stderr, "!");
			if (param_dbg) {
				fprintf(stderr, "seqno = %ld \n", seqno);
				dump_memory(raw, raw_len, "Wi-Fi recv raw - memory dump");
				dump_memory(buf, dec_len, "Wi-Fi recv decrypted - memory dump");
			}
		}
		if (FD_ISSET(udpfd, &readset)) {
			if (0 >= receive_packet_udp(udpfd, (struct sockaddr *)&addr, sizeof(raw), raw, &raw_len))
				continue;
			decrypt_payload(raw, raw_len, param_enc, param_pwd, buf, &dec_len);
			fill_buf_to_payload(buf, &seqno, &td);
			fprintf(stderr, "?");
			if (param_dbg) {
				fprintf(stderr, "seqno = %ld \n", seqno);
				dump_memory(raw, raw_len, "UDP recv raw - memory dump");
				dump_memory(buf, dec_len, "UDP recv decrypted - memory dump");
			}
		}
	}
	
	close(udpfd);
	close(iface.selectable_fd);
	iniparser_freedict(ini);
	return 0;
}
