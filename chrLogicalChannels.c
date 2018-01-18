#include "chrTrace.h"
#include "chrLogicalChannels.h"
#include "chrCallSession.h"
#include "chrH323Endpoint.h"


extern CHRH323EndPoint gH323ep; /** < global endpoint*/

CHRLogicalChannel* chrAddNewLogicalChannel(CHRH323CallData *call, int channelNo,
	int sessionID, char *dir,
	chrH323EpCapability *epCap)
	/**
	(1). new channel
	(2). assign port
	(3).
	*/
{
	CHRLogicalChannel *pNewChannel = NULL, *pChannel = NULL;
	CHRMediaInfo *pMediaInfo = NULL;
	CHRTRACEDBG5("Adding new media channel for cap %d dir %s (%s, %s)\n",
		epCap->cap, dir, call->callType, call->callToken);
	/* Create a new logical channel entry */
	pNewChannel = (CHRLogicalChannel*)memAlloc(call->pctxt, sizeof(CHRLogicalChannel));
	if (!pNewChannel)
	{
		CHRTRACEERR3("ERROR:Memory - ooAddNewLogicalChannel - pNewChannel "
			"(%s, %s)\n", call->callType, call->callToken);
		return NULL;
	}

	memset(pNewChannel, 0, sizeof(CHRLogicalChannel));
	pNewChannel->channelNo = channelNo;
	pNewChannel->sessionID = sessionID;
	pNewChannel->state = CHR_LOGICALCHAN_IDLE;
	pNewChannel->type = epCap->capType;
	/*   strcpy(pNewChannel->type, type);*/
	strcpy(pNewChannel->dir, dir);

	pNewChannel->chanCap = epCap;
	CHRTRACEDBG4("Adding new channel with cap %d (%s, %s)\n", epCap->cap, call->callType, call->callToken);
	/* As per standards, media control port should be same for all
	proposed channels with same session ID. However, most applications
	use same media port for transmit and receive of audio streams. Infact,
	testing of OpenH323 based asterisk assumed that same ports are used.
	Hence we first search for existing media ports for same session and use
	them. This should take care of all cases.
	*/
	if (call->mediaInfo)
	{
		pMediaInfo = call->mediaInfo;
		while (pMediaInfo)
		{
			if (!strcmp(pMediaInfo->dir, dir) &&
				(pMediaInfo->cap == epCap->cap))
			{
				break;
			}
			pMediaInfo = pMediaInfo->next;
		}
	}

	if (pMediaInfo)
	{
		CHRTRACEDBG3("Using configured media info (%s, %s)\n", call->callType, call->callToken);
		pNewChannel->localRtpPort = pMediaInfo->lMediaPort;
		pNewChannel->localRtcpPort = pMediaInfo->lMediaCntrlPort;
		/* If user application has not specified a specific ip and is using
		multihomed mode, substitute appropriate ip.
		*/
		if (!strcmp(pMediaInfo->lMediaIP, "0.0.0.0"))
			strcpy(pNewChannel->localIP, call->localIP);
		else
			strcpy(pNewChannel->localIP, pMediaInfo->lMediaIP);
	}
	else
	{
		CHRTRACEDBG3("Using default media info (%s, %s)\n", call->callType, call->callToken);
		pNewChannel->localRtpPort = chrGetNextPort(CHRRTP);

		/* Ensures that RTP port is an even one */
		if ((pNewChannel->localRtpPort & 1) == 1)
			pNewChannel->localRtpPort = chrGetNextPort(CHRRTP);

		pNewChannel->localRtcpPort = chrGetNextPort(CHRRTP);
		strcpy(pNewChannel->localIP, call->localIP);
	}

	/* Add new channel to the list */
	pNewChannel->next = NULL;
	if (!call->logicalChans)
	{
		call->logicalChans = pNewChannel;
	}
	else
	{
		pChannel = call->logicalChans;
		while (pChannel->next)  pChannel = pChannel->next;
		pChannel->next = pNewChannel;
	}

	/* increment logical channels */
	call->noOfLogicalChannels++;
	CHRTRACEINFO3("Created new logical channel entry (%s, %s)\n", call->callType,
		call->callToken);
	return pNewChannel;
}

CHRLogicalChannel* chrFindLogicalChannelByLogicalChannelNo(CHRH323CallData *call,
	int ChannelNo)
{
	CHRLogicalChannel *pLogicalChannel = NULL;
	if (!call->logicalChans)
	{
		CHRTRACEERR3("ERROR: No Open LogicalChannels - Failed "
			"FindLogicalChannelByChannelNo (%s, %s\n", call->callType,
			call->callToken);
		return NULL;
	}
	pLogicalChannel = call->logicalChans;
	while (pLogicalChannel)
	{
		if (pLogicalChannel->channelNo == ChannelNo)
			break;
		else
			pLogicalChannel = pLogicalChannel->next;
	}

	return pLogicalChannel;
}

CHRLogicalChannel * chrFindLogicalChannelByOLC(CHRH323CallData *call,
	H245OpenLogicalChannel *olc)
{
	H245DataType * psDataType = NULL;
	H245H2250LogicalChannelParameters * pslcp = NULL;
	CHRTRACEDBG4("ooFindLogicalChannel by olc %d (%s, %s)\n",
		olc->forwardLogicalChannelNumber, call->callType, call->callToken);
	if (olc->m.reverseLogicalChannelParametersPresent)
	{
		CHRTRACEDBG3("Finding receive channel (%s,%s)\n", call->callType,
			call->callToken);
		psDataType = &olc->reverseLogicalChannelParameters.dataType;
		/* Only H2250LogicalChannelParameters are supported */
		if (olc->reverseLogicalChannelParameters.multiplexParameters.t !=
			T_H245OpenLogicalChannel_reverseLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters){
			CHRTRACEERR4("Error:Invalid olc %d received (%s, %s)\n",
				olc->forwardLogicalChannelNumber, call->callType, call->callToken);
			return NULL;
		}
		pslcp = olc->reverseLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;

		return chrFindLogicalChannel(call, pslcp->sessionID, "receive", psDataType);
	}
	else
	{
		CHRTRACEDBG3("Finding transmit channel (%s, %s)\n", call->callType,
			call->callToken);
		psDataType = &olc->forwardLogicalChannelParameters.dataType;
		/* Only H2250LogicalChannelParameters are supported */
		if (olc->forwardLogicalChannelParameters.multiplexParameters.t !=
			T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
		{
			CHRTRACEERR4("Error:Invalid olc %d received (%s, %s)\n",
				olc->forwardLogicalChannelNumber, call->callType, call->callToken);
			return NULL;
		}
		pslcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
		return chrFindLogicalChannel(call, pslcp->sessionID, "transmit", psDataType);
	}
}

CHRLogicalChannel * chrFindLogicalChannel(CHRH323CallData *call, int sessionID,
	char *dir, H245DataType * dataType)
{
	CHRLogicalChannel * pChannel = NULL;
	pChannel = call->logicalChans;
	while (pChannel)
	{
		CHRTRACEDBG3("ooFindLogicalChannel, checking channel: %d:%s\n",
			pChannel->sessionID, pChannel->dir);
		if (pChannel->sessionID == sessionID)
		{
			if (!strcmp(pChannel->dir, dir))
			{
				CHRTRACEDBG3("ooFindLogicalChannel, comparing channel: %d:%s\n",
					pChannel->sessionID, pChannel->dir);
				if (!strcmp(dir, "receive"))
				{
					if (chrCapabilityCheckCompatibility(call, pChannel->chanCap,
						dataType, CHRRX)) {
						return pChannel;
					}
				}
				else if (!strcmp(dir, "transmit"))
				{
					if (chrCapabilityCheckCompatibility(call, pChannel->chanCap,
						dataType, CHRTX)) {
						return pChannel;
					}
				}
			}
		}
		pChannel = pChannel->next;
	}
	return NULL;
}

/* This function is used to get a logical channel with a particular session ID */
CHRLogicalChannel* chrGetLogicalChannel(CHRH323CallData *call, int sessionID, char *dir)
{
	CHRLogicalChannel * pChannel = NULL;
	pChannel = call->logicalChans;
	while (pChannel)
	{
		if (pChannel->sessionID == sessionID && !strcmp(pChannel->dir, dir))
			return pChannel;
		else
			pChannel = pChannel->next;
	}
	return NULL;
}

int chrClearAllLogicalChannels(CHRH323CallData *call)
/**
(1). iterator clear
*/
{
	CHRLogicalChannel * temp = NULL, *prev = NULL;

	CHRTRACEINFO3("Clearing all logical channels (%s, %s)\n", call->callType,
		call->callToken);

	temp = call->logicalChans;
	while (temp)
	{
		prev = temp;
		temp = temp->next;
		chrClearLogicalChannel(call, prev->channelNo);
	}
	call->logicalChans = NULL;
	return CHR_OK;
}

int chrClearLogicalChannel(CHRH323CallData *call, int channelNo)
/**
(1). find and clear
*/
{

	CHRLogicalChannel *pLogicalChannel = NULL;
	chrH323EpCapability *epCap = NULL;

	CHRTRACEDBG4("Clearing logical channel number %d. (%s, %s)\n", channelNo,
		call->callType, call->callToken);

	pLogicalChannel = chrFindLogicalChannelByLogicalChannelNo(call, channelNo);
	if (!pLogicalChannel)
	{
		CHRTRACEWARN4("Logical Channel %d doesn't exist, in clearLogicalChannel."
			" (%s, %s)\n",
			channelNo, call->callType, call->callToken);
		return CHR_OK;
	}

	epCap = (chrH323EpCapability*)pLogicalChannel->chanCap;
	if (!strcmp(pLogicalChannel->dir, "receive"))
	{
		if (epCap->stopReceiveChannel)
		{
			epCap->stopReceiveChannel(call, pLogicalChannel);
			CHRTRACEINFO4("Stopped Receive channel %d (%s, %s)\n",
				channelNo, call->callType, call->callToken);
		}
		else{
			CHRTRACEERR4("ERROR:No callback registered for stopReceiveChannel %d "
				"(%s, %s)\n", channelNo, call->callType, call->callToken);
		}
	}
	else
	{
		if (pLogicalChannel->state == CHR_LOGICALCHAN_ESTABLISHED)
		{
			if (epCap->stopTransmitChannel)
			{
				epCap->stopTransmitChannel(call, pLogicalChannel);
				CHRTRACEINFO4("Stopped Transmit channel %d (%s, %s)\n",
					channelNo, call->callType, call->callToken);
			}
			else{
				CHRTRACEERR4("ERROR:No callback registered for stopTransmitChannel"
					" %d (%s, %s)\n", channelNo, call->callType,
					call->callToken);
			}
		}
	}
	chrRemoveLogicalChannel(call, channelNo);/* TODO: efficiency - This causes re-search of
											 of logical channel in the list. Can be
											 easily improved.*/
	return CHR_OK;
}

int chrRemoveLogicalChannel(CHRH323CallData *call, int ChannelNo)
{
	CHRLogicalChannel * temp = NULL, *prev = NULL;
	if (!call->logicalChans)
	{
		CHRTRACEERR4("ERROR:Remove Logical Channel - Channel %d not found "
			"Empty channel List(%s, %s)\n", ChannelNo, call->callType,
			call->callToken);
		return CHR_FAILED;
	}

	temp = call->logicalChans;
	while (temp)
	{
		if (temp->channelNo == ChannelNo)
		{
			if (!prev)   call->logicalChans = temp->next;
			else   prev->next = temp->next;
			/* A double-free may occur here because the capability record
			may be shared between receive and transmit logical channel
			records.  Commenting out for now. Added diags to determine
			how frequently this record is allocated in order to allow
			proper clean-up. (ED, 7/14/2010)
			memFreePtr(call->pctxt, temp->chanCap);
			*/
			memFreePtr(call->pctxt, temp);
			CHRTRACEDBG4("Removed logical channel %d (%s, %s)\n", ChannelNo,
				call->callType, call->callToken);
			call->noOfLogicalChannels--;
			return CHR_OK;
		}
		prev = temp;
		temp = temp->next;
	}

	CHRTRACEERR4("ERROR:Remove Logical Channel - Channel %d not found "
		"(%s, %s)\n", ChannelNo, call->callType, call->callToken);
	return CHR_FAILED;
}

int chrOnLogicalChannelEstablished(CHRH323CallData *call, CHRLogicalChannel * pChannel)
{
	CHRLogicalChannel * temp = NULL, *prev = NULL;
	CHRTRACEDBG3("In ooOnLogicalChannelEstablished (%s, %s)\n",
		call->callType, call->callToken);
	pChannel->state = CHR_LOGICALCHAN_ESTABLISHED;
	temp = call->logicalChans;
	while (temp)
	{
		if (temp->channelNo != pChannel->channelNo &&
			temp->sessionID == pChannel->sessionID &&
			!strcmp(temp->dir, pChannel->dir))
		{
			prev = temp;
			temp = temp->next;
			chrClearLogicalChannel(call, prev->channelNo);
		}
		else
			temp = temp->next;
	}
	return CHR_OK;
}

