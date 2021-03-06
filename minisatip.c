/*
 * Copyright (C) 2014-2020 Catalin Toda <catalinii@yahoo.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
 * USA
 *
 */

#include "minisatip.h"
#include <errno.h>
#include <getopt.h>
#include <signal.h>
#include <sys/ucontext.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <syslog.h>
#include "socketworks.h"
#include "stream.h"
#include "adapter.h"
#include "dvb.h"
#include "dvbapi.h"

struct struct_opts opts;

extern sockets s[MAX_SOCKS];
char public[] = "Public: OPTIONS, DESCRIBE, SETUP, PLAY, TEARDOWN";
int rtsp, http, si, si1, ssdp1;

void
usage ()
{
	LOGL (0, "minisatip [-f] [-r remote_rtp_host] [-d device_id] [-w http_server[:port]] [-p public_host] [-s rtp_port] [-a no] [-m mac] [-l] [-a X:Y:Z] [-e X-Y,Z]\n \
		-f foreground, otherwise run in background\n\
		-r remote_rtp_host: send remote rtp to remote_rtp_host\n \
		-d specify the device id (in case there are multiple SAT>IP servers in the network)\n \
		-w http_server[:port]: specify the host and the port where the xml file can be downloaded from \n\
		-x port: port for listening on http\n\
		-y rtsp_port: port for listening for rtsp requests (default: 554)\n\
		-z force to get signal from the DVB hardware every 200ms (use with care, only when needed)\n\
		-a x:y:z simulate x DVB-S2, y DVB-T2 and z DVB-C adapters on this box (0 means auto-detect)\n\
		-m xx: simulate xx as local mac address, generates UUID based on mac\n\
		-e list_of_enabled adapters: enable only specified adapters, example 0-2,5,7 (no spaces between parameters)\n\
		-c X: bandwidth capping for the output to the network (default: unlimited)\n\
		-b X:Y : set the app adapter buffer to X Bytes (default: %d) and set the kernel DVB buffer to Y Bytes (default: %d) - both multiple of 188\n\
		-l increases the verbosity (you can use multiple -l), logging to stdout in foreground mode or in /tmp/log when a daemon\n\
		-g use syslog instead stdout for logging, multiple -g - print to stderr as well\n\
		-p url: specify playlist url using X_SATIPM3U header \n\
		-u unicable_string: defines the unicable adapters (A) and their slot (S), frequency (F) and optionally the PIN for the switch:\n\
		\tThe format is: A1:S1-F1[-PIN][,A2:S2-F2[-PIN][,...]] example: 2:0-1284[-1111]\n\
		-j jess_string - same format as unicable_string \n\
		-o host:port - specify the hostname and port for the dvbapi server (oscam) \n\
		-s DELSYS:host:port - specify the remote satip host and port with delivery system DELSYS: example: dvbs2:192.168.9.9[:554]\n\
		-t clean the PSI from all CA information, the client will see the channel as clear if decrypted successfully\n\
		",
		ADAPTER_BUFFER, DVR_BUFFER);
	exit (1);
}


void
set_options (int argc, char *argv[])
{
	int opt;
	int sethost = 0;
	int index;
	char *lip;

	opts.log = 1;
	opts.rrtp = NULL;
	opts.disc_host = "239.255.255.250";
	opts.start_rtp = 5500;
	opts.http_port = 8080;
	opts.http_host = NULL;
	opts.log = 0;
	opts.timeout_sec = 30000;
	opts.force_sadapter = 0;
	opts.force_tadapter = 0;
	opts.force_cadapter = 0;
	opts.mac[0] = 0;
	opts.daemon = 1;
	opts.bw = 0;
	opts.device_id = 0;
	opts.bootid = 0;
	opts.force_scan = 0;
	opts.dvr_buffer = DVR_BUFFER;
	opts.adapter_buffer = ADAPTER_BUFFER;
	opts.file_line = 0;
	opts.dvbapi_port = 0;
	opts.dvbapi_host = NULL;
	opts.satipc = NULL;
	opts.drop_encrypted = 1;
	opts.rtsp_port = 554;
	opts.clean_psi = 0;
	opts.output_buffer = 512*1024;
	memset(opts.playlist, sizeof(opts.playlist), 0);
	
	
	while ((opt = getopt (argc, argv, "flr:a:td:w:p:s:hc:b:m:p:e:x:u:j:o:gy:z")) != -1)
	{
		//              printf("options %d %c %s\n",opt,opt,optarg);
		switch (opt)
		{
			case FOREGROUND_OPT:
			{
				opts.daemon = 0;
				break;
			}
			case MAC_OPT:
			{
				strncpy (opts.mac, optarg, 12);
				opts.mac[12] = 0;
				break;
			}
			case RRTP_OPT:
			{
				opts.rrtp = optarg;
				break;
			}

			case DEVICEID_OPT:
			{
				opts.device_id = atoi (optarg);
				break;
			}

			case HTTPSERVER_OPT:
			{
				//                              int i=0;
				opts.http_host = optarg;
				sethost = 1;
				break;
			}

			case LOG_OPT:
			{
				opts.log++;
				break;
			}

			case SYSLOG_OPT:
			{
				opts.slog++;
				break;
			}

			case HELP_OPT:
			{
				usage ();
				exit (0);
			}

			case HTTPPORT_OPT:
			{
				opts.http_port = atoi (optarg);
				break;
			}

			case BW_OPT:
			{
				opts.bw = atoi (optarg) * 1024;
				break;
			}

			case DVRBUFFER_OPT:
			{
				sscanf(optarg,"%d:%d", &opts.adapter_buffer, &opts.dvr_buffer) ;
				opts.adapter_buffer = (opts.adapter_buffer/188) * 188;
				if(opts.adapter_buffer < ADAPTER_BUFFER)
					opts.adapter_buffer = ADAPTER_BUFFER;
				if(opts.dvr_buffer == 0 )
					opts.dvr_buffer = DVR_BUFFER;
					
				break;
			}

			case DVBS2_ADAPTERS_OPT:
			{
				sscanf(optarg,"%d:%d:%d", &opts.force_sadapter, &opts.force_tadapter, &opts.force_cadapter) ;
				break;
			}

			case CLEANPSI_OPT:
			{
				opts.clean_psi = 1;
				break;
			}
			
			case SCAN_OPT:
			{
				opts.force_scan = 1;
				break;
			}

			case PLAYLIST_OPT:
			{
				snprintf(opts.playlist, sizeof(opts.playlist), "<satip:X_SATIPM3U xmlns:satip=\"urn:ses-com:satip\">%s</satip:X_SATIPM3U>\r\n",optarg);
				break;
			}
			
			case ENABLE_ADAPTERS_OPT:
			{
				enable_adapters(optarg);
				break;
			}
			
			case UNICABLE_OPT:
			{
				set_unicable_adapters(optarg, SWITCH_UNICABLE);
				break;
			}

			case JESS_OPT:
			{
				set_unicable_adapters(optarg, SWITCH_JESS);
				break;
			}
			
			case DVBAPI_OPT:
			{
				char* sep1 = strchr(optarg, ':');
				if ( sep1 != NULL)
				{
					*sep1 = 0;
					opts.dvbapi_host = optarg;
					opts.dvbapi_port = map_int(sep1 + 1, NULL );
				}
				break;
			}
			
			case RTSPPORT_OPT:
			{
				opts.rtsp_port = atoi (optarg);
				break;
			}

			case SATIPCLIENT_OPT:
				opts.satipc = optarg;
				break;
			}

			
	}
	
	if(opts.bw && (opts.bw < opts.adapter_buffer))
			opts.adapter_buffer = (opts.bw / 188) * 188;
			
	lip = getlocalip ();
	if (!opts.http_host)
	{
		opts.http_host = (char *) malloc (MAX_HOST);
		sprintf (opts.http_host, "%s:%d", lip, opts.http_port);
	}
}



#define RBUF 4000

int
read_rtsp (sockets * s)
{
	char *arg[50];
	int cseq, la, i, rlen;
	char *proto, *transport = NULL;
	int sess_id = 0;
	char buf[2000];
	streams *sid = get_sid(s->sid);

	if(s->buf[0]==0x24 && s->buf[1]<2)
	{
		if(sid)
			sid->rtime = s->rtime;

		int rtsp_len = s->buf[2]*256+s->buf[3];
		LOG("Received RTSP over tcp packet (sock_id %d, stream %d, rlen %d) packet len: %d, type %02X %02X discarding %s...", 
			s->id, s->sid, s->rlen, rtsp_len , s->buf[4], s->buf[5], (s->rlen == rtsp_len+4)?"complete":"fragment" );		
		if(s->rlen == rtsp_len+4){ // we did not receive the entire packet
			s->rlen = 0;			
			return 0;
		}
	}
	
	if (s->rlen < 4 || !end_of_header(s->buf + s->rlen - 4))
	{
		if( s->rlen > RBUF - 10 )
		{
			LOG("Discarding %d bytes from the socket buffer, request > %d, consider increasing  RBUF", s->rlen, RBUF);
			s->rlen = 0;
		}
		LOG("read_rtsp: read %d bytes from handle %d, sock_id %d, flags %d not ending with \\r\\n\\r\\n", s->rlen, s->sock, s->id, s->flags);
		if ( s->flags & 1 ) 
			return 0;		
		unsigned char *new_alloc = malloc1 (RBUF);
		memcpy(new_alloc, s->buf, s->rlen);
		s->buf = new_alloc;
		s->flags = s->flags | 1;
		return 0;
	}

	rlen = s->rlen;
	s->rlen = 0;

	LOG ("read RTSP (from handle %d sock_id %d, len: %d):\n%s", s->sock, s->id, s->rlen, s->buf);

	if( (s->type != TYPE_HTTP ) && (strncasecmp(s->buf, "GET", 3) == 0))
	{
		http_response (s , 404, NULL, NULL, 0, 0);
		return 0;
	}
	
	la = split (arg, s->buf, 50, ' ');
	cseq = 0;	
	if (la<2)
		LOG_AND_RETURN(0, "Most likely not an RTSP packet sock_id: %d sid: %d rlen: %d, dropping ....", s->id, s->sid, rlen); 
	
	if(s->sid<0)
		for (i = 0; i < la; i++)	
			if (strncasecmp ("Session:", arg[i], 8) == 0)
			{
				sess_id = map_int(header_parameter(arg, i), NULL);
				s->sid = find_session_id(sess_id);		
			}

	if(strstr(arg[1], "freq") || strstr(arg[1], "pids"))
	{
		int old_sid = s->sid;
		sid = (streams *) setup_stream (arg[1], s);
	}
	sid = get_sid(s->sid);
	if(sid)
		sid->rtime = s->rtime;

	if (sess_id)    
			set_session_id(s->sid, sess_id);
		
	
	for (i = 0; i < la; i++)
		if (strncasecmp ("CSeq:", arg[i], 5) == 0)
			cseq = map_int (header_parameter(arg, i), NULL);
		else if (strncasecmp ("Transport:", arg[i], 9) == 0){
			transport = header_parameter(arg, i);

			if( -1 == decode_transport (s, transport, opts.rrtp, opts.start_rtp))
			{
				http_response (s, 400, NULL, NULL, cseq, 0);
				return 0;
			}
		}
	else if (strstr (arg[i], "LIVE555"))
			if(sid) sid->timeout = 0;
	else if (strstr (arg[i], "Lavf"))
			if(sid) sid->timeout = 0;
	
	if((strncasecmp (arg[0], "PLAY", 4) == 0) || (strncasecmp (arg[0], "GET", 3) == 0) || (strncasecmp (arg[0], "SETUP", 5) == 0)) 
	{
		char ra[100];
		int rv;
			
		if (!( sid = get_sid(s->sid)))
		{
			http_response (s, 454, NULL, NULL, cseq, 0);
			return 0;
		}

		if ((strncasecmp (arg[0], "PLAY", 3) == 0) || (strncasecmp (arg[0], "GET", 3) == 0))
			if ((rv = start_play (sid, s)) < 0)
			{
				http_response (s, -rv , NULL, NULL, cseq, 0);
				return 0;
			}
		strcpy(ra, inet_ntoa (sid->sa.sin_addr));
		buf[0] = 0;
		if(transport)
		{
			int s_timeout = (sid->timeout ? sid->timeout : opts.timeout_sec) / 1000;
			switch (sid->type)
			{
				case STREAM_RTSP_UDP:
					if (atoi (ra) < 239)
						snprintf (buf, sizeof(buf), "Transport: RTP/AVP;unicast;destination=%s;source=%s;client_port=%d-%d;server_port=%d-%d\r\nSession: %010d;timeout=%d\r\ncom.ses.streamID: %d",
							ra, get_sock_host (s->sock), ntohs (sid->sa.sin_port), ntohs (sid->sa.sin_port) + 1,
//							opts.start_rtp, opts.start_rtp + 1, 
							get_sock_port(sid->rsock), get_sock_port(sid->rtcp),
							get_session_id (s->sid), s_timeout, sid->sid + 1);
					else
						snprintf (buf, sizeof(buf), "Transport: RTP/AVP;multicast;destination=%s;port=%d-%d\r\nSession: %010d;timeout=%d\r\ncom.ses.streamID: %d",
							ra, ntohs (sid->sa.sin_port), ntohs (sid->sa.sin_port) + 1,
							get_session_id (s->sid), s_timeout , sid->sid + 1);
					break;
				case STREAM_RTSP_TCP:
					snprintf(buf, sizeof(buf), "Transport: RTP/AVP/TCP;interleaved=0-1\r\nSession: %010d;timeout=%d\r\ncom.ses.streamID: %d", 
						get_session_id (s->sid), s_timeout, sid->sid + 1);
					break;
			}
		}
		
		if (strncasecmp(arg[0], "PLAY", 4) == 0)
		{
			char *qm = strchr(arg[1], '?');
			if(qm)
				*qm = 0;
			if(buf[0])
				strcat(buf, "\r\n");
			
			snprintf(buf + strlen(buf), sizeof(buf) - strlen(buf) - 1,  "RTP-Info: url=%s;seq=%d;rtptime=%lld\r\nRange: npt=0.000-", arg[1], getTick(),getTickUs()/1000000);
		}
		if(buf[0]==0 && sid->type == STREAM_HTTP)
				snprintf(buf, sizeof(buf), "Content-Type: video/mp2t");
		http_response (s, 200, buf, NULL, cseq, 0);
	}
	else if (strncmp (arg[0], "TEARDOWN", 8) == 0)
	{
		streams *sid;
		buf[0] = 0;
		if(get_sid(s->sid))
			sprintf(buf, "Session: %010d", get_session_id(s->sid));
		close_stream (s->sid);		
		http_response (s, 200, buf, NULL, cseq, 0);
	}
	else
	{
		if (strncmp (arg[0], "DESCRIBE", 8) == 0)
		{
			char sbuf[1000];
			char *rv;
			rv = describe_streams(s, arg[1], sbuf, sizeof(sbuf));
			if (! rv)
			{
				http_response (s, 404, NULL, NULL, cseq, 0);
				return 0;
			}	
			snprintf(buf, sizeof(buf), "Content-type: application/sdp\r\nContent-Base: rtsp://%s/", get_sock_host(s->sock));
			http_response (s, 200, buf, sbuf, cseq, 0);
				
		}
		else if (strncmp (arg[0], "OPTIONS", 8) == 0)
		{
			http_response (s, 200, public, NULL, cseq, 0);
		}
	}
	return 0;
}

#define REPLY_AND_RETURN(c) {http_response (s, c, NULL, NULL, 0, 0);return 0;}

char uuid[100];
int uuidi;
struct sockaddr_in ssdp_sa;

int
read_http (sockets * s)
{
	char buf[20000];
	char *arg[50];
	int la, rlen;
	char *xml =
		"<?xml version=\"1.0\"?>"
		"<root xmlns=\"urn:schemas-upnp-org:device-1-0\" configId=\"0\">"
		"<specVersion>"
		"<major>1</major>"
		"<minor>1</minor>"
		"</specVersion>"
		"<device>"
		"<deviceType>urn:ses-com:device:SatIPServer:1</deviceType>"
		"<friendlyName>minisatip</friendlyName>"
		"<manufacturer>cata</manufacturer>"
		"<manufacturerURL>http://github.com/catalinii/minisatip</manufacturerURL>"
		"<modelDescription>minisatip for Linux</modelDescription>"
		"<modelName>minisatip</modelName>"
		"<modelNumber>1.1</modelNumber>"
		"<modelURL></modelURL>"
		"<serialNumber>1</serialNumber>"
		"<UDN>uuid:%s</UDN>"
		"<iconList><icon>"
 		"<mimetype>image/png</mimetype>"
 		"<width>48</width>"
 		"<height>48</height>"
 		"<depth>24</depth>"
 		"<url>/icons/sm.png</url>"
 		"</icon><icon>"
 		"<mimetype>image/png</mimetype>"
 		"<width>120</width>"
 		"<height>120</height>"
 		"<depth>24</depth>"
 		"<url>/icons/lr.png</url>"
 		"</icon><icon>"
 		"<mimetype>image/jpeg</mimetype>"
 		"<width>48</width>"
 		"<height>48</height>"
 		"<depth>24</depth>"
 		"<url>/icons/sm.jpg</url>"
 		"</icon><icon>"
 		"<mimetype>image/jpeg</mimetype>"
 		"<width>120</width>"
 		"<height>120</height>"
 		"<depth>24</depth>"
 		"<url>/icons/lr.jpg</url>"
 		"</icon></iconList>"
		"<presentationURL>http://%s/</presentationURL>\r\n"
		"<satip:X_SATIPCAP xmlns:satip=\"urn:ses-com:satip\">%s</satip:X_SATIPCAP>"
		"%s"
		"</device></root>";

	if (s->rlen < 5 || !end_of_header(s->buf + s->rlen - 4))
	{
		if( s->rlen > RBUF - 10 )
		{
			LOG("Discarding %d bytes from the socket buffer, request > %d, consider increasing  RBUF", s->rlen, RBUF);
			s->rlen = 0;
		}
		if ( s->flags & 1 ) 
			return 0;		
		unsigned char *new_alloc = malloc1 (RBUF);
		memcpy(new_alloc, s->buf, s->rlen);
		set_socket_buffer(s->id, new_alloc, RBUF);
		s->flags = s->flags | 1;
		return 0;
	}

	if (strncasecmp(s->buf,"GET ",4)==0 && strstr(s->buf, "/?" ))
	{
		read_rtsp(s);
		return 0;
	}

	rlen = s->rlen;
	s->rlen = 0;
	
	LOG ("read HTTP from %d sid: %d: %s", s->sock, s->sid, s->buf);
	
	la = split (arg, s->buf, 50, ' ');
	//      LOG("args: %s -> %s -> %s",arg[0],arg[1],arg[2]);
	if (strncmp (arg[0], "GET", 3) != 0)
		REPLY_AND_RETURN(503);
	if (uuidi == 0)
		ssdp_discovery (s);

	if (strncmp (arg[1], "/desc.xml", 9) == 0)
	{
		int tuner_s2, tuner_t, tuner_c, tuner_t2, tuner_c2;
		char adapters[400];
		char headers[500];
		tuner_s2 = getS2Adapters ();
		tuner_t = getTAdapters ();
		tuner_c = getCAdapters ();
		tuner_t2 = getT2Adapters ();
		tuner_c2 = getC2Adapters ();
		
		memset(adapters, 0, sizeof(adapters));
		if(tuner_s2)sprintf(adapters, "DVBS2-%d,", tuner_s2);
		if(tuner_t)sprintf(adapters + strlen(adapters), "DVBT-%d,", tuner_t);
		if(tuner_c)sprintf(adapters + strlen(adapters), "DVBC-%d,", tuner_c);
		if(tuner_t2)sprintf(adapters + strlen(adapters), "DVBT2-%d,", tuner_t2);
		if(tuner_c2)sprintf(adapters + strlen(adapters), "DVBC2-%d,", tuner_c2);
		if(tuner_s2 + tuner_t + tuner_c + tuner_t2 + tuner_c2 == 0)
			strcpy(adapters,"DVBS2-0,");
		adapters[strlen(adapters)-1] = 0;
		sprintf (buf, xml, uuid, getlocalip(), adapters, opts.playlist);
		sprintf (headers,  "CACHE-CONTROL: no-cache\r\nContent-type: text/xml\r\nX-SATIP-RTSP-Port: %d", opts.rtsp_port);
		http_response (s, 200, headers, buf, 0, 0);
		return 0;
	}

	if (strncmp (arg[1], "/icons/", 7) == 0)
	{
		char *ctype = NULL;
		int nl = sizeof(buf);
		if( arg[1][strlen(arg[1])-2] == 'n' )
			ctype = "Content-type: image/png\r\nConnection: close";
		else if (arg[1][strlen(arg[1])-2] == 'p')			
			ctype = "Content-type: image/jpeg\r\nConnection: close";
		else REPLY_AND_RETURN(404); 
		
		readfile(arg[1], buf, &nl);
		if(nl == 0)
			http_response (s, 404, NULL, NULL, 0, 0);
		else
			http_response (s, 200, ctype, buf, 0, nl);
		return 0;
	}	
	
	http_response (s, 404, NULL, NULL, 0, 0);
	return 0;
}


int
close_http (sockets * s)
{
	streams *sid = get_sid(s->sid);
	if (s->flags & 1  && s->buf)
		free1 (s->buf);
	s->flags = 0;
	s->buf = NULL;
	LOG ("Requested stream close %d timeout %d type %d", s->sid, sid?sid->timeout:-1, sid?sid->type:-1);
	if(sid && ((sid->type == STREAM_RTSP_UDP && sid->timeout != 0) || sid->type == 0 && sid->timeout != 0)) 
			// Do not close rtsp udp as most likely there was no TEARDOWN at this point
		return 0;
	close_stream (s->sid);
	return 0;
}


int
ssdp_discovery (sockets * s)
{
	char *reply = "NOTIFY * HTTP/1.1\r\n"
		"HOST: %s:1900\r\n"
		"CACHE-CONTROL: max-age=1800\r\n"
		"LOCATION: http://%s/desc.xml\r\n"
		"NT: %s\r\n"
		"NTS: ssdp:alive \r\n"
		"SERVER: Linux/1.0 UPnP/1.1 minisatip/%s\r\n"
		"USN: uuid:%s%s\r\n"
		"BOOTID.UPNP.ORG: %d\r\n"
		"CONFIGID.UPNP.ORG: 0\r\n" "DEVICEID.SES.COM: %d\r\n\r\n\0";
	char buf[500], mac[15] = "00000000000000";
	char nt[3][50];
	
	char uuid1[] = "11223344-9999-0000-b7ae";
	socklen_t salen;
	int i;

	if (uuidi == 0)
	{
		uuidi = 1;
		get_mac (mac);
		sprintf (uuid, "%s-%s", uuid1, mac);
		fill_sockaddr (&ssdp_sa, opts.disc_host, 1900);
	}
	strcpy(nt[0], "::upnp:rootdevice");
	sprintf(nt[1], "::uuid:%s", uuid);
	strcpy(nt[2], "::urn:ses-com:device:SatIPServer:1");
	
	if(s->type != TYPE_UDP) return 0;
	
	LOG ("ssdp_discovery: bootid: %d deviceid: %d http: %s", opts.bootid, opts.device_id, opts.http_host);	
	
	for(i = 0; i<3; i++)
	{
		sprintf (buf, reply, opts.disc_host, opts.http_host, nt[i] + 2, VERSION, uuid, i==1?"":nt[i], opts.bootid, opts.device_id);
		salen = sizeof (ssdp_sa);	
		LOGL(3, "Discovery packet %d:\n%s", i+1, buf);
		sendto (s->sock, buf, strlen (buf), MSG_NOSIGNAL,
			(const struct sockaddr *) &ssdp_sa, salen);
	}
	s->rtime = getTick ();
	return 0;
}


int ssdp;
int
ssdp_reply (sockets * s)
{
	char *reply = "HTTP/1.1 200 OK\r\n"
		"CACHE-CONTROL: max-age=1800\r\n"
		"DATE: %s\r\n"
		"EXT:\r\n"
		"LOCATION: http://%s/desc.xml\r\n"
		"SERVER: Linux/1.0 UPnP/1.1 minisatip/%s\r\n"
		"ST: urn:ses-com:device:SatIPServer:1\r\n"
		"USN: uuid:%s::urn:ses-com:device:SatIPServer:1\r\n"
		"BOOTID.UPNP.ORG: %d\r\n"
		"CONFIGID.UPNP.ORG: 0\r\n" "DEVICEID.SES.COM: %d\r\n\0";
	char *device_id_conflict = "M-SEARCH * HTTP/1.1\r\n"
		"HOST: %s:1900\r\n"
		"MAN: \"ssdp:discover\"\r\n"
		"ST: urn:ses-com:device:SatIPServer:1\r\n"
		"USER-AGENT: Linux/1.0 UPnP/1.1 minisatip/%s\r\n"
		"DEVICEID.SES.COM: %d\r\n\0";
	socklen_t salen;
	char *man, *man_sd, *didsescom, *ruuid, *rdid;
	char buf[500];
	int did = 0;

	if (uuidi == 0)
		ssdp_discovery (s);

	salen = sizeof (s->sa);
	ruuid = strcasestr(s->buf, "uuid:");	
	if( ruuid && strncmp(uuid, strip(ruuid+5), strlen(uuid)) == 0)
	{
		LOGL(3, "Dropping packet from the same UUID as mine (from %s:%d)", inet_ntoa(s->sa.sin_addr), ntohs(s->sa.sin_port));		
		return 0;
	}

	// not my uuid
	LOG("Received SSDP packet from %s:%d -> handle %d", inet_ntoa(s->sa.sin_addr), ntohs(s->sa.sin_port), s->sock);
	LOGL(3, "%s", s->buf);
	
	if (strncasecmp (s->buf, "NOTIFY", 6) == 0)
	{
		rdid = strcasestr(s->buf, "DEVICEID.SES.COM:");
		if(rdid && opts.device_id == map_int(strip(rdid + 17), NULL))
		{						
			snprintf(buf, sizeof(buf), device_id_conflict, getlocalip(), VERSION, opts.device_id);
			LOG("A new device joined the network with the same Device ID:  %s, asking to change DEVICEID.SES.COM", inet_ntoa(s->sa.sin_addr));
			sendto (ssdp, buf, strlen (buf), MSG_NOSIGNAL, (const struct sockaddr *) &s->sa, salen);
		}
			
		return 0;
	}	
	
	man = strcasestr(s->buf, "MAN");
	man_sd = strcasestr(s->buf, "ssdp:discover");
	if(( didsescom = strcasestr(s->buf, "DEVICEID.SES.COM:") ))
		did = map_int(didsescom + 17, NULL);		
	
	if(man && man_sd && didsescom && (s->rtime < 15000) && did == opts.device_id)  // SSDP Device ID clash, only first 5 seconds after the announcement
	{
			opts.device_id ++;
			s[si].close_sec = 1800 * 1000;
			s[si].rtime = - s[si].close_sec;		
			LOG("Device ID conflict, changing our device id to %d, destination SAT>IP server %s", opts.device_id, inet_ntoa(s->sa.sin_addr));
			readBootID();
	} else did = opts.device_id;
	
	if(strncmp(s->buf,"HTTP/1",6)==0)
		LOG_AND_RETURN(0, "ssdp_reply: the message is a reply, ignoring....");
	
	sprintf (buf, reply, get_current_timestamp (), opts.http_host, VERSION, uuid, opts.bootid, did);
	
	LOG ("ssdp_reply fd: %d -> %s:%d, bootid: %d deviceid: %d http: %s", ssdp, inet_ntoa (s->sa.sin_addr), ntohs(s->sa.sin_port), opts.bootid, did, opts.http_host);
								 //use ssdp (unicast) even if received to multicast address
	LOGL (3, "%s", buf);
	sendto (ssdp, buf, strlen (buf), MSG_NOSIGNAL, (const struct sockaddr *) &s->sa, salen);
	return 0;
}


int
new_rtsp (sockets * s)
{
	s->type = TYPE_RTSP;
	s->action = (socket_action) read_rtsp;
	s->close = (socket_action) close_http;
	return 0;
}


int
new_http (sockets * s)
{
	s->type = TYPE_HTTP;
	s->action = (socket_action) read_http;
	s->close = (socket_action) close_http;
	return 0;
}




void write_pid_file()
{
	FILE *f;
	if((f = fopen (PID_FILE, "wt")))
	{
		fprintf(f, "%d", getpid());
		fclose(f);
	}
}

extern char pn[256];

int
main (int argc, char *argv[])
{
	realpath(argv[0], pn);
		
	set_signal_handler ();
	set_options (argc, argv);
	if (opts.daemon)
		becomeDaemon ();
	if (opts.slog)
		openlog ("minisatip", LOG_NDELAY|LOG_NOWAIT|LOG_PID|(opts.slog>1?LOG_PERROR:0), LOG_DAEMON);
	LOGL(0, "Starting minisatip version %s, compiled with s2api version: %04X",VERSION, DVBAPIVERSION);
	readBootID();
	if ((ssdp = udp_bind (NULL, 1900)) < 1)
		FAIL ("SSDP: Could not bind on udp port 1900");
	if ((ssdp1 = udp_bind (opts.disc_host, 1900)) < 1)
		FAIL ("SSDP: Could not bind on %s udp port 1900", opts.disc_host);
	if ((rtsp = tcp_listen (NULL, opts.rtsp_port)) < 1)
		FAIL ("RTSP: Could not listen on port %d", opts.rtsp_port);
	if ((http = tcp_listen (NULL, opts.http_port)) < 1)
		FAIL ("Could not listen on http port %d", opts.http_port);

	si =
		sockets_add (ssdp, NULL, -1, TYPE_UDP, (socket_action) ssdp_reply, NULL,
		(socket_action) ssdp_discovery);
	si1 =
		sockets_add (ssdp1, NULL, -1, TYPE_UDP, (socket_action) ssdp_reply, NULL,
		(socket_action) ssdp_discovery);
	if (si < 0 || si1 < 0)
		FAIL ("sockets_add failed for ssdp");

	s[si].close_sec = 60 * 1000;
	s[si].rtime = -s[si].close_sec;
	if (0 > sockets_add (rtsp, NULL, -1, TYPE_SERVER, (socket_action) new_rtsp,
		NULL, (socket_action) close_http))
		FAIL ("sockets_add failed for rtsp");
	if (0 > sockets_add (http, NULL, -1, TYPE_SERVER, (socket_action) new_http,
		NULL, (socket_action) close_http))
		FAIL ("sockets_add failed for http");
	
	LOGL (0, "Initializing with %d devices", init_hw ());
	write_pid_file();
#ifndef DISABLE_DVBCSA	
	init_dvbapi();
#endif
	select_and_execute ();
	unlink(PID_FILE);
	free_all ();
	if (opts.slog)
		closelog();
        return 0;
}


int readBootID()
{
	int did = 0;
	opts.bootid = 0;
	FILE *f=fopen("bootid","rt");
	if(f)
	{
		fscanf(f,"%d %d", &opts.bootid, &did);
		fclose(f);
		if(opts.device_id < 1)
			opts.device_id = did;			
	}
	opts.bootid++;
	if(opts.device_id < 1)
		opts.device_id = 1;
	f=fopen("bootid","wt");
	if(f)
	{
		fprintf(f,"%d %d",opts.bootid, opts.device_id);
		fclose(f);
	}
	return opts.bootid;
}


int readfile(char *fn,char *buf,int *len)
{
	char ffn[500];
	char *path[]={".","/usr/share/minisatip",NULL};
	int fd,i,nl=0;
	
	if(strstr(fn, ".."))
		return 0;
	for( i=0; path[i]; i++)
	{
		strcpy(ffn, path[i]);
		strncat(ffn, fn, sizeof(ffn)-strlen(path[i])-1);
		ffn[sizeof(ffn)-1] = 0;
		if((fd = open(ffn,O_RDONLY))<0)
			continue;
		
		nl = read(fd, buf, *len);
		close(fd);
		LOG("opened %s fd %d and read %d bytes from it", ffn, fd, nl);
	}
	*len = nl;
	buf[nl] = 0;
	
	return nl;
}


char *
http_response (sockets *s, int rc, char *ah, char *desc, int cseq, int lr)
{
	int binary = 0;
	char *desc1;
	char *reply =
		"%s/1.0 %d %s\r\nDate: %s%s%s\r\n%s\r\nContent-Length: %d\r\n\r\n%s";
	char *reply0 = 
		"%s/1.0 %d %s\r\nDate: %s%s%s\r\n%s\r\n\r\n";
	char *d;
	char *proto;
	char sess_id[100], scseq[100];
	
	
	if( s->type == TYPE_HTTP)
		proto = "HTTP";
	else 
		proto = "RTSP";
		
	if (!ah)
		ah = public;
	if (!desc)
		desc = "";
	if (rc == 200)
		d = "OK";
	else if (rc == 400)
		d = "Bad Request";
	else if (rc == 403)
		d = "Forbidden";
	else if (rc == 404)
		d = "Not Found";
	else if (rc == 500)
		d = "Internal Server Error";
	else if (rc == 501)
		d = "Not Implemented";
	else if (rc == 405)
		d = "Method Not Allowed";
	else if (rc == 454)
		d = "Session Not Found";
	else 
	{
		d = "Service Unavailable";
		rc = 503;
	}
	static char resp[5000];
	desc1 = desc;
	if(!lr)
		lr = strlen (desc);
	else 
	{
		binary = 1;
		desc1 = "";
	}
	
	sess_id[0] = 0;
	scseq[0] = 0;
	if(s->type != TYPE_HTTP && get_sid(s->sid) && ah && !strstr(ah, "Session"))
		sprintf(sess_id, "\r\nSession: %010d", get_session_id(s->sid));
	if(s->type != TYPE_HTTP && cseq > 0)
			sprintf(scseq, "\r\nCseq: %d", cseq);
	
	if (lr)
		sprintf (resp, reply, proto, rc, d, get_current_timestamp (), sess_id, scseq, ah, lr, desc1);
	else
		sprintf (resp, reply0, proto, rc, d, get_current_timestamp (), sess_id, scseq, ah);
	LOG ("reply -> %d (%s:%d) CL:%d :\n%s", s->sock, inet_ntoa(s->sa.sin_addr), ntohs(s->sa.sin_port), lr, resp);
	send (s->sock, resp, strlen (resp), MSG_NOSIGNAL);
	if(binary)
		send (s->sock, desc, lr, MSG_NOSIGNAL);
	return resp;
}
