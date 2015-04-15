/*
 *****************************************************************************
 *                                                                           *
 *                 CS203/468 Integrated RFID Reader                          *
 *                                                                           *
 * This source code is the sole property of Convergence Systems Ltd.         *
 * Reproduction or utilization of this source code in whole or in part is    *
 * forbidden without the prior written consent of Convergence Systems Ltd.   *
 *                                                                           *
 * (c) Copyright Convergence Systems Ltd. 2010. All rights reserved.         *
 *                                                                           *
 *****************************************************************************/

/**
 *****************************************************************************
 **
 ** @file  example.c
 **
 ** @brief Simple example of reader
 **
 **
 ** The steps:
 **     - Connect to the reader (TCP)
 **     - Reader Initialization
 **     - Start Inventory
 **     - Read tag (5s)
 **     - Stop Inventory
 **	- Close
 **  
 *****************************************************************************/


#include <stdio.h>
#include <unistd.h>
#include <string.h>
#include <syslog.h>
#include <termios.h>
#include <fcntl.h>
#include <ifaddrs.h>
#include <sys/types.h>
#include <sys/times.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <net/if.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <signal.h>
#include <wait.h>
#include <sys/poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <pthread.h>

#include "cs203_dc.h"

#define TIMESTART(x)			x=time(NULL)
#define TIMEOUT(x,y) 		((time(NULL)-x)>y?1:0)

#if debug
#define DB(cmd)			cmd2
#else
#define DB(cmd)
#define sylog(cmd)
#endif

#define DEFAULT_CTRLCMDPORT	1516
#define DEFAULT_INTELCMDPORT	1515
#define DEFAULT_UDPPORT		3041
#define DEFAULT_DUPELICOUNT	0

#define DATABUFSIZE 256		// general data buffer size

// UDP command Mode
#if 1
#define BOARDCASEADDRESS	ipv4addr
#else
#define BOARDCASEADDRESS	INADDR_BROADCAST
#endif


int _strHex2ByteArray(char* in, char* out)
{
	char szTmp[3];
	unsigned int i;
	char* endptr;
	
	if(in == NULL || out == NULL)
		return -1;
		
	/* make sure that length of the message is even number */
	if (strlen(in) % 2 !=0)
		return -1;
		
	for (i=0;i<strlen(in)/2;i++)
	{
		memcpy(szTmp,&in[i*2],2);
		szTmp[2]='\0';
		out[i]=strtol(szTmp,&endptr,16);
	}
	
	return i;
}

// compare a hex string with a byte array, limit to len <cmplen> in bytes,
// both string and array should be the same size and longer than <cmplen> or -1 will be returned
// return 0 for both string and byte contains same value, otherwise, return -1
int _cmpHexStr2HexByteArray(char* hexstr, char* hexbytearray, int arraydatasize, int cmplen)
{
	char buf[64];
	int datasize;
	int len;
	int i;
	
	if(arraydatasize <= 0)
		return -1;
		
	datasize = _strHex2ByteArray(hexstr, buf);

	if(datasize <= 0)
		return -1;
	
	if(cmplen > 0)
	{
		if(cmplen <= datasize && cmplen <= arraydatasize)
			len = cmplen;
		else
			return -1;
	}
	else
	{
		if(datasize != arraydatasize)
			return -1;
		else
			len = datasize;
	}

	for(i = 0; i < len; i++)
	{
		if(buf[i] != hexbytearray[i])
			return -1;
	}

	return 0;
}


void printcurtime ()
{
	time_t   timep; 

   timep = time (NULL);    
   printf ("%s", (char *)asctime (localtime (&timep)));
}
/*
*	Send TCP Command
*
*   return number of command response, if <0 : -1 = socket error, -2 = send error, -3 = recv error
*/
int SendTcpCommand (int socket, const void *sendbuffer, size_t sendlength, void *recvbuffer, size_t recvlength)
{
	int nsent, nrecv;
	
	if (!socket || recvlength <= 0)
		return -1;
	
	// Clear TCP Buffers
	do {
		nrecv = recv (socket, recvbuffer, recvlength, 0);	
	} while (nrecv > 0);

	// Send Command
	nsent = send (socket, sendbuffer, sendlength, 0);
	if (nsent != sendlength)
		return -2;

	// Receive Response
	time_t start_time = time (NULL);
	while ((time (NULL) -  start_time) < 10) // 10 seconds timeout
	{
		nrecv = recv (socket, recvbuffer, recvlength, 0);
		if (nrecv > 0)
			return nrecv;
		sleep (1);
	}

	return -3;
}

int CS203DC_Close (CS203DC_Handle* handle)
{
	if(handle == NULL)
	{
		return -2;
	}

	if(handle->intelcmdfd >= 0)
	{
		shutdown(handle->intelcmdfd, SHUT_RDWR);
		close(handle->intelcmdfd);	
		handle->intelcmdfd = -1;
	}

	if(handle->controlcmdfd >= 0)
	{
		shutdown(handle->controlcmdfd, SHUT_RDWR);
		close(handle->controlcmdfd);
		handle->controlcmdfd = -1;	
	}

	if(handle->udpfd >= 0)
	{
		close(handle->udpfd);
		handle->udpfd = -1;	
	}

	handle->state = CS203DCSTATE_IDLE;

	return 0;
}

int CS203DC_OpenConnection (CS203DC_Handle* handle, int intelcmdport, int controlcmdport, int udpport)
{
	int ret = 0;
	struct sockaddr_in inet_addrs, cnet_addrs, unet_addrs;
	unsigned int ipv4addr;

   int optval;
   socklen_t optlen = sizeof(optval);


	if (handle == NULL || handle->ReaderHostName == NULL || intelcmdport < 0 || intelcmdport > 65535 || controlcmdport < 0 || controlcmdport > 65535 || udpport < 0 || udpport > 65535)
		return -2;

	if (handle->state != CS203DCSTATE_IDLE)
		return -2;
	
	if (!gethostbyname2 (handle->ReaderHostName, AF_INET))
	{
		printf ("can not get host by name2 %s", handle->ReaderHostName);
		return -3;
	}
	
	handle->ipv4addr = inet_addr (handle->ReaderHostName);
	ipv4addr = handle->ipv4addr;

// Open Control Command Port
	memset (&cnet_addrs, 0, sizeof (inet_addrs));
	cnet_addrs.sin_family = AF_INET;
	cnet_addrs.sin_addr.s_addr = handle->ipv4addr;
	cnet_addrs.sin_port = htons (controlcmdport);   // set endpoint port #
	handle->controlcmdfd = socket(PF_INET, SOCK_STREAM, 0);
	if (handle->controlcmdfd < 0)                               // error opening socket
	{
		ret = -6;                                 // Return error status
	}
	if (connect(handle->controlcmdfd, (struct sockaddr *) &cnet_addrs, sizeof(cnet_addrs)) < 0)
	{
		ret = -7;                                 // Return error code
	}

	if (ret == 0)
	{
		fcntl (handle->controlcmdfd, F_SETFL, fcntl (handle->controlcmdfd, F_GETFL, 0) | O_NONBLOCK);
		printf ("%s(): line %d, connection to host %s:%d successful\n", __FUNCTION__, __LINE__, handle->ReaderHostName, controlcmdport);
	}
	
// for test linux tcp/ip keep alive function
#if 0
   /* Set the option active */
   optval = 1;
   optlen = sizeof(optval);
   if(setsockopt(handle->controlcmdfd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0) {
      close(handle->controlcmdfd);
      exit(1);
   }
   printf("SO_KEEPALIVE set on socket\n");

   /* Check the status for the keepalive option */
   if(getsockopt(handle->controlcmdfd, SOL_SOCKET, SO_KEEPALIVE, &optval, &optlen) < 0) {
      perror("getsockopt()");
      close(handle->controlcmdfd);
      exit(1);
   }
   printf("SO_KEEPALIVE is %s\n", (optval ? "ON" : "OFF"));	
#endif	

// Open Intel Command Port 
	if(ret == 0)
	{
		memset (&inet_addrs, 0, sizeof (inet_addrs));
		inet_addrs.sin_family = AF_INET;
		inet_addrs.sin_addr.s_addr = handle->ipv4addr;
		inet_addrs.sin_port = htons (intelcmdport);   // set endpoint port #
		handle->intelcmdfd = socket (PF_INET, SOCK_STREAM, 0);
		if (handle->intelcmdfd < 0)
			ret = -4;
		if (connect (handle->intelcmdfd, (struct sockaddr *) &inet_addrs, sizeof(inet_addrs)) < 0)
			ret = -5;                                 // Return error code
	 	if (ret == 0)
	 	{
	 		fcntl (handle->intelcmdfd, F_SETFL, fcntl (handle->intelcmdfd, F_GETFL, 0) | O_NONBLOCK);
			printf ("%s(): line %d, connection to host %s:%d successful\n", __FUNCTION__, __LINE__, handle->ReaderHostName, intelcmdport);
		}

// for test linux tcp/ip keep alive function
   /* Set the option active */
   optval = 1;
   optlen = sizeof(optval);
   if(setsockopt(handle->intelcmdfd, SOL_SOCKET, SO_KEEPALIVE, &optval, optlen) < 0) {
      close(handle->intelcmdfd);
      exit(1);
   }
   printf("SO_KEEPALIVE set on socket\n");

   /* Check the status for the keepalive option */
   if(getsockopt(handle->intelcmdfd, SOL_SOCKET, SO_KEEPALIVE, &optval, &optlen) < 0) {
      perror("getsockopt()");
      close(handle->intelcmdfd);
      exit(1);
   }
   printf("SO_KEEPALIVE is %s\n", (optval ? "ON" : "OFF"));

	}

	// UDP server for listening to CS203 errors
	if(ret == 0)
	{
		memset(&unet_addrs,0,sizeof(unet_addrs));
		unet_addrs.sin_family = AF_INET;
		unet_addrs.sin_port = htons(udpport);   // set endpoint port #
		unet_addrs.sin_addr.s_addr = BOARDCASEADDRESS;
		handle->udpfd = socket(PF_INET, SOCK_DGRAM, 0);
		if (handle->udpfd < 0)                               // error opening socket
		{
			// if error opening socket exit
			syslog(LOG_ERR,"%s(): line %d, create socket failed...exiting", __FUNCTION__, __LINE__);
			ret = -8;                                 // Return error status
		}
		else
		{
    		fcntl (handle->udpfd, F_SETFL, O_NONBLOCK | FASYNC);
    	}
	}

	usleep (1); // Delay for success connection

	if (ret != 0)
		CS203DC_Close (handle);
	
	return ret;
}

// Check CS203
int CS203DC_DisableUDPKeepaLive (unsigned int ipv4addr)
{
	int ret = 0;
	int sockfd;
	struct sockaddr_in to;
	struct sockaddr_in from;
	int nsent;
	int nrecv;
	unsigned char buf1[7];
	unsigned char buf2[4];
	int buf1_datalen;
	int fromlen;
      	int broadcast = 1;

	if (ipv4addr == 0)
		return -2;
		
	sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 	if (sockfd == -1)
 		return -21;
 		
   if ((setsockopt (sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast)) == -1)
 		return -21;

   fcntl (sockfd, F_SETFL, O_NONBLOCK | FASYNC);
    	
	// Disable UDP keep alive Command "/0x80/0xxx/0xxx/0xxx/0xxx/0x01/0x04"
	buf1[0] = 0x80;
	buf1[1] = (char)(ipv4addr & 0xff);
	buf1[2] = (char)((ipv4addr >> 8) & 0xff);
	buf1[3] = (char)((ipv4addr >> 16) & 0xff);
	buf1[4] = (char)((ipv4addr >> 24) & 0xff);
	buf1[5] = 0x01;
	buf1[6] = 0x04;

	// Send UDP Packet
	memset(&to, 0, sizeof(to));
	to.sin_addr.s_addr = BOARDCASEADDRESS;	// UDP broad cast
	to.sin_port = htons (DEFAULT_UDPPORT); // Net Founder Port Number
	to.sin_family = AF_INET;
	nsent = sendto (sockfd, buf1, sizeof (buf1), 0, (const struct sockaddr *)&to, sizeof (to));

	// Recv UDP Packet
	ret = 1;
	fromlen = sizeof (from);
	time_t start_time = time (NULL);
	while ((time (NULL) -  start_time) < 1.0) //Wait 2 second
	{
		nrecv = recvfrom (sockfd, buf2, sizeof (buf2), 0, (struct sockaddr *)&from, &fromlen);	
		if (nrecv == 4 && buf2[0] == 0x81 && buf2[1] == 0x01 && buf2[2] == 0x00 && buf2[3] == 0x04)
		{
			ret = 0;			break;
		}
	}
			
	close (sockfd);

	return ret;
}


int CS203DC_Open (char* hostaddress, int intelcmdport, int controlcmdport, int udpport, CS203DC_Handle* handle)
{
	int retint1, retint2;
	int ret = 0;
	struct sockaddr_in inet_addrs, cnet_addrs, unet_addrs;
	unsigned char buf1[DATABUFSIZE];
	unsigned char buf2[DATABUFSIZE];
	int buf1_datalen = 0;
	int nsent;
	int nrecv;
	int i;
	int rfidboardinited = 0;
	struct  hostent  *hp1 = NULL;
	long 	ipv4addr;


	ret = CS203DC_OpenConnection (handle, intelcmdport, controlcmdport, udpport);
	if (ret != 0)
		return ret;
		
	// network connections are ok at this point

	// initialize CS203
	// check firmware version (control command)
	if(ret == 0)
	{
		buf1_datalen = _strHex2ByteArray(CS203DC_GETVERSION, buf1);
		nrecv = SendTcpCommand (handle->controlcmdfd, buf1, buf1_datalen, buf2, 8);
		if(_cmpHexStr2HexByteArray(CS203DC_GETVERSION_RSP, buf2, nrecv, 4) != 0)
		{
			printf ("%s(): line %d, cannot get firmware version", __FUNCTION__, __LINE__);
			ret = -15;
		}
		if (buf2[4] < 2 || (buf2[4] == 2 && (buf2[5] < 18 || (buf2[5] == 18 && buf2[6] < 8))))
		{
			printf ("%s(): line %d, firmware version too old", __FUNCTION__, __LINE__);
			ret = -15;
		}
	}

	// turn off RFID board (control command)
	/*
	if(ret == 0)
	{
		buf1_datalen = _strHex2ByteArray(CS203DC_TURNOFFRFID, buf1);
		nrecv = SendTcpCommand (handle->controlcmdfd, buf1, buf1_datalen, buf2, 5);
		if(_cmpHexStr2HexByteArray(CS203DC_TURNOFFRFID_RSP, buf2, nrecv, -1) != 0)
		{
			syslog(LOG_ERR,"%s(): line %d, cannot turn off RFID board", __FUNCTION__, __LINE__);
			ret = -20;
		}
	}
	*/
	// turn on RFID board (control command)
	if(ret == 0)
	{
		buf1_datalen = _strHex2ByteArray(CS203DC_TURNONRFID, buf1);
		nrecv = SendTcpCommand (handle->controlcmdfd, buf1, buf1_datalen, buf2, 5);
		if(_cmpHexStr2HexByteArray(CS203DC_TURNONRFID_RSP, buf2, nrecv, -1) != 0)
		{
			syslog(LOG_ERR,"%s(): line %d, cannot turn on RFID board", __FUNCTION__, __LINE__);
			ret = -8;
		}

		int cnt;
		for (cnt = 0; cnt<6; cnt++)
			usleep(2000000);
	}

	// enable TCP notification mode (control command)
	if(ret == 0)
	{
		buf1_datalen = _strHex2ByteArray(CS203DC_ENABLETCPNOTIFY, buf1);
		nrecv = SendTcpCommand (handle->controlcmdfd, buf1, buf1_datalen, buf2, 5);
		if(_cmpHexStr2HexByteArray(CS203DC_ENABLETCPNOTIFY_RSP, buf2, nrecv, -1) != 0)
		{
			syslog(LOG_INFO,"%s(): line %d, cannot enable TCP notification mode", __FUNCTION__, __LINE__);
			ret = -14; // may failed at this point if not wait 10-12 second after disconnect from CS230
		}
	}

	// control command is done at this point
	
	// sending intel commands
	// retrieve attached radio list (intel command), to check if the board is turn on


	// open radio (intel commands)
	if(ret == 0)
	{
		buf1_datalen = _strHex2ByteArray(CS203DC_ABORTCMD, buf1);
		nrecv = SendTcpCommand (handle->intelcmdfd, buf1, buf1_datalen, buf2, 8);
		if(_cmpHexStr2HexByteArray(CS203DC_ABORTCMD_RSP, buf2, nrecv, -1) != 0)
		{
			syslog(LOG_ERR,"%s(): line %d, cannot abort radio", __FUNCTION__, __LINE__);
			ret = -12;
		}
		else
		{
			if(_sendintelcmd(handle, CS203DC_STARTINVENTORY01) < 0)
			{
				syslog(LOG_ERR,"%s(): line %d, cannot send HST_ANT_CYCLES", __FUNCTION__, __LINE__);
				ret = -13;
			}

			if(_sendintelcmd(handle, CS203DC_HSTANTCYCLES) < 0)
			{
				ret = -13;
			}
		}
	}
	/*
	// set channel (intel commands)
	if (handle->Freq_CN)
		SetFreq_CN (handle, handle->Freq_CN);
	*/
	// check status and do necessary clean up
	if (ret != 0)
	{
		if(handle->intelcmdfd >= 0)
		{
			if(rfidboardinited != 0)
			{
				// sent abort radio command anyway
				buf1_datalen = _strHex2ByteArray(CS203DC_ABORTCMD, buf1);
				nrecv = SendTcpCommand (handle->intelcmdfd, buf1, buf1_datalen, buf2, 8);
				printf ("ABORTCMD ");
				if(_cmpHexStr2HexByteArray(CS203DC_ABORTCMD_RSP, buf2, nrecv, -1) != 0)
					printf ("Fail\n");
				else
					printf ("OK\n");

				CS203DC_Close (handle);
			}
		}
	}
	else
	{
		handle->state = CS203DCSTATE_CONNECTED;
		syslog(LOG_INFO,"%s(): line %d, cs203 opened", __FUNCTION__, __LINE__);
	}

	return ret;
}

int _sendintelcmd(CS203DC_Handle* handle, char* strdata)
{
	usleep(1000);	//just to ensure we have at least 1ms of delay
	unsigned char buf1[DATABUFSIZE];
	int buf1_datalen = 0;
	int nsent;
	int i;

	for(i = 0; i < 10; i++)
	{
		DB(syslog(LOG_INFO,"%s(): line %d, try %d", __FUNCTION__, __LINE__, i));
		buf1_datalen = _strHex2ByteArray(strdata, buf1);
		nsent = send(handle->intelcmdfd, buf1, buf1_datalen, 0);
		usleep (10);
		DB(syslog(LOG_INFO,"%s(): line %d, command %d bytes, sent %d", __FUNCTION__, __LINE__, buf1_datalen, nsent));
		if(nsent < 0)
		{
			if(errno != EAGAIN)
			{
				return errno;
			}
		}
		else
		{
			// assume always sent all data
			break;
		}
	}
	return nsent;
}
int _readintelrsp(CS203DC_Handle* handle, char* buf, int len)
{
	int nrecv;
	int i;

	for(i = 0; i < 10; i++)
	{
		DB(syslog(LOG_INFO,"%s(): line %d, try %d", __FUNCTION__, __LINE__, i));
		usleep(1000); // sleep 1ms to wait for data
		nrecv = recv(handle->intelcmdfd, buf, len, MSG_PEEK); // do not want to block here, peek first
		if(nrecv < 0)
		{
			if(errno != EAGAIN)
			{
				return errno;
			}
		}
		else
		{
			if(nrecv < len)
			{
				continue;
			}
			else
			{
				nrecv = recv(handle->intelcmdfd, buf, len, 0); // consume the data
			}
			break;
		}
	}
	return nrecv;
}

int CS203DC_StartInventory(CS203DC_Handle* handle)
{
	int err;
	
	if(handle == NULL)
	{
		return -2;
	}

	if(handle->state != CS203DCSTATE_CONNECTED)
	{
		return -2;
	}

	fcntl(handle->intelcmdfd, F_SETFL, fcntl(handle->intelcmdfd,F_GETFL,0)&(~O_NONBLOCK));
	/*
	if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY01)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY01, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY02)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY02, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY03)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY03, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY04)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY04, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else
	{
		int nread = 0;
		unsigned char buf[8];
		// response, 8-byte, no need to check the content
		nread = recv(handle->intelcmdfd, buf, 8, 0);
		if(nread != 8)
		{
		        syslog(LOG_ERR,"%s(): line %d, response error %d", __FUNCTION__, __LINE__, errno);
			return errno;
		}
		DB(syslog(LOG_INFO,"%s(): line %d, respond 0x%02X%02X%02X%02X%02X%02X%02X%02X", __FUNCTION__, __LINE__, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]));
	}
	if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY05)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY05, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY09)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY09, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY10)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY10, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else
	{
		int nread = 0;
		unsigned char buf[8];
		// response, 8-byte, no need to check the content
		nread = recv(handle->intelcmdfd, buf, 8, 0);
		if(nread != 8)
		{
		        DB(syslog(LOG_INFO,"%s(): line %d, response error %d", __FUNCTION__, __LINE__, errno));
			return errno;
		}
		DB(syslog(LOG_INFO,"%s(): line %d, respond 0x%02X%02X%02X%02X%02X%02X%02X%02X", __FUNCTION__, __LINE__, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]));
	}
	*/
	SetRadioSpecialLBTForJapan (handle, handle->iConf_lbt);

	ChannelSel (handle, handle->iConf_channel);

	if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY14)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY14, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
/*	else
	{
		int nread = 0;
		unsigned char buf[16];
		// response, 16-byte
		nread = recv(handle->intelcmdfd, buf, 16, 0);
		if(nread != 16)
		{
		        syslog(LOG_ERR,"%s(): line %d, response error %d", __FUNCTION__, __LINE__, errno);
			return errno;
		}
		syslog(LOG_INFO,"%s(): line %d, respond 0x%02X%02X%02X%02X%02X%02X%02X%02X", __FUNCTION__, __LINE__, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]);
		syslog(LOG_INFO,"%s(): line %d, respond 0x%02X%02X%02X%02X%02X%02X%02X%02X", __FUNCTION__, __LINE__, buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]);
		if(_cmpHexStr2HexByteArray(CS203DC_COMMANDBEGIN_RSP, buf, nread, 8) != 0)
		{
			syslog(LOG_ERR,"%s(): line %d, command begin failed", __FUNCTION__, __LINE__);
			return -2;
		}
	}
*/
	
	// initialized status
	DB(syslog(LOG_INFO,"%s(): line %d, reset buffer (%d)", __FUNCTION__, __LINE__, sizeof(handle->tag)));
	handle->tagbeginidx = 0;
	memset(handle->tag, 0, sizeof(handle->tag));	
	handle->state = CS203DCSTATE_INV;
	fcntl(handle->intelcmdfd, F_SETFL, fcntl(handle->intelcmdfd,F_GETFL,0)|O_NONBLOCK);
	
	return 0;
}

/// <summary>
/// Read Register
/// </summary>
int ReadRegister (CS203DC_Handle* handle, unsigned int add, unsigned int *value)
{
	char CMD[100];
	unsigned char recvbuf[100];
	int nread = 0;

	sprintf (CMD, "7000%02x%02x00000000", 
		(unsigned char)add,
        	(unsigned char)(add >> 8));

	_sendintelcmd(handle, CMD);

	nread = recv(handle->intelcmdfd, recvbuf, 8, 0);

	if(nread != 8)
	{
		printf ("%s() line %d, response error %d", __FUNCTION__, __LINE__, errno);
		return 0;
	}

       	*value = (unsigned int)(recvbuf[7] << 24 | recvbuf[6] << 16 | recvbuf[5] << 8 | recvbuf[4]);
	return 1;
}

/// <summary>
/// Write Register
/// </summary>
int WriteRegister(CS203DC_Handle* handle, unsigned int add, unsigned int value)
{
	char CMD[100];

	sprintf (CMD, "7001%02x%02x%02x%02x%02x%02x", 
		(unsigned char)add,
        	(unsigned char)(add >> 8),
		(unsigned char)value,
		(unsigned char)(value >> 8),
		(unsigned char)(value >> 16),
		(unsigned char)(value >> 24));

	_sendintelcmd(handle, CMD);

	return 1;
}

int SetRadioSpecialLBTForJapan (CS203DC_Handle* handle, int Enable)
{
	uint temp = 0;

	//set LBT mode
       	if (WriteRegister(handle, 0x300 /*MacRegister.HST_PROTSCH_SMIDX*/, 1) == 0)
       		return 0;

      	if (WriteRegister(handle, 0x304 /*MacRegister.HST_PROTSCH_SMCFG_SEL*/, 1) == 0)
      		return 0;

	if (ReadRegister(handle, 0x301 /*MacRegister.HST_PROTSCH_SMCFG*/, &temp) == 0)
        	return 0;

	temp &= 0xFFFFFFFC;
	if (Enable != 0)
        	temp |= 0x01;

        if (WriteRegister(handle, 0x301 /*MacRegister.HST_PROTSCH_SMCFG*/, temp) == 0)
		return 0;

        //set LBT off time
        if (WriteRegister(handle, 0x305 /*MacRegister.HST_PROTSCH_TXTIME_SEL*/, 1) == 0)
                return 0;

        if (WriteRegister(handle, 0x307 /*MacRegister.HST_PROTSCH_TXTIME_OFF*/, 0x3c) == 0) //measured value for 60ms
        	return 0;

	return 1;
}

int ChannelSel (CS203DC_Handle* handle, int channel)
{
	int i;

	for (i = 0; i < 14; i++)
	{
		WriteRegister (handle, 0xc01, i);
   		WriteRegister (handle, 0xc02, 0);
	}

	WriteRegister (handle, 0xc01, channel - 1);
    	WriteRegister (handle, 0xc02, 1);
}



/*
        /// <summary>
        /// Set Frequency Band - Basic Function
        /// </summary>
        /// <param name="frequencySelector"></param>
        /// <param name="config"></param>
        /// <param name="multdiv"></param>
        /// <param name="pllcc"></param>
        /// <returns></returns>
        private Result SetFrequencyBand
            (
                UInt32 frequencySelector,
                BandState config,
                UInt32 multdiv,
                UInt32 pllcc
            )
        {
            m_result = MacWriteRegister(MacRegister.HST_RFTC_FRQCH_SEL, frequencySelector);
            if (m_result != Result.OK) return m_result;

            m_result = MacWriteRegister(MacRegister.HST_RFTC_FRQCH_CFG, (uint)config);
            if (m_result != Result.OK) return m_result;

            if (config == BandState.ENABLE)
            {

                m_result = MacWriteRegister(MacRegister.HST_RFTC_FRQCH_DESC_PLLDIVMULT, multdiv);
                if (m_result != Result.OK) return m_result;

                m_result = MacWriteRegister(MacRegister.HST_RFTC_FRQCH_DESC_PLLDACCTL, pllcc);
                if (m_result != Result.OK) return m_result;
            }

            return m_result;
        }
*/


int CS203DC_StopInventory(CS203DC_Handle* handle)
{
	unsigned char buf1[DATABUFSIZE];
	int buf1_datalen = 0;
	int nsent;
	int i;

	if(handle == NULL)
	{
		return -2;
	}

	if(handle->state == CS203DCSTATE_IDLE)
	{
		return -2;
	}

	for(i = 0; i < 10; i++)
	{
		usleep(10000);
		DB(syslog(LOG_INFO,"%s(): line %d, abort command, try %d", __FUNCTION__, __LINE__, i));
		buf1_datalen = _strHex2ByteArray(CS203DC_ABORTCMD, buf1);
		nsent = send(handle->intelcmdfd, buf1, buf1_datalen, 0);
		DB(syslog(LOG_INFO,"%s(): line %d, command %d bytes, sent %d", __FUNCTION__, __LINE__, buf1_datalen, nsent));
		if(nsent < 0)
		{
			if(errno != EAGAIN)
			{
				return errno;
			}
		}
		else
		{
			break;
		}
	}
	
	handle->state = CS203DCSTATE_CONNECTED;
	
	// Clean Read Tag Buffer
	for (i = 0; i < 100; i++)
		usleep (10000);

	return 0;
}

int CS203DC_RestartInventory(CS203DC_Handle* handle)
{
	int err;
	
	if(handle == NULL)
	{
		return -2;
	}

	if(handle->state != CS203DCSTATE_CONNECTED)
	{
		return -2;
	}

	fcntl(handle->intelcmdfd, F_SETFL, fcntl(handle->intelcmdfd,F_GETFL,0)&(~O_NONBLOCK));
	// usually have a CS203DC_StopInventory() is called before, consume the command abort response first
	{
		int nread = 0;
		char buf[8];
		// response, 8-byte, no need to check the content
		nread = recv(handle->intelcmdfd, buf, 8, 0);
		if(nread != 8)
		{
		        syslog(LOG_ERR,"%s(): line %d, response error %d", __FUNCTION__, __LINE__, errno);
			return errno;
		}
		DB(syslog(LOG_INFO,"%s(): line %d, respond 0x%02X%02X%02X%02X%02X%02X%02X%02X", __FUNCTION__, __LINE__, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]));
	}
//	usleep(100000);
	if((err = _sendintelcmd(handle, CS203DC_STARTINVENTORY14)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send STARTINVENTORY14, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else
	{
		int nread = 0;
		char buf[16];
		// response, 16-byte
		nread = recv(handle->intelcmdfd, buf, 16, 0);
		if(nread != 16)
		{
		        syslog(LOG_ERR,"%s(): line %d, response error, read: %d, errno: %d", __FUNCTION__, __LINE__, nread, errno);
			if(nread > 0)
			{
				int i;
				for(i = 0; i < nread; i++)
				{
		        		syslog(LOG_ERR,"%s(): line %d, (%d) 0x%02X", __FUNCTION__, __LINE__, i, buf[i]);
				}
			}
			return errno;
		}
		DB(syslog(LOG_INFO,"%s(): line %d, respond 0x%02X%02X%02X%02X%02X%02X%02X%02X", __FUNCTION__, __LINE__, buf[0], buf[1], buf[2], buf[3], buf[4], buf[5], buf[6], buf[7]));
		DB(syslog(LOG_INFO,"%s(): line %d, respond 0x%02X%02X%02X%02X%02X%02X%02X%02X", __FUNCTION__, __LINE__, buf[8], buf[9], buf[10], buf[11], buf[12], buf[13], buf[14], buf[15]));
		if(_cmpHexStr2HexByteArray(CS203DC_COMMANDBEGIN_RSP, buf, nread, 8) != 0)
		{
			syslog(LOG_ERR,"%s(): line %d, command begin failed", __FUNCTION__, __LINE__);
			return -2;
		}

	}
	
	// initialized status
	DB(syslog(LOG_INFO,"%s(): line %d, reset buffer (%d)", __FUNCTION__, __LINE__, sizeof(handle->tag)));
	handle->tagbeginidx = 0;
	memset(handle->tag, 0, sizeof(handle->tag));	
	handle->state = CS203DCSTATE_INV;
	fcntl(handle->intelcmdfd, F_SETFL, fcntl(handle->intelcmdfd,F_GETFL,0)|O_NONBLOCK);
	
	return 0;
}


char tzTagID [32];

/*
		Read TID
*/
int CS203DC_ReadTid (CS203DC_Handle *handle, char *tzTagNumber)
{
	unsigned char sPacketCommand[32];
	char cutbuf[9];
	unsigned char sRecvBuffer[1024];
	int nread;
	int MachineState;
	int bufstart = 0;
	
	printf ("Read TID data from Tag %s\n", tzTagNumber);
	
	// Clear Network Buffer
	do {
		nread = recv (handle->intelcmdfd, sRecvBuffer, sizeof (sRecvBuffer), 0);	
	} while (nread > 0);
	
	// Step 1: Set Inventory Parameters

	sprintf(sPacketCommand, "7001000701000000"); // ANT_CYCLES
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001000980010000"); // RUERY_CFG
	_sendintelcmd(handle, sPacketCommand);


	// Step 2: Set Inventory Algorithm. (Assume only 1 tag in front of reader and Fixed Q = 0)

	sprintf(sPacketCommand, "7001020900000000"); // INV_SEL
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030900000000"); // INV_ALG_PARM_0
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001050900000000"); // INV_ALG_PARM_2
	_sendintelcmd(handle, sPacketCommand);


	// Step 3: Select the desired tag.

	sprintf(sPacketCommand, "7001010809000000"); // TAGMSG_DESC_CFG
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001020801000000"); // TAGMSG_BANK
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030820000000"); // TAGMSG_PTR
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001040860000000"); // TAGMSG_LEN
	_sendintelcmd(handle, sPacketCommand);

	cutbuf[8] = 0;

	memcpy (cutbuf, tzTagNumber, 8);
	sprintf(sPacketCommand, "70010508%s", cutbuf); // TAGMSG_0_3
	_sendintelcmd(handle, sPacketCommand);

	memcpy (cutbuf, &tzTagNumber[8], 8);
	sprintf(sPacketCommand, "70010608%s", cutbuf); // TAGMSG_4_7
	_sendintelcmd(handle, sPacketCommand);

	memcpy (cutbuf, &tzTagNumber[16], 8);
	sprintf(sPacketCommand, "70010708%s", cutbuf); // TAGMSG_8_11
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001010940400000"); // INV_CFG
	_sendintelcmd(handle, sPacketCommand);


	// Step 4: Start reading the tag data.

	sprintf(sPacketCommand, "7001020A02000000");	// TAGACC_BANK
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030A00000000");	// TAGACC_PTR
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001040A02000000");	// TAGACC_CNT
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001060A00000000");	// TAGACC_ACCPWD
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "700100F010000000");	// HST_CMD
	_sendintelcmd(handle, sPacketCommand);

	MachineState = 0; // 0 = Init Mode, 1 = Command Mode
	
	time_t tStartTime;

	TIMESTART (tStartTime); // Reset Timeout
	while (1)
	{
		unsigned char *pRecvBuffer = sRecvBuffer;
		int i;


		nread = recv(handle->intelcmdfd, &sRecvBuffer[bufstart], sizeof (sRecvBuffer) - bufstart, 0);

		if (nread <= 0)
		{
			if (TIMEOUT (tStartTime, 1))
				break;
			else
				continue;
		}
		TIMESTART (tStartTime); // Reset Timeout

#if 0
		printf ("Receive Data LEN = %d : Remind Data Len = %d\n", nread, bufstart);
#endif

		nread += bufstart;

#if 0
		for (i = 0; i < nread; i++)
			printf ("%02x", sRecvBuffer[i]);
		printf ("\n");
#endif

		while (nread > 0)
		{
			int Packetversion = pRecvBuffer[0];
			int PacketType = (int)(pRecvBuffer[3] << 8 | pRecvBuffer[2]);
			int PacketLength = 8 + ((int)(pRecvBuffer[5] << 8 | pRecvBuffer[4])) * 4;

			// Handle Keep-a-live packet
			if (memcmp (pRecvBuffer, "\x98\x98\x98\x98\x98\x98\x98\x98", 8) == 0)
			{
				printf ("Notification Packet Received! ");
				pRecvBuffer += 8;
				nread -= 8;
				continue;
			}

			if (PacketLength > nread)
			{
				memcpy (sRecvBuffer, pRecvBuffer, nread);
				nread = 0;
				continue;
			}

			switch (PacketType)
			{
				case 0x8000: // Command-Begin Packet
					printf ("Receive Command Begin Packet\n");					
					if (Packetversion != 0x02)
					{
						printf ("Can not handle version %d Command-Begin Packet\n", Packetversion);
						break;
					}

					if (MachineState != 0)
						printf ("Machine State not in idle mode\n");

					MachineState = 1; // Set to Command mode
					printf ("Command %02x%02x%02x%02x\n", pRecvBuffer[11], pRecvBuffer[10], pRecvBuffer[9], pRecvBuffer[8]);
					
					break;
					

				case 0x8001: // Command-End Packet
					printf ("Receive Command End Packet\n");					
					if (Packetversion != 0x02)
					{
						printf ("Can not handle version %d Command-End Packet\n", Packetversion);
						break;
					}

					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					MachineState = 0; // Set to idle mode
					printf ("Status %02x%02x%02x%02x\n", pRecvBuffer[15], pRecvBuffer[14], pRecvBuffer[13], pRecvBuffer[12]);
					break;


				case 0x8005: // Inventory-Response Packet
					printf ("Receive Inventory Response Packet\n");
					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					if (pRecvBuffer[1] & 0x01)
					{
						printf ("CRC Error!!!\n");
						break;
					}
					
					printf ("RSSI = %d\n", (int)(pRecvBuffer[13] << 8 | pRecvBuffer[12])); 
					printf ("Inventory Data : ");
					for (i = 20; i < PacketLength; i++)
						printf ("%02x", pRecvBuffer[i]);
					printf ("\n");

					break;
					
				case 0x0006: // Tag-Access Packet
					printf ("Receive Tag Access Packet\n");
					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					if (pRecvBuffer[1] & 0x01)
					{
						printf ("An error occurred!!!\n");
						break;
					}

					printf ("Command = %x\n", pRecvBuffer[12]);
					printf ("Data : ");
					for (i = 20; i < PacketLength; i++)
						printf ("%02x", pRecvBuffer[i]);
					printf ("\n");

					break;
					
					
				default:
					printf ("Receive Error Packet Type : 0x%04x\n", PacketType);
					break;
			}

			pRecvBuffer += PacketLength;
			nread -= PacketLength;
			bufstart = nread;
		}		
	}
}

/*
INT SetGen2 ()
{
	// Set FixedQ Q=2
	sprintf(sPacketCommand, "7001020900000000"); // INV_SEL
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030900000000"); // INV_ALG_PARM_0
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001050902000000"); // INV_ALG_PARM_2
	_sendintelcmd(handle, sPacketCommand);

	// Set Session = S0

	// Set Profile = 2

	// Set Power = 300
}
*/

int SetFreq_None (CS203DC_Handle *handle)
{
	char sPacketCommand[50];
	int i;
	
	for (i = 4; i < 50; i++)
	{
		sprintf(sPacketCommand, "7001010C%02X000000", i); // Select Channel
		_sendintelcmd(handle, sPacketCommand);
	
		sprintf(sPacketCommand, "7001020C00000000"); // Disable Channel
		_sendintelcmd(handle, sPacketCommand);	
	}
}

int SetFreq_CN (CS203DC_Handle *handle, int Bank)
{
	char sPacketCommand[50];
	
	SetFreq_None (handle);

//CN1 = 920.625, 920.875, 921.125, 921.375 MHz
//CN2 = 921.625, 921.875, 922.125, 922.375 MHz
//CN3 = 922.625, 922.875, 923.125, 923.375 Mhz
//CN4 = 923.625, 923.875, 924.125, 924.375 Mhz

	switch (Bank)
	{
//CN1 = 920.625, 920.875, 921.125, 921.375 MHz
		case 1:
			sprintf(sPacketCommand, "7001010C00000000"); // Select Channel 0
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CC51C3000"); // Set Freq 920.625, 0x00301CC5
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C01000000"); // Select Channel 1
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CC71C3000"); // Set Freq 920.875, 0x00301CC7
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C02000000"); // Select Channel 2
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CC91C3000"); // Set Freq 921.125, 0x00301CC9
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C03000000"); // Select Channel 3
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CCB1C3000"); // Set Freq 921.375, 0x00301CCB
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);
			break;
			
//CN2 = 921.625, 921.875, 922.125, 922.375 MHz
		case 2:
			sprintf(sPacketCommand, "7001010C00000000"); // Select Channel 0
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CCD1C3000"); // Set Freq 921.625, 0x00301CCD
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C01000000"); // Select Channel 1
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CCF1C3000"); // Set Freq 921.875, 0x00301CCF
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C02000000"); // Select Channel 2
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CD11C3000"); // Set Freq 922.125, 0x00301CD1
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C03000000"); // Select Channel 3
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CD31C3000"); // Set Freq 922.375, 0x00301CD3
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);
			break;

//CN3 = 922.625, 922.875, 923.125, 923.375 Mhz
		case 3:
			sprintf(sPacketCommand, "7001010C00000000"); // Select Channel 0
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CD51C3000"); // Set Freq 922.625, 0x00301CD5
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C01000000"); // Select Channel 1
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CD71C3000"); // Set Freq 922.875, 0x00301CD7
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C02000000"); // Select Channel 2
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CD91C3000"); // Set Freq 923.125, 0x00301CD9
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C03000000"); // Select Channel 3
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CDB1C3000"); // Set Freq 923.375, 0x00301CDB
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);
			break;

//CN4 = 923.625, 923.875, 924.125, 924.375 Mhz
		case 4:
			sprintf(sPacketCommand, "7001010C00000000"); // Select Channel 0
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CDD1C3000"); // Set Freq 923.625, 0x00301CDD
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C01000000"); // Select Channel 1
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CDF1C3000"); // Set Freq 923.875, 0x00301CDF
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C02000000"); // Select Channel 2
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CE11C3000"); // Set Freq 924.125, 0x00301CE1
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);

			sprintf(sPacketCommand, "7001010C03000000"); // Select Channel 3
			_sendintelcmd(handle, sPacketCommand);	
			sprintf(sPacketCommand, "7001030CE31C3000"); // Set Freq 924.375, 0x00301CE3
			_sendintelcmd(handle, sPacketCommand);
			sprintf(sPacketCommand, "7001020C01000000"); // Enable Channel
			_sendintelcmd(handle, sPacketCommand);
			break;

		default:
			printf ("Slecet Freq Bank Error\n");
			return 0;
	}
	
	return 1;
}



/*
		Read TID
*/
int CS203DC_ReadUserData (CS203DC_Handle *handle, char *tzTagNumber)
{
	unsigned char sPacketCommand[32];
	char cutbuf[9];
	unsigned char sRecvBuffer[1024];
	int nread;
	int MachineState;
	int bufstart = 0;
	
	printf ("Read User Data(Bank 3) from Tag %s\n", tzTagNumber);
	
	// Clear Network Buffer
	do {
		nread = recv (handle->intelcmdfd, sRecvBuffer, sizeof (sRecvBuffer), 0);	
	} while (nread > 0);
	
	// Step 1: Set Inventory Parameters

	sprintf(sPacketCommand, "7001000701000000"); // ANT_CYCLES
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001000980010000"); // RUERY_CFG
	_sendintelcmd(handle, sPacketCommand);


	// Step 2: Set Inventory Algorithm. (Assume only 1 tag in front of reader and Fixed Q = 0)

	sprintf(sPacketCommand, "7001020900000000"); // INV_SEL
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030900000000"); // INV_ALG_PARM_0
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001050900000000"); // INV_ALG_PARM_2
	_sendintelcmd(handle, sPacketCommand);


	// Step 3: Select the desired tag.

	sprintf(sPacketCommand, "7001010809000000"); // TAGMSG_DESC_CFG
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001020801000000"); // TAGMSG_BANK
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030820000000"); // TAGMSG_PTR
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001040860000000"); // TAGMSG_LEN
	_sendintelcmd(handle, sPacketCommand);

	cutbuf[8] = 0;

	memcpy (cutbuf, tzTagNumber, 8);
	sprintf(sPacketCommand, "70010508%s", cutbuf); // TAGMSG_0_3
	_sendintelcmd(handle, sPacketCommand);

	memcpy (cutbuf, &tzTagNumber[8], 8);
	sprintf(sPacketCommand, "70010608%s", cutbuf); // TAGMSG_4_7
	_sendintelcmd(handle, sPacketCommand);

	memcpy (cutbuf, &tzTagNumber[16], 8);
	sprintf(sPacketCommand, "70010708%s", cutbuf); // TAGMSG_8_11
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001010940400000"); // INV_CFG
	_sendintelcmd(handle, sPacketCommand);


	// Step 4: Start reading the tag data.
	sprintf(sPacketCommand, "7001020A03000000");	// TAGACC_BANK (User Data)
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030A00000000");	// TAGACC_PTR
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001040A02000000");	// TAGACC_CNT
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001060A00000000");	// TAGACC_ACCPWD
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "700100F010000000");	// HST_CMD
	_sendintelcmd(handle, sPacketCommand);

	MachineState = 0; // 0 = Init Mode, 1 = Command Mode
	
	time_t tStartTime;

	TIMESTART (tStartTime); // Reset Timeout
	while (1)
	{
		unsigned char *pRecvBuffer = sRecvBuffer;
		int i;


		nread = recv(handle->intelcmdfd, &sRecvBuffer[bufstart], sizeof (sRecvBuffer) - bufstart, 0);

		if (nread <= 0)
		{
			if (TIMEOUT (tStartTime, 1))
				break;
			else
				continue;
		}
		TIMESTART (tStartTime); // Reset Timeout

#if 0
		printf ("Receive Data LEN = %d : Remind Data Len = %d\n", nread, bufstart);
#endif

		nread += bufstart;

#if 0
		for (i = 0; i < nread; i++)
			printf ("%02x", sRecvBuffer[i]);
		printf ("\n");
#endif

		while (nread > 0)
		{
			int Packetversion = pRecvBuffer[0];
			int PacketType = (int)(pRecvBuffer[3] << 8 | pRecvBuffer[2]);
			int PacketLength = 8 + ((int)(pRecvBuffer[5] << 8 | pRecvBuffer[4])) * 4;

			// Handle Keep-a-live packet
			if (memcmp (pRecvBuffer, "\x98\x98\x98\x98\x98\x98\x98\x98", 8) == 0)
			{
				printf ("Notification Packet Received! ");
				pRecvBuffer += 8;
				nread -= 8;
				continue;
			}

			if (PacketLength > nread)
			{
				memcpy (sRecvBuffer, pRecvBuffer, nread);
				nread = 0;
				continue;
			}

			switch (PacketType)
			{
				case 0x8000: // Command-Begin Packet
					printf ("Receive Command Begin Packet\n");					
					if (Packetversion != 0x02)
					{
						printf ("Can not handle version %d Command-Begin Packet\n", Packetversion);
						break;
					}

					if (MachineState != 0)
						printf ("Machine State not in idle mode\n");

					MachineState = 1; // Set to Command mode
					printf ("Command %02x%02x%02x%02x\n", pRecvBuffer[11], pRecvBuffer[10], pRecvBuffer[9], pRecvBuffer[8]);
					
					break;


				case 0x8001: // Command-End Packet
					printf ("Receive Command End Packet\n");					
					if (Packetversion != 0x02)
					{
						printf ("Can not handle version %d Command-End Packet\n", Packetversion);
						break;
					}

					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					MachineState = 0; // Set to idle mode
					printf ("Status %02x%02x%02x%02x\n", pRecvBuffer[15], pRecvBuffer[14], pRecvBuffer[13], pRecvBuffer[12]);
					break;


				case 0x8005: // Inventory-Response Packet
					printf ("Receive Inventory Response Packet\n");
					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					if (pRecvBuffer[1] & 0x01)
					{
						printf ("CRC Error!!!\n");
						break;
					}
					
					printf ("RSSI = %d\n", (int)(pRecvBuffer[13] << 8 | pRecvBuffer[12])); 
					printf ("Inventory Data : ");
					for (i = 20; i < PacketLength; i++)
						printf ("%02x", pRecvBuffer[i]);
					printf ("\n");

					break;
					

				case 0x0006: // Tag-Access Packet
					printf ("Receive Tag Access Packet\n");
					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					printf ("Tag-Access Packet Length = %d\n", PacketLength);

					if (pRecvBuffer[1] & 0x01)
					{
						printf ("An error occurred!!!\n");
						break;
					}

					printf ("Command = %x\n", pRecvBuffer[12]);
					printf ("Data : ");
					for (i = 20; i < PacketLength; i++)
						printf ("%02x", pRecvBuffer[i]);
					printf ("\n");

					break;
					
					
				default:
					printf ("Receive Error Packet Type : 0x%04x\n", PacketType);
					break;
			}

			pRecvBuffer += PacketLength;
			nread -= PacketLength;
			bufstart = nread;
		}		
	}
}

/*
		Write User Data
*/
int CS203DC_WriteEPCData (CS203DC_Handle *handle, char *tzTagNumber)
{
	unsigned char sPacketCommand[32];
	char cutbuf[9];
	unsigned char sRecvBuffer[1024];
	int nread;
	int MachineState;
	int bufstart = 0;
	
	printf ("Write user data to Tag %s\n", tzTagNumber);
	
	// Clear Network Buffer
	do {
		nread = recv (handle->intelcmdfd, sRecvBuffer, sizeof (sRecvBuffer), 0);	
	} while (nread > 0);
	
	// Step 1: Set Inventory Parameters

	sprintf(sPacketCommand, "7001000701000000"); // ANT_CYCLES
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001000980010000"); // RUERY_CFG
	_sendintelcmd(handle, sPacketCommand);


	// Step 2: Set Inventory Algorithm. (Assume only 1 tag in front of reader and Fixed Q = 0)

	sprintf(sPacketCommand, "7001020900000000"); // INV_SEL
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030900000000"); // INV_ALG_PARM_0
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001050900000000"); // INV_ALG_PARM_2
	_sendintelcmd(handle, sPacketCommand);


	// Step 3: Select the desired tag.

	sprintf(sPacketCommand, "7001010809000000"); // TAGMSG_DESC_CFG
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001020801000000"); // TAGMSG_BANK
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030820000000"); // TAGMSG_PTR
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001040860000000"); // TAGMSG_LEN
	_sendintelcmd(handle, sPacketCommand);

	cutbuf[8] = 0;

	memcpy (cutbuf, tzTagNumber, 8);
	sprintf(sPacketCommand, "70010508%s", cutbuf); // TAGMSG_0_3
	_sendintelcmd(handle, sPacketCommand);

	memcpy (cutbuf, &tzTagNumber[8], 8);
	sprintf(sPacketCommand, "70010608%s", cutbuf); // TAGMSG_4_7
	_sendintelcmd(handle, sPacketCommand);

	memcpy (cutbuf, &tzTagNumber[16], 8);
	sprintf(sPacketCommand, "70010708%s", cutbuf); // TAGMSG_8_11
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001010940400000"); // INV_CFG
	_sendintelcmd(handle, sPacketCommand);


	// Step 4: Start writing tag USER data.

	sprintf(sPacketCommand, "7001010A0F000000");	// TAGACC_DESC_CFG
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001020A01000000");	// TAGACC_BANK
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001030A00000000");	// TAGACC_PTR
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001040A06000000");	// TAGACC_CNT
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001060A00000000");	// TAGACC_ACCPWD
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "7001090A22110200");	// TAGWRDAT_0
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "70010A0A44330300");	// TAGWRDAT_1
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "70010B0A66550400");	// TAGWRDAT_2
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "70010C0A88770500");	// TAGWRDAT_3
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "70010D0A00990600");	// TAGWRDAT_4
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "70010E0A22110700");	// TAGWRDAT_5
	_sendintelcmd(handle, sPacketCommand);

	sprintf(sPacketCommand, "700100F011000000");	// HST_CMD
	_sendintelcmd(handle, sPacketCommand);

	MachineState = 0; // 0 = Init Mode, 1 = Command Mode
	
	time_t tStartTime;

	TIMESTART (tStartTime); // Reset Timeout
	while (1)
	{
		unsigned char *pRecvBuffer = sRecvBuffer;
		int i;


		nread = recv(handle->intelcmdfd, &sRecvBuffer[bufstart], sizeof (sRecvBuffer) - bufstart, 0);

		if (nread <= 0)
		{
			if (TIMEOUT (tStartTime, 1))
				break;
			else
				continue;
		}
		TIMESTART (tStartTime); // Reset Timeout

#if 0
		printf ("Receive Data LEN = %d : Remind Data Len = %d\n", nread, bufstart);
#endif

		nread += bufstart;

#if 0
		for (i = 0; i < nread; i++)
			printf ("%02x", sRecvBuffer[i]);
		printf ("\n");
#endif

		while (nread > 0)
		{
			int Packetversion = pRecvBuffer[0];
			int PacketType = (int)(pRecvBuffer[3] << 8 | pRecvBuffer[2]);
			int PacketLength = 8 + ((int)(pRecvBuffer[5] << 8 | pRecvBuffer[4])) * 4;

			// Handle Keep-a-live packet
			if (memcmp (pRecvBuffer, "\x98\x98\x98\x98\x98\x98\x98\x98", 8) == 0)
			{
				printf ("Notification Packet Received! ");
				pRecvBuffer += 8;
				nread -= 8;
				continue;
			}

			if (PacketLength > nread)
			{
				memcpy (sRecvBuffer, pRecvBuffer, nread);
				nread = 0;
				continue;
			}

			switch (PacketType)
			{
				case 0x8000: // Command-Begin Packet
					printf ("Receive Command Begin Packet\n");					
					if (Packetversion != 0x02)
					{
						printf ("Can not handle version %d Command-Begin Packet\n", Packetversion);
						break;
					}

					if (MachineState != 0)
						printf ("Machine State not in idle mode\n");

					MachineState = 1; // Set to Command mode
					printf ("Command %02x%02x%02x%02x\n", pRecvBuffer[11], pRecvBuffer[10], pRecvBuffer[9], pRecvBuffer[8]);
					
					break;
					

				case 0x8001: // Command-End Packet
					printf ("Receive Command End Packet\n");					
					if (Packetversion != 0x02)
					{
						printf ("Can not handle version %d Command-End Packet\n", Packetversion);
						break;
					}

					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					MachineState = 0; // Set to idle mode
					printf ("Status %02x%02x%02x%02x\n", pRecvBuffer[15], pRecvBuffer[14], pRecvBuffer[13], pRecvBuffer[12]);
					break;


				case 0x8005: // Inventory-Response Packet
					printf ("Receive Inventory Response Packet\n");
					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					if (pRecvBuffer[1] & 0x01)
					{
						printf ("CRC Error!!!\n");
						break;
					}
					
					printf ("RSSI = %d\n", (int)(pRecvBuffer[13] << 8 | pRecvBuffer[12])); 
					printf ("Inventory Data : ");
					for (i = 20; i < PacketLength; i++)
						printf ("%02x", pRecvBuffer[i]);
					printf ("\n");

					break;
					
				case 0x0006: // Tag-Access Packet
					printf ("Receive Tag Access Packet\n");
					if (MachineState != 1)
						printf ("Machine State not in command mode\n");

					if (pRecvBuffer[1] & 0x01)
					{
						printf ("An error occurred!!!\n");
						break;
					}

					printf ("Command = %x\n", pRecvBuffer[12]);
					printf ("Data : ");
					for (i = 20; i < PacketLength; i++)
						printf ("%02x", pRecvBuffer[i]);
					printf ("\n");

					break;
					
					
				default:
					printf ("Receive Error Packet Type : 0x%04x\n", PacketType);
					break;
			}

			pRecvBuffer += PacketLength;
			nread -= PacketLength;
			bufstart = nread;
		}		
	}
}

int CS203DC_DecodePacket(CS203DC_Handle* handle, unsigned char* inbuf, int inlen, int* inprocessed, unsigned char* outbuf, int outlen, int* outused, unsigned int* errorcode)
{
	int nproc = 0;
	int nused = 0;
	unsigned char* curinbuf = inbuf;
	int curinlen = inlen;
	unsigned char* curoutbuf = outbuf;
	int curoutlen = outlen;
	int ret = 0;
	int packetlen;
	static time_t CycleEndPacketTime;
	
	*inprocessed = 0;
	*outused = 0;
	*curoutbuf = 0;
		
	while(curinlen >= 8)
	{
		if (_cmpHexStr2HexByteArray(CS203DC_ABORTCMD_RSP, curinbuf, 8, 8) == 0) // check for abort command response, need 8 byte
		{
			printf ("%s(): line %d, abort command response received", __FUNCTION__, __LINE__);
			curinbuf += 8;
			curinlen -= 8;
			nproc += 8;
			*inprocessed = nproc;
		}
		else if (memcmp (curinbuf, "\x98\x98\x98\x98\x98\x98\x98\x98", 8) == 0)
		{
			printf ("Notification Packet Received! ");
			curinbuf += 8;
			curinlen -= 8;
			nproc += 8;
			*inprocessed = nproc;
		}
		else
		{
			CS203DC_PacketHeader header;

			header.pkt_ver = curinbuf[0];
			header.flags = curinbuf[1];
			header.pkt_type = curinbuf[2] + (curinbuf[3] << 8);
			header.pkt_len = curinbuf[4] + (curinbuf[5] << 8);
			header.reserved = curinbuf[6] + (curinbuf[7] << 8);
			packetlen = 8 + (header.pkt_len * 4);
			if(curinlen >= packetlen)
			{
				// complete packet is read
				switch(header.pkt_type)
				{
					case 0x8007: // Antenna-Cycle-End Packet
						printf ("Receive Antenna Cycle End Packet\n");
						CycleEndPacketTime = time(NULL);
						break;

					case 0x8005:
					// inventory response
					DB(syslog(LOG_INFO,"%s(): line %d, inventory response", __FUNCTION__, __LINE__));
					if((header.flags & 0x01) == 0)
					{
						int epcbytelen;
						CS203DC_TagInfo* tag = NULL;
						DB(syslog(LOG_INFO,"%s(): line %d, inventory response: CRC ok", __FUNCTION__, __LINE__));
						epcbytelen = ((curinbuf[20] >> 3) & 0x0000001F) * 2;
						DB(syslog(LOG_INFO,"%s(): line %d, epc length %d bytes", __FUNCTION__, __LINE__, epcbytelen));
						// check with the duplication elimination buffer

						if(handle->dupelicount > 0)
						{
							int i;
							tag = NULL;
							for(i = 0; i < MAX_TAGBUFLEN; i++)
							{
								if(epcbytelen == handle->tag[i].len)
								{
									if(memcmp(&curinbuf[22], handle->tag[i].epc, epcbytelen) == 0)
									{
										// tag existed
										if(handle->tag[i].count <= 0)
										{
											// count down to 0 already, reset the entry and consider as not found
											memset(handle->tag[i].epc, 0, MAX_EPCLEN);
											handle->tag[i].len = 0;
											handle->tag[i].count = 0;
										}
										else
										{
											handle->tag[i].count--;
											tag = &(handle->tag[i]);
											break;
										}
									}
								}
							}
	
							// add to buffer if it is a new tag
							if(tag == NULL)
							{
								handle->tag[handle->tagbeginidx].len = epcbytelen;
								handle->tag[handle->tagbeginidx].count = handle->dupelicount;
								memcpy(handle->tag[handle->tagbeginidx].epc, &curinbuf[22], epcbytelen);
								handle->tagbeginidx++;
								if(handle->tagbeginidx >= MAX_TAGBUFLEN)
								{
									handle->tagbeginidx = 0;
								}
							}
						} // if(handle->dupelicount > 0)
				
						// output only when tag is new and output buffer have enough space
						if(tag == NULL && curoutlen > (epcbytelen * 2) + 6) // +6 for the output format <EPC:XX...XX>
						{
							int i;
							DB(syslog(LOG_INFO,"%s(): line %d, curoutlen %d, *outused %d", __FUNCTION__, __LINE__, curoutlen, *outused));
							sprintf (curoutbuf, "<EPC:");
							curoutbuf += 5;
							curoutlen -= 5;
							nused += 5;
							for(i = 0; i < epcbytelen; i++, curoutbuf += 2, curoutlen -= 2, nused += 2)
							{
								sprintf(curoutbuf, "%02X", curinbuf[22 + i]);
								sprintf(&tzTagID[i << 1], "%02X", curinbuf[22 + i]);
								DB(syslog(LOG_INFO,"%s(): line %d, 0x%02X", __FUNCTION__, __LINE__, curinbuf[22 + i]));
							}

							sprintf (curoutbuf, " Port=%02d%02d>",curinbuf[19],curinbuf[18]);
							curoutbuf+=11;
							curoutlen-=11;
							nused += 11;

							/*
							sprintf (curoutbuf, ">");
							
							curoutbuf++;
							curoutlen--;
							nused++;
							*/
						}
						else
						{
							// just discard that tag
							DB(syslog(LOG_INFO,"%s(): line %d, tag discarded, or duplicated", __FUNCTION__, __LINE__));
						}
					}
					else
					{
						DB(syslog(LOG_INFO,"%s(): line %d, inventory response: CRC error", __FUNCTION__, __LINE__));
					}
					break;

					case 0x8000:	// command begin
						syslog(LOG_INFO,"%s(): line %d, command begin", __FUNCTION__, __LINE__);
						CycleEndPacketTime = time(NULL);
						break;

					case 0x8001:
					// command end
						syslog(LOG_INFO,"%s(): line %d, command end", __FUNCTION__, __LINE__);

					// decode error code
						*errorcode = curinbuf[12] + (curinbuf[13] << 8) + (curinbuf[14] << 16) + (curinbuf[15] << 24);
						printf ("%s(): line %d, curinbuf: 0x%02X 0x%02X 0x%02X 0x%02X", __FUNCTION__, __LINE__, curinbuf[12], curinbuf[13], curinbuf[14], curinbuf[15]);
						printf ("%s(): line %d, command end with error code: 0x%08X", __FUNCTION__, __LINE__, *errorcode);
						ret = 1;
						break;

					case 0x000B:
					// inventory cycle begin
						DB(syslog(LOG_INFO,"%s(): line %d, inventory cycle begin", __FUNCTION__, __LINE__));
						break;

					case 0x000C:
					// inventory cycle end
						DB(syslog(LOG_INFO,"%s(): line %d, inventory cycle end", __FUNCTION__, __LINE__));
						break;

					case 0x1008:
					// inventory cycle end diagnostics
						DB(syslog(LOG_INFO,"%s(): line %d, inventory cycle end disgnostics", __FUNCTION__, __LINE__));
						break;

					default:
						syslog(LOG_WARNING,"%s(): line %d, unknown packet type 0x%04X", __FUNCTION__, __LINE__, header.pkt_type);
						break;
				}
				
				// consume the packet
				curinbuf += packetlen;
				curinlen -= packetlen;
				nproc += packetlen;
				*inprocessed = nproc;
				*outused = nused;
			}
			else // if(curinlen > packetlen)
			{
				// need more data, return normally
				break;
			} // if(curinlen > packetlen)			
		}
	}
	
	if (curinlen)
	{
			// need more data, return normally
		DB(syslog(LOG_INFO,"%s(): line %d, return curinlen too small %d bytes, header", __FUNCTION__, __LINE__, curinlen));
		ret = 0;
	} 
/*
	if (TIMEOUT(CycleEndPacketTime, 2)) // if no Cycle End Packet receive over 2s
		return 2;
*/
	return ret;
}

int CS203DC_CheckUDP(CS203DC_Handle* handle)
{
	unsigned char buf[DATABUFSIZE];
	struct sockaddr_in6 from;
	int fromlen;
	int nrecv;
	int i;

	fromlen = sizeof(from);
	nrecv = recvfrom(handle->udpfd, buf, DATABUFSIZE, 0, (struct sockaddr *) &from, &fromlen);

	syslog(LOG_INFO,"%s(): %d:d bytes read", __FUNCTION__, __LINE__, nrecv);
	for(i = 0; i < nrecv; i++)
	{
		syslog(LOG_INFO,"%s(): %d: (%d) 0x%02X", __FUNCTION__, __LINE__, i, buf[i]);
	}

	// TODO: handle error
		
	return nrecv;
}

int CS203DC_SetPower(CS203DC_Handle* handle, int power)
{
	int rtn = 0;
	int err;
	unsigned char cmd[DATABUFSIZE];
	
	if(handle == NULL)
		return -1;
		
	if(power < 0 || power > 300)
		return -2;

	if(handle->state != CS203DCSTATE_CONNECTED)
		return -3;

	sprintf(cmd, "70010607%02X%02X0000", power & 0x000000FF, (power & 0x0000FF00) >> 8);
	DB(syslog(LOG_ERR,"%s(): line %d, cmd: %s", __FUNCTION__, __LINE__, cmd));
	
	if((err = _sendintelcmd(handle, CS203DC_HSTANTDESCSEL)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send CS203DC_HSTANTDESCSEL, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else if((err = _sendintelcmd(handle, cmd)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot set rf power, err %d", __FUNCTION__, __LINE__, err);
		return err;}
		
	return rtn;
}

int CS203DC_GetMacError(CS203DC_Handle* handle, int* macerror)
{
	int rtn = 0;
	int err;
	unsigned char rsp[DATABUFSIZE];
	
	if(handle == NULL || macerror == NULL)
		return -1;

	if(handle->state != CS203DCSTATE_CONNECTED)
		return -2;

	DB(syslog(LOG_ERR,"%s(): line %d, CS203DC_MACERROR", __FUNCTION__, __LINE__));	
	if((err = _sendintelcmd(handle, CS203DC_MACERROR)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send CS203DC_MACERROR, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}

	DB(syslog(LOG_ERR,"%s(): line %d, read rsp", __FUNCTION__, __LINE__));
	if((err = _readintelrsp(handle, rsp, 8)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot read CS203DC_MACERROR_RSP, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}

	if(_cmpHexStr2HexByteArray(CS203DC_MACERROR_RSP, rsp, 8, 4) == 0) // check for get mac error response, first 4 byte
	{
		*macerror = rsp[4] + (rsp[5] << 8) + (rsp[6] << 16) + (rsp[7] << 24);
		syslog(LOG_ERR,"%s(): line %d, mac error: 0x%08X", __FUNCTION__, __LINE__, *macerror);

		rtn = 0;
	}
	else
	{
		syslog(LOG_ERR,"%s(): line %d, not CS203DC_MACERROR_RSP, read: 0x%02X%02X%02X%02X%02X%02X%02X%02X", __FUNCTION__, __LINE__, rsp[0], rsp[1], rsp[2], rsp[3], rsp[4], rsp[5], rsp[6], rsp[7]);
		return -3;
	}

	return rtn;
}

int CS203DC_SetMode_High (unsigned int ipv4addr, int udpport)
{
	int ret = 0;
	int sockfd;
	struct sockaddr_in to;
	struct sockaddr_in from;
	int nsent;
	int nrecv;
	unsigned char buf1[DATABUFSIZE];
	unsigned char buf2[DATABUFSIZE];
	int buf1_datalen;
	int fromlen;

	if (ipv4addr == 0)
		return -2;
		
	sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 	if (sockfd == -1)
 		return -21;

   syslog (LOG_INFO, "%s(): line: %d: <Reboot IP : %x>", __FUNCTION__, __LINE__, ipv4addr);
  	fcntl (sockfd, F_SETFL, O_NONBLOCK | FASYNC);
      
	// Set High Level Mode Command "/0x80/0x00/0x00/0x00/0x01/0x02"
	buf1[0] = 0x80;
	buf1[4] = (char)((ipv4addr >> 24) & 0xff);
	buf1[3] = (char)((ipv4addr >> 16) & 0xff);
	buf1[2] = (char)((ipv4addr >> 8) & 0xff);
	buf1[1] = (char)(ipv4addr & 0xff);
	buf1[5] = 0x01;
	buf1[6] = 0x0d;
	buf1_datalen = 7;

	// Send UDP Packet
	memset(&to, 0, sizeof(to));
	to.sin_addr.s_addr = BOARDCASEADDRESS;
	to.sin_port = htons (udpport);
	to.sin_family = AF_INET;
	nsent = sendto (sockfd, buf1, buf1_datalen, 0, (const struct sockaddr *)&to, sizeof (to));

	time_t start_time = time (NULL);
	while ((time (NULL) -  start_time) < 2); //Wait 2 seconds

	fromlen = sizeof (from);
	nrecv = recvfrom (sockfd, buf2, 4, 0, (struct sockaddr *)&from, &fromlen); // except 5-byte reply

	// Check Check Mode Respose
	if (nrecv != 4 || buf2[0] != 0x81 || buf2[1] != 0x01 || buf2[2] != 0x00 || buf2[3] != 0x0d)
	{
		syslog (LOG_INFO,"%s(): line %d, cannot set high level command", __FUNCTION__, __LINE__);
		ret = -23;
	}
	else
	{
		ret = 6;
	}
	
	close (sockfd);

	return ret;	
}

int CS203DC_SetMode_Low (unsigned int ipv4addr, int udpport)
{
	int ret = 0;
	int sockfd;
	struct sockaddr_in to;
	struct sockaddr_in from;
	int nsent;
	int nrecv;
	unsigned char buf1[DATABUFSIZE];
	unsigned char buf2[DATABUFSIZE];
	int buf1_datalen;
	int fromlen;

	if (ipv4addr == 0)
		return -2;
		
	sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 	if (sockfd == -1)
 		return -21;


        syslog (LOG_INFO, "%s(): line: %d: <Reboot IP : %x>", __FUNCTION__, __LINE__, ipv4addr);
    	fcntl (sockfd, F_SETFL, O_NONBLOCK | FASYNC);

      
	// Get Mode Command "/0x80/0x00/0x00/0x00/0x01/0x02"
	buf1[0] = 0x80;
	buf1[4] = (char)((ipv4addr >> 24) & 0xff);
	buf1[3] = (char)((ipv4addr >> 16) & 0xff);
	buf1[2] = (char)((ipv4addr >> 8) & 0xff);
	buf1[1] = (char)(ipv4addr & 0xff);
	buf1[5] = 0x01;
	buf1[6] = 0x0c;
	buf1_datalen = 7;

	// Send UDP Packet
	memset(&to, 0, sizeof(to));
	to.sin_addr.s_addr = BOARDCASEADDRESS;
	to.sin_port = htons (udpport);
	to.sin_family = AF_INET;
	nsent = sendto (sockfd, buf1, buf1_datalen, 0, (const struct sockaddr *)&to, sizeof (to));

	time_t start_time = time (NULL);
	while ((time (NULL) -  start_time) < 2); //Wait 2 seconds

	fromlen = sizeof (from);
	nrecv = recvfrom (sockfd, buf2, 4, 0, (struct sockaddr *)&from, &fromlen); // except 5-byte reply

	// Check Check Mode Respose
	if (nrecv != 4 || buf2[0] != 0x81 || buf2[1] != 0x01 || buf2[2] != 0x00 || buf2[3] != 0x0c)
	{
		syslog (LOG_INFO,"%s(): line %d, cannot set low level command", __FUNCTION__, __LINE__);
		ret = -24;
	}
	else
	{
		ret = 6;
	}
		
	close (sockfd);

	return ret;	
}

int CS203DC_GetMode (unsigned int ipv4addr, int udpport)
{
	int ret = 0;
	int sockfd;
	struct sockaddr_in to;
	struct sockaddr_in from;
	int nsent;
	int nrecv;
	unsigned char buf1[DATABUFSIZE];
	unsigned char buf2[DATABUFSIZE];
	int buf1_datalen;
	int fromlen;

	if (ipv4addr == 0)
		return -2;
		
	sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 	if (sockfd == -1)
 		return -21;


        syslog (LOG_INFO, "%s(): line: %d: <Reboot IP : %x>", __FUNCTION__, __LINE__, ipv4addr);
    	fcntl (sockfd, F_SETFL, O_NONBLOCK | FASYNC);

      
	// Get Mode Command "/0x80/0x00/0x00/0x00/0x01/0x02"
	buf1[0] = 0x80;
	buf1[4] = (char)((ipv4addr >> 24) & 0xff);
	buf1[3] = (char)((ipv4addr >> 16) & 0xff);
	buf1[2] = (char)((ipv4addr >> 8) & 0xff);
	buf1[1] = (char)(ipv4addr & 0xff);
	buf1[5] = 0x01;
	buf1[6] = 0x0e;
	buf1_datalen = 7;

	// Send UDP Packet
	memset(&to, 0, sizeof(to));
	to.sin_addr.s_addr = BOARDCASEADDRESS;
	to.sin_port = htons (udpport);
	to.sin_family = AF_INET;
	nsent = sendto (sockfd, buf1, buf1_datalen, 0, (const struct sockaddr *)&to, sizeof (to));

	fromlen = sizeof (from);

	time_t start_time = time (NULL);
	while ((time (NULL) -  start_time) < 6)
	{
		nrecv = recvfrom (sockfd, buf2, 5, 0, (struct sockaddr *)&from, &fromlen); // except 5-byte reply

// Check Check Mode Respose
		if (nrecv == 5 && (unsigned char)buf2[0] == 0x81 && buf2[1] == 0x01 && buf2[2] == 0x01 && buf2[3] == 0x0e)
		{
			switch (buf2[4])
			{
				case 0x00:
					ret = 4;
					break;				
				case 0x01:
					ret = 5;
					break;
			}
			
			break;
		}
		else if (nrecv == 5)
		{
			printf ("%s(): line %d, Get mode response %x:%x:%x:%x:%x:%x\n", __FUNCTION__, __LINE__, nrecv, buf2[0], buf2[1], buf2[2], buf2[3], buf2[4]);
		}
	}
	
	if (ret == 0)
	{
		printf ("%s(): line %d, Get Mode Error\n", __FUNCTION__, __LINE__);
		ret = -25;
	}
		
	close (sockfd);

	return ret;	
}

int RFID_SetDwell (CS203DC_Handle* handle, unsigned int dwell)
{
	int rtn = 0;
	int err;
	unsigned char cmd[DATABUFSIZE];
	
	if(handle == NULL)
		return -1;
		
	if(handle->state != CS203DCSTATE_CONNECTED)
		return -3;

	//ANT_PORT_DWELL
	sprintf(cmd, "70010507%02X%02X%02X%02X", (unsigned char)(dwell & 0xff), (unsigned char)((dwell >> 8) & 0xff), (unsigned char)((dwell >> 16) & 0xff), (unsigned char)((dwell >> 24) & 0xff));
	DB(syslog(LOG_ERR,"%s(): line %d, cmd: %s", __FUNCTION__, __LINE__, cmd));
	
	if((err = _sendintelcmd(handle, CS203DC_HSTANTDESCSEL)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot send CS203DC_HSTANTDESCSEL, err %d", __FUNCTION__, __LINE__, err);
		return err;
	}
	else if((err = _sendintelcmd(handle, cmd)) < 0)
	{
		syslog(LOG_ERR,"%s(): line %d, cannot set rf power, err %d", __FUNCTION__, __LINE__, err);
		return err;}

	return rtn;
}

/*
	return 1 = Success Get Data
	return -1 = No Data
	return 0 = 0 Byte Data / Socket Disconnect
*/
int readtag (CS203DC_Handle *handle)
{
	int nprocessed;
   int macerr;
   int nserialused;
   int nrecv;
   int err;
	char fromLanBuff[1600];
	char toSerialBuff[1600];

	nrecv = recv (handle->intelcmdfd, fromLanBuff, sizeof (fromLanBuff), 0);
 
   if (nrecv > 0)
	{
		struct in_addr in;	

		printcurtime ();		
		in.s_addr = handle->ipv4addr;
	//	printf ("IP:%s receive %d Bytes, ", inet_ntoa(in), nrecv);	
		err = CS203DC_DecodePacket (handle, fromLanBuff, nrecv, &nprocessed, toSerialBuff, sizeof (toSerialBuff), &nserialused, &macerr);
		printf ("%s\n", toSerialBuff);

		if (err == 2)
			return 2;
		
		return 1;
	}
	else if (nrecv != -1)
	{
		return 0;
	}
	
	return -1;
}

// Reboot IOLAN and traget IP
int CS203DC_Reboot (unsigned int ipv4addr, int udpport)
{
	int ret = 0;
	int sockfd;
	struct sockaddr_in to;
	struct sockaddr_in from;
	int nsent;
	int nrecv;
	unsigned char buf1[DATABUFSIZE];
	unsigned char buf2[DATABUFSIZE];
	int buf1_datalen;
	int fromlen;

	if (ipv4addr == 0)
		return -2;
		
	sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 	if (sockfd == -1)
 		return -21;


        syslog (LOG_INFO, "%s(): line: %d: <Reboot IP : %x>", __FUNCTION__, __LINE__, ipv4addr);
//      	FD_ZERO (&readfd);
//      	FD_SET (sockfd, &readfd); 

//    	if (ioctlsocket (sock, FIONBIO, &nonblocking) != 0)
    	fcntl (sockfd, F_SETFL, O_NONBLOCK | FASYNC);

      
	// Reboot Command "/0x80/0x00/0x00/0x00/0x01/0x02"
	buf1[0] = 0x80;
	buf1[4] = (char)((ipv4addr >> 24) & 0xff);
	buf1[3] = (char)((ipv4addr >> 16) & 0xff);
	buf1[2] = (char)((ipv4addr >> 8) & 0xff);
	buf1[1] = (char)(ipv4addr & 0xff);
	buf1[5] = 0x01;
	buf1[6] = 0x02;
	buf1_datalen = 7;

	// Send UDP Packet
	memset(&to, 0, sizeof(to));
	to.sin_addr.s_addr = BOARDCASEADDRESS;
	to.sin_port = htons (udpport);
	to.sin_family = AF_INET;
	nsent = sendto (sockfd, buf1, buf1_datalen, 0, (const struct sockaddr *)&to, sizeof (to));


	time_t start_time = time (NULL);
	while ((time (NULL) -  start_time) < 5); //Wait 2 seconds

	fromlen = sizeof (from);
	nrecv = recvfrom (sockfd, buf2, 4, 0, (struct sockaddr *)&from, &fromlen); // except 3-byte reply

	// Check Reboot Respose "/0x81/0x01/0x02"
	if (nrecv != 4 || buf2[0] != 0x81 || buf2[1] != 0x01 || buf2[2] != 0x00 || buf2[3] != 0x02)
	{
		syslog (LOG_INFO,"%s(): line %d, cannot Reboot", __FUNCTION__, __LINE__);
		ret = -22;
	}
	else
	{
		ret = 0;
	}
		
	close (sockfd);

	
	return ret;	
}


char tzLocalIP[NI_MAXHOST];

// Check CS203 exist
int CS203DC_Netfinder (CS203DC_Handle* handle)
{
	int ret = 0;
	int sockfd;
	struct sockaddr_in to;
	struct sockaddr_in from;
	int nsent;
	int nrecv;
	unsigned char buf1[DATABUFSIZE];
	unsigned char buf2[DATABUFSIZE];
	int buf1_datalen;
	int fromlen;
	int iRandomID;
    int broadcast = 1;
    int cnt;
	if (handle->ipv4addr == 0)
		return -2;
		
	sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 	if (sockfd == -1)
 		return -21;
 		
   if ((setsockopt (sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast)) == -1)
 		return -21;

//        printf ("%s(): line: %d: <Check CS203 IP : %x>", __FUNCTION__, __LINE__, ipv4addr);
  	fcntl (sockfd, F_SETFL, O_NONBLOCK | FASYNC);
    	
	srand((int)time(0));
  	iRandomID = rand() % 0x7ffff;

	// Search Packet Command "/0x00/0x00/0xxx/0xxx"
	buf1[0] = 0x00;
	buf1[1] = 0x00;
	buf1[2] = iRandomID & 0xff;
	buf1[3] = (iRandomID >> 8) & 0xff;
	buf1_datalen = 4;

	// Send UDP Packet
	memset(&to, 0, sizeof(to));
//	to.sin_addr.s_addr = BOARDCASEADDRESS;	// UDP broad cast
	to.sin_addr.s_addr = handle->ipv4addr;	// UDP unicast
	to.sin_port = htons (3040); // Net Founder Port Number
	to.sin_family = AF_INET;
	nsent = sendto (sockfd, buf1, buf1_datalen, 0, (const struct sockaddr *)&to, sizeof (to));

	// Recv UDP Packet
	fromlen = sizeof (from);
	time_t start_time = time (NULL);
	while ((time (NULL) -  start_time) < 2 && ret == 0) //Wait 2 seconds
	{
		nrecv = recvfrom (sockfd, buf2, sizeof (buf2), 0, (struct sockaddr *)&from, &fromlen);
	
		if (nrecv <= 0)
			 continue;
		
		if (nrecv < 32)
		{
			printf ("Receive wrong packet\n");
			printf ("Packet content : Len[%d]", nrecv);
			for (cnt = 0; cnt < nrecv; cnt++)
			{
				printf ("%02x", buf2[cnt]);	
			}
			printf ("\n");
			continue;
		}
		
		if (!(buf2[0] == 0x01 && (buf2[1] == 0x00 || buf2[1] == 0x01) && 
			buf2[2] == (iRandomID & 0xff) && buf1[3] == ((iRandomID >> 8) & 0xff)))
		{
			printf ("Receive wrong packet header\n");
			printf ("Packet header : %02x%02x%02x%02x\n", buf2[0], buf2[1], buf2[2], buf2[3]);
			continue;
		}
		
		
		if (!(buf2[20] == (handle->ipv4addr & 0xff) && 
			buf2[21] == (handle->ipv4addr >> 8 & 0xff) && 
			buf2[22] == (handle->ipv4addr >> 16 & 0xff) && 
			buf2[23] == (handle->ipv4addr >> 24 & 0xff)))
		{
			printf ("Receive wrong IP address %02x%02x%02x%02x\n", buf2[20], buf2[21], buf2[22], buf2[23]);
			continue;
		}

//		Recevied correct packet
		ret = 1;
		memcpy (handle->sMacAdd, &buf2[14], 6);
		strcpy (handle->tzDevName, &buf2[32]);
	}
			
	close (sockfd);

	/*
	if (ret == 0)
		printf ("CS203/468 not found\n");
	*/
	return ret;
}

// Get Local IP
unsigned int GetLocalIP ()
{
	struct ifaddrs *ifaddr, *ifa;
   int family, s;
   char host[NI_MAXHOST];
   int ret = 0;

   if (getifaddrs (&ifaddr) == -1)
   {
		printf ("getifaddrs\n");
   		return ret;
   }

   /* Walk through linked list, maintaining head pointer so we can free list later */

   for (ifa = ifaddr; ifa != NULL; ifa = ifa->ifa_next) 
   {
   	if (ifa->ifa_addr == NULL)
      	continue;

      family = ifa->ifa_addr->sa_family;

		if (strcmp (ifa->ifa_name, "eth0") == 0 && family == AF_INET)
		{
      	s = getnameinfo (ifa->ifa_addr, sizeof(struct sockaddr_in), tzLocalIP, NI_MAXHOST, NULL, 0, NI_NUMERICHOST);

        	if (s != 0) {
        		printf("getnameinfo() failed: %s\n", gai_strerror(s));
        		ret = -1;
        	}
        	else
        	{
        		printf ("Local IP Addr : %s\n", tzLocalIP);
        		ret = 1;
        	}
		}
	}

	freeifaddrs(ifaddr);

	return (ret);
}


int CS203DC_CheckStatus (unsigned int ipv4addr)
{
	int sockfd;
	struct sockaddr_in to;
	struct sockaddr_in from;
	int nsent;
	int nrecv;
	int fromlen;
	time_t start_time;
	unsigned int iplocalv4addr;
	unsigned char sPacketCommand[7];
	unsigned char buf2[14];
  	int ret;
  	int broadcast = 1;
	
	printf ("Check Status\n");

	// Init	
	sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 	if (sockfd == -1)
 		return -21;
   if ((setsockopt (sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast)) == -1)
 		return -21;
   fcntl (sockfd, F_SETFL, O_NONBLOCK | FASYNC);

	memset (sPacketCommand, 0, sizeof (sPacketCommand));
		
	// Assignmnet Packet Command
	sPacketCommand[0] = 0x80;			// Command
	sPacketCommand[1] = (ipv4addr & 0xff);		// CS203 IP Addr
	sPacketCommand[2] = (ipv4addr >> 8 & 0xff);
	sPacketCommand[3] = (ipv4addr >> 16 & 0xff);
	sPacketCommand[4] = (ipv4addr >> 24 & 0xff);
	sPacketCommand[5] = 0;		// Number of value byte
	sPacketCommand[6] = 0x01;	// Check Status Command


	// Send Out UDP Command
	memset(&to, 0, sizeof(to));
	to.sin_addr.s_addr = BOARDCASEADDRESS;	// UDP broad cast
	to.sin_port = htons (3041); // UDP Command Port
	to.sin_family = AF_INET;
	nsent = sendto (sockfd, sPacketCommand, sizeof (sPacketCommand), 0, (const struct sockaddr *)&to, sizeof (to));

	// Get Reply 
	fromlen = sizeof (from);
	ret = -22;
	
	start_time = time (NULL);
	while ((time (NULL) -  start_time) < 2) //Wait 2 second
	{
		nrecv = recvfrom (sockfd, buf2, 14, 0, (struct sockaddr *)&from, &fromlen);

		if (nrecv == 14 && buf2[0] == 0x81 && buf2[1] == 0x01 && buf2[2] == 0x09 && buf2[3] == 0x01)
		{
			ret = 0;
			break;
		}
	}

	// Close			
	close (sockfd);
	
	return ret;
}


int CS203DC_SetTrustedServer (CS203DC_Handle* handle)
{
	int sockfd;
	struct sockaddr_in to;
	struct sockaddr_in from;
	int nsent;
	int nrecv;
	int fromlen;
	time_t start_time;
	unsigned int iplocalv4addr;
	unsigned char sPacketCommand[56];
	unsigned char buf2[4];
  	int iRandomID;
  	int ret;
  	int broadcast = 1;
	
	printf ("Set Trusted Server\n");

	// Init	
	sockfd = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
 	if (sockfd == -1)
 		return -21;
        if ((setsockopt (sockfd, SOL_SOCKET, SO_BROADCAST, &broadcast, sizeof broadcast)) == -1)
 		return -21;
    	fcntl (sockfd, F_SETFL, O_NONBLOCK | FASYNC);

	memset (sPacketCommand, 0, sizeof (sPacketCommand));
	srand((int)time(0));
      	iRandomID = rand() % 0x7ffff;
		
	iplocalv4addr = inet_addr (tzLocalIP);
	
	// Assignmnet Packet Command
	sPacketCommand[0] = 0x02;			// Command
	sPacketCommand[2] = iRandomID & 0xff;		// ID
	sPacketCommand[3] = (iRandomID >> 8) & 0xff;
	sPacketCommand[4] = (handle->ipv4addr & 0xff);		// CS203 IP Addr
	sPacketCommand[5] = (handle->ipv4addr >> 8 & 0xff);
	sPacketCommand[6] = (handle->ipv4addr >> 16 & 0xff);
	sPacketCommand[7] = (handle->ipv4addr >> 24 & 0xff);
	sPacketCommand[8] = (iplocalv4addr & 0xff);		// Trusted Server IP
	sPacketCommand[9] = (iplocalv4addr >> 8 & 0xff);
	sPacketCommand[10] = (iplocalv4addr >> 16 & 0xff);
	sPacketCommand[11] = (iplocalv4addr >> 24 & 0xff);
	sPacketCommand[14] = 1;				// DHCP retry count
	sPacketCommand[15] = 1;				// set to static IP
	memcpy (&sPacketCommand[16], handle->sMacAdd, 6); // CS203 MAC Addr
//	sPacketCommand[22] = 1;				// Enabdle trusted mode
	sPacketCommand[22] = 0;				// Disabdle trusted mode
	strcpy (&sPacketCommand[24], handle->tzDevName);	// Device Name

	// Send Out UDP Command
	memset(&to, 0, sizeof(to));
//	to.sin_addr.s_addr = BOARDCASEADDRESS;	// UDP broad cast
	to.sin_addr.s_addr = handle->ipv4addr;
	to.sin_port = htons (3040); // Net Founder Port Number
	to.sin_family = AF_INET;
	nsent = sendto (sockfd, sPacketCommand, sizeof (sPacketCommand), 0, (const struct sockaddr *)&to, sizeof (to));

	// Get Reply 
	fromlen = sizeof (from);
	start_time = time (NULL);
	while ((time (NULL) -  start_time) < 1) //Wait 1 second
	{
		nrecv = recvfrom (sockfd, buf2, 4, 0, (struct sockaddr *)&from, &fromlen); // Reply Packet (4 Bytes)

		if (nrecv == 4 && buf2[0] == 0x03 && buf2[2] == (iRandomID & 0xff) && buf2[3] == ((iRandomID >> 8) & 0xff))
		{
			switch (buf2[1])
			{
				case 0:
					printf ("MAC address error\n");
					break;
				case 1:
					ret = 1;
					break;
				default:
					printf ("Error Format\n");
			}
					ret = 1;
		}
	}

	// Close			
	close (sockfd);
	
	usleep (2000000); // Reboot delay (2 seconds)

	return ret;
}

// Second way to detect connect broke
int iConnOk;
void sig_handler (int sig)
{
	switch (sig)
	{
		case SIGPIPE:
			iConnOk = 0;
			break;
	}
}


void *run (void *fd)
{
	CS203DC_Handle *CS203fd = fd;
	int ret, i;
	int iport, cport, uport;
	unsigned int cnt;
	
	iport = DEFAULT_INTELCMDPORT;
	cport = DEFAULT_CTRLCMDPORT;
	uport = DEFAULT_UDPPORT;

printf("in run\n");
// Check IP
	printf ("Check CS203 IP:%s\n", CS203fd->ReaderHostName);
	CS203fd->ipv4addr = inet_addr (CS203fd->ReaderHostName);
	
	for (i=0;i<3;i++)
	{
		if ((ret = CS203DC_Netfinder (CS203fd)) == 1)
			break;
	}
	if (ret != 1)
	{
		printf ("IP:%s no response\n", CS203fd->ReaderHostName);
		
		ret = CS203DC_CheckStatus (CS203fd->ipv4addr);		
		if (ret != 0)
		{
			printf ("CS203 not found (Error Code :%d)\n", ret);
			return 0;
		}
		
		// Clear CS203 unpredictable error
		CS203DC_Reboot (inet_addr (CS203fd->ReaderHostName), uport);
		sleep (2);

		ret = CS203DC_Netfinder (CS203fd);
		if (ret != 1)
		{
			printf ("Unpredictable error\n", ret);
			return 0;
		}
	}
	
// Set Trusted Server
	CS203DC_SetTrustedServer (CS203fd);
	if (ret != 1)
	{
		printf ("Error Code :%d\n", ret);
		return 0;
	}
	
 // Open:IP
 	printf ("Connecting IP : %s\n", CS203fd->ReaderHostName);
	ret = CS203DC_Open (CS203fd->ReaderHostName, iport, cport, uport, CS203fd);
 	if (ret != 0)
 	{
 		printf ("Open Error : %d\n", ret);	
 		return 0;
  	}
  	
 // Check Mode
	printf ("Check API Mode\n");
	ret = CS203DC_GetMode (CS203fd->ipv4addr, uport);
	
	if (ret != 5) //Not in Low Level Mode
 	{
		printf ("Change to Low Level Mode\n");
 		CS203DC_SetMode_Low (CS203fd->ipv4addr, uport);
		ret = CS203DC_Close (CS203fd);

		// wait 2 second, for CS203 reboot when mode changed
		usleep (20000000);
 
		// ReOpen if change mode
 	    	printf ("Connecting IP : %s\n", CS203fd->ReaderHostName);
		ret = CS203DC_Open (CS203fd->ReaderHostName, iport, cport, uport, CS203fd);
 		if (ret != 0)
 		{
 			printf ("Open Error : %d\n", ret);	
 			return 0;
 		}
  	}
  	else
  	{
  		printf ("Current is Low Level Mode\n");
// 		CS203DC_SetMode_High (CS203fd->ipv4addr, uport);
  	}

	// Set DWell Time to 1s (will receive Antenna Cycle End Packet per second) for single port / CS203 applications

	RFID_SetDwell (CS203fd, 1000);
	// set RF power (intel commands)
	CS203DC_SetPower (CS203fd, CS203fd->power);
	CS468_SetPort (CS203fd);

// StartInv
	ret = 0;
	if (CS203fd->iConf_ReadTag == 1)
	{
	  	printf ("Start Inventory\n");
	 	ret = CS203DC_StartInventory (CS203fd);
	 	if (ret != 0)
	 	{
 			printf ("StartInv Error : %d\n", ret);	
 			return 0;
	 	}
	}
 	
// Read Tag and Display 
	time_t start_time = time (NULL);
	time_t time_tagsuccess = time (NULL);
	
//	while ((time (NULL) -  start_time) < 20 || CS203fd->iConf_Forever == 1) 
	while ((time (NULL) -  start_time) < 5 || CS203fd->iConf_Forever == 1) 
	{
		int result = readtag (CS203fd);
		
		if (result == 1)
		{		
			time_tagsuccess = time (NULL);  // Reset TCP Timer
		}
		else
		{
			if (result != 2 && (time (NULL) -  time_tagsuccess) < 10) // Test return code and 
				continue;
			
			printf ("Reconnect... !!!\n");

			ret = 1;
				
			while (ret != 0)
			{
				char command[100];
				time_t   timep; 
				char *p, *p1;

				ret = CS203DC_Close (CS203fd);
					
			   	timep = time (NULL);
			   	p = (char *)asctime (localtime (&timep));
				p1 = strchr (p, '\n');
				*p1 = 0x00;
				   
				sprintf (command, "echo Disconnect : %s >> %s", p, CS203fd->ReaderHostName);
				system (command);

				printf ("%s", command);

				sleep (1);
					
				ret = CS203DC_Open (CS203fd->ReaderHostName, iport, cport, uport, CS203fd);
			}

			if (CS203fd->iConf_ReadTag == 1)
			{
				// Set DWell Time to 1s (will receive Antenna Cycle End Packet per second)
				RFID_SetDwell (CS203fd, 1000);
				CS203DC_SetPower (CS203fd, CS203fd->power);
				CS468_SetPort (CS203fd);
				CS203DC_StartInventory (CS203fd);
			}
			time_tagsuccess = time (NULL);  // Reset TCP Timer

			usleep (10); // after 10 seconds reconnect
		}
	}
	
	if (ret != 0)
	{
		printf ("Connection Failure %d\n", ret);
		exit (1);
	}
	
// StopInv
	ret = CS203DC_StopInventory (CS203fd);

// Read TID and UserData
	CS203DC_ReadTid (CS203fd, tzTagID);

	CS203DC_ReadUserData (CS203fd, tzTagID);

// Write Data
	if (CS203fd->iConf_WriteUser == 1)
	{
		CS203DC_WriteEPCData (CS203fd, tzTagID);
	}

// Close
	ret = CS203DC_Close (CS203fd);

// Reboot CS203
	CS203DC_Reboot (CS203fd->ipv4addr, uport);

	return 0;
}


CS203DC_Handle *handler_alloc ()
{
	CS203DC_Handle *fd;

	fd = (CS203DC_Handle *)malloc (sizeof (CS203DC_Handle));
	
	if (fd != NULL)
	{
		fd->ipv4addr = 0;
		fd->state = CS203DCSTATE_IDLE;
		fd->intelcmdfd = 0;
		fd->controlcmdfd = 0;
		fd->udpfd = 0;
		fd->iConf_ReadTag = 0;
		fd->iConf_Forever = 0;
		fd->iConf_WriteUser = 0;
		fd->NextHandle = NULL;
		fd->Freq_CN = 0;
		fd->iConf_lbt = 0;
		fd->iConf_channel = 1;
	}
	else
	{
   	printf ("Insufficient memory, can not allocate handler!\n");
	}
		
	return fd;
}

struct StruPortConfig
{
	long	Active;
	long	Power;
	long	Dwell;
	long	InventoryRounds;
	long	LocalInventory;
	long	Algorithm;
	long	StartQ;
	long	LocalProfile;
	long	Profile;
	long	LocalChannel;
	long	Channel;
};

struct StruPortConfig PortConfig[16];

int conf_Init (char *confFile, CS203DC_Handle *CS203fd_first)
{
	CS203DC_Handle *CS203fd;
	int linecnt = 0;
	FILE *fin;
	char linebuf [200];
	char *spacepos;

	fin = fopen (confFile, "rt");
	if (fin == NULL)
	{
		printf ("Can not open port config file '%s'\n", confFile);
		return -1;
	}
	
	while (fscanf (fin, "%[^\n]\n", linebuf) != EOF)
	{
		spacepos = strchr (linebuf, '\r');
		if (spacepos != NULL)
			*spacepos = 0;
		
		printf ("Conf : %s\n", linebuf);
	
		linecnt ++;
	
		if (linecnt == 1)
		{
			CS203fd = CS203fd_first;
		}
		else
		{
			CS203fd->NextHandle = handler_alloc ();
			if (CS203fd->NextHandle == NULL)
				return -1;
			CS203fd = CS203fd->NextHandle;
		}
			
		spacepos = strchr (linebuf, ' ');
		if (spacepos != NULL)
			*spacepos = 0;

		strncpy (CS203fd->ReaderHostName, linebuf, 50);

		while (spacepos != NULL)
		{
			spacepos ++;
				
			if (memcmp (spacepos, "-tag", 4) == 0)
			{
				CS203fd->iConf_ReadTag = 1;
			}
			else if (memcmp (spacepos, "-loop", 5) == 0)
			{
				CS203fd->iConf_Forever = 1; // Test Forever					
			}
			else if (memcmp (spacepos, "-write", 5) == 0)
			{
				CS203fd->iConf_WriteUser = 1; // Test Forever								
			}
				
			spacepos = strchr (spacepos, ' ');
		}
	}
	
	fclose (fin);

	return 0;
}

int CS468_Portconf (char *filename, CS203DC_Handle* handle)
{
	FILE *fin;
	char cvsbuf[12][20];
	int cnt=0;
	
	fin = fopen (filename, "rt");
	if (fin == NULL)
	{
		printf ("Can not open port config file '%s'\n", filename);
		return -1;
	}

	while (fscanf (fin, "%s %s %s %s %s %s %s %s %s %s %s %s\n", cvsbuf[0], cvsbuf[1], cvsbuf[2], cvsbuf[3], cvsbuf[4], cvsbuf[5], cvsbuf[6], cvsbuf[7], cvsbuf[8], cvsbuf[9], cvsbuf[10], cvsbuf[11], cvsbuf[12]) != EOF)
	{
		if (strstr(cvsbuf[0],"PortNumber"))
		{
			printf ("%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s,%s\n", cvsbuf[0], cvsbuf[1], cvsbuf[2], cvsbuf[3], cvsbuf[4], cvsbuf[5], cvsbuf[6], cvsbuf[7], cvsbuf[8], cvsbuf[9], cvsbuf[10], cvsbuf[11], cvsbuf[12]);
			continue;	//skipping the header line of the file
		}

		int port = atoi (cvsbuf[0]);
		if (port < 0 || port > 15)
		{
			return -1;
		}
		PortConfig[port].Active = strcasecmp (cvsbuf[1], "enable") == 0;
		PortConfig[port].Power = atol (cvsbuf[2]);
		PortConfig[port].Dwell = atol (cvsbuf[3]);
		PortConfig[port].InventoryRounds = atol (cvsbuf[4]);
		PortConfig[port].LocalInventory = strcasecmp (cvsbuf[5], "enable") == 0;
		PortConfig[port].Algorithm = atol (cvsbuf[6]);
		PortConfig[port].StartQ = atol (cvsbuf[7]);
		PortConfig[port].LocalProfile = strcasecmp (cvsbuf[8], "enable") == 0;
		PortConfig[port].Profile = atol (cvsbuf[9]);
		PortConfig[port].LocalChannel = strcasecmp (cvsbuf[10], "enable") == 0;
		PortConfig[port].Channel = atol (cvsbuf[11]);

		//Generate sequence
		if (PortConfig[port].Active)
		{
			handle->portSequence[cnt]=port;
			cnt++;
		}

		printf ("%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n", port, PortConfig[port].Active, PortConfig[port].Power, PortConfig[port].Dwell, PortConfig[port].InventoryRounds, PortConfig[port].LocalInventory, PortConfig[port].Algorithm, PortConfig[port].StartQ, PortConfig[port].LocalProfile, PortConfig[port].Profile, PortConfig[port].LocalChannel, PortConfig[port].Channel);
	}
	handle->portSequenceCount=cnt;
	
	fclose (fin);
	
	return 0;
}

int CS468_SetPort (CS203DC_Handle *handle)
{
	unsigned char sPacketCommand[32];
	unsigned char buf2[10];
	unsigned int buf;
	unsigned int buf3;
	int nrecv;
	int cnt, cnt2, nread;
	int ret, err;
	unsigned char sRecvBuffer[1024];

//global variables
// QUERY_CFG
	buf = (handle->query_target << 4 & 0x10) | (handle->query_session << 5 & 0x60);
	sprintf(sPacketCommand, "70010009%02X%02X%02X%02X", (unsigned char)(buf & 0xff), (unsigned char)(buf >> 8 & 0xff),(unsigned char)(buf >> 16 & 0xff),(unsigned char)(buf >> 24 & 0xff));
	_sendintelcmd(handle, sPacketCommand);
//Set Inventory Algorithm
// INV_CFG
	sprintf(sPacketCommand, "70010109%02X000000",handle->Algorithm ? 3 : 0);
	_sendintelcmd(handle, sPacketCommand);
// INV_SEL (FixedQ/DynamicQ)
	sprintf(sPacketCommand, "70010209%02X000000",handle->Algorithm ? 3 : 0);
	_sendintelcmd(handle, sPacketCommand);
// INV_ALG_PARM_0 (Algorithm parameters)
	buf=(handle->StartQ & 0xF) | (handle->MaxQ << 4 & 0xF0) | (handle->MinQ << 8 & 0xF00) | (handle->ThreadholdMultipler << 12 & 0xF000);
	sprintf(sPacketCommand, "70010309%02X%02X%02X%02X",(unsigned char)(buf & 0xff), (unsigned char)(buf >> 8 & 0xff), (unsigned char)(buf >> 16 & 0xff), (unsigned char) (buf >> 24 & 0xff));
	_sendintelcmd(handle, sPacketCommand);
//	INV_ALG_PARM_2 (toggle)
	if (handle->query_session == 0)
		sprintf(sPacketCommand, "70010509%02X000000", handle->toggle ? 3 : 2); // INV_ALG_PARM_2
	else
		sprintf(sPacketCommand, "70010509%02X000000", handle->toggle ? 1 : 0); // INV_ALG_PARM_2
	_sendintelcmd(handle, sPacketCommand);

	if(handle->multiport)
	{
		//local settings for inidividual ports
		for (cnt = 0; cnt < 16; cnt++)
		{
		// ANT_PORT_SEL
			sprintf(sPacketCommand, "70010107%02X000000", (unsigned char) (cnt));
			_sendintelcmd(handle, sPacketCommand);

		// ANT_PORT_CFG
			buf = ((PortConfig[cnt].Active != 0) ? 0x00000001 : 0) |
					((PortConfig[cnt].LocalInventory != 0) ? 0x00000002 : 0) |
					((PortConfig[cnt].Algorithm & 0x03) << 2) |
					((PortConfig[cnt].StartQ & 0x0f) << 4) |
					((PortConfig[cnt].LocalProfile != 0) ? 0x00000100 : 0) |
					((PortConfig[cnt].Profile & 15) << 9) |
					((PortConfig[cnt].LocalChannel != 0) ? 0x00002000 : 0) |
					((PortConfig[cnt].Channel & 63) << 14);
			sprintf(sPacketCommand, "70010207%02X%02X%02X%02X", (unsigned char)(buf & 0xff), (unsigned char)(buf >> 8 & 0xff),(unsigned char)(buf >> 16 & 0xff),(unsigned char)(buf >> 24 & 0xff));
			_sendintelcmd(handle, sPacketCommand);

		// ANT_PORT_DWELL
			sprintf(sPacketCommand, "70010507%02X%02X%02X%02X", (unsigned char) (PortConfig[cnt].Dwell), (unsigned char) (PortConfig[cnt].Dwell >> 8), (unsigned char) (PortConfig[cnt].Dwell >> 16), (unsigned char) (PortConfig[cnt].Dwell >> 24));
			_sendintelcmd(handle, sPacketCommand);

		// ANT_PORT_POWER
			sprintf(sPacketCommand, "70010607%02X%02X0000", (unsigned char) (PortConfig[cnt].Power), (unsigned char) (PortConfig[cnt].Power >> 8));
			_sendintelcmd(handle, sPacketCommand);
		
		// ANT_PORT_INV_CNT (hardcode to 0 - run infinitely)
			sprintf(sPacketCommand, "7001070700000000");
			_sendintelcmd(handle, sPacketCommand);

		}
	}

// ANT_CYCLES (switching ports that are enabled at random pattern if sequence mode is selected)

	//set either normal mode or sequence mode
	buf=0;
	if (handle->sequenceMode)
	{
		buf |= (handle->portSequenceCount << 18 & 0xFC0000);
		buf |= (1 << 16);
	}
	else
		buf=0;
	sprintf(sPacketCommand, "70010007FFFF%02X%02X", (unsigned char)(buf >> 16 & 0xff), (unsigned char)(buf >> 24 & 0xff));
	_sendintelcmd(handle, sPacketCommand);

	fcntl(handle->intelcmdfd, F_SETFL, fcntl(handle->intelcmdfd,F_GETFL,0)&(~O_NONBLOCK));
	//modify OEM table
	if (handle->sequenceMode)
	{
		buf=0; buf3=0;
		for (cnt=0;cnt<16;cnt++)
		{
			if (handle->portSequenceCount<cnt) break;
			if (cnt < 8)
				buf |= (handle->portSequence[cnt] & 0xF) << (28 - (4*cnt));
			else
				buf3 |= (handle->portSequence[cnt] & 0xF) << (28 - (4*(cnt-8)));
		}
		sprintf(sPacketCommand, "70010005A7000000");
		_sendintelcmd(handle, sPacketCommand);
		sprintf(sPacketCommand, "70010105%02X%02X%02X%02X", (unsigned char)(buf & 0xff), (unsigned char)(buf >> 8 & 0xff), (unsigned char)(buf >> 16 & 0xff), (unsigned char) (buf >> 24 & 0xff));
		_sendintelcmd(handle, sPacketCommand);
		printf("\nAntenna Sequence Order #1: %s\n",sPacketCommand);
		//send HST_CMD
		sprintf(sPacketCommand, "700100F002000000");	// HST_CMD
		_sendintelcmd(handle, sPacketCommand);
		{
			int nread = 0;
			unsigned char buf[32];
			// response, 8-byte, no need to check the content
			nread = recv(handle->intelcmdfd, buf, 32, 0);

		}

		sprintf(sPacketCommand, "70010005A8000000");
		_sendintelcmd(handle, sPacketCommand);
		sprintf(sPacketCommand, "70010105%02X%02X%02X%02X", (unsigned char)(buf3 & 0xff), (unsigned char)(buf3 >> 8 & 0xff), (unsigned char)(buf3 >> 16 & 0xff), (unsigned char)(buf3 >> 24 & 0xff));
		_sendintelcmd(handle, sPacketCommand);
		printf("Antenna Sequence Order #2: %s\n",sPacketCommand);
		sprintf(sPacketCommand, "700100F002000000");	// HST_CMD
		_sendintelcmd(handle, sPacketCommand);
		{
			int nread = 0;
			unsigned char buf[32];
			// response, 32-byte, no need to check the content
			nread = recv(handle->intelcmdfd, buf, 32, 0);
		}

	}
	_sendintelcmd(handle, "7001010700000000");

	return 0;

}

/**
 *****************************************************************************
 **
 ** @brief  Command main routine
 **
 ** Command synopsis:
 **
 **     example READERHOSTNAME
 **
 ** @exitcode   0               
 **
 *****************************************************************************/
int main (int ac, char *av[])
{
	CS203DC_Handle *CS203fd_first, *CS203fd;
	int conf_fromfile = 0;
	int rc;
	int cnt;
printf("in main!!!\n");
    /*
     * Process comand arguments, determine reader name
     * and verbosity level.
     */
     
	printf ("CS203 Demo Program v2.18.11 \n");
	if (ac < 2)
	{
		printf("\nUsage: %s [-conf filename] IPaddress [-tag] [-loop] [-power nnn] [-lbt 0|1] [-channel n] [-session s] [-sequence 0|1] [-target 0|1] [-toggle 0|1] [-portconf filename] [-algorithm a,p,p,p,p]\n", av[0]);
		printf("-conf : get setting from file, ignore others commane line setting\n");
		printf("-tag : Enable read tag (default : disable read tag)\n");
		printf("-loop : Loop forever (default : test 20 seconds)\n");
		printf("-power : set RF power (for CS203 only)\n");
		printf("-portconf : get port setting from file (for CS468 only)\n");
		printf("-write : write \"112233445566778899001122\" to EPC ID on last access tag (please use -tag)\n");
		printf("-lbt : set LBT function, 0=disable, 1=enable (default LBT disable)\n");
		printf("-channel : channel number (default 1)\n");
		printf("-session : session number (0-3)\n");
		printf("-sequence : 0=normal mode, 1=sequence mode\n");
		printf("-target : A=0, B=1\n");
		printf("-toggle : 1=Gen 2 A/B flag will be toggled after all arounds have been run on current target, 0=no toggle\n");
		printf("-algorithm : FixedQ=[0,QValue] DynamicQ=[1,StartQ,MinQ,MaxQ,ThreadholdMultiplier \n");
		
		exit(1);
	}


	CS203fd_first = handler_alloc ();
	if (CS203fd_first == NULL)
	{
   		printf ("Memory Allocation Error!\n");
   		exit (2);
	}
	
	//set default values
	CS203fd_first->power=300;
	CS203fd_first->multiport=0;
	CS203fd_first->query_session=0;
	CS203fd_first->query_target=0;
	CS203fd_first->toggle=1;
	CS203fd_first->sequenceMode=0;
	CS203fd_first->Algorithm=1;	//dynamicQ
	CS203fd_first->MaxQ=15;
	CS203fd_first->StartQ=7;
	CS203fd_first->MinQ=0;
	CS203fd_first->ThreadholdMultipler=4;

	// Check Command Line
	cnt = 1;
	while (cnt < ac)
	{
		if (av[cnt][0] == '-')
		{
			if (strcmp (&av[cnt][1], "conf") == 0)
			{
				conf_fromfile = 1;
				conf_Init (av[cnt + 1], CS203fd_first);
				break;
			}
			else if (strcmp (&av[cnt][1], "tag") == 0)
			{
		                printf("reads tag\n");	
                 		CS203fd_first->iConf_ReadTag = 1;
			}
			else if (strcmp (&av[cnt][1], "loop") == 0)
			{
					CS203fd_first->iConf_Forever = 1; // Test Forever
			}
			else if (strcmp (&av[cnt][1], "write") == 0)
			{
					CS203fd_first->iConf_WriteUser = 1;								
			}
			else if (strcmp (&av[cnt][1], "power") == 0)
			{
				int power;
				power = atoi (av[cnt + 1]);
				CS203fd_first->power=power;
			}
			else if (strcmp (&av[cnt][1], "portconf") == 0)
			{
				CS468_Portconf (av[cnt + 1],CS203fd_first);
				CS203fd_first->multiport=1;
			}
			else if (strcmp (&av[cnt][1], "sequence") == 0)
			{
				CS203fd_first->sequenceMode=atoi (av[cnt + 1]);
			}
			else if (strcmp (&av[cnt][1], "CN1") == 0)
			{
				CS203fd_first->Freq_CN = 1;
			}			
			else if (strcmp (&av[cnt][1], "CN2") == 0)
			{
				CS203fd_first->Freq_CN = 2;
			}			
			else if (strcmp (&av[cnt][1], "CN3") == 0)
			{
				CS203fd_first->Freq_CN = 3;
			}			
			else if (strcmp (&av[cnt][1], "CN4") == 0)
			{
				CS203fd_first->Freq_CN = 4;
			}
			else if (strcmp (&av[cnt][1], "lbt") == 0)
			{
				CS203fd_first->iConf_lbt = atoi (av[cnt + 1]);
			}
			else if (strcmp (&av[cnt][1], "channel") == 0)
			{
				CS203fd_first->iConf_channel = atoi (av[cnt + 1]);
			}
			else if (strcmp (&av[cnt][1], "session") == 0)
			{
				CS203fd_first->query_session = atoi (av[cnt + 1]);
			}
			else if (strcmp (&av[cnt][1], "target") == 0)
			{
				CS203fd_first->query_target = atoi (av[cnt + 1]);
			}
			else if (strcmp (&av[cnt][1], "toggle") == 0)
			{
				CS203fd_first->toggle= atoi (av[cnt + 1]);
			}
			else if (strcmp (&av[cnt][1], "algorithm") == 0)
			{
				char* pch;
				pch = strtok (av[cnt+1],",");
				if (atoi(pch) == 0)	//fixedQ
				{
					CS203fd_first->Algorithm=0;
					CS203fd_first->QValue=atoi(strtok (NULL, ","));
				}
				else	//dynamicQ
				{
					CS203fd_first->Algorithm=1;
					CS203fd_first->StartQ=atoi(strtok (NULL, ","));
					CS203fd_first->MinQ=atoi(strtok (NULL, ","));
					CS203fd_first->MaxQ=atoi(strtok (NULL, ","));
					CS203fd_first->ThreadholdMultipler=atoi(strtok (NULL, ","));
				}
			}
		}

		cnt ++;
	}
	
	if (conf_fromfile == 0)
		strncpy (CS203fd_first->ReaderHostName, av[1], 50 - 1); 

	if (GetLocalIP () != 1)  // Get Locatio IP and store in global variable
		return -11;

	/*
    * Run application, capture return value for exit status
   */
   CS203fd = CS203fd_first;
	while (CS203fd != NULL)
	{
	   rc = pthread_create (&(CS203fd->threadfd), NULL, run, (void*)CS203fd);
		CS203fd = CS203fd->NextHandle;
	}
	
	/*
    * Check Run application, capture return value for exit status
   */
   
   CS203fd = CS203fd_first;
	while (CS203fd != NULL)
	{
	   rc = pthread_join (CS203fd->threadfd, NULL);
		CS203fd = CS203fd->NextHandle;
	}

	printf("INFO: Done\n");

   /*
    * Exit with the right status.
   */
	if(rc)
   		exit (2);

	exit(0);
}
