// rx (c)2015 befinitiv. Based on packetspammer by Andy Green. Dirty mods by Rodizio. GPL2 licensed.
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
#include "fec.h"
#include "lib.h"
#include "wifibroadcast.h"
#include "radiotap.h"
#include "xxtea.h"
#include <time.h>
#include <sys/resource.h>
#include <limits.h>
#include <iniparser.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <arpa/inet.h>

#define MAX_PACKET_LENGTH 4192
#define MAX_USER_PACKET_LENGTH 2278
#define MAX_DATA_OR_FEC_PACKETS_PER_BLOCK 32

#define PROGRAM_NAME "rx"
#define DEBUG 0
#define debug_print(fmt, ...) do { if (DEBUG) fprintf(stderr, fmt, __VA_ARGS__); } while (0)

#define PCAP_FILTER_CHAR "(ether[0x00:2] == 0x0801 || ether[0x00:2] == 0x0802 || ether[0x00:4] == 0xb4010000) && ether[0x04:1] == 0x%.2x"

// this is where we store a summary of the information from the radiotap header
typedef struct  {
	int m_nChannel;
	int m_nChannelFlags;
	int m_n80211mode;		// 802.11 0-abg / 1-n /2-ac
	int m_nRate;
	int m_nMCSIndex;		// 802.11n only
	int m_nAntenna;
	int m_nRadiotapFlags;
} __attribute__((packed)) PENUMBRA_RADIOTAP_DATA;

int flagHelp = 0;
int param_port = 0;
int param_data_packets_per_block = 8;
int param_fec_packets_per_block = 4;
int param_block_buffers = 1;
int param_packet_length = 1024;
int param_mode = 0;
uint16_t param_udp_in_bind_port = 0;
struct sockaddr_in udp_in_bind_addr;	
int udpin_fd;

wifibroadcast_rx_status_t *rx_status = NULL;
int max_block_num = -1;
struct sockaddr_in udp_send_addr;
struct sockaddr_in udp_bind_addr;	
int udp_sockfd, slen = sizeof(udp_send_addr);
FILE * save_fd;
int param_recording_en;
char *param_recording_path;
int param_wifi_mode = 0;
int param_encrypt_enable = 0;
char * param_encrypt_password = NULL;

long long current_timestamp() {
    struct timeval te; 
    gettimeofday(&te, NULL); // get current time
    long long milliseconds = te.tv_sec*1000LL + te.tv_usec/1000; // caculate milliseconds
    return milliseconds;
}

long long prev_time = 0;
long long now = 0;
int bytes_written = 0;

int packets_missing;
int packets_missing_last;

int dbm[6];
int dbm_last[6];

int packetcounter[6];
int packetcounter_last[6];

long long pm_prev_time = 0;
long long pm_now = 0;

long long dbm_ts_prev[6];
long long dbm_ts_now[6];

long long packetcounter_ts_prev[6];
long long packetcounter_ts_now[6];


void usage(void) {
	printf(
		"rx (c)2015 befinitiv. Based on packetspammer by Andy Green. Dirty mods by Rodizio. GPL2 licensed\n"
		"Dirty mod by libc0607@Github\n"
		"\n"
		"Usage: rx <config.ini>\n"
		"\n"
		"config.ini example:\n"
		"\t[rx]\n"
		"\tport=0             # Port number 0-255 (default 0)\n"
		"\tdatanum=8          # Number of data packets in a block (default 8). Needs to match with rx\n"
		"\tfecnum=4           # Number of FEC packets per block (default 4). Needs to match with rx\n"
		"\tpacketsize=1024    # Number of bytes per packet (default %d, max. %d). This is also the FEC block size. Needs to match with rx\n"
		"\tbufsize=1          # Number of transmissions blocks that are buffered (default 1). This is needed in case of diversity if one\n"
		"\tnic=wlan0          # Wi-Fi interface\n"
		"\tencrypt=1          # enable encrypt. Note that when encrypt is enabled, the actual payload length will be reduced by 4\n"
		"\tpassword=yarimasune1919810\n"
		"\tmode=0			  # 0-wifi 1-wifi+udp "
		"\tudp_in_bind_port=2323 # udp listen port (input)"
		, 1024, MAX_USER_PACKET_LENGTH
	);
	exit(1);
}

typedef struct {
	pcap_t *ppcap;
	int selectable_fd;
	int n80211HeaderLength;
} monitor_interface_t;

typedef struct {
	int block_num;
	packet_buffer_t *packet_buffer_list;
} block_buffer_t;

void open_and_configure_interface(const char *name, int port, monitor_interface_t *interface) 
{
	struct bpf_program bpfprogram;
	char szProgram[512];
	char szErrbuf[PCAP_ERRBUF_SIZE];

	int port_encoded = (port * 2) + 1;

	// open the interface in pcap
	szErrbuf[0] = '\0';

	interface->ppcap = pcap_open_live(name, 2350, 0, -1, szErrbuf);
	if (interface->ppcap == NULL) {
		fprintf(stderr, "Unable to open %s: %s\n", name, szErrbuf);
		exit(1);
	}
	
	if (pcap_setnonblock(interface->ppcap, 1, szErrbuf) < 0) {
		fprintf(stderr, "Error setting %s to nonblocking mode: %s\n", name, szErrbuf);
	}

	if (pcap_setdirection(interface->ppcap, PCAP_D_IN) < 0) {
		fprintf(stderr, "Error setting %s direction\n", name);
	}

	int nLinkEncap = pcap_datalink(interface->ppcap);

	if (nLinkEncap == DLT_IEEE802_11_RADIO) {
//			interface->n80211HeaderLength = 0x18; // Use the first 5 bytes as header, first two bytes frametype, next two bytes duration, then port
			// match on data short, data, rts (and port)
			sprintf(szProgram, PCAP_FILTER_CHAR, port_encoded);
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
			// ?
		}
		pcap_freecode(&bpfprogram);
	}

	interface->selectable_fd = pcap_get_selectable_fd(interface->ppcap);
}

void block_buffer_list_reset(block_buffer_t *block_buffer_list, size_t block_buffer_list_len, 
																		int block_buffer_len) 
{
    int i;
    block_buffer_t *rb = block_buffer_list;

    for(i=0; i<block_buffer_list_len; ++i) {
        rb->block_num = -1;
        int j;
        packet_buffer_t *p = rb->packet_buffer_list;
        for(j=0; j<param_data_packets_per_block+param_fec_packets_per_block; ++j) {
            p->valid = 0;
            p->crc_correct = 0;
            p->len = 0;
            p++;
        }
        rb++;
    }
}

void process_payload(uint8_t *data, size_t data_len, int crc_correct, 
								block_buffer_t *block_buffer_list, int adapter_no) 
{
    wifi_packet_header_t *wph;
    int block_num;
    int packet_num;
    int i;
    int kbitrate = 0;

    wph = (wifi_packet_header_t*)data;
    data += sizeof(wifi_packet_header_t);
    data_len -= sizeof(wifi_packet_header_t);

    block_num = wph->sequence_number / (param_data_packets_per_block+param_fec_packets_per_block);//if aram_data_packets_per_block+param_fec_packets_per_block would be limited to powers of two, this could be replaced by a logical AND operation

    //debug_print("adap %d rec %x blk %x crc %d len %d\n", adapter_no, wph->sequence_number, block_num, crc_correct, data_len);

    //we have received a block number that exceeds the currently seen ones -> we need to make room for this new block
    //or we have received a block_num that is several times smaller than the current window of buffers -> this indicated that either the window is too small or that the transmitter has been restarted
    int tx_restart = (block_num + 128*param_block_buffers < max_block_num);
    if((block_num > max_block_num || tx_restart) && crc_correct) {
        if(tx_restart) {
			rx_status->tx_restart_cnt++;
			rx_status->received_block_cnt = 0;
			rx_status->damaged_block_cnt = 0;
			rx_status->received_packet_cnt = 0;
			rx_status->lost_packet_cnt = 0;
			rx_status->kbitrate = 0;
			int g;
			for(g=0; g<MAX_PENUMBRA_INTERFACES; ++g) {
				rx_status->adapter[g].received_packet_cnt = 0;
				rx_status->adapter[g].wrong_crc_cnt = 0;
				rx_status->adapter[g].current_signal_dbm = -126;
				rx_status->adapter[g].signal_good = 0;
			}
	//          fprintf(stderr, "TX re-start detected\n");
				block_buffer_list_reset(block_buffer_list, param_block_buffers, param_data_packets_per_block + param_fec_packets_per_block);
		}

        //first, find the minimum block num in the buffers list. this will be the block that we replace
        int min_block_num = INT_MAX;
        int min_block_num_idx;
        for(i=0; i<param_block_buffers; ++i) {
            if(block_buffer_list[i].block_num < min_block_num) {
                min_block_num = block_buffer_list[i].block_num;
                min_block_num_idx = i;
            }
        }

        //debug_print("removing block %x at index %i for block %x\n", min_block_num, min_block_num_idx, block_num);

        packet_buffer_t *packet_buffer_list = block_buffer_list[min_block_num_idx].packet_buffer_list;
        int last_block_num = block_buffer_list[min_block_num_idx].block_num;

        if(last_block_num != -1) {
            rx_status->received_block_cnt++;

            //we have both pointers to the packet buffers (to get information about crc and vadility) and raw data pointers for fec_decode
            packet_buffer_t *data_pkgs[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
            packet_buffer_t *fec_pkgs[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
            uint8_t *data_blocks[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
            uint8_t *fec_blocks[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
            int datas_missing = 0, datas_corrupt = 0, fecs_missing = 0,fecs_corrupt = 0;
            int di = 0, fi = 0;

            //first, split the received packets into DATA a FEC packets and count the damaged packets
            i = 0;
            while(di < param_data_packets_per_block || fi < param_fec_packets_per_block) {
                if(di < param_data_packets_per_block) {
                    data_pkgs[di] = packet_buffer_list + i++;
                    data_blocks[di] = data_pkgs[di]->data;
                    if(!data_pkgs[di]->valid) datas_missing++;
//                  if(data_pkgs[di]->valid && !data_pkgs[di]->crc_correct) datas_corrupt++; // not needed as we dont receive fcs fail frames
                    di++;
                }
                if(fi < param_fec_packets_per_block) {
                    fec_pkgs[fi] = packet_buffer_list + i++;
                    if(!fec_pkgs[fi]->valid) fecs_missing++;
//                  if(fec_pkgs[fi]->valid && !fec_pkgs[fi]->crc_correct) fecs_corrupt++; // not needed as we dont receive fcs fail frames
                    fi++;
                }
            }

            const int good_fecs_c = param_fec_packets_per_block - fecs_missing - fecs_corrupt;
            const int datas_missing_c = datas_missing;
            const int datas_corrupt_c = datas_corrupt;
            const int fecs_missing_c = fecs_missing;
//          const int fecs_corrupt_c = fecs_corrupt;

			int packets_lost_in_block = 0;
//          int good_fecs = good_fecs_c;
            //the following three fields are infos for fec_decode
            unsigned int fec_block_nos[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
            unsigned int erased_blocks[MAX_DATA_OR_FEC_PACKETS_PER_BLOCK];
            unsigned int nr_fec_blocks = 0;

            if(datas_missing_c + fecs_missing_c > 0) {
				packets_lost_in_block = (datas_missing_c + fecs_missing_c);
				rx_status->lost_packet_cnt = rx_status->lost_packet_cnt + packets_lost_in_block;
			}

			rx_status->received_packet_cnt = rx_status->received_packet_cnt + param_data_packets_per_block + param_fec_packets_per_block - packets_lost_in_block;

			packets_missing_last = packets_missing;
			packets_missing = packets_lost_in_block;

			if (packets_missing < packets_missing_last) { // if we have less missing packets than last time, ignore
				packets_missing = packets_missing_last;
			}

			pm_now = current_timestamp();
			if (pm_now - pm_prev_time > 220) {
				pm_prev_time = current_timestamp();
				//fprintf(stderr, "miss: %d   last: %d\n", packets_missing,packets_missing_last);
				rx_status->lost_per_block_cnt = packets_missing;
				packets_missing = 0;
				packets_missing_last = 0;
			}

            fi = 0;
            di = 0;

            //look for missing DATA and replace them with good FECs
            while(di < param_data_packets_per_block && fi < param_fec_packets_per_block) {
                //if this data is fine we go to the next
                if(data_pkgs[di]->valid && data_pkgs[di]->crc_correct) { 
					di++; 
					continue; 
				}
                //if this DATA is corrupt and there are less good fecs than missing datas we cannot do anything for this data
//                if(data_pkgs[di]->valid && !data_pkgs[di]->crc_correct && good_fecs <= datas_missing) { di++; continue; } // not needed as we dont receive fcs fail frames
                //if this FEC is not received we go on to the next
                if(!fec_pkgs[fi]->valid) { 
					fi++; 
					continue; 
				}
                //if this FEC is corrupted and there are more lost packages than good fecs we should replace this DATA even with this corrupted FEC // not needed as we dont receive fcs fail frames
//                if(!fec_pkgs[fi]->crc_correct && datas_missing > good_fecs) { fi++; continue; }

                if(!data_pkgs[di]->valid) 
					datas_missing--;
//              else if(!data_pkgs[di]->crc_correct) 
//					datas_corrupt--; // not needed as we dont receive fcs fail frames

//               if(fec_pkgs[fi]->crc_correct) good_fecs--; // not needed as we dont receive fcs fail frames

                //at this point, data is invalid and fec is good -> replace data with fec
                erased_blocks[nr_fec_blocks] = di;
                fec_block_nos[nr_fec_blocks] = fi;
                fec_blocks[nr_fec_blocks] = fec_pkgs[fi]->data;
                di++;
                fi++;
                nr_fec_blocks++;
            }

            int reconstruction_failed = datas_missing_c + datas_corrupt_c > good_fecs_c;
            if(reconstruction_failed) {
                //we did not have enough FEC packets to repair this block
                rx_status->damaged_block_cnt++;
                //fprintf(stderr, "Could not fully reconstruct block %x! Damage rate: %f (%d / %d blocks)\n", last_block_num, 1.0 * rx_status->damaged_block_cnt / rx_status->received_block_cnt, rx_status->damaged_block_cnt, rx_status->received_block_cnt);
                //debug_print("Data mis: %d\tData corr: %d\tFEC mis: %d\tFEC corr: %d\n", datas_missing_c, datas_corrupt_c, fecs_missing_c, fecs_corrupt_c);
            }

            //decode data and write it to STDOUT
            fec_decode((unsigned int) param_packet_length, data_blocks, 
							param_data_packets_per_block, fec_blocks, fec_block_nos, 
							erased_blocks, nr_fec_blocks);
            for(i=0; i<param_data_packets_per_block; ++i) {
                payload_header_t *ph = (payload_header_t*)data_blocks[i];
                if (!reconstruction_failed || data_pkgs[i]->valid) {
                    //if reconstruction did fail, the data_length value is undefined. better limit it to some sensible value
                    if (ph->data_length > param_packet_length) {
						ph->data_length = param_packet_length;
					}
					
					// decrypt data if needed
					uint8_t * p_write_data;
					size_t write_data_length;
					if (param_encrypt_enable) {
						p_write_data = xxtea_decrypt(data_blocks[i] + sizeof(payload_header_t),
											ph->data_length, param_encrypt_password, &write_data_length);
					} else {
						p_write_data = data_blocks[i] + sizeof(payload_header_t);
						write_data_length = ph->data_length;
					}
					
					// write it to udp & file
					if (p_write_data != NULL) {
						sendto(udp_sockfd, p_write_data, write_data_length, 
								0, (struct sockaddr*)&udp_send_addr, slen);
						if (param_recording_en) {
							write((int)save_fd, p_write_data, write_data_length);
						}
					}
					
					// free memory if encrypt enabled
					if (param_encrypt_enable && (p_write_data != NULL)) {
						free(p_write_data);
					}
					
					//write(STDOUT_FILENO, data_blocks[i] + sizeof(payload_header_t), ph->data_length);
					//fflush(stdout);
					now = current_timestamp();
					bytes_written = bytes_written + ph->data_length;
					if (now - prev_time > 500) {
						prev_time = current_timestamp();
						kbitrate = ((bytes_written * 8) / 1024) * 2;
		//    			fprintf(stderr, "\t\tkbitrate:%d\n", kbitrate);
						rx_status->kbitrate = kbitrate;
						bytes_written = 0;
					}
                }
            }

            //reset buffers
            for(i=0; i<param_data_packets_per_block + param_fec_packets_per_block; ++i) {
                packet_buffer_t *p = packet_buffer_list + i;
                p->valid = 0;
                p->crc_correct = 0;
                p->len = 0;
            }
        }
		block_buffer_list[min_block_num_idx].block_num = block_num;
		max_block_num = block_num;
    }

    //find the buffer into which we have to write this packet
    block_buffer_t *rbb = block_buffer_list;
    for(i=0; i<param_block_buffers; ++i) {
        if(rbb->block_num == block_num) {
            break;
        }
        rbb++;
    }

    //check if we have actually found the corresponding block. this could not be the case due to a corrupt packet
    if(i != param_block_buffers) {
        packet_buffer_t *packet_buffer_list = rbb->packet_buffer_list;
        packet_num = wph->sequence_number % (param_data_packets_per_block+param_fec_packets_per_block); //if retr_block_size would be limited to powers of two, this could be replace by a locical and operation

        //only overwrite packets where the checksum is not yet correct. otherwise the packets are already received correctly
        if(packet_buffer_list[packet_num].crc_correct == 0) {
//	    	fprintf(stderr, "rx INFO: packet_buffer_list[packet_numer].crc_correct=0");
			memcpy(packet_buffer_list[packet_num].data, data, data_len);
            packet_buffer_list[packet_num].len = data_len;
            packet_buffer_list[packet_num].valid = 1;
            packet_buffer_list[packet_num].crc_correct = crc_correct;
        }
    }

}

void process_packet(monitor_interface_t *interface, block_buffer_t *block_buffer_list, 
																	int adapter_no) 
{
	struct pcap_pkthdr * ppcapPacketHeader = NULL;
	struct ieee80211_radiotap_iterator rti;
	PENUMBRA_RADIOTAP_DATA prd;
	u8 payloadBuffer[MAX_PACKET_LENGTH];
	u8 *pu8Payload = payloadBuffer;
	int bytes;
	int n;
	int retval;
	int u16HeaderLen;
	
	bzero(&prd, sizeof(prd));
	
	retval = pcap_next_ex(interface->ppcap, &ppcapPacketHeader, 
								(const u_char**)&pu8Payload); // receive

	if (retval < 0) {
		if (strcmp("The interface went down",pcap_geterr(interface->ppcap)) == 0) {
			fprintf(stderr, "rx ERROR: The interface went down\n");
			exit(9);
		} else {
			fprintf(stderr, "rx ERROR: %s\n", pcap_geterr(interface->ppcap));
			exit(2);
		}
	}

	if (retval != 1) {
	//	fprintf(stderr, "rx ERROR retval != 1: retval: %d\n", retval);
		return;
	}

	// fetch radiotap header length from radiotap header 
	// 36 for Atheros 802.11g, 39 for Atheros 802.11n
	// 18 for Ralink  802.11g
	u16HeaderLen = (pu8Payload[2] + (pu8Payload[3] << 8));
//	fprintf(stderr, "u16headerlen: %d\n", u16HeaderLen);
 
	// check for packet type and set headerlen accordingly
	pu8Payload += u16HeaderLen;
	switch (pu8Payload[1]) {
	case 0x01: // data short, rts
//		fprintf(stderr,"payload 0x01 data short, rts\n");
		interface->n80211HeaderLength = 0x05;
		break;
	case 0x02: // data
//		fprintf(stderr,"payload 0x02 data\n");
		interface->n80211HeaderLength = 0x18;
		break;
	default:
//		fprintf(stderr, "rx ERROR: uknown packet type received!\n");
		break;
	}
	pu8Payload -= u16HeaderLen;

	if (ppcapPacketHeader->len < (u16HeaderLen + interface->n80211HeaderLength)) {
		fprintf(stderr, "rx ERROR: ppcapheaderlen < u16headerlen+n80211headerlen: ppcapPacketHeader->len: %d\n", ppcapPacketHeader->len);
		return;
	}

	bytes = ppcapPacketHeader->len - (u16HeaderLen + interface->n80211HeaderLength);
	if (bytes < 0) {
		fprintf(stderr, "rx ERROR: bytes < 0: bytes: %d\n", bytes);
		return;
	}

	if (ieee80211_radiotap_iterator_init(&rti,(struct ieee80211_radiotap_header *)pu8Payload, ppcapPacketHeader->len) < 0) {
		fprintf(stderr, "rx ERROR: radiotap_iterator_init < 0\n");
		return;
	}

	while ((n = ieee80211_radiotap_iterator_next(&rti)) == 0) {
		switch (rti.this_arg_index) {
		case IEEE80211_RADIOTAP_RATE:
			prd.m_nRate = (*rti.this_arg);
			break;
		case IEEE80211_RADIOTAP_CHANNEL:
			prd.m_nChannel =
			    le16_to_cpu(*((u16 *)rti.this_arg));
			prd.m_nChannelFlags =
			    le16_to_cpu(*((u16 *)(rti.this_arg + 2)));
			break;
		case IEEE80211_RADIOTAP_ANTENNA:
			prd.m_nAntenna = (*rti.this_arg) + 1;
			break;
		case IEEE80211_RADIOTAP_FLAGS:
			prd.m_nRadiotapFlags = *rti.this_arg;
			break;
		case IEEE80211_RADIOTAP_DBM_ANTSIGNAL:
			//rx_status->adapter[adapter_no].current_signal_dbm = (int8_t)(*rti.this_arg);

			dbm_last[adapter_no] = dbm[adapter_no];
			dbm[adapter_no] = (int8_t)(*rti.this_arg);
			// What should dbm_last[] do?
			
			dbm_ts_now[adapter_no] = current_timestamp();
			if (dbm_ts_now[adapter_no] - dbm_ts_prev[adapter_no] > 220) {
			    dbm_ts_prev[adapter_no] = current_timestamp();
			    rx_status->adapter[adapter_no].current_signal_dbm = dbm[adapter_no];
			}
			break;
		}
	}

	pu8Payload += u16HeaderLen + interface->n80211HeaderLength;
//	fprintf(stderr, "pu8payload: %d\n", pu8Payload);

	// Ralink and Atheros both always supply the FCS to userspace, no need to check
	//if (prd.m_nRadiotapFlags & IEEE80211_RADIOTAP_F_FCS)
	//bytes -= 4;

	// TODO: disable checksum handling in process_payload(), not needed since we have fscfail disabled
	int checksum_correct = 1;

	rx_status->adapter[adapter_no].received_packet_cnt++;
//	rx_status->adapter[adapter_no].last_update = dbm_ts_now[adapter_no];
//	fprintf(stderr,"lu[%d]: %lld\n",adapter_no,rx_status->adapter[adapter_no].last_update);
//	rx_status->adapter[adapter_no].last_update = current_timestamp();

	process_payload(pu8Payload, bytes, checksum_correct, block_buffer_list, adapter_no);
}

void status_memory_init(wifibroadcast_rx_status_t *s) 
{
	s->received_block_cnt = 0;
	s->damaged_block_cnt = 0;
	s->received_packet_cnt = 0;
	s->lost_packet_cnt = 0;
	s->tx_restart_cnt = 0;
	s->wifi_adapter_cnt = 0;
	s->kbitrate = 0;

	int i;
	for(i=0; i<MAX_PENUMBRA_INTERFACES; ++i) {
		s->adapter[i].received_packet_cnt = 0;
		s->adapter[i].wrong_crc_cnt = 0;
		s->adapter[i].current_signal_dbm = -126;
		s->adapter[i].type = 2; // set to 2 to see if it didnt get set later ...
	}
}

wifibroadcast_rx_status_t *status_memory_open(void) 
{
	char buf[128];
	int fd;
	
	sprintf(buf, "/wifibroadcast_rx_status_%d", param_port);
///	fd = shm_open(buf, O_RDWR, S_IRUSR | S_IWUSR);
	fd = shm_open(buf, O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);

	if(fd < 0) {
		perror("shm_open");
		exit(1);
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
	
	wifibroadcast_rx_status_t *tretval = (wifibroadcast_rx_status_t*)retval;
	status_memory_init(tretval);
	
	return tretval;
}

int main(int argc, char *argv[]) 
{
	setpriority(PRIO_PROCESS, 0, -10);

	monitor_interface_t interfaces[MAX_PENUMBRA_INTERFACES];
	int i;
	//struct stat status;
	prev_time = current_timestamp();
	now = current_timestamp();

	block_buffer_t *block_buffer_list;
	
	if (argc != 2) {
		usage();
	}
	char *file = argv[1];
	dictionary *ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	param_fec_packets_per_block = iniparser_getint(ini, PROGRAM_NAME":fecnum", 0);
	param_data_packets_per_block = iniparser_getint(ini, PROGRAM_NAME":datanum", 0); 
	param_packet_length = iniparser_getint(ini, PROGRAM_NAME":packetsize", 0);
	param_port = iniparser_getint(ini, PROGRAM_NAME":port", 0);
	param_block_buffers = iniparser_getint(ini, PROGRAM_NAME":bufsize", 0);
	param_recording_en = iniparser_getint(ini, PROGRAM_NAME":recording", 0);
	param_recording_path = (char *)iniparser_getstring(ini, PROGRAM_NAME":recording_dir", NULL);
	param_mode = iniparser_getint(ini, PROGRAM_NAME":mode", 0);
	/*
	 * Note: PROGRAM_NAME":packetsize" is the length that actually in air
	 * packetsize mod 4 should be 0 (for xxtea-encrypt compatible)
	 * if encrypt is enabled, the valid data length in each packet decreased by 4 
	*/
	param_packet_length = iniparser_getint(ini, PROGRAM_NAME":packetsize", 0);		
	param_encrypt_enable = iniparser_getint(ini, PROGRAM_NAME":encrypt", 0);
	if (param_encrypt_enable == 1) {
		param_encrypt_password = (char *)iniparser_getstring(ini, PROGRAM_NAME":password", NULL);
		//param_packet_length -= 4;	// xxtea-c library has a 4 bytes header
	}
	if ((param_packet_length % 4) != 0) {
		fprintf(stderr, "ERROR: packetsize must be an integer multiple of 4; Use default 1024.");
		param_packet_length = 1024;
	}
	if (param_packet_length > MAX_USER_PACKET_LENGTH) {
		printf("Packet length is limited to %d bytes (you requested %d bytes)\n", 
								MAX_USER_PACKET_LENGTH, param_packet_length);
		return (1);
	}
	if (param_mode == 1) {
		bzero(&udp_in_bind_addr, sizeof(struct sockaddr_in));
		udp_in_bind_addr.sin_family = AF_INET;
		udp_in_bind_addr.sin_port = htons(atoi(iniparser_getstring(ini, PROGRAM_NAME":udp_in_bind_port", NULL)));
		udp_in_bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
		if ((udpin_fd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) 
			printf("ERROR: Could not create UDP in socket!");
		if (-1 == bind(udpin_fd, (struct sockaddr*)&udp_in_bind_addr, sizeof(udp_in_bind_addr))) {
			fprintf(stderr, "Bind UDP in port failed.\n");
			exit(0);
		}
	}
	
	char save_filename[128];	// should enough..?
	bzero(&save_filename, sizeof(save_filename));
	if (param_recording_en) {		
		// use timestamp in filename
		sprintf(save_filename, "%s/video-%lld.h264", 
				iniparser_getstring(ini, PROGRAM_NAME":recording_dir", NULL), current_timestamp());
		save_fd = fopen(save_filename, "wb");
	}
	fprintf(stderr, "%s Config: packet %d/%d/%d, port %d, buf %d, recording %d, recordpath %s\n",
		argv[0], param_data_packets_per_block, param_fec_packets_per_block, param_packet_length,
		param_port, param_block_buffers, param_recording_en, save_filename
	);

	fec_init();

	rx_status = status_memory_open();

	//int x = optind;

	char path[45], line[100];
	FILE* procfile;

	bzero(&udp_send_addr, slen);
	udp_send_addr.sin_family = AF_INET;
	udp_send_addr.sin_port = htons(iniparser_getint(ini, PROGRAM_NAME":udp_port", 0));
	udp_send_addr.sin_addr.s_addr = inet_addr(iniparser_getstring(ini, PROGRAM_NAME":udp_ip", NULL));
	if ((udp_sockfd = socket(PF_INET, SOCK_DGRAM, 0)) == -1) 
		printf("ERROR: Could not create UDP socket!");
	
	bzero(&udp_bind_addr, slen);	
	udp_bind_addr.sin_family = AF_INET;
	udp_bind_addr.sin_port = htons(atoi(iniparser_getstring(ini, PROGRAM_NAME":udp_bind_port", NULL)));
	udp_bind_addr.sin_addr.s_addr = htonl(INADDR_ANY);
	// always bind on the same source port to avoid UDP "connection" fail
	if (-1 == bind(udp_sockfd, (struct sockaddr*)&udp_bind_addr, sizeof(udp_bind_addr))) {
		fprintf(stderr, "Bind UDP port failed.\n");
		iniparser_freedict(ini);
		close(udp_sockfd);
		return 0;
	}
	
	// ini supports only support one interface now
	open_and_configure_interface(iniparser_getstring(ini, PROGRAM_NAME":nic", NULL), 
									param_port, interfaces);
	snprintf(path, 45, "/sys/class/net/%s/device/uevent", iniparser_getstring(ini, PROGRAM_NAME":nic", NULL));
	procfile = fopen(path, "r");
	if (!procfile) {
		fprintf(stderr,"ERROR: opening %s failed!\n", path); 
		return 0;
	}
	fgets(line, 100, procfile); // read the first line
	if(strncmp(line, "DRIVER=ath9k", 12) == 0) { // it's an atheros card
		fprintf(stderr, "ath9k card\n");
		rx_status->adapter[0].type = (int8_t)(0);
	} else {
		fprintf(stderr, "Ralink (or other) card\n");
		rx_status->adapter[0].type = (int8_t)(1);
	}
	fclose(procfile);
	usleep(10000);
	rx_status->wifi_adapter_cnt = 1;
	
	//block buffers contain both the block_num as well as packet buffers for a block.
	block_buffer_list = malloc(sizeof(block_buffer_t) * param_block_buffers);
	for(i=0; i<param_block_buffers; ++i) {
		block_buffer_list[i].block_num = -1;
		block_buffer_list[i].packet_buffer_list = lib_alloc_packet_buffer_list(
										param_data_packets_per_block+param_fec_packets_per_block, 
										MAX_PACKET_LENGTH);
	}
	int fd_sum = 0;
	for(;;) {
		packetcounter_ts_now[i] = current_timestamp();
		if (packetcounter_ts_now[i] - packetcounter_ts_prev[i] > 220) {
		    packetcounter_ts_prev[i] = current_timestamp();
			packetcounter_last[i] = packetcounter[i];
			packetcounter[i] = rx_status->adapter[i].received_packet_cnt;
//			fprintf(stderr,"counter:%d last:%d   ",packetcounter[i],packetcounter_last[i]);
			if (packetcounter[i] == packetcounter_last[i]) {
				rx_status->adapter[i].signal_good = 0;
//			    fprintf(stderr,"signal_good[%d]:%d\n",i,rx_status->adapter[i].signal_good);
			} else {
				rx_status->adapter[i].signal_good = 1;
//			    fprintf(stderr,"signal_good[%d]:%d\n",i,rx_status->adapter[i].signal_good);
			}
		}
		fd_set readset;
		struct timeval to;
		to.tv_sec = 0;
		to.tv_usec = 1e5; // 100ms

		FD_ZERO(&readset);
		fd_sum = 0;
		FD_SET(interfaces[0].selectable_fd, &readset);
		if (param_mode)
			FD_SET(udpin_fd, &readset);
		fd_sum = interfaces[0].selectable_fd + udpin_fd;
		int n = select(fd_sum+1, &readset, NULL, NULL, &to);
		//int n = select(30, &readset, NULL, NULL, &to);	// what is 30???
		if (n == 0) 
			continue;
		if(FD_ISSET(interfaces[0].selectable_fd, &readset)) {
			process_packet(interfaces, block_buffer_list, 0);
		}
		if(FD_ISSET(udpin_fd, &readset)) {
			// to-do
		}
	}
	iniparser_freedict(ini);
	fclose(save_fd);
	close(udp_sockfd);
	return (0);
}

