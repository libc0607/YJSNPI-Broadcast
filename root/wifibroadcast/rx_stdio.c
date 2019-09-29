/*
stdout <- wi-fi monitor
raw data; no fec & retransmission
just a low-level packet receiver
*/

#include "lib.h"
#include "radiotap.h"
#include <iniparser.h>
#include <time.h>
#include <sys/resource.h>

#define PROGRAM_NAME rx_stdio
#define PCAP_FILTER_STRING "ether[0x00:2] == 0xb401 && ether[0x04:1] == 0xff"
#define PACKET_BUF_SIZE_BYTES 4096		// i didn't check if packet is oversized.. so keep it big 

typedef struct  {
	int m_nChannel;
	int m_nChannelFlags;
	int m_nRate;
	int m_nAntenna;
	int m_nRadiotapFlags;
	int m_nAntSignal;
} __attribute__((packed)) PENUMBRA_RADIOTAP_DATA;

void usage() 
{
	printf(
	    "PROGRAM_NAME by Github @libc0607\n"
	    "Usage: PROGRAM_NAME <config.file>\n\n"
	    "config example:\n"
		"[PROGRAM_NAME]\n"
		"nic=wlan0\n"
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
		sprintf(szProgram, PCAP_FILTER_STRING); // match on frametype, 1st byte of mac (ff) and portnumber (255 = 127 for rssi)
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

// input: 
//		monitor_interface_t *interface
//		uint8_t *buf
// output: 
//		PENUMBRA_RADIOTAP_DATA *prd: contains wireless info
//		uint8_t **dat, int *len: data pointer & length
uint8_t process_packet(monitor_interface_t *interface, uint8_t *buf, PENUMBRA_RADIOTAP_DATA *prd, uint8_t **dat, int *len) 
{
	struct pcap_pkthdr * ppcapPacketHeader = NULL;
	struct ieee80211_radiotap_iterator rti;
	uint8_t *pu8Payload = buf;
	int bytes, n, retval, u16HeaderLen;

	bzero(prd, sizeof(PENUMBRA_RADIOTAP_DATA));
	
	retval = pcap_next_ex(interface->ppcap, &ppcapPacketHeader,(const u_char**)&pu8Payload);

	if (retval < 0) {
		fprintf(stderr, "rx: %s\n", pcap_geterr(interface->ppcap));
		exit(2);
	} else if (retval != 1) {
		return 0;
	}
		
	// fetch radiotap header length from radiotap header 
	u16HeaderLen = (pu8Payload[2] + (pu8Payload[3] << 8));
	if (ppcapPacketHeader->len < (u16HeaderLen + interface->n80211HeaderLength)) {
		exit(1);
	}

	bytes = ppcapPacketHeader->len - (u16HeaderLen + interface->n80211HeaderLength);
	if (bytes < 0) {
		return 0;
	}
	

	if (ieee80211_radiotap_iterator_init(&rti, (struct ieee80211_radiotap_header *)pu8Payload, 
										ppcapPacketHeader->len) < 0) {
		exit(1);									
	}
		
	while ((n = ieee80211_radiotap_iterator_next(&rti)) == 0) {
		switch (rti.this_arg_index) {
		case IEEE80211_RADIOTAP_FLAGS:
			prd->m_nRadiotapFlags = *rti.this_arg;
			break;
		case IEEE80211_RADIOTAP_RATE:
			prd->m_nRate = (*rti.this_arg);
			break;
		case IEEE80211_RADIOTAP_CHANNEL:
			prd->m_nChannel = le16_to_cpu(*((u16 *)rti.this_arg));
			prd->m_nChannelFlags = le16_to_cpu(*((u16 *)(rti.this_arg + 2)));
			break;
		case IEEE80211_RADIOTAP_ANTENNA:
			prd->m_nAntenna = (*rti.this_arg) + 1;
			break;
		case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
			prd->m_nAntSignal = (int8_t)(*rti.this_arg);
			break;
		}
	}
	pu8Payload += u16HeaderLen + interface->n80211HeaderLength;
	*dat = pu8Payload;
	*len = bytes;
	
	return 0;
}

int main(int argc, char *argv[]) 
{
	setpriority(PRIO_PROCESS, 0, 10);

	monitor_interface_t iface;
	fd_set readset;
	struct timeval to;
	PENUMBRA_RADIOTAP_DATA prd;
	int n = 0, len = 0;
	uint8_t *buf = NULL, *dat = NULL;
	
	if (argc !=2) {
		usage();
	}
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	
	open_and_configure_interface((char *)iniparser_getstring(ini, "PROGRAM_NAME:nic", NULL), &iface);
	buf = (uint8_t *)malloc(PACKET_BUF_SIZE_BYTES);
	if (buf == NULL) {
		fprintf(stderr, "Error: malloc() failed.\n");
		exit(1);
	}
	
	to.tv_sec = 0;
	to.tv_usec = 1e4;
	while (1) {
		FD_ZERO(&readset);
		FD_SET(iface.selectable_fd, &readset);

		n = select(iface.selectable_fd+1, &readset, NULL, NULL, &to);
		if (n == 0) {
			// pass
		}
		if (FD_ISSET(iface.selectable_fd, &readset)) {
			process_packet(&iface, buf, &prd, &dat, &len);
			write(STDOUT_FILENO, dat, len);
			// to-do: write prd data to shmem
		}
	}
	free(buf);
	iniparser_freedict(ini);
	return 0;
}
