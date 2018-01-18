#include "stdio.h"
#include "chrTypes.h"
#include "chrTrace.h"
#include "chrSocket.h"
#include "chrChannels.h"
#include "chrCallSession.h"
#include "printHandler.h"
#include "chrH323Protocol.h"
#include "chrH323Endpoint.h"
#include "chrStackCmds.h"



extern CHRH323EndPoint gH323ep; /** < global endpoint*/

extern DList g_TimerList;
static OOBOOL gMonitor = FALSE;
static int clearCall(CHRH323CallData* call, CHRCallClearReason reason, ASN1OCTET* pmsgbuf);
int chrSetFDSETs(fd_set *pReadfds, fd_set *pWritefds, int *nfds);
int chrProcessFDSETsAndTimers(fd_set *pReadfds, fd_set *pWritefds, struct timeval *pToMin);
/** H.225 */
int chrCreateH225Listener()
/**
 create->bind->listen
*/
{
	int ret = 0;
	CHRSOCKET channelSocket = 0;
	CHRIPADDR ipaddrs;

	if ((ret = chrSocketCreate(&channelSocket)) != ASN_OK)
	{
		CHRTRACEERR1("Failed to create socket for H323 Listener\n");
		return CHR_FAILED;
	}
	ret = chrSocketStrToAddr(gH323ep.signallingIP, &ipaddrs);

	if ((ret = chrSocketBind(channelSocket, ipaddrs, gH323ep.listenPort)) == ASN_OK)
	{
		gH323ep.listener = (CHRSOCKET*)memAlloc(&gH323ep.ctxt, sizeof(CHRSOCKET));
		*(gH323ep.listener) = channelSocket;
		chrSocketListen(channelSocket, 20); /* should to improve 20 on performance test*/
		CHRTRACEINFO1("H323 listener creation - successful\n");
		return CHR_OK;
	}
	else
	{
		CHRTRACEERR1("ERROR:Failed to create H323 listener\n");
		return CHR_FAILED;
	}
}
int chrCloseH225Listener(void)
{
	/** todo: */
	return 0;
}
int chrCreateH225Connection(CHRH323CallData *call)
/**
create->bind(specify port)->connect
*/
{
	int ret = 0;
	CHRSOCKET channelSocket = 0;
	if ((ret = chrSocketCreate(&channelSocket)) != ASN_OK)
	{
		CHRTRACEERR3("Failed to create socket for transmit H2250 channel (%s, %s)"
			"\n", call->callType, call->callToken);
		if (call->callState < CHR_CALL_CLEAR)
		{
			call->callState = CHR_CALL_CLEAR;
			call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
		}
		return CHR_FAILED;
	}
	else
	{
#ifndef _WIN32
		ret = chrBindPort(CHRTCP, channelSocket, call->localIP);
#else
		ret = chrBindOSAllocatedPort(channelSocket, call->localIP); /** bind any port */
#endif

		if (ret == CHR_FAILED)
		{
			CHRTRACEERR3("Error:Unable to bind to a TCP port (%s, %s)\n",
				call->callType, call->callToken);
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callState = CHR_CALL_CLEAR;
				call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
			}
			return CHR_FAILED;
		}

		if (0 == call->pH225Channel) {
			call->pH225Channel =
				(CHRH323Channel*)memAllocZ(call->pctxt, sizeof(CHRH323Channel));
		}
		call->pH225Channel->port = ret;

		CHRTRACEINFO5("Trying to connect to remote endpoint(%s:%d) to setup "
			"H2250 channel (%s, %s)\n", call->remoteIP,
			call->remotePort, call->callType, call->callToken);

		if ((ret = chrSocketConnect(channelSocket, call->remoteIP,
			call->remotePort)) == ASN_OK)
		{
			call->pH225Channel->sock = channelSocket;

			CHRTRACEINFO3("H2250 transmiter channel creation - successful "
				"(%s, %s)\n", call->callType, call->callToken);

			/* If multihomed, get ip from socket */
			if (!strcmp(call->localIP, "0.0.0.0"))
			{
				CHRTRACEDBG3("Determining IP address for outgoing call in "
					"multihomed mode. (%s, %s)\n", call->callType,
					call->callToken);
				ret = chrSocketGetIpAndPort(channelSocket, call->localIP, 20,
					&call->pH225Channel->port);
				if (ret != ASN_OK)
				{
					CHRTRACEERR3("ERROR:Failed to retrieve local ip and port from "
						"socket for multihomed mode.(%s, %s)\n",
						call->callType, call->callToken);
					if (call->callState < CHR_CALL_CLEAR)
					{  /* transport failure */
						call->callState = CHR_CALL_CLEAR;
						call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
					}
					return CHR_FAILED;
				}
				CHRTRACEDBG4("Using local ip %s for outgoing call(multihomedMode)."
					" (%s, %s)\n", call->localIP, call->callType,
					call->callToken);
			}
			return CHR_OK;
		}
		else
		{
			CHRTRACEERR3("ERROR:Failed to connect to remote destination for "
				"transmit H2250 channel(%s, %s)\n", call->callType,
				call->callToken);
			if (call->callState < CHR_CALL_CLEAR)
			{  /* No one is listening at remote end */
				call->callState = CHR_CALL_CLEAR;
				call->callEndReason = CHR_REASON_NOUSER;
			}
			return CHR_FAILED;
		}

		return CHR_FAILED;
	}
}

int chrAcceptH225Connection()
{
	CHRH323CallData * call;
	int ret;
	char callToken[20];
	CHRSOCKET h225Channel = 0;
	ret = chrSocketAccept(*(gH323ep.listener), &h225Channel,
		NULL, NULL);
	if (ret != ASN_OK)
	{
		CHRTRACEERR1("Error:Accepting h225 connection\n");
		return CHR_FAILED;
	}
	chrGenerateCallToken(callToken, sizeof(callToken));

	call = chrCreateCall("incoming", callToken, NULL);
	if (!call)
	{
		CHRTRACEERR1("ERROR:Failed to create an incoming call\n");
		return CHR_FAILED;
	}

	call->pH225Channel = (CHRH323Channel*)
		memAllocZ(call->pctxt, sizeof(CHRH323Channel));

	call->pH225Channel->sock = h225Channel;

	/* If multihomed, get ip from socket */
	if (!strcmp(call->localIP, "0.0.0.0"))
	{
		CHRTRACEDBG3("Determining IP address for incoming call in multihomed "
			"mode (%s, %s)\n", call->callType, call->callToken);

		ret = chrSocketGetIpAndPort(h225Channel, call->localIP, 20,
			&call->pH225Channel->port);
		if (ret != ASN_OK)
		{
			CHRTRACEERR3("Error:Failed to retrieve local ip and port from "
				"socket for multihomed mode.(%s, %s)\n",
				call->callType, call->callToken);
			if (call->callState < CHR_CALL_CLEAR)
			{  /* transport failure */
				call->callState = CHR_CALL_CLEAR;
				call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
			}
			return CHR_FAILED;
		}
		CHRTRACEDBG4("Using Local IP address %s for incoming call in multihomed "
			"mode. (%s, %s)\n", call->localIP, call->callType,
			call->callToken);
	}

	return CHR_OK;
}

int chrCloseH225Connection(CHRH323CallData *call)
{
	if (0 != call->pH225Channel)
	{
		if (call->pH225Channel->sock != 0)
			chrSocketClose(call->pH225Channel->sock);
		if (call->pH225Channel->outQueue.count > 0)
		{
			dListFreeAll(call->pctxt, &(call->pH225Channel->outQueue));
		}
		memFreePtr(call->pctxt, call->pH225Channel);
		call->pH225Channel = NULL;
	}
	return CHR_OK;
}
int chrSendH225MsgtoQueue(CHRH323CallData *call, Q931Message *msg)
{
	int iRet = 0;
	ASN1OCTET * encodebuf;
	if (!call)
		return CHR_FAILED;

	encodebuf = (ASN1OCTET*)memAlloc(call->pctxt, MAXMSGLEN);
	if (!encodebuf)
	{
		CHRTRACEERR3("Error:Failed to allocate memory for encoding H225 "
			"message(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	iRet = chrEncodeH225Message(call, msg, encodebuf, MAXMSGLEN);
	if (iRet != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to encode H225 message. (%s, %s)\n",
			call->callType, call->callToken);
		memFreePtr(call->pctxt, encodebuf);
		return CHR_FAILED;
	}

	if (encodebuf[0] == CHRReleaseComplete || (encodebuf[0] == CHRFacility && encodebuf[1] == CHREndSessionCommand)) /** high priority messages*/
	{
		dListFreeAll(call->pctxt, &call->pH225Channel->outQueue);
		dListAppend(call->pctxt, &call->pH225Channel->outQueue, encodebuf);
	}
	else
	{
		dListAppend(call->pctxt, &call->pH225Channel->outQueue, encodebuf);

		CHRTRACEDBG4("Queued H225 messages %d. (%s, %s)\n",
			call->pH225Channel->outQueue.count,
			call->callType, call->callToken);
	}
	return CHR_OK;
}
int chrReceiveH225Msg(CHRH323CallData *call)
{
	int  recvLen = 0, total = 0, ret = 0;
	ASN1OCTET msglenbuf[4];
	ASN1OCTET* pmsgbuf;
	int len;
	Q931Message *pmsg;
	OOCTXT *pctxt = &gH323ep.msgctxt;
	struct timeval timeout;
	fd_set readfds;

	recvLen = chrSocketRecv(call->pH225Channel->sock, msglenbuf, 4);/* TPKT header 4 bytes */
	if (recvLen <= 0)
	{
		if (recvLen == 0)
			CHRTRACEWARN3("Warn:RemoteEndpoint closed connection (%s, %s)\n", call->callType, call->callToken);
		else
			CHRTRACEERR3("Error:Transport failure while reading Q931 message (%s, %s)\n", call->callType, call->callToken);
		chrCloseH225Connection(call);
		if (call->callState < CHR_CALL_CLEARED)
		{
			if (call->callState < CHR_CALL_CLEAR)
				call->callEndReason = CHR_REASON_TRANSPORTFAILURE;

			call->callState = CHR_CALL_CLEARED;
		}
		return CHR_OK;
	}
	CHRTRACEDBG3("Receiving H.2250 message (%s, %s)\n", call->callType, call->callToken);
	/* Since we are working with TCP, need to determine the
	message boundary. Has to be done at channel level, as channels
	know the message formats and can determine boundaries
	*/
	if (recvLen != 4)
	{
		CHRTRACEERR4("Error: Reading TPKT header for H225 message recvLen= %d (%s, %s)\n", recvLen, call->callType,	call->callToken);

		return clearCall(call, CHR_REASON_INVALIDMESSAGE, 0);
	}

	len = msglenbuf[2];
	len = len << 8;
	len = len | msglenbuf[3];
	len = len - 4;

	CHRTRACEDBG2("H.2250 message length is %d\n", len);

	pmsgbuf = (ASN1OCTET*)memAlloc(pctxt, len);
	if (0 == pmsgbuf) 
	{
		CHRTRACEERR3("ERROR: Failed to allocate memory for incoming H.2250 message (%s, %s)\n", call->callType, call->callToken);
		memReset(&gH323ep.msgctxt);
		return CHR_FAILED;
	}

	/* Now read actual Q931 message body. We should make sure that we
	receive complete message as indicated by len. If we don't then there
	is something wrong. The lchrp below receives message, then checks whether
	complete message is received. If not received, then uses select to peek
	for remaining bytes of the message. If message is not received in 3
	seconds, then we have a problem. Report an error and exit.
	*/
	while (total < len)
	{
		recvLen = chrSocketRecv
			(call->pH225Channel->sock, pmsgbuf + total, len - total);

		total += recvLen;

		if (total == len) break; /* Complete message is received */

		FD_ZERO(&readfds);
		FD_SET(call->pH225Channel->sock, &readfds);
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;
		ret = chrSocketSelect(call->pH225Channel->sock + 1, &readfds, NULL,
			NULL, &timeout);
		if (ret == -1)
		{
			CHRTRACEERR3("Error in select while receiving H.2250 message - clearing call (%s, %s)\n", call->callType, call->callToken);
			return clearCall(call, CHR_REASON_TRANSPORTFAILURE, pmsgbuf);
		}
		/* If remaining part of the message is not received in 3 seconds
		exit */
		if (!FD_ISSET(call->pH225Channel->sock, &readfds))
		{
			CHRTRACEERR3("Error: Incomplete H.2250 message received - clearing "
				"call (%s, %s)\n", call->callType, call->callToken);

			return clearCall(call, CHR_REASON_INVALIDMESSAGE, pmsgbuf);
		}
	}

	CHRTRACEDBG3("Received Q.931 message: (%s, %s)\n",
		call->callType, call->callToken);

	initializePrintHandler(&printHandler, "Received H.2250 Message");
	setEventHandler(pctxt, &printHandler);

	pmsg = (Q931Message*)memAlloc(pctxt, sizeof(Q931Message));
	if (!pmsg)
	{
		CHRTRACEERR3("ERROR: Failed to allocate memory for Q.931 message "
			"structure (%s, %s)\n", call->callType, call->callToken);
		memReset(&gH323ep.msgctxt);
		return CHR_FAILED;
	}
	memset(pmsg, 0, sizeof(Q931Message));

	ret = chrQ931Decode(call, pmsg, len, pmsgbuf, TRUE);
	if (ret != CHR_OK) {
		CHRTRACEERR3("Error:Failed to decode received H.2250 message. (%s, %s)\n",
			call->callType, call->callToken);
	}
	CHRTRACEDBG3("Decoded Q931 message (%s, %s)\n", call->callType,
		call->callToken);
	finishPrint();
	removeEventHandler(pctxt);
	if (ret == CHR_OK) {
		chrHandleH2250Message(call, pmsg);
	}

	memFreePtr(pctxt, pmsgbuf);

	return ret;
}


/** H.245 */
int chrCreateH245Listener(CHRH323CallData *call)
{
	int ret = 0;
	CHRSOCKET channelSocket = 0;
	CHRTRACEINFO1("Creating H245 listener\n");
	if ((ret = chrSocketCreate(&channelSocket)) != ASN_OK)
	{
		CHRTRACEERR3("ERROR: Failed to create socket for H245 listener "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	ret = chrBindPort(CHRTCP, channelSocket, call->localIP);
	if (ret == CHR_FAILED)
	{
		CHRTRACEERR3("Error:Unable to bind to a TCP port - H245 listener creation"
			" (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	call->h245listenport = (int*)memAlloc(call->pctxt, sizeof(int));
	*(call->h245listenport) = ret;
	call->h245listener = (CHRSOCKET*)memAlloc(call->pctxt, sizeof(CHRSOCKET));
	*(call->h245listener) = channelSocket;
	ret = chrSocketListen(*(call->h245listener), 20);
	if (ret != ASN_OK)
	{
		CHRTRACEERR3("Error:Unable to listen on H.245 socket (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	CHRTRACEINFO4("H245 listener creation - successful(port %d) (%s, %s)\n",
		*(call->h245listenport), call->callType, call->callToken);
	return CHR_OK;
}
int chrCloseH245Listener(CHRH323CallData *call)
{
	CHRTRACEINFO3("Closing H.245 Listener (%s, %s)\n", call->callType,
		call->callToken);
	if (call->h245listener)
	{
		chrSocketClose(*(call->h245listener));
		memFreePtr(call->pctxt, call->h245listener);
		call->h245listener = NULL;
	}
	return CHR_OK;
}

int chrCreateH245Connection(CHRH323CallData *call)
{
	int ret = 0;
	CHRSOCKET channelSocket = 0;
	chrTimerCallback *cbData = NULL;

	CHRTRACEINFO1("Creating H245 Connection\n");
	if ((ret = chrSocketCreate(&channelSocket)) != ASN_OK)
	{
		CHRTRACEERR3("ERROR:Failed to create socket for H245 connection "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	else
	{
		if (0 == call->pH245Channel) 
		{
			call->pH245Channel = (CHRH323Channel*)memAllocZ(call->pctxt, sizeof(CHRH323Channel));
		}

		/*
		bind socket to a port before connecting. Thus avoiding
		implicit bind done by a connect call.
		*/
		ret = chrBindPort(CHRTCP, channelSocket, call->localIP);
		if (ret == CHR_FAILED)
		{
			CHRTRACEERR3("Error:Unable to bind to a TCP port - h245 connection "
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		call->pH245Channel->port = ret;
		CHRTRACEDBG4("Local H.245 port is %d (%s, %s)\n",
			call->pH245Channel->port,
			call->callType, call->callToken);
		CHRTRACEINFO5("Trying to connect to remote endpoint to setup H245 "
			"connection %s:%d(%s, %s)\n", call->remoteIP,
			call->remoteH245Port, call->callType, call->callToken);

		if ((ret = chrSocketConnect(channelSocket, call->remoteIP,
			call->remoteH245Port)) == ASN_OK)
		{
			call->pH245Channel->sock = channelSocket;
			call->h245SessionState = CHR_H245SESSION_ACTIVE;

			CHRTRACEINFO3("H245 connection creation successful (%s, %s)\n",
				call->callType, call->callToken);

			/*Start terminal capability exchange and master slave determination */
			ret = chrSendTermCapMsg(call);
			if (ret != CHR_OK)
			{
				CHRTRACEERR3("ERROR:Sending Terminal capability message (%s, %s)\n",
					call->callType, call->callToken);
				return ret;
			}
			ret = chrSendMasterSlaveDetermination(call);
			if (ret != CHR_OK)
			{
				CHRTRACEERR3("ERROR:Sending Master-slave determination message "
					"(%s, %s)\n", call->callType, call->callToken);
				return ret;
			}
		}
		else
		{
			if (call->h245ConnectionAttempts >= 3)
			{
				CHRTRACEERR3("Error:Failed to setup an H245 connection with remote "
					"destination. (%s, %s)\n", call->callType,
					call->callToken);
				if (call->callState < CHR_CALL_CLEAR)
				{
					call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
					call->callState = CHR_CALL_CLEAR;
				}
				return CHR_FAILED;
			}
			else{
				CHRTRACEWARN4("Warn:Failed to connect to remote destination for "
					"H245 connection - will retry after %d seconds"
					"(%s, %s)\n", DEFAULT_H245CONNECTION_RETRYTIMEOUT,
					call->callType, call->callToken);

				cbData = (chrTimerCallback*)memAlloc(call->pctxt,
					sizeof(chrTimerCallback));
				if (!cbData)
				{
					CHRTRACEERR3("Error:Unable to allocate memory for timer "
						"callback.(%s, %s)\n", call->callType,
						call->callToken);
					return CHR_FAILED;
				}
				cbData->call = call;
				cbData->timerType = CHR_H245CONNECT_TIMER;
				if (!chrTimerCreate(call->pctxt, &call->timerList,
					&chrCallH245ConnectionRetryTimerExpired,
					DEFAULT_H245CONNECTION_RETRYTIMEOUT, cbData,
					FALSE))
				{
					CHRTRACEERR3("Error:Unable to create H245 connection retry timer"
						"(%s, %s)\n", call->callType, call->callToken);
					memFreePtr(call->pctxt, cbData);
					return CHR_FAILED;
				}
				return CHR_OK;
			}
		}
	}
	return CHR_OK;
}


int chrAcceptH245Connection(CHRH323CallData *call)
{
	int ret;
	CHRSOCKET h245Channel = 0;
	ret = chrSocketAccept(*(call->h245listener), &h245Channel,
		NULL, NULL);
	if (ret != ASN_OK)
	{
		CHRTRACEERR1("Error:Accepting h245 connection\n");
		return CHR_FAILED;
	}

	if (0 == call->pH245Channel) {
		call->pH245Channel =
			(CHRH323Channel*)memAllocZ(call->pctxt, sizeof(CHRH323Channel));
	}
	call->pH245Channel->sock = h245Channel;
	call->h245SessionState = CHR_H245SESSION_ACTIVE;


	CHRTRACEINFO3("H.245 connection established (%s, %s)\n",
		call->callType, call->callToken);


	/* Start terminal capability exchange and master slave determination */
	ret = chrSendTermCapMsg(call);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Sending Terminal capability message (%s, %s)\n",
			call->callType, call->callToken);
		return ret;
	}
	ret = chrSendMasterSlaveDetermination(call);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Sending Master-slave determination message "
			"(%s, %s)\n", call->callType, call->callToken);
		return ret;
	}
	return CHR_OK;
}


int chrCloseH245Connection(CHRH323CallData *call)
{
	CHRTRACEINFO3("Closing H.245 connection (%s, %s)\n", call->callType,
		call->callToken);

	if (0 != call->pH245Channel)
	{
		if (0 != call->pH245Channel->sock)
			chrSocketClose(call->pH245Channel->sock);
		if (call->pH245Channel->outQueue.count > 0)
			dListFreeAll(call->pctxt, &(call->pH245Channel->outQueue));
		memFreePtr(call->pctxt, call->pH245Channel);
		call->pH245Channel = NULL;
		CHRTRACEDBG3("Closed H245 connection. (%s, %s)\n", call->callType,
			call->callToken);
	}
	call->h245SessionState = CHR_H245SESSION_CLOSED;

	return CHR_OK;
}


int chrReceiveH245Msg(CHRH323CallData *call)
{
	int  recvLen, ret, len, total = 0;
	ASN1OCTET msglenbuf[4];
	ASN1OCTET* pmsgbuf;
	H245Message *pmsg;
	OOCTXT *pctxt = &gH323ep.msgctxt;
	struct timeval timeout;
	fd_set readfds;

	pmsg = (H245Message*)memAlloc(pctxt, sizeof(H245Message));

	/* First read just TPKT header which is four bytes */
	recvLen = chrSocketRecv(call->pH245Channel->sock, msglenbuf, 4);

	/* Since we are working with TCP, need to determine the
	message boundary. Has to be done at channel level, as channels
	know the message formats and can determine boundaries
	*/
	if (recvLen <= 0 && call->h245SessionState != CHR_H245SESSION_PAUSED)
	{
		if (recvLen == 0)
			CHRTRACEINFO3("Closing H.245 channels as remote end point closed H.245"
			" connection (%s, %s)\n", call->callType, call->callToken);
		else
			CHRTRACEERR3("Error: Transport failure while trying to receive H245"
			" message (%s, %s)\n", call->callType, call->callToken);

		chrCloseH245Connection(call);
		chrFreeH245Message(call, pmsg);
		return clearCall(call, CHR_REASON_TRANSPORTFAILURE, 0);
	}

	if (call->h245SessionState == CHR_H245SESSION_PAUSED)
	{
		CHRLogicalChannel *temp;

		CHRTRACEINFO3("Call Paused, closing logical channels"
			" (%s, %s)\n", call->callType, call->callToken);

		temp = call->logicalChans;
		while (temp)
		{
			if (temp->state == CHR_LOGICALCHAN_ESTABLISHED)
			{
				/* Sending closelogicalchannel only for outgoing channels*/
				if (!strcmp(temp->dir, "transmit"))
				{
					chrSendCloseLogicalChannel(call, temp);
				}
			}
			temp = temp->next;
		}
		call->masterSlaveState = CHR_MasterSlave_Idle;
		call->callState = CHR_CALL_PAUSED;
		call->localTermCapState = CHR_LocalTermCapExchange_Idle;
		call->remoteTermCapState = CHR_RemoteTermCapExchange_Idle;
		call->h245SessionState = CHR_H245SESSION_IDLE;
		call->logicalChans = NULL;
	}

	CHRTRACEDBG1("Receiving H245 message\n");

	if (recvLen != 4)
	{
		CHRTRACEERR3("ERROR: Reading TPKT header for H245 message (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeH245Message(call, pmsg);
		return clearCall(call, CHR_REASON_INVALIDMESSAGE, 0);
	}

	len = msglenbuf[2];
	len = len << 8;
	len = (len | msglenbuf[3]);
	/* Remaining message length is length - tpkt length */
	len = len - 4;

	CHRTRACEDBG2("H.245 message length is %d\n", len);

	pmsgbuf = (ASN1OCTET*)memAlloc(pctxt, len);
	if (0 == pmsgbuf) {
		CHRTRACEERR3("ERROR: Failed to allocate memory for incoming H.245 "
			"message (%s, %s)\n", call->callType, call->callToken);
		memReset(&gH323ep.msgctxt);
		return clearCall(call, CHR_REASON_INVALIDMESSAGE, 0);
	}

	/* Now read actual H245 message body. We should make sure that we
	receive complete message as indicated by len. If we don't then there
	is something wrong. The lchrp below receives message, then checks whether
	complete message is received. If not received, then uses select to peek
	for remaining bytes of the message. If message is not received in 3
	seconds, then we have a problem. Report an error and exit.
	*/
	while (total < len)
	{
		recvLen = chrSocketRecv
			(call->pH245Channel->sock, pmsgbuf + total, len - total);

		total += recvLen;
		if (total == len) break; /* Complete message is received */

		FD_ZERO(&readfds);
		FD_SET(call->pH245Channel->sock, &readfds);
		timeout.tv_sec = 3;
		timeout.tv_usec = 0;
		ret = chrSocketSelect(call->pH245Channel->sock + 1, &readfds, NULL,
			NULL, &timeout);
		if (ret == -1)
		{
			CHRTRACEERR3("Error in select...H245 Receive-Clearing call (%s, %s)\n",
				call->callType, call->callToken);
			chrFreeH245Message(call, pmsg);
			return clearCall(call, CHR_REASON_TRANSPORTFAILURE, 0);
		}
		/* If remaining part of the message is not received in 3 seconds
		exit */
		if (!FD_ISSET(call->pH245Channel->sock, &readfds))
		{
			CHRTRACEERR3("Error: Incomplete h245 message received (%s, %s)\n",
				call->callType, call->callToken);
			chrFreeH245Message(call, pmsg);
			return clearCall(call, CHR_REASON_TRANSPORTFAILURE, 0);
		}
	}

	CHRTRACEDBG3("Complete H245 message received (%s, %s)\n",
		call->callType, call->callToken);
	setPERBuffer(pctxt, pmsgbuf, total, TRUE);
	initializePrintHandler(&printHandler, "Received H.245 Message");

	/* Set event handler */
	setEventHandler(pctxt, &printHandler);

	ret = asn1PD_H245MultimediaSystemControlMessage(pctxt, &(pmsg->h245Msg));
	if (ret != ASN_OK)
	{
		CHRTRACEERR3("Error decoding H245 message (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeH245Message(call, pmsg);
		return CHR_FAILED;
	}
	finishPrint();
	removeEventHandler(pctxt);
	memFreePtr(pctxt, pmsgbuf);

	chrHandleH245Message(call, pmsg);

	return CHR_OK;
}
int chrSendH245MsgtoQueue(struct CHRH323CallData *call)
{
	/** todo */
	return 0;
}


/** Command Server Socket Receive or Pipe Read */
int chrMonitorChannels()
{
	int ret = 0, nfds = 0;
	struct timeval toMin, toNext;
	fd_set readfds, writefds;
	ASN1OCTET stackCmd[sizeof(CHRStackCommand)];
	int recvLen, stkCmdIdx = 0;

	gMonitor = TRUE;

	toMin.tv_sec = 3;
	toMin.tv_usec = 0;
	//chrH323EpPrintConfig();

	//if (gH323ep.gkClient) {
	//	chrGkClientPrintConfig(gH323ep.gkClient);
	//	if (CHR_OK != chrGkClientStart(gH323ep.gkClient))
	//	{
	//		CHRTRACEERR1("Error:Failed to start Gatekeeper client\n");
	//		chrGkClientDestroy();
	//	}
	//}

	while (1)
	{
		FD_ZERO(&readfds);
		FD_ZERO(&writefds);
		nfds = 0;
		chrSetFDSETs(&readfds, &writefds, &nfds);

		if (!gMonitor) {
			CHRTRACEINFO1("Ending Monitor thread\n");
			break;
		}


		if (nfds == 0)
#ifdef _WIN32
			Sleep(10);
#else
		{
			toMin.tv_sec = 0;
			toMin.tv_usec = 10000;
			chrSocketSelect(1, 0, 0, 0, &toMin);
		}
#endif
		else
			ret = chrSocketSelect(nfds, &readfds, &writefds,
			NULL, &toMin);

		if (ret == -1)
		{

			CHRTRACEERR1("Error in select ...exiting\n");
			exit(-1);
		}

		toMin.tv_sec = 0;
		toMin.tv_usec = 100000; /* 100ms*/
		/*This is for test application. Not part of actual stack */

		chrTimerFireExpired(&gH323ep.ctxt, &g_TimerList);
		if (chrTimerNextTimeout(&g_TimerList, &toNext))
		{
			if (chrCompareTimeouts(&toMin, &toNext)>0)
			{
				toMin.tv_sec = toNext.tv_sec;
				toMin.tv_usec = toNext.tv_usec;
			}
		}

		/* Read and process stack command */
		if (0 != gH323ep.cmdSock && FD_ISSET(gH323ep.cmdSock, &readfds)) {
			ASN1UINT nbytesToRead = sizeof(CHRStackCommand)-stkCmdIdx;
#ifdef _WIN32
			recvLen = chrSocketRecv
				(gH323ep.cmdSock, &stackCmd[stkCmdIdx], nbytesToRead);
#else
			recvLen = read(gH323ep.cmdSock, &stackCmd[stkCmdIdx], nbytesToRead);
#endif
			if (recvLen < 0) {
				CHRTRACEERR1("ERROR: Failed to read CMD message\n");
				chrStopMonitorCalls();
				continue;
			}
			else if ((ASN1UINT)recvLen == nbytesToRead) {
				CHRTRACEDBG5("read stack cmd: t=%d, p1=%x, p2=%x, p3=%x\n",
					((CHRStackCommand*)stackCmd)->type,
					((CHRStackCommand*)stackCmd)->param1,
					((CHRStackCommand*)stackCmd)->param2,
					((CHRStackCommand*)stackCmd)->param3);

				/* Received complete stack command */
				chrProcessStackCommand((CHRStackCommand*)stackCmd);

				stkCmdIdx = 0;
			}
			else {
				/* Received partial command */
				stkCmdIdx += recvLen;
			}
		}

		if (chrProcessFDSETsAndTimers(&readfds, &writefds, &toMin) != CHR_OK)
		{
			chrStopMonitorCalls();
			continue;
		}

	}/* while(1)*/
	return CHR_OK;
}




int chrSetFDSETs(fd_set *pReadfds, fd_set *pWritefds, int *nfds)
{
	CHRH323CallData *call = NULL;

	if (gH323ep.listener)
	{
		FD_SET(*(gH323ep.listener), pReadfds);
		if (*nfds < (int)*(gH323ep.listener))
			*nfds = *((int*)gH323ep.listener);
	}

	if (gH323ep.cmdListener)
	{
		FD_SET(gH323ep.cmdListener, pReadfds);
		if (*nfds < (int)gH323ep.cmdListener)
			*nfds = (int)gH323ep.cmdListener;
	}
	if (gH323ep.cmdSock)
	{
		FD_SET(gH323ep.cmdSock, pReadfds);
		if (*nfds < (int)gH323ep.cmdSock)
			*nfds = (int)gH323ep.cmdSock;
	}



	if (gH323ep.callList)
	{
		call = gH323ep.callList;
		while (call)
		{
			if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
			{
				FD_SET(call->pH225Channel->sock, pReadfds);
				if (call->pH225Channel->outQueue.count > 0 ||
					(CHR_TESTFLAG(call->flags, CHR_M_TUNNELING) &&
					0 != call->pH245Channel &&
					call->pH245Channel->outQueue.count>0))
					FD_SET(call->pH225Channel->sock, pWritefds);
				if (*nfds < (int)call->pH225Channel->sock)
					*nfds = call->pH225Channel->sock;
			}

			if (0 != call->pH245Channel &&  call->pH245Channel->sock != 0)
			{
				FD_SET(call->pH245Channel->sock, pReadfds);
				if (call->pH245Channel->outQueue.count>0)
					FD_SET(call->pH245Channel->sock, pWritefds);
				if (*nfds < (int)call->pH245Channel->sock)
					*nfds = call->pH245Channel->sock;
			}
			else if (call->h245listener)
			{
				CHRTRACEINFO3("H.245 Listerner socket being monitored "
					"(%s, %s)\n", call->callType, call->callToken);
				FD_SET(*(call->h245listener), pReadfds);
				if (*nfds < (int)*(call->h245listener))
					*nfds = *(call->h245listener);
			}
			call = call->next;

		}/* while(call) */
	}/*if(gH323ep.callList) */


	if (*nfds != 0) *nfds = *nfds + 1;

	return CHR_OK;

}

int chrProcessFDSETsAndTimers
(fd_set *pReadfds, fd_set *pWritefds, struct timeval *pToMin)
{
	CHRH323CallData *call, *prev = NULL;
	struct timeval toNext;
#ifdef _WIN32
	if (gH323ep.cmdListener)
	{
		if (FD_ISSET(gH323ep.cmdListener, pReadfds))
		{
			if (chrAcceptCmdConnection() != CHR_OK){
				CHRTRACEERR1("Error:Failed to accept command connection\n");
				return CHR_FAILED;
			}
		}
	}
#endif


	if (gH323ep.listener)
	{
		if (FD_ISSET(*(gH323ep.listener), pReadfds))
		{
			CHRTRACEDBG1("New connection at H225 receiver\n");
			chrAcceptH225Connection();
		}
	}


	if (gH323ep.callList)
	{
		call = gH323ep.callList;
		while (call)
		{
			chrTimerFireExpired(call->pctxt, &call->timerList);
			if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
			{
				if (FD_ISSET(call->pH225Channel->sock, pReadfds))
				{
					if (chrReceiveH225Msg(call) != CHR_OK)
					{
						CHRTRACEERR3("ERROR:Failed chrH2250Receive - Clearing call "
							"(%s, %s)\n", call->callType, call->callToken);
						if (call->callState < CHR_CALL_CLEAR)
						{
							call->callEndReason = CHR_REASON_INVALIDMESSAGE;
							call->callState = CHR_CALL_CLEAR;
						}
					}
				}
			}


			if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
			{
				if (FD_ISSET(call->pH245Channel->sock, pReadfds))
				{
					chrReceiveH245Msg(call);
				}
			}

			if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
			{
				if (FD_ISSET(call->pH245Channel->sock, pWritefds))
				{
					if (call->pH245Channel->outQueue.count>0)
						chrSendMsg(call, CHRH245MSG);
				}
			}
			else if (call->h245listener)
			{
				if (FD_ISSET(*(call->h245listener), pReadfds))
				{
					CHRTRACEDBG3("Incoming H.245 connection (%s, %s)\n",
						call->callType, call->callToken);
					chrAcceptH245Connection(call);
				}
			}

			if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
			{
				if (FD_ISSET(call->pH225Channel->sock, pWritefds))
				{
					if (call->pH225Channel->outQueue.count>0)
					{
						CHRTRACEDBG3("Sending H225 message (%s, %s)\n",
							call->callType, call->callToken);
						chrSendMsg(call, CHRQ931MSG);
					}
					if (call->pH245Channel &&
						call->pH245Channel->outQueue.count>0 &&
						CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
					{
						CHRTRACEDBG3("H245 message needs to be tunneled. "
							"(%s, %s)\n", call->callType,
							call->callToken);
						chrSendMsg(call, CHRH245MSG);
					}
				}
			}

			if (chrTimerNextTimeout(&call->timerList, &toNext))
			{
				if (chrCompareTimeouts(pToMin, &toNext) > 0)
				{
					pToMin->tv_sec = toNext.tv_sec;
					pToMin->tv_usec = toNext.tv_usec;
				}
			}
			prev = call;
			call = call->next;
			if (prev->callState >= CHR_CALL_CLEAR)
				chrEndCall(prev);
		}/* while(call) */
	}/* if(gH323ep.callList) */

	return CHR_OK;

}

static int clearCall
(CHRH323CallData* call, CHRCallClearReason reason, ASN1OCTET* pmsgbuf)
{
	if (0 != pmsgbuf) { memFreePtr(&gH323ep.msgctxt, pmsgbuf); }

	if (call->callState < CHR_CALL_CLEAR) {
		call->callEndReason = reason;
		call->callState = CHR_CALL_CLEAR;
	}

	return CHR_FAILED;
}




/* Generic Send Message functionality. Based on type of message to be sent,
it calls the corresponding function to retrieve the message buffer and
then transmits on the associated channel
Interpreting msgptr:
Q931 messages except facility
1st octet - msgType, next 4 octets - tpkt header,
followed by encoded msg
Q931 message facility
1st octect - CHRFacility, 2nd octet - tunneled msg
type(in case no tunneled msg - CHRFacility),
3rd and 4th octet - associated logical channel
of the tunneled msg(0 when no channel is
associated. ex. in case of MSD, TCS), next
4 octets - tpkt header, followed by encoded
message.

H.245 messages no tunneling
1st octet - msg type, next two octets - logical
channel number(0, when no channel is associated),
next two octets - total length of the message
(including tpkt header)

H.245 messages - tunneling.
1st octet - msg type, next two octets - logical
channel number(0, when no channel is associated),
next two octets - total length of the message.
Note, no tpkt header is present in this case.

*/
int chrSendMsg(CHRH323CallData *call, int type)
{

	int len = 0, ret = 0, msgType = 0, tunneledMsgType = 0, logicalChannelNo = 0;
	DListNode * p_msgNode = NULL;
	ASN1OCTET *msgptr, *msgToSend = NULL;



	if (call->callState == CHR_CALL_CLEARED)
	{
		CHRTRACEDBG3("Warning:Call marked for cleanup. Can not send message."
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_OK;
	}

	if (type == CHRQ931MSG)
	{
		if (call->pH225Channel->outQueue.count == 0)
		{
			CHRTRACEWARN3("WARN:No H.2250 message to send. (%s, %s)\n",
				call->callType, call->callToken);
			return CHR_FAILED;
		}

		CHRTRACEDBG3("Sending Q931 message (%s, %s)\n", call->callType,
			call->callToken);
		p_msgNode = call->pH225Channel->outQueue.head;
		msgptr = (ASN1OCTET*)p_msgNode->data;
		msgType = msgptr[0];

		if (msgType == CHRFacility)
		{
			tunneledMsgType = msgptr[1];
			logicalChannelNo = msgptr[2];
			logicalChannelNo = logicalChannelNo << 8;
			logicalChannelNo = (logicalChannelNo | msgptr[3]);
			len = msgptr[6];
			len = len << 8;
			len = (len | msgptr[7]);
			msgToSend = msgptr + 4;
		}
		else {
			len = msgptr[3];
			len = len << 8;
			len = (len | msgptr[4]);
			msgToSend = msgptr + 1;
		}

		/* Remove the message from rtdlist pH225Channel->outQueue */
		dListRemove(&(call->pH225Channel->outQueue), p_msgNode);
		if (p_msgNode)
			memFreePtr(call->pctxt, p_msgNode);

		/*TODO: This is not required ideally. We will see for some time and if
		we don't face any problems we will delete this code */
//#if 0
//		/* Check whether connection with remote is alright */
//		if (!chrChannelsIsConnectionOK(call, call->pH225Channel->sock))
//		{
//			CHRTRACEERR3("Error:Transport failure for signalling channel. "
//				"Abandoning message send and marking call for cleanup.(%s"
//				"'%s)\n", call->callType, call->callToken);
//			if (call->callState < CHR_CALL_CLEAR)
//				call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
//			call->callState = CHR_CALL_CLEARED;
//			return CHR_OK;
//		}
//#endif
		/* Send message out via TCP */
		ret = chrSocketSend(call->pH225Channel->sock, msgToSend, len);
		if (ret == ASN_OK)
		{
			memFreePtr(call->pctxt, msgptr);
			CHRTRACEDBG3("H2250/Q931 Message sent successfully (%s, %s)\n",
				call->callType, call->callToken);
			chrOnSendMsg(call, msgType, tunneledMsgType, logicalChannelNo);
			return CHR_OK;
		}
		else{
			CHRTRACEERR3("H2250Q931 Message send failed (%s, %s)\n",
				call->callType, call->callToken);
			memFreePtr(call->pctxt, msgptr);
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
				call->callState = CHR_CALL_CLEAR;
			}
			return CHR_FAILED;
		}
	}/* end of type==CHRQ931MSG */
	if (type == CHRH245MSG)
	{
		if (call->pH245Channel->outQueue.count == 0)
		{
			CHRTRACEWARN3("WARN:No H.245 message to send. (%s, %s)\n",
				call->callType, call->callToken);
			return CHR_FAILED;
		}
		CHRTRACEDBG3("Sending H245 message (%s, %s)\n", call->callType,
			call->callToken);
		p_msgNode = call->pH245Channel->outQueue.head;
		msgptr = (ASN1OCTET*)p_msgNode->data;
		msgType = msgptr[0];

		logicalChannelNo = msgptr[1];
		logicalChannelNo = logicalChannelNo << 8;
		logicalChannelNo = (logicalChannelNo | msgptr[2]);

		len = msgptr[3];
		len = len << 8;
		len = (len | msgptr[4]);
		/* Remove the message from queue */
		dListRemove(&(call->pH245Channel->outQueue), p_msgNode);
		if (p_msgNode)
			memFreePtr(call->pctxt, p_msgNode);

		/* Send message out */
		if (0 == call->pH245Channel && !CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
		{
			CHRTRACEWARN3("Neither H.245 channel nor tunneling active "
				"(%s, %s)\n", call->callType, call->callToken);
			memFreePtr(call->pctxt, msgptr);
			/*chrCloseH245Session(call);*/
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
				call->callState = CHR_CALL_CLEAR;
			}
			return CHR_OK;
		}

		if (0 != call->pH245Channel && 0 != call->pH245Channel->sock)
		{
			CHRTRACEDBG4("Sending %s H245 message over H.245 channel. "
				"(%s, %s)\n", chrGetMsgTypeText(msgType),
				call->callType, call->callToken);

			ret = chrSocketSend(call->pH245Channel->sock, msgptr + 5, len);
			if (ret == ASN_OK)
			{
				memFreePtr(call->pctxt, msgptr);
				CHRTRACEDBG3("H245 Message sent successfully (%s, %s)\n",
					call->callType, call->callToken);
				chrOnSendMsg(call, msgType, tunneledMsgType, logicalChannelNo);
				return CHR_OK;
			}
			else{
				memFreePtr(call->pctxt, msgptr);
				CHRTRACEERR3("ERROR:H245 Message send failed (%s, %s)\n",
					call->callType, call->callToken);
				if (call->callState < CHR_CALL_CLEAR)
				{
					call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
					call->callState = CHR_CALL_CLEAR;
				}
				return CHR_FAILED;
			}
		}
	}
	/* Need to add support for other messages such as T38 etc */
	CHRTRACEWARN3("ERROR:Unknown message type - message not Sent (%s, %s)\n",
		call->callType, call->callToken);
	return CHR_FAILED;
}



int chrOnSendMsg
(CHRH323CallData *call, int msgType, int tunneledMsgType, int associatedChan)
{
	chrTimerCallback *cbData = NULL;
	switch (msgType)
	{
	case CHRSetup:
		CHRTRACEINFO3("Sent Message - Setup (%s, %s)\n", call->callType,
			call->callToken);
		/* Start call establishment timer */
		cbData = (chrTimerCallback*)memAlloc(call->pctxt,
			sizeof(chrTimerCallback));
		if (!cbData)
		{
			CHRTRACEERR3("Error:Unable to allocate memory for timer callback."
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		cbData->call = call;
		cbData->timerType = CHR_CALLESTB_TIMER;
		if (!chrTimerCreate(call->pctxt, &call->timerList, &chrCallEstbTimerExpired,
			gH323ep.callEstablishmentTimeout, cbData, FALSE))
		{
			CHRTRACEERR3("Error:Unable to create call establishment timer. "
				"(%s, %s)\n", call->callType, call->callToken);
			memFreePtr(call->pctxt, cbData);
			return CHR_FAILED;
		}

		if (gH323ep.h323Callbacks.onOutgoingCall)
			gH323ep.h323Callbacks.onOutgoingCall(call);
		break;
	case CHRCallProceeding:
		CHRTRACEINFO3("Sent Message - CallProceeding (%s, %s)\n", call->callType,
			call->callToken);
		break;
	case CHRAlert:
		CHRTRACEINFO3("Sent Message - Alerting (%s, %s) \n", call->callType,
			call->callToken);
		if (gH323ep.h323Callbacks.onAlerting && call->callState < CHR_CALL_CLEAR)
			gH323ep.h323Callbacks.onAlerting(call);
		break;
	case CHRConnect:
		CHRTRACEINFO3("Sent Message - Connect (%s, %s)\n", call->callType,
			call->callToken);
		if (gH323ep.h323Callbacks.onCallEstablished)
			gH323ep.h323Callbacks.onCallEstablished(call);
		break;
	case CHRReleaseComplete:
		CHRTRACEINFO3("Sent Message - ReleaseComplete (%s, %s)\n", call->callType,
			call->callToken);

		if (call->callState == CHR_CALL_CLEAR_RELEASERECVD)
			call->callState = CHR_CALL_CLEARED;
		else{
			call->callState = CHR_CALL_CLEAR_RELEASESENT;
		}

		if (call->callState == CHR_CALL_CLEAR_RELEASESENT &&
			call->h245SessionState == CHR_H245SESSION_IDLE)
		{
			cbData = (chrTimerCallback*)memAlloc(call->pctxt,
				sizeof(chrTimerCallback));
			if (!cbData)
			{
				CHRTRACEERR3("Error:Unable to allocate memory for timer callback "
					"data.(%s, %s)\n", call->callType, call->callToken);
				return CHR_FAILED;
			}
			cbData->call = call;
			cbData->timerType = CHR_SESSION_TIMER;
			cbData->channelNumber = 0;
			if (!chrTimerCreate(call->pctxt, &call->timerList,
				&chrSessionTimerExpired, gH323ep.sessionTimeout, cbData, FALSE))
			{
				CHRTRACEERR3("Error:Unable to create EndSession timer- "
					"ReleaseComplete.(%s, %s)\n", call->callType,
					call->callToken);
				memFreePtr(call->pctxt, cbData);
				return CHR_FAILED;
			}
		}

		if (call->h245SessionState == CHR_H245SESSION_CLOSED)
		{
			call->callState = CHR_CALL_CLEARED;
		}

		break;
	case CHRFacility:
		if (tunneledMsgType == CHRFacility)
		{
			CHRTRACEINFO3("Sent Message - Facility. (%s, %s)\n",
				call->callType, call->callToken);
		}
		else{
			CHRTRACEINFO4("Sent Message - Facility(%s) (%s, %s)\n",
				chrGetMsgTypeText(tunneledMsgType),
				call->callType, call->callToken);

			chrOnSendMsg(call, tunneledMsgType, 0, associatedChan);
		}


		break;
	case CHRMasterSlaveDetermination:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - MasterSlaveDetermination (%s, %s)\n",
			call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - MasterSlaveDetermination (%s, %s)\n",
			call->callType, call->callToken);
		/* Start MSD timer */
		cbData = (chrTimerCallback*)memAlloc(call->pctxt,
			sizeof(chrTimerCallback));
		if (!cbData)
		{
			CHRTRACEERR3("Error:Unable to allocate memory for timer callback data."
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		cbData->call = call;
		cbData->timerType = CHR_MSD_TIMER;
		if (!chrTimerCreate(call->pctxt, &call->timerList, &chrMSDTimerExpired,
			gH323ep.msdTimeout, cbData, FALSE))
		{
			CHRTRACEERR3("Error:Unable to create MSD timer. "
				"(%s, %s)\n", call->callType, call->callToken);
			memFreePtr(call->pctxt, cbData);
			return CHR_FAILED;
		}

		break;
	case CHRMasterSlaveAck:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - MasterSlaveDeterminationAck (%s, %s)"
			"\n", call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - MasterSlaveDeterminationAck (%s, %s)\n",
			call->callType, call->callToken);
		break;
	case CHRMasterSlaveReject:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - MasterSlaveDeterminationReject "
			"(%s, %s)\n", call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - MasterSlaveDeterminationReject(%s, %s)\n",
			call->callType, call->callToken);
		break;
	case CHRMasterSlaveRelease:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - MasterSlaveDeterminationRelease "
			"(%s, %s)\n", call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - MasterSlaveDeterminationRelease "
			"(%s, %s)\n", call->callType, call->callToken);
		break;
	case CHRTerminalCapabilitySet:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING)) {
			/* If session isn't marked active yet, do it. possible in case of
			tunneling */
			if (call->h245SessionState == CHR_H245SESSION_IDLE ||
				call->h245SessionState == CHR_H245SESSION_PAUSED) {
				call->h245SessionState = CHR_H245SESSION_ACTIVE;
			}
			CHRTRACEINFO3("Tunneled Message - TerminalCapabilitySet (%s, %s)\n",
				call->callType, call->callToken);
		}
		else {
			CHRTRACEINFO3("Sent Message - TerminalCapabilitySet (%s, %s)\n",
				call->callType, call->callToken);
		}
		/* Start TCS timer */
		cbData = (chrTimerCallback*)memAlloc(call->pctxt,
			sizeof(chrTimerCallback));
		if (!cbData)
		{
			CHRTRACEERR3("Error:Unable to allocate memory for timer callback data."
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		cbData->call = call;
		cbData->timerType = CHR_TCS_TIMER;
		if (!chrTimerCreate(call->pctxt, &call->timerList, &chrTCSTimerExpired,
			gH323ep.tcsTimeout, cbData, FALSE))
		{
			CHRTRACEERR3("Error:Unable to create TCS timer. "
				"(%s, %s)\n", call->callType, call->callToken);
			memFreePtr(call->pctxt, cbData);
			return CHR_FAILED;
		}

		break;
	case CHRTerminalCapabilitySetAck:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - TerminalCapabilitySetAck (%s, %s)\n",
			call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - TerminalCapabilitySetAck (%s, %s)\n",
			call->callType, call->callToken);
		break;
	case CHRTerminalCapabilitySetReject:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - TerminalCapabilitySetReject "
			"(%s, %s)\n", call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - TerminalCapabilitySetReject (%s, %s)\n",
			call->callType, call->callToken);
		break;
	case CHROpenLogicalChannel:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO4("Tunneled Message - OpenLogicalChannel(%d). (%s, %s)\n",
			associatedChan, call->callType, call->callToken);
		else
			CHRTRACEINFO4("Sent Message - OpenLogicalChannel(%d). (%s, %s)\n",
			associatedChan, call->callType, call->callToken);
		/* Start LogicalChannel timer */
		cbData = (chrTimerCallback*)memAlloc(call->pctxt,
			sizeof(chrTimerCallback));
		if (!cbData)
		{
			CHRTRACEERR3("Error:Unable to allocate memory for timer callback data."
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		cbData->call = call;
		cbData->timerType = CHR_OLC_TIMER;
		cbData->channelNumber = associatedChan;
		if (!chrTimerCreate(call->pctxt, &call->timerList,
			&chrOpenLogicalChannelTimerExpired, gH323ep.logicalChannelTimeout,
			cbData, FALSE))
		{
			CHRTRACEERR3("Error:Unable to create OpenLogicalChannel timer. "
				"(%s, %s)\n", call->callType, call->callToken);
			memFreePtr(call->pctxt, cbData);
			return CHR_FAILED;
		}

		break;
	case CHROpenLogicalChannelAck:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO4("Tunneled Message - OpenLogicalChannelAck(%d) (%s,%s)\n",
			associatedChan, call->callType, call->callToken);
		else
			CHRTRACEINFO4("Sent Message - OpenLogicalChannelAck(%d) (%s, %s)\n",
			associatedChan, call->callType, call->callToken);
		break;
	case CHROpenLogicalChannelReject:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO4("Tunneled Message - OpenLogicalChannelReject(%d)"
			"(%s, %s)\n", associatedChan, call->callType,
			call->callToken);
		else
			CHRTRACEINFO4("Sent Message - OpenLogicalChannelReject(%d) (%s, %s)\n",
			associatedChan, call->callType, call->callToken);
		break;
	case CHREndSessionCommand:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - EndSessionCommand(%s, %s)\n",
			call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - EndSessionCommand (%s, %s)\n",
			call->callType, call->callToken);
		if (call->h245SessionState == CHR_H245SESSION_ACTIVE)
		{
			/* Start EndSession timer */
			call->h245SessionState = CHR_H245SESSION_ENDSENT;
			cbData = (chrTimerCallback*)memAlloc(call->pctxt,
				sizeof(chrTimerCallback));
			if (!cbData)
			{
				CHRTRACEERR3("Error:Unable to allocate memory for timer callback "
					"data.(%s, %s)\n", call->callType, call->callToken);
				return CHR_FAILED;
			}
			cbData->call = call;
			cbData->timerType = CHR_SESSION_TIMER;
			cbData->channelNumber = 0;
			if (!chrTimerCreate(call->pctxt, &call->timerList,
				&chrSessionTimerExpired, gH323ep.sessionTimeout, cbData, FALSE))
			{
				CHRTRACEERR3("Error:Unable to create EndSession timer. "
					"(%s, %s)\n", call->callType, call->callToken);
				memFreePtr(call->pctxt, cbData);
				return CHR_FAILED;
			}
		}
		else{
			chrCloseH245Connection(call);
		}
		break;
	case CHRCloseLogicalChannel:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - CloseLogicalChannel (%s, %s)\n",
			call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - CloseLogicalChannel (%s, %s)\n",
			call->callType, call->callToken);
		/* Start LogicalChannel timer */
		cbData = (chrTimerCallback*)memAlloc(call->pctxt,
			sizeof(chrTimerCallback));
		if (!cbData)
		{
			CHRTRACEERR3("Error:Unable to allocate memory for timer callback data."
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		cbData->call = call;
		cbData->timerType = CHR_CLC_TIMER;
		cbData->channelNumber = associatedChan;
		if (!chrTimerCreate(call->pctxt, &call->timerList,
			&chrCloseLogicalChannelTimerExpired, gH323ep.logicalChannelTimeout,
			cbData, FALSE))
		{
			CHRTRACEERR3("Error:Unable to create CloseLogicalChannel timer. "
				"(%s, %s)\n", call->callType, call->callToken);
			memFreePtr(call->pctxt, cbData);
			return CHR_FAILED;
		}

		break;
	case CHRCloseLogicalChannelAck:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - CloseLogicalChannelAck (%s, %s)\n",
			call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - CloseLogicalChannelAck (%s, %s)\n",
			call->callType, call->callToken);
		break;
	case CHRRequestChannelClose:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - RequestChannelClose (%s, %s)\n",
			call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - RequestChannelClose (%s, %s)\n",
			call->callType, call->callToken);
		/* Start RequestChannelClose timer */
		cbData = (chrTimerCallback*)memAlloc(call->pctxt,
			sizeof(chrTimerCallback));
		if (!cbData)
		{
			CHRTRACEERR3("Error:Unable to allocate memory for timer callback data."
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		cbData->call = call;
		cbData->timerType = CHR_RCC_TIMER;
		cbData->channelNumber = associatedChan;
		if (!chrTimerCreate(call->pctxt, &call->timerList,
			&chrRequestChannelCloseTimerExpired, gH323ep.logicalChannelTimeout,
			cbData, FALSE))
		{
			CHRTRACEERR3("Error:Unable to create RequestChannelClose timer. "
				"(%s, %s)\n", call->callType, call->callToken);
			memFreePtr(call->pctxt, cbData);
			return CHR_FAILED;
		}
		break;
	case CHRRequestChannelCloseAck:
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			CHRTRACEINFO3("Tunneled Message - RequestChannelCloseAck (%s, %s)\n",
			call->callType, call->callToken);
		else
			CHRTRACEINFO3("Sent Message - RequestChannelCloseAck (%s, %s)\n",
			call->callType, call->callToken);
		break;

	default:
		;
	}
	return CHR_OK;
}


int chrStopMonitorCalls()
{
	CHRH323CallData * call;
	if (gMonitor)
	{
		CHRTRACEINFO1("Doing chrStopMonitorCalls\n");
		if (gH323ep.cmdSock)
		{
			chrCloseCmdConnection();
		}

		if (gH323ep.callList)
		{
			CHRTRACEWARN1("Warn:Abruptly ending calls as stack going down\n");
			call = gH323ep.callList;
			while (call)
			{
				CHRTRACEWARN3("Clearing call (%s, %s)\n", call->callType,
					call->callToken);
				call->callEndReason = CHR_REASON_LOCAL_CLEARED;
				chrCleanCall(call);
				call = NULL;
				call = gH323ep.callList;
			}
			gH323ep.callList = NULL;
		}
		CHRTRACEINFO1("Stopping listener for incoming calls\n");
		if (gH323ep.listener)
		{
			chrSocketClose(*(gH323ep.listener));
			memFreePtr(&gH323ep.ctxt, gH323ep.listener);
			gH323ep.listener = NULL;
		}

		gMonitor = FALSE;
		CHRTRACEINFO1("Done chrStopMonitorCalls\n");
	}
	return CHR_OK;
}
//
//OOBOOL chrChannelsIsConnectionOK(CHRH323CallData *call, CHRSOCKET sock)
//{
//	struct timeval to;
//	fd_set readfds;
//	int ret = 0, nfds = 0;
//
//	to.tv_sec = 0;
//	to.tv_usec = 500;
//	FD_ZERO(&readfds);
//
//	FD_SET(sock, &readfds);
//	if (nfds < (int)sock)
//		nfds = (int)sock;
//
//	nfds++;
//
//	ret = chrSocketSelect(nfds, &readfds, NULL, NULL, &to);
//
//	if (ret == -1)
//	{
//		CHRTRACEERR3("Error in select ...broken pipe check(%s, %s)\n",
//			call->callType, call->callToken);
//		return FALSE;
//	}
//
//	if (FD_ISSET(sock, &readfds))
//	{
//		ASN1OCTET buf[2];
//		if (chrSocketRecvPeek(sock, buf, 2) == 0)
//		{
//			CHRTRACEWARN3("Broken pipe detected. (%s, %s)", call->callType,
//				call->callToken);
//			if (call->callState < CHR_CALL_CLEAR)
//				call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
//			call->callState = CHR_CALL_CLEARED;
//			return FALSE;
//		}
//	}
//	return TRUE;
//}


int chrGetNextPort(CHRH323PortType type)/* Get the next port of type TCP/UDP/RTP */
{
	if (type == CHRTCP)
	{
		if (gH323ep.tcpPorts.current <= gH323ep.tcpPorts.max)
			return gH323ep.tcpPorts.current++;
		else
		{
			gH323ep.tcpPorts.current = gH323ep.tcpPorts.start;
			return gH323ep.tcpPorts.current++;
		}
	}
	if (type == CHRUDP)
	{
		if (gH323ep.udpPorts.current <= gH323ep.udpPorts.max)
			return gH323ep.udpPorts.current++;
		else
		{
			gH323ep.udpPorts.current = gH323ep.udpPorts.start;
			return gH323ep.udpPorts.current++;
		}
	}
	if (type == CHRRTP)
	{
		if (gH323ep.rtpPorts.current <= gH323ep.rtpPorts.max)
			return gH323ep.rtpPorts.current++;
		else
		{
			gH323ep.rtpPorts.current = gH323ep.rtpPorts.start;
			return gH323ep.rtpPorts.current++;
		}
	}
	return CHR_FAILED;
}

int chrBindPort(CHRH323PortType type, CHRSOCKET socket, char *ip)
{
	int initialPort, bindPort, ret;
	CHRIPADDR ipAddrs;

	initialPort = chrGetNextPort(type);
	bindPort = initialPort;

	ret = chrSocketStrToAddr(ip, &ipAddrs);

	while (1)
	{
		if ((ret = chrSocketBind(socket, ipAddrs, bindPort)) == 0)
		{
			return bindPort;
		}
		else
		{
			bindPort = chrGetNextPort(type);
			if (bindPort == initialPort) return CHR_FAILED;
		}
	}
}

#ifdef _WIN32
int chrBindOSAllocatedPort(CHRSOCKET socket, char *ip)
{
	CHRIPADDR ipAddrs;
	int size, ret;
	struct sockaddr_in name;
	size = sizeof(struct sockaddr_in);
	ret = chrSocketStrToAddr(ip, &ipAddrs);
	if ((ret = chrSocketBind(socket, ipAddrs, 0)) == ASN_OK)
	{
		ret = chrSocketGetSockName(socket, &name, &size);
		if (ret == ASN_OK)
		{
			return name.sin_port;

		}
	}

	return CHR_FAILED;
}
#endif


