#include "chrTrace.h"
#include "chrCapability.h"
#include "chrCallSession.h"
#include "chrH323Endpoint.h"
/** Global endpoint structure */
extern CHRH323EndPoint gH323ep;

static int giDynamicRTPPayloadType = 101; 
/* Used for g711 ulaw/alaw, g728, g729 and g7231 */
int chrCapabilityAddSimpleCapability
(CHRH323CallData *call, int cap, int txframes,
int rxframes, OOBOOL silenceSuppression, int dir,
cb_StartReceiveChannel startReceiveChannel,
cb_StartTransmitChannel startTransmitChannel,
cb_StopReceiveChannel stopReceiveChannel,
cb_StopTransmitChannel stopTransmitChannel,
OOBOOL remote)
/**
(1). assign chrH323EpCapability/CHRCapParams
(2). add local capability
(3). add remote capability
*/
{
	chrH323EpCapability *epCap = NULL, *cur = NULL;
	CHRCapParams *params = NULL;
	OOCTXT *pctxt = NULL;
	if (!call) pctxt = &gH323ep.ctxt;
	else pctxt = call->pctxt;

	epCap = (chrH323EpCapability*)memAlloc(pctxt, sizeof(chrH323EpCapability));
	params = (CHRCapParams*)memAlloc(pctxt, sizeof(CHRCapParams));
	if (!epCap || !params)
	{
		CHRTRACEERR1("ERROR: Memory - ooCapabilityAddSimpleCapability - "
			"epCap/params\n");
		return CHR_FAILED;
	}
	CHRTRACEDBG2("New H323EpCapability record allocated (%x)\n", epCap);

	params->txframes = txframes;
	params->rxframes = rxframes;
	params->silenceSuppression = FALSE; /* Set to false for g711 and g729*/

	if (dir & CHRRXANDTX) {
		epCap->dir = CHRRX;
		epCap->dir |= CHRTX;
	}
	else {
		epCap->dir = dir;
	}

	epCap->cap = cap;
	epCap->capType = CHR_CAP_TYPE_AUDIO;
	epCap->params = (void*)params;
	epCap->startReceiveChannel = startReceiveChannel;
	epCap->startTransmitChannel = startTransmitChannel;
	epCap->stopReceiveChannel = stopReceiveChannel;
	epCap->stopTransmitChannel = stopTransmitChannel;
	epCap->next = NULL;

	if (0 == call)
	{
		CHRTRACEDBG2("Adding endpoint capability %s. \n",
			chrGetCapTypeText(epCap->cap));

		chrAppendCapToCapList(&gH323ep.myCaps, epCap);
		chrAppendCapToCapPrefs(NULL, cap);
		gH323ep.noOfCaps++;
	}
	else if (remote)
	{
		CHRTRACEDBG4("Adding remote call-specific capability %s. (%s, %s)\n",
			chrGetCapTypeText(epCap->cap), call->callType,
			call->callToken);

		chrAppendCapToCapList(&call->remoteCaps, epCap);
	}
	else 
	{
		CHRTRACEDBG4("Adding call-specific capability %s. (%s, %s)\n",
			chrGetCapTypeText(epCap->cap), call->callType,
			call->callToken);

		if (!call->ourCaps){
			chrResetCapPrefs(call);
		}
		chrAppendCapToCapList(&call->ourCaps, epCap);
		chrAppendCapToCapPrefs(call, cap);
	}

	return CHR_OK;
}

int chrCapabilityAddH264VideoCapability(struct CHRH323CallData *call,
	CHRH264CapParams *capParams, int dir,
	cb_StartReceiveChannel startReceiveChannel,
	cb_StartTransmitChannel startTransmitChannel,
	cb_StopReceiveChannel stopReceiveChannel,
	cb_StopTransmitChannel stopTransmitChannel,
	OOBOOL remote)
/**
(1). assign chrH323EpCapability/CHRCapParams
(2). add local capability
(3). add remote capability
*/
{
	chrH323EpCapability *epCap = NULL, *cur = NULL;
	CHRH264CapParams *params = NULL;
	OOCTXT *pctxt = NULL;
	int cap = CHR_H264VIDEO;

	if (!call) pctxt = &gH323ep.ctxt;
	else pctxt = call->pctxt;

	epCap = (chrH323EpCapability*)memAllocZ(pctxt, sizeof(chrH323EpCapability));
	params = (CHRH264CapParams*)memAllocZ(pctxt, sizeof(CHRH264CapParams));
	if (!epCap || !params)
	{
		CHRTRACEERR1("Error:Memory - ooCapabilityAddH264Capability - epCap/params"
			".\n");
		return CHR_FAILED;
	}

	memcpy(params, capParams, sizeof(*params));

	if (dir & CHRRXANDTX)
	{
		epCap->dir = CHRRX;
		epCap->dir |= CHRTX;
	}
	else
		epCap->dir = dir;

	epCap->cap = CHR_H264VIDEO;
	epCap->capType = CHR_CAP_TYPE_VIDEO;
	epCap->params = (void*)params;
	epCap->startReceiveChannel = startReceiveChannel;
	epCap->startTransmitChannel = startTransmitChannel;
	epCap->stopReceiveChannel = stopReceiveChannel;
	epCap->stopTransmitChannel = stopTransmitChannel;

	epCap->next = NULL;

	if (0 == call)
	{/*Add as local capability */
		CHRTRACEDBG1("Adding endpoint H264 video capability.\n");
		chrAppendCapToCapList(&gH323ep.myCaps, epCap);
		chrAppendCapToCapPrefs(NULL, cap);
		gH323ep.noOfCaps++;
	}
	else if (remote) {
		/*Add as remote capability */
		chrAppendCapToCapList(&call->remoteCaps, epCap);
	}
	else {
		/*Add as our capability */
		CHRTRACEDBG3("Adding call specific H264 video capability. "
			"(%s, %s)\n", call->callType,
			call->callToken);

		if (0 == call->ourCaps) {
			chrResetCapPrefs(call);
		}
		chrAppendCapToCapList(&call->ourCaps, epCap);
		chrAppendCapToCapPrefs(call, cap);
	}

	return CHR_OK;
}


struct H245VideoCapability* chrCapabilityCreateVideoCapability
	(chrH323EpCapability *epCap, OOCTXT *pctxt, int dir)
{

	if (!epCap)
	{
		CHRTRACEERR1("Error:Invalid capability parameter passed to "
			"ooCapabilityCreateVideoCapability.\n");
		return NULL;
	}

	if (!(epCap->dir & dir))
	{
		CHRTRACEERR1("Error:Failed to create capability due to direction "
			"mismatch.\n");
		return NULL;
	}

	switch (epCap->cap)
	{
	case CHR_H264VIDEO:
		return chrCapabilityCreateH264VideoCapability(epCap, pctxt, dir);
	default:
		CHRTRACEERR2("ERROR: Don't know how to create video capability %s\n",
			chrGetCapTypeText(epCap->cap));
	}
	return NULL;
}



struct H245AudioCapability* chrCapabilityCreateAudioCapability
	(chrH323EpCapability *epCap, OOCTXT *pctxt, int dir)
{

	if (!epCap)
	{
		CHRTRACEERR1("Error:Invalid capability parameter passed to "
			"ooCapabilityCreateAudioCapability.\n");
		return NULL;
	}

	if (!(epCap->dir & dir))
	{
		CHRTRACEERR1("Error:Failed to create capability due to direction "
			"mismatch.\n");
		return NULL;
	}

	switch (epCap->cap)
	{
	case CHR_G711ALAW64K:
	case CHR_G711ULAW64K:
		return chrCapabilityCreateG711AudioCapability(epCap, pctxt, dir);
	default:
		CHRTRACEERR2("ERROR: Don't know how to create audio capability %d\n",
			epCap->cap);
	}
	return NULL;
}

struct H245VideoCapability* chrCapabilityCreateH264VideoCapability(chrH323EpCapability *epCap, OOCTXT* pctxt, int dir)
{
	H245VideoCapability *pVideo = NULL;
	CHRH264CapParams *params = NULL;
	H245GenericCapability *pGenericCap = NULL;

	DList* collapsing = NULL;
	H245GenericParameter *profile;
	H245GenericParameter *level;
	H245GenericParameter *customMaxBRandCPB;
	H245GenericParameter *customMaxMBPS; /** chr add line */
	H245GenericParameter *customMaxFS; /** chr add line */
	H245GenericParameter *sampleAspectRatiosSupported;

	if (!epCap || !epCap->params)
	{
		CHRTRACEERR1("Error:Invalid capability parameters to "
			"ooCapabilityCreateH264VideoCapability.\n");
		return NULL;
	}
	params = (CHRH264CapParams*)epCap->params;

	pVideo = (H245VideoCapability*)memAllocZ(pctxt,sizeof(H245VideoCapability));
	pGenericCap = (H245GenericCapability*)memAllocZ(pctxt,sizeof(H245GenericCapability));
	if (!pVideo || !pGenericCap)
	{
		CHRTRACEERR1("ERROR:Memory - ooCapabilityCreateH264VideoCapability - "
			"pVideo/pGenericCap\n");
		return NULL;
	}
	{
		/** H.241 parameters */
		pVideo->t = T_H245VideoCapability_genericVideoCapability;
		pVideo->u.genericVideoCapability = pGenericCap;
		pGenericCap->maxBitRate = params->maxBitRate;
		pGenericCap->capabilityIdentifier.t = 1;
		pGenericCap->capabilityIdentifier.u.standard = memAllocTypeZ(pctxt, ASN1OBJID);
		pGenericCap->capabilityIdentifier.u.standard->numids = 7;
		pGenericCap->capabilityIdentifier.u.standard->subid[0] = 0;
		pGenericCap->capabilityIdentifier.u.standard->subid[1] = 0;
		pGenericCap->capabilityIdentifier.u.standard->subid[2] = 8;
		pGenericCap->capabilityIdentifier.u.standard->subid[3] = 241;
		pGenericCap->capabilityIdentifier.u.standard->subid[4] = 0;
		pGenericCap->capabilityIdentifier.u.standard->subid[5] = 0;
		pGenericCap->capabilityIdentifier.u.standard->subid[6] = 1;
		pGenericCap->m.maxBitRatePresent = 1;
		pGenericCap->m.collapsingPresent = 1;
		collapsing = &pGenericCap->collapsing;
		dListInit(collapsing);

		profile = (H245GenericParameter*)memAllocZ(pctxt,sizeof(H245GenericParameter));
		profile->parameterIdentifier.t = 1;
		profile->parameterIdentifier.u.standard = 41;
		profile->parameterValue.t = 2;
		profile->parameterValue.u.booleanArray = 72;

		level = (H245GenericParameter*)memAllocZ(pctxt,sizeof(H245GenericParameter));
		level->parameterIdentifier.t = 1;
		level->parameterIdentifier.u.standard = 42;
		level->parameterValue.t = 3;
		level->parameterValue.u.unsignedMin = 85;

		/** chr add begin */
		customMaxMBPS = (H245GenericParameter*)memAllocZ(pctxt, sizeof(H245GenericParameter));
		customMaxMBPS->parameterIdentifier.t = 1;
		customMaxMBPS->parameterIdentifier.u.standard = 3;
		customMaxMBPS->parameterValue.t = 3;
		customMaxMBPS->parameterValue.u.unsignedMin = 972;

		customMaxFS = (H245GenericParameter*)memAllocZ(pctxt, sizeof(H245GenericParameter));
		customMaxFS->parameterIdentifier.t = 1;
		customMaxFS->parameterIdentifier.u.standard = 4;
		customMaxFS->parameterValue.t = 3;
		customMaxFS->parameterValue.u.unsignedMin = 32;
		/** chr add end */
		customMaxBRandCPB = (H245GenericParameter*)memAllocZ(pctxt,sizeof(H245GenericParameter));
		customMaxBRandCPB->parameterIdentifier.t = 1;
		customMaxBRandCPB->parameterIdentifier.u.standard = 6;
		customMaxBRandCPB->parameterValue.t = 3;
		customMaxBRandCPB->parameterValue.u.unsignedMin = 667;

		sampleAspectRatiosSupported = (H245GenericParameter*)memAllocZ(pctxt,sizeof(H245GenericParameter));
		sampleAspectRatiosSupported->parameterIdentifier.t = 1;
		sampleAspectRatiosSupported->parameterIdentifier.u.standard = 10;
		sampleAspectRatiosSupported->parameterValue.t = 3;
		sampleAspectRatiosSupported->parameterValue.u.unsignedMin = 13;

		dListAppend(pctxt, collapsing, profile);
		dListAppend(pctxt, collapsing, level);
		dListAppend(pctxt, collapsing, customMaxMBPS); /** chr add line */
		dListAppend(pctxt, collapsing, customMaxFS); /** chr add line */
		//dListAppend(pctxt, collapsing, customMaxBRandCPB); /** chr delete line */
		//dListAppend(pctxt, collapsing, sampleAspectRatiosSupported); /** chr delete line */
		return pVideo;
	}

	CHRTRACEERR1("ERROR:Unknown Video Capability - ooCapabilityCreateH264VideoCapability - "
		"pVideo/pGenericCap\n");
	return NULL;
}

struct H245AudioCapability* chrCapabilityCreateG711AudioCapability(chrH323EpCapability *epCap, OOCTXT* pctxt, int dir)
{
	H245AudioCapability *pAudio = NULL;
	CHRCapParams *params;
	if (!epCap || !epCap->params)
	{
		CHRTRACEERR1("Error:Invalid capability parameters to "
			"ooCapabilityCreateSimpleCapability.\n");
		return NULL;
	}
	params = (CHRCapParams*)epCap->params;
	pAudio = (H245AudioCapability*)memAlloc(pctxt,sizeof(H245AudioCapability));
	if (!pAudio)
	{
		CHRTRACEERR1("ERROR:Memory - ooCapabilityCreateSimpleCapability - pAudio\n");
		return NULL;
	}


	switch (epCap->cap)
	{
	case CHR_G711ALAW64K:
		pAudio->t = T_H245AudioCapability_g711Alaw64k;
		if (dir & CHRRX)
			pAudio->u.g711Alaw64k = params->rxframes;
		else
			pAudio->u.g711Alaw64k = params->txframes;
		return pAudio;
	case CHR_G711ULAW64K:
		pAudio->t = T_H245AudioCapability_g711Ulaw64k;
		if (dir & CHRRX)
			pAudio->u.g711Ulaw64k = params->rxframes;
		else
			pAudio->u.g711Ulaw64k = params->txframes;
		return pAudio;
	default:
		CHRTRACEERR2("ERROR: Don't know how to create audio capability %d\n",
			epCap->cap);
	}
	return NULL;
}

ASN1BOOL chrCapabilityCheckCompatibility_Audio(CHRH323CallData *call, chrH323EpCapability* epCap,H245AudioCapability* audioCap, int dir)
{
	int noofframes = 0, cap;

	CHRTRACEDBG2("Comparing channel with codec type: %d\n", audioCap->t);

	switch (audioCap->t)
	{
	case T_H245AudioCapability_g711Ulaw64k:
		cap = CHR_G711ULAW64K;
		noofframes = audioCap->u.g711Ulaw64k;
		break;
	case T_H245AudioCapability_g711Alaw64k:
		cap = CHR_G711ALAW64K;
		noofframes = audioCap->u.g711Alaw64k;
		break;
	default:
		return FALSE;
	}

	CHRTRACEDBG3("Comparing codecs: current=%d, requested=%d\n",epCap->cap, cap);
	if (cap != epCap->cap) { return FALSE; }

	/* Can we receive this capability */
	if (dir & CHRRX)
	{
		CHRTRACEDBG3("Comparing RX frame rate: channel's=%d, requested=%d\n",
			((CHRCapParams*)epCap->params)->rxframes, noofframes);
		if (((CHRCapParams*)epCap->params)->rxframes >= noofframes) {
			return TRUE;
		}
		//else {
		//  not supported, as already told other ep our max. receive rate
		//  our ep can't receive more rate than it
		//  return FALSE;
		//}
	}

	/* Can we transmit compatible stream */
	if (dir & CHRTX)
	{
		CHRTRACEDBG3("Comparing TX frame rate: channel's=%d, requested=%d\n",
			((CHRCapParams*)epCap->params)->txframes, noofframes);
		if (((CHRCapParams*)epCap->params)->txframes <= noofframes) {
			return TRUE;
		}
		//else {
		//   TODO: reduce our ep transmission rate, as peer EP has low receive
		//   cap, than return TRUE
		//}
	}
	return FALSE;

}



OOBOOL chrCapabilityCheckCompatibility_Video(CHRH323CallData *call, chrH323EpCapability* epCap,H245VideoCapability* videoCap, int dir)
{
	switch (videoCap->t)
	{
	default:
		CHRTRACEDBG3("ooCapabilityCheckCompatibility_Video - Unsupported video "
			"capability. (%s, $s)\n", call->callType, call->callToken);
	}
	return FALSE;
}

OOBOOL chrCapabilityCheckCompatibility(struct CHRH323CallData *call, chrH323EpCapability* epCap,H245DataType* dataType, int dir)
{
	switch (dataType->t)
	{
	case T_H245DataType_audioData:
		if (epCap->capType == CHR_CAP_TYPE_AUDIO)
			return chrCapabilityCheckCompatibility_Audio(call, epCap,
			dataType->u.audioData, dir);
		break;
	case T_H245DataType_videoData:
		if (epCap->capType == CHR_CAP_TYPE_VIDEO)
			return chrCapabilityCheckCompatibility_Video(call, epCap,
			dataType->u.videoData, dir);
		break;
	case T_H245DataType_data:
	default:
		CHRTRACEDBG3("ooCapabilityCheckCompatibility - Unsupported  "
			"capability. (%s, $s)\n", call->callType, call->callToken);
	}

	return FALSE;
}

chrH323EpCapability* chrIsAudioDataTypeSimpleSupported(CHRH323CallData *call, H245AudioCapability* audioCap, int dir)
{
	int cap, framesPerPkt = 0;
	chrH323EpCapability *cur = NULL, *epCap = NULL;
	CHRCapParams * params = NULL;

	/* Find similar capability */
	switch (audioCap->t)
	{
	case T_H245AudioCapability_g711Alaw64k:
		framesPerPkt = audioCap->u.g711Alaw64k;
		cap = CHR_G711ALAW64K;
		break;
	case T_H245AudioCapability_g711Ulaw64k:
		framesPerPkt = audioCap->u.g711Ulaw64k;
		cap = CHR_G711ULAW64K;
		break;
	default:
		return NULL;
	}

	CHRTRACEDBG4("Determined Simple audio data type to be of type %s. Searching"
		" for matching capability.(%s, %s)\n",
		chrGetCapTypeText(cap), call->callType, call->callToken);

	/* If we have call specific caps, we use them; otherwise use general
	endpoint caps
	*/
	if (call->ourCaps)
		cur = call->ourCaps;
	else
		cur = gH323ep.myCaps;

	while (cur)
	{
		CHRTRACEDBG4("Local cap being compared %s. (%s, %s)\n",
			chrGetCapTypeText(cur->cap), call->callType, call->callToken);

		if (cur->cap == cap && (cur->dir & dir))
			break;
		cur = cur->next;
	}

	if (!cur) return NULL;

	CHRTRACEDBG4("Found matching simple audio capability type %s. Comparing"
		" other parameters. (%s, %s)\n", chrGetCapTypeText(cap),
		call->callType, call->callToken);

	/* can we receive this capability */
	if (dir & CHRRX)
	{
		if (((CHRCapParams*)cur->params)->rxframes < framesPerPkt)
			return NULL;
		else{
			CHRTRACEDBG4("We can receive Simple capability %s. (%s, %s)\n",
				chrGetCapTypeText(cur->cap), call->callType,
				call->callToken);
			epCap = (chrH323EpCapability*)memAlloc(call->pctxt,	sizeof(chrH323EpCapability));
			params = (CHRCapParams*)memAlloc(call->pctxt, sizeof(CHRCapParams));
			if (!epCap || !params)
			{
				CHRTRACEERR3("Error:Memory - ooIsAudioDataTypeSimpleSupported - "
					"epCap/params (%s, %s)\n", call->callType,
					call->callToken);
				return NULL;
			}
			epCap->params = params;
			epCap->cap = cur->cap;
			epCap->dir = cur->dir;
			epCap->capType = cur->capType;
			epCap->startReceiveChannel = cur->startReceiveChannel;
			epCap->startTransmitChannel = cur->startTransmitChannel;
			epCap->stopReceiveChannel = cur->stopReceiveChannel;
			epCap->stopTransmitChannel = cur->stopTransmitChannel;
			epCap->next = NULL;
			memcpy(epCap->params, cur->params, sizeof(CHRCapParams));
			CHRTRACEDBG4("Returning copy of matched receive capability %s. "
				"(%s, %s)\n",
				chrGetCapTypeText(cur->cap), call->callType,
				call->callToken);
			return epCap;
		}
	}

	/* Can we transmit compatible stream */
	if (dir & CHRTX)
	{
		CHRTRACEDBG4("We can transmit Simple capability %s. (%s, %s)\n",
			chrGetCapTypeText(cur->cap), call->callType,
			call->callToken);
		epCap = (chrH323EpCapability*)memAlloc(call->pctxt,
			sizeof(chrH323EpCapability));
		params = (CHRCapParams*)memAlloc(call->pctxt, sizeof(CHRCapParams));
		if (!epCap || !params)
		{
			CHRTRACEERR3("Error:Memory - ooIsAudioDataTypeSimpleSupported - "
				"epCap/params (%s, %s)\n", call->callType,
				call->callToken);
			return NULL;
		}
		epCap->params = params;
		epCap->cap = cur->cap;
		epCap->dir = cur->dir;
		epCap->capType = cur->capType;
		epCap->startReceiveChannel = cur->startReceiveChannel;
		epCap->startTransmitChannel = cur->startTransmitChannel;
		epCap->stopReceiveChannel = cur->stopReceiveChannel;
		epCap->stopTransmitChannel = cur->stopTransmitChannel;
		epCap->next = NULL;
		memcpy(epCap->params, cur->params, sizeof(CHRCapParams));
		if (params->txframes > framesPerPkt)
		{
			CHRTRACEINFO5("Reducing framesPerPkt for transmission of Simple "
				"capability from %d to %d to match receive capability of"
				" remote endpoint.(%s, %s)\n", params->txframes,
				framesPerPkt, call->callType, call->callToken);
			params->txframes = framesPerPkt;
		}
		CHRTRACEDBG4("Returning copy of matched transmit capability %s."
			"(%s, %s)\n",
			chrGetCapTypeText(cur->cap), call->callType,
			call->callToken);
		return epCap;
	}
	return NULL;
}

chrH323EpCapability* chrIsAudioDataTypeSupported(CHRH323CallData *call, H245AudioCapability* audioCap, int dir)
{
	/* Find similar capability */
	switch (audioCap->t)
	{
	case T_H245AudioCapability_g711Alaw64k:
	case T_H245AudioCapability_g711Ulaw64k:
		return chrIsAudioDataTypeSimpleSupported(call, audioCap, dir);
	default:
		return NULL;
	}
}


int chrParseGenericH264Params(H245GenericCapability* pGenCap, CHRH264CapParams *params)
{
	H245GenericParameter *h245GenericParameter = NULL;
	unsigned int i = 0;
	DListNode* curNode = NULL;

	params->maxBitRate = 192400;
	params->profile = 0x41;

	if (pGenCap->m.maxBitRatePresent == 1)
	{
		params->maxBitRate = pGenCap->maxBitRate;
	}

	if (pGenCap->m.collapsingPresent == 1)
	{
		for (i = 0; i < pGenCap->collapsing.count; i++)
		{
			curNode = dListFindByIndex(&pGenCap->collapsing, i);
			if (curNode != NULL){
				h245GenericParameter = (H245GenericParameter*)curNode->data;
				if (h245GenericParameter != NULL)
				{
					if (h245GenericParameter->parameterIdentifier.t == 1)
					{
						switch (h245GenericParameter->parameterIdentifier.u.standard)
						{
						case 41:/* profile */
							params->profile = h245GenericParameter->parameterValue.u.booleanArray;
							break;
						case 42:/* level */
							params->level = h245GenericParameter->parameterValue.u.unsignedMin;
							break;
						default:
							break;
						}
					}
				}
			}
		}
	}
	return CHR_OK;
}

chrH323EpCapability* chrIsVideoDataTypeH264Supported(CHRH323CallData *call, H245GenericCapability* pGenCap, int dir)
{
	int cap;
	chrH323EpCapability *cur = NULL, *epCap = NULL;
	CHRH264CapParams *params = NULL;

	cap = CHR_H264VIDEO;

	CHRTRACEDBG3("Looking for H264 video capability. (%s, %s)\n",
		call->callType, call->callToken);

	/* If we have call specific caps, we use them; otherwise use general
	endpoint caps
	*/
	if (call->ourCaps)
		cur = call->ourCaps;
	else
		cur = gH323ep.myCaps;

	while (cur)
	{
		CHRTRACEDBG6("Local cap being compared %d/%d:%s. (%s, %s)\n",
			cap, cur->cap, chrGetCapTypeText(cur->cap), call->callType, call->callToken);

		if (cur->cap == cap && (cur->dir & dir))
		{
			break; //todo: compare other params
		}
		cur = cur->next;
	}

	if (!cur) return NULL;

	CHRTRACEDBG4("Found matching H.264 video capability type %s. Comparing"
		" other parameters. (%s, %s)\n", chrGetCapTypeText(cap),
		call->callType, call->callToken);
	if (dir & CHRRX)
	{
		{
			epCap = (chrH323EpCapability*)memAlloc(call->pctxt,
			sizeof(chrH323EpCapability));
			params = (CHRH264CapParams*)memAlloc(call->pctxt,
				sizeof(CHRH264CapParams));
			if (!epCap || !params)
			{
				CHRTRACEERR3("Error:Memory - ooIsVideoDataTypeH264Supported - "
					"epCap/params. (%s, %s)\n", call->callType,
					call->callToken);
				return NULL;
			}
			memset(params, 0, sizeof(*params));
			chrParseGenericH264Params(pGenCap, params);
			epCap->params = params;
			epCap->cap = cur->cap;
			epCap->dir = cur->dir;
			epCap->capType = cur->capType;
			epCap->startReceiveChannel = cur->startReceiveChannel;
			epCap->startTransmitChannel = cur->startTransmitChannel;
			epCap->stopReceiveChannel = cur->stopReceiveChannel;
			epCap->stopTransmitChannel = cur->stopTransmitChannel;
			epCap->next = NULL;
			// memcpy(epCap->params, cur->params, sizeof(CHRH264CapParams));
			CHRTRACEDBG4("Returning copy of matched receive capability %s. "
				"(%s, %s)\n", chrGetCapTypeText(cur->cap), call->callType,
				call->callToken);
			return epCap;
		}
	}
	if (dir & CHRTX)
	{
		epCap = (chrH323EpCapability*)memAlloc(call->pctxt,
			sizeof(chrH323EpCapability));
		params = (CHRH264CapParams*)memAlloc(call->pctxt,
			sizeof(CHRH264CapParams));
		if (!epCap || !params)
		{
			CHRTRACEERR3("Error:Memory - ooIsVideoDataTypeH264Supported - "
				"epCap/params. (%s, %s)\n", call->callType,
				call->callToken);
			return NULL;
		}
		memset(params, 0, sizeof(*params));
		chrParseGenericH264Params(pGenCap, params);
		epCap->params = params;
		memcpy(params, cur->params, sizeof(*params));

		epCap->cap = cur->cap;
		epCap->dir = cur->dir;
		epCap->capType = cur->capType;
		epCap->startReceiveChannel = cur->startReceiveChannel;
		epCap->startTransmitChannel = cur->startTransmitChannel;
		epCap->stopReceiveChannel = cur->stopReceiveChannel;
		epCap->stopTransmitChannel = cur->stopTransmitChannel;
		epCap->next = NULL;
		// memcpy(epCap->params, cur->params, sizeof(CHRH264CapParams));

		CHRTRACEDBG4("Returning copy of matched receive capability %s. "
			"(%s, %s)\n", chrGetCapTypeText(cur->cap), call->callType,
			call->callToken);
		return epCap;
	}
	return NULL;

}

int chrIsGenericH264Video(H245GenericCapability *pGenCap)
{
	if (pGenCap->capabilityIdentifier.t == 1) {// standard
		if (7 == pGenCap->capabilityIdentifier.u.standard->numids) {
			ASN1UINT *subid = pGenCap->capabilityIdentifier.u.standard->subid;

			// printf("capacityIdentifer: %d.%d.%d.%d.%d.%d.%d\n",
			//    subid[0], subid[1], subid[2], subid[3], subid[4], subid[5], subid[6]);

			if (subid[0] == 0 && subid[1] == 0 && subid[2] == 8 && subid[3] == 241 &&
				subid[4] == 0 && subid[5] == 0 && subid[6] == 1) { //0.0.8.241.0.0.1 = H264
				return 1;
			}
		}
	}
	return 0;
}

chrH323EpCapability* chrIsVideoDataTypeGenericSupported(CHRH323CallData *call, H245GenericCapability* pGenCap, int dir)
{
	if (chrIsGenericH264Video(pGenCap)) {
		return chrIsVideoDataTypeH264Supported(call, pGenCap, dir);
	}

	return NULL;
}

chrH323EpCapability* chrIsVideoDataTypeSupported(CHRH323CallData *call, H245VideoCapability* pVideoCap, int dir)
{
	switch (pVideoCap->t)
	{
	case T_H245VideoCapability_genericVideoCapability:
		return chrIsVideoDataTypeGenericSupported(call, pVideoCap->u.genericVideoCapability, dir);
		break;
	default:
		CHRTRACEDBG1("Unsupported video capability type in "
			"ooIsVideoDataTypeSupported\n");
		return NULL;
	}
	return NULL;
}

chrH323EpCapability* chrIsDataTypeSupported(CHRH323CallData *call, H245DataType *data, int dir)
{
	CHRTRACEDBG3("Looking for data type support. (%s, %s)\n", call->callType,
		call->callToken);

	switch (data->t)
	{
	case T_H245DataType_videoData:
		CHRTRACEDBG3("Looking for video dataType support. (%s, %s)\n",
			call->callType, call->callToken);
		return chrIsVideoDataTypeSupported(call, data->u.videoData, dir);
	case T_H245DataType_audioData:
		CHRTRACEDBG3("Looking for audio dataType support. (%s, %s)\n",
			call->callType, call->callToken);
		return chrIsAudioDataTypeSupported(call, data->u.audioData, dir);
	case T_H245DataType_data:
		CHRTRACEDBG3("Data type not supported.(%s, %s)\n",
			call->callType, call->callToken);
		return NULL;
	default:
		CHRTRACEINFO3("Unknown data type (%s, %s)\n", call->callType,
			call->callToken);
	}
	return NULL;
}

int chrResetCapPrefs(CHRH323CallData *call)
{
	if (0 != call) {
		CHRTRACEINFO3("Reset capabilities preferences. (%s, %s)\n",
			call->callType, call->callToken);
		memset(&call->capPrefs, 0, sizeof(CHRCapPrefs));
	}
	else {
		CHRTRACEINFO1("Reset capabilities preferences in endpoint\n");
		memset(&gH323ep.capPrefs, 0, sizeof(CHRCapPrefs));
	}

	return CHR_OK;
}

int chrRemoveCapFromCapPrefs(CHRH323CallData *call, int cap)
{
	int i = 0, j = 0;
	CHRCapPrefs *capPrefs = NULL, oldPrefs;
	if (call)
		capPrefs = &call->capPrefs;
	else
		capPrefs = &gH323ep.capPrefs;

	memcpy(&oldPrefs, capPrefs, sizeof(CHRCapPrefs));
	memset(capPrefs, 0, sizeof(CHRCapPrefs));
	for (i = 0; i<oldPrefs.index; i++)
	{
		if (oldPrefs.order[i] != cap)
			capPrefs->order[j++] = oldPrefs.order[i];
	}
	capPrefs->index = j;
	return CHR_OK;
}

void chrAppendCapToCapList(chrH323EpCapability** pphead, chrH323EpCapability* pcap)
{
	if (0 == *pphead) {
		*pphead = pcap;
	}
	else {
		chrH323EpCapability* p = *pphead;
		while (0 != p->next) p = p->next;
		p->next = pcap;
	}
}

int chrAppendCapToCapPrefs(CHRH323CallData *call, int cap)
{
	CHRCapPrefs* capPrefs = (0 != call) ? &call->capPrefs : &gH323ep.capPrefs;
	capPrefs->order[capPrefs->index++] = cap;

	CHRTRACEINFO3("Appended %d to capPrefs, count = %d\n",
		cap, capPrefs->index);

	return CHR_OK;
}

int chrChangeCapPrefOrder(CHRH323CallData *call, int cap, int pos)
{
	int i = 0, j = 0;
	CHRCapPrefs *capPrefs = NULL;

	/* Whether to change prefs for call or for endpoint as a whole */
	if (call)
		capPrefs = &call->capPrefs;
	else
		capPrefs = &gH323ep.capPrefs;

	/* check whether cap exists, cap must exist */
	for (i = 0; i<capPrefs->index; i++)
	{
		if (capPrefs->order[i] == cap)
			break;
	}
	if (i == capPrefs->index) return CHR_FAILED;

	if (i == pos) return CHR_OK; /* No need to change */

	/* Decrease Pref order */
	if (i < pos)
	{
		for (; i<pos; i++)
			capPrefs->order[i] = capPrefs->order[i + 1];
		capPrefs->order[i] = cap;
		return CHR_OK;
	}
	/* Increase Pref order */
	if (i>pos)
	{
		for (j = i; j>pos; j--)
			capPrefs->order[j] = capPrefs->order[j - 1];
		capPrefs->order[j] = cap;
		return CHR_OK;
	}

	return CHR_FAILED;

}

int chrPreppendCapToCapPrefs(CHRH323CallData *call, int cap)
{
	int i = 0, j = 0;
	CHRCapPrefs *capPrefs = NULL, oldPrefs;
	if (call)
		capPrefs = &call->capPrefs;
	else
		capPrefs = &gH323ep.capPrefs;

	memcpy(&oldPrefs, capPrefs, sizeof(CHRCapPrefs));


	capPrefs->order[j++] = cap;

	for (i = 0; i<oldPrefs.index; i++)
	{
		if (oldPrefs.order[i] != cap)
			capPrefs->order[j++] = oldPrefs.order[i];
	}
	capPrefs->index = j;
	return CHR_OK;
}


int chrAddRemoteCapability(CHRH323CallData *call, H245Capability *cap)
{
	switch (cap->t)
	{
	case T_H245Capability_receiveAudioCapability:
		return chrAddRemoteAudioCapability(call, cap->u.receiveAudioCapability,
			CHRRX);
	case T_H245Capability_transmitAudioCapability:
		return chrAddRemoteAudioCapability(call, cap->u.transmitAudioCapability,
			CHRTX);
	case T_H245Capability_receiveAndTransmitAudioCapability:
		return chrAddRemoteAudioCapability(call,
			cap->u.receiveAndTransmitAudioCapability, CHRRXTX);
	default:
		CHRTRACEDBG4("Unsupported cap type [%d] encountered. Ignoring. (%s, %s)\n",
			cap->t, call->callType, call->callToken);
	}
	return CHR_OK;
}

int chrAddRemoteAudioCapability(CHRH323CallData *call,
	H245AudioCapability *audioCap,
	int dir)
{
	int rxframes = 0, txframes = 0;

	switch (audioCap->t)
	{
	case T_H245AudioCapability_g711Alaw64k:
		if (dir&CHRTX) txframes = audioCap->u.g711Alaw64k;
		else if (dir&CHRRX) rxframes = audioCap->u.g711Alaw64k;
		else{
			txframes = audioCap->u.g711Alaw64k;
			rxframes = audioCap->u.g711Alaw64k;
		}
		return chrCapabilityAddSimpleCapability(call, CHR_G711ALAW64K, txframes,
			rxframes, FALSE, dir, NULL, NULL, NULL, NULL, TRUE);
	case T_H245AudioCapability_g711Ulaw64k:
		if (dir&CHRTX) txframes = audioCap->u.g711Ulaw64k;
		else if (dir&CHRRX) rxframes = audioCap->u.g711Ulaw64k;
		else{
			txframes = audioCap->u.g711Ulaw64k;
			rxframes = audioCap->u.g711Ulaw64k;
		}
		return chrCapabilityAddSimpleCapability(call, CHR_G711ULAW64K, txframes,
			rxframes, FALSE, dir, NULL, NULL, NULL, NULL, TRUE);

	default:
		CHRTRACEDBG3("Unsupported audio capability type %d:%s\n",
			audioCap->t, ooH245AudioCapText(audioCap->t));

	}

	return CHR_OK;
}


int chrCapabilityUpdateJointCapabilities(CHRH323CallData* call, H245Capability *cap)
{
	chrH323EpCapability * epCap = NULL, *cur = NULL;
	CHRTRACEDBG3("checking whether we need to add cap to joint capabilities"
		"(%s, %s)\n", call->callType, call->callToken);

	switch (cap->t)
	{
	case T_H245Capability_receiveAudioCapability:
		epCap = chrIsAudioDataTypeSupported
			(call, cap->u.receiveAudioCapability, CHRTX);
		break;

	case T_H245Capability_transmitAudioCapability:
		epCap = chrIsAudioDataTypeSupported
			(call, cap->u.transmitAudioCapability, CHRRX);
		break;

	case T_H245Capability_receiveAndTransmitAudioCapability:
		epCap = chrIsAudioDataTypeSupported
			(call, cap->u.receiveAndTransmitAudioCapability, CHRRX | CHRTX);
		break;

	case T_H245Capability_receiveVideoCapability:
		return chrCapabilityUpdateJointCapabilitiesVideo
			(call, cap->u.receiveVideoCapability, CHRTX);

	case T_H245Capability_transmitVideoCapability:
		return chrCapabilityUpdateJointCapabilitiesVideo
			(call, cap->u.transmitVideoCapability, CHRRX);

	case T_H245Capability_receiveAndTransmitVideoCapability:
		return chrCapabilityUpdateJointCapabilitiesVideo
			(call, cap->u.receiveAndTransmitVideoCapability, CHRRX | CHRTX);

	default:
		CHRTRACEDBG4("Unsupported cap type [%d] encountered. Ignoring. (%s, %s)\n",
			cap->t, call->callType, call->callToken);
	}

	if (epCap)
	{
		CHRTRACEDBG3("Adding cap to joint capabilities(%s, %s)\n", call->callType,
			call->callToken);
		/* Note:we add jointCaps in remote endpoints preference order.*/
		if (!call->jointCaps)
			call->jointCaps = epCap;
		else {
			cur = call->jointCaps;
			while (cur->next) cur = cur->next;
			cur->next = epCap;
		}

		return CHR_OK;
	}

	CHRTRACEDBG3("Not adding to joint capabilities. (%s, %s)\n", call->callType,
		call->callToken);
	return CHR_OK;
}



int chrCapabilityUpdateJointCapabilitiesVideo
(CHRH323CallData *call, H245VideoCapability *videoCap, int dir)
{
	switch (videoCap->t)
	{
	case T_H245VideoCapability_genericVideoCapability:
		if (chrIsGenericH264Video(videoCap->u.genericVideoCapability)) {
			CHRTRACEDBG3("ooCapabilityUpdateJointCapabilitiesVideo - Received H264 "
				"capability type. (%s, %s)\n", call->callType, call->callToken);
			return chrCapabilityUpdateJointCapabilitiesVideoH264(call,
				videoCap->u.genericVideoCapability, dir);
		}
		else {
			CHRTRACEDBG3("ooCapabilityUpdateJointCapabilitiesVideo - Unsupported"
				"capability type. (%s, %s)\n", call->callType, call->callToken);

		}
	default:
		CHRTRACEDBG3("ooCapabilityUpdateJointCapabilitiesVideo - Unsupported"
			"capability type. (%s, %s)\n", call->callType,
			call->callToken);
	}
	return CHR_OK;
}


int chrCapabilityUpdateJointCapabilitiesVideoH264(CHRH323CallData *call, H245GenericCapability *pGenCap, int dir)
{
	chrH323EpCapability *epCap = NULL, *cur = NULL;
	if (1)
	{
		epCap = chrIsVideoDataTypeH264Supported(call, pGenCap, dir);
		if (epCap)
		{
			CHRTRACEDBG3("Adding H264 to joint capabilities(%s, %s)\n",
				call->callType, call->callToken);
			/* Note:we add jointCaps in remote endpoints preference order.*/
			if (!call->jointCaps)
				call->jointCaps = epCap;
			else {
				cur = call->jointCaps;
				while (cur->next) cur = cur->next;
				cur->next = epCap;
			}

		}
	}

	return CHR_OK;
}

const char* chrGetCapTypeText(CHRCapabilities cap)
{
	static const char *capTypes[] = {
		"CHR_NONSTANDARD",
		"CHR_G711ALAW64K",
		"CHR_G711ULAW64K",
		"CHR_H264VIDEO",
	};

	return chrUtilsGetText(cap, capTypes, OONUMBEROF(capTypes));
}

void chrCapabilityDiagPrint(const chrH323EpCapability* pvalue)
{
	CHRTRACEINFO2("Capability: %s\n", chrGetCapTypeText(pvalue->cap));

	CHRTRACEINFO1("  direction:");
	if (pvalue->dir & CHRRX) { CHRTRACEINFO1(" receive"); }
	if (pvalue->dir & CHRTX) { CHRTRACEINFO1(" transmit"); }
	CHRTRACEINFO1("\n");

	CHRTRACEINFO1("  type:");
	switch (pvalue->capType) {
	case CHR_CAP_TYPE_AUDIO: CHRTRACEINFO1(" audio\n"); break;
	case CHR_CAP_TYPE_VIDEO: CHRTRACEINFO1(" video\n"); break;
	case CHR_CAP_TYPE_DATA:  CHRTRACEINFO1(" data\n"); break;
	default: CHRTRACEINFO2(" ? (%d)\n", pvalue->capType);
	}

	/* TODO: print params specific to each capability type */
}
