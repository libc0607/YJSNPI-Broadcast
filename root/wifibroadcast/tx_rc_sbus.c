
// Usage: ./tx_rc_sbus config.ini
/*

[tx_rc_sbus]
mode=0	# 0-send packet to air, 1-send to udp, 2-both
nic=wlan0				// optional, when mode set to 0or2
udp_ip=127.0.0.1		// optional, when mode set to 1or2
udp_port=30302			// optional, when mode set to 1or2
udp_bind_port=30300		// optional, when mode set to 1or2
uart=/dev/ttyUSB0
wifimode=0				// 0-b/g 1-n
rate=6					// Mbit(802.11b/g) / mcs index(802.11n/ac)
ldpc=0					// 802.11n/ac only
stbc=0
retrans=2
encrypt=0
password=1145141919810

*/
//


#include "lib.h"
#include "xxtea.h"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <iniparser.h>
#include <linux/serial.h>
#include <net/if.h>
#include <netinet/ether.h>
#include <netpacket/packet.h>
#include <poll.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stropts.h>
#include <sys/mman.h>
#include <sys/random.h>
#include <sys/resource.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

// Note: redefinition issue -- use setsbus first
//#include <asm/termios.h>
#include <sys/ioctl.h>

int sockfd;
int uartfd;
int udpfd;
int urandom_fd;

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
        180, 191, 0, 0, // frame control field (2 bytes), duration (2 bytes)
        0xff, // 1st byte of IEEE802.11 RA (mac) must be 0xff or something odd (wifi hardware determines broadcast/multicast through odd/even check)
};

// To-do: do not use #define 
#define FRAME_BODY_LENGTH 25
#define FRAME_HEADER_LENGTH 16
#define FRAME_VER 0
struct framedata_s 
{
	// info
	uint8_t ver;					// version
	uint8_t headerlen;				// header length
	uint16_t bodylen;				// body length
	
	uint32_t crc;					// full framedata_s crc

    uint32_t seqnum;				// seq number
	
	uint8_t subseqnum;				// 
	uint8_t zero[3];				// future use
	
	// payload
    uint8_t body[FRAME_BODY_LENGTH];

} __attribute__ ((__packed__));

static const uint32_t crc32_table[] =
{
  0x00000000, 0x04c11db7, 0x09823b6e, 0x0d4326d9, 0x130476dc, 0x17c56b6b, 0x1a864db2, 0x1e475005,
  0x2608edb8, 0x22c9f00f, 0x2f8ad6d6, 0x2b4bcb61, 0x350c9b64, 0x31cd86d3, 0x3c8ea00a, 0x384fbdbd, 
  0x4c11db70, 0x48d0c6c7, 0x4593e01e, 0x4152fda9, 0x5f15adac, 0x5bd4b01b, 0x569796c2, 0x52568b75,
  0x6a1936c8, 0x6ed82b7f, 0x639b0da6, 0x675a1011, 0x791d4014, 0x7ddc5da3, 0x709f7b7a, 0x745e66cd,
  0x9823b6e0, 0x9ce2ab57, 0x91a18d8e, 0x95609039, 0x8b27c03c, 0x8fe6dd8b, 0x82a5fb52, 0x8664e6e5,
  0xbe2b5b58, 0xbaea46ef, 0xb7a96036, 0xb3687d81, 0xad2f2d84, 0xa9ee3033, 0xa4ad16ea, 0xa06c0b5d,
  0xd4326d90, 0xd0f37027, 0xddb056fe, 0xd9714b49, 0xc7361b4c, 0xc3f706fb, 0xceb42022, 0xca753d95,
  0xf23a8028, 0xf6fb9d9f, 0xfbb8bb46, 0xff79a6f1, 0xe13ef6f4, 0xe5ffeb43, 0xe8bccd9a, 0xec7dd02d,
  0x34867077, 0x30476dc0, 0x3d044b19, 0x39c556ae, 0x278206ab, 0x23431b1c, 0x2e003dc5, 0x2ac12072,
  0x128e9dcf, 0x164f8078, 0x1b0ca6a1, 0x1fcdbb16, 0x018aeb13, 0x054bf6a4, 0x0808d07d, 0x0cc9cdca,
  0x7897ab07, 0x7c56b6b0, 0x71159069, 0x75d48dde, 0x6b93dddb, 0x6f52c06c, 0x6211e6b5, 0x66d0fb02,
  0x5e9f46bf, 0x5a5e5b08, 0x571d7dd1, 0x53dc6066, 0x4d9b3063, 0x495a2dd4, 0x44190b0d, 0x40d816ba,
  0xaca5c697, 0xa864db20, 0xa527fdf9, 0xa1e6e04e, 0xbfa1b04b, 0xbb60adfc, 0xb6238b25, 0xb2e29692,
  0x8aad2b2f, 0x8e6c3698, 0x832f1041, 0x87ee0df6, 0x99a95df3, 0x9d684044, 0x902b669d, 0x94ea7b2a,
  0xe0b41de7, 0xe4750050, 0xe9362689, 0xedf73b3e, 0xf3b06b3b, 0xf771768c, 0xfa325055, 0xfef34de2,
  0xc6bcf05f, 0xc27dede8, 0xcf3ecb31, 0xcbffd686, 0xd5b88683, 0xd1799b34, 0xdc3abded, 0xd8fba05a,
  0x690ce0ee, 0x6dcdfd59, 0x608edb80, 0x644fc637, 0x7a089632, 0x7ec98b85, 0x738aad5c, 0x774bb0eb,
  0x4f040d56, 0x4bc510e1, 0x46863638, 0x42472b8f, 0x5c007b8a, 0x58c1663d, 0x558240e4, 0x51435d53,
  0x251d3b9e, 0x21dc2629, 0x2c9f00f0, 0x285e1d47, 0x36194d42, 0x32d850f5, 0x3f9b762c, 0x3b5a6b9b,
  0x0315d626, 0x07d4cb91, 0x0a97ed48, 0x0e56f0ff, 0x1011a0fa, 0x14d0bd4d, 0x19939b94, 0x1d528623,
  0xf12f560e, 0xf5ee4bb9, 0xf8ad6d60, 0xfc6c70d7, 0xe22b20d2, 0xe6ea3d65, 0xeba91bbc, 0xef68060b,
  0xd727bbb6, 0xd3e6a601, 0xdea580d8, 0xda649d6f, 0xc423cd6a, 0xc0e2d0dd, 0xcda1f604, 0xc960ebb3,
  0xbd3e8d7e, 0xb9ff90c9, 0xb4bcb610, 0xb07daba7, 0xae3afba2, 0xaafbe615, 0xa7b8c0cc, 0xa379dd7b,
  0x9b3660c6, 0x9ff77d71, 0x92b45ba8, 0x9675461f, 0x8832161a, 0x8cf30bad, 0x81b02d74, 0x857130c3,
  0x5d8a9099, 0x594b8d2e, 0x5408abf7, 0x50c9b640, 0x4e8ee645, 0x4a4ffbf2, 0x470cdd2b, 0x43cdc09c,
  0x7b827d21, 0x7f436096, 0x7200464f, 0x76c15bf8, 0x68860bfd, 0x6c47164a, 0x61043093, 0x65c52d24,
  0x119b4be9, 0x155a565e, 0x18197087, 0x1cd86d30, 0x029f3d35, 0x065e2082, 0x0b1d065b, 0x0fdc1bec,
  0x3793a651, 0x3352bbe6, 0x3e119d3f, 0x3ad08088, 0x2497d08d, 0x2056cd3a, 0x2d15ebe3, 0x29d4f654,
  0xc5a92679, 0xc1683bce, 0xcc2b1d17, 0xc8ea00a0, 0xd6ad50a5, 0xd26c4d12, 0xdf2f6bcb, 0xdbee767c,
  0xe3a1cbc1, 0xe760d676, 0xea23f0af, 0xeee2ed18, 0xf0a5bd1d, 0xf464a0aa, 0xf9278673, 0xfde69bc4,
  0x89b8fd09, 0x8d79e0be, 0x803ac667, 0x84fbdbd0, 0x9abc8bd5, 0x9e7d9662, 0x933eb0bb, 0x97ffad0c,
  0xafb010b1, 0xab710d06, 0xa6322bdf, 0xa2f33668, 0xbcb4666d, 0xb8757bda, 0xb5365d03, 0xb1f740b4
};

uint32_t xcrc32 (const unsigned char *buf, int len, unsigned int init)
{
	unsigned int crc = init;
	while (len--) {
		crc = (crc << 8) ^ crc32_table[((crc >> 24) ^ *buf) & 255];
		buf++;
	}
	return crc;
}

void usage(void)
{
    printf(
        "tx_rc_sbus by libc0607. GPL2\n"
        "\n"
        "Usage: tx_rc_sbus config.ini \n"
        "\n"
        "\n");
    exit(1);
}

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

wifibroadcast_rx_status_t *telemetry_wbc_status_memory_open (void) 
{
    int fd = 0;

    while(1) {
        fd = shm_open("/wifibroadcast_rx_status_0", O_RDONLY, S_IRUSR | S_IWUSR);
	    if(fd < 0) {
			fprintf(stderr, "Could not open wifibroadcast rx status - will try again ...\n");
	    } else {
			break;
	    }
	    usleep(500000);
    }

	void *retval = mmap(NULL, sizeof(wifibroadcast_rx_status_t), PROT_READ, MAP_SHARED, fd, 0);
	if (retval == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	return (wifibroadcast_rx_status_t*)retval;
}

void telemetry_init(telemetry_data_t *td) 
{
    td->rx_status = telemetry_wbc_status_memory_open();
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

int uart_sbus_init(int fd) 
{
	// Note: CH340 only works with Linux kernel 4.10+
	
	// redefinition issue
#ifndef PARODD
	fprintf(stderr, "redefinition issue -- please call setsbus first to set baudrate\n");
	return 0;
#else
	int rate = 100000;
	struct termios2 tio;
	ioctl(fd, TCGETS2, &tio);
	// 8bit
	tio.c_cflag &= ~CSIZE;   
	tio.c_cflag |= CS8;
	// even
	tio.c_cflag &= ~(PARODD | CMSPAR);
	tio.c_cflag |= PARENB;
	// 2 stop bits
	tio.c_cflag |= CSTOPB;	 
	// baud rate
	tio.c_ispeed = rate;
	tio.c_ospeed = rate;
	// other
	tio.c_iflag |= (INPCK|IGNBRK|IGNCR|ISTRIP);
	tio.c_cflag &= ~CBAUD;
	tio.c_cflag |= (BOTHER|CREAD|CLOCAL);
	// apply
	return ioctl(fd, TCSETS2, &tio);
#endif
}

// return: rtheader_length
int packet_rtheader_init (int offset, uint8_t *buf, dictionary *ini) 
{
	int param_bitrate = iniparser_getint(ini, "tx_rc_sbus:rate", 0);
	int param_wifimode = iniparser_getint(ini, "tx_rc_sbus:wifimode", 0);
	int param_ldpc = (param_wifimode == 1)? iniparser_getint(ini, "tx_rc_sbus:ldpc", 0): 0;
	int param_stbc = (param_wifimode == 1)? iniparser_getint(ini, "tx_rc_sbus:stbc", 0): 0;
	uint8_t * p_rtheader = (param_wifimode == 1)? u8aRadiotapHeader80211n: u8aRadiotapHeader;
	size_t rtheader_length = (param_wifimode == 1)? sizeof(u8aRadiotapHeader80211n): sizeof(u8aRadiotapHeader);

	// set args
	if (param_wifimode == 0) {	// 802.11g
		u8aRadiotapHeader[8] = bitrate_to_rtap8(param_bitrate);
	} else if (param_wifimode == 1) {					// 802.11n
		if (param_ldpc == 0) {
			u8aRadiotapHeader80211n[10] &= (~0x10);
			u8aRadiotapHeader80211n[11] &= (~0x10);
			u8aRadiotapHeader80211n[12] = (uint8_t)param_bitrate;
		} else {
			u8aRadiotapHeader80211n[10] |= 0x10;
			u8aRadiotapHeader80211n[11] |= 0x10;
			u8aRadiotapHeader80211n[12] = (uint8_t)param_bitrate;
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
			// clear all bits
			u8aRadiotapHeader80211n[10] &= (~0x20);
			u8aRadiotapHeader80211n[11] &= (~0x60);
		break;
		}
		
	}
	// copy radiotap header
	memcpy(buf+offset, p_rtheader, rtheader_length);
	return rtheader_length;
}

// return: ieeeheader_length
int packet_ieeeheader_init (int offset, uint8_t * buf, dictionary *ini)
{
	// default rts frame
	memcpy(buf+offset, u8aIeeeHeader_rts, sizeof(u8aIeeeHeader_rts));
	return sizeof(u8aIeeeHeader_rts);
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

int main (int argc, char *argv[]) 
{
	struct framedata_s framedata;
	uint8_t framedata_body_length = sizeof(framedata);
	uint8_t buf[128];
	bzero(buf, sizeof(buf));
	
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
	int param_mode = iniparser_getint(ini, "tx_rc_sbus:mode", 0);
	int param_retrans = iniparser_getint(ini, "tx_rc_sbus:retrans", 0);
	int param_debug = iniparser_getint(ini, "tx_rc_sbus:debug", 0); 
	int param_encrypt = iniparser_getint(ini, "tx_rc_sbus:encrypt", 0);
	char * param_password = (param_encrypt == 1)? (char *)iniparser_getstring(ini, "tx_rc_sbus:password", NULL): NULL;
	// init wifi raw socket
	if (param_mode == 0 || param_mode == 2) {
		char * nic_name = (char *)iniparser_getstring(ini, "tx_rc_sbus:nic", NULL);
		sockfd = open_sock(nic_name);
		usleep(20000); // wait a bit 
	}
	
	// init udp socket & bind
	int16_t port;
	struct sockaddr_in send_addr;
	struct sockaddr_in source_addr;	
	int slen = sizeof(send_addr);
	if (param_mode == 1 || param_mode == 2) {
		port = atoi(iniparser_getstring(ini, "tx_rc_sbus:udp_port", NULL));
		bzero(&send_addr, sizeof(send_addr));
		send_addr.sin_family = AF_INET;
		send_addr.sin_port = htons(port);
		send_addr.sin_addr.s_addr = inet_addr(iniparser_getstring(ini, "tx_rc_sbus:udp_ip", NULL));
		bzero(&source_addr, sizeof(source_addr));
		source_addr.sin_family = AF_INET;
		source_addr.sin_port = htons(atoi(iniparser_getstring(ini, "tx_rc_sbus:udp_bind_port", NULL)));
		source_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		if ((udpfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) 
			printf("ERROR: Could not create UDP socket!");
		if (-1 == bind(udpfd, (struct sockaddr*)&source_addr, sizeof(source_addr))) {
			fprintf(stderr, "Bind UDP port failed.\n");
			exit(0);
		}
	}
	
	// init uart to b100000,8E2
	uartfd = open( (char *)iniparser_getstring(ini, "tx_rc_sbus:uart", NULL),
					O_RDWR|O_NOCTTY|O_NDELAY);	// not using |O_NONBLOCK now
	if (uartfd < 0) {
		fprintf(stderr, "open uart failed!\n"); 
	}
	int r = uart_sbus_init(uartfd);
	if (r != 0) {
        perror("uart ioctl");
    }
	
	// init RSSI shared memory
	telemetry_data_t td;
	telemetry_init(&td);
	
	// init radiotap header to buf
	int rtheader_length = packet_rtheader_init(0, buf, ini);
	
	// init ieee header
	int ieeeheader_length = packet_ieeeheader_init(rtheader_length, buf, ini);
	
	// set length
	framedata.length = htonl(framedata_body_length);
	
	// Main loop
	uint32_t seqno = 0;
	uint8_t subseqno = 0;
	int k = 0;
	int uart_read_len = 0;
	int full_header_length = rtheader_length + ieeeheader_length;
	int encrypted_data_length = 0;
	size_t rand_size = -1;
	uint8_t uart_read_buf[64] = {0};
	
	while (1) {
		// 1. Get data from UART
		//uart_read_len = read(uartfd, buf+full_header_length+FRAME_HEADER_LENGTH, FRAME_BODY_LENGTH);
		bzero(uart_read_buf, sizeof(uart_read_buf));
		uart_read_len = read(uartfd, uart_read_buf, FRAME_BODY_LENGTH);
		if (param_debug) {
			fprintf(stderr, "uart read() got %d bytes\n", uart_read_len);
			dump_memory(buf+full_header_length+FRAME_HEADER_LENGTH, uart_read_len, "uart read");
		}
		if (uart_read_len == 0)
			continue;
		if (uart_read_len < 0) {
			fprintf(stderr, "uart read() got an error\n");
			continue;
		}
		subseqno = 0;
		// 2. send packet
		for (k = 0; k < param_retrans; k++) {
			
			// 2.0 clean buffer
			bzero(buf+full_header_length+FRAME_HEADER_LENGTH, sizeof(framedata)-FRAME_HEADER_LENGTH);
			
			// 2.1 fill frame (seqno, data, info(wip...) )
			framedata.ver = FRAME_VER;
			framedata.headerlen = FRAME_HEADER_LENGTH;
			framedata.bodylen = htons(FRAME_BODY_LENGTH);
			framedata.seqnum = htonl(seqno);
			framedata.subseqnum = subseqno;
			memcpy(buf+full_header_length+FRAME_HEADER_LENGTH, uart_read_buf, uart_read_len);
			
			// 2.2 pass

			// 2.3 calculate crc32: fill 0xffffffff first
			framedata.crc = 0xffffffff;
			framedata.crc = htonl(xcrc32(buf+full_header_length, sizeof(framedata), 0xffffffff));
			
			// 2.4 encrypt full packet
			encrypted_data_length = encrypt_payload(buf+full_header_length, 
									sizeof(framedata), param_encrypt, param_password);
			
			// 2.5 send packet 
			if (param_mode == 0 || param_mode == 2) {	
				// 2.5.1 wi-fi
				if ( write(sockfd, buf, full_header_length + encrypted_data_length) < 0 ) {
					fprintf(stderr, "ERROR: wireless injection failed, seqno=%d\n", seqno);	
				}
			} else if (param_mode == 1 || param_mode == 2) {
				// 2.5.2 udp
				if (sendto(udpfd, buf +full_header_length, encrypted_data_length, 0, 
									(struct sockaddr*)&send_addr, slen) == -1) {
					fprintf(stderr, "ERROR: UDP Could not send data! seqno=%d\n", seqno);
				} 
			}
			usleep(2000);
			subseqno++;
		}
		seqno++;
	}
	
	close(sockfd);
	if (param_mode == 0 || param_mode == 2)
		close(uartfd);
	if (param_mode == 1 || param_mode == 2)
		close(udpfd);
	close(urandom_fd);
	iniparser_freedict(ini);
	return EXIT_SUCCESS;
}

