/*
* @file: simple.c
* @author: chenhaoran
*  chrH323 test
*****************************************************************************/

#include "chrTypes.h"
#include "chrTrace.h"
#include "chrSocket.h"
#include "chrCapability.h"
#include "chrLogicalChannels.h"
#include "chrChannels.h"
#include "chrCallSession.h"
#include "chrH323Endpoint.h"
#include "chrH323Protocol.h"
#include "chrStackCmds.h"
#include <ctype.h>
#ifndef _WIN32
#include <pthread.h>
#endif

/** 1. media transfer callback */
int chrAudioStartReceiveChannel(chrCallData *call, CHRLogicalChannel *pChannel);
int chrAudioStartTransmitChannel(chrCallData *call, CHRLogicalChannel *pChannel);
int chrAudioStopReceiveChannel(chrCallData *call, CHRLogicalChannel *pChannel);
int chrAudioStopTransmitChannel(chrCallData *call, CHRLogicalChannel *pChannel);
int chrVideoStartReceiveChannel(chrCallData *call, CHRLogicalChannel *pChannel);
int chrVideoStartTransmitChannel(chrCallData *call, CHRLogicalChannel *pChannel);
int chrVideoStopReceiveChannel(chrCallData *call, CHRLogicalChannel *pChannel);
int chrVideoStopTransmitChannel(chrCallData *call, CHRLogicalChannel *pChannel);
/** 2. call proceduce callback */
int chrOnIncomingCall(chrCallData* call);    /**  called */
int chrOnMakeCall(chrCallData* call);        /**  caller or called */
int chrOnCallEstablished(chrCallData* call); /**  caller or called */
int chrOnCallCleared(chrCallData* call);     /**  caller or called */
int chrOnReceivedVideoFastUpdate(chrCallData *call, int channelNo);
int chrOnReceivedCommand(CHRH323CallData *call, H245CommandMessage *command);
/** 3. command handle callback */
void * chrHandleCommand(void*);


static char callerToken[20];
static char calledToken[60][20];
static char localip[20];
static OOBOOL bActive = FALSE;
static OOBOOL bIsCalled = FALSE;
static int calledCount = 0;
static int localH225Port = 1720;
static int localCmdPort = 8972;
static int audioRecvBasePort = 5000;
static int audioCtrlRecvBasePort = 5001;
static int audioTransBasePort = 5010;
static int audioCtrlTransBasePort = 5011;
static int videoRecvBasePort = 20000;
static int videoCtrlRecvBasePort = 20001;
static int videoTransBasePort = 20010;
static int videoCtrlTransBasePort = 20011;
static OOBOOL gCmdThrd;

static char gPlayFile[100];

int main(int argc, char ** argv)
{
	/* 1. configure socket h323 initial */
	int ret = 0;
	char user[50], user_num[50], destip[256];
	int trace_level = 0;
	CHRH323CALLBACKS h323Callbacks;
#ifdef _WIN32
	HANDLE threadHdl;
	//	const char* mediadll = "oomedia.dll";
#else
	pthread_t threadHdl;
	//	const char* mediadll = "liboomedia.so";
#endif
#ifdef _WIN32
	chrSocketInit(); /*Initialize the windows socket api  */
#endif

	gPlayFile[0] = '\0';
	localip[0] = '\0';
	destip[0] = '\0';
	user[0] = '\0';
	user_num[0] = '\0';

	for (int i = 0; i < 20; i++)
		calledToken[i][0] = '\0';
	gCmdThrd = FALSE;
	if (argc < 4)
	{
		printf("Useage: simple localip h225listenport cmdlistenport                         \n");
		printf("For example: simple 192.168.1.104 1720 8972                                 \n");
		return -1;
	}


	printf("Chen Haoran H323 protocol. No copyright. Because I copy from ooh323.       \n");
	printf("If you want to make a call, please input c destip:destport after 'CMD>'    \n");
	printf("If you want to receive a call, just wait.                                  \n");
	printf("If you want to hung a call, please input u after 'CMD>'                    \n");
	printf("If you want to hung a fixed number call, please input h number after 'CMD>'\n");
	printf("If you want to quit, please input q after 'CMD>'                           \n");

	strncpy(user, "leochen", sizeof(user)-1);
	user[sizeof(user)-1] = '\0';
	strncpy(user_num, "10000", sizeof(user_num)-1);
	user_num[sizeof(user_num)-1] = '\0';
	strncpy(localip, argv[1], sizeof(localip)-1);
	localip[sizeof(localip)-1] = '\0';
	localH225Port = atoi(argv[2]);
	localCmdPort = atoi(argv[3]);

	/** for test */
	destip[sizeof(destip)-1] = '\0';

	if (chrUtilsIsStrEmpty(localip))
	{
		chrGetLocalIPAddress(localip);
	}

	ret = chrH323EpInitialize(CHR_CALLMODE_VIDEOCALL, "simple.log", localCmdPort);
	if (ret != CHR_OK)
	{
		printf("Failed to initialize H.323 Endpoint\n");
		return -1;
	}
	chrH323EpSetTraceLevel(CHRTRCLVLDBG);
	chrH323EpSetLocalAddress(localip, localH225Port);

#ifdef _WIN32
	if (chrH323EpCreateCmdListener(0) != CHR_OK) /** command listener, localCmdPort(argv[3]) should assign here! */
	{
		printf("Failed to initialize Command Listener\n");
		return -1;
	}
#endif
	if (!chrUtilsIsStrEmpty(user))
		chrH323EpSetCallerID(user);
	if (!chrUtilsIsStrEmpty(user_num))
		chrH323EpSetCallingPartyNumber(user_num);

	/* 2. Set callbacks */
	h323Callbacks.onNewCallCreated = chrOnMakeCall;
	h323Callbacks.onAlerting = NULL;
	h323Callbacks.onIncomingCall = chrOnIncomingCall;
	h323Callbacks.onOutgoingCall = NULL;
	h323Callbacks.onCallEstablished = chrOnCallEstablished;
	h323Callbacks.onCallCleared = chrOnCallCleared;
	h323Callbacks.openLogicalChannels = NULL;
	h323Callbacks.onReceivedVideoFastUpdate = chrOnReceivedVideoFastUpdate;
	h323Callbacks.onReceivedCommand = chrOnReceivedCommand;

	chrH323EpSetH323Callbacks(h323Callbacks);

	/* 3. Add audio video capability */
	chrH323EpAddG711Capability(CHR_G711ULAW64K, 240, 240, CHRRXANDTX,
		&chrAudioStartReceiveChannel,
		&chrAudioStartTransmitChannel,
		&chrAudioStopReceiveChannel,
		&chrAudioStopTransmitChannel);

	CHRH264CapParams params = { 0 };
	params.maxBitRate = 10240;
	chrH323EpAddH264VideoCapability(CHR_H264VIDEO, &params, CHRRXANDTX,
		&chrVideoStartReceiveChannel, &chrVideoStartTransmitChannel,
		&chrVideoStopReceiveChannel, &chrVideoStopTransmitChannel);


	/* 4. Create H.225 Listener */
	ret = chrCreateH225Listener();
	if (ret != CHR_OK)
	{
		OOTRACEERR1("Failed to Create H.323 Listener");
		return -1;
	}

	/* 5. Make call */
	//memset(callToken, 0, 20);
	//chrMakeCall(destip, callToken, sizeof(callToken), NULL); 
	//bActive = TRUE;

	/* 6. Create a thread to handle command */
#ifdef _WIN32
	threadHdl = CreateThread(0, 0, (LPTHREAD_START_ROUTINE)chrHandleCommand,
		0, 0, 0);
#else
	pthread_create(&threadHdl, NULL, chrHandleCommand, NULL);
#endif

	/* 7. Handle message or command */
	chrMonitorChannels();

	/* 8. Destroy */
	if (gCmdThrd)
	{
#ifdef _WIN32
		TerminateThread(threadHdl, 0);
#else
		pthread_cancel(threadHdl);
		pthread_join(threadHdl, NULL);

#endif
	}
	printf("--->Destroying H323Ep\n");
	chrH323EpDestroy();
	return 0;
}

//int main(int argc, char ** argv)
//{
//	return 0;
//}

/** 1. Media transfer callback */
/* Callback to start transmit media channel */
int chrAudioStartTransmitChannel(chrCallData *call, CHRLogicalChannel *pChannel)
{
	printf("\nStarting transmit audio channel %s:%d", call->remoteIP, pChannel->remoteMediaPort);
	fflush(stdout);
	//chrCreateTransmitRTPChannel(call->remoteIP, pChannel->remoteMediaPort);
	//chrStartTransmitWaveFile(gPlayFile);
	return CHR_OK;
}
/* Callback to stop transmit media channel*/
int chrAudioStopTransmitChannel(chrCallData *call, CHRLogicalChannel *pChannel)
{
	printf("\nStopping transmit audio channel");
	fflush(stdout);
	//chrStopTransmitWaveFile();
	return CHR_OK;
}
/* Callback to start receive media channel */
int chrAudioStartReceiveChannel(chrCallData *call, CHRLogicalChannel *pChannel)
{
	printf("\nStarting receive audio channel at %s:%d", pChannel->localIP, pChannel->localRtpPort);
	fflush(stdout);
	//ooCreateReceiveRTPChannel(pChannel->localIP,pChannel->localRtpPort);
	//ooStartReceiveAudioAndPlayback();
	return CHR_OK;
}
/* Callback to stop receive media channel */
int chrAudioStopReceiveChannel(chrCallData *call, CHRLogicalChannel *pChannel)
{
	printf("\nStopping receive audio channel");
	fflush(stdout);
	//ooStopReceiveAudioAndPlayback();
	return CHR_OK;
}

/* Callback to start transmit media channel */
int chrVideoStartTransmitChannel(chrCallData *call, CHRLogicalChannel *pChannel)
{
	printf("\nStarting transmit video channel %s:%d", call->remoteIP, pChannel->remoteMediaPort);
	fflush(stdout);
	//chrCreateTransmitRTPChannel(call->remoteIP, pChannel->remoteMediaPort);
	//chrStartTransmitWaveFile(gPlayFile);
	return CHR_OK;
}
/* Callback to stop transmit media channel*/
int chrVideoStopTransmitChannel(chrCallData *call, CHRLogicalChannel *pChannel)
{
	printf("\nStopping transmit video channel");
	fflush(stdout);
	//chrStopTransmitWaveFile();
	return CHR_OK;
}
/* Callback to start receive media channel */
int chrVideoStartReceiveChannel(chrCallData *call, CHRLogicalChannel *pChannel)
{
	printf("\nStarting receive video channel at %s:%d", pChannel->localIP, pChannel->localRtpPort);
	int res = 0;
	char syscmd[256] = "./rtp_receiver ";
#ifdef _WIN32
	char rtpPort[10];
	_itoa_s(pChannel->localRtpPort,rtpPort,10,10);
	strcat(syscmd, rtpPort);
	strcat(syscmd, " &");
	printf("###### syscmd = %s, res = %d #######\n", syscmd, res);
	fflush(stdout);
	//system(syscmd);
#else
	char* rtpPort = (char *)malloc(sizeof(int)+1);
	memset(rtpPort, 0, sizeof(int)+1);
	sprintf(rtpPort, "%d", (int)pChannel->localRtpPort);
	// no itoa or _itoa or _itoa_s in ubuntu, so use "sprintf"
	strcat(syscmd, rtpPort);
	strcat(syscmd, " &");
	printf("###### syscmd = %s, res = %d #######\n", syscmd, res);
	fflush(stdout);
	//system(syscmd);
	free(rtpPort);
#endif
	return CHR_OK;
}
/* Callback to stop receive media channel */
int chrVideoStopReceiveChannel(chrCallData *call, CHRLogicalChannel *pChannel)
{
	printf("\nStopping receive video channel");
	fflush(stdout);
	//ooStopReceiveAudioAndPlayback();
	//system("killall rtp_receiver");
	return CHR_OK;
}


/** 2. Call proceduce callback */
int chrOnMakeCall(chrCallData* call)
{
	chrMediaInfo mediaInfo1, mediaInfo2, mediaInfo3, mediaInfo4;
	memset(&mediaInfo1, 0, sizeof(chrMediaInfo));
	memset(&mediaInfo2, 0, sizeof(chrMediaInfo));
	memset(&mediaInfo3, 0, sizeof(chrMediaInfo));
	memset(&mediaInfo4, 0, sizeof(chrMediaInfo));

	/* Configure mediainfo for transmit media channel of type G711 */
	mediaInfo1.lMediaCntrlPort = audioCtrlTransBasePort;
	mediaInfo1.lMediaPort = audioTransBasePort;
	strcpy(mediaInfo1.lMediaIP, call->localIP);
	strcpy(mediaInfo1.dir, "transmit");
	mediaInfo1.cap = CHR_G711ULAW64K;
	chrAddMediaInfo(call, mediaInfo1);
	audioCtrlTransBasePort += 2;
	audioTransBasePort += 2;
	/* Configure mediainfo for receive media channel of type G711 */
	mediaInfo2.lMediaCntrlPort = audioCtrlRecvBasePort;
	mediaInfo2.lMediaPort = audioRecvBasePort;
	strcpy(mediaInfo2.lMediaIP, call->localIP);
	strcpy(mediaInfo2.dir, "receive");
	mediaInfo2.cap = CHR_G711ULAW64K;
	chrAddMediaInfo(call, mediaInfo2);
	audioCtrlRecvBasePort += 2;
	audioRecvBasePort += 2;
	/* Configure mediainfo for transmit media channel of type H264 */
	mediaInfo3.lMediaCntrlPort = videoCtrlTransBasePort;
	mediaInfo3.lMediaPort = videoTransBasePort;
	strcpy(mediaInfo3.lMediaIP, call->localIP);
	strcpy(mediaInfo3.dir, "transmit");
	mediaInfo3.cap = CHR_H264VIDEO;
	chrAddMediaInfo(call, mediaInfo3);
	videoCtrlTransBasePort += 2;
	videoTransBasePort += 2;
	/* Configure mediainfo for receive media channel of type H264 */
	mediaInfo4.lMediaCntrlPort = videoCtrlRecvBasePort;
	mediaInfo4.lMediaPort = videoRecvBasePort;
	strcpy(mediaInfo4.lMediaIP, call->localIP);
	strcpy(mediaInfo4.dir, "receive");
	mediaInfo4.cap = CHR_H264VIDEO;
	chrAddMediaInfo(call, mediaInfo4);
	videoCtrlRecvBasePort += 2;
	videoRecvBasePort += 2;
	//if (!bIsCalled)
	strcpy(callerToken, call->callToken);
	return CHR_OK;
}

/* on incoming call callback */
int chrOnIncomingCall(chrCallData* call)
{
	if (!bActive)
	{
		bActive = TRUE;
	}
	if (!bIsCalled)
	{
		bIsCalled = TRUE;
	}
	//else
	//{
	//	chrHangCall(call->callToken, CHR_REASON_LOCAL_BUSY);
	//	printf("\n--->Incoming call Rejected - line busy");
	//	printf("\nCMD>");
	//	fflush(stdout);
	//	return CHR_OK;
	//}
	printf("\n--> Call->callToken = %s is incoming.\n", call->callToken);
	return CHR_OK;
}
/* on incoming call callback */
int chrOnCallEstablished(chrCallData* call)
{
	printf("\n--> Call->callToken = %s is established.\n", call->callToken);
	if (bIsCalled)
	{
		strcpy(calledToken[calledCount++], call->callToken);
		chrSendVideoFastUpdate(call->callToken);
	}		
	else
	{
		strcpy(callerToken, call->callToken);
		chrSendVideoFastUpdate(call->callToken);
	}		
	return CHR_OK;
}
/* CallCleared callback */
int chrOnCallCleared(chrCallData* call)
{
	if (bIsCalled)
	{
		for (int i = 0; i < calledCount; i++)
		{
			if (!strcmp(call->callToken, calledToken[calledCount]))
			{
				printf("\n--->Call %s Ended - %s\n", calledToken[calledCount], chrGetReasonCodeText(call->callEndReason));
				printf("\nCMD>");
				fflush(stdout);
			}
		}
		calledToken[calledCount][0] = '\0';
		calledCount--;
		if (calledCount == 0)
		{
			bActive = FALSE;
			bIsCalled = FALSE;
		}
		printf("\n--> Call->callToken = %s is cleared.\n", call->callToken);
		return CHR_OK;
	}
	else
	{
		if (!strcmp(call->callToken, callerToken))
		{
			printf("\n--->Call %s Ended - %s\n", callerToken, chrGetReasonCodeText(call->callEndReason));
			printf("\nCMD>");
			fflush(stdout);
		}
		callerToken[0] = '\0';
		bActive = FALSE;
		printf("\n--> Call->callToken = %s is cleared.\n", call->callToken);
		return CHR_OK;
	}

}

int chrOnReceivedVideoFastUpdate(chrCallData* call, int channelNo)
{
	return CHR_OK;
}

int chrOnReceivedCommand(CHRH323CallData *call, H245CommandMessage *command)
{
	return CHR_OK;
}
//#if 0
//void* chrHandleCommand(void* dummy)
//{
//	int ret;
//	char command[20];
//	gCmdThrd = TRUE;
//#ifndef _WIN32
//	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
//#endif
//	memset(command, 0, sizeof(command));
//	printf("Hit <ENTER> to Hang call\n");
//	fgets(command, 20, stdin);
//	if (bActive)
//	{
//		printf("Hanging up call\n");
//		ret = chrHangCall(callToken, CHR_REASON_LOCAL_CLEARED);
//	}
//	else {
//		printf("No active call\n");
//	}
//	printf("Hit <ENTER> to close stack\n");
//	fgets(command, 20, stdin);
//	printf("Closing down stack\n");
//	chrStopMonitor();
//	gCmdThrd = FALSE;
//	return dummy;
//}
//
//#endif

void* chrHandleCommand(void* dummy)
{
	int cmdLen, callNum = 100, i = 0;
	char command[256], *p = NULL, ch = 0, destip[256];
	CHRStkCmdStat stat;
	gCmdThrd = TRUE;
#ifndef _WIN32
	pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL);
#endif
	command[0] = '\0';

	while (ch != 'q')
	{
		printf("CMD>");
		fflush(stdout);
		memset(command, 0, sizeof(command));
		fgets(command, 256, stdin);
		cmdLen = strlen(command);
		for (p = command; (*p == ' ' || *p == '\t') && p < command + cmdLen; p++);

		if (*p == '\n')
			continue;

		if (p >= command + cmdLen)
		{
			printf("Invalid Command\n");
			continue;
		}

		ch = tolower(*p);
		if (*(p + 1) != ' ' && *(p + 1) != '\t' && *(p + 1) != '\n') {
			ch = 'i'; /* invalid command */
		}
		switch (ch)
		{
		case 'c':
			//if (bActive)
			//{
			//	printf("--->Can not make a new call while an active call is present\n");
			//	break;
			//}
			p++;
			while ((*p == ' ' || *p == '\t') && p<command + cmdLen) p++;
			if (p >= command + cmdLen || *p == '\n')
			{
				printf("--->Invalid Command\n");
				break;
			}
			i = 0;
			while (*p != ' ' && *p != '\t' && *p != '\n')
			{
				destip[i++] = *p;
				p++;
			}
			destip[i] = '\0';

			if ((stat = chrMakeCall(destip, callerToken, sizeof(callerToken), NULL)) != CHR_STKCMD_SUCCESS)
			{
				printf("--->Failed to place a call to %s \n", destip);
				printf("    Reason: %s\n", chrGetStkCmdStatusCodeTxt(stat));
			}
			else{
				printf("--->Calling %s \n", destip);
				bActive = TRUE;
			}
			break;
		case 'h':
			if (bActive)
			{
				p++;
				callNum = atoi(p);
				if (callNum > calledCount)
				{
					printf("allNum > calledCount or no number!\n");
					break;
				}
				if (calledToken[callNum][0] == '\0')
				{
					printf("callNum does not exist!\n");
					break;
				}
				if ((stat = chrHangCall(calledToken[callNum], CHR_REASON_LOCAL_CLEARED)) != CHR_OK)
				{
					printf("Failed to hang call %s\n", calledToken[callNum]);
					printf("Reason: %s\n", chrGetStkCmdStatusCodeTxt(stat));
				}
				else
				{
					printf("Hanging call %s\n", calledToken[callNum]);
				}
			}
			else
			{
				printf("No active call to hang\n");
			}
			break;
		case 'u':
			if (bActive)
			{
				if ((stat = chrHangCall(callerToken, CHR_REASON_LOCAL_CLEARED)) != CHR_OK)
				{
					printf("Failed to hang call %s\n", callerToken);
					printf("Reason: %s\n", chrGetStkCmdStatusCodeTxt(stat));
				}
				else
				{
					printf("Hanging call %s\n", callerToken);
				}
			}
			else
			{
				printf("No active call to hang\n");
			}
			break;
		case 'q':
			if (bActive)
			{
				printf("Quiting After Hanging All Active Call %s\n");
				for (int i = 0; i < 20; i++)
				{
					if (calledToken[i][0] != '\0')
					{
						if ((stat = chrHangCall(calledToken[i], CHR_REASON_LOCAL_CLEARED)) != CHR_STKCMD_SUCCESS)
						{
							printf("Quit failed to hang call %s\n", calledToken[i]);
							printf("Quit reason: %s\n", chrGetStkCmdStatusCodeTxt(stat));
						}
					}
				}
			}
			break;
		case 'i':
		default:
			printf("Invalid Command\n");
		}
	}

	printf("--->Closing down stack\n");
	fflush(stdout);
	chrStopMonitor();
	gCmdThrd = FALSE;
	return dummy;
}

