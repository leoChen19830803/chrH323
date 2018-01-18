
#include <string.h>
#include "chrTrace.h"
#include "chrTypes.h"
#include "chrCapability.h"
#include "chrCallSession.h"
#include "chrH323Endpoint.h"
#include "chrStackCmds.h"

/** Global endpoint structure */
chrEndPoint gH323ep;

#ifdef _WIN32
extern int gCmdPort;
extern char gCmdIP[20];
#endif
extern DList g_TimerList;

static int setTraceFilename(const char* tracefile)
{
	if (!chrUtilsIsStrEmpty(tracefile)) {
		strncpy(gH323ep.traceFile, tracefile, MAXFILENAME - 1);
		gH323ep.traceFile[MAXFILENAME - 1] = '\0';
		if (strlen(tracefile) >= MAXFILENAME) {
			printf("ERROR: File name longer than allowed maximum %d\n",
				MAXFILENAME - 1);

			return CHR_FAILED;
		}
	}
	else {
		strcpy(gH323ep.traceFile, DEFAULT_TRACEFILE);
	}

	return 0;
}

int chrH323EpInitialize(enum CHRCallMode callMode, const char* tracefile, int commandPort)
{
	memset(&gH323ep, 0, sizeof(chrEndPoint));

	initContext(&(gH323ep.ctxt));
	initContext(&(gH323ep.msgctxt));

	if (0 != setTraceFilename(tracefile)) {
		return CHR_FAILED;
	}

	gH323ep.fptraceFile = fopen(gH323ep.traceFile, "a");
	if (gH323ep.fptraceFile == NULL)
	{
		printf("Error:Failed to open trace file %s for write.\n",
			gH323ep.traceFile);
		return CHR_FAILED;
	}

	/* Initialize default port ranges that will be used by stack.
	Apps can override these by explicitely setting port ranges
	*/

	gH323ep.tcpPorts.start = TCPPORTSSTART;
	gH323ep.tcpPorts.max = TCPPORTSEND;
	gH323ep.tcpPorts.current = TCPPORTSSTART;

	gH323ep.udpPorts.start = UDPPORTSSTART;
	gH323ep.udpPorts.max = UDPPORTSEND;
	gH323ep.udpPorts.current = UDPPORTSSTART;

	gH323ep.rtpPorts.start = RTPPORTSSTART;
	gH323ep.rtpPorts.max = RTPPORTSEND;
	gH323ep.rtpPorts.current = RTPPORTSSTART;

	CHR_SETFLAG(gH323ep.flags, CHR_M_FASTSTART);
	CHR_SETFLAG(gH323ep.flags, CHR_M_TUNNELING);
	CHR_SETFLAG(gH323ep.flags, CHR_M_AUTOANSWER);
	CHR_CLRFLAG(gH323ep.flags, CHR_M_GKROUTED);

	gH323ep.aliases = NULL;
	gH323ep.termType = DEFAULT_TERMTYPE;
	gH323ep.t35CountryCode = DEFAULT_T35COUNTRYCODE;
	gH323ep.t35Extension = DEFAULT_T35EXTENSION;
	gH323ep.manufacturerCode = DEFAULT_MANUFACTURERCODE;
	gH323ep.productID = DEFAULT_PRODUCTID;
	gH323ep.versionID = "v1.0";
	gH323ep.callType = T_H225CallType_pointToPoint;
	chrGetLocalIPAddress(gH323ep.signallingIP);
	gH323ep.listenPort = DEFAULT_H323PORT;
	gH323ep.listener = NULL;

	chrH323EpSetCallerID(DEFAULT_CALLERID);

	gH323ep.myCaps = NULL;
	gH323ep.noOfCaps = 0;
	gH323ep.callList = NULL;
	gH323ep.callingPartyNumber[0] = '\0';
	gH323ep.callMode = callMode;
	gH323ep.bearercap = Q931TransferUnrestrictedDigital;

	dListInit(&g_TimerList);/* This is for test application chansetup only*/

	gH323ep.callEstablishmentTimeout = DEFAULT_CALLESTB_TIMEOUT;

	gH323ep.msdTimeout = DEFAULT_MSD_TIMEOUT;

	gH323ep.tcsTimeout = DEFAULT_TCS_TIMEOUT;

	gH323ep.logicalChannelTimeout = DEFAULT_LOGICALCHAN_TIMEOUT;

	gH323ep.sessionTimeout = DEFAULT_ENDSESSION_TIMEOUT;
	gH323ep.ifList = NULL;

	chrSetTraceThreshold(CHRTRCLVLINFO);
	CHR_SETFLAG(gH323ep.flags, CHR_M_ENDPOINTCREATED);

	gH323ep.cmdSock = 0;
#ifdef _WIN32
	gH323ep.cmdListener = 0;
	gH323ep.cmdPort = commandPort/*CHR_DEFAULT_CMDLISTENER_PORT*/;
	gCmdPort = commandPort;
#endif

	CHR_SETFLAG(gH323ep.flags, CHR_M_AUTOANSWER);
	CHR_CLRFLAG(gH323ep.flags, CHR_M_FASTSTART);
	CHR_CLRFLAG(gH323ep.flags, CHR_M_TUNNELING);
	CHR_SETFLAG(gH323ep.flags, CHR_M_DISABLEGK);
	return CHR_OK;
}

int chrH323EpDestroy(void)
{
	/* free any internal memory allocated
	close trace file free context structure
	*/
	CHRH323CallData * cur, *temp;
	if (CHR_TESTFLAG(gH323ep.flags, CHR_M_ENDPOINTCREATED))
	{
		CHRTRACEINFO1("Destroying H323 Endpoint\n");
		if (gH323ep.callList)
		{
			cur = gH323ep.callList;
			while (cur)
			{
				temp = cur;
				cur = cur->next;
				temp->callEndReason = CHR_REASON_LOCAL_CLEARED;
				chrCleanCall(temp);
			}
			gH323ep.callList = NULL;
		}


		if (gH323ep.listener)
		{
			chrSocketClose(*(gH323ep.listener));
			gH323ep.listener = NULL;
		}
#ifdef _WIN32
		if (gH323ep.cmdListener != 0)
		{
			chrSocketClose(gH323ep.cmdListener);
			gH323ep.cmdListener = 0;
		}
#endif
		//ooGkClientDestroy();

		if (gH323ep.fptraceFile)
		{
			fclose(gH323ep.fptraceFile);
			gH323ep.fptraceFile = NULL;
		}

		freeContext(&(gH323ep.ctxt));

		CHR_CLRFLAG(gH323ep.flags, CHR_M_ENDPOINTCREATED);
	}
	return CHR_OK;
}

int chrH323EpSetBearerCap(const char* configText)
{
	if (!strcasecmp(configText, "unrestricted_digital") ||
		!strcasecmp(configText, "unrestricteddigital")) {
		gH323ep.bearercap = Q931TransferUnrestrictedDigital;
	}
	else if (!strcasecmp(configText, "speech")) {
		gH323ep.bearercap = Q931TransferSpeech;
	}
	else {
		CHRTRACEERR2("ERROR: invalid/unsupported value %s specified for "
			"bearercap\n", configText);
		return CHR_FAILED;
	}

	return CHR_OK;
}


int chrH323EpSetLocalAddress(const char* localip, int listenport)
{
	if (localip)
	{
		strcpy(gH323ep.signallingIP, localip);
#ifdef _WIN32
		strcpy(gCmdIP, localip);
#endif
		CHRTRACEINFO2("Signalling IP address is set to %s\n", localip);
	}

	if (listenport)
	{
		gH323ep.listenPort = listenport;
		CHRTRACEINFO2("Listen port number is set to %d\n", listenport);
	}
	return CHR_OK;
}

#ifdef _WIN32
int chrH323EpCreateCmdListener(int cmdPort)
{
	if (cmdPort != 0)
	{
		gH323ep.cmdPort = cmdPort;
		gCmdPort = cmdPort;
	}
	if (chrCreateCmdListener() != CHR_OK)
		return CHR_FAILED;

	return CHR_OK;
}
#endif

int chrH323EpAddAliasH323ID(const char *h323id)
{
	chrAliases * psNewAlias = NULL;
	psNewAlias = (chrAliases*)memAlloc(&gH323ep.ctxt, sizeof(chrAliases));
	if (!psNewAlias)
	{
		CHRTRACEERR1("Error: Failed to allocate memory for new H323-ID alias\n");
		return CHR_FAILED;
	}
	psNewAlias->type = T_H225AliasAddress_h323_ID;
	psNewAlias->registered = FALSE;
	psNewAlias->value = (char*)memAlloc(&gH323ep.ctxt, strlen(h323id) + 1);
	if (!psNewAlias->value)
	{
		CHRTRACEERR1("Error: Failed to allocate memory for the new H323-ID alias "
			"value\n");
		memFreePtr(&gH323ep.ctxt, psNewAlias);
		return CHR_FAILED;
	}
	strcpy(psNewAlias->value, h323id);
	psNewAlias->next = gH323ep.aliases;
	gH323ep.aliases = psNewAlias;
	CHRTRACEDBG2("Added alias: H323ID - %s\n", h323id);
	return CHR_OK;
}

int chrH323EpAddAliasDialedDigits(const char* dialedDigits)
{
	chrAliases * psNewAlias = NULL;
	psNewAlias = (chrAliases*)memAlloc(&gH323ep.ctxt, sizeof(chrAliases));
	if (!psNewAlias)
	{
		CHRTRACEERR1("Error: Failed to allocate memory for new DialedDigits "
			"alias\n");
		return CHR_FAILED;
	}
	psNewAlias->type = T_H225AliasAddress_dialedDigits;
	psNewAlias->registered = FALSE;
	psNewAlias->value = (char*)memAlloc(&gH323ep.ctxt, strlen(dialedDigits) + 1);
	if (!psNewAlias->value)
	{
		CHRTRACEERR1("Error: Failed to allocate memory for the new DialedDigits"
			" alias value\n");
		memFreePtr(&gH323ep.ctxt, psNewAlias);
		return CHR_FAILED;
	}
	strcpy(psNewAlias->value, dialedDigits);
	psNewAlias->next = gH323ep.aliases;
	gH323ep.aliases = psNewAlias;
	CHRTRACEDBG2("Added alias: DialedDigits - %s\n", dialedDigits);
	return CHR_OK;
}

int chrH323EpAddAliasTransportID(const char * ipaddress)
{
	chrAliases * psNewAlias = NULL;
	psNewAlias = (chrAliases*)memAlloc(&gH323ep.ctxt, sizeof(chrAliases));
	if (!psNewAlias)
	{
		CHRTRACEERR1("Error: Failed to allocate memory for new Transport-ID "
			"alias\n");
		return CHR_FAILED;
	}
	psNewAlias->type = T_H225AliasAddress_transportID;
	psNewAlias->registered = FALSE;
	psNewAlias->value = (char*)memAlloc(&gH323ep.ctxt, strlen(ipaddress) + 1);
	if (!psNewAlias->value)
	{
		CHRTRACEERR1("Error: Failed to allocate memory for the new Transport-ID "
			"alias value\n");
		memFreePtr(&gH323ep.ctxt, psNewAlias);
		return CHR_FAILED;
	}
	strcpy(psNewAlias->value, ipaddress);
	psNewAlias->next = gH323ep.aliases;
	gH323ep.aliases = psNewAlias;
	CHRTRACEDBG2("Added alias: Transport-ID - %s\n", ipaddress);
	return CHR_OK;
}

int chrH323EpClearAllAliases(void)
{
	chrAliases *pAlias = NULL, *pTemp;
	if (gH323ep.aliases)
	{
		pAlias = gH323ep.aliases;
		while (pAlias)
		{
			pTemp = pAlias;
			pAlias = pAlias->next;
			memFreePtr(&gH323ep.ctxt, pTemp);
		}
		gH323ep.aliases = NULL;
	}
	return CHR_OK;
}


int chrH323EpSetH225MsgCallbacks(CHRH225MsgCallbacks h225Callbacks)
{
	gH323ep.h225Callbacks.onReceivedSetup = h225Callbacks.onReceivedSetup;
	gH323ep.h225Callbacks.onReceivedConnect = h225Callbacks.onReceivedConnect;
	gH323ep.h225Callbacks.onBuiltSetup = h225Callbacks.onBuiltSetup;
	gH323ep.h225Callbacks.onBuiltConnect = h225Callbacks.onBuiltConnect;

	return CHR_OK;
}

int chrH323EpSetH323Callbacks(CHRH323CALLBACKS h323Callbacks)
{
	gH323ep.h323Callbacks.onNewCallCreated = h323Callbacks.onNewCallCreated;
	gH323ep.h323Callbacks.onAlerting = h323Callbacks.onAlerting;
	gH323ep.h323Callbacks.onIncomingCall = h323Callbacks.onIncomingCall;
	gH323ep.h323Callbacks.onOutgoingCall = h323Callbacks.onOutgoingCall;
	gH323ep.h323Callbacks.onCallEstablished = h323Callbacks.onCallEstablished;
	gH323ep.h323Callbacks.onCallCleared = h323Callbacks.onCallCleared;
	gH323ep.h323Callbacks.openLogicalChannels = h323Callbacks.openLogicalChannels;
	gH323ep.h323Callbacks.onReceivedCommand = h323Callbacks.onReceivedCommand;
	gH323ep.h323Callbacks.onReceivedVideoFastUpdate = h323Callbacks.onReceivedVideoFastUpdate;
	return CHR_OK;
}


int chrH323EpEnableMediaWaitForConnect(void)
{
	CHR_SETFLAG(gH323ep.flags, CHR_M_MEDIAWAITFORCONN);
	return CHR_OK;
}

int chrH323EpDisableMediaWaitForConnect(void)
{
	CHR_CLRFLAG(gH323ep.flags, CHR_M_MEDIAWAITFORCONN);
	return CHR_OK;
}

int chrH323EpSetTermType(int value)
{
	gH323ep.termType = value;
	return CHR_OK;
}

int chrH323EpSetProductID(const char* productID)
{
	if (0 != productID) {
		char* pstr = (char*)memAlloc(&gH323ep.ctxt, strlen(productID) + 1);
		strcpy(pstr, productID);
		if (gH323ep.productID)
			memFreePtr(&gH323ep.ctxt, gH323ep.productID);
		gH323ep.productID = pstr;
		return CHR_OK;
	}
	else return CHR_FAILED;
}

int chrH323EpSetVersionID(const char* versionID)
{
	if (0 != versionID) {
		char* pstr = (char*)memAlloc(&gH323ep.ctxt, strlen(versionID) + 1);
		strcpy(pstr, versionID);
		if (gH323ep.versionID)
			memFreePtr(&gH323ep.ctxt, gH323ep.versionID);
		gH323ep.versionID = pstr;
		return CHR_OK;
	}
	else return CHR_FAILED;
}

int chrH323EpSetCallerID(const char* callerID)
{
	if (0 != callerID) {
		char* pstr = (char*)memAlloc(&gH323ep.ctxt, strlen(callerID) + 1);
		strcpy(pstr, callerID);
		if (gH323ep.callerid)
			memFreePtr(&gH323ep.ctxt, gH323ep.callerid);
		gH323ep.callerid = pstr;
		return CHR_OK;
	}
	else return CHR_FAILED;
}

int chrH323EpSetCallingPartyNumber(const char* number)
{
	int ret = CHR_OK;
	if (number)
	{
		strncpy(gH323ep.callingPartyNumber, number,
			sizeof(gH323ep.callingPartyNumber) - 1);
		ret = chrH323EpAddAliasDialedDigits((char*)number);
		return ret;
	}
	else return CHR_FAILED;
}

int chrH323EpSetTraceLevel(int traceLevel)
{
	chrSetTraceThreshold(traceLevel);
	return CHR_OK;
}

int chrH323EpAddG711Capability(int cap, int txframes, int rxframes, int dir,
	cb_StartReceiveChannel startReceiveChannel,
	cb_StartTransmitChannel startTransmitChannel,
	cb_StopReceiveChannel stopReceiveChannel,
	cb_StopTransmitChannel stopTransmitChannel)
{
	return chrCapabilityAddSimpleCapability(NULL, cap, txframes, rxframes, FALSE,
		dir, startReceiveChannel, startTransmitChannel,
		stopReceiveChannel, stopTransmitChannel, FALSE);
}

int chrH323EpAddH264VideoCapability(int cap, CHRH264CapParams *capParams, int dir,
	cb_StartReceiveChannel startReceiveChannel,
	cb_StartTransmitChannel startTransmitChannel,
	cb_StopReceiveChannel stopReceiveChannel,
	cb_StopTransmitChannel stopTransmitChannel){

	return chrCapabilityAddH264VideoCapability(NULL, capParams, dir,
		startReceiveChannel, startTransmitChannel,
		stopReceiveChannel, stopTransmitChannel,
		FALSE);

}


/* 0-1024 are reserved for well known services */
int chrH323EpSetTCPPortRange(int base, int max)
{
	if (base <= 1024)
		gH323ep.tcpPorts.start = 1025;
	else
		gH323ep.tcpPorts.start = base;
	if (max > 65500)
		gH323ep.tcpPorts.max = 65500;
	else
		gH323ep.tcpPorts.max = max;

	if (gH323ep.tcpPorts.max<gH323ep.tcpPorts.start)
	{
		CHRTRACEERR1("Error: Failed to set tcp ports- "
			"Max port number less than Start port number\n");
		return CHR_FAILED;
	}
	gH323ep.tcpPorts.current = gH323ep.tcpPorts.start;

	CHRTRACEINFO1("TCP port range initialize - successful\n");
	return CHR_OK;
}

int chrH323EpSetUDPPortRange(int base, int max)
{
	if (base <= 1024)
		gH323ep.udpPorts.start = 1025;
	else
		gH323ep.udpPorts.start = base;
	if (max > 65500)
		gH323ep.udpPorts.max = 65500;
	else
		gH323ep.udpPorts.max = max;

	if (gH323ep.udpPorts.max<gH323ep.udpPorts.start)
	{
		CHRTRACEERR1("Error: Failed to set udp ports- Max port number"
			" less than Start port number\n");
		return CHR_FAILED;
	}

	gH323ep.udpPorts.current = gH323ep.udpPorts.start;

	CHRTRACEINFO1("UDP port range initialize - successful\n");

	return CHR_OK;
}

int chrH323EpSetRTPPortRange(int base, int max)
{
	if (base <= 1024)
		gH323ep.rtpPorts.start = 1025;
	else
		gH323ep.rtpPorts.start = base;
	if (max > 65500)
		gH323ep.rtpPorts.max = 65500;
	else
		gH323ep.rtpPorts.max = max;

	if (gH323ep.rtpPorts.max<gH323ep.rtpPorts.start)
	{
		CHRTRACEERR1("Error: Failed to set rtp ports- Max port number"
			" less than Start port number\n");
		return CHR_FAILED;
	}

	gH323ep.rtpPorts.current = gH323ep.rtpPorts.start;
	CHRTRACEINFO1("RTP port range initialize - successful\n");
	return CHR_OK;
}
