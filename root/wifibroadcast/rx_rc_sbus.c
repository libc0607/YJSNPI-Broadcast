// rx_rc_sbus
// receive rc packets from wi-fi monitor (todo: or udp), output to uart(sbus)
// note: only support one wi-fi interface 
// 
// Usage: ./rx_rc_sbus config.ini
// config.ini example:
/*

[rx_rc_sbus]
mode=0	# 0-air, #1-udp, 2-both
nic=wlan0				// optional, when mode set to 0or2
#udp_bind_port=30300		// optional, when mode set to 1or2
uart=/dev/ttyUSB0

*/
//


#include "lib.h"
#include "mavlink/common/mavlink.h"
#include "radiotap.h"
#include "wifibroadcast.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>    
#include <getopt.h>
#include <iniparser.h>
#include <linux/serial.h>
#include <net/if.h>
//#include <netinet/ether.h>
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
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define PROGRAM_NAME rx_rc_sbus
#define PCAP_FILTER_CHAR "ether[0x00:4] == 0xb4bf0000"
// this is where we store a summary of the information from the radiotap header
typedef struct  {
	int m_nChannel;
	int m_nChannelFlags;
	int m_nRate;
	int m_nAntenna;
	int m_nRadiotapFlags;
} __attribute__((packed)) PENUMBRA_RADIOTAP_DATA;


int uartfd;
int udpfd;

int param_baudrate;
int param_mode;
int param_debug;
uint32_t seqno, last_seqno, crc32_val;
uint8_t frame_length;
uint8_t sbus_output_buf[25];

wifibroadcast_rx_status_t_rc *rx_status_rc = NULL;
uint32_t sumdcrc = 0;

static const unsigned int crc32_table[] =
{
  0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9,
  0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
  0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61,
  0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd,
  0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9,
  0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
  0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011,
  0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
  0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039,
  0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
  0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81,
  0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
  0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49,
  0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
  0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1,
  0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
  0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae,
  0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
  0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16,
  0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
  0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde,
  0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
  0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066,
  0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
  0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e,
  0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
  0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6,
  0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
  0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e,
  0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
  0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686,
  0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
  0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637,
  0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
  0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f,
  0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
  0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47,
  0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
  0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff,
  0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
  0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7,
  0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
  0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f,
  0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
  0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7,
  0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
  0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f,
  0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
  0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640,
  0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
  0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8,
  0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
  0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30,
  0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
  0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088,
  0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
  0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0,
  0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
  0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18,
  0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
  0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0,
  0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
  0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668,
  0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

void usage(void)
{
    printf(
        "rx_rc_sbus by libc0607. GPL2\n"
        "\n"
        "Usage:rx_rc_sbus config.ini \n"
        "\n"
        "\n");
    exit(1);
}

typedef struct {
	pcap_t *ppcap;
	int selectable_fd;
	int n80211HeaderLength;
} monitor_interface_t;

unsigned int xcrc32 (const unsigned char *buf, int len, unsigned int init)
{
	unsigned int crc = init;
	while (len--) {
		crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ *buf) & 255];
		buf++;
	}
	return crc;
}

void dump_memory(void* p, int length, char * tag)
{
	int i, j;
	unsigned char *addr = (unsigned char *)p;

	fprintf(stderr, "\n");
	fprintf(stderr, "===== Memory dump at %s: 0x%x, length=%d =====", tag, p, length);
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

void open_and_configure_interface(const char *name, monitor_interface_t *interface) 
{
	struct bpf_program bpfprogram;
	char pcap_filter_char[64];
	char szErrbuf[PCAP_ERRBUF_SIZE];

	szErrbuf[0] = '\0';
	interface->ppcap = pcap_open_live(name, 400, 0, -1, szErrbuf);
	if (interface->ppcap == NULL) {
		fprintf(stderr, "Unable to open %s: %s\n", name, szErrbuf);
		exit(1);
	}
	
	if(pcap_setnonblock(interface->ppcap, 1, szErrbuf) < 0) {
		fprintf(stderr, "Error setting %s to nonblocking mode: %s\n", name, szErrbuf);
	}

	int nLinkEncap = pcap_datalink(interface->ppcap);

	if (nLinkEncap == DLT_IEEE802_11_RADIO) {
		sprintf(pcap_filter_char, PCAP_FILTER_CHAR);
	} else {
		fprintf(stderr, "ERROR: unknown encapsulation on %s! check if monitor mode is supported and enabled\n", name);
		exit(1);
	}

	if (pcap_compile(interface->ppcap, &bpfprogram, pcap_filter_char, 1, 0) == -1) {
		puts(pcap_filter_char);
		puts(pcap_geterr(interface->ppcap));
		exit(1);
	} else {
		if (pcap_setfilter(interface->ppcap, &bpfprogram) == -1) {
			fprintf(stderr, "%s\n", pcap_filter_char);
			fprintf(stderr, "%s\n", pcap_geterr(interface->ppcap));
		}
		pcap_freecode(&bpfprogram);
	}
	interface->selectable_fd = pcap_get_selectable_fd(interface->ppcap);
}

// 
int32_t process_packet(monitor_interface_t *interface, int serialport) 
{
    PENUMBRA_RADIOTAP_DATA prd;
    int bytes, dbm_tmp, n, retval, u16HeaderLen;
    struct ieee80211_radiotap_iterator rti;
    struct pcap_pkthdr * ppcapPacketHeader = NULL;
	uint8_t payloadBuffer[300];
    uint8_t *pu8Payload = payloadBuffer;
    
	
    // receive
    retval = pcap_next_ex(interface->ppcap, &ppcapPacketHeader, (const u_char**)&pu8Payload);
    if (retval < 0) {
		if (strcmp("The interface went down", pcap_geterr(interface->ppcap)) == 0) {
			fprintf(stderr, "rx: The interface went down\n");
			exit(9);
		} else {
			fprintf(stderr, "rx: %s\n", pcap_geterr(interface->ppcap));
			exit(2);
		}
    }
    if (retval != 1)
		return;

    // fetch radiotap header length from radiotap header 
    u16HeaderLen = (pu8Payload[2] + (pu8Payload[3] << 8));

    pu8Payload += u16HeaderLen;
	interface->n80211HeaderLength = 0x04;
    pu8Payload -= u16HeaderLen;

    if (ppcapPacketHeader->len < (u16HeaderLen + interface->n80211HeaderLength)) 
		exit(1);

    bytes = ppcapPacketHeader->len - (u16HeaderLen + interface->n80211HeaderLength);
    if (bytes < 0) 
		exit(1);

    if (ieee80211_radiotap_iterator_init(&rti, (struct ieee80211_radiotap_header *)pu8Payload, ppcapPacketHeader->len) < 0) 
		exit(1);
	
    while ((n = ieee80211_radiotap_iterator_next(&rti)) == 0) {
		switch (rti.this_arg_index) {
		case IEEE80211_RADIOTAP_FLAGS:
			prd.m_nRadiotapFlags = *rti.this_arg;
			break;
		case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
			dbm_tmp = (int8_t)(*rti.this_arg);
			break;
		}
    }
	// write signal dbm to shmem
	rx_status_rc->adapter[0].current_signal_dbm = dbm_tmp;
	rx_status_rc->adapter[0].received_packet_cnt++;
	rx_status_rc->last_update = time(NULL);
	pu8Payload += u16HeaderLen + interface->n80211HeaderLength;
	
	// Process packet

	crc32_val = *((uint32_t *)pu8Payload);
	pu8Payload += 4;
	// "framedata.length"
	frame_length = *((uint8_t *)pu8Payload);
	pu8Payload += 1;
	// verify crc32

	uint32_t crc32_calculated;
	uint32_t crc32_startvalue = 0xffffffff;
	memcpy(pu8Payload, &crc_magic, sizeof(crc_magic));
	crc32_calculated = htonl( xcrc32( pu8Payload-5, (int)frame_length, crc32_startvalue) );
	fprintf(stderr, "Got packet: length=%d, crc=0x%x(0x%x)", frame_length, crc32_val, crc32_calculated);
	if (crc32_calculated != crc32_val) {
		fprintf(stderr, "process_packet(): got a packet with crc32 error.\n");
		return;
	}
	// "framedata.seqno"
	// e.g. when last_seqno=10, seqno=14 => 11~13 were lost => (14-10+1) packets lost
	seqno = ntohl(*((uint32_t *)pu8Payload));
	if (last_seqno + 1 == seqno) {
		rx_status_rc->adapter[0].signal_good = 1;
	} else {
		rx_status_rc->lost_packet_cnt = rx_status_rc->lost_packet_cnt + (seqno - last_seqno + 1);
		rx_status_rc->adapter[0].signal_good = 0;
	}
	last_seqno = seqno;
	pu8Payload += 4;
	// "framedata.info"
	// pass
	pu8Payload += 4;
	// "framedata.sbus_data"
	memcpy(sbus_output_buf, pu8Payload, sizeof(sbus_output_buf)); 
	
	// Write data to uart	
	write(serialport, sbus_output_buf, sizeof(sbus_output_buf));

	return;
}

void status_memory_init_rc(wifibroadcast_rx_status_t_rc *s) 
{
	s->received_block_cnt = 0;
	s->damaged_block_cnt = 0;
	s->received_packet_cnt = 0;	//used
	s->lost_packet_cnt = 0;		//used
	s->tx_restart_cnt = 0;
	s->wifi_adapter_cnt = 0;	// 1
	s->kbitrate = 0;

	int i;
	for(i=0; i<6; ++i) {
		s->adapter[i].received_packet_cnt = 0;		//used
		s->adapter[i].wrong_crc_cnt = 0;			//used
		s->adapter[i].current_signal_dbm = -126;	//used
		s->adapter[i].signal_good = 0;				//used
	}	
}

wifibroadcast_rx_status_t_rc *status_memory_open_rc(void) 
{
	char buf[128];
	int fd;
	
	sprintf(buf, "/wifibroadcast_rx_status_rc");
	fd = shm_open(buf, O_RDWR, S_IRUSR | S_IWUSR);
	if(fd < 0) {
		perror("shm_open"); 
		exit(1);
	}
	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t_rc), PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) {
		perror("mmap"); 
		exit(1);
	}
	wifibroadcast_rx_status_t_rc *tretval = (wifibroadcast_rx_status_t_rc*)retval;
	status_memory_init_rc(tretval);
	return tretval;
}

int check_nic_type (char *nic_name) 
{
	char path[45], line[100];
	FILE* procfile;
	int type;
	
	snprintf(path, 45, "/sys/class/net/%s/device/uevent", nic_name);
	procfile = fopen(path, "r");
	if (!procfile) {
		fprintf(stderr,"ERROR: opening %s failed - device not exist?\n", path); 
		return -1;
	}
	fgets(line, 100, procfile); 
	if(strncmp(line, "DRIVER=ath9k", 12) == 0) { 
		fprintf(stderr, "PROGRAM_NAME: Driver: Atheros ath9k\n");
		type = 0;
	} else { 
		fprintf(stderr, "PROGRAM_NAME: Driver: Ralink (or other)\n");
		type = 1;
	}
	
	fclose(procfile);
	return type;
}
	
int main(int argc, char *argv[]) 
{
	monitor_interface_t iface;
	
	// Get ini from file
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	if (argc != 2) {
		usage();
	}

	// init & check nic
	char * nic_name = iniparser_getstring(ini, "PROGRAM_NAME:nic", NULL);
	int nic_type = 0;
	if (-1 == (nic_type = check_nic_type(nic_name))) {
		exit(1);
	}
	open_and_configure_interface(nic_name, &iface);
	
	// Open shared memory
	rx_status_rc = status_memory_open_rc();
	rx_status_rc->wifi_adapter_cnt = 1;	

	// init uart
	char * uart_name = iniparser_getstring(ini, "PROGRAM_NAME:uart", NULL);
	uartfd = open(uart_name, O_RDWR|O_NOCTTY|O_NDELAY);			
	if (uartfd < 0) {
		fprintf(stderr, "PROGRAM_NAME ERROR: Unable to open UART. Ensure it is not in use by another application.\n");
	}
	
	struct timeval to;
	to.tv_sec = 0;
	to.tv_usec = 1e5; 
	
	for(;;) {
	    fd_set readset;
	    FD_ZERO(&readset);
		FD_SET(iface.selectable_fd, &readset);
			
	    int n = select(iface.selectable_fd+1, &readset, NULL, NULL, &to); 
	    if (n == 0) {
			continue;
		}
		if (FD_ISSET(iface.selectable_fd, &readset)) {
			process_packet(&iface, uartfd);
		}
	}
	close(uartfd);
	close(iface.selectable_fd);
	iniparser_freedict(ini);
	return (0);
}
