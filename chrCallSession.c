#include "chrTrace.h"
#include "chrTypes.h"
#include "chrCapability.h"
#include "chrLogicalChannels.h"
#include "chrChannels.h"
#include "chrCallSession.h"
#include "chrH323Protocol.h"
#include "chrH323Endpoint.h"


extern CHRH323EndPoint gH323ep;

CHRH323CallData* chrCreateCall(char* type, char*callToken, void *usrData)
/**
 (1). callData assignment
 (2). check gH323ep callbacks
*/
{
	CHRH323CallData *call = NULL;
	OOCTXT *pctxt = NULL;

	pctxt = newContext(); /** ASN1 context block*/
	if (!pctxt)
	{
		CHRTRACEERR1("ERROR:Failed to create OOCTXT for new call\n");
		return NULL;
	}
	call = (CHRH323CallData*)memAlloc(pctxt, sizeof(CHRH323CallData));
	if (!call)
	{
		CHRTRACEERR1("ERROR:Memory - chrCreateCall - call\n");
		return NULL;
	}
	/*   memset(call, 0, sizeof(CHRH323CallData));*//** ???? */
	call->pctxt = pctxt;
	call->callMode = gH323ep.callMode;
	sprintf(call->callToken, "%s", callToken);
	sprintf(call->callType, "%s", type);
	call->callReference = 0;
	if (gH323ep.callerid) 
	{
		strncpy(call->ourCallerId, gH323ep.callerid, sizeof(call->ourCallerId) - 1);
		call->ourCallerId[sizeof(call->ourCallerId) - 1] = '\0';
	}
	else 
	{
		call->ourCallerId[0] = '\0';
	}

	memset(&call->callIdentifier, 0, sizeof(H225CallIdentifier));
	memset(&call->confIdentifier, 0, sizeof(H225ConferenceIdentifier));

	call->flags = 0;
	if (CHR_TESTFLAG(gH323ep.flags, CHR_M_TUNNELING))
		CHR_SETFLAG(call->flags, CHR_M_TUNNELING);

	if (gH323ep.gkClient)
	{
		if (CHR_TESTFLAG(gH323ep.flags, CHR_M_GKROUTED))
		{
			CHR_SETFLAG(call->flags, CHR_M_GKROUTED);
		}
	}

	if (CHR_TESTFLAG(gH323ep.flags, CHR_M_FASTSTART))
		CHR_SETFLAG(call->flags, CHR_M_FASTSTART);

	if (CHR_TESTFLAG(gH323ep.flags, CHR_M_MEDIAWAITFORCONN))
		CHR_SETFLAG(call->flags, CHR_M_MEDIAWAITFORCONN);

	call->callState = CHR_CALL_CREATED;
	call->callEndReason = CHR_REASON_UNKNOWN;

	if (!strcmp(call->callType, "incoming"))
	{
		call->callingPartyNumber = NULL;
	}
	else
	{
		if (chrUtilsIsStrEmpty(gH323ep.callingPartyNumber))
		{
			call->callingPartyNumber = NULL;
		}
		else
		{
			call->callingPartyNumber = (char*)memAlloc(call->pctxt,
				strlen(gH323ep.callingPartyNumber) + 1);
			if (call->callingPartyNumber)
			{
				strcpy(call->callingPartyNumber, gH323ep.callingPartyNumber);
			}
			else{
				CHRTRACEERR3("Error:Memory - chrCreateCall - callingPartyNumber"
					".(%s, %s)\n", call->callType, call->callToken);
				freeContext(pctxt);
				return NULL;
			}
		}
	}

	call->calledPartyNumber = NULL;
	call->h245ConnectionAttempts = 0;
	call->h245SessionState = CHR_H245SESSION_IDLE;
	call->mediaInfo = NULL;
	strcpy(call->localIP, gH323ep.signallingIP);
	call->pH225Channel = NULL;
	call->pH245Channel = NULL;
	call->h245listener = NULL;
	call->h245listenport = NULL;
	call->remoteIP[0] = '\0';
	call->remotePort = 0;
	call->remoteH245Port = 0;
	call->remoteDisplayName = NULL;
	call->remoteAliases = NULL;
	call->ourAliases = NULL;
	call->masterSlaveState = CHR_MasterSlave_Idle;
	call->statusDeterminationNumber = 0;
	call->localTermCapState = CHR_LocalTermCapExchange_Idle;
	call->remoteTermCapState = CHR_RemoteTermCapExchange_Idle;
	call->ourCaps = NULL;
	call->remoteCaps = NULL;
	call->jointCaps = NULL;
	dListInit(&call->remoteFastStartOLCs);
	call->remoteTermCapSeqNo = 0;
	call->localTermCapSeqNo = 0;
	memcpy(&call->capPrefs, &gH323ep.capPrefs, sizeof(CHRCapPrefs));
	call->logicalChans = NULL;
	call->noOfLogicalChannels = 0;
	call->logicalChanNoBase = 1001;
	call->logicalChanNoMax = 1100;
	call->logicalChanNoCur = 1001;
	call->nextSessionID = 4; /* 1,2,3 are reserved for audio, video and data */
	dListInit(&call->timerList);
	call->msdRetries = 0;
	call->usrData = usrData;

	CHRTRACEINFO3("Created a new call (%s, %s)\n", call->callType,
		call->callToken);
	/* Add new call to calllist */
	chrAddCallToList(call);
	if (gH323ep.h323Callbacks.onNewCallCreated &&
		gH323ep.h323Callbacks.onNewCallCreated(call) == CHR_OK)
	{
		return call;
	}
	else 
	{
		CHRTRACEINFO3("ERROR:onNewCallCreated returned error (%s, %s)\n",
			call->callType, call->callToken);
		chrRemoveCallFromList(call);
		freeContext(pctxt);
		return NULL;
	}
}


int chrEndCall(CHRH323CallData *call)
/**
(1). change call state
(2). clear logical channels
(3). SendEndSessionCommand
(4). change h245 state
(5). SendReleaseComplete
*/
{
	CHRTRACEDBG4("In chrEndCall call state is - %s (%s, %s)\n",
		chrGetCallStateText(call->callState), call->callType,
		call->callToken);

	if (call->callState == CHR_CALL_CLEARED)
	{
		chrCleanCall(call);
		return CHR_OK;
	}

	if (call->logicalChans)
	{
		CHRTRACEINFO3("Clearing all logical channels. (%s, %s)\n", call->callType,
			call->callToken);
		chrClearAllLogicalChannels(call);
	}
	if (!CHR_TESTFLAG(call->flags, CHR_M_ENDSESSION_BUILT))
	{
		if (call->h245SessionState == CHR_H245SESSION_ACTIVE ||
			call->h245SessionState == CHR_H245SESSION_ENDRECVD)
		{
			chrSendEndSessionCommand(call);
			CHR_SETFLAG(call->flags, CHR_M_ENDSESSION_BUILT);
		}
	}


	if (!call->pH225Channel || call->pH225Channel->sock == 0)
	{
		call->callState = CHR_CALL_CLEARED;
	}
	else
	{
		if (!CHR_TESTFLAG(call->flags, CHR_M_RELEASE_BUILT))
		{
			if (call->callState == CHR_CALL_CLEAR ||
				call->callState == CHR_CALL_CLEAR_RELEASERECVD)
			{
				chrSendReleaseComplete(call);
				CHR_SETFLAG(call->flags, CHR_M_RELEASE_BUILT);
			}
		}
	}

	return CHR_OK;
}

int chrCleanCall(CHRH323CallData *call)
/**
(1). clean all the logical channels
(2). close h245 connection --> stop channel transmit/receive, remove channel
(3). close h245 linstener --> socket 
(4). close h225 connection 
(5). clean timers
(6). gk client clean
*/
{
	OOCTXT *pctxt;

	CHRTRACEWARN4("Cleaning Call (%s, %s)- reason:%s\n",
		call->callType, call->callToken,
		chrGetReasonCodeText(call->callEndReason));

	if (call->logicalChans)
		chrClearAllLogicalChannels(call);

	if (call->h245SessionState != CHR_H245SESSION_CLOSED)
		chrCloseH245Connection(call);
	else
	{
		if (call->pH245Channel && call->pH245Channel->outQueue.count > 0)
		{
			dListFreeAll(call->pctxt, &(call->pH245Channel->outQueue));
			memFreePtr(call->pctxt, call->pH245Channel);
		}
	}
	if (call->h245listener)
	{
		chrCloseH245Listener(call);
	}

	if (0 != call->pH225Channel && 0 != call->pH225Channel->sock)
	{
		chrCloseH225Connection(call);
	}

	if (call->timerList.count > 0)
	{
		dListFreeAll(call->pctxt, &(call->timerList));
	}

	chrRemoveCallFromList(call);
	CHRTRACEINFO3("Removed call (%s, %s) from list\n", call->callType,
		call->callToken);

	if (gH323ep.h323Callbacks.onCallCleared)
		gH323ep.h323Callbacks.onCallCleared(call);

	pctxt = call->pctxt;
	freeContext(pctxt);
	ASN1CRTFREE0(pctxt);
	return CHR_OK;
}



int chrAddCallToList(CHRH323CallData *call)
{
	if (!gH323ep.callList)
	{
		gH323ep.callList = call;
		call->next = NULL;
		call->prev = NULL;
	}
	else
	{
		call->next = gH323ep.callList;
		call->prev = NULL;
		gH323ep.callList->prev = call;
		gH323ep.callList = call;
	}
	return CHR_OK;
}

int chrRemoveCallFromList(CHRH323CallData *call)
{
	if (!call)
		return CHR_OK;

	if (call == gH323ep.callList)
	{
		if (!call->next)
			gH323ep.callList = NULL;
		else{
			call->next->prev = NULL;
			gH323ep.callList = call->next;
		}
	}
	else{
		call->prev->next = call->next;
		if (call->next)
			call->next->prev = call->prev;
	}
	return CHR_OK;
}

int chrCallSetCallerId(CHRH323CallData* call, const char* callerid)
{
	if (!call || !callerid) return CHR_FAILED;
	strncpy(call->ourCallerId, callerid, sizeof(call->ourCallerId) - 1);
	call->ourCallerId[sizeof(call->ourCallerId) - 1] = '\0';
	return CHR_OK;
}

int chrCallSetCallingPartyNumber(CHRH323CallData *call, const char *number)
{
	if (call->callingPartyNumber)
		memFreePtr(call->pctxt, call->callingPartyNumber);

	call->callingPartyNumber = (char*)memAlloc(call->pctxt, strlen(number) + 1);
	if (call->callingPartyNumber)
	{
		strcpy(call->callingPartyNumber, number);
	}
	else{
		CHRTRACEERR3("Error:Memory - chrCallSetCallingPartyNumber - "
			"callingPartyNumber.(%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	/* Set dialed digits alias */
	/*   if(!strcmp(call->callType, "outgoing"))
	{
	chrCallAddAliasDialedDigits(call, number);
	}*/
	return CHR_OK;
}

int chrCallGetCallingPartyNumber(CHRH323CallData *call, char *buffer, int len)
{
	if (call->callingPartyNumber)
	{
		if (len>(int)strlen(call->callingPartyNumber))
		{
			strcpy(buffer, call->callingPartyNumber);
			return CHR_OK;
		}
	}

	return CHR_FAILED;
}


int chrCallSetCalledPartyNumber(CHRH323CallData *call, const char *number)
{
	if (call->calledPartyNumber)
		memFreePtr(call->pctxt, call->calledPartyNumber);

	call->calledPartyNumber = (char*)memAlloc(call->pctxt, strlen(number) + 1);
	if (call->calledPartyNumber)
	{
		strcpy(call->calledPartyNumber, number);
	}
	else{
		CHRTRACEERR3("Error:Memory - chrCallSetCalledPartyNumber - "
			"calledPartyNumber.(%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	return CHR_OK;
}

int chrCallGetCalledPartyNumber(CHRH323CallData *call, char *buffer, int len)
{
	if (call->calledPartyNumber)
	{
		if (len>(int)strlen(call->calledPartyNumber))
		{
			strcpy(buffer, call->calledPartyNumber);
			return CHR_OK;
		}
	}

	return CHR_FAILED;
}

int chrCallClearAliases(CHRH323CallData *call)
{
	if (call->ourAliases)
		memFreePtr(call->pctxt, call->ourAliases);
	call->ourAliases = NULL;
	return CHR_OK;
}


int chrCallAddAlias(CHRH323CallData *call, int aliasType, const char *value, OOBOOL local)
{
	chrAliases * psNewAlias = NULL;
	psNewAlias = (chrAliases*)memAlloc(call->pctxt, sizeof(chrAliases));
	if (!psNewAlias)
	{
		CHRTRACEERR3("Error:Memory - chrCallAddAlias - psNewAlias"
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	psNewAlias->type = aliasType;
	psNewAlias->value = (char*)memAlloc(call->pctxt, strlen(value) + 1);
	if (!psNewAlias->value)
	{
		CHRTRACEERR3("Error:Memory - chrCallAddAlias - psNewAlias->value"
			" (%s, %s)\n", call->callType, call->callToken);
		memFreePtr(call->pctxt, psNewAlias);
		return CHR_FAILED;
	}
	strcpy(psNewAlias->value, value);

	if (local)
	{
		psNewAlias->next = call->ourAliases;
		call->ourAliases = psNewAlias;
	}
	else {
		psNewAlias->next = call->remoteAliases;
		call->remoteAliases = psNewAlias;
	}

	CHRTRACEDBG5("Added %s alias %s to call. (%s, %s)\n",
		local ? "local" : "remote", value, call->callType, call->callToken);
	return CHR_OK;
}

int chrCallAddAliasH323ID(CHRH323CallData *call, const char* h323id)
{
	return chrCallAddAlias(call, T_H225AliasAddress_h323_ID, h323id, TRUE);
}


int chrCallAddAliasDialedDigits(CHRH323CallData *call, const char* dialedDigits)
{
	return chrCallAddAlias
		(call, T_H225AliasAddress_dialedDigits, dialedDigits, TRUE);
}


int chrCallAddRemoteAliasH323ID(CHRH323CallData *call, const char* h323id)
{
	return chrCallAddAlias(call, T_H225AliasAddress_h323_ID, h323id, FALSE);
}

int chrCallAddRemoteAliasDialedDigits(CHRH323CallData *call, const char* dialedDigits)
{
	return chrCallAddAlias
		(call, T_H225AliasAddress_dialedDigits, dialedDigits, FALSE);
}


int chrCallAddG711Capability(CHRH323CallData *call, int cap, int txframes,
	int rxframes, int dir,
	cb_StartReceiveChannel startReceiveChannel,
	cb_StartTransmitChannel startTransmitChannel,
	cb_StopReceiveChannel stopReceiveChannel,
	cb_StopTransmitChannel stopTransmitChannel)
{
	return chrCapabilityAddSimpleCapability(call, cap, txframes, rxframes, FALSE,
		dir, startReceiveChannel, startTransmitChannel,
		stopReceiveChannel, stopTransmitChannel, FALSE);
}

int chrCallAddH264VideoCapability
(CHRH323CallData *call, int cap, CHRH264CapParams *params, int dir,
cb_StartReceiveChannel startReceiveChannel,
cb_StartTransmitChannel startTransmitChannel,
cb_StopReceiveChannel stopReceiveChannel,
cb_StopTransmitChannel stopTransmitChannel)
{

	return chrCapabilityAddH264VideoCapability(call, params, dir,
		startReceiveChannel, startTransmitChannel,
		stopReceiveChannel, stopTransmitChannel,
		FALSE);

}


CHRH323CallData* chrFindCallByToken(char *callToken)
{
	CHRH323CallData *call;
	if (!callToken)
	{
		CHRTRACEERR1("ERROR:Invalid call token passed - chrFindCallByToken\n");
		return NULL;
	}
	if (!gH323ep.callList)
	{
		CHRTRACEERR1("ERROR: Empty calllist - chrFindCallByToken failed\n");
		return NULL;
	}
	call = gH323ep.callList;
	while (call)
	{
		if (!strcmp(call->callToken, callToken))
			break;
		else
			call = call->next;
	}

	if (!call)
	{
		CHRTRACEERR2("ERROR:Call with token %s not found\n", callToken);
		return NULL;
	}
	return call;
}



/* Checks whether session with suplied ID and direction is already active*/
ASN1BOOL chrIsSessionEstablished(CHRH323CallData *call, int sessionID, char* dir)
{
	CHRLogicalChannel * temp = NULL;
	temp = call->logicalChans;
	while (temp)
	{
		if (temp->sessionID == sessionID              &&
			temp->state == CHR_LOGICALCHAN_ESTABLISHED &&
			!strcmp(temp->dir, dir))
			return TRUE;
		temp = temp->next;
	}
	return FALSE;
}

int chrAddMediaInfo(CHRH323CallData *call, CHRMediaInfo mediaInfo)
{
	CHRMediaInfo *newMediaInfo = NULL;

	if (!call)
	{
		CHRTRACEERR3("Error:Invalid 'call' param for chrAddMediaInfo.(%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}
	newMediaInfo = (CHRMediaInfo*)memAlloc(call->pctxt, sizeof(CHRMediaInfo));
	if (!newMediaInfo)
	{
		CHRTRACEERR3("Error:Memory - chrAddMediaInfo - newMediaInfo. "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	memcpy(newMediaInfo, &mediaInfo, sizeof(CHRMediaInfo));

	CHRTRACEDBG4("Configured mediainfo for cap %s (%s, %s)\n",
		chrGetCapTypeText(mediaInfo.cap),
		call->callType, call->callToken);
	if (!call->mediaInfo) 
	{
		newMediaInfo->next = NULL;
		call->mediaInfo = newMediaInfo;
	}
	else 
	{
		newMediaInfo->next = call->mediaInfo;
		call->mediaInfo = newMediaInfo;
	}
	return CHR_OK;
}

unsigned chrCallGenerateSessionID(CHRH323CallData *call, CHRCapType type, char *dir)
{
	unsigned sessionID = 0;

	if (type == CHR_CAP_TYPE_AUDIO)
	{
		if (!chrGetLogicalChannel(call, 1, dir))
		{
			sessionID = 1;
		}
		else
		{
			if (call->masterSlaveState == CHR_MasterSlave_Master)
				sessionID = call->nextSessionID++;
			else
			{
				CHRTRACEDBG4("Session id for %s channel of type audio has to be "
					"provided by remote.(%s, %s)\n", dir, call->callType,
					call->callToken);
				sessionID = 0; /* Will be assigned by remote */
			}
		}
	}

	if (type == CHR_CAP_TYPE_VIDEO)
	{
		if (!chrGetLogicalChannel(call, 2, dir))
		{
			sessionID = 2;
		}
		else
		{
			if (call->masterSlaveState == CHR_MasterSlave_Master)
				sessionID = call->nextSessionID++;
			else
			{
				sessionID = 0; /* Will be assigned by remote */
				CHRTRACEDBG4("Session id for %s channel of type video has to be "
					"provided by remote.(%s, %s)\n", dir, call->callType,
					call->callToken);
			}
		}
	}
	return sessionID;

}


int chrCallH245ConnectionRetryTimerExpired(void *data)
{
	chrTimerCallback *cbData = (chrTimerCallback*)data;
	CHRH323CallData *call = cbData->call;

	CHRTRACEINFO3("H245 connection retry timer expired. (%s, %s)\n",
		call->callType, call->callToken);
	memFreePtr(call->pctxt, cbData);

	call->h245ConnectionAttempts++;

	chrCreateH245Connection(call);

	return CHR_OK;
}

const char* chrGetReasonCodeText(OOUINT32 code)
{
	static const char* reasonCodeText[] = {
		"CHR_REASON_UNKNOWN",
		"CHR_REASON_INVALIDMESSAGE",
		"CHR_REASON_TRANSPORTFAILURE",
		"CHR_REASON_NOROUTE",
		"CHR_REASON_NOUSER",
		"CHR_REASON_NOBW",
		"CHR_REASON_GK_NOCALLEDUSER",
		"CHR_REASON_GK_NOCALLERUSER",
		"CHR_REASON_GK_NORESOURCES",
		"CHR_REASON_GK_UNREACHABLE",
		"CHR_REASON_GK_CLEARED",
		"CHR_REASON_NOCOMMON_CAPABILITIES",
		"CHR_REASON_REMOTE_FWDED",
		"CHR_REASON_LOCAL_FWDED",
		"CHR_REASON_REMOTE_CLEARED",
		"CHR_REASON_LOCAL_CLEARED",
		"CHR_REASON_REMOTE_BUSY",
		"CHR_REASON_LOCAL_BUSY",
		"CHR_REASON_REMOTE_NOANSWER",
		"CHR_REASON_LOCAL_NOTANSWERED",
		"CHR_REASON_REMOTE_REJECTED",
		"CHR_REASON_LOCAL_REJECTED",
		"CHR_REASON_REMOTE_CONGESTED",
		"CHR_REASON_LOCAL_CONGESTED"
	};
	return chrUtilsGetText(code, reasonCodeText, OONUMBEROF(reasonCodeText));
}

const char* chrGetCallStateText(CHRCallState callState)
{
	static const char* callStateText[] = {
		"CHR_CALL_CREATED",
		"CHR_CALL_WAITING_ADMISSION",
		"CHR_CALL_CONNECTING",
		"CHR_CALL_CONNECTED",
		"CHR_CALL_PAUSED",
		"CHR_CALL_CLEAR",
		"CHR_CALL_CLEAR_RELEASERECVD",
		"CHR_CALL_CLEAR_RELEASESENT",
		"CHR_CALL_CLEARED"
	};
	return chrUtilsGetText(callState, callStateText, OONUMBEROF(callStateText));
}

