/*
Copyright (c) 2012, Broadcom Europe Ltd
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in the
      documentation and/or other materials provided with the distribution.
    * Neither the name of the copyright holder nor the
      names of its contributors may be used to endorse or promote products
      derived from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY
DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
(INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

// Video deocode demo using OpenMAX IL though the ilcient helper library

// Modified for get video stream from UDP & specified bufsize
// Usage: 
//	./hello_video.bin conf.ini

/* 
[video]
udp_port=35005
udp_bufsize=524288 	#byte
*/
// Listen UDP Port 35005, UDP recv bufsize = 512kb

// If build failed, check Makefile: LDFLAGS+=-lilclient -liniparser

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bcm_host.h"
#include "ilclient.h"

#include <arpa/inet.h>
#include <errno.h>
#include <resolv.h>
#include <utime.h>
#include <unistd.h>
#include <getopt.h>
#include <endian.h>
#include <fcntl.h>
#include <iniparser.h>


static int video_decode_test(char *udp_port, char *buf_size, dictionary *ini)
{
	OMX_VIDEO_PARAM_PORTFORMATTYPE format;
	OMX_TIME_CONFIG_CLOCKSTATETYPE cstate;
	COMPONENT_T *video_decode = NULL, *video_scheduler = NULL, *video_render = NULL, *clock = NULL;
	COMPONENT_T *list[5];
	TUNNEL_T tunnel[4];
	ILCLIENT_T *client;
	int status = 0;
	unsigned int data_len = 0;
   
	struct sockaddr_in addr;
	int sockfd;
	int slen = sizeof(addr);

	memset(list, 0, sizeof(list));
	memset(tunnel, 0, sizeof(tunnel));
	bzero(&addr, sizeof(addr));

	addr.sin_family = AF_INET;
	addr.sin_port = htons(atoi(udp_port));
	addr.sin_addr.s_addr = htonl(INADDR_ANY);
	if ( (sockfd = socket(PF_INET, SOCK_DGRAM, 0)) < 0 ){
		fprintf(stderr, "ERROR: Could not create UDP socket!");
		status = -99;
		exit(1);
	}
	bind(sockfd, (struct sockaddr*)&addr, sizeof(addr));
	int udp_bufsize = atoi(buf_size);
	// We should increase net.core.rmem_max as well (>= udp_bufsize/2)
    setsockopt(sockfd, SOL_SOCKET, SO_RCVBUF, (const char*)&udp_bufsize, sizeof(udp_bufsize));
 
	if((client = ilclient_init()) == NULL)
	{
		return -3;
	}

	if(OMX_Init() != OMX_ErrorNone)
	{
		ilclient_destroy(client);
		return -4;
	}

	// create video_decode
	if(ilclient_create_component(client, &video_decode, "video_decode", ILCLIENT_DISABLE_ALL_PORTS | ILCLIENT_ENABLE_INPUT_BUFFERS) != 0)
		status = -14;
	list[0] = video_decode;

	// create video_render
	if(status == 0 && ilclient_create_component(client, &video_render, "video_render", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		status = -14;
	list[1] = video_render;

	// create clock
	if(status == 0 && ilclient_create_component(client, &clock, "clock", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		status = -14;
	list[2] = clock;

	memset(&cstate, 0, sizeof(cstate));
	cstate.nSize = sizeof(cstate);
	cstate.nVersion.nVersion = OMX_VERSION;
	cstate.eState = OMX_TIME_ClockStateWaitingForStartTime;
	cstate.nWaitMask = 1;
	if(clock != NULL && OMX_SetParameter(ILC_GET_HANDLE(clock), OMX_IndexConfigTimeClockState, &cstate) != OMX_ErrorNone)
		status = -13;

	// create video_scheduler
	if(status == 0 && ilclient_create_component(client, &video_scheduler, "video_scheduler", ILCLIENT_DISABLE_ALL_PORTS) != 0)
		status = -14;
	list[3] = video_scheduler;

	set_tunnel(tunnel, video_decode, 131, video_scheduler, 10);
	set_tunnel(tunnel+1, video_scheduler, 11, video_render, 90);
	set_tunnel(tunnel+2, clock, 80, video_scheduler, 12);

	// setup clock tunnel first
	if(status == 0 && ilclient_setup_tunnel(tunnel+2, 0, 0) != 0)
		status = -15;
	else
		ilclient_change_component_state(clock, OMX_StateExecuting);

	if(status == 0)
		ilclient_change_component_state(video_decode, OMX_StateIdle);

	memset(&format, 0, sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE));
	format.nSize = sizeof(OMX_VIDEO_PARAM_PORTFORMATTYPE);
	format.nVersion.nVersion = OMX_VERSION;
	format.nPortIndex = 130;
	format.eCompressionFormat = OMX_VIDEO_CodingAVC;

	format.xFramerate = 240 << 16;

	if(status == 0 &&
		OMX_SetParameter(ILC_GET_HANDLE(video_decode), OMX_IndexParamVideoPortFormat, &format) == OMX_ErrorNone &&
		ilclient_enable_port_buffers(video_decode, 130, NULL, NULL, NULL) == 0)
	{
		OMX_BUFFERHEADERTYPE *buf;
		int port_settings_changed = 0;
		int first_packet = 1;

		ilclient_change_component_state(video_decode, OMX_StateExecuting);

		while((buf = ilclient_get_input_buffer(video_decode, 130, 1)) != NULL)
		{
		 // feed data and wait until we get port settings changed
			unsigned char *dest = buf->pBuffer;

			//data_len = read(STDIN_FILENO, dest, buf->nAllocLen-data_len);
			data_len = recvfrom(sockfd, dest, buf->nAllocLen-data_len, 0, 
						(struct sockaddr*)&addr, &slen);

			if(port_settings_changed == 0 &&
				((data_len > 0 && ilclient_remove_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1) == 0) ||
				(data_len == 0 && ilclient_wait_for_event(video_decode, OMX_EventPortSettingsChanged, 131, 0, 0, 1,
													   ILCLIENT_EVENT_ERROR | ILCLIENT_PARAMETER_CHANGED, 10000) == 0)))
			{
				port_settings_changed = 1;

				if(ilclient_setup_tunnel(tunnel, 0, 0) != 0)
				{
					status = -7;
					break;
				}

				ilclient_change_component_state(video_scheduler, OMX_StateExecuting);

				// now setup tunnel to video_render
				if(ilclient_setup_tunnel(tunnel+1, 0, 1000) != 0)
				{
					status = -12;
					break;
				}

				ilclient_change_component_state(video_render, OMX_StateExecuting);
		 }
		 if(!data_len)
			break;

		 buf->nFilledLen = data_len;
		 data_len = 0;

		 buf->nOffset = 0;
		 if(first_packet)
		 {
			buf->nFlags = OMX_BUFFERFLAG_STARTTIME;
			first_packet = 0;
		 }
		 else
			buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN;

		 if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
		 {
			status = -6;
			break;
		 }
		}

		buf->nFilledLen = 0;
		buf->nFlags = OMX_BUFFERFLAG_TIME_UNKNOWN | OMX_BUFFERFLAG_EOS;

		if(OMX_EmptyThisBuffer(ILC_GET_HANDLE(video_decode), buf) != OMX_ErrorNone)
		 status = -20;

		// wait for EOS from render
		ilclient_wait_for_event(video_render, OMX_EventBufferFlag, 90, 0, OMX_BUFFERFLAG_EOS, 0,
							  ILCLIENT_BUFFER_FLAG_EOS, 10000);

		// need to flush the renderer to allow video_decode to disable its input port
		ilclient_flush_tunnels(tunnel, 0);

		ilclient_disable_port_buffers(video_decode, 130, NULL, NULL, NULL);
	}


	ilclient_disable_tunnel(tunnel);
	ilclient_disable_tunnel(tunnel+1);
	ilclient_disable_tunnel(tunnel+2);
	ilclient_teardown_tunnels(tunnel);

	ilclient_state_transition(list, OMX_StateIdle);
	ilclient_state_transition(list, OMX_StateLoaded);

	ilclient_cleanup_components(list);

	OMX_Deinit();

	ilclient_destroy(client);

	close(sockfd);
	iniparser_freedict(ini);
	return status;
}

int main (int argc, char **argv)
{
	bcm_host_init();
	
	char *file = argv[1];
	ini = iniparser_load(file);
	if (!ini) {
		fprintf(stderr,"iniparser: failed to load %s.\n", file);
		exit(1);
	}
	if (strcmp(iniparser_getstring(ini, "video:mode", NULL), "rx")) {
		fprintf(stderr, "Sleep - not rx mode ...\n");
		while(1) {
			sleep(50000);
		}
	}
	fprintf(stderr, "Mode rx, Listen UDP port %s, buf size %s\n", 
			iniparser_getstring(ini, "video:udp_port", NULL), 
			iniparser_getstring(ini, "video:udp_bufsize", NULL));
	
	//return video_decode_test(argv[1], argv[2]);
	return video_decode_test(iniparser_getstring(ini, "video:udp_port", NULL), 
								iniparser_getstring(ini, "video:udp_bufsize", NULL), ini);
}

