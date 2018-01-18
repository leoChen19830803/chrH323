
#include "ooasn1.h"
#include <time.h>
#include <ctype.h>
#include "printHandler.h"
#include "chrTrace.h"
#include "chrTypes.h"
#include "chrCapability.h"
#include "chrLogicalChannels.h"
#include "chrChannels.h"
#include "chrCallSession.h"
#include "chrH323Endpoint.h"
#include "chrH323Protocol.h"


/****************************** RAS ******************************/
//todo:
/****************************** Q931 ******************************/
/** Global endpoint structure */
extern CHRH323EndPoint gH323ep;
static ASN1OBJID gProtocolID = { 6, { 0, 0, 8, 2250, 0, 4 }
};


EXTERN int chrQ931Decode(CHRH323CallData *call,
	Q931Message* msg, int length, ASN1OCTET *data, OOBOOL doCallbacks)
{
	int offset, x;
	int rv = ASN_OK;
	char number[128];
	OOCTXT *pctxt = &gH323ep.msgctxt;

	dListInit(&msg->ies); /* clear information elements list */

	if (length < 5)  /* Packet tchr short */
		return Q931_E_TOOSHORT;

	msg->protocolDiscriminator = data[0];
	CHRTRACEDBG2("   protocolDiscriminator = %d\n", msg->protocolDiscriminator);
	if (data[1] != 2) /* Call reference must be 2 bytes long */
		return Q931_E_INVCALLREF;

	msg->callReference = ((data[2] & 0x7f) << 8) | data[3];

	CHRTRACEDBG2("   callReference = %d\n", msg->callReference);

	msg->fromDestination = (data[2] & 0x80) != 0;
	if (msg->fromDestination)
		CHRTRACEDBG1("   from = destination\n");
	else
		CHRTRACEDBG1("   from = originator\n");


	msg->messageType = data[4];
	CHRTRACEDBG2("   messageType = %x\n", msg->messageType);


	/* Have preamble, start getting the informationElements into buffers */
	offset = 5;
	while (offset < length) {
		Q931InformationElement *ie;
		int ieOff = offset;
		/* Get field discriminator */
		int discriminator = data[offset++];

		/* For discriminator with high bit set there is no data */
		if ((discriminator & 0x80) == 0) {
			int len = data[offset++], alen;

			if (discriminator == Q931UserUserIE) {
				/* Special case of User-user field, there is some confusion here as
				the Q931 documentation claims the length is a single byte,
				unfortunately all H.323 based apps have a 16 bit length here, so
				we allow for said longer length. There is presumably an addendum
				to Q931 which describes this, and provides a means to
				discriminate between the old 1 byte and the new 2 byte systems.
				However, at present we assume it is always 2 bytes until we find
				something that breaks it.
				*/
				len <<= 8;
				len |= data[offset++];

				/* we also have a protocol discriminator, which we ignore */
				offset++;
				len--;
			}

			/* watch out for negative lengths! (ED, 11/5/03) */
			if (len < 0) {
				return Q931_E_INVLENGTH;
			}
			else if (offset + len > length) {
				alen = 0;
				len = -len;
				rv = Q931_E_INVLENGTH;
			}
			else alen = len;

			ie = (Q931InformationElement*)
				memAlloc(pctxt, sizeof(*ie) - sizeof(ie->data) + alen);
			if (!ie)
			{
				CHRTRACEERR3("Error:Memory - chrQ931Decode - ie(%s, %s)\n",
					call->callType, call->callToken);
				return CHR_FAILED;
			}
			ie->discriminator = discriminator;
			ie->offset = ieOff;
			ie->length = len;
			if (alen != 0)
				memcpy(ie->data, data + offset, alen);
			offset += len;
		}
		else {
			ie = (Q931InformationElement*)memAlloc(pctxt,
				sizeof(*ie) - sizeof(ie->data));
			if (!ie)
			{
				CHRTRACEERR3("Error:Memory - chrQ931Decode - ie(%s, %s)\n",
					call->callType, call->callToken);
				return CHR_FAILED;
			}
			ie->discriminator = discriminator;
			ie->offset = offset;
			ie->length = 0;
		}
		if (ie->discriminator == Q931BearerCapabilityIE)
		{
			CHRTRACEDBG1("   Bearer-Capability IE = {\n");
			for (x = 0; x<ie->length; x++)
			{
				if (x == 0)
					CHRTRACEDBG2("      %x", ie->data[x]);
				else
					CHRTRACEDBG2(", %x", ie->data[x]);
			}
			CHRTRACEDBG1("   }\n");
		}
		if (ie->discriminator == Q931DisplayIE)
		{
			CHRTRACEDBG1("   Display IE = {\n");
			CHRTRACEDBG2("      %s\n", ie->data);
			CHRTRACEDBG1("   }\n");
		}
		/* Extract calling party number TODO:Give respect to presentation and
		screening indicators ;-) */
		if (ie->discriminator == Q931CallingPartyNumberIE)
		{
			CHRTRACEDBG1("   CallingPartyNumber IE = {\n");
			if (ie->length < CHR_MAX_NUMBER_LENGTH)
			{
				int numoffset = 1;
				if (!(0x80 & ie->data[0])) numoffset = 2;
				memcpy(number, ie->data + numoffset, ie->length - numoffset);
				number[ie->length - numoffset] = '\0';
				CHRTRACEDBG2("      %s\n", number);
				if (!call->callingPartyNumber)
					chrCallSetCallingPartyNumber(call, number);
			}
			else{
				CHRTRACEERR3("Error:Calling party number tchr long. (%s, %s)\n",
					call->callType, call->callToken);
			}
			CHRTRACEDBG1("   }\n");
		}

		/* Extract called party number */
		if (ie->discriminator == Q931CalledPartyNumberIE)
		{
			CHRTRACEDBG1("   CalledPartyNumber IE = {\n");
			if (ie->length < CHR_MAX_NUMBER_LENGTH)
			{
				memcpy(number, ie->data + 1, ie->length - 1);
				number[ie->length - 1] = '\0';
				CHRTRACEDBG2("      %s\n", number);
				if (!call->calledPartyNumber)
					chrCallSetCalledPartyNumber(call, number);
			}
			else{
				CHRTRACEERR3("Error:Calling party number tchr long. (%s, %s)\n",
					call->callType, call->callToken);
			}
			CHRTRACEDBG1("   }\n");
		}

		/* Handle Cause ie */
		if (ie->discriminator == Q931CauseIE)
		{
			msg->causeIE = ie;
			CHRTRACEDBG1("   Cause IE = {\n");
			CHRTRACEDBG2("      %s\n", chrGetQ931CauseValueText(ie->data[1] & 0x7f));
			CHRTRACEDBG1("   }\n");
		}

		/* TODO: Get rid of ie list.*/
		dListAppend(pctxt, &msg->ies, ie);
		if (rv != ASN_OK)
			return rv;
	}

	/*cisco router sends Q931Notify without UU ie,
	we just ignore notify message as of now as handling is optional for
	end point*/
	if (msg->messageType != Q931NotifyMsg)
		rv = chrDecodeUUIE(msg);
	return rv;
}

EXTERN Q931InformationElement* chrQ931GetIE(const Q931Message* q931msg,
	int ieCode)
{
	DListNode* curNode;
	unsigned int i;

	for (i = 0, curNode = q931msg->ies.head; i < q931msg->ies.count; i++) {
		Q931InformationElement *ie = (Q931InformationElement*)curNode->data;
		if (ie->discriminator == ieCode) {
			return ie;
		}
		curNode = curNode->next;
	}
	return NULL;
}
/** internal function */
char* chrQ931GetMessageTypeName(int messageType, char* buf) {
	switch (messageType) {
	case Q931AlertingMsg:
		strcpy(buf, "Alerting");
		break;
	case Q931CallProceedingMsg:
		strcpy(buf, "CallProceeding");
		break;
	case Q931ConnectMsg:
		strcpy(buf, "Connect");
		break;
	case Q931ConnectAckMsg:
		strcpy(buf, "ConnectAck");
		break;
	case Q931ProgressMsg:
		strcpy(buf, "Progress");
		break;
	case Q931SetupMsg:
		strcpy(buf, "Setup");
		break;
	case Q931SetupAckMsg:
		strcpy(buf, "SetupAck");
		break;
	case Q931FacilityMsg:
		strcpy(buf, "Facility");
		break;
	case Q931ReleaseCompleteMsg:
		strcpy(buf, "ReleaseComplete");
		break;
	case Q931StatusEnquiryMsg:
		strcpy(buf, "StatusEnquiry");
		break;
	case Q931StatusMsg:
		strcpy(buf, "Status");
		break;
	case Q931InformationMsg:
		strcpy(buf, "Information");
		break;
	case Q931NationalEscapeMsg:
		strcpy(buf, "Escape");
		break;
	default:
		sprintf(buf, "<%u>", messageType);
	}
	return buf;
}
/** internal function */
char* chrQ931GetIEName(int number, char* buf) {
	switch (number) {
	case Q931BearerCapabilityIE:
		strcpy(buf, "Bearer-Capability");
		break;
	case Q931CauseIE:
		strcpy(buf, "Cause");
		break;
	case Q931FacilityIE:
		strcpy(buf, "Facility");
		break;
	case Q931ProgressIndicatorIE:
		strcpy(buf, "Progress-Indicator");
		break;
	case Q931CallStateIE:
		strcpy(buf, "Call-State");
		break;
	case Q931DisplayIE:
		strcpy(buf, "Display");
		break;
	case Q931SignalIE:
		strcpy(buf, "Signal");
		break;
	case Q931CallingPartyNumberIE:
		strcpy(buf, "Calling-Party-Number");
		break;
	case Q931CalledPartyNumberIE:
		strcpy(buf, "Called-Party-Number");
		break;
	case Q931RedirectingNumberIE:
		strcpy(buf, "Redirecting-Number");
		break;
	case Q931UserUserIE:
		strcpy(buf, "User-User");
		break;
	default:
		sprintf(buf, "0x%02x", number);
	}
	return buf;
}

EXTERN void chrQ931Print(const Q931Message* q931msg) {
	char buf[1000];
	DListNode* curNode;
	unsigned int i;

	printf("Q.931 Message:\n");
	printf("   protocolDiscriminator: %i\n", q931msg->protocolDiscriminator);
	printf("   callReference: %i\n", q931msg->callReference);
	printf("   from: %s\n", (q931msg->fromDestination ?
		"destination" : "originator"));
	printf("   messageType: %s (0x%X)\n\n",
		chrQ931GetMessageTypeName(q931msg->messageType, buf),
		q931msg->messageType);

	for (i = 0, curNode = q931msg->ies.head; i < q931msg->ies.count; i++) {
		Q931InformationElement *ie = (Q931InformationElement*)curNode->data;
		int length = (ie->length >= 0) ? ie->length : -ie->length;
		printf("   IE[%i] (offset 0x%X):\n", i, ie->offset);
		printf("      discriminator: %s (0x%X)\n",
			chrQ931GetIEName(ie->discriminator, buf), ie->discriminator);
		printf("      data length: %i\n", length);

		curNode = curNode->next;
		printf("\n");
	}
}

int chrCreateQ931Message(Q931Message **q931msg, int msgType)
{
	OOCTXT *pctxt = &gH323ep.msgctxt;

	*q931msg = (Q931Message*)memAllocZ(pctxt, sizeof(Q931Message));

	if (!*q931msg)
	{
		CHRTRACEERR1("Error:Memory -  chrCreateQ931Message - q931msg\n");
		return CHR_FAILED;
	}
	else
	{
		(*q931msg)->protocolDiscriminator = 8;
		(*q931msg)->fromDestination = FALSE;
		(*q931msg)->messageType = msgType;
		(*q931msg)->tunneledMsgType = msgType;
		(*q931msg)->logicalChannelNo = 0;
		(*q931msg)->bearerCapabilityIE = NULL;
		(*q931msg)->callingPartyNumberIE = NULL;
		(*q931msg)->calledPartyNumberIE = NULL;
		(*q931msg)->causeIE = NULL;
		return CHR_OK;
	}
}


int chrGenerateCallToken(char *callToken, size_t size)
{
	static int counter = 1;
	char aCallToken[200];
	int  ret = 0;

	sprintf(aCallToken, "chrh323c_%d", counter++);

	if (counter > CHR_MAX_CALL_TOKEN)
		counter = 1;

	if ((strlen(aCallToken) + 1) < size)
		strcpy(callToken, aCallToken);
	else {
		CHRTRACEERR1("Error: Insufficient buffer size to generate call token");
		ret = CHR_FAILED;
	}


	return ret;
}

/* CallReference is a two octet field, thus max value can be 0xffff
or 65535 decimal. We restrict max value to 32760, however, this should
not cause any problems as there won't be those many simultaneous calls
CallRef has to be locally unique and generated by caller.
*/
ASN1USINT chrGenerateCallReference()
{
	static ASN1USINT lastCallRef = 0;
	ASN1USINT newCallRef = 0;


	if (lastCallRef == 0)
	{
		/* Generate a new random callRef */
		srand((unsigned)time(0));
		lastCallRef = (ASN1USINT)(rand() % 100);
	}
	else
		lastCallRef++;

	/* Note callReference can be at the most 15 bits that is from 0 to 32767.
	if we generate number bigger than that, bring it in range.
	*/
	if (lastCallRef >= 32766)
		lastCallRef = 1;

	newCallRef = lastCallRef;


	CHRTRACEDBG2("Generated callRef %d\n", newCallRef);
	return newCallRef;
}


static int genGloballyUniqueID(ASN1OCTET guid[16])
{
	ASN1INT64 timestamp;
	int i;
#ifdef _WIN32
	SYSTEMTIME systemTime;
	GetLocalTime(&systemTime);
	SystemTimeToFileTime(&systemTime, (LPFILETIME)&timestamp);
#else
	struct timeval systemTime;
	gettimeofday(&systemTime, NULL);
	timestamp = systemTime.tv_sec * 10000000 + systemTime.tv_usec * 10;
#endif
	guid[0] = 'c';
	guid[1] = 'h';
	guid[2] = 'r';
	guid[3] = 'h';
	guid[4] = '3';
	guid[5] = '2';
	guid[6] = '3';
	guid[7] = '-';

	for (i = 8; i < 16; i++)
		guid[i] = (ASN1OCTET)((timestamp >> ((i - 8 + 1) * 8)) & 0xff);

	return 0;
}

int chrGenerateCallIdentifier(H225CallIdentifier *callid)
{
	callid->guid.numocts = 16;
	return genGloballyUniqueID(callid->guid.data);
}

int chrFreeQ931Message(Q931Message *q931Msg)
{
	if (!q931Msg)
	{
		memReset(&gH323ep.msgctxt);
	}
	return CHR_OK;
}

int chrEncodeUUIE(Q931Message *q931msg)
{
	ASN1OCTET msgbuf[1024];
	ASN1OCTET * msgptr = NULL;
	int  len;
	ASN1BOOL aligned = TRUE;
	Q931InformationElement* ie = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;
	/*   memset(msgbuf, 0, sizeof(msgbuf));*/
	if (!q931msg)
	{
		CHRTRACEERR1("ERROR: Invalid Q931 message in add user-user IE\n");
		return CHR_FAILED;
	}

	if (!q931msg->userInfo)
	{
		CHRTRACEERR1("ERROR: No User-User IE to encode\n");
		return CHR_FAILED;
	}

	setPERBuffer(pctxt, msgbuf, sizeof(msgbuf), aligned);

	if (asn1PE_H225H323_UserInformation(pctxt,
		q931msg->userInfo) == ASN_OK)
	{
		CHRTRACEDBG1("UserInfo encoding - successful\n");
	}
	else{
		CHRTRACEERR1("ERROR: UserInfo encoding failed\n");
		return CHR_FAILED;
	}
	msgptr = encodeGetMsgPtr(pctxt, &len);

	/* Allocate memory to hold complete UserUser Information */
	ie = (Q931InformationElement*)memAlloc(pctxt,
		sizeof(*ie) - sizeof(ie->data) + len);
	if (ie == NULL)
	{
		CHRTRACEERR1("Error: Memory -  chrEncodeUUIE - ie\n");
		return CHR_FAILED;
	}
	ie->discriminator = Q931UserUserIE;
	ie->length = len;
	memcpy(ie->data, msgptr, len);
	/* Add the user to user IE NOTE: ALL IEs SHOULD BE IN ASCENDING ORDER OF
	THEIR DISCRIMINATOR AS PER SPEC.
	*/
	dListInit(&(q931msg->ies));
	if ((dListAppend(pctxt,
		&(q931msg->ies), ie)) == NULL)
	{
		CHRTRACEERR1("Error: Failed to add UUIE in outgoing message\n");
		return CHR_FAILED;
	}

	return CHR_OK;
}

int chrDecodeUUIE(Q931Message *q931Msg)
{
	DListNode* curNode;
	unsigned int i;
	ASN1BOOL aligned = TRUE;
	int stat;
	Q931InformationElement *ie = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;
	if (q931Msg == NULL)
	{
		CHRTRACEERR1("Error: chrDecodeUUIE failed - NULL q931 message\n");
		return CHR_FAILED;
	}

	/* Search for UserUser IE */
	for (i = 0, curNode = q931Msg->ies.head; i < q931Msg->ies.count;
		i++, curNode = curNode->next)
	{
		ie = (Q931InformationElement*)curNode->data;
		if (ie->discriminator == Q931UserUserIE)
			break;
	}
	if (i == q931Msg->ies.count)
	{
		CHRTRACEERR1("No UserUser IE found in chrDecodeUUIE\n");
		return CHR_FAILED;
	}

	/* Decode user-user ie */
	q931Msg->userInfo = (H225H323_UserInformation *)memAlloc(pctxt,
		sizeof(H225H323_UserInformation));
	if (!q931Msg->userInfo)
	{
		CHRTRACEERR1("ERROR:Memory - chrDecodeUUIE - userInfo\n");
		return CHR_FAILED;
	}
	memset(q931Msg->userInfo, 0, sizeof(H225H323_UserInformation));

	setPERBuffer(pctxt, ie->data, ie->length, aligned);

	stat = asn1PD_H225H323_UserInformation(pctxt, q931Msg->userInfo);
	if (stat != ASN_OK)
	{
		CHRTRACEERR1("Error: UserUser IE decode failed\n");
		return CHR_FAILED;
	}
	CHRTRACEDBG1("UUIE decode successful\n");
	return CHR_OK;
}

#ifndef _COMPACT
static void chrQ931PrintMessage
(CHRH323CallData* call, ASN1OCTET *msgbuf, ASN1UINT msglen)
{

	OOCTXT *pctxt = &gH323ep.msgctxt;
	Q931Message q931Msg;
	int ret;

	initializePrintHandler(&printHandler, "Q931 Message");

	/* Set event handler */
	setEventHandler(pctxt, &printHandler);

	setPERBuffer(pctxt, msgbuf, msglen, TRUE);

	ret = chrQ931Decode(call, &q931Msg, msglen, msgbuf, FALSE);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed decoding Q931 message. (%s, %s)\n",
			call->callType, call->callToken);
	}
	finishPrint();
	removeEventHandler(pctxt);

}
#endif

int chrEncodeH225Message(CHRH323CallData *call, Q931Message *pq931Msg,
	ASN1OCTET* msgbuf, size_t size)
{
	int len = 0, i = 0, j = 0, ieLen = 0;
	int stat = 0;
	DListNode* curNode = NULL;

	if (!msgbuf || size<200)
	{
		CHRTRACEERR3("Error: Invalid message buffer/size for chrEncodeH245Message."
			" (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	if (pq931Msg->messageType == Q931SetupMsg){
		msgbuf[i++] = CHRSetup;
	}
	else if (pq931Msg->messageType == Q931ConnectMsg){
		msgbuf[i++] = CHRConnect;
	}
	else if (pq931Msg->messageType == Q931CallProceedingMsg){
		msgbuf[i++] = CHRCallProceeding;
	}
	else if (pq931Msg->messageType == Q931AlertingMsg){
		msgbuf[i++] = CHRAlert;
	}
	else if (pq931Msg->messageType == Q931ReleaseCompleteMsg){
		msgbuf[i++] = CHRReleaseComplete;
	}
	else if (pq931Msg->messageType == Q931InformationMsg){
		msgbuf[i++] = CHRInformationMessage;
	}
	else if (pq931Msg->messageType == Q931StatusMsg){
		msgbuf[i++] = CHRStatus;
	}
	else if (pq931Msg->messageType == Q931FacilityMsg){
		msgbuf[i++] = CHRFacility;
		msgbuf[i++] = pq931Msg->tunneledMsgType;
		msgbuf[i++] = pq931Msg->logicalChannelNo >> 8;
		msgbuf[i++] = pq931Msg->logicalChannelNo;
	}
	else{
		CHRTRACEERR3("ERROR: Unknown Q931 message type. (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	stat = chrEncodeUUIE(pq931Msg);
	if (stat != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to encode uuie. (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}

	msgbuf[i++] = 3; /* TPKT version */
	msgbuf[i++] = 0; /* TPKT resevred */
	/* 1st octet of length, will be populated once len is determined */
	msgbuf[i++] = 0;
	/* 2nd octet of length, will be populated once len is determined */
	msgbuf[i++] = 0;
	/* Q931 protocol discriminator */
	msgbuf[i++] = pq931Msg->protocolDiscriminator;
	msgbuf[i++] = 2; /* length of call ref is two octets */
	msgbuf[i] = (pq931Msg->callReference >> 8); /* populate 1st octet */
	if (!strcmp(call->callType, "incoming"))
		msgbuf[i++] |= 0x80;   /* fromDestination*/
	else
		i++;   /* fromOriginator*/


	msgbuf[i++] = pq931Msg->callReference; /* populate 2nd octet */
	msgbuf[i++] = pq931Msg->messageType; /* type of q931 message */

	/* Note: the order in which ies are added is important. It is in the
	ascending order of ie codes.
	*/
	/* Add bearer IE */
	if (pq931Msg->bearerCapabilityIE)
	{
		msgbuf[i++] = Q931BearerCapabilityIE; /* ie discriminator */
		msgbuf[i++] = pq931Msg->bearerCapabilityIE->length;
		memcpy(msgbuf + i, pq931Msg->bearerCapabilityIE->data,
			pq931Msg->bearerCapabilityIE->length);
		i += pq931Msg->bearerCapabilityIE->length;
	}

	/* Add cause IE */
	if (pq931Msg->causeIE)
	{
		msgbuf[i++] = Q931CauseIE;
		msgbuf[i++] = pq931Msg->causeIE->length;
		memcpy(msgbuf + i, pq931Msg->causeIE->data, pq931Msg->causeIE->length);
		i += pq931Msg->causeIE->length;
	}

	/*Add progress indicator IE
	if(pq931Msg->messageType == Q931AlertingMsg || pq931Msg->messageType == Q931CallProceedingMsg)
	{
	msgbuf[i++] = Q931ProgressIndicatorIE;
	msgbuf[i++] = 2; //Length is 2 octet
	msgbuf[i++] = 0x80; //PI=8
	msgbuf[i++] = 0x88;
	}*/

	/*Add display ie. */
	if (!chrUtilsIsStrEmpty(call->ourCallerId))
	{
		msgbuf[i++] = Q931DisplayIE;
		ieLen = strlen(call->ourCallerId) + 1;
		msgbuf[i++] = ieLen;
		memcpy(msgbuf + i, call->ourCallerId, ieLen - 1);
		i += ieLen - 1;
		msgbuf[i++] = '\0';
	}

	/* Add calling Party ie */
	if (pq931Msg->callingPartyNumberIE)
	{
		msgbuf[i++] = Q931CallingPartyNumberIE;
		msgbuf[i++] = pq931Msg->callingPartyNumberIE->length;
		memcpy(msgbuf + i, pq931Msg->callingPartyNumberIE->data,
			pq931Msg->callingPartyNumberIE->length);
		i += pq931Msg->callingPartyNumberIE->length;
	}

	/* Add called Party ie */
	if (pq931Msg->calledPartyNumberIE)
	{
		msgbuf[i++] = Q931CalledPartyNumberIE;
		msgbuf[i++] = pq931Msg->calledPartyNumberIE->length;
		memcpy(msgbuf + i, pq931Msg->calledPartyNumberIE->data,
			pq931Msg->calledPartyNumberIE->length);
		i += pq931Msg->calledPartyNumberIE->length;
	}

	/* Add keypad ie */
	if (pq931Msg->keypadIE)
	{
		msgbuf[i++] = Q931KeypadIE;
		msgbuf[i++] = pq931Msg->keypadIE->length;
		memcpy(msgbuf + i, pq931Msg->keypadIE->data, pq931Msg->keypadIE->length);
		i += pq931Msg->keypadIE->length;
	}

	/* Note: Have to fix this, though it works. Need to get rid of ie list.
	Right now we only put UUIE in ie list. Can be easily removed.
	*/

	for (j = 0, curNode = pq931Msg->ies.head; j < (int)pq931Msg->ies.count; j++)
	{
		Q931InformationElement *ie = (Q931InformationElement*)curNode->data;

		ieLen = ie->length;

		/* Add the ie discriminator in message buffer */
		msgbuf[i++] = ie->discriminator;

		/* For user-user IE, we have to add protocol discriminator */
		if (ie->discriminator == Q931UserUserIE)
		{
			ieLen++; /* length includes protocol discriminator octet. */
			msgbuf[i++] = (ieLen >> 8); /* 1st octet for length */
			msgbuf[i++] = ieLen;      /* 2nd octet for length */
			ieLen--;
			msgbuf[i++] = 5; /* protocol discriminator */
			memcpy((msgbuf + i), ie->data, ieLen);

			i += ieLen;

		}
		else
		{
			CHRTRACEWARN1("Warning: Only UUIE is supported currently\n");
			return CHR_FAILED;
		}
	}
	//   len = i+1-4; /* complete message length */


	/* Tpkt length octets populated with total length of the message */
	if (msgbuf[0] != CHRFacility)
	{
		len = i - 1;
		msgbuf[3] = (len >> 8);
		msgbuf[4] = len;        /* including tpkt header */
	}
	else{
		len = i - 4;
		msgbuf[6] = (len >> 8);
		msgbuf[7] = len;
	}

#ifndef _COMPACT
	if (msgbuf[0] != CHRFacility)
		chrQ931PrintMessage(call, (ASN1OCTET*)msgbuf + 5, len - 4);
	else
		chrQ931PrintMessage(call, (ASN1OCTET*)msgbuf + 8, len - 4);
#endif
	return CHR_OK;
}


static Q931Message* new_Q931_UUIE
(OOCTXT* pctxt, CHRH323CallData* call, int msgType)
{
	Q931Message* q931msg;

	if (chrCreateQ931Message(&q931msg, msgType) != CHR_OK) {
		CHRTRACEERR1("Error allocating memory for Q.931 message\n");
		return 0;
	}

	q931msg->callReference = call->callReference;

	q931msg->userInfo = memAllocTypeZ(pctxt, H225H323_UserInformation);
	if (0 == q931msg->userInfo) {
		CHRTRACEERR1("Error allocating memory for H323_UserInformation\n");
		memFreePtr(pctxt, q931msg);
		return 0;
	}

	q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent = 1;

	q931msg->userInfo->h323_uu_pdu.h245Tunneling =
		CHR_TESTFLAG(gH323ep.flags, CHR_M_TUNNELING);

	return q931msg;
}

int chrSendStatus(CHRH323CallData* call)
{
	int ret;
	H225Status_UUIE* pStatusUUIE;
	Q931Message* q931msg;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	CHRTRACEDBG3("Building Status-UUIE (%s, %s)\n",
		call->callType, call->callToken);

	q931msg = new_Q931_UUIE(pctxt, call, Q931StatusMsg);
	if (0 == q931msg) {
		CHRTRACEERR1("ERROR: Memory - chrSendStatus - userInfo\n");
		return CHR_FAILED;
	}

	q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
		T_H225H323_UU_PDU_h323_message_body_status;

	pStatusUUIE = memAllocTypeZ(pctxt, H225Status_UUIE);
	if (0 == pStatusUUIE) {
		CHRTRACEERR1("ERROR: Memory - chrSendStatus - pStatusUUIE\n");
		memFreePtr(pctxt, q931msg);
		return CHR_FAILED;
	}

	q931msg->userInfo->
		h323_uu_pdu.h323_message_body.u.status = pStatusUUIE;

	pStatusUUIE->protocolIdentifier = gProtocolID;

	if (call->callIdentifier.guid.numocts > 0) {
		pStatusUUIE->callIdentifier.guid.numocts =
			call->callIdentifier.guid.numocts;

		memcpy(pStatusUUIE->callIdentifier.guid.data,
			call->callIdentifier.guid.data,
			call->callIdentifier.guid.numocts);
	}

	CHRTRACEDBG3("Built Status (%s, %s)\n",
		call->callType, call->callToken);

	ret = chrSendH225MsgtoQueue(call, q931msg);
	if (ret != CHR_OK) {
		CHRTRACEERR3
			("ERROR: Failed to enqueue Status message to outbound "
			"queue.(%s, %s)\n", call->callType, call->callToken);
	}

	memReset(&gH323ep.msgctxt);

	return ret;
}

int chrSendCallProceeding(CHRH323CallData *call)
{
	int ret;
	H225VendorIdentifier *vendor;
	H225CallProceeding_UUIE *callProceeding;
	Q931Message *q931msg;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	CHRTRACEDBG3("Building CallProceeding (%s, %s)\n", call->callType,
		call->callToken);

	q931msg = new_Q931_UUIE(pctxt, call, Q931CallProceedingMsg);
	if (0 == q931msg) {
		CHRTRACEERR1("ERROR: Memory - chrSendCallProceeding - userInfo\n");
		return CHR_FAILED;
	}

	q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
		T_H225H323_UU_PDU_h323_message_body_callProceeding;

	callProceeding = (H225CallProceeding_UUIE*)memAlloc(pctxt,
		sizeof(H225CallProceeding_UUIE));
	if (!callProceeding)
	{
		CHRTRACEERR1("ERROR:Memory - chrSendCallProceeding - callProceeding\n");
		return CHR_FAILED;
	}
	memset(callProceeding, 0, sizeof(H225CallProceeding_UUIE));
	q931msg->userInfo->
		h323_uu_pdu.h323_message_body.u.callProceeding = callProceeding;
	callProceeding->m.multipleCallsPresent = 1;
	callProceeding->m.maintainConnectionPresent = 1;
	callProceeding->multipleCalls = FALSE;
	callProceeding->maintainConnection = FALSE;

	if (call->callIdentifier.guid.numocts > 0) {
		callProceeding->m.callIdentifierPresent = 1;
		callProceeding->callIdentifier.guid.numocts =
			call->callIdentifier.guid.numocts;

		memcpy(callProceeding->callIdentifier.guid.data,
			call->callIdentifier.guid.data,
			call->callIdentifier.guid.numocts);
	}
	else callProceeding->m.callIdentifierPresent = 0;

	callProceeding->protocolIdentifier = gProtocolID;

	/* Pose as Terminal or Gateway */
	callProceeding->destinationInfo.m.terminalPresent = TRUE;

	callProceeding->destinationInfo.m.vendorPresent = 1;
	vendor = &callProceeding->destinationInfo.vendor;
	if (gH323ep.productID)
	{
		vendor->m.productIdPresent = 1;
		vendor->productId.numocts = ASN1MIN(strlen(gH323ep.productID),
			sizeof(vendor->productId.data));
		strncpy((char*)vendor->productId.data, gH323ep.productID,
			vendor->productId.numocts);
	}
	if (gH323ep.versionID)
	{
		vendor->m.versionIdPresent = 1;
		vendor->versionId.numocts = ASN1MIN(strlen(gH323ep.versionID),
			sizeof(vendor->versionId.data));
		strncpy((char*)vendor->versionId.data, gH323ep.versionID,
			vendor->versionId.numocts);
	}

	vendor->vendor.t35CountryCode = gH323ep.t35CountryCode;
	vendor->vendor.t35Extension = gH323ep.t35Extension;
	vendor->vendor.manufacturerCode = gH323ep.manufacturerCode;

	CHRTRACEDBG3("Built Call Proceeding(%s, %s)\n", call->callType,
		call->callToken);
	ret = chrSendH225MsgtoQueue(call, q931msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue CallProceeding message to outbound queue.(%s, %s)\n", call->callType, call->callToken);
	}

	memReset(&gH323ep.msgctxt);

	return ret;
}

int chrSendAlerting(CHRH323CallData *call)
{
	int ret;
	H225Alerting_UUIE *alerting;
	H225VendorIdentifier *vendor;
	Q931Message *q931msg;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	q931msg = new_Q931_UUIE(pctxt, call, Q931AlertingMsg);
	if (0 == q931msg) {
		CHRTRACEERR1("ERROR: Memory - chrSendAlerting - userInfo\n");
		return CHR_FAILED;
	}

	q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
		T_H225H323_UU_PDU_h323_message_body_alerting;

	alerting = (H225Alerting_UUIE*)memAlloc(pctxt,
		sizeof(H225Alerting_UUIE));
	if (!alerting)
	{
		CHRTRACEERR1("ERROR:Memory -  chrSendAlerting - alerting\n");
		return CHR_FAILED;
	}
	memset(alerting, 0, sizeof(H225Alerting_UUIE));
	q931msg->userInfo->h323_uu_pdu.h323_message_body.u.alerting = alerting;
	alerting->m.multipleCallsPresent = 1;
	alerting->m.maintainConnectionPresent = 1;
	alerting->multipleCalls = FALSE;
	alerting->maintainConnection = FALSE;

	/*Populate aliases */
	alerting->m.alertingAddressPresent = TRUE;
	if (call->ourAliases)
		ret = chrPopulateAliasList(pctxt, call->ourAliases,
		&alerting->alertingAddress);
	else
		ret = chrPopulateAliasList(pctxt, gH323ep.aliases,
		&alerting->alertingAddress);
	if (CHR_OK != ret)
	{
		CHRTRACEERR1("Error:Failed to populate alias list in Alert message\n");
		memReset(pctxt);
		return CHR_FAILED;
	}
	alerting->m.presentationIndicatorPresent = TRUE;
	alerting->presentationIndicator.t =
		T_H225PresentationIndicator_presentationAllowed;
	alerting->m.screeningIndicatorPresent = TRUE;
	alerting->screeningIndicator = userProvidedNotScreened;

	if (call->callIdentifier.guid.numocts > 0) {
		alerting->m.callIdentifierPresent = 1;
		alerting->callIdentifier.guid.numocts =
			call->callIdentifier.guid.numocts;

		memcpy(alerting->callIdentifier.guid.data,
			call->callIdentifier.guid.data,
			call->callIdentifier.guid.numocts);
	}
	else alerting->m.callIdentifierPresent = 0;

	alerting->protocolIdentifier = gProtocolID;

	/* Pose as Terminal or Gateway */
	alerting->destinationInfo.m.terminalPresent = TRUE;

	alerting->destinationInfo.m.vendorPresent = 1;
	vendor = &alerting->destinationInfo.vendor;
	if (gH323ep.productID)
	{
		vendor->m.productIdPresent = 1;
		vendor->productId.numocts = ASN1MIN(strlen(gH323ep.productID),
			sizeof(vendor->productId.data));
		strncpy((char*)vendor->productId.data, gH323ep.productID,
			vendor->productId.numocts);
	}
	if (gH323ep.versionID)
	{
		vendor->m.versionIdPresent = 1;
		vendor->versionId.numocts = ASN1MIN(strlen(gH323ep.versionID),
			sizeof(vendor->versionId.data));
		strncpy((char*)vendor->versionId.data, gH323ep.versionID,
			vendor->versionId.numocts);
	}

	vendor->vendor.t35CountryCode = gH323ep.t35CountryCode;
	vendor->vendor.t35Extension = gH323ep.t35Extension;
	vendor->vendor.manufacturerCode = gH323ep.manufacturerCode;

	/*ret = chrSetFastStartResponse(call, q931msg,
		&alerting->fastStart.n, &alerting->fastStart.elem);*/
	if (ret != ASN_OK) { return ret; }
	if (alerting->fastStart.n > 0) {
		alerting->m.fastStartPresent = TRUE;
	}
	else {
		alerting->m.fastStartPresent = FALSE;
	}

	CHRTRACEDBG3("Built Alerting (%s, %s)\n", call->callType, call->callToken);

	ret = chrSendH225MsgtoQueue(call, q931msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error: Failed to enqueue Alerting message to outbound queue. (%s, %s)\n", call->callType, call->callToken);
	}

	memReset(&gH323ep.msgctxt);

	return ret;
}


int chrSendFacility(CHRH323CallData *call)
{
	int ret = 0;
	Q931Message *pQ931Msg = NULL;
	H225Facility_UUIE *facility = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	CHRTRACEDBG3("Building Facility message (%s, %s)\n", call->callType,
		call->callToken);

	pQ931Msg = new_Q931_UUIE(pctxt, call, Q931FacilityMsg);
	if (0 == pQ931Msg) {
		CHRTRACEERR1("ERROR: Memory - chrSendFacility - userInfo\n");
		return CHR_FAILED;
	}

	pQ931Msg->userInfo->h323_uu_pdu.h323_message_body.t =
		T_H225H323_UU_PDU_h323_message_body_facility;

	facility = (H225Facility_UUIE*)
		memAllocZ(pctxt, sizeof(H225Facility_UUIE));

	if (!facility)
	{
		CHRTRACEERR3("ERROR:Memory - chrSendFacility - facility (%s, %s)"
			"\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	pQ931Msg->userInfo->h323_uu_pdu.h323_message_body.u.facility = facility;

	/* Populate Facility UUIE */
	facility->protocolIdentifier = gProtocolID;

	if (call->callIdentifier.guid.numocts > 0) {
		facility->m.callIdentifierPresent = 1;
		facility->callIdentifier.guid.numocts =
			call->callIdentifier.guid.numocts;

		memcpy(facility->callIdentifier.guid.data,
			call->callIdentifier.guid.data,
			call->callIdentifier.guid.numocts);
	}
	else facility->m.callIdentifierPresent = 0;

	facility->reason.t = T_H225FacilityReason_transportedInformation;
	CHRTRACEDBG3("Built Facility message to send (%s, %s)\n", call->callType,
		call->callToken);

	ret = chrSendH225MsgtoQueue(call, pQ931Msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3
			("Error:Failed to enqueue Facility message to outbound "
			"queue.(%s, %s)\n", call->callType, call->callToken);
	}
	memReset(&gH323ep.msgctxt);
	return ret;
}

int chrSendReleaseComplete(CHRH323CallData *call)
{
	int ret;
	Q931Message *q931msg = NULL;
	H225ReleaseComplete_UUIE *releaseComplete;
	enum Q931CauseValues cause = Q931ErrorInCauseIE;
	unsigned h225ReasonCode = T_H225ReleaseCompleteReason_undefinedReason;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	CHRTRACEDBG3("Building Release Complete message to send(%s, %s)\n",
		call->callType, call->callToken);

	q931msg = new_Q931_UUIE(pctxt, call, Q931ReleaseCompleteMsg);
	if (0 == q931msg) {
		CHRTRACEERR1("ERROR: Memory - chrSendReleaseComplete - userInfo\n");

		if (call->callState < CHR_CALL_CLEAR)
		{
			call->callEndReason = CHR_REASON_LOCAL_CLEARED;
			call->callState = CHR_CALL_CLEAR;
		}
		return CHR_FAILED;
	}

	releaseComplete = (H225ReleaseComplete_UUIE*)memAlloc(pctxt,
		sizeof(H225ReleaseComplete_UUIE));
	if (!releaseComplete)
	{
		CHRTRACEERR3("Error:Memory - chrSendReleaseComplete - releaseComplete"
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	memset(releaseComplete, 0, sizeof(H225ReleaseComplete_UUIE));
	q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
		T_H225H323_UU_PDU_h323_message_body_releaseComplete;

	/* Get cause value and h225 reason code corresponding to CHRCallClearReason*/
	chrQ931GetCauseAndReasonCodeFromCallClearReason(call->callEndReason,
		&cause, &h225ReasonCode);
	/* Set Cause IE */
	chrQ931SetCauseIE(q931msg, cause, 0, 0);

	/* Set H225 releaseComplete reasonCode */
	releaseComplete->m.reasonPresent = TRUE;
	releaseComplete->reason.t = h225ReasonCode;

	/* Add user-user ie */
	q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent = TRUE;
	q931msg->userInfo->h323_uu_pdu.h245Tunneling =
		CHR_TESTFLAG(call->flags, CHR_M_TUNNELING);

	q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
		T_H225H323_UU_PDU_h323_message_body_releaseComplete;

	q931msg->userInfo->h323_uu_pdu.
		h323_message_body.u.releaseComplete = releaseComplete;

	if (call->callIdentifier.guid.numocts > 0) {
		releaseComplete->m.callIdentifierPresent = 1;
		releaseComplete->protocolIdentifier = gProtocolID;
		releaseComplete->callIdentifier.guid.numocts =
			call->callIdentifier.guid.numocts;

		memcpy(releaseComplete->callIdentifier.guid.data,
			call->callIdentifier.guid.data,
			call->callIdentifier.guid.numocts);
	}
	else releaseComplete->m.callIdentifierPresent = 0;

	CHRTRACEDBG3("Built Release Complete message (%s, %s)\n",
		call->callType, call->callToken);

	/* Send H225 message */
	ret = chrSendH225MsgtoQueue(call, q931msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue ReleaseComplete message to outbound"
			" queue.(%s, %s)\n", call->callType, call->callToken);
	}
	memReset(&gH323ep.msgctxt);

	return ret;
}


int chrSendConnect(CHRH323CallData *call)
{
	chrAcceptCall(call);
	return CHR_OK;
}

/*TODO: Need to clean logical channel in case of failure after creating one */
int chrAcceptCall(CHRH323CallData *call)
{
	int ret = 0;
	H225Connect_UUIE *connect;
	H225TransportAddress_ipAddress *h245IpAddr;
	H225VendorIdentifier *vendor;
	Q931Message *q931msg = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	ret = chrCreateQ931Message(&q931msg, Q931ConnectMsg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR1("Error: In allocating memory for - H225 "
			"Connect message\n");
		return CHR_FAILED;
	}
	q931msg->callReference = call->callReference;

	/* Set bearer capability */

	if (gH323ep.bearercap == Q931TransferUnrestrictedDigital) {
		ret = chrSetBearerCapabilityIE
			(q931msg, Q931CCITTStd, Q931TransferUnrestrictedDigital,
			Q931TransferPacketMode, Q931TransferRatePacketMode,
			Q931UserInfoLayer1G722G725);
	}
	else {
		ret = chrSetBearerCapabilityIE
			(q931msg, Q931CCITTStd, Q931TransferSpeech, Q931TransferCircuitMode,
			Q931TransferRate64Kbps, Q931UserInfoLayer1G711ULaw);
	}

	if (ret != CHR_OK) {
		CHRTRACEERR3("ERROR: Failed to set bearer capability IE. (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	q931msg->userInfo = (H225H323_UserInformation*)
		memAllocZ(pctxt, sizeof(H225H323_UserInformation));

	if (!q931msg->userInfo)
	{
		CHRTRACEERR1("ERROR:Memory - chrAcceptCall - userInfo\n");
		return CHR_FAILED;
	}

	q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent = TRUE;

	q931msg->userInfo->h323_uu_pdu.h245Tunneling =
		CHR_TESTFLAG(call->flags, CHR_M_TUNNELING);

	q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
		T_H225H323_UU_PDU_h323_message_body_connect;

	connect = (H225Connect_UUIE*)
		memAllocZ(pctxt, sizeof(H225Connect_UUIE));

	if (!connect)
	{
		CHRTRACEERR1("ERROR:Memory - chrAcceptCall - connect\n");
		return CHR_FAILED;
	}

	q931msg->userInfo->h323_uu_pdu.h323_message_body.u.connect = connect;
	connect->m.fastStartPresent = 0;
	connect->m.multipleCallsPresent = 1;
	connect->m.maintainConnectionPresent = 1;
	connect->multipleCalls = FALSE;
	connect->maintainConnection = FALSE;

	connect->conferenceID.numocts = 16;
	genGloballyUniqueID(connect->conferenceID.data);

	if (call->callIdentifier.guid.numocts > 0) {
		connect->m.callIdentifierPresent = 1;
		connect->callIdentifier.guid.numocts =
			call->callIdentifier.guid.numocts;

		memcpy(connect->callIdentifier.guid.data,
			call->callIdentifier.guid.data,
			call->callIdentifier.guid.numocts);
	}
	else connect->m.callIdentifierPresent = 0;

	connect->conferenceID.numocts = call->confIdentifier.numocts;
	memcpy(connect->conferenceID.data, call->confIdentifier.data,
		call->confIdentifier.numocts);

	/* Populate alias addresses */
	connect->m.connectedAddressPresent = TRUE;
	if (call->ourAliases)
		ret = chrPopulateAliasList(pctxt, call->ourAliases,
		&connect->connectedAddress);
	else
		ret = chrPopulateAliasList(pctxt, gH323ep.aliases,
		&connect->connectedAddress);
	if (CHR_OK != ret)
	{
		CHRTRACEERR1("Error:Failed to populate alias list in Connect message\n");
		memReset(pctxt);
		return CHR_FAILED;
	}
	connect->m.presentationIndicatorPresent = TRUE;
	connect->presentationIndicator.t =
		T_H225PresentationIndicator_presentationAllowed;
	connect->m.screeningIndicatorPresent = TRUE;
	connect->screeningIndicator = userProvidedNotScreened;

	connect->protocolIdentifier = gProtocolID;

	/* Pose as Terminal or Gateway */
	connect->destinationInfo.m.terminalPresent = TRUE;


	connect->destinationInfo.m.vendorPresent = 1;
	vendor = &connect->destinationInfo.vendor;

	vendor->vendor.t35CountryCode = gH323ep.t35CountryCode;
	vendor->vendor.t35Extension = gH323ep.t35Extension;
	vendor->vendor.manufacturerCode = gH323ep.manufacturerCode;
	if (gH323ep.productID)
	{
		vendor->m.productIdPresent = 1;
		vendor->productId.numocts = ASN1MIN(strlen(gH323ep.productID),
			sizeof(vendor->productId.data));
		strncpy((char*)vendor->productId.data, gH323ep.productID,
			vendor->productId.numocts);
	}
	if (gH323ep.versionID)
	{
		vendor->m.versionIdPresent = 1;
		vendor->versionId.numocts = ASN1MIN(strlen(gH323ep.versionID),
			sizeof(vendor->versionId.data));
		strncpy((char*)vendor->versionId.data, gH323ep.versionID,
			vendor->versionId.numocts);
	}

	//ret = chrSetFastStartResponse(call, q931msg,
	//	&connect->fastStart.n, &connect->fastStart.elem);
	if (ret != ASN_OK) { return ret; }
	if (connect->fastStart.n > 0) {
		connect->m.fastStartPresent = TRUE;
	}
	else {
		connect->m.fastStartPresent = FALSE;
	}

	/* Add h245 listener address. Do not add H245 listener address in case
	of fast-start. */
	if ((!CHR_TESTFLAG(call->flags, CHR_M_FASTSTART) ||
		call->remoteFastStartOLCs.count == 0) &&
		!CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
	{
		ret = chrCreateH245Listener(call); /* First create an H.245 listener */

		if (ret != CHR_OK)
		{
			return ret;
		}

		connect->m.h245AddressPresent = TRUE;
		connect->h245Address.t = T_H225TransportAddress_ipAddress;

		h245IpAddr = (H225TransportAddress_ipAddress*)
			memAllocZ(pctxt, sizeof(H225TransportAddress_ipAddress));
		if (!h245IpAddr)
		{
			CHRTRACEERR3("Error:Memory - chrAcceptCall - h245IpAddr"
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		chrSocketConvertIpToNwAddr
			(call->localIP, h245IpAddr->ip.data, sizeof(h245IpAddr->ip.data));
		h245IpAddr->ip.numocts = 4;
		h245IpAddr->port = *(call->h245listenport);
		connect->h245Address.u.ipAddress = h245IpAddr;
	}

	CHRTRACEDBG3("Built H.225 Connect message (%s, %s)\n", call->callType,
		call->callToken);

	/* H225 message callback */
	if (gH323ep.h225Callbacks.onBuiltConnect)
		gH323ep.h225Callbacks.onBuiltConnect(call, q931msg);

	ret = chrSendH225MsgtoQueue(call, q931msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue Connect message to outbound queue.(%s, %s)\n", call->callType, call->callToken);
		memReset(&gH323ep.msgctxt);
		return CHR_FAILED;
	}
	memReset(&gH323ep.msgctxt);

#if 0
	if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
	{
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
	}
#endif
	return CHR_OK;
}

int chrH323MakeCall(char *dest, char *callToken, chrCallOptions *opts)
{
	OOCTXT *pctxt;
	CHRH323CallData *call;
	int ret = 0;
	char tmp[30] = "\0";
	char *ip = NULL, *port = NULL;

	if (!dest)
	{
		CHRTRACEERR1("ERROR:Invalid destination for new call\n");
		return CHR_FAILED;
	}
	if (!callToken)
	{
		CHRTRACEERR1("ERROR: Invalid callToken parameter to make call\n");
		return CHR_FAILED;
	}

	if (opts && opts->usrData) {
		call = chrCreateCall("outgoing", callToken, opts->usrData);
	}
	else {
		call = chrCreateCall("outgoing", callToken, NULL);
	}

	if (!call) return CHR_FAILED;

	pctxt = call->pctxt;
	if (opts)
	{
		if (opts->disableGk)
			CHR_SETFLAG(call->flags, CHR_M_DISABLEGK);
		else
			CHR_CLRFLAG(call->flags, CHR_M_DISABLEGK);

		call->callMode = opts->callMode;
	}


	ret = chrParseDestination(call, dest, tmp, 30, &call->remoteAliases);
	if (ret != CHR_OK)
	{
		CHRTRACEERR2("Error: Failed to parse the destination string %s for "
			"new call\n", dest);
		chrCleanCall(call);
		return CHR_FAILED;
	}

	/* Check whether we have ip address */
	if (!chrUtilsIsStrEmpty(tmp)) {
		ip = tmp;
		port = strchr(tmp, ':');
		*port = '\0';
		port++;
		strcpy(call->remoteIP, ip);
		call->remotePort = atoi(port);
	}

	strcpy(callToken, call->callToken);
	call->callReference = chrGenerateCallReference();
	chrGenerateCallIdentifier(&call->callIdentifier);
	call->confIdentifier.numocts = 16;
	genGloballyUniqueID(call->confIdentifier.data);

    ret = chrH323CallAdmitted(call);

	return CHR_OK;
}


int chrH323CallAdmitted(CHRH323CallData *call)
{
	int ret = 0;

	if (!call)
	{
		/* Call not supplied. Must locate it in list */
		CHRTRACEERR1("ERROR: Invalid call parameter to chrH323CallAdmitted");
		return CHR_FAILED;
	}

	if (!strcmp(call->callType, "outgoing")) {
		ret = chrCreateH225Connection(call);
		if (ret != CHR_OK)
		{
			CHRTRACEERR3("ERROR:Failed to create H225 connection to %s:%d\n",
				call->remoteIP, call->remotePort);
			if (call->callState< CHR_CALL_CLEAR)
			{
				call->callState = CHR_CALL_CLEAR;
				call->callEndReason = CHR_REASON_UNKNOWN;
			}
			return CHR_FAILED;
		}

		ret = chrH323MakeCall_helper(call);
	}
	else {
		/* incoming call */
		if (gH323ep.h323Callbacks.onIncomingCall) {
			/* Incoming call callback function */
			gH323ep.h323Callbacks.onIncomingCall(call);
		}

		/* Check for manual ringback generation */
		if (!CHR_TESTFLAG(gH323ep.flags, CHR_M_MANUALRINGBACK))
		{
			chrSendAlerting(call); /* Send alerting message */

			if (CHR_TESTFLAG(gH323ep.flags, CHR_M_AUTOANSWER)) {
				chrSendConnect(call); /* Send connect message - call accepted */
			}
		}
	}

	return CHR_OK;
}

int chrH323MakeCall_helper(CHRH323CallData *call)
{
	int ret = 0, k;
	Q931Message *q931msg = NULL;
	H225Setup_UUIE *setup;

	ASN1DynOctStr *pFS = NULL;
	DList fastStartList; /* list of encoded fast start elements */
	H225TransportAddress_ipAddress *destCallSignalIpAddress;

	H225TransportAddress_ipAddress *srcCallSignalIpAddress;
	chrH323EpCapability *epCap = NULL;
	OOCTXT *pctxt = NULL;
	H245OpenLogicalChannel *olc, printOlc;
	ASN1BOOL aligned = 1;
	chrAliases *pAlias = NULL;

	dListInit(&fastStartList);

	pctxt = &gH323ep.msgctxt;

	ret = chrCreateQ931Message(&q931msg, Q931SetupMsg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR1("ERROR:Failed to Create Q931 SETUP Message\n ");
		return CHR_FAILED;
	}

	q931msg->callReference = call->callReference;

	/* Set bearer capability */

	if (gH323ep.bearercap == Q931TransferUnrestrictedDigital) {
		ret = chrSetBearerCapabilityIE
			(q931msg, Q931CCITTStd, Q931TransferUnrestrictedDigital,
			Q931TransferPacketMode, Q931TransferRatePacketMode,
			Q931UserInfoLayer1G722G725);
	}
	else {
		ret = chrSetBearerCapabilityIE
			(q931msg, Q931CCITTStd, Q931TransferSpeech, Q931TransferCircuitMode,
			Q931TransferRate64Kbps, Q931UserInfoLayer1G711ULaw);
	}

	if (ret != CHR_OK) {
		CHRTRACEERR3("ERROR: Failed to set bearer capability IE. (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	/* Set calling party number Q931 IE */
	if (call->callingPartyNumber)
		chrQ931SetCallingPartyNumberIE(q931msg,
		(const char*)call->callingPartyNumber, 1, 0, 0, 0);


	/* Set called party number Q931 IE */
	if (call->calledPartyNumber)
		chrQ931SetCalledPartyNumberIE(q931msg,
		(const char*)call->calledPartyNumber, 1, 0);
	else if (call->remoteAliases) {
		pAlias = call->remoteAliases;
		while (pAlias) {
			if (pAlias->type == T_H225AliasAddress_dialedDigits)
				break;
			pAlias = pAlias->next;
		}
		if (pAlias)
		{
			call->calledPartyNumber = (char*)memAlloc(call->pctxt,
				strlen(pAlias->value) + 1);
			if (!call->calledPartyNumber)
			{
				CHRTRACEERR3("Error:Memory - chrH323MakeCall_helper - "
					"calledPartyNumber(%s, %s)\n", call->callType,
					call->callToken);
				return CHR_FAILED;
			}
			strcpy(call->calledPartyNumber, pAlias->value);
			chrQ931SetCalledPartyNumberIE(q931msg,
				(const char*)call->calledPartyNumber, 1, 0);
		}

	}

	q931msg->userInfo = (H225H323_UserInformation*)memAlloc(pctxt,
		sizeof(H225H323_UserInformation));
	if (!q931msg->userInfo)
	{
		CHRTRACEERR1("ERROR:Memory - chrH323MakeCall_helper - userInfo\n");
		return CHR_FAILED;
	}
	memset(q931msg->userInfo, 0, sizeof(H225H323_UserInformation));

	setup = (H225Setup_UUIE*)memAlloc(pctxt, sizeof(H225Setup_UUIE));
	if (!setup)
	{
		CHRTRACEERR3("Error:Memory -  chrH323MakeCall_helper - setup (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}
	memset(setup, 0, sizeof(H225Setup_UUIE));
	setup->protocolIdentifier = gProtocolID;

	/* Populate Alias Address.*/

	if (call->ourAliases || gH323ep.aliases)
	{
		setup->m.sourceAddressPresent = TRUE;
		if (call->ourAliases)
			ret = chrPopulateAliasList(pctxt, call->ourAliases,
			&setup->sourceAddress);
		else if (gH323ep.aliases)
			ret = chrPopulateAliasList(pctxt, gH323ep.aliases,
			&setup->sourceAddress);
		if (CHR_OK != ret)
		{
			CHRTRACEERR1("Error:Failed to populate alias list in SETUP message\n");
			memReset(pctxt);
			return CHR_FAILED;
		}
	}

	setup->m.presentationIndicatorPresent = TRUE;
	setup->presentationIndicator.t =
		T_H225PresentationIndicator_presentationAllowed;
	setup->m.screeningIndicatorPresent = TRUE;
	setup->screeningIndicator = userProvidedNotScreened;

	setup->m.multipleCallsPresent = TRUE;
	setup->multipleCalls = FALSE;
	setup->m.maintainConnectionPresent = TRUE;
	setup->maintainConnection = FALSE;

	/* Populate Destination aliases */
	if (call->remoteAliases)
	{
		setup->m.destinationAddressPresent = TRUE;
		ret = chrPopulateAliasList(pctxt, call->remoteAliases,
			&setup->destinationAddress);
		if (CHR_OK != ret)
		{
			CHRTRACEERR1("Error:Failed to populate destination alias list in SETUP"
				"message\n");
			memReset(pctxt);
			return CHR_FAILED;
		}
	}

	/* Populate the vendor information */
	setup->sourceInfo.m.terminalPresent = TRUE;

	setup->sourceInfo.m.vendorPresent = TRUE;
	setup->sourceInfo.vendor.vendor.t35CountryCode = gH323ep.t35CountryCode;
	setup->sourceInfo.vendor.vendor.t35Extension = gH323ep.t35Extension;
	setup->sourceInfo.vendor.vendor.manufacturerCode = gH323ep.manufacturerCode;

	if (gH323ep.productID)
	{
		setup->sourceInfo.vendor.m.productIdPresent = TRUE;
		setup->sourceInfo.vendor.productId.numocts = ASN1MIN(
			strlen(gH323ep.productID),
			sizeof(setup->sourceInfo.vendor.productId.data));
		strncpy((char*)setup->sourceInfo.vendor.productId.data,
			gH323ep.productID, setup->sourceInfo.vendor.productId.numocts);
	}
	else
		setup->sourceInfo.vendor.m.productIdPresent = FALSE;

	if (gH323ep.versionID)
	{
		setup->sourceInfo.vendor.m.versionIdPresent = TRUE;
		setup->sourceInfo.vendor.versionId.numocts = ASN1MIN(
			strlen(gH323ep.versionID),
			sizeof(setup->sourceInfo.vendor.versionId.data));
		strncpy((char*)setup->sourceInfo.vendor.versionId.data,
			gH323ep.versionID, setup->sourceInfo.vendor.versionId.numocts);
	}
	else
		setup->sourceInfo.vendor.m.versionIdPresent = FALSE;

	setup->sourceInfo.mc = FALSE;
	setup->sourceInfo.undefinedNode = FALSE;

	/* Populate the destination Call Signal Address */
	setup->destCallSignalAddress.t = T_H225TransportAddress_ipAddress;
	destCallSignalIpAddress = (H225TransportAddress_ipAddress*)memAlloc(pctxt,
		sizeof(H225TransportAddress_ipAddress));
	if (!destCallSignalIpAddress)
	{
		CHRTRACEERR3("Error:Memory -  chrH323MakeCall_helper - "
			"destCallSignalAddress. (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	chrSocketConvertIpToNwAddr
		(call->remoteIP, destCallSignalIpAddress->ip.data,
		sizeof(destCallSignalIpAddress->ip.data));

	destCallSignalIpAddress->ip.numocts = 4;
	destCallSignalIpAddress->port = call->remotePort;

	setup->destCallSignalAddress.u.ipAddress = destCallSignalIpAddress;
	setup->m.destCallSignalAddressPresent = TRUE;
	setup->activeMC = FALSE;

	/* Populate the source Call Signal Address */
	setup->sourceCallSignalAddress.t = T_H225TransportAddress_ipAddress;
	srcCallSignalIpAddress = (H225TransportAddress_ipAddress*)memAlloc(pctxt,
		sizeof(H225TransportAddress_ipAddress));
	if (!srcCallSignalIpAddress)
	{
		CHRTRACEERR3("Error:Memory - chrH323MakeCall_helper - srcCallSignalAddress"
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	chrSocketConvertIpToNwAddr
		(call->localIP, srcCallSignalIpAddress->ip.data,
		sizeof(srcCallSignalIpAddress->ip.data));

	srcCallSignalIpAddress->ip.numocts = 4;
	srcCallSignalIpAddress->port = call->pH225Channel->port;
	setup->sourceCallSignalAddress.u.ipAddress = srcCallSignalIpAddress;
	setup->m.sourceCallSignalAddressPresent = TRUE;
	/* No fast start */
	if (!CHR_TESTFLAG(gH323ep.flags, CHR_M_FASTSTART))
	{
		setup->m.fastStartPresent = FALSE;
	}
	else{
		setup->m.fastStartPresent = TRUE;

		/* Use preference order of codecs */
		for (k = 0; k< call->capPrefs.index; k++)
		{
			CHRTRACEDBG5("Preffered capability at index %d is %s. (%s, %s)\n",
				k, chrGetCapTypeText(call->capPrefs.order[k]),
				call->callType, call->callToken);

			if (call->ourCaps) {
				epCap = call->ourCaps;
				CHRTRACEDBG3("Using call specific capabilities in faststart of "
					"setup message. (%s, %s)\n", call->callType,
					call->callToken);
			}
			else{
				epCap = gH323ep.myCaps;
				CHRTRACEDBG3("Using end-point capabilities for faststart of setup"
					"message. (%s, %s)\n", call->callType,
					call->callToken);
			}

			while (epCap){
				if (epCap->cap == call->capPrefs.order[k]) break;
				else epCap = epCap->next;
			}
			if (!epCap)
			{
				CHRTRACEWARN4("Warn:Preferred capability %s is abscent in "
					"capability list. (%s, %s)\n",
					chrGetCapTypeText(call->capPrefs.order[k]),
					call->callType, call->callToken);
				continue;
			}



			CHRTRACEDBG4("Building olcs with capability %s. (%s, %s)\n",
				chrGetCapTypeText(epCap->cap), call->callType,
				call->callToken);
			if (epCap->dir & CHRRX)
			{
				olc = (H245OpenLogicalChannel*)memAlloc(pctxt,
					sizeof(H245OpenLogicalChannel));
				if (!olc)
				{
					CHRTRACEERR3("ERROR:Memory - chrH323MakeCall_helper - olc(%s, %s)"
						"\n", call->callType, call->callToken);
					chrFreeQ931Message(q931msg);
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_LOCAL_CLEARED;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}
				memset(olc, 0, sizeof(H245OpenLogicalChannel));
				olc->forwardLogicalChannelNumber = call->logicalChanNoCur++;
				if (call->logicalChanNoCur > call->logicalChanNoMax)
					call->logicalChanNoCur = call->logicalChanNoBase;

				chrBuildFastStartOLC(call, olc, epCap, pctxt, CHRRX);
				/* Do not specify msg buffer let automatic allocation work */
				setPERBuffer(pctxt, NULL, 0, aligned);
				if (asn1PE_H245OpenLogicalChannel(pctxt, olc) != ASN_OK)
				{
					CHRTRACEERR3("ERROR:Encoding of olc failed for faststart(%s, %s)"
						"\n", call->callType, call->callToken);
					chrFreeQ931Message(q931msg);
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_LOCAL_CLEARED;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}
				pFS = memAllocType(pctxt, ASN1DynOctStr);
				if (0 == pFS) {
					CHRTRACEERR1("ERROR: No memory available\n");
					return CHR_FAILED;
				}
				pFS->data = encodeGetMsgPtr(pctxt, (int*)&(pFS->numocts));
				dListAppend(pctxt, &fastStartList, (void*)pFS);

				/* Dump faststart element in logfile for debugging purpose */
				setPERBuffer(pctxt, (ASN1OCTET*)pFS->data, pFS->numocts, 1);
				initializePrintHandler(&printHandler, "FastStart Element");
				setEventHandler(pctxt, &printHandler);
				memset(&printOlc, 0, sizeof(printOlc));
				ret = asn1PD_H245OpenLogicalChannel(pctxt, &(printOlc));
				if (ret != ASN_OK)
				{
					CHRTRACEERR3("Error: Failed decoding FastStart Element."
						"(%s, %s)\n", call->callType, call->callToken);
					chrFreeQ931Message(q931msg);
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_LOCAL_CLEARED;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}
				finishPrint();
				removeEventHandler(pctxt);

				olc = NULL;
				CHRTRACEDBG4("Added RX fs element with capability %s(%s, %s)\n",
					chrGetCapTypeText(epCap->cap), call->callType,
					call->callToken);
			}

			if (epCap->dir & CHRTX)
			{
				olc = memAllocTypeZ(pctxt, H245OpenLogicalChannel);
				if (!olc)
				{
					CHRTRACEERR3("ERROR:Memory - chrH323MakeCall_helper - olc(%s, %s)"
						"\n", call->callType, call->callToken);
					chrFreeQ931Message(q931msg);
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_LOCAL_CLEARED;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}
				olc->forwardLogicalChannelNumber = call->logicalChanNoCur++;
				if (call->logicalChanNoCur > call->logicalChanNoMax)
					call->logicalChanNoCur = call->logicalChanNoBase;

				chrBuildFastStartOLC(call, olc, epCap, pctxt, CHRTX);
				/* Do not specify msg buffer let automatic allocation work */
				setPERBuffer(pctxt, NULL, 0, aligned);
				if (asn1PE_H245OpenLogicalChannel(pctxt, olc) != ASN_OK)
				{
					CHRTRACEERR3("ERROR:Encoding of olc failed for faststart(%s, %s)"
						"\n", call->callType, call->callToken);
					chrFreeQ931Message(q931msg);
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_LOCAL_CLEARED;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}
				pFS = memAllocType(pctxt, ASN1DynOctStr);
				if (0 == pFS) {
					CHRTRACEERR1("ERROR: No memory available\n");
					return CHR_FAILED;
				}
				pFS->data = encodeGetMsgPtr(pctxt, (int*)&(pFS->numocts));
				dListAppend(pctxt, &fastStartList, (void*)pFS);

				/* Dump faststart element in logfile for debugging purpose */
				setPERBuffer(pctxt, (ASN1OCTET*)pFS->data, pFS->numocts, 1);
				initializePrintHandler(&printHandler, "FastStart Element");
				setEventHandler(pctxt, &printHandler);
				memset(&printOlc, 0, sizeof(printOlc));
				ret = asn1PD_H245OpenLogicalChannel(pctxt, &(printOlc));
				if (ret != ASN_OK)
				{
					CHRTRACEERR3("Error: Failed decoding FastStart Element."
						"(%s, %s)\n", call->callType, call->callToken);
					chrFreeQ931Message(q931msg);
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_LOCAL_CLEARED;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}
				finishPrint();
				removeEventHandler(pctxt);

				olc = NULL;
				CHRTRACEDBG4("Added TX fs element with capability %s(%s, %s)\n",
					chrGetCapTypeText(epCap->cap), call->callType,
					call->callToken);
			}

		}
		CHRTRACEDBG4("Added %d fast start elements to SETUP message (%s, %s)\n",
			fastStartList.count, call->callType, call->callToken);

		setup->fastStart.elem = (ASN1DynOctStr*)
			memAlloc(pctxt, fastStartList.count * sizeof(ASN1DynOctStr));

		if (0 == setup->fastStart.elem) {
			CHRTRACEERR1("ERROR: No memory available\n");
			return CHR_FAILED;
		}

		/* Add fast start elements to setup structure */
		setup->fastStart.n = 0;
		while (0 != (pFS = dListDeleteHead(pctxt, &fastStartList))) {
			setup->fastStart.elem[setup->fastStart.n].numocts = pFS->numocts;
			setup->fastStart.elem[setup->fastStart.n++].data = pFS->data;
			memFreePtr(pctxt, pFS);
		}
	}

	setup->conferenceID.numocts = call->confIdentifier.numocts;
	memcpy(setup->conferenceID.data, call->confIdentifier.data,
		call->confIdentifier.numocts);

	setup->conferenceGoal.t = T_H225Setup_UUIE_conferenceGoal_create;
	/* H.225 point to point call */
	setup->callType.t = T_H225CallType_pointToPoint;

	/* Populate optional fields */

	if (call->callIdentifier.guid.numocts > 0) {
		setup->m.callIdentifierPresent = TRUE;
		setup->callIdentifier.guid.numocts = call->callIdentifier.guid.numocts;
		memcpy(setup->callIdentifier.guid.data,
			call->callIdentifier.guid.data,
			call->callIdentifier.guid.numocts);
	}
	else setup->m.callIdentifierPresent = FALSE;

	setup->m.mediaWaitForConnectPresent = TRUE;
	if (CHR_TESTFLAG(call->flags, CHR_M_MEDIAWAITFORCONN)) {
		setup->mediaWaitForConnect = TRUE;
	}
	else {
		setup->mediaWaitForConnect = FALSE;
	}
	setup->m.canOverlapSendPresent = TRUE;
	setup->canOverlapSend = FALSE;

	/* Populate the userInfo structure with the setup UUIE */

	q931msg->userInfo->h323_uu_pdu.h323_message_body.t =
		T_H225H323_UU_PDU_h323_message_body_setup;
	q931msg->userInfo->h323_uu_pdu.h323_message_body.u.setup = setup;
	q931msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent = 1;

	q931msg->userInfo->h323_uu_pdu.h245Tunneling =
		CHR_TESTFLAG(call->flags, CHR_M_TUNNELING);

	/* For H.323 version 4 and higher, if fast connect, tunneling should be
	supported.
	*/
	if (CHR_TESTFLAG(call->flags, CHR_M_FASTSTART))
		q931msg->userInfo->h323_uu_pdu.h245Tunneling = TRUE;

	CHRTRACEDBG3("Built SETUP message (%s, %s)\n", call->callType,
		call->callToken);

	/* H225 message callback */
	if (gH323ep.h225Callbacks.onBuiltSetup)
		gH323ep.h225Callbacks.onBuiltSetup(call, q931msg);

	ret = chrSendH225MsgtoQueue(call, q931msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue SETUP message to outbound queue. (%s, %s)\n", call->callType, call->callToken);
	}
	memReset(&gH323ep.msgctxt);

	return ret;
}


int chrH323HangCall(char * callToken, CHRCallClearReason reason)
{
	CHRH323CallData *call;

	call = chrFindCallByToken(callToken);
	if (!call)
	{
		CHRTRACEWARN2("WARN: Call hangup failed - Call %s not present\n",
			callToken);
		return CHR_FAILED;
	}
	CHRTRACEINFO3("Hanging up call (%s, %s)\n", call->callType, call->callToken);
	if (call->callState < CHR_CALL_CLEAR)
	{
		call->callEndReason = reason;
		call->callState = CHR_CALL_CLEAR;
	}
	return CHR_OK;
}

int chrSetBearerCapabilityIE
(Q931Message *pmsg, enum Q931CodingStandard codingStandard,
enum Q931InformationTransferCapability capability,
enum Q931TransferMode transferMode, enum Q931TransferRate transferRate,
enum Q931UserInfoLayer1Protocol userInfoLayer1)
{
	unsigned size = 3;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	if (pmsg->bearerCapabilityIE)
	{
		memFreePtr(pctxt, pmsg->bearerCapabilityIE);
		pmsg->bearerCapabilityIE = NULL;
	}

	pmsg->bearerCapabilityIE = (Q931InformationElement*)
		memAlloc(pctxt, sizeof(Q931InformationElement)+size - 1);
	if (!pmsg->bearerCapabilityIE)
	{
		CHRTRACEERR1("Error:Memory - chrSetBearerCapabilityIE - bearerCapabilityIE"
			"\n");
		return CHR_FAILED;
	}

	pmsg->bearerCapabilityIE->discriminator = Q931BearerCapabilityIE;
	pmsg->bearerCapabilityIE->length = size;
	pmsg->bearerCapabilityIE->data[0] = (ASN1OCTET)(0x80 | ((codingStandard & 3) << 5) | (capability & 31));

	pmsg->bearerCapabilityIE->data[1] = (0x80 | ((transferMode & 3) << 5) | (transferRate & 31));

	pmsg->bearerCapabilityIE->data[2] = (0x80 | (1 << 5) | userInfoLayer1);

	return CHR_OK;
}

int chrQ931SetKeypadIE(Q931Message *pmsg, const char* data)
{
	unsigned len = 0;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	len = strlen(data);
	pmsg->keypadIE = (Q931InformationElement*)
		memAlloc(pctxt, sizeof(Q931InformationElement)+len - 1);
	if (!pmsg->keypadIE)
	{
		CHRTRACEERR1("Error:Memory - chrQ931SetKeypadIE - keypadIE\n");
		return CHR_FAILED;
	}

	pmsg->keypadIE->discriminator = Q931KeypadIE;
	pmsg->keypadIE->length = len;
	memcpy(pmsg->keypadIE->data, data, len);
	return CHR_OK;
}




int chrQ931SetCallingPartyNumberIE
(Q931Message *pmsg, const char *number, unsigned plan, unsigned type,
unsigned presentation, unsigned screening)
{
	unsigned len = 0;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	if (pmsg->callingPartyNumberIE)
	{
		memFreePtr(pctxt, pmsg->callingPartyNumberIE);
		pmsg->callingPartyNumberIE = NULL;
	}

	len = strlen(number);
	pmsg->callingPartyNumberIE = (Q931InformationElement*)
		memAlloc(pctxt, sizeof(Q931InformationElement)+len + 2 - 1);
	if (!pmsg->callingPartyNumberIE)
	{
		CHRTRACEERR1("Error:Memory - chrQ931SetCallingPartyNumberIE - "
			"callingPartyNumberIE\n");
		return CHR_FAILED;
	}
	pmsg->callingPartyNumberIE->discriminator = Q931CallingPartyNumberIE;
	pmsg->callingPartyNumberIE->length = len + 2;
	pmsg->callingPartyNumberIE->data[0] = (((type & 7) << 4) | (plan & 15));
	pmsg->callingPartyNumberIE->data[1] = (0x80 | ((presentation & 3) << 5) | (screening & 3));
	memcpy(pmsg->callingPartyNumberIE->data + 2, number, len);

	return CHR_OK;
}

int chrQ931SetCalledPartyNumberIE
(Q931Message *pmsg, const char *number, unsigned plan, unsigned type)
{
	unsigned len = 0;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	if (pmsg->calledPartyNumberIE)
	{
		memFreePtr(pctxt, pmsg->calledPartyNumberIE);
		pmsg->calledPartyNumberIE = NULL;
	}

	len = strlen(number);
	pmsg->calledPartyNumberIE = (Q931InformationElement*)
		memAlloc(pctxt, sizeof(Q931InformationElement)+len + 1 - 1);
	if (!pmsg->calledPartyNumberIE)
	{
		CHRTRACEERR1("Error:Memory - chrQ931SetCalledPartyNumberIE - "
			"calledPartyNumberIE\n");
		return CHR_FAILED;
	}
	pmsg->calledPartyNumberIE->discriminator = Q931CalledPartyNumberIE;
	pmsg->calledPartyNumberIE->length = len + 1;
	pmsg->calledPartyNumberIE->data[0] = (0x80 | ((type & 7) << 4) | (plan & 15));
	memcpy(pmsg->calledPartyNumberIE->data + 1, number, len);

	return CHR_OK;
}

int chrQ931SetCauseIE(Q931Message *pmsg, enum Q931CauseValues cause, unsigned coding,unsigned location)
{
	OOCTXT *pctxt = &gH323ep.msgctxt;

	if (pmsg->causeIE){
		memFreePtr(pctxt, pmsg->causeIE);
		pmsg->causeIE = NULL;
	}

	pmsg->causeIE = (Q931InformationElement*)
		memAlloc(pctxt, sizeof(Q931InformationElement)+1);
	if (!pmsg->causeIE)
	{
		CHRTRACEERR1("Error:Memory - chrQ931SetCauseIE - causeIE\n");
		return CHR_FAILED;
	}
	pmsg->causeIE->discriminator = Q931CauseIE;
	pmsg->causeIE->length = 2;
	pmsg->causeIE->data[0] = (0x80 | ((coding & 0x03) << 5) | (location & 0x0F));

	pmsg->causeIE->data[1] = (0x80 | cause);

	return CHR_OK;
}


int chrCallEstbTimerExpired(void *data)
{

	chrTimerCallback *cbData = (chrTimerCallback*)data;
	CHRH323CallData *call = cbData->call;
	CHRTRACEINFO3("Call Establishment timer expired. (%s, %s)\n",
		call->callType, call->callToken);
	memFreePtr(call->pctxt, cbData);
	if (call->callState < CHR_CALL_CLEAR){
		call->callState = CHR_CALL_CLEAR;
		call->callEndReason = CHR_REASON_LOCAL_CLEARED;
	}

	return CHR_OK;
}


int chrQ931GetCauseAndReasonCodeFromCallClearReason
(CHRCallClearReason clearReason, enum Q931CauseValues *cause,
unsigned *reasonCode)
{
	switch (clearReason)
	{
	case CHR_REASON_INVALIDMESSAGE:
	case CHR_REASON_TRANSPORTFAILURE:
		*reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
		*cause = Q931ProtocolErrorUnspecified;
		break;
	case CHR_REASON_NOBW:
		*reasonCode = T_H225ReleaseCompleteReason_noBandwidth;
		*cause = Q931ErrorInCauseIE;
		break;
	case CHR_REASON_GK_NOCALLEDUSER:
		*reasonCode = T_H225ReleaseCompleteReason_calledPartyNotRegistered;
		*cause = Q931SubscriberAbsent;
		break;
	case CHR_REASON_GK_NOCALLERUSER:
		*reasonCode = T_H225ReleaseCompleteReason_callerNotRegistered;
		*cause = Q931SubscriberAbsent;
		break;
	case CHR_REASON_GK_UNREACHABLE:
		*reasonCode = T_H225ReleaseCompleteReason_unreachableGatekeeper;
		*cause = Q931TemporaryFailure;
		break;
	case CHR_REASON_GK_NORESOURCES:
	case CHR_REASON_GK_CLEARED:
		*reasonCode = T_H225ReleaseCompleteReason_gatekeeperResources;
		*cause = Q931Congestion;
		break;
	case CHR_REASON_NOCOMMON_CAPABILITIES:
		*reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
		*cause = Q931IncompatibleDestination;
		break;
	case CHR_REASON_LOCAL_FWDED:
	case CHR_REASON_REMOTE_FWDED:
		*reasonCode = T_H225ReleaseCompleteReason_facilityCallDeflection;
		*cause = Q931Redirection;
		break;
	case CHR_REASON_REMOTE_CLEARED:
	case CHR_REASON_LOCAL_CLEARED:
		*reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
		*cause = Q931NormalCallClearing;
		break;
	case CHR_REASON_REMOTE_BUSY:
	case CHR_REASON_LOCAL_BUSY:
		*reasonCode = T_H225ReleaseCompleteReason_inConf;
		*cause = Q931UserBusy;
		break;
	case CHR_REASON_REMOTE_NOANSWER:
	case CHR_REASON_LOCAL_NOTANSWERED:
		*reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
		*cause = Q931NoAnswer;
		break;
	case CHR_REASON_REMOTE_REJECTED:
	case CHR_REASON_LOCAL_REJECTED:
		*reasonCode = T_H225ReleaseCompleteReason_destinationRejection;
		*cause = Q931CallRejected;
		break;
	case CHR_REASON_REMOTE_CONGESTED:
	case CHR_REASON_LOCAL_CONGESTED:
		*reasonCode = T_H225ReleaseCompleteReason_noBandwidth;
		*cause = Q931Congestion;
		break;
	case CHR_REASON_NOROUTE:
		*reasonCode = T_H225ReleaseCompleteReason_unreachableDestination;
		*cause = Q931NoRouteToDestination;
		break;
	case CHR_REASON_NOUSER:
		*reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
		*cause = Q931SubscriberAbsent;
		break;
	case CHR_REASON_UNKNOWN:
	default:
		*reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
		*cause = Q931NormalUnspecified;
	}

	return CHR_OK;
}

enum CHRCallClearReason chrGetCallClearReasonFromCauseAndReasonCode
	(enum Q931CauseValues cause, unsigned reasonCode)
{
	switch (cause)
	{
	case Q931NormalCallClearing:
		return CHR_REASON_REMOTE_CLEARED;

	case Q931UserBusy:
		return CHR_REASON_REMOTE_BUSY;

	case Q931NoResponse:
	case Q931NoAnswer:
		return CHR_REASON_REMOTE_NOANSWER;

	case Q931CallRejected:
		return CHR_REASON_REMOTE_REJECTED;

	case Q931Redirection:
		return CHR_REASON_REMOTE_FWDED;

	case Q931NetworkOutOfOrder:
	case Q931TemporaryFailure:
		return CHR_REASON_TRANSPORTFAILURE;

	case Q931NoCircuitChannelAvailable:
	case Q931Congestion:
	case Q931RequestedCircuitUnAvailable:
	case Q931ResourcesUnavailable:
		return CHR_REASON_REMOTE_CONGESTED;

	case Q931NoRouteToDestination:
	case Q931NoRouteToNetwork:
		return CHR_REASON_NOROUTE;
	case Q931NumberChanged:
	case Q931UnallocatedNumber:
	case Q931SubscriberAbsent:
		return CHR_REASON_NOUSER;
	case Q931ChannelUnacceptable:
	case Q931DestinationOutOfOrder:
	case Q931InvalidNumberFormat:
	case Q931NormalUnspecified:
	case Q931StatusEnquiryResponse:
	case Q931IncompatibleDestination:
	case Q931ProtocolErrorUnspecified:
	case Q931RecoveryOnTimerExpiry:
	case Q931InvalidCallReference:
	default:
		switch (reasonCode)
		{
		case T_H225ReleaseCompleteReason_noBandwidth:
			return CHR_REASON_NOBW;
		case T_H225ReleaseCompleteReason_gatekeeperResources:
			return CHR_REASON_GK_NORESOURCES;
		case T_H225ReleaseCompleteReason_unreachableDestination:
			return CHR_REASON_NOROUTE;
		case T_H225ReleaseCompleteReason_destinationRejection:
			return CHR_REASON_REMOTE_REJECTED;
		case T_H225ReleaseCompleteReason_inConf:
			return CHR_REASON_REMOTE_BUSY;
		case T_H225ReleaseCompleteReason_facilityCallDeflection:
			return CHR_REASON_REMOTE_FWDED;
		case T_H225ReleaseCompleteReason_calledPartyNotRegistered:
			return CHR_REASON_GK_NOCALLEDUSER;
		case T_H225ReleaseCompleteReason_callerNotRegistered:
			return CHR_REASON_GK_NOCALLERUSER;
		case T_H225ReleaseCompleteReason_gatewayResources:
			return CHR_REASON_GK_NORESOURCES;
		case T_H225ReleaseCompleteReason_unreachableGatekeeper:
			return CHR_REASON_GK_UNREACHABLE;
		case T_H225ReleaseCompleteReason_invalidRevision:
		case T_H225ReleaseCompleteReason_noPermission:
		case T_H225ReleaseCompleteReason_badFormatAddress:
		case T_H225ReleaseCompleteReason_adaptiveBusy:
		case T_H225ReleaseCompleteReason_undefinedReason:
		case T_H225ReleaseCompleteReason_securityDenied:
		case T_H225ReleaseCompleteReason_newConnectionNeeded:
		case T_H225ReleaseCompleteReason_nonStandardReason:
		case T_H225ReleaseCompleteReason_replaceWithConferenceInvite:
		case T_H225ReleaseCompleteReason_genericDataReason:
		case T_H225ReleaseCompleteReason_neededFeatureNotSupported:
		case T_H225ReleaseCompleteReason_tunnelledSignallingRejected:
		case T_H225ReleaseCompleteReason_invalidCID:
		case T_H225ReleaseCompleteReason_securityError:
		case T_H225ReleaseCompleteReason_hopCountExceeded:
		case T_H225ReleaseCompleteReason_extElem1:
		default:
			return CHR_REASON_UNKNOWN;
		}
	}
	return CHR_REASON_UNKNOWN;
}

/**
This function is used to parse destination string passed to chrH323MakeCall and
chrH323ForwardCall. If the string contains ip address and port, it is returned
in the parsedIP buffer and if it contains alias, it is added to aliasList
*/
int chrParseDestination
(struct CHRH323CallData *call, char *dest, char* parsedIP, unsigned len,
chrAliases** aliasList)
{
	int iEk = -1, iDon = -1, iTeen = -1, iChaar = -1, iPort = -1, i;
	chrAliases * psNewAlias = NULL;
	char *cAt = NULL, *host = NULL;
	char tmp[256], buf[30];
	char *alias = NULL;
	OOCTXT *pctxt = call->pctxt;
	parsedIP[0] = '\0';

	CHRTRACEINFO2("Parsing destination %s\n", dest);

	/* Test for an IP address:Note that only supports dotted IPv4.
	IPv6 won't pass the test and so will numeric IP representation*/

	sscanf(dest, "%d.%d.%d.%d:%d", &iEk, &iDon, &iTeen, &iChaar, &iPort);
	if ((iEk > 0 && iEk <= 255) &&
		(iDon >= 0 && iDon <= 255) &&
		(iTeen >= 0 && iTeen <= 255) &&
		(iChaar >= 0 && iChaar <= 255) &&
		(!strchr(dest, ':') || iPort != -1))
	{
		if (!strchr(dest, ':'))
			iPort = 1720; /*Default h.323 port */

		sprintf(buf, "%d.%d.%d.%d:%d", iEk, iDon, iTeen, iChaar, iPort);
		if (strlen(buf) + 1>len)
		{
			CHRTRACEERR1("Error:Insufficient buffer space for parsed ip - "
				"chrParseDestination\n");
			return CHR_FAILED;
		}

		strcpy(parsedIP, buf);
		return CHR_OK;
	}

	/* alias@host */
	strncpy(tmp, dest, sizeof(tmp)-1);
	tmp[sizeof(tmp)-1] = '\0';
	if ((host = strchr(tmp, '@')) != NULL)
	{
		*host = '\0';
		host++;
		sscanf(host, "%d.%d.%d.%d:%d", &iEk, &iDon, &iTeen, &iChaar, &iPort);
		if ((iEk > 0 && iEk <= 255) &&
			(iDon >= 0 && iDon <= 255) &&
			(iTeen >= 0 && iTeen <= 255) &&
			(iChaar >= 0 && iChaar <= 255) &&
			(!strchr(host, ':') || iPort != -1))
		{
			if (!strchr(dest, ':'))
				iPort = 1720; /*Default h.323 port */

			sprintf(buf, "%d.%d.%d.%d:%d", iEk, iDon, iTeen, iChaar, iPort);
			if (strlen(buf) + 1>len)
			{
				CHRTRACEERR1("Error:Insufficient buffer space for parsed ip - "
					"chrParseDestination\n");
				return CHR_FAILED;
			}

			strncpy(parsedIP, buf, len - 1);
			parsedIP[len - 1] = '\0';
			alias = tmp;
		}
	}

	if (!alias)
	{
		alias = dest;
	}
	/* url test */
	if (alias == strstr(alias, "http://"))
	{
		psNewAlias = (chrAliases*)memAlloc(pctxt, sizeof(chrAliases));
		if (!psNewAlias)
		{
			CHRTRACEERR1("Error:Memory - chrParseDestination - psNewAlias\n");
			return CHR_FAILED;
		}
		psNewAlias->type = T_H225AliasAddress_url_ID;
		psNewAlias->value = (char*)memAlloc(pctxt, strlen(alias) + 1);
		if (!psNewAlias->value)
		{
			CHRTRACEERR1("Error:Memory - chrParseDestination - "
				"psNewAlias->value\n");
			memFreePtr(pctxt, psNewAlias);
			return CHR_FAILED;
		}
		strcpy(psNewAlias->value, alias);
		psNewAlias->next = *aliasList;
		*aliasList = psNewAlias;
		CHRTRACEINFO2("Destination parsed as url %s\n", psNewAlias->value);
		return CHR_OK;
	}

	/* E-mail ID test */
	if ((cAt = strchr(alias, '@')) && alias != strchr(alias, '@'))
	{
		if (strchr(cAt, '.'))
		{
			psNewAlias = (chrAliases*)memAlloc(pctxt, sizeof(chrAliases));
			if (!psNewAlias)
			{
				CHRTRACEERR1("Error:Memory - chrParseDestination - psNewAlias\n");
				return CHR_FAILED;
			}
			psNewAlias->type = T_H225AliasAddress_email_ID;
			psNewAlias->value = (char*)memAlloc(pctxt, strlen(alias) + 1);
			if (!psNewAlias->value)
			{
				CHRTRACEERR1("Error:Memory - chrParseDestination - "
					"psNewAlias->value\n");
				memFreePtr(pctxt, psNewAlias);
				return CHR_FAILED;
			}
			strcpy(psNewAlias->value, alias);
			psNewAlias->next = *aliasList;
			*aliasList = psNewAlias;
			CHRTRACEINFO2("Destination is parsed as email %s\n", psNewAlias->value);
			return CHR_OK;
		}
	}


	/* e-164 */
	/* strspn(dest, "1234567890*#,") == strlen(dest)*/
	/* Dialed digits test*/
	for (i = 0; *(alias + i) != '\0'; i++)
	{
		if (!isdigit(alias[i]) && alias[i] != '#' && alias[i] != '*' &&
			alias[i] != ',')
			break;
	}
	if (*(alias + i) == '\0')
	{
		psNewAlias = (chrAliases*)memAlloc(pctxt, sizeof(chrAliases));
		if (!psNewAlias)
		{
			CHRTRACEERR1("Error:Memory - chrParseDestination - psNewAlias\n");
			return CHR_FAILED;
		}
		/*      memset(psNewAlias, 0, sizeof(chrAliases));*/
		psNewAlias->type = T_H225AliasAddress_dialedDigits;
		psNewAlias->value = (char*)memAlloc(pctxt, strlen(alias) + 1);
		if (!psNewAlias->value)
		{
			CHRTRACEERR1("Error:Memroy - chrParseDestination - "
				"psNewAlias->value\n");
			memFreePtr(pctxt, psNewAlias);
			return CHR_FAILED;
		}
		strcpy(psNewAlias->value, alias);
		psNewAlias->next = *aliasList;
		*aliasList = psNewAlias;
		CHRTRACEINFO2("Destination is parsed as dialed digits %s\n",
			psNewAlias->value);
		/* Also set called party number */
		if (!call->calledPartyNumber)
		{
			if (chrCallSetCalledPartyNumber(call, alias) != CHR_OK)
			{
				CHRTRACEWARN3("Warning:Failed to set calling party number."
					"(%s, %s)\n", call->callType, call->callToken);
			}
		}
		return CHR_OK;
	}
	/* Evrything else is an h323-id for now */
	psNewAlias = (chrAliases*)memAlloc(pctxt, sizeof(chrAliases));
	if (!psNewAlias)
	{
		CHRTRACEERR1("Error:Memory - chrParseDestination - psNewAlias\n");
		return CHR_FAILED;
	}
	psNewAlias->type = T_H225AliasAddress_h323_ID;
	psNewAlias->value = (char*)memAlloc(pctxt, strlen(alias) + 1);
	if (!psNewAlias->value)
	{
		CHRTRACEERR1("Error:Memory - chrParseDestination - psNewAlias->value\n");
		memFreePtr(pctxt, psNewAlias);
		return CHR_FAILED;
	}
	strcpy(psNewAlias->value, alias);
	psNewAlias->next = *aliasList;
	*aliasList = psNewAlias;
	CHRTRACEINFO2("Destination for new call is parsed as h323-id %s \n",
		psNewAlias->value);
	return CHR_OK;
}

const char* chrGetMsgTypeText(int msgType)
{
	static const char *msgTypeText[] = {
		"CHRQ931MSG",
		"CHRH245MSG",
		"CHRSetup",
		"CHRCallProceeding",
		"CHRAlert",
		"CHRConnect",
		"CHRReleaseComplete",
		"CHRFacility",
		"CHRInformation",
		"CHRMasterSlaveDetermination",
		"CHRMasterSlaveAck",
		"CHRMasterSlaveReject",
		"CHRMasterSlaveRelease",
		"CHRTerminalCapabilitySet",
		"CHRTerminalCapabilitySetAck",
		"CHRTerminalCapabilitySetReject",
		"CHRTerminalCapabilitySetRelease",
		"CHROpenLogicalChannel",
		"CHROpenLogicalChannelAck",
		"CHROpenLogicalChannelReject",
		"CHROpenLogicalChannelRelease",
		"CHROpenLogicalChannelConfirm",
		"CHRCloseLogicalChannel",
		"CHRCloseLogicalChannelAck",
		"CHRRequestChannelClose",
		"CHRRequestChannelCloseAck",
		"CHRRequestChannelCloseReject",
		"CHRRequestChannelCloseRelease",
		"CHREndSessionCommand",
		"CHRUserInputIndication",
		"CHRRequestDelayResponse"
	};
	int idx = msgType - CHR_MSGTYPE_MIN;
	return chrUtilsGetText(idx, msgTypeText, OONUMBEROF(msgTypeText));
}

const char* chrGetQ931CauseValueText(int val)
{
	switch (val)
	{
	case Q931UnallocatedNumber:
		return "Q931UnallocatedNumber";
	case Q931NoRouteToNetwork:
		return "Q931NoRouteToNetwork";
	case Q931NoRouteToDestination:
		return "Q931NoRouteToDestination";
	case Q931ChannelUnacceptable:
		return "Q931ChannelUnacceptable";
	case Q931NormalCallClearing:
		return "Q931NormalCallClearing";
	case Q931UserBusy:
		return "Q931UserBusy";
	case Q931NoResponse:
		return "Q931NoResponse";
	case Q931NoAnswer:
		return "Q931NoAnswer";
	case Q931SubscriberAbsent:
		return "Q931SubscriberAbsent";
	case Q931CallRejected:
		return "Q931CallRejected";
	case Q931NumberChanged:
		return "Q931NumberChanged";
	case Q931Redirection:
		return "Q931Redirection";
	case Q931DestinationOutOfOrder:
		return "Q931DestinationOutOfOrder";
	case Q931InvalidNumberFormat:
		return "Q931InvalidNumberFormat";
	case Q931NormalUnspecified:
		return "Q931NormalUnspecified";
	case Q931StatusEnquiryResponse:
		return "Q931StatusEnquiryResponse";
	case Q931NoCircuitChannelAvailable:
		return "Q931NoCircuitChannelAvailable";
	case Q931NetworkOutOfOrder:
		return "Q931NetworkOutOfOrder";
	case Q931TemporaryFailure:
		return "Q931TemporaryFailure";
	case Q931Congestion:
		return "Q931Congestion";
	case Q931RequestedCircuitUnAvailable:
		return "Q931RequestedCircuitUnavailable";
	case Q931ResourcesUnavailable:
		return "Q931ResourcesUnavailable";
	case Q931IncompatibleDestination:
		return "Q931IncompatibleDestination";
	case Q931ProtocolErrorUnspecified:
		return "Q931ProtocolErrorUnspecified";
	case Q931RecoveryOnTimerExpiry:
		return "Q931RecoveryOnTimerExpiry";
	case Q931InvalidCallReference:
		return "Q931InvaliedCallReference";
	default:
		return "Unsupported Cause Type";
	}
	return "Unsupported Cause Type";
}



/****************************** H245 ******************************/

static ASN1OBJID gh245ProtocolID = {
	6, { 0, 0, 8, 245, 0, 8 }
};

int chrCreateH245Message(H245Message **pph245msg, int type)
{
	OOCTXT* pctxt = &gH323ep.msgctxt;

	*pph245msg = (H245Message*)memAlloc(pctxt, sizeof(H245Message));

	if (!(*pph245msg))
	{
		CHRTRACEERR1("ERROR:Failed to allocate memory for h245 message\n");
		return CHR_FAILED;
	}
	else
	{
		(*pph245msg)->h245Msg.t = type;
		(*pph245msg)->logicalChannelNo = 0;
		switch (type)
		{
		case  T_H245MultimediaSystemControlMessage_request:
			(*pph245msg)->h245Msg.u.request = (H245RequestMessage*)
				memAllocZ(pctxt, sizeof(H245RequestMessage));

			/*Check for successful mem allocation, and if successful initialize
			mem to zero*/
			if (!(*pph245msg)->h245Msg.u.request)
			{
				CHRTRACEERR1("ERROR:Memory allocation for H.245 request"
					" message failed\n");
				return CHR_FAILED;
			}
			break;

		case T_H245MultimediaSystemControlMessage_response:
			(*pph245msg)->h245Msg.u.response = (H245ResponseMessage*)
				memAllocZ(pctxt, sizeof(H245ResponseMessage));

			/*Check for successful mem allocation, and if successful initialize
			mem to zero*/
			if (!(*pph245msg)->h245Msg.u.response)
			{
				CHRTRACEERR1("ERROR:Memory allocation for H.245 response"
					" message failed\n");
				return CHR_FAILED;
			}
			break;

		case T_H245MultimediaSystemControlMessage_command:
			(*pph245msg)->h245Msg.u.command = (H245CommandMessage*)
				memAllocZ(pctxt, sizeof(H245CommandMessage));

			/*Check for successful mem allocation, and if successful initialize
			mem to zero*/
			if (!(*pph245msg)->h245Msg.u.command)
			{
				CHRTRACEERR1("ERROR:Memory allocation for H.245 command"
					" message failed\n");
				return CHR_FAILED;
			}
			break;

		case T_H245MultimediaSystemControlMessage_indication:
			(*pph245msg)->h245Msg.u.indication = (H245IndicationMessage*)
				memAllocZ(pctxt, sizeof(H245IndicationMessage));

			/*Check for successful mem allocation, and if successful initialize
			mem to zero*/
			if (!(*pph245msg)->h245Msg.u.indication)
			{
				CHRTRACEERR1("ERROR:Memory allocation for H.245 indication"
					" message failed\n");
				return CHR_FAILED;
			}
			break;

		default:
			CHRTRACEERR1("ERROR: H245 message type not supported\n");
		}

		return CHR_OK;
	}
}

int chrFreeH245Message(CHRH323CallData *call, H245Message *pmsg)
{
	/* In case of tunneling, memory is freed when corresponding Q931 message is freed.*/
	CHRTRACEDBG1("msgCtxt Reset?");
	if (0 != pmsg) {
		if (!CHR_TESTFLAG(call->flags, CHR_M_TUNNELING)){
			memReset(&gH323ep.msgctxt);
			CHRTRACEDBG3(" Done (%s, %s)\n", call->callType, call->callToken);
			return CHR_OK;
		}
	}
	CHRTRACEDBG3("Not Done (%s, %s)\n", call->callType, call->callToken);
	return CHR_OK;
}

#ifndef _COMPACT
static void chrPrintH245Message
(CHRH323CallData* call, ASN1OCTET* msgbuf, ASN1UINT msglen)
{
	OOCTXT ctxt;
	H245MultimediaSystemControlMessage mmMsg;
	int ret;

	initContext(&ctxt);

	setPERBuffer(&ctxt, msgbuf, msglen, TRUE);

	initializePrintHandler(&printHandler, "Sending H.245 Message");

	/* Set event handler */
	setEventHandler(&ctxt, &printHandler);

	ret = asn1PD_H245MultimediaSystemControlMessage(&ctxt, &mmMsg);
	if (ret != ASN_OK)
	{
		CHRTRACEERR3("Error decoding H245 message (%s, %s)\n",
			call->callType, call->callToken);
		CHRTRACEERR1(errGetText(&ctxt));
	}
	finishPrint();
	freeContext(&ctxt);
}
#endif

int chrEncodeH245Message
(CHRH323CallData *call, H245Message *ph245Msg, ASN1OCTET* msgbuf, size_t size)
{
	int len = 0, encodeLen = 0, i = 0;
	int stat = 0;
	ASN1OCTET* encodePtr = NULL;
	H245MultimediaSystemControlMessage *multimediaMsg;
	OOCTXT *pctxt = &gH323ep.msgctxt;
	multimediaMsg = &(ph245Msg->h245Msg);

	if (!msgbuf || size<200)
	{
		CHRTRACEERR3("Error: Invalid message buffer/size for "
			"chrEncodeH245Message. (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	msgbuf[i++] = ph245Msg->msgType;
	msgbuf[i++] = (ph245Msg->logicalChannelNo >> 8);
	msgbuf[i++] = ph245Msg->logicalChannelNo;
	/* This will contain the total length of the encoded message */
	msgbuf[i++] = 0;
	msgbuf[i++] = 0;

	if (!CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
	{
		/* Populate message buffer to be returned */
		len = 4;
		msgbuf[i++] = 3; /* TPKT version */
		msgbuf[i++] = 0; /* TPKT resevred */
		/* 1st octet of length, will be populated once len is determined */
		msgbuf[i++] = 0;
		/* 2nd octet of length, will be populated once len is determined */
		msgbuf[i++] = 0;
	}

	setPERBuffer(pctxt, msgbuf + i, (size - i), TRUE);

	stat = asn1PE_H245MultimediaSystemControlMessage(&gH323ep.msgctxt,
		multimediaMsg);

	if (stat != ASN_OK) {
		CHRTRACEERR3("ERROR: H245 Message encoding failed (%s, %s)\n",
			call->callType, call->callToken);
		CHRTRACEERR1(errGetText(&gH323ep.msgctxt));
		return CHR_FAILED;
	}

	encodePtr = encodeGetMsgPtr(pctxt, &encodeLen);
	len += encodeLen;
	msgbuf[3] = (len >> 8);
	msgbuf[4] = len;
	if (!CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
	{
		msgbuf[7] = len >> 8;
		msgbuf[8] = len;
	}
#ifndef _COMPACT
	chrPrintH245Message(call, encodePtr, encodeLen);
#endif
	return CHR_OK;
}

int chrSendH245Msg(CHRH323CallData *call, H245Message *msg)
{
	int iRet = 0, len = 0, msgType = 0, logicalChannelNo = 0;
	ASN1OCTET * encodebuf;


	if (!call)
		return CHR_FAILED;

	encodebuf = (ASN1OCTET*)memAlloc(call->pctxt, MAXMSGLEN);
	if (!encodebuf)
	{
		CHRTRACEERR3("Error:Failed to allocate memory for encoding H245 "
			"message(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	iRet = chrEncodeH245Message(call, msg, encodebuf, MAXMSGLEN);

	if (iRet != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to encode H245 message. (%s, %s)\n",
			call->callType, call->callToken);
		memFreePtr(call->pctxt, encodebuf);
		return CHR_FAILED;
	}
	if (!call->pH245Channel)
	{
		call->pH245Channel =
			(CHRH323Channel*)memAllocZ(call->pctxt, sizeof(CHRH323Channel));
		if (!call->pH245Channel)
		{
			CHRTRACEERR3("Error:Failed to allocate memory for H245Channel "
				"structure. (%s, %s)\n", call->callType, call->callToken);
			memFreePtr(call->pctxt, encodebuf);
			return CHR_FAILED;
		}
	}

	/* We need to send EndSessionCommand immediately.*/
	if (!CHR_TESTFLAG(call->flags, CHR_M_TUNNELING)){
		if (encodebuf[0] == CHREndSessionCommand) /* High priority message */
		{
			dListFreeAll(call->pctxt, &call->pH245Channel->outQueue);
			dListAppend(call->pctxt, &call->pH245Channel->outQueue, encodebuf);
			chrSendMsg(call, CHRH245MSG);
		}
		else{
			dListAppend(call->pctxt, &call->pH245Channel->outQueue, encodebuf);
			CHRTRACEDBG4("Queued H245 messages %d. (%s, %s)\n",
				call->pH245Channel->outQueue.count,
				call->callType, call->callToken);
		}
	}

	return CHR_OK;
}

int chrSendTermCapMsg(CHRH323CallData *call)
{
	int ret;
	H245RequestMessage *request = NULL;
	OOCTXT *pctxt = NULL;
	chrH323EpCapability *epCap = NULL;
	chrH323EpCapability *pListHeadCap = NULL;
	H245TerminalCapabilitySet *termCap = NULL;
	H245AudioCapability *audioCap = NULL;
	H245AudioTelephonyEventCapability *ateCap = NULL;
	H245UserInputCapability *userInputCap = NULL;
	H245CapabilityTableEntry *entry = NULL;
	H245AlternativeCapabilitySet *altSet = NULL;
	H245CapabilityDescriptor *capDesc = NULL;
	H245Message *ph245msg = NULL;
	H245VideoCapability *videoCap = NULL;

	int i = 0, j = 0, k = 0;
	if (call->localTermCapState == CHR_LocalTermCapSetSent)
	{
		CHRTRACEINFO3("TerminalCapabilitySet exchange procedure already in "
			"progress. (%s, %s)\n", call->callType, call->callToken);
		return CHR_OK;
	}

	ret = chrCreateH245Message
		(&ph245msg, T_H245MultimediaSystemControlMessage_request);

	if (ret == CHR_FAILED)
	{
		CHRTRACEERR3("Error:Failed to create H245 message for Terminal "
			"CapabilitySet (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	/* Set request type as TerminalCapabilitySet */
	request = ph245msg->h245Msg.u.request;
	pctxt = &gH323ep.msgctxt;
	ph245msg->msgType = CHRTerminalCapabilitySet;
	if (request == NULL)
	{
		CHRTRACEERR3("ERROR: No memory allocated for request message (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}
	memset(request, 0, sizeof(H245RequestMessage));

	request->t = T_H245RequestMessage_terminalCapabilitySet;
	request->u.terminalCapabilitySet = (H245TerminalCapabilitySet*)
		memAlloc(pctxt, sizeof(H245TerminalCapabilitySet));
	termCap = request->u.terminalCapabilitySet;
	memset(termCap, 0, sizeof(H245TerminalCapabilitySet));
	termCap->m.multiplexCapabilityPresent = 0;
	termCap->m.capabilityTablePresent = 1;
	termCap->m.capabilityDescriptorsPresent = 1;
	termCap->sequenceNumber = ++(call->localTermCapSeqNo);
	termCap->protocolIdentifier = gh245ProtocolID; /* protocol id */

	/* Add audio Capabilities */

	dListInit(&(termCap->capabilityTable));

	CHRTRACEINFO2("Populating capabilityTable, number of preferred "
		"capabilities = %d\n", call->capPrefs.index);

	if (0 != call->ourCaps) {
		CHRTRACEINFO1("Using call-specific capabilities.\n");
		pListHeadCap = call->ourCaps;
	}
	else {
		CHRTRACEINFO1("Using endpoint capabilities.\n");
		pListHeadCap = gH323ep.myCaps;
	}

	for (k = 0; k < (int)call->capPrefs.index; k++)
	{
		epCap = pListHeadCap;
		while (epCap) {
			if (epCap->cap == call->capPrefs.order[k])
				break;
			epCap = epCap->next;
		}
		if (!epCap)
		{
			CHRTRACEWARN4("WARN:Preferred capability %d not supported.(%s, %s)\n",
				call->capPrefs.order[k], call->callType, call->callToken);
			continue;
		}

		if (epCap->capType == CHR_CAP_TYPE_AUDIO)
		{

			/* Create audio capability. If capability supports receive, we only
			add it as receive capability in TCS. However, if it supports only
			transmit, we add it as transmit capability in TCS.
			*/
			if ((epCap->dir & CHRRX))
			{

				CHRTRACEDBG4("Sending receive capability %s in TCS.(%s, %s)\n",
					chrGetCapTypeText(epCap->cap), call->callType, call->callToken);

				audioCap = chrCapabilityCreateAudioCapability(epCap, pctxt, CHRRX);
				if (!audioCap)
				{
					CHRTRACEWARN4("WARN:Failed to create audio capability %s "
						"(%s, %s)\n", chrGetCapTypeText(epCap->cap),
						call->callType, call->callToken);
					continue;
				}
			}
			else if (epCap->dir & CHRTX)
			{
				CHRTRACEDBG4("Sending transmit capability %s in TCS.(%s, %s)\n",
					chrGetCapTypeText(epCap->cap), call->callType, call->callToken);
				audioCap = chrCapabilityCreateAudioCapability(epCap, pctxt, CHRTX);
				if (!audioCap)
				{
					CHRTRACEWARN4("WARN:Failed to create audio capability %s "
						"(%s, %s)\n", chrGetCapTypeText(epCap->cap),
						call->callType, call->callToken);
					continue;
				}
			}
			else{
				CHRTRACEWARN3("Warn:Capability is not RX/TX/RXANDTX. Symmetric "
					"capabilities are not supported.(%s, %s)\n",
					call->callType, call->callToken);
				continue;
			}
			/* Add  Capabilities to Capability Table */
			entry = (H245CapabilityTableEntry*)memAlloc(pctxt,
				sizeof(H245CapabilityTableEntry));
			if (!entry)
			{
				CHRTRACEERR3("Error:Memory - chrSendTermCapMsg - entry(audio Cap)."
					"(%s, %s)\n", call->callType, call->callToken);
				return CHR_FAILED;
			}
			memset(entry, 0, sizeof(H245CapabilityTableEntry));
			entry->m.capabilityPresent = 1;
			if ((epCap->dir & CHRRX))
			{
				entry->capability.t = T_H245Capability_receiveAudioCapability;
				entry->capability.u.receiveAudioCapability = audioCap;
			}
			else{
				entry->capability.t = T_H245Capability_transmitAudioCapability;
				entry->capability.u.transmitAudioCapability = audioCap;
			}
			entry->capabilityTableEntryNumber = i + 1;
			dListAppend(pctxt, &(termCap->capabilityTable), entry);
			i++;
		}
		else if (epCap->capType == CHR_CAP_TYPE_VIDEO)
		{
			if ((epCap->dir & CHRRX))
			{
				CHRTRACEDBG4("Sending receive capability %s in TCS.(%s, %s)\n",
					chrGetCapTypeText(epCap->cap), call->callType, call->callToken);
				videoCap = chrCapabilityCreateVideoCapability(epCap, pctxt, CHRRX);
				if (!videoCap)
				{
					CHRTRACEWARN4("WARN:Failed to create Video capability %s "
						"(%s, %s)\n", chrGetCapTypeText(epCap->cap),
						call->callType, call->callToken);
					continue;
				}
			}
			else if (epCap->dir & CHRTX)
			{
				CHRTRACEDBG4("Sending transmit capability %s in TCS.(%s, %s)\n",
					chrGetCapTypeText(epCap->cap), call->callType, call->callToken);
				videoCap = chrCapabilityCreateVideoCapability(epCap, pctxt, CHRTX);
				if (!videoCap)
				{
					CHRTRACEWARN4("WARN:Failed to create video capability %s "
						"(%s, %s)\n", chrGetCapTypeText(epCap->cap),
						call->callType, call->callToken);
					continue;
				}
			}
			else{
				CHRTRACEWARN3("Warn:Capability is not RX/TX/RXANDTX. Symmetric "
					"capabilities are not supported.(%s, %s)\n",
					call->callType, call->callToken);
				continue;
			}
			/* Add Video capabilities to Capability Table */
			entry = (H245CapabilityTableEntry*)memAlloc(pctxt,
				sizeof(H245CapabilityTableEntry));
			if (!entry)
			{
				CHRTRACEERR3("Error:Memory - chrSendTermCapMsg - entry(video Cap)."
					"(%s, %s)\n", call->callType, call->callToken);
				return CHR_FAILED;
			}
			memset(entry, 0, sizeof(H245CapabilityTableEntry));
			entry->m.capabilityPresent = 1;
			if ((epCap->dir & CHRRX))
			{
				entry->capability.t = T_H245Capability_receiveVideoCapability;
				entry->capability.u.receiveVideoCapability = videoCap;
			}
			else{
				entry->capability.t = T_H245Capability_transmitVideoCapability;
				entry->capability.u.transmitVideoCapability = videoCap;
			}
			entry->capabilityTableEntryNumber = i + 1;
			dListAppend(pctxt, &(termCap->capabilityTable), entry);
			i++;
		}
	}

	/*TODO:Add Video and Data capabilities, if required*/
	if (i == 0)
	{
		CHRTRACEERR3("Error:No capabilities found to send in TCS message."
			" (%s, %s)\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}

	/* Define capability descriptior */
	capDesc = (H245CapabilityDescriptor*)
		memAlloc(pctxt, sizeof(H245CapabilityDescriptor));
	memset(capDesc, 0, sizeof(H245CapabilityDescriptor));
	capDesc->m.simultaneousCapabilitiesPresent = 1;
	capDesc->capabilityDescriptorNumber = 1;
	dListInit(&(capDesc->simultaneousCapabilities));
	/* Add Alternative Capability Set.
	TODO: Right now all capabilities are added in separate
	alternate capabilities set. Need a way for application
	developer to specify the alternative capability sets.
	*/
	for (j = 0; j<i; j++)
	{
		altSet = (H245AlternativeCapabilitySet*)
			memAlloc(pctxt, sizeof(H245AlternativeCapabilitySet));
		memset(altSet, 0, sizeof(H245AlternativeCapabilitySet));
		altSet->n = 1;
		altSet->elem[0] = j + 1;

		dListAppend(pctxt, &(capDesc->simultaneousCapabilities), altSet);
	}

	dListInit(&(termCap->capabilityDescriptors));
	dListAppend(pctxt, &(termCap->capabilityDescriptors), capDesc);

	CHRTRACEDBG3("Built terminal capability set message (%s, %s)\n",
		call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue TCS message to outbound queue. "
			"(%s, %s)\n", call->callType, call->callToken);
	}
	else {
		call->localTermCapState = CHR_LocalTermCapSetSent;
	}

	chrFreeH245Message(call, ph245msg);

	return ret;
}


ASN1UINT chrGenerateStatusDeterminationNumber()
{
	ASN1UINT statusDeterminationNumber;
	ASN1UINT random_factor = getpid();

#ifdef _WIN32
	SYSTEMTIME systemTime;
	GetLocalTime(&systemTime);
	srand((systemTime.wMilliseconds ^ systemTime.wSecond) + random_factor);
#else
	struct timeval tv;
	gettimeofday(&tv, NULL);
	srand((tv.tv_usec ^ tv.tv_sec) + random_factor);
#endif

	statusDeterminationNumber = rand() % 16777216;
	return statusDeterminationNumber;
}
/* TODO: Should Send MasterSlave Release when no response from
Remote endpoint after MasterSlaveDetermination sent within
timeout.
*/
int chrHandleMasterSlave(CHRH323CallData *call, void * pmsg,
	int msgType)
{
	H245MasterSlaveDetermination *masterSlave;
	H245MasterSlaveDeterminationAck *masterSlaveAck;
	ASN1UINT statusDeterminationNumber;
	ASN1UINT diff;

	switch (msgType)
	{
	case CHRMasterSlaveDetermination:
		CHRTRACEINFO3("Master Slave Determination received (%s, %s)\n",
			call->callType, call->callToken);

		masterSlave = (H245MasterSlaveDetermination*)pmsg;

		if (masterSlave->terminalType < gH323ep.termType)
		{
			chrSendMasterSlaveDeterminationAck(call, "slave");
			call->masterSlaveState = CHR_MasterSlave_Master;
			CHRTRACEINFO3("MasterSlaveDetermination done - Master(%s, %s)\n",
				call->callType, call->callToken);
			return CHR_OK;
		}
		if (masterSlave->terminalType > gH323ep.termType)
		{
			chrSendMasterSlaveDeterminationAck(call, "master");
			call->masterSlaveState = CHR_MasterSlave_Slave;
			CHRTRACEINFO3("MasterSlaveDetermination done - Slave(%s, %s)\n",
				call->callType, call->callToken);
			return CHR_OK;
		}
		/* Since term types are same, master slave determination will
		be done based on statusdetermination number
		*/

		CHRTRACEDBG3("Determining master-slave based on StatusDetermination"
			"Number (%s, %s)\n", call->callType, call->callToken);
		if (call->masterSlaveState == CHR_MasterSlave_DetermineSent)
			statusDeterminationNumber = call->statusDeterminationNumber;
		else
			statusDeterminationNumber = chrGenerateStatusDeterminationNumber();

		diff = (masterSlave->statusDeterminationNumber -
			statusDeterminationNumber) & 0xFFFFFFu;

		if (diff == 0 || diff == 0x800000u)
		{
			chrSendMasterSlaveDeterminationReject(call);

			CHRTRACEERR3("ERROR:MasterSlaveDetermination failed- identical "
				"numbers (%s, %s)\n", call->callType, call->callToken);
		}
		else if (diff < 0x800000u)
		{
			chrSendMasterSlaveDeterminationAck(call, "slave");
			call->masterSlaveState = CHR_MasterSlave_Master;
			CHRTRACEINFO3("MasterSlaveDetermination done - Master(%s, %s)\n",
				call->callType, call->callToken);
		}
		else
		{
			chrSendMasterSlaveDeterminationAck(call, "master");
			call->masterSlaveState = CHR_MasterSlave_Slave;
			CHRTRACEINFO3("MasterSlaveDetermination done - Slave(%s, %s)\n",
				call->callType, call->callToken);
		}
		break;

	case CHRMasterSlaveAck:
		masterSlaveAck = (H245MasterSlaveDeterminationAck*)pmsg;
		if (call->masterSlaveState == CHR_MasterSlave_DetermineSent)
		{
			if (masterSlaveAck->decision.t ==
				T_H245MasterSlaveDeterminationAck_decision_master)
			{
				chrSendMasterSlaveDeterminationAck(call, "slave");
				call->masterSlaveState = CHR_MasterSlave_Master;
				CHRTRACEINFO3("MasterSlaveDetermination done - Master(%s, %s)\n",
					call->callType, call->callToken);
			}
			else
			{
				chrSendMasterSlaveDeterminationAck(call, "master");
				call->masterSlaveState = CHR_MasterSlave_Slave;
				CHRTRACEINFO3("MasterSlaveDetermination done - Slave(%s, %s)\n",
					call->callType, call->callToken);
			}
		}

		if (call->localTermCapState == CHR_LocalTermCapSetAckRecvd &&
			call->remoteTermCapState == CHR_RemoteTermCapSetAckSent)
		{
			/*Since Cap exchange and MasterSlave Procedures are done */
			if (gH323ep.h323Callbacks.openLogicalChannels)
				gH323ep.h323Callbacks.openLogicalChannels(call);
			else{
				if (!call->logicalChans)
					chrOpenLogicalChannels(call);
			}
#if 0
			if (!call->logicalChans){
				if (!gH323ep.h323Callbacks.openLogicalChannels)
					chrOpenLogicalChannels(call);
				else
					gH323ep.h323Callbacks.openLogicalChannels(call);
			}
#endif
		}
		else
			CHRTRACEDBG1("Not opening logical channels as Cap exchange "
			"remaining\n");
		break;
	default:
		CHRTRACEWARN3("Warn:Unhandled Master Slave message received - %s - "
			"%s\n", call->callType, call->callToken);
	}
	return CHR_OK;
}

int chrSendMasterSlaveDetermination(CHRH323CallData *call)
{
	int ret;
	H245Message* ph245msg = NULL;
	H245RequestMessage *request;
	OOCTXT *pctxt = &gH323ep.msgctxt;
	H245MasterSlaveDetermination* pMasterSlave;

	/* Check whether Master Slave Determination already in progress */
	if (call->masterSlaveState != CHR_MasterSlave_Idle)
	{
		CHRTRACEINFO3("MasterSlave determination already in progress (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_OK;
	}

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_request);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error: creating H245 message - MasterSlave Determination "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRMasterSlaveDetermination;
	request = ph245msg->h245Msg.u.request;
	request->t = T_H245RequestMessage_masterSlaveDetermination;
	request->u.masterSlaveDetermination = (H245MasterSlaveDetermination*)
		ASN1MALLOC(pctxt, sizeof(H245MasterSlaveDetermination));


	pMasterSlave = request->u.masterSlaveDetermination;
	memset(pMasterSlave, 0, sizeof(H245MasterSlaveDetermination));
	pMasterSlave->terminalType = gH323ep.termType;
	pMasterSlave->statusDeterminationNumber =
		chrGenerateStatusDeterminationNumber();
	call->statusDeterminationNumber = pMasterSlave->statusDeterminationNumber;

	CHRTRACEDBG3("Built MasterSlave Determination (%s, %s)\n", call->callType,
		call->callToken);
	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue MasterSlaveDetermination message to"
			" outbound queue. (%s, %s)\n", call->callType,
			call->callToken);
	}
	else
		call->masterSlaveState = CHR_MasterSlave_DetermineSent;

	chrFreeH245Message(call, ph245msg);

	return ret;
}

int chrSendMasterSlaveDeterminationAck(CHRH323CallData* call,
	char * status)
{
	int ret = 0;
	H245ResponseMessage * response = NULL;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_response);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:H245 message creation failed for - MasterSlave "
			"Determination Ack (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRMasterSlaveAck;
	response = ph245msg->h245Msg.u.response;
	memset(response, 0, sizeof(H245ResponseMessage));
	response->t = T_H245ResponseMessage_masterSlaveDeterminationAck;
	response->u.masterSlaveDeterminationAck = (H245MasterSlaveDeterminationAck*)
		ASN1MALLOC(pctxt, sizeof(H245MasterSlaveDeterminationAck));
	memset(response->u.masterSlaveDeterminationAck, 0,
		sizeof(H245MasterSlaveDeterminationAck));
	if (!strcmp("master", status))
		response->u.masterSlaveDeterminationAck->decision.t =
		T_H245MasterSlaveDeterminationAck_decision_master;
	else
		response->u.masterSlaveDeterminationAck->decision.t =
		T_H245MasterSlaveDeterminationAck_decision_slave;

	CHRTRACEDBG3("Built MasterSlave determination Ack (%s, %s)\n",
		call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue MasterSlaveDeterminationAck message"
			" to outbound queue. (%s, %s)\n", call->callType,
			call->callToken);
	}

	chrFreeH245Message(call, ph245msg);
	return ret;
}

int chrSendMasterSlaveDeterminationReject(CHRH323CallData* call)
{
	int ret = 0;
	H245ResponseMessage* response = NULL;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	ret = chrCreateH245Message
		(&ph245msg, T_H245MultimediaSystemControlMessage_response);

	if (ret != CHR_OK) {
		CHRTRACEERR3("Error:H245 message creation failed for - MasterSlave "
			"Determination Reject (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRMasterSlaveReject;
	response = ph245msg->h245Msg.u.response;

	response->t = T_H245ResponseMessage_masterSlaveDeterminationReject;

	response->u.masterSlaveDeterminationReject =
		(H245MasterSlaveDeterminationReject*)
		memAlloc(pctxt, sizeof(H245MasterSlaveDeterminationReject));

	response->u.masterSlaveDeterminationReject->cause.t =
		T_H245MasterSlaveDeterminationReject_cause_identicalNumbers;

	CHRTRACEDBG3("Built MasterSlave determination reject (%s, %s)\n",
		call->callType, call->callToken);

	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK) {
		CHRTRACEERR3
			("Error:Failed to enqueue MasterSlaveDeterminationReject "
			"message to outbound queue.(%s, %s)\n", call->callType,
			call->callToken);
	}

	chrFreeH245Message(call, ph245msg);

	return ret;
}

int chrSendMasterSlaveDeterminationRelease(CHRH323CallData * call)
{
	int ret = 0;
	H245IndicationMessage* indication = NULL;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	ret = chrCreateH245Message
		(&ph245msg, T_H245MultimediaSystemControlMessage_indication);

	if (ret != CHR_OK) {
		CHRTRACEERR3("Error:H245 message creation failed for - MasterSlave "
			"Determination Release (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRMasterSlaveRelease;
	indication = ph245msg->h245Msg.u.indication;

	indication->t = T_H245IndicationMessage_masterSlaveDeterminationRelease;

	indication->u.masterSlaveDeterminationRelease =
		(H245MasterSlaveDeterminationRelease*)
		memAlloc(pctxt, sizeof(H245MasterSlaveDeterminationRelease));

	if (!indication->u.masterSlaveDeterminationRelease)
	{
		CHRTRACEERR3("Error: Failed to allocate memory for MSDRelease message."
			" (%s, %s)\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	CHRTRACEDBG3("Built MasterSlave determination Release (%s, %s)\n",
		call->callType, call->callToken);

	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK) {
		CHRTRACEERR3
			("Error:Failed to enqueue MasterSlaveDeterminationRelease "
			"message to outbound queue.(%s, %s)\n", call->callType,
			call->callToken);
	}

	chrFreeH245Message(call, ph245msg);
	return ret;
}

int chrHandleMasterSlaveReject
(CHRH323CallData *call, H245MasterSlaveDeterminationReject* reject)
{
	if (call->msdRetries < DEFAULT_MAX_RETRIES)
	{
		call->msdRetries++;
		CHRTRACEDBG3("Retrying MasterSlaveDetermination. (%s, %s)\n",
			call->callType, call->callToken);
		call->masterSlaveState = CHR_MasterSlave_Idle;
		chrSendMasterSlaveDetermination(call);
		return CHR_OK;
	}
	CHRTRACEERR3("Error:Failed to complete MasterSlaveDetermination - "
		"Ending call. (%s, %s)\n", call->callType, call->callToken);
	if (call->callState < CHR_CALL_CLEAR)
	{
		call->callEndReason = CHR_REASON_LOCAL_CLEARED;
		call->callState = CHR_CALL_CLEAR;
	}
	return CHR_OK;
}


int chrHandleOpenLogicalChannel(CHRH323CallData* call,
	H245OpenLogicalChannel *olc)
{

	H245OpenLogicalChannel_forwardLogicalChannelParameters *flcp =
		&(olc->forwardLogicalChannelParameters);

#if 0
	if (!call->logicalChans)
		chrOpenLogicalChannels(call);
#endif

	/* Check whether channel type is supported. Only supported channel
	type for now is g711ulaw audio channel.
	*/
	switch (flcp->dataType.t)
	{
	case T_H245DataType_nonStandard:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_nonStandard' not supported (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_nullData:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_nullData' not supported (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_videoData:
	case T_H245DataType_audioData:
		chrHandleOpenLogicalChannel_helper(call, olc);
		break;
	case T_H245DataType_data:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_data' not supported (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_encryptionData:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_encryptionData' not supported (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_h235Control:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_h235Control' not supported (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_h235Media:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_h235Media' not supported (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_multiplexedStream:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_multiplexedStream' not supported(%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_redundancyEncoding:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_redundancyEncoding' not supported (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_multiplePayloadStream:
		CHRTRACEWARN3("Warn:Media channel data type "
			"'T_H245DataType_multiplePayloadStream' not supported (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	case T_H245DataType_fec:
		CHRTRACEWARN3("Warn:Media channel data type 'T_H245DataType_fec' not "
			"supported (%s, %s)\n", call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		break;
	default:
		CHRTRACEERR3("ERROR:Unknown media channel data type (%s, %s)\n",
			call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
	}

	return CHR_OK;
}

/*TODO: Need to clean logical channel in case of failure after creating one */
int chrHandleOpenLogicalChannel_helper(CHRH323CallData *call,
	H245OpenLogicalChannel*olc)
{
	int ret = 0;
	H245Message *ph245msg = NULL;
	H245ResponseMessage *response;
	H245OpenLogicalChannelAck *olcAck;
	chrH323EpCapability *epCap = NULL;
	H245H2250LogicalChannelAckParameters *h2250lcap = NULL;
	OOCTXT *pctxt;
	H245UnicastAddress *unicastAddrs, *unicastAddrs1;
	H245UnicastAddress_iPAddress *iPAddress, *iPAddress1;
	CHRLogicalChannel *pLogicalChannel = NULL;
	H245H2250LogicalChannelParameters *h2250lcp = NULL;
	H245OpenLogicalChannel_forwardLogicalChannelParameters *flcp =
		&(olc->forwardLogicalChannelParameters);

	if (!flcp || flcp->multiplexParameters.t != T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
	{
		CHRTRACEERR3("Error:chrHandleOpenLogicalChannel_helper - invalid forward "
			"logical channel parameters. (%s, %s)\n", call->callType,
			call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_unspecified);
		return CHR_FAILED;
	}

	h2250lcp = flcp->multiplexParameters.u.h2250LogicalChannelParameters;

	if (!(epCap = chrIsDataTypeSupported(call, &flcp->dataType, CHRRX)))
	{
		CHRTRACEERR4("ERROR:HandleOpenLogicalChannel_helper - capability %d not "
			"supported (%s, %s)\n", flcp->dataType.t, call->callType, call->callToken);
		chrSendOpenLogicalChannelReject(call, olc->forwardLogicalChannelNumber,
			T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported);
		return CHR_FAILED;
	}

	if (epCap->cap == CHR_H264VIDEO && epCap->params) { /* find payload type */
		CHRH264CapParams *params = (CHRH264CapParams *)epCap->params;

		if (h2250lcp->m.dynamicRTPPayloadTypePresent == 1) {
			params->recv_pt = h2250lcp->dynamicRTPPayloadType;
			CHRTRACEERR4("Channel DynamicPT: %d - CHR_H264VIDEO (%s, %s)\n",
				params->recv_pt, call->callType, call->callToken);
		}
	}

	/* Generate an Ack for the open channel request */
	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_response);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error: H245 message creation failed for - "
			"OpenLogicalChannel Ack (%s, %s)\n", call->callType,
			call->callToken);
		memFreePtr(call->pctxt, epCap);
		epCap = NULL;
		return CHR_FAILED;
	}

	ph245msg->msgType = CHROpenLogicalChannelAck;
	ph245msg->logicalChannelNo = olc->forwardLogicalChannelNumber;
	response = ph245msg->h245Msg.u.response;
	pctxt = &gH323ep.msgctxt;
	memset(response, 0, sizeof(H245ResponseMessage));
	response->t = T_H245ResponseMessage_openLogicalChannelAck;
	response->u.openLogicalChannelAck = (H245OpenLogicalChannelAck*)
		memAlloc(pctxt, sizeof(H245OpenLogicalChannelAck));
	olcAck = response->u.openLogicalChannelAck;
	memset(olcAck, 0, sizeof(H245OpenLogicalChannelAck));
	olcAck->forwardLogicalChannelNumber = olc->forwardLogicalChannelNumber;

	olcAck->m.forwardMultiplexAckParametersPresent = 1;
	olcAck->forwardMultiplexAckParameters.t =
		T_H245OpenLogicalChannelAck_forwardMultiplexAckParameters_h2250LogicalChannelAckParameters;
	olcAck->forwardMultiplexAckParameters.u.h2250LogicalChannelAckParameters =
		(H245H2250LogicalChannelAckParameters*)ASN1MALLOC(pctxt,
		sizeof(H245H2250LogicalChannelAckParameters));
	h2250lcap =
		olcAck->forwardMultiplexAckParameters.u.h2250LogicalChannelAckParameters;
	memset(h2250lcap, 0, sizeof(H245H2250LogicalChannelAckParameters));

	h2250lcap->m.mediaChannelPresent = 1;
	h2250lcap->m.mediaControlChannelPresent = 1;
	h2250lcap->m.sessionIDPresent = 1;

	if (h2250lcp->sessionID == 0)
		h2250lcap->sessionID = chrCallGenerateSessionID(call, epCap->capType, "receive");
	else
		h2250lcap->sessionID = h2250lcp->sessionID;

	h2250lcap->mediaChannel.t =
		T_H245TransportAddress_unicastAddress;
	h2250lcap->mediaChannel.u.unicastAddress = (H245UnicastAddress*)
		ASN1MALLOC(pctxt, sizeof(H245UnicastAddress));

	unicastAddrs = h2250lcap->mediaChannel.u.unicastAddress;
	memset(unicastAddrs, 0, sizeof(H245UnicastAddress));
	unicastAddrs->t = T_H245UnicastAddress_iPAddress;
	unicastAddrs->u.iPAddress = (H245UnicastAddress_iPAddress*)
		memAlloc(pctxt, sizeof(H245UnicastAddress_iPAddress));
	iPAddress = unicastAddrs->u.iPAddress;
	memset(iPAddress, 0, sizeof(H245UnicastAddress_iPAddress));

	pLogicalChannel = chrAddNewLogicalChannel(call,
		olc->forwardLogicalChannelNumber, h2250lcap->sessionID,
		"receive", epCap);
	if (!pLogicalChannel)
	{
		CHRTRACEERR3("ERROR:Failed to add new logical channel entry to call "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	chrSocketConvertIpToNwAddr
		(call->mediaInfo->lMediaIP, iPAddress->network.data, sizeof(iPAddress->network.data));

	iPAddress->network.numocts = 4;
	iPAddress->tsapIdentifier = pLogicalChannel->localRtpPort;

	/* media contrcol channel */
	h2250lcap->mediaControlChannel.t =
		T_H245TransportAddress_unicastAddress;
	h2250lcap->mediaControlChannel.u.unicastAddress = (H245UnicastAddress*)
		ASN1MALLOC(pctxt, sizeof(H245UnicastAddress));

	unicastAddrs1 = h2250lcap->mediaControlChannel.u.unicastAddress;
	memset(unicastAddrs1, 0, sizeof(H245UnicastAddress));
	unicastAddrs1->t = T_H245UnicastAddress_iPAddress;
	unicastAddrs1->u.iPAddress = (H245UnicastAddress_iPAddress*)
		memAlloc(pctxt, sizeof(H245UnicastAddress_iPAddress));
	iPAddress1 = unicastAddrs1->u.iPAddress;
	memset(iPAddress1, 0, sizeof(H245UnicastAddress_iPAddress));

	chrSocketConvertIpToNwAddr
		(call->mediaInfo->lMediaIP, iPAddress1->network.data, sizeof(iPAddress1->network.data));

	iPAddress1->network.numocts = 4;
	iPAddress1->tsapIdentifier = pLogicalChannel->localRtcpPort;

	CHRTRACEDBG3("Built OpenLogicalChannelAck (%s, %s)\n", call->callType,
		call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue OpenLogicalChannelAck message to "
			"outbound queue. (%s, %s)\n", call->callType,
			call->callToken);
	}
	chrFreeH245Message(call, ph245msg);


	if (epCap->startReceiveChannel)
	{
		epCap->startReceiveChannel(call, pLogicalChannel);
		CHRTRACEINFO6("Receive channel of type %s started at %s:%d(%s, %s)\n",
			chrGetCapTypeText(epCap->cap), call->localIP,
			pLogicalChannel->localRtpPort, call->callType,
			call->callToken);
	}
	else{
		CHRTRACEERR3("ERROR:No callback registered to start receive audio "
			"channel (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	pLogicalChannel->state = CHR_LOGICALCHAN_ESTABLISHED;
	return ret;
}

int chrSendOpenLogicalChannelReject
(CHRH323CallData *call, ASN1UINT channelNum, ASN1UINT cause)
{
	int ret = 0;
	H245ResponseMessage* response = NULL;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	ret = chrCreateH245Message
		(&ph245msg, T_H245MultimediaSystemControlMessage_response);

	if (ret != CHR_OK) {
		CHRTRACEERR3("Error:H245 message creation failed for - OpenLogicalChannel"
			"Reject (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHROpenLogicalChannelReject;
	response = ph245msg->h245Msg.u.response;

	response->t = T_H245ResponseMessage_openLogicalChannelReject;

	response->u.openLogicalChannelReject =
		(H245OpenLogicalChannelReject*)
		memAlloc(pctxt, sizeof(H245OpenLogicalChannelReject));

	if (!response->u.openLogicalChannelReject)
	{
		CHRTRACEERR3("Error: Failed to allocate memory for OpenLogicalChannel"
			"Reject message. (%s, %s)\n", call->callType,
			call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	response->u.openLogicalChannelReject->forwardLogicalChannelNumber =
		channelNum;
	response->u.openLogicalChannelReject->cause.t = cause;

	CHRTRACEDBG3("Built OpenLogicalChannelReject (%s, %s)\n",
		call->callType, call->callToken);

	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK) {
		CHRTRACEERR3
			("Error:Failed to enqueue OpenLogicalChannelReject "
			"message to outbound queue.(%s, %s)\n", call->callType,
			call->callToken);
	}

	chrFreeH245Message(call, ph245msg);

	return ret;
}


int chrOnReceivedOpenLogicalChannelAck(CHRH323CallData *call,
	H245OpenLogicalChannelAck *olcAck)
{
	char remoteip[20];
	CHRLogicalChannel *pLogicalChannel;
	H245H2250LogicalChannelAckParameters *h2250lcap;
	H245UnicastAddress *unicastAddr;
	H245UnicastAddress_iPAddress *iPAddress;
	H245UnicastAddress *unicastAddr1;
	H245UnicastAddress_iPAddress *iPAddress1;

	if (!((olcAck->m.forwardMultiplexAckParametersPresent == 1) &&
		(olcAck->forwardMultiplexAckParameters.t ==
		T_H245OpenLogicalChannelAck_forwardMultiplexAckParameters_h2250LogicalChannelAckParameters)))
	{
		CHRTRACEERR3("Error: Processing open logical channel ack - LogicalChannel"
			"Ack parameters absent (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_OK;  /* should send CloseLogicalChannel request */
	}

	h2250lcap =
		olcAck->forwardMultiplexAckParameters.u.h2250LogicalChannelAckParameters;
	/* Extract media channel address */
	if (h2250lcap->m.mediaChannelPresent != 1)
	{
		CHRTRACEERR3("Error: Processing OpenLogicalChannelAck - media channel "
			"absent (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	if (h2250lcap->mediaChannel.t != T_H245TransportAddress_unicastAddress)
	{
		CHRTRACEERR3("Error: Processing OpenLogicalChannelAck - media channel "
			"address type is not unicast (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}

	unicastAddr = h2250lcap->mediaChannel.u.unicastAddress;
	if (unicastAddr->t != T_H245UnicastAddress_iPAddress)
	{
		CHRTRACEERR3("Error: Processing OpenLogicalChannelAck - media channel "
			"address type is not IP (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	iPAddress = unicastAddr->u.iPAddress;

	sprintf(remoteip, "%d.%d.%d.%d", iPAddress->network.data[0],
		iPAddress->network.data[1],
		iPAddress->network.data[2],
		iPAddress->network.data[3]);

	/* Extract media control channel address */
	if (h2250lcap->m.mediaControlChannelPresent != 1)
	{
		CHRTRACEERR3("Error: Processing OpenLogicalChannelAck - Missing media "
			"control channel (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	if (h2250lcap->mediaControlChannel.t !=
		T_H245TransportAddress_unicastAddress)
	{
		CHRTRACEERR3("Error: Processing OpenLogicalChannelAck - media control "
			"channel addres type is not unicast (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	unicastAddr1 = h2250lcap->mediaControlChannel.u.unicastAddress;
	if (unicastAddr1->t != T_H245UnicastAddress_iPAddress)
	{
		CHRTRACEERR3("Error: Processing OpenLogicalChannelAck - media control "
			"channel address type is not IP (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	iPAddress1 = unicastAddr1->u.iPAddress;

	/* Set remote destination address for rtp session */
	//   strcpy(call->remoteIP, remoteip);

	/* Start channel here */
	pLogicalChannel = chrFindLogicalChannelByLogicalChannelNo(call, olcAck->forwardLogicalChannelNumber);
	if (!pLogicalChannel)
	{
		CHRTRACEERR4("ERROR:Logical channel %d not found in the channel list for "
			"call (%s, %s)\n", olcAck->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	/* Update session id if we were waiting for remote to assign one and remote
	did assign one. */
	if (pLogicalChannel->sessionID == 0 && h2250lcap->m.sessionIDPresent)
		pLogicalChannel->sessionID = h2250lcap->sessionID;

	/* Populate ports &ip  for channel */
	strcpy(pLogicalChannel->remoteIP, remoteip);
	pLogicalChannel->remoteMediaPort = iPAddress->tsapIdentifier;
	pLogicalChannel->remoteMediaControlPort = iPAddress1->tsapIdentifier;

	if (pLogicalChannel->chanCap->startTransmitChannel)
	{
		pLogicalChannel->chanCap->startTransmitChannel(call, pLogicalChannel);
		CHRTRACEINFO4("TransmitLogical Channel of type %s started (%s, %s)\n",
			chrGetCapTypeText(pLogicalChannel->chanCap->cap),
			call->callType, call->callToken);
	}
	else{
		CHRTRACEERR3("ERROR:No callback registered for starting transmit channel "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	pLogicalChannel->state = CHR_LOGICALCHAN_ESTABLISHED;
	return CHR_OK;
}

int chrOnReceivedOpenLogicalChannelRejected(CHRH323CallData *call,
	H245OpenLogicalChannelReject *olcReject)
{
	switch (olcReject->cause.t)
	{
	case T_H245OpenLogicalChannelReject_cause_unspecified:
		CHRTRACEINFO4("Open logical channel %d rejected - unspecified (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber, call->callType,
			call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_unsuitableReverseParameters:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"unsuitableReverseParameters (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber, call->callType,
			call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_dataTypeNotSupported:
		CHRTRACEINFO4("Open logical channel %d rejected - dataTypeNotSupported"
			"(%s, %s)\n", olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_dataTypeNotAvailable:
		CHRTRACEINFO4("Open logical channel %d rejected - dataTypeNotAvailable"
			"(%s, %s)\n", olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_unknownDataType:
		CHRTRACEINFO4("Open logical channel %d rejected - unknownDataType"
			"(%s, %s)\n", olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_dataTypeALCombinationNotSupported:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"dataTypeALCombinationNotSupported(%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_multicastChannelNotAllowed:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"multicastChannelNotAllowed (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_insufficientBandwidth:
		CHRTRACEINFO4("Open logical channel %d rejected - insufficientBandwidth"
			"(%s, %s)\n", olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_separateStackEstablishmentFailed:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"separateStackEstablishmentFailed (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_invalidSessionID:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"invalidSessionID (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_masterSlaveConflict:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"invalidSessionID (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_waitForCommunicationMode:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"waitForCommunicationMode (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_invalidDependentChannel:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"invalidDependentChannel (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	case T_H245OpenLogicalChannelReject_cause_replacementForRejected:
		CHRTRACEINFO4("Open logical channel %d rejected - "
			"replacementForRejected (%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		break;
	default:
		CHRTRACEERR4("Error: OpenLogicalChannel %d rejected - "
			"invalid cause(%s, %s)\n",
			olcReject->forwardLogicalChannelNumber,
			call->callType, call->callToken);
	}
	if (call->callState < CHR_CALL_CLEAR)
	{
		call->callState = CHR_CALL_CLEAR;
		call->callEndReason = CHR_REASON_LOCAL_CLEARED;
	}
	return CHR_OK;
}

/**
* Currently only disconnect end session command is supported.
**/
int chrSendEndSessionCommand(CHRH323CallData *call)
{
	int ret;
	H245CommandMessage * command;
	OOCTXT *pctxt;
	H245Message *ph245msg = NULL;
	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_command);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error: H245 message creation failed for - End Session "
			"Command (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHREndSessionCommand;

	command = ph245msg->h245Msg.u.command;
	pctxt = &gH323ep.msgctxt;
	memset(command, 0, sizeof(H245CommandMessage));
	command->t = T_H245CommandMessage_endSessionCommand;
	command->u.endSessionCommand = (H245EndSessionCommand*)ASN1MALLOC(pctxt,
		sizeof(H245EndSessionCommand));
	memset(command->u.endSessionCommand, 0, sizeof(H245EndSessionCommand));
	command->u.endSessionCommand->t = T_H245EndSessionCommand_disconnect;
	CHRTRACEDBG3("Built EndSession Command (%s, %s)\n", call->callType,
		call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue EndSession message to outbound "
			"queue.(%s, %s)\n", call->callType, call->callToken);
	}
	chrFreeH245Message(call, ph245msg);
	return ret;
}

int chrSendVideoFastUpdateCommand(CHRH323CallData *call)
{
	int ret;
	H245CommandMessage * command;
	OOCTXT *pctxt;
	H245Message *ph245msg = NULL;
	CHRLogicalChannel * pChannel = NULL;
	int videoChannelNo = 0;

	if (!call) return CHR_FAILED;

	pChannel = call->logicalChans;
	while (pChannel) {
		if (pChannel->type == CHR_CAP_TYPE_VIDEO && (!strcmp(pChannel->dir, "receive"))) {
			videoChannelNo = pChannel->channelNo;
		}
		pChannel = pChannel->next;
	}

	if (videoChannelNo == 0) return CHR_OK;

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_command);
	if (ret != CHR_OK) {
		CHRTRACEERR3("Error: H245 message creation failed for - videoFastUpdate "
			"Command (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRH245MSG;

	command = ph245msg->h245Msg.u.command;
	pctxt = &gH323ep.msgctxt;
	memset(command, 0, sizeof(H245CommandMessage));
	command->t = T_H245CommandMessage_miscellaneousCommand;;
	command->u.miscellaneousCommand = (H245MiscellaneousCommand*)ASN1MALLOC(pctxt,
		sizeof(H245MiscellaneousCommand));
	command->u.miscellaneousCommand->logicalChannelNumber = videoChannelNo;
	command->u.miscellaneousCommand->type.t = T_H245MiscellaneousCommand_type_videoFastUpdatePicture;
	CHRTRACEDBG4("Built videoFastUpdate Command channelNo=%d (%s, %s)\n",
		videoChannelNo, call->callType, call->callToken);

	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK) {
		CHRTRACEERR3("Error:Failed to enqueue videoFastUpdate message to outbound "
			"queue.(%s, %s)\n", call->callType, call->callToken);
	}

	chrFreeH245Message(call, ph245msg);

	return ret;
}

int chrH245CommandVideoFastUpdate(CHRH323CallData *call,
	H245CommandMessage *command)
{
	if (!gH323ep.h323Callbacks.onReceivedVideoFastUpdate) return CHR_OK;
	/* TODO maybe we need the logical channel number? hard codec as 0 for now */
	return gH323ep.h323Callbacks.onReceivedVideoFastUpdate(call, 0);
}

int chrHandleH245Command(CHRH323CallData *call,
	H245CommandMessage *command)
{
	ASN1UINT i;
	DListNode *pNode = NULL;
	CHRTimer *pTimer = NULL;
	CHRTRACEDBG3("Handling H.245 command message. (%s, %s)\n", call->callType,
		call->callToken);
	switch (command->t)
	{
	case T_H245CommandMessage_endSessionCommand:
		CHRTRACEINFO3("Received EndSession command (%s, %s)\n",
			call->callType, call->callToken);
		if (call->h245SessionState == CHR_H245SESSION_ENDSENT)
		{
			/* Disable Session timer */
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if (((chrTimerCallback*)pTimer->cbData)->timerType &
					CHR_SESSION_TIMER)
				{
					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted Session Timer. (%s, %s)\n",
						call->callType, call->callToken);
					break;
				}
			}
			chrCloseH245Connection(call);
		}
		else{

			call->h245SessionState = CHR_H245SESSION_ENDRECVD;
#if 0
			if (call->callState < CHR_CALL_CLEAR)
				call->callState = CHR_CALL_CLEAR;
#else
			if (call->logicalChans)
			{
				CHRTRACEINFO3("In response to received EndSessionCommand - "
					"Clearing all logical channels. (%s, %s)\n",
					call->callType, call->callToken);
				chrClearAllLogicalChannels(call);
			}
			chrSendEndSessionCommand(call);
#endif
		}


		break;
	case T_H245CommandMessage_sendTerminalCapabilitySet:
		CHRTRACEWARN3("Warning: Received command Send terminal capability set "
			"- Not handled (%s, %s)\n", call->callType,
			call->callToken);
		break;
	case T_H245CommandMessage_flowControlCommand:
		CHRTRACEWARN3("Warning: Flow control command received - Not handled "
			"(%s, %s)\n", call->callType, call->callToken);
		break;
	case T_H245CommandMessage_miscellaneousCommand:
		CHRTRACEWARN3("Received miscellaneous command - videoFastUpdate "
			"(%s, %s)\n", call->callType, call->callToken);
		chrH245CommandVideoFastUpdate(call, command);
		if (gH323ep.h323Callbacks.onReceivedCommand) {
			gH323ep.h323Callbacks.onReceivedCommand(call, command);
		}
		break;
	default:
		CHRTRACEWARN4("Warning: Unhandled H245 command message [%d] received "
			"(%s, %s)\n", command->t, call->callType, call->callToken);
	}
	CHRTRACEDBG3("Handling H.245 command message done. (%s, %s)\n",
		call->callType, call->callToken);
	return CHR_OK;
}


int chrOnReceivedTerminalCapabilitySetAck(CHRH323CallData* call)
{
	call->localTermCapState = CHR_LocalTermCapSetAckRecvd;
	if (call->remoteTermCapState != CHR_RemoteTermCapSetAckSent)
		return CHR_OK;

	if (call->masterSlaveState == CHR_MasterSlave_Master ||
		call->masterSlaveState == CHR_MasterSlave_Slave)
	{
		if (gH323ep.h323Callbacks.openLogicalChannels)
			gH323ep.h323Callbacks.openLogicalChannels(call);
		else{
			if (!call->logicalChans)
				chrOpenLogicalChannels(call);
		}
#if 0
		if (!call->logicalChans){
			if (!gH323ep.h323Callbacks.openLogicalChannels)
				chrOpenLogicalChannels(call);
			else
				gH323ep.h323Callbacks.openLogicalChannels(call);
		}
#endif
	}

	return CHR_OK;
}

int chrCloseAllLogicalChannels(CHRH323CallData *call)
{
	CHRLogicalChannel *temp;

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
			else{
				chrSendRequestCloseLogicalChannel(call, temp);
			}
		}
		temp = temp->next;
	}
	return CHR_OK;
}

int chrSendCloseLogicalChannel(CHRH323CallData *call, CHRLogicalChannel *logicalChan)
{
	int ret = CHR_OK, error = 0;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt;
	H245RequestMessage *request;
	H245CloseLogicalChannel* clc;

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_request);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Failed to create H245 message for closeLogicalChannel"
			" message (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRCloseLogicalChannel;
	ph245msg->logicalChannelNo = logicalChan->channelNo;
	pctxt = &gH323ep.msgctxt;
	request = ph245msg->h245Msg.u.request;

	request->t = T_H245RequestMessage_closeLogicalChannel;
	request->u.closeLogicalChannel = (H245CloseLogicalChannel*)ASN1MALLOC(pctxt,
		sizeof(H245CloseLogicalChannel));
	if (!request->u.closeLogicalChannel)
	{
		CHRTRACEERR3("ERROR:Memory allocation for CloseLogicalChannel failed "
			"(%s, %s)\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	clc = request->u.closeLogicalChannel;
	memset(clc, 0, sizeof(H245CloseLogicalChannel));

	clc->forwardLogicalChannelNumber = logicalChan->channelNo;
	clc->source.t = T_H245CloseLogicalChannel_source_lcse;
	clc->m.reasonPresent = 1;
	clc->reason.t = T_H245CloseLogicalChannel_reason_unknown;

	CHRTRACEDBG4("Built close logical channel for %d (%s, %s)\n",
		logicalChan->channelNo, call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue CloseLogicalChannel to outbound queue.(%s, %s)\n", call->callType,
			call->callToken);
		error++;
	}
	chrFreeH245Message(call, ph245msg);

	/* Stop the media transmission */
	CHRTRACEINFO4("Closing logical channel %d (%s, %s)\n",
		clc->forwardLogicalChannelNumber, call->callType,
		call->callToken);
	ret = chrClearLogicalChannel(call, clc->forwardLogicalChannelNumber);
	if (ret != CHR_OK)
	{
		CHRTRACEERR4("ERROR:Failed to close logical channel %d (%s, %s)\n",
			clc->forwardLogicalChannelNumber, call->callType, call->callToken);
		return CHR_FAILED;
	}
	if (error) return CHR_FAILED;
	return ret;
}

/*TODO: Need to pass reason as a parameter */
int chrSendRequestCloseLogicalChannel(CHRH323CallData *call,
	CHRLogicalChannel *logicalChan)
{
	int ret = CHR_OK;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt;
	H245RequestMessage *request;
	H245RequestChannelClose *rclc;

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_request);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Failed to create H245 message for "
			"requestCloseLogicalChannel message (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRRequestChannelClose;
	ph245msg->logicalChannelNo = logicalChan->channelNo;
	pctxt = &gH323ep.msgctxt;
	request = ph245msg->h245Msg.u.request;

	request->t = T_H245RequestMessage_requestChannelClose;
	request->u.requestChannelClose = (H245RequestChannelClose*)ASN1MALLOC(pctxt,
		sizeof(H245RequestChannelClose));
	if (!request->u.requestChannelClose)
	{
		CHRTRACEERR3("ERROR:Memory allocation for RequestCloseLogicalChannel "
			" failed (%s, %s)\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}

	rclc = request->u.requestChannelClose;
	memset(rclc, 0, sizeof(H245RequestChannelClose));
	rclc->forwardLogicalChannelNumber = logicalChan->channelNo;

	rclc->m.reasonPresent = 1;
	rclc->reason.t = T_H245RequestChannelClose_reason_unknown;

	CHRTRACEDBG4("Built RequestCloseChannel for %d (%s, %s)\n",
		logicalChan->channelNo, call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue the RequestCloseChannel to outbound"
			" queue (%s, %s)\n", call->callType,
			call->callToken);
	}
	chrFreeH245Message(call, ph245msg);

	return ret;
}

int chrSendRequestChannelCloseRelease(CHRH323CallData *call, int channelNum)
{
	int ret = CHR_OK;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt;
	H245IndicationMessage *indication;

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_indication);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Failed to create H245 message for "
			"RequestChannelCloseRelease message (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRRequestChannelCloseRelease;
	ph245msg->logicalChannelNo = channelNum;
	pctxt = &gH323ep.msgctxt;
	indication = ph245msg->h245Msg.u.indication;
	indication->t = T_H245IndicationMessage_requestChannelCloseRelease;
	indication->u.requestChannelCloseRelease = (H245RequestChannelCloseRelease*)
		ASN1MALLOC(pctxt, sizeof(H245RequestChannelCloseRelease));
	if (!indication->u.requestChannelCloseRelease)
	{
		CHRTRACEERR3("Error:Failed to allocate memory for "
			"RequestChannelCloseRelease message. (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
	}

	indication->u.requestChannelCloseRelease->forwardLogicalChannelNumber =
		channelNum;

	CHRTRACEDBG4("Built RequestChannelCloseRelease for %d (%s, %s)\n",
		channelNum, call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue the RequestChannelCloseRelease to "
			"outbound queue (%s, %s)\n", call->callType, call->callToken);
	}
	chrFreeH245Message(call, ph245msg);

	return ret;
}



int chrOnReceivedRequestChannelClose(CHRH323CallData *call,
	H245RequestChannelClose *rclc)
{
	int ret = 0, error = 0;
	H245Message *ph245msg = NULL;
	H245ResponseMessage *response = NULL;
	OOCTXT *pctxt = NULL;
	H245RequestChannelCloseAck *rclcAck;
	CHRLogicalChannel * lChannel = NULL;
	/* Send Ack: TODO: Need to send reject, if doesn't exist
	*/
	lChannel = chrFindLogicalChannelByLogicalChannelNo(call,
		rclc->forwardLogicalChannelNumber);
	if (!lChannel)
	{
		CHRTRACEERR4("ERROR:Channel %d requested to be closed not found "
			"(%s, %s)\n", rclc->forwardLogicalChannelNumber,
			call->callType, call->callToken);
		return CHR_FAILED;
	}
	else{
		if (strcmp(lChannel->dir, "transmit"))
		{
			CHRTRACEERR4("ERROR:Channel %d requested to be closed, Not a forward "
				"channel (%s, %s)\n", rclc->forwardLogicalChannelNumber,
				call->callType, call->callToken);
			return CHR_FAILED;
		}
	}
	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_response);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Memory allocation for RequestChannelCloseAck message "
			"failed (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	pctxt = &gH323ep.msgctxt;
	ph245msg->msgType = CHRRequestChannelCloseAck;
	ph245msg->logicalChannelNo = rclc->forwardLogicalChannelNumber;
	response = ph245msg->h245Msg.u.response;
	response->t = T_H245ResponseMessage_requestChannelCloseAck;
	response->u.requestChannelCloseAck = (H245RequestChannelCloseAck*)ASN1MALLOC
		(pctxt, sizeof(H245RequestChannelCloseAck));
	if (!response->u.requestChannelCloseAck)
	{
		CHRTRACEERR3("ERROR:Failed to allocate memory for RequestChannelCloseAck "
			"message (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	rclcAck = response->u.requestChannelCloseAck;
	memset(rclcAck, 0, sizeof(H245RequestChannelCloseAck));
	rclcAck->forwardLogicalChannelNumber = rclc->forwardLogicalChannelNumber;

	CHRTRACEDBG3("Built RequestCloseChannelAck message (%s, %s)\n",
		call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue RequestCloseChannelAck to outbound queue. (%s, %s)\n", call->callType,
			call->callToken);
		error++;
	}

	chrFreeH245Message(call, ph245msg);

	/* Send Close Logical Channel*/
	ret = chrSendCloseLogicalChannel(call, lChannel);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Failed to build CloseLgicalChannel message(%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	if (error) return CHR_FAILED;

	return ret;
}

int chrOnReceivedRoundTripDelayRequest(CHRH323CallData *call,
	H245SequenceNumber sequenceNumber)
{
	int ret = 0;
	H245Message *ph245msg = NULL;
	H245ResponseMessage *response = NULL;
	OOCTXT *pctxt = NULL;
	H245RoundTripDelayResponse *rtdr;

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_response);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Memory allocation for RoundTripDelayResponse message "
			"failed (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	pctxt = &gH323ep.msgctxt;
	ph245msg->msgType = CHRRequestDelayResponse;
	response = ph245msg->h245Msg.u.response;
	response->t = T_H245ResponseMessage_roundTripDelayResponse;
	response->u.roundTripDelayResponse = (H245RoundTripDelayResponse *)ASN1MALLOC
		(pctxt, sizeof(H245RoundTripDelayResponse));
	if (!response->u.roundTripDelayResponse)
	{
		CHRTRACEERR3("ERROR:Failed to allocate memory for H245RoundTripDelayResponse "
			"message (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	rtdr = response->u.roundTripDelayResponse;
	memset(rtdr, 0, sizeof(H245RoundTripDelayResponse));
	rtdr->sequenceNumber = sequenceNumber;

	CHRTRACEDBG3("Built RoundTripDelayResponse message (%s, %s)\n",
		call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue RoundTripDelayResponse to outbound queue. (%s, %s)\n",
			call->callType, call->callToken);
	}

	chrFreeH245Message(call, ph245msg);

	return ret;
}

/*
We clear channel here. Ideally the remote endpoint should send
CloseLogicalChannel and then the channel should be cleared. But there's no
timer for this and if remote endpoint misbehaves, the call will keep waiting
for CloseLogicalChannel and hence, wouldn't be cleared. In case when remote
endpoint sends CloseLogicalChannel, we call chrClearLogicalChannel again,
which simply returns CHR_OK as channel was already cleared. Other option is
to start a timer for call cleanup and if call is not cleaned up within
timeout, we clean call forcefully. Note, no such timer is defined in
standards.
*/
int chrOnReceivedRequestChannelCloseAck
(CHRH323CallData *call, H245RequestChannelCloseAck *rccAck)
{
	int ret = CHR_OK;
	/* Remote endpoint is ok to close channel. So let's do it */
	ret = chrClearLogicalChannel(call, rccAck->forwardLogicalChannelNumber);
	if (ret != CHR_OK)
	{
		CHRTRACEERR4("Error:Failed to clear logical channel %d. (%s, %s)\n",
			rccAck->forwardLogicalChannelNumber, call->callType,
			call->callToken);
	}

	return ret;
}

int chrOnReceivedRequestChannelCloseReject
(CHRH323CallData *call, H245RequestChannelCloseReject *rccReject)
{
	int ret = 0;
	switch (rccReject->cause.t)
	{
	case T_H245RequestChannelCloseReject_cause_unspecified:
		CHRTRACEDBG4("Remote endpoint has rejected request to close logical "
			"channel %d - cause unspecified. (%s, %s)\n",
			rccReject->forwardLogicalChannelNumber, call->callType,
			call->callToken);
		break;
	case T_H245RequestChannelCloseReject_cause_extElem1:
		CHRTRACEDBG4("Remote endpoint has rejected request to close logical "
			"channel %d - cause propriatory. (%s, %s)\n",
			rccReject->forwardLogicalChannelNumber, call->callType,
			call->callToken);
		break;
	default:
		CHRTRACEDBG4("Remote endpoint has rejected request to close logical "
			"channel %d - cause INVALID. (%s, %s)\n",
			rccReject->forwardLogicalChannelNumber, call->callType,
			call->callToken);
	}
	CHRTRACEDBG4("Clearing logical channel %d. (%s, %s)\n",
		rccReject->forwardLogicalChannelNumber, call->callType,
		call->callToken);
	ret = chrClearLogicalChannel(call, rccReject->forwardLogicalChannelNumber);
	if (ret != CHR_OK)
	{
		CHRTRACEERR4("Error: failed to clear logical channel %d.(%s, %s)\n",
			rccReject->forwardLogicalChannelNumber, call->callType,
			call->callToken);
	}
	return ret;
}

/****/
int chrOnReceivedCloseLogicalChannel(CHRH323CallData *call,
	H245CloseLogicalChannel* clc)
{
	int ret = 0;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt = NULL;
	H245CloseLogicalChannelAck * clcAck;
	H245ResponseMessage *response;

	CHRTRACEINFO4("Closing logical channel number %d (%s, %s)\n",
		clc->forwardLogicalChannelNumber, call->callType, call->callToken);

	ret = chrClearLogicalChannel(call, clc->forwardLogicalChannelNumber);
	if (ret != CHR_OK)
	{
		CHRTRACEERR4("ERROR:Failed to close logical channel %d (%s, %s)\n",
			clc->forwardLogicalChannelNumber, call->callType, call->callToken);
		return CHR_FAILED;
	}

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_response);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR:Failed to create H245 message for "
			"closeLogicalChannelAck (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	pctxt = &gH323ep.msgctxt;
	ph245msg->msgType = CHRCloseLogicalChannelAck;
	ph245msg->logicalChannelNo = clc->forwardLogicalChannelNumber;
	response = ph245msg->h245Msg.u.response;
	response->t = T_H245ResponseMessage_closeLogicalChannelAck;
	response->u.closeLogicalChannelAck = (H245CloseLogicalChannelAck*)
		ASN1MALLOC(pctxt, sizeof(H245CloseLogicalChannelAck));
	clcAck = response->u.closeLogicalChannelAck;
	if (!clcAck)
	{
		CHRTRACEERR3("ERROR:Failed to allocate memory for closeLogicalChannelAck "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_OK;
	}
	memset(clcAck, 0, sizeof(H245CloseLogicalChannelAck));
	clcAck->forwardLogicalChannelNumber = clc->forwardLogicalChannelNumber;

	CHRTRACEDBG3("Built CloseLogicalChannelAck message (%s, %s)\n",
		call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue CloseLogicalChannelAck message to "
			"outbound queue.(%s, %s)\n", call->callType, call->callToken);
	}

	chrFreeH245Message(call, ph245msg);
	return ret;
}

int chrOnReceivedCloseChannelAck(CHRH323CallData* call,
	H245CloseLogicalChannelAck* clcAck)
{
	int ret = CHR_OK;
	return ret;
}

int chrHandleH245Message(CHRH323CallData *call, H245Message * pmsg)
{
	ASN1UINT i;
	DListNode *pNode = NULL;
	CHRTimer *pTimer = NULL;
	H245Message *pH245 = (H245Message*)pmsg;
	/* There are four major types of H.245 messages that can be received.
	Request/Response/Command/Indication. Each one of them need to be
	handled separately.
	*/
	H245RequestMessage *request = NULL;
	H245ResponseMessage *response = NULL;
	H245CommandMessage *command = NULL;
	H245IndicationMessage *indication = NULL;

	CHRTRACEDBG3("Handling H245 message. (%s, %s)\n", call->callType,
		call->callToken);

	switch (pH245->h245Msg.t)
	{
		/* H.245 Request message is received */
	case (T_H245MultimediaSystemControlMessage_request) :
		request = pH245->h245Msg.u.request;
		switch (request->t)
		{
		case T_H245RequestMessage_terminalCapabilitySet:
			/* If session isn't marked active yet, do it. possible in case of
			tunneling */
			if (call->h245SessionState == CHR_H245SESSION_IDLE)
				call->h245SessionState = CHR_H245SESSION_ACTIVE;

			chrOnReceivedTerminalCapabilitySet(call, pH245);
			if (call->localTermCapState == CHR_LocalTermCapExchange_Idle)
				chrSendTermCapMsg(call);
			break;
		case T_H245RequestMessage_masterSlaveDetermination:
			chrHandleMasterSlave(call,
				request->u.masterSlaveDetermination,
				CHRMasterSlaveDetermination);
			break;
		case T_H245RequestMessage_openLogicalChannel:
			chrHandleOpenLogicalChannel(call,
				request->u.openLogicalChannel);
			break;
		case T_H245RequestMessage_closeLogicalChannel:
			CHRTRACEINFO4("Received close logical Channel - %d (%s, %s)\n",
				request->u.closeLogicalChannel->forwardLogicalChannelNumber,
				call->callType, call->callToken);
			chrOnReceivedCloseLogicalChannel(call,
				request->u.closeLogicalChannel);
			break;
		case T_H245RequestMessage_requestChannelClose:
			CHRTRACEINFO4("Received RequestChannelClose - %d (%s, %s)\n",
				request->u.requestChannelClose->forwardLogicalChannelNumber,
				call->callType, call->callToken);
			chrOnReceivedRequestChannelClose(call,
				request->u.requestChannelClose);
			break;
		case T_H245RequestMessage_roundTripDelayRequest:
			chrOnReceivedRoundTripDelayRequest(call, request->u.roundTripDelayRequest->sequenceNumber);
			break;
		default:
			;
		} /* End of Request Message */
		break;
		/* H.245 Response message is received */
	case (T_H245MultimediaSystemControlMessage_response) :
		response = pH245->h245Msg.u.response;
		switch (response->t)
		{
		case T_H245ResponseMessage_masterSlaveDeterminationAck:
			/* Disable MSD timer */
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if (((chrTimerCallback*)pTimer->cbData)->timerType & CHR_MSD_TIMER)
				{
					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted MSD Timer. (%s, %s)\n", call->callType,
						call->callToken);
					break;
				}
			}

			chrHandleMasterSlave(call,
				response->u.masterSlaveDeterminationAck,
				CHRMasterSlaveAck);
			break;
		case T_H245ResponseMessage_masterSlaveDeterminationReject:
			/* Disable MSD timer */
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if (((chrTimerCallback*)pTimer->cbData)->timerType & CHR_MSD_TIMER)
				{
					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted MSD Timer. (%s, %s)\n", call->callType,
						call->callToken);
					break;
				}
			}
			chrHandleMasterSlaveReject(call,
				response->u.masterSlaveDeterminationReject);
			break;
		case T_H245ResponseMessage_terminalCapabilitySetAck:
			/* Disable TCS timer */
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if (((chrTimerCallback*)pTimer->cbData)->timerType & CHR_TCS_TIMER)
				{
					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted TCS Timer. (%s, %s)\n", call->callType,
						call->callToken);
					break;
				}
			}
			chrOnReceivedTerminalCapabilitySetAck(call);
			break;
		case T_H245ResponseMessage_terminalCapabilitySetReject:
			CHRTRACEINFO3("TerminalCapabilitySetReject message received."
				" (%s, %s)\n", call->callType, call->callToken);
			if (response->u.terminalCapabilitySetReject->sequenceNumber !=
				call->localTermCapSeqNo)
			{
				CHRTRACEINFO5("Ignoring TCSReject with mismatched seqno %d "
					"(local - %d). (%s, %s)\n",
					response->u.terminalCapabilitySetReject->sequenceNumber,
					call->localTermCapSeqNo, call->callType, call->callToken);
				break;
			}
			/* Disable TCS timer */
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if (((chrTimerCallback*)pTimer->cbData)->timerType & CHR_TCS_TIMER)
				{
					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted TCS Timer. (%s, %s)\n", call->callType,
						call->callToken);
					break;
				}
			}
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callState = CHR_CALL_CLEAR;
				call->callEndReason = CHR_REASON_NOCOMMON_CAPABILITIES;
			}
			break;
		case T_H245ResponseMessage_openLogicalChannelAck:
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if ((((chrTimerCallback*)pTimer->cbData)->timerType & CHR_OLC_TIMER) &&
					((chrTimerCallback*)pTimer->cbData)->channelNumber ==
					response->u.openLogicalChannelAck->forwardLogicalChannelNumber)
				{

					memFreePtr(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted OpenLogicalChannel Timer. (%s, %s)\n",
						call->callType, call->callToken);
					break;
				}
			}
			chrOnReceivedOpenLogicalChannelAck(call,
				response->u.openLogicalChannelAck);
			break;
		case T_H245ResponseMessage_openLogicalChannelReject:
			CHRTRACEINFO3("Open Logical Channel Reject received (%s, %s)\n",
				call->callType, call->callToken);
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if ((((chrTimerCallback*)pTimer->cbData)->timerType & CHR_OLC_TIMER) &&
					((chrTimerCallback*)pTimer->cbData)->channelNumber ==
					response->u.openLogicalChannelAck->forwardLogicalChannelNumber)
				{

					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted OpenLogicalChannel Timer. (%s, %s)\n",
						call->callType, call->callToken);
					break;
				}
			}
			chrOnReceivedOpenLogicalChannelRejected(call,
				response->u.openLogicalChannelReject);
			break;
		case T_H245ResponseMessage_closeLogicalChannelAck:
			CHRTRACEINFO4("CloseLogicalChannelAck received for %d (%s, %s)\n",
				response->u.closeLogicalChannelAck->forwardLogicalChannelNumber,
				call->callType, call->callToken);
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if ((((chrTimerCallback*)pTimer->cbData)->timerType & CHR_CLC_TIMER) &&
					((chrTimerCallback*)pTimer->cbData)->channelNumber ==
					response->u.closeLogicalChannelAck->forwardLogicalChannelNumber)
				{

					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted CloseLogicalChannel Timer. (%s, %s)\n",
						call->callType, call->callToken);
					break;
				}
			}
			chrOnReceivedCloseChannelAck(call,
				response->u.closeLogicalChannelAck);
			break;
		case T_H245ResponseMessage_requestChannelCloseAck:
			CHRTRACEINFO4("RequestChannelCloseAck received - %d (%s, %s)\n",
				response->u.requestChannelCloseAck->forwardLogicalChannelNumber,
				call->callType, call->callToken);
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if ((((chrTimerCallback*)pTimer->cbData)->timerType & CHR_RCC_TIMER) &&
					((chrTimerCallback*)pTimer->cbData)->channelNumber ==
					response->u.requestChannelCloseAck->forwardLogicalChannelNumber)
				{

					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted RequestChannelClose Timer. (%s, %s)\n",
						call->callType, call->callToken);
					break;
				}
			}
			chrOnReceivedRequestChannelCloseAck(call,
				response->u.requestChannelCloseAck);
			break;
		case T_H245ResponseMessage_requestChannelCloseReject:
			CHRTRACEINFO4("RequestChannelCloseReject received - %d (%s, %s)\n",
				response->u.requestChannelCloseReject->forwardLogicalChannelNumber,
				call->callType, call->callToken);
			for (i = 0; i<call->timerList.count; i++)
			{
				pNode = dListFindByIndex(&call->timerList, i);
				pTimer = (CHRTimer*)pNode->data;
				if ((((chrTimerCallback*)pTimer->cbData)->timerType & CHR_RCC_TIMER) &&
					((chrTimerCallback*)pTimer->cbData)->channelNumber ==
					response->u.requestChannelCloseReject->forwardLogicalChannelNumber)
				{

					ASN1MEMFREEPTR(call->pctxt, pTimer->cbData);
					chrTimerDelete(call->pctxt, &call->timerList, pTimer);
					CHRTRACEDBG3("Deleted RequestChannelClose Timer. (%s, %s)\n",
						call->callType, call->callToken);
					break;
				}
			}
			chrOnReceivedRequestChannelCloseReject(call,
				response->u.requestChannelCloseReject);
			break;
		default:
			;
		}
		break;
		/* H.245 command message is received */
	case (T_H245MultimediaSystemControlMessage_command) :
		command = pH245->h245Msg.u.command;
		chrHandleH245Command(call, command);
		break;
	default:
		;
	}
	CHRTRACEDBG3("Finished handling H245 message. (%s, %s)\n",
		call->callType, call->callToken);
	return CHR_OK;
}


int chrOnReceivedTerminalCapabilitySet(CHRH323CallData *call, H245Message *pmsg)
{
	int ret = 0, k;
	H245TerminalCapabilitySet *tcs = NULL;
	DListNode *pNode = NULL;
	H245CapabilityTableEntry *capEntry = NULL;

	tcs = pmsg->h245Msg.u.request->u.terminalCapabilitySet;
	if (call->remoteTermCapSeqNo >= tcs->sequenceNumber &&
		call->remoteTermCapSeqNo != 0)
	{
		CHRTRACEINFO4("Rejecting TermCapSet message with SeqNo %d, as already "
			"acknowledged message with this SeqNo (%s, %s)\n",
			call->remoteTermCapSeqNo, call->callType, call->callToken);
		chrSendTerminalCapabilitySetReject(call, tcs->sequenceNumber,
			T_H245TerminalCapabilitySetReject_cause_unspecified);
		return CHR_OK;
	}

	if (!tcs->m.capabilityTablePresent)
	{
		// CHRTRACEWARN3("Warn:Ignoring TCS as no capability table present(%s, %s)\n",
		CHRTRACEWARN3("Empty TCS found.  Pausing call...(%s, %s)\n",
			call->callType, call->callToken);
		call->h245SessionState = CHR_H245SESSION_PAUSED;
		//chrSendTerminalCapabilitySetReject(call, tcs->sequenceNumber,
		//                   T_H245TerminalCapabilitySetReject_cause_unspecified);
		//return CHR_OK;
	}
	call->remoteTermCapSeqNo = tcs->sequenceNumber;

	if (tcs->m.capabilityTablePresent) {
		for (k = 0; k<(int)tcs->capabilityTable.count; k++)
		{
			pNode = dListFindByIndex(&tcs->capabilityTable, k);
			if (pNode)
			{
				CHRTRACEDBG4("Processing CapabilityTable Entry %d (%s, %s)\n",
					k, call->callType, call->callToken);
				capEntry = (H245CapabilityTableEntry*)pNode->data;
				if (capEntry->m.capabilityPresent){
					ret = chrAddRemoteCapability(call, &capEntry->capability);
					if (ret != CHR_OK)
					{
						CHRTRACEERR4("Error:Failed to process remote capability in "
							"capability table at index %d. (%s, %s)\n",
							k, call->callType, call->callToken);
					}
					chrCapabilityUpdateJointCapabilities(call, &capEntry->capability);
				}
			}
			pNode = NULL;
			capEntry = NULL;
		}
	}


	/* Update remoteTermCapSetState */
	call->remoteTermCapState = CHR_RemoteTermCapSetRecvd;

	chrH245AcknowledgeTerminalCapabilitySet(call);

	/* If we haven't yet send TCS then send it now */
	if (call->localTermCapState == CHR_LocalTermCapExchange_Idle)
	{
		ret = chrSendTermCapMsg(call);
		if (ret != CHR_OK)
		{
			CHRTRACEERR3("ERROR:Sending Terminal capability message (%s, %s)\n",
				call->callType, call->callToken);
			return ret;
		}
	}

	if (call->remoteTermCapState != CHR_RemoteTermCapSetAckSent ||
		call->localTermCapState != CHR_LocalTermCapSetAckRecvd)
		return CHR_OK;

	/* Check MasterSlave procedure has finished */
	if (call->masterSlaveState != CHR_MasterSlave_Master &&
		call->masterSlaveState != CHR_MasterSlave_Slave)
		return CHR_OK;

	/* As both MasterSlave and TerminalCapabilitySet procedures have finished,
	OpenLogicalChannels */

	if (gH323ep.h323Callbacks.openLogicalChannels)
		gH323ep.h323Callbacks.openLogicalChannels(call);
	else{
		if (!call->logicalChans)
			chrOpenLogicalChannels(call);
	}
#if 0
	if (!call->logicalChans){
		if (!gH323ep.h323Callbacks.openLogicalChannels)
			ret = chrOpenLogicalChannels(call);
		else
			gH323ep.h323Callbacks.openLogicalChannels(call);
	}
#endif
	return CHR_OK;
}

int chrSendTerminalCapabilitySetReject
(CHRH323CallData *call, int seqNo, ASN1UINT cause)
{
	H245Message *ph245msg = NULL;
	H245ResponseMessage * response = NULL;
	OOCTXT *pctxt = NULL;
	int ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_response);
	if (ret != CHR_OK)
	{
		CHRTRACEERR1("ERROR:H245 message creation failed for - "
			"TerminalCapabilitySetReject\n");
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRTerminalCapabilitySetReject;
	response = ph245msg->h245Msg.u.response;
	memset(response, 0, sizeof(H245ResponseMessage));
	pctxt = &gH323ep.msgctxt;
	response->t = T_H245ResponseMessage_terminalCapabilitySetReject;

	response->u.terminalCapabilitySetReject = (H245TerminalCapabilitySetReject*)
		ASN1MALLOC(pctxt, sizeof(H245TerminalCapabilitySetReject));

	memset(response->u.terminalCapabilitySetReject, 0,
		sizeof(H245TerminalCapabilitySetReject));
	response->u.terminalCapabilitySetReject->sequenceNumber = seqNo;
	response->u.terminalCapabilitySetReject->cause.t = cause;

	CHRTRACEDBG3("Built TerminalCapabilitySetReject (%s, %s)\n",
		call->callType, call->callToken);

	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue TCSReject to outbound queue. "
			"(%s, %s)\n", call->callType, call->callToken);
	}
	else
		call->remoteTermCapState = CHR_RemoteTermCapExchange_Idle;

	chrFreeH245Message(call, ph245msg);
	return ret;
}

int chrH245AcknowledgeTerminalCapabilitySet(CHRH323CallData *call)
{
	H245Message *ph245msg = NULL;
	H245ResponseMessage * response = NULL;
	OOCTXT *pctxt = NULL;
	int ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_response);
	if (ret != CHR_OK)
	{
		CHRTRACEERR1("ERROR:H245 message creation failed for - "
			"TerminalCapability Set Ack\n");
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRTerminalCapabilitySetAck;
	response = ph245msg->h245Msg.u.response;
	memset(response, 0, sizeof(H245ResponseMessage));
	pctxt = &gH323ep.msgctxt;
	response->t = T_H245ResponseMessage_terminalCapabilitySetAck;

	response->u.terminalCapabilitySetAck = (H245TerminalCapabilitySetAck*)
		ASN1MALLOC(pctxt, sizeof(H245TerminalCapabilitySetAck));

	memset(response->u.terminalCapabilitySetAck, 0,
		sizeof(H245TerminalCapabilitySetAck));
	response->u.terminalCapabilitySetAck->sequenceNumber = call->remoteTermCapSeqNo;

	CHRTRACEDBG3("Built TerminalCapabilitySet Ack (%s, %s)\n",
		call->callType, call->callToken);
	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue TCSAck to outbound queue. (%s, %s)\n", call->callType, call->callToken);
	}
	else
		call->remoteTermCapState = CHR_RemoteTermCapSetAckSent;

	chrFreeH245Message(call, ph245msg);
	return ret;
}


int chrSendTerminalCapabilitySetRelease(CHRH323CallData * call)
{
	int ret = 0;
	H245IndicationMessage* indication = NULL;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	ret = chrCreateH245Message
		(&ph245msg, T_H245MultimediaSystemControlMessage_indication);

	if (ret != CHR_OK) {
		CHRTRACEERR3("Error:H245 message creation failed for - Terminal"
			"CapabilitySetRelease (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRTerminalCapabilitySetRelease;
	indication = ph245msg->h245Msg.u.indication;

	indication->t = T_H245IndicationMessage_terminalCapabilitySetRelease;

	indication->u.terminalCapabilitySetRelease =
		(H245TerminalCapabilitySetRelease*)
		memAlloc(pctxt, sizeof(H245TerminalCapabilitySetRelease));

	if (!indication->u.terminalCapabilitySetRelease)
	{
		CHRTRACEERR3("Error: Failed to allocate memory for TCSRelease message."
			" (%s, %s)\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	CHRTRACEDBG3("Built TerminalCapabilitySetRelease (%s, %s)\n",
		call->callType, call->callToken);

	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK) {
		CHRTRACEERR3
			("Error:Failed to enqueue TerminalCapabilitySetRelease "
			"message to outbound queue.(%s, %s)\n", call->callType,
			call->callToken);
	}

	chrFreeH245Message(call, ph245msg);
	return ret;
}


int chrSendH245UserInputIndication_alphanumeric
(CHRH323CallData *call, const char *data)
{
	int ret = 0;
	H245IndicationMessage* indication = NULL;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	ret = chrCreateH245Message
		(&ph245msg, T_H245MultimediaSystemControlMessage_indication);

	if (ret != CHR_OK) {
		CHRTRACEERR3("Error:H245 message creation failed for - H245UserInput"
			"Indication_alphanumeric (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRUserInputIndication;
	indication = ph245msg->h245Msg.u.indication;

	indication->t = T_H245IndicationMessage_userInput;
	indication->u.userInput =
		(H245UserInputIndication*)
		memAllocZ(pctxt, sizeof(H245UserInputIndication));

	if (!indication->u.userInput)
	{
		CHRTRACEERR3("Error: Memory - chrH245UserInputIndication_alphanumeric - "
			" userInput (%s, %s)\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	indication->u.userInput->t = T_H245UserInputIndication_alphanumeric;
	indication->u.userInput->u.alphanumeric = (ASN1GeneralString)
		memAlloc(pctxt, strlen(data) + 1);
	if (!indication->u.userInput->u.alphanumeric)
	{
		CHRTRACEERR3("Error: Memory - chrH245UserInputIndication-alphanumeric - "
			"alphanumeric (%s, %s).\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	strcpy((char*)indication->u.userInput->u.alphanumeric, data);
	CHRTRACEDBG3("Built UserInputIndication_alphanumeric (%s, %s)\n",
		call->callType, call->callToken);

	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK) {
		CHRTRACEERR3
			("Error:Failed to enqueue UserInputIndication_alphanumeric "
			"message to outbound queue.(%s, %s)\n", call->callType,
			call->callToken);
	}

	chrFreeH245Message(call, ph245msg);
	return ret;
}

int chrSendH245UserInputIndication_signal
(CHRH323CallData *call, const char *data)
{
	int ret = 0;
	H245IndicationMessage* indication = NULL;
	H245Message *ph245msg = NULL;
	OOCTXT *pctxt = &gH323ep.msgctxt;

	ret = chrCreateH245Message
		(&ph245msg, T_H245MultimediaSystemControlMessage_indication);

	if (ret != CHR_OK) {
		CHRTRACEERR3("Error:H245 message creation failed for - H245UserInput"
			"Indication_signal (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	ph245msg->msgType = CHRUserInputIndication;
	indication = ph245msg->h245Msg.u.indication;

	indication->t = T_H245IndicationMessage_userInput;
	indication->u.userInput =
		(H245UserInputIndication*)
		memAllocZ(pctxt, sizeof(H245UserInputIndication));

	if (!indication->u.userInput)
	{
		CHRTRACEERR3("Error: Memory - chrH245UserInputIndication_signal - "
			" userInput (%s, %s)\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	indication->u.userInput->t = T_H245UserInputIndication_signal;
	indication->u.userInput->u.signal = (H245UserInputIndication_signal*)
		memAllocZ(pctxt, sizeof(H245UserInputIndication_signal));
	indication->u.userInput->u.signal->signalType = (ASN1IA5String)
		memAlloc(pctxt, strlen(data) + 1);
	if (!indication->u.userInput->u.signal ||
		!indication->u.userInput->u.signal->signalType)
	{
		CHRTRACEERR3("Error: Memory - chrH245UserInputIndication_signal - "
			"signal (%s, %s).\n", call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	strcpy((char*)indication->u.userInput->u.signal->signalType, data);
	CHRTRACEDBG3("Built UserInputIndication_signal (%s, %s)\n",
		call->callType, call->callToken);

	ret = chrSendH245Msg(call, ph245msg);

	if (ret != CHR_OK) {
		CHRTRACEERR3
			("Error:Failed to enqueue UserInputIndication_signal "
			"message to outbound queue.(%s, %s)\n", call->callType,
			call->callToken);
	}

	chrFreeH245Message(call, ph245msg);
	return ret;
}


int chrOpenLogicalChannels(CHRH323CallData *call)
{
	int ret = 0;
	CHRTRACEINFO3("Opening logical channels (%s, %s)\n", call->callType,
		call->callToken);

	/* Audio channels */
	if (gH323ep.callMode == CHR_CALLMODE_AUDIOCALL ||
		gH323ep.callMode == CHR_CALLMODE_AUDIOTX)
	{
		//if (!CHR_TESTFLAG (call->flags, CHR_M_AUDIOSESSION))
		//{
		ret = chrOpenLogicalChannel(call, CHR_CAP_TYPE_AUDIO);
		if (ret != CHR_OK)
		{
			CHRTRACEERR3("ERROR:Failed to open audio channels. Clearing call."
				"(%s, %s)\n", call->callType, call->callToken);
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_LOCAL_CLEARED;
				call->callState = CHR_CALL_CLEAR;
			}
			return ret;
		}
		// }
	}

	if (gH323ep.callMode == CHR_CALLMODE_VIDEOCALL)
	{
		/*      if (!CHR_TESTFLAG (call->flags, CHR_M_AUDIOSESSION))
		{*/
		ret = chrOpenLogicalChannel(call, CHR_CAP_TYPE_AUDIO);
		if (ret != CHR_OK)
		{
			CHRTRACEERR3("ERROR:Failed to open audio channel. Clearing call."
				"(%s, %s)\n", call->callType, call->callToken);
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_LOCAL_CLEARED;
				call->callState = CHR_CALL_CLEAR;
			}
			return ret;
		}
		//}
		/*      if(!CHR_TESTFLAG(call->flags, CHR_M_VIDEOSESSION))
		{*/
		ret = chrOpenLogicalChannel(call, CHR_CAP_TYPE_VIDEO);
		if (ret != CHR_OK)
		{
			CHRTRACEERR3("ERROR:Failed to open video channel. Clearing call."
				"(%s, %s)\n", call->callType, call->callToken);
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_LOCAL_CLEARED;
				call->callState = CHR_CALL_CLEAR;
			}
			return ret;
		}
		//}
	}
	return CHR_OK;
}

/* CapType indicates whether to Open Audio or Video channel */
int chrOpenLogicalChannel(CHRH323CallData *call, enum CHRCapType capType)
{
	chrH323EpCapability *epCap = NULL;
	int k = 0;

	/* Check whether local endpoint has audio capability */
	if (gH323ep.myCaps == 0 && call->ourCaps == 0)
	{
		CHRTRACEERR3("ERROR:Local endpoint does not have any audio capabilities"
			" (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	/* Go through local endpoints capabilities sequentially, and find out the
	first one which has a match in the remote endpoints receive capabilities.
	*/
	CHRTRACEINFO3("Lchrking for matching capabilities. (%s, %s)\n",
		call->callType, call->callToken);
	if (call->masterSlaveState == CHR_MasterSlave_Master)
	{
		for (k = 0; k<call->capPrefs.index; k++)
		{
			/*Search for audio caps only */
			if (capType == CHR_CAP_TYPE_AUDIO &&
				call->capPrefs.order[k] > CHR_CAP_VIDEO_BASE)
				continue;
			/* Search for video caps only */
			if (capType == CHR_CAP_TYPE_VIDEO &&
				call->capPrefs.order[k] <= CHR_CAP_VIDEO_BASE)
				continue;

			epCap = call->jointCaps;

			while (epCap){
				if (epCap->cap == call->capPrefs.order[k] && (epCap->dir & CHRTX))
					break;
				epCap = epCap->next;
			}
			if (!epCap)
			{
				CHRTRACEDBG4("Prefereed capability %d is not a local transmit "
					"capability(%s, %s)\n", call->capPrefs.order[k],
					call->callType, call->callToken);
				continue;
			}
			break;
		}
		if (!epCap)
		{
			CHRTRACEERR4("ERROR:Incompatible capabilities - Can not open "
				"%s channel (%s, %s)\n",
				(capType == CHR_CAP_TYPE_AUDIO) ? "audio" : "video", call->callType,
				call->callToken);
			return CHR_FAILED;
		}

	}
	else if (call->masterSlaveState == CHR_MasterSlave_Slave)
	{
		epCap = call->jointCaps;

		while (epCap){
			if (epCap->capType == capType && epCap->dir & CHRTX) { break; }
			epCap = epCap->next;
		}
		if (!epCap)
		{
			CHRTRACEERR4("ERROR:Incompatible audio capabilities - Can not open "
				"%s channel (%s, %s)\n",
				(capType == CHR_CAP_TYPE_AUDIO) ? "audio" : "video", call->callType,
				call->callToken);
			return CHR_FAILED;
		}

	}

	switch (epCap->cap)
	{
	case CHR_G711ALAW64K:
	case CHR_G711ULAW64K:
	case CHR_H264VIDEO:
		chrOpenChannel(call, epCap);
		break;
	default:
		CHRTRACEERR3("ERROR:Unknown Audio Capability type (%s, %s)\n",
			call->callType, call->callToken);
	}
	return CHR_OK;
}

int chrOpenChannel(CHRH323CallData* call, chrH323EpCapability *epCap)
{
	int ret;
	H245Message *ph245msg = NULL;
	H245RequestMessage * request;
	OOCTXT *pctxt = NULL;
	H245OpenLogicalChannel_forwardLogicalChannelParameters *flcp = NULL;
	H245AudioCapability *audioCap = NULL;
	H245VideoCapability *videoCap = NULL;
	H245H2250LogicalChannelParameters *h2250lcp = NULL;
	H245UnicastAddress *unicastAddrs = NULL;
	H245UnicastAddress_iPAddress *iPAddress = NULL;
	unsigned session_id = 0;
	CHRLogicalChannel *pLogicalChannel = NULL;

	CHRTRACEDBG4("Doing Open Channel for %s. (%s, %s)\n",
		chrGetCapTypeText(epCap->cap), call->callType,
		call->callToken);

	ret = chrCreateH245Message(&ph245msg,
		T_H245MultimediaSystemControlMessage_request);
	if (ret != CHR_OK)
	{
		CHRTRACEERR4("Error: H245 message creation failed for - Open %s"
			"channel (%s, %s)\n", chrGetCapTypeText(epCap->cap),
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	ph245msg->msgType = CHROpenLogicalChannel;

	ph245msg->logicalChannelNo = call->logicalChanNoCur++;
	if (call->logicalChanNoCur > call->logicalChanNoMax)
		call->logicalChanNoCur = call->logicalChanNoBase;

	request = ph245msg->h245Msg.u.request;
	pctxt = &gH323ep.msgctxt;
	memset(request, 0, sizeof(H245RequestMessage));

	request->t = T_H245RequestMessage_openLogicalChannel;
	request->u.openLogicalChannel = (H245OpenLogicalChannel*)
		memAlloc(pctxt, sizeof(H245OpenLogicalChannel));
	if (!request->u.openLogicalChannel)
	{
		CHRTRACEERR3("Error:Memory - chrOpenChannel - openLogicalChannel."
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;

	}
	memset(request->u.openLogicalChannel, 0,
		sizeof(H245OpenLogicalChannel));
	request->u.openLogicalChannel->forwardLogicalChannelNumber =
		ph245msg->logicalChannelNo;


	session_id = chrCallGenerateSessionID(call, epCap->capType, "transmit");


	pLogicalChannel = chrAddNewLogicalChannel(call,
		request->u.openLogicalChannel->forwardLogicalChannelNumber,
		session_id, "transmit", epCap);

	if (!pLogicalChannel)
	{
		CHRTRACEERR3("ERROR:Failed to add new logical channel entry (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeH245Message(call, ph245msg);
		return CHR_FAILED;
	}
	/* Populate H245OpenLogicalChannel_ForwardLogicalChannel Parameters*/
	flcp = &(request->u.openLogicalChannel->forwardLogicalChannelParameters);
	flcp->m.portNumberPresent = 0;
	flcp->m.forwardLogicalChannelDependencyPresent = 0;
	flcp->m.replacementForPresent = 0;

	/* data type of channel */
	if (epCap->capType == CHR_CAP_TYPE_AUDIO)
	{
		flcp->dataType.t = T_H245DataType_audioData;
		/* set audio capability for channel */
		audioCap = chrCapabilityCreateAudioCapability(epCap, pctxt, CHRTX);
		if (!audioCap)
		{
			CHRTRACEERR4("Error:Failed to create duplicate audio capability in "
				"chrOpenChannel- %s (%s, %s)\n",
				chrGetCapTypeText(epCap->cap), call->callType,
				call->callToken);
			chrFreeH245Message(call, ph245msg);
			return CHR_FAILED;
		}

		flcp->dataType.u.audioData = audioCap;
	}
	else if (epCap->capType == CHR_CAP_TYPE_VIDEO)
	{
		flcp->dataType.t = T_H245DataType_videoData;
		videoCap = chrCapabilityCreateVideoCapability(epCap, pctxt, CHRTX);
		if (!videoCap)
		{
			CHRTRACEERR4("Error:Failed to create duplicate video capability in "
				"chrOpenChannel- %s (%s, %s)\n",
				chrGetCapTypeText(epCap->cap), call->callType,
				call->callToken);
			chrFreeH245Message(call, ph245msg);
			return CHR_FAILED;
		}

		flcp->dataType.u.videoData = videoCap;
	}
	else{
		CHRTRACEERR1("Error: Unhandled media type in chrOpenChannel\n");
		return CHR_FAILED;
	}


	flcp->multiplexParameters.t =
		T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters;
	flcp->multiplexParameters.u.h2250LogicalChannelParameters =
		(H245H2250LogicalChannelParameters*)ASN1MALLOC(pctxt,
		sizeof(H245H2250LogicalChannelParameters));

	h2250lcp = flcp->multiplexParameters.u.h2250LogicalChannelParameters;
	memset(h2250lcp, 0, sizeof(H245H2250LogicalChannelParameters));

	h2250lcp->sessionID = session_id;

	h2250lcp->mediaGuaranteedDelivery = 0;
	h2250lcp->silenceSuppression = 0;
	h2250lcp->m.mediaControlChannelPresent = 1;

	h2250lcp->mediaControlChannel.t =
		T_H245TransportAddress_unicastAddress;
	h2250lcp->mediaControlChannel.u.unicastAddress = (H245UnicastAddress*)
		ASN1MALLOC(pctxt, sizeof(H245UnicastAddress));

	unicastAddrs = h2250lcp->mediaControlChannel.u.unicastAddress;
	memset(unicastAddrs, 0, sizeof(H245UnicastAddress));
	unicastAddrs->t = T_H245UnicastAddress_iPAddress;
	unicastAddrs->u.iPAddress = (H245UnicastAddress_iPAddress*)
		ASN1MALLOC(pctxt, sizeof(H245UnicastAddress_iPAddress));
	iPAddress = unicastAddrs->u.iPAddress;
	memset(iPAddress, 0, sizeof(H245UnicastAddress_iPAddress));

	chrSocketConvertIpToNwAddr
		(pLogicalChannel->localIP, iPAddress->network.data,
		sizeof(iPAddress->network.data));

	iPAddress->network.numocts = 4;
	iPAddress->tsapIdentifier = pLogicalChannel->localRtcpPort;

	if (epCap->cap == CHR_H264VIDEO)
	{
		CHRH264CapParams *params = (CHRH264CapParams *)epCap->params;
		CHRTRACEDBG6("Building OpenLogicalChannel-%s H264(br=%d, pt=%d) (%s, %s)\n",
			chrGetCapTypeText(epCap->cap), params->maxBitRate, params->send_pt,
			call->callType, call->callToken);

		if (params->send_pt && params->send_pt >= 96 && params->send_pt <= 127) {
			h2250lcp->m.dynamicRTPPayloadTypePresent = 1;
			h2250lcp->dynamicRTPPayloadType = params->send_pt;

			h2250lcp->mediaPacketization.u.rtpPayloadType = (H245RTPPayloadType *)memAlloc(pctxt, sizeof(H245RTPPayloadType));

			if (h2250lcp->mediaPacketization.u.rtpPayloadType) {
				static ASN1OBJID oid = { 8, { 0, 0, 8, 241, 0, 0, 0, 0 } };  //single NALU mode

				h2250lcp->m.mediaPacketizationPresent = 1;
				h2250lcp->mediaPacketization.t = 2;
				h2250lcp->mediaPacketization.u.rtpPayloadType->m.payloadTypePresent = 1;
				h2250lcp->mediaPacketization.u.rtpPayloadType->payloadDescriptor.t = 3;
				h2250lcp->mediaPacketization.u.rtpPayloadType->payloadDescriptor.u.oid = &oid;
				h2250lcp->mediaPacketization.u.rtpPayloadType->payloadType = params->send_pt;
			}
		}
	}

	pLogicalChannel->state = CHR_LOGICALCHAN_PROPOSED;
	CHRTRACEDBG4("Built OpenLogicalChannel-%s (%s, %s)\n",
		chrGetCapTypeText(epCap->cap), call->callType,
		call->callToken);
	ret = chrSendH245Msg(call, ph245msg);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("Error:Failed to enqueue OpenLogicalChannel to outbound "
			"queue. (%s, %s)\n", call->callType,
			call->callToken);
	}
	chrFreeH245Message(call, ph245msg);

	return ret;
}


/* Used to build  OLCs for fast connect. Keep in mind that forward and
reverse
are always with respect to the endpoint which proposes channels
TODO: Need to clean logical channel in case of failure.    */
int chrBuildFastStartOLC
(CHRH323CallData *call, H245OpenLogicalChannel *olc,
chrH323EpCapability *epCap, OOCTXT*pctxt, int dir)
{
	OOBOOL reverse = FALSE, forward = FALSE;
	unsigned sessionID = 0;
	H245OpenLogicalChannel_forwardLogicalChannelParameters *flcp = NULL;
	H245OpenLogicalChannel_reverseLogicalChannelParameters *rlcp = NULL;
	H245H2250LogicalChannelParameters *pH2250lcp1 = NULL, *pH2250lcp2 = NULL;
	H245UnicastAddress *pUnicastAddrs = NULL, *pUniAddrs = NULL;
	H245UnicastAddress_iPAddress *pIpAddrs = NULL, *pUniIpAddrs = NULL;
	unsigned session_id = 0;
	CHRLogicalChannel *pLogicalChannel = NULL;
	int outgoing = FALSE;

	if (!strcmp(call->callType, "outgoing"))
		outgoing = TRUE;

	if (dir & CHRRX)
	{
		CHRTRACEDBG3("Building OpenLogicalChannel for Receive  Capability "
			"(%s, %s)\n", call->callType, call->callToken);
		session_id = chrCallGenerateSessionID(call, epCap->capType, "receive");
		pLogicalChannel = chrAddNewLogicalChannel(call,
			olc->forwardLogicalChannelNumber, session_id,
			"receive", epCap);
		if (outgoing)
			reverse = TRUE;
		else
			forward = TRUE;
	}
	else if (dir & CHRTX)
	{
		CHRTRACEDBG3("Building OpenLogicalChannel for transmit Capability "
			"(%s, %s)\n", call->callType, call->callToken);
		session_id = chrCallGenerateSessionID(call, epCap->capType, "transmit");
		pLogicalChannel = chrAddNewLogicalChannel(call,
			olc->forwardLogicalChannelNumber, session_id,
			"transmit", epCap);
		if (outgoing)
			forward = TRUE;
		else
			reverse = TRUE;
	}
	else if (dir & CHRRXTX)
	{
		CHRTRACEDBG3("Building OpenLogicalChannel for ReceiveAndTransmit  "
			"Capability (%s, %s)\n", call->callType, call->callToken);
		reverse = 1;
		forward = 1;
		CHRTRACEERR3("Symmetric capability is not supported as of now (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	if (forward)
	{
		CHRTRACEDBG3("Building forward olc. (%s, %s)\n", call->callType,
			call->callToken);
		flcp = &(olc->forwardLogicalChannelParameters);
		memset(flcp, 0,
			sizeof(H245OpenLogicalChannel_forwardLogicalChannelParameters));

		if (epCap->capType == CHR_CAP_TYPE_AUDIO) {
			sessionID = 1;
			flcp->dataType.t = T_H245DataType_audioData;
			flcp->dataType.u.audioData = chrCapabilityCreateAudioCapability(epCap,
				pctxt, dir);
		}
		else if (epCap->capType == CHR_CAP_TYPE_VIDEO) {
			sessionID = 2;
			flcp->dataType.t = T_H245DataType_videoData;
			flcp->dataType.u.videoData = chrCapabilityCreateVideoCapability(epCap,
				pctxt, dir);
		}
		flcp->multiplexParameters.t = T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters;
		pH2250lcp1 = (H245H2250LogicalChannelParameters*)ASN1MALLOC(pctxt,
			sizeof(H245H2250LogicalChannelParameters));
		memset(pH2250lcp1, 0, sizeof(H245H2250LogicalChannelParameters));
		flcp->multiplexParameters.t = T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters;

		flcp->multiplexParameters.u.h2250LogicalChannelParameters = pH2250lcp1;

		pH2250lcp1->sessionID = sessionID;
		if (!outgoing)
		{
			pH2250lcp1->m.mediaChannelPresent = 1;
			pH2250lcp1->mediaChannel.t =
				T_H245TransportAddress_unicastAddress;
			pUniAddrs = (H245UnicastAddress*)ASN1MALLOC(pctxt,
				sizeof(H245UnicastAddress));
			memset(pUniAddrs, 0, sizeof(H245UnicastAddress));
			pH2250lcp1->mediaChannel.u.unicastAddress = pUniAddrs;
			pUniAddrs->t = T_H245UnicastAddress_iPAddress;
			pUniIpAddrs = (H245UnicastAddress_iPAddress*)ASN1MALLOC(pctxt,
				sizeof(H245UnicastAddress_iPAddress));
			memset(pUniIpAddrs, 0, sizeof(H245UnicastAddress_iPAddress));
			pUniAddrs->u.iPAddress = pUniIpAddrs;

			chrSocketConvertIpToNwAddr
				(pLogicalChannel->localIP, pUniIpAddrs->network.data,
				sizeof(pUniIpAddrs->network.data));

			pUniIpAddrs->network.numocts = 4;
			pUniIpAddrs->tsapIdentifier = pLogicalChannel->localRtpPort;
		}
		pH2250lcp1->m.mediaControlChannelPresent = 1;
		pH2250lcp1->mediaControlChannel.t =
			T_H245TransportAddress_unicastAddress;
		pUnicastAddrs = (H245UnicastAddress*)ASN1MALLOC(pctxt,
			sizeof(H245UnicastAddress));
		memset(pUnicastAddrs, 0, sizeof(H245UnicastAddress));
		pH2250lcp1->mediaControlChannel.u.unicastAddress = pUnicastAddrs;
		pUnicastAddrs->t = T_H245UnicastAddress_iPAddress;
		pIpAddrs = (H245UnicastAddress_iPAddress*)ASN1MALLOC(pctxt,
			sizeof(H245UnicastAddress_iPAddress));
		memset(pIpAddrs, 0, sizeof(H245UnicastAddress_iPAddress));
		pUnicastAddrs->u.iPAddress = pIpAddrs;

		chrSocketConvertIpToNwAddr
			(pLogicalChannel->localIP, pIpAddrs->network.data,
			sizeof(pIpAddrs->network.data));

		pIpAddrs->network.numocts = 4;
		pIpAddrs->tsapIdentifier = pLogicalChannel->localRtcpPort;
		if (!outgoing)
		{
			if (epCap->startReceiveChannel)
			{
				epCap->startReceiveChannel(call, pLogicalChannel);
				CHRTRACEINFO4("Receive channel of type %s started (%s, %s)\n",
					(epCap->capType == CHR_CAP_TYPE_AUDIO) ? "audio" : "video",
					call->callType, call->callToken);
			}
			else{
				CHRTRACEERR4("ERROR:No callback registered to start receive %s"
					" channel (%s, %s)\n",
					(epCap->capType == CHR_CAP_TYPE_AUDIO) ? "audio" : "video",
					call->callType, call->callToken);
				return CHR_FAILED;
			}
		}
	}

	if (reverse)
	{
		CHRTRACEDBG3("Building reverse olc. (%s, %s)\n", call->callType,
			call->callToken);
		olc->forwardLogicalChannelParameters.dataType.t =
			T_H245DataType_nullData;
		olc->forwardLogicalChannelParameters.multiplexParameters.t =
			T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_none;
		olc->m.reverseLogicalChannelParametersPresent = 1;
		rlcp = &(olc->reverseLogicalChannelParameters);
		memset(rlcp, 0, sizeof(H245OpenLogicalChannel_reverseLogicalChannelParameters));
		if (epCap->capType == CHR_CAP_TYPE_AUDIO) {
			sessionID = 1;
			rlcp->dataType.t = T_H245DataType_audioData;

			rlcp->dataType.u.audioData = chrCapabilityCreateAudioCapability(epCap,
				pctxt, dir);
		}
		else if (epCap->capType == CHR_CAP_TYPE_VIDEO)  {
			sessionID = 2;
			rlcp->dataType.t = T_H245DataType_videoData;

			rlcp->dataType.u.videoData = chrCapabilityCreateVideoCapability(epCap,
				pctxt, dir);
		}

		rlcp->m.multiplexParametersPresent = 1;
		rlcp->multiplexParameters.t = T_H245OpenLogicalChannel_reverseLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters;
		pH2250lcp2 = (H245H2250LogicalChannelParameters*)ASN1MALLOC(pctxt, sizeof(H245H2250LogicalChannelParameters));
		rlcp->multiplexParameters.u.h2250LogicalChannelParameters = pH2250lcp2;
		memset(pH2250lcp2, 0, sizeof(H245H2250LogicalChannelParameters));
		pH2250lcp2->sessionID = sessionID;

		if (outgoing)
		{
			pH2250lcp2->m.mediaChannelPresent = 1;

			pH2250lcp2->mediaChannel.t =
				T_H245TransportAddress_unicastAddress;
			pUnicastAddrs = (H245UnicastAddress*)memAlloc(pctxt,
				sizeof(H245UnicastAddress));
			memset(pUnicastAddrs, 0, sizeof(H245UnicastAddress));
			pH2250lcp2->mediaChannel.u.unicastAddress = pUnicastAddrs;

			pUnicastAddrs->t = T_H245UnicastAddress_iPAddress;
			pIpAddrs = (H245UnicastAddress_iPAddress*)memAlloc(pctxt,
				sizeof(H245UnicastAddress_iPAddress));
			memset(pIpAddrs, 0, sizeof(H245UnicastAddress_iPAddress));
			pUnicastAddrs->u.iPAddress = pIpAddrs;

			chrSocketConvertIpToNwAddr
				(pLogicalChannel->localIP, pIpAddrs->network.data,
				sizeof(pIpAddrs->network.data));

			pIpAddrs->network.numocts = 4;
			pIpAddrs->tsapIdentifier = pLogicalChannel->localRtpPort;
		}
		pH2250lcp2->m.mediaControlChannelPresent = 1;
		pH2250lcp2->mediaControlChannel.t =
			T_H245TransportAddress_unicastAddress;
		pUniAddrs = (H245UnicastAddress*)ASN1MALLOC(pctxt, sizeof(H245UnicastAddress));

		memset(pUniAddrs, 0, sizeof(H245UnicastAddress));
		pH2250lcp2->mediaControlChannel.u.unicastAddress = pUniAddrs;


		pUniAddrs->t = T_H245UnicastAddress_iPAddress;
		pUniIpAddrs = (H245UnicastAddress_iPAddress*)ASN1MALLOC(pctxt, sizeof(H245UnicastAddress_iPAddress));
		memset(pUniIpAddrs, 0, sizeof(H245UnicastAddress_iPAddress));
		pUniAddrs->u.iPAddress = pUniIpAddrs;

		chrSocketConvertIpToNwAddr
			(pLogicalChannel->localIP, pUniIpAddrs->network.data,
			sizeof(pUniIpAddrs->network.data));

		pUniIpAddrs->network.numocts = 4;
		pUniIpAddrs->tsapIdentifier = pLogicalChannel->localRtcpPort;

		/*
		In case of fast start, the local endpoint need to be ready to
		receive all the media types proposed in the fast connect, before
		the actual call is established.
		*/
		if (outgoing)
		{
			if (epCap->startReceiveChannel)
			{
				epCap->startReceiveChannel(call, pLogicalChannel);
				CHRTRACEINFO4("Receive channel of type %s started (%s, %s)\n",
					(epCap->capType == CHR_CAP_TYPE_AUDIO) ? "audio" : "video",
					call->callType, call->callToken);
			}
			else{
				CHRTRACEERR4("ERROR:No callback registered to start receive %s "
					"channel (%s, %s)\n",
					(epCap->capType == CHR_CAP_TYPE_AUDIO) ? "audio" : "video",
					call->callType, call->callToken);
				return CHR_FAILED;
			}
		}
	}

	/* State of logical channel. for out going calls, as we are sending setup,
	state of all channels are proposed, for incoming calls, state is
	established. */
	if (!outgoing) {
		pLogicalChannel->state = CHR_LOGICALCHAN_ESTABLISHED;
	}
	else {
		/* Calling other ep, with SETUP message */
		/* Call is "outgoing */
		pLogicalChannel->state = CHR_LOGICALCHAN_PROPOSED;
	}

	return CHR_OK;
}



int chrMSDTimerExpired(void *data)
{
	chrTimerCallback *cbData = (chrTimerCallback*)data;
	CHRH323CallData *call = cbData->call;
	CHRTRACEINFO3("MasterSlaveDetermination timeout. (%s, %s)\n", call->callType,
		call->callToken);
	ASN1MEMFREEPTR(call->pctxt, cbData);
	chrSendMasterSlaveDeterminationRelease(call);
	if (call->callState < CHR_CALL_CLEAR)
	{
		call->callState = CHR_CALL_CLEAR;
		call->callEndReason = CHR_REASON_LOCAL_CLEARED;
	}

	return CHR_OK;
}

int chrTCSTimerExpired(void *data)
{
	chrTimerCallback *cbData = (chrTimerCallback*)data;
	CHRH323CallData *call = cbData->call;
	CHRTRACEINFO3("TerminalCapabilityExchange timeout. (%s, %s)\n",
		call->callType, call->callToken);
	ASN1MEMFREEPTR(call->pctxt, cbData);
	chrSendTerminalCapabilitySetRelease(call);
	if (call->callState < CHR_CALL_CLEAR)
	{
		call->callState = CHR_CALL_CLEAR;
		call->callEndReason = CHR_REASON_LOCAL_CLEARED;
	}

	return CHR_OK;
}

int chrOpenLogicalChannelTimerExpired(void *pdata)
{
	chrTimerCallback *cbData = (chrTimerCallback*)pdata;
	CHRH323CallData *call = cbData->call;
	CHRLogicalChannel *pChannel = NULL;
	CHRTRACEINFO3("OpenLogicalChannelTimer expired. (%s, %s)\n", call->callType,
		call->callToken);
	pChannel = chrFindLogicalChannelByLogicalChannelNo(call,
		cbData->channelNumber);
	if (pChannel)
		chrSendCloseLogicalChannel(call, pChannel);

	if (call->callState < CHR_CALL_CLEAR)
	{
		call->callState = CHR_CALL_CLEAR;
		call->callEndReason = CHR_REASON_LOCAL_CLEARED;
	}
	ASN1MEMFREEPTR(call->pctxt, cbData);
	return CHR_OK;
}

int chrCloseLogicalChannelTimerExpired(void *pdata)
{
	chrTimerCallback *cbData = (chrTimerCallback*)pdata;
	CHRH323CallData *call = cbData->call;

	CHRTRACEINFO3("CloseLogicalChannelTimer expired. (%s, %s)\n", call->callType,
		call->callToken);

	chrClearLogicalChannel(call, cbData->channelNumber);

	if (call->callState < CHR_CALL_CLEAR)
	{
		call->callState = CHR_CALL_CLEAR;
		call->callEndReason = CHR_REASON_LOCAL_CLEARED;
	}
	ASN1MEMFREEPTR(call->pctxt, cbData);
	return CHR_OK;
}

int chrRequestChannelCloseTimerExpired(void *pdata)
{
	int ret = 0;
	chrTimerCallback *cbData = (chrTimerCallback*)pdata;
	CHRH323CallData *call = cbData->call;

	CHRTRACEINFO3("OpenLogicalChannelTimer expired. (%s, %s)\n", call->callType,
		call->callToken);

	chrSendRequestChannelCloseRelease(call, cbData->channelNumber);

	ret = chrClearLogicalChannel(call, cbData->channelNumber);
	if (ret != CHR_OK)
	{
		CHRTRACEERR4("Error:Failed to clear logical channel %d. (%s, %s)\n",
			cbData->channelNumber, call->callType, call->callToken);
	}

	if (call->callState < CHR_CALL_CLEAR)
	{
		call->callState = CHR_CALL_CLEAR;
		call->callEndReason = CHR_REASON_LOCAL_CLEARED;
	}
	ASN1MEMFREEPTR(call->pctxt, cbData);
	return CHR_OK;
}

int chrSessionTimerExpired(void *pdata)
{
	int ret = 0;
	chrTimerCallback *cbData = (chrTimerCallback*)pdata;
	CHRH323CallData *call = cbData->call;

	CHRTRACEINFO3("SessionTimer expired. (%s, %s)\n", call->callType,
		call->callToken);

	if (call->h245SessionState != CHR_H245SESSION_IDLE &&
		call->h245SessionState != CHR_H245SESSION_CLOSED &&
		call->h245SessionState != CHR_H245SESSION_PAUSED) {

		ret = chrCloseH245Connection(call);

		if (ret != CHR_OK) {
			CHRTRACEERR3("Error:Failed to close H.245 connection (%s, %s)\n",
				call->callType, call->callToken);
		}
	}

	memFreePtr(call->pctxt, cbData);

	if (call->callState == CHR_CALL_CLEAR_RELEASESENT)
		call->callState = CHR_CALL_CLEARED;

	return CHR_OK;
}


int chrGetIpPortFromH245TransportAddress
(CHRH323CallData *call, H245TransportAddress *h245Address, char *ip,
int *port)
{
	H245UnicastAddress *unicastAddress = NULL;
	H245UnicastAddress_iPAddress *ipAddress = NULL;

	if (h245Address->t != T_H245TransportAddress_unicastAddress)
	{
		CHRTRACEERR3("ERROR:Unsupported H245 address type "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	unicastAddress = h245Address->u.unicastAddress;
	if (unicastAddress->t != T_H245UnicastAddress_iPAddress)
	{
		CHRTRACEERR3("ERROR:H245 Address type is not IP"
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	ipAddress = unicastAddress->u.iPAddress;

	*port = ipAddress->tsapIdentifier;

	sprintf(ip, "%d.%d.%d.%d", ipAddress->network.data[0],
		ipAddress->network.data[1],
		ipAddress->network.data[2],
		ipAddress->network.data[3]);
	return CHR_OK;
}


int chrPrepareFastStartResponseOLC
(CHRH323CallData *call, H245OpenLogicalChannel *olc,
chrH323EpCapability *epCap, OOCTXT*pctxt, int dir)
{
	OOBOOL reverse = FALSE, forward = FALSE;
	H245OpenLogicalChannel_forwardLogicalChannelParameters *flcp = NULL;
	H245OpenLogicalChannel_reverseLogicalChannelParameters *rlcp = NULL;
	H245H2250LogicalChannelParameters *pH2250lcp1 = NULL, *pH2250lcp2 = NULL;
	H245UnicastAddress *pUnicastAddrs = NULL, *pUniAddrs = NULL;
	H245UnicastAddress_iPAddress *pIpAddrs = NULL, *pUniIpAddrs = NULL;
	unsigned session_id = 0;
	CHRLogicalChannel *pLogicalChannel = NULL;

	if (dir & CHRRX)
	{
		CHRTRACEDBG3("chrPrepareFastStartResponseOLC for Receive  Capability "
			"(%s, %s)\n", call->callType, call->callToken);
		session_id = chrCallGenerateSessionID(call, epCap->capType, "receive");
		pLogicalChannel = chrAddNewLogicalChannel(call,
			olc->forwardLogicalChannelNumber, session_id,
			"receive", epCap);
		forward = TRUE;
	}
	else if (dir & CHRTX)
	{
		CHRTRACEDBG3("chrPrepareFastStartResponseOLC for transmit Capability "
			"(%s, %s)\n", call->callType, call->callToken);
		session_id = chrCallGenerateSessionID(call, epCap->capType, "transmit");
		pLogicalChannel = chrAddNewLogicalChannel(call,
			olc->forwardLogicalChannelNumber, session_id,
			"transmit", epCap);
		reverse = TRUE;
	}
	else if (dir & CHRRXTX)
	{
		CHRTRACEDBG3("chrPrepareFastStartResponseOLC for ReceiveAndTransmit  "
			"Capability (%s, %s)\n", call->callType, call->callToken);
		reverse = 1;
		forward = 1;
		CHRTRACEERR3("Symmetric capability is not supported as of now (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}

	if (forward)
	{
		CHRTRACEDBG3("Preparing olc for receive channel. (%s, %s)\n",
			call->callType, call->callToken);
		flcp = &(olc->forwardLogicalChannelParameters);

		pH2250lcp1 = flcp->multiplexParameters.u.h2250LogicalChannelParameters;


		pH2250lcp1->m.mediaChannelPresent = 1;
		pH2250lcp1->mediaChannel.t = T_H245TransportAddress_unicastAddress;
		pUniAddrs = (H245UnicastAddress*)memAlloc(pctxt,
			sizeof(H245UnicastAddress));
		pUniIpAddrs = (H245UnicastAddress_iPAddress*)memAlloc(pctxt,
			sizeof(H245UnicastAddress_iPAddress));
		if (!pUniAddrs || !pUniIpAddrs)
		{
			CHRTRACEERR3("Error:Memory - chrPrepareFastStartResponseOLC - pUniAddrs"
				"/pUniIpAddrs (%s, %s)\n", call->callType,
				call->callToken);
			return CHR_FAILED;
		}

		pH2250lcp1->mediaChannel.u.unicastAddress = pUniAddrs;
		pUniAddrs->t = T_H245UnicastAddress_iPAddress;
		pUniAddrs->u.iPAddress = pUniIpAddrs;

		chrSocketConvertIpToNwAddr
			(pLogicalChannel->localIP, pUniIpAddrs->network.data,
			sizeof(pUniIpAddrs->network.data));

		pUniIpAddrs->network.numocts = 4;
		pUniIpAddrs->tsapIdentifier = pLogicalChannel->localRtpPort;

		pH2250lcp1->m.mediaControlChannelPresent = 1;
		pH2250lcp1->mediaControlChannel.t =
			T_H245TransportAddress_unicastAddress;
		pUnicastAddrs = (H245UnicastAddress*)memAlloc(pctxt,
			sizeof(H245UnicastAddress));
		pIpAddrs = (H245UnicastAddress_iPAddress*)memAlloc(pctxt,
			sizeof(H245UnicastAddress_iPAddress));
		if (!pUnicastAddrs || !pIpAddrs)
		{
			CHRTRACEERR3("Error:Memory - chrPrepareFastStartResponseOLC - "
				"pUnicastAddrs/pIpAddrs (%s, %s)\n", call->callType,
				call->callToken);
			return CHR_FAILED;
		}
		memset(pUnicastAddrs, 0, sizeof(H245UnicastAddress));
		pH2250lcp1->mediaControlChannel.u.unicastAddress = pUnicastAddrs;
		pUnicastAddrs->t = T_H245UnicastAddress_iPAddress;

		pUnicastAddrs->u.iPAddress = pIpAddrs;

		chrSocketConvertIpToNwAddr
			(pLogicalChannel->localIP, pIpAddrs->network.data,
			sizeof(pIpAddrs->network.data));

		pIpAddrs->network.numocts = 4;
		pIpAddrs->tsapIdentifier = pLogicalChannel->localRtcpPort;
	}

	if (reverse)
	{
		CHRTRACEDBG3("Building reverse olc. (%s, %s)\n", call->callType,
			call->callToken);

		rlcp = &(olc->reverseLogicalChannelParameters);

		pH2250lcp2 = rlcp->multiplexParameters.u.h2250LogicalChannelParameters;
		pH2250lcp2->m.mediaChannelPresent = 0;
		memset(&pH2250lcp2->mediaChannel, 0, sizeof(H245TransportAddress));

		pH2250lcp2->m.mediaControlChannelPresent = 1;
		pH2250lcp2->mediaControlChannel.t =
			T_H245TransportAddress_unicastAddress;
		pUniAddrs = (H245UnicastAddress*)memAlloc(pctxt,
			sizeof(H245UnicastAddress));
		pUniIpAddrs = (H245UnicastAddress_iPAddress*)memAlloc(pctxt,
			sizeof(H245UnicastAddress_iPAddress));
		if (!pUniAddrs || !pUniIpAddrs)
		{
			CHRTRACEERR3("Error:Memory - chrPrepareFastStartResponseOLC - "
				"pUniAddrs/pUniIpAddrs (%s, %s)\n", call->callType,
				call->callToken);
			return CHR_FAILED;
		}

		pH2250lcp2->mediaControlChannel.u.unicastAddress = pUniAddrs;

		pUniAddrs->t = T_H245UnicastAddress_iPAddress;

		pUniAddrs->u.iPAddress = pUniIpAddrs;

		chrSocketConvertIpToNwAddr
			(pLogicalChannel->localIP, pUniIpAddrs->network.data,
			sizeof(pUniIpAddrs->network.data));

		pUniIpAddrs->network.numocts = 4;
		pUniIpAddrs->tsapIdentifier = pLogicalChannel->localRtcpPort;

	}

	pLogicalChannel->state = CHR_LOGICALCHAN_ESTABLISHED;

	return CHR_OK;
}

/****************************** H225&H245 ******************************/


int chrOnReceivedReleaseComplete(CHRH323CallData *call, Q931Message *q931Msg)
{
	int ret = CHR_OK;
	H225ReleaseComplete_UUIE * releaseComplete = NULL;
	ASN1UINT i;
	DListNode *pNode = NULL;
	CHRTimer *pTimer = NULL;
	unsigned reasonCode = T_H225ReleaseCompleteReason_undefinedReason;
	enum Q931CauseValues cause = Q931ErrorInCauseIE;

	if (q931Msg->causeIE)
	{
		cause = q931Msg->causeIE->data[1];
		/* Get rid of the extension bit.For more info, check chrQ931SetCauseIE */
		cause = cause & 0x7f;
		CHRTRACEDBG4("Cause of Release Complete is %x. (%s, %s)\n", cause,
			call->callType, call->callToken);
	}

	/* Remove session timer, if active*/
	for (i = 0; i<call->timerList.count; i++)
	{
		pNode = dListFindByIndex(&call->timerList, i);
		pTimer = (CHRTimer*)pNode->data;
		if (((chrTimerCallback*)pTimer->cbData)->timerType &
			CHR_SESSION_TIMER)
		{
			memFreePtr(call->pctxt, pTimer->cbData);
			chrTimerDelete(call->pctxt, &call->timerList, pTimer);
			CHRTRACEDBG3("Deleted Session Timer. (%s, %s)\n",
				call->callType, call->callToken);
			break;
		}
	}


	if (!q931Msg->userInfo)
	{
		CHRTRACEERR3("ERROR:No User-User IE in received ReleaseComplete message "
			"(%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	releaseComplete = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.releaseComplete;
	if (!releaseComplete)
	{
		CHRTRACEWARN3("WARN: ReleaseComplete UUIE not found in received "
			"ReleaseComplete message - %s "
			"%s\n", call->callType, call->callToken);
	}
	else{

		if (releaseComplete->m.reasonPresent)
		{
			CHRTRACEINFO4("Release complete reason code %d. (%s, %s)\n",
				releaseComplete->reason.t, call->callType, call->callToken);
			reasonCode = releaseComplete->reason.t;
		}
	}

	if (call->callEndReason == CHR_REASON_UNKNOWN)
		call->callEndReason = chrGetCallClearReasonFromCauseAndReasonCode(cause,
		reasonCode);
#if 0
	if (q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
		q931Msg->userInfo->h323_uu_pdu.h245Tunneling          &&
		CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
	{
		CHRTRACEDBG3("Handling tunneled messages in ReleaseComplete. (%s, %s)\n",
			call->callType, call->callToken);
		ret = chrHandleTunneledH245Messages
			(call, &q931Msg->userInfo->h323_uu_pdu);
		CHRTRACEDBG3("Finished handling tunneled messages in ReleaseComplete."
			" (%s, %s)\n", call->callType, call->callToken);
	}
#endif
	if (call->h245SessionState != CHR_H245SESSION_IDLE &&
		call->h245SessionState != CHR_H245SESSION_CLOSED)
	{
		chrCloseH245Connection(call);
	}

	if (call->callState != CHR_CALL_CLEAR_RELEASESENT)
	{
		;
	}
	call->callState = CHR_CALL_CLEARED;

	return ret;
}

int chrOnReceivedSetup(CHRH323CallData *call, Q931Message *q931Msg)
{
	H225Setup_UUIE *setup = NULL;
	int i = 0, ret = 0;
	H245OpenLogicalChannel* olc;
	ASN1OCTET msgbuf[MAXMSGLEN];
	H225TransportAddress_ipAddress_ip *ip = NULL;
	Q931InformationElement* pDisplayIE = NULL;
	CHRAliases *pAlias = NULL;

	call->callReference = q931Msg->callReference;

	if (!q931Msg->userInfo)
	{
		CHRTRACEERR3("ERROR:No User-User IE in received SETUP message (%s, %s)\n",
			call->callType, call->callToken);
		return CHR_FAILED;
	}
	setup = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.setup;
	if (!setup)
	{
		CHRTRACEERR3("Error: Setup UUIE not found in received setup message - %s "
			"%s\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	memcpy(call->callIdentifier.guid.data, setup->callIdentifier.guid.data,
		setup->callIdentifier.guid.numocts);
	call->callIdentifier.guid.numocts = setup->callIdentifier.guid.numocts;

	memcpy(call->confIdentifier.data, setup->conferenceID.data,
		setup->conferenceID.numocts);
	call->confIdentifier.numocts = setup->conferenceID.numocts;

	/* check for display ie */
	pDisplayIE = chrQ931GetIE(q931Msg, Q931DisplayIE);
	if (pDisplayIE)
	{
		call->remoteDisplayName = (char*)memAllocZ
			(call->pctxt, pDisplayIE->length + 1);

		strncpy
			(call->remoteDisplayName, (const char*)pDisplayIE->data,
			pDisplayIE->length);
	}
	/*Extract Remote Aliases, if present*/
	if (setup->m.sourceAddressPresent)
	{
		if (setup->sourceAddress.count>0)
		{
			chrH323RetrieveAliases(call, &setup->sourceAddress,
				&call->remoteAliases);
			pAlias = call->remoteAliases;
			while (pAlias)
			{
				if (pAlias->type == T_H225AliasAddress_dialedDigits)
				{
					if (!call->callingPartyNumber)
					{
						call->callingPartyNumber = (char*)memAlloc(call->pctxt,
							strlen(pAlias->value)*+1);
						if (call->callingPartyNumber)
						{
							strcpy(call->callingPartyNumber, pAlias->value);
						}
					}
					break;
				}
				pAlias = pAlias->next;
			}
		}
	}
	/* Extract, aliases used for us, if present. Also,
	Populate calledPartyNumber from dialedDigits, if not yet populated using
	calledPartyNumber Q931 IE.
	*/
	if (setup->m.destinationAddressPresent)
	{
		if (setup->destinationAddress.count>0)
		{
			chrH323RetrieveAliases(call, &setup->destinationAddress,
				&call->ourAliases);
			pAlias = call->ourAliases;
			while (pAlias)
			{
				if (pAlias->type == T_H225AliasAddress_dialedDigits)
				{
					if (!call->calledPartyNumber)
					{
						call->calledPartyNumber = (char*)memAlloc(call->pctxt,
							strlen(pAlias->value)*+1);
						if (call->calledPartyNumber)
						{
							strcpy(call->calledPartyNumber, pAlias->value);
						}
					}
					break;
				}
				pAlias = pAlias->next;
			}
		}
	}

	/* Check for tunneling */
	if (q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent)
	{
		/* Tunneling enabled only when tunneling is set to true and h245
		address is absent. In the presence of H.245 address in received
		SETUP message, tunneling is disabled, irrespective of tunneling
		flag in the setup message*/
		if (q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
			!setup->m.h245AddressPresent)
		{
			if (CHR_TESTFLAG(gH323ep.flags, CHR_M_TUNNELING))
			{
				CHR_SETFLAG(call->flags, CHR_M_TUNNELING);
				CHRTRACEINFO3("Call has tunneling active (%s,%s)\n", call->callType,
					call->callToken);
			}
			else
				CHRTRACEINFO3("ERROR:Remote endpoint wants to use h245Tunneling, "
				"local endpoint has it disabled (%s,%s)\n",
				call->callType, call->callToken);
		}
		else {
			if (CHR_TESTFLAG(gH323ep.flags, CHR_M_TUNNELING))
			{
				CHRTRACEINFO3("Tunneling disabled by remote endpoint. (%s, %s)\n",
					call->callType, call->callToken);
			}
			CHR_CLRFLAG(call->flags, CHR_M_TUNNELING);
		}
	}
	else {
		if (CHR_TESTFLAG(gH323ep.flags, CHR_M_TUNNELING))
		{
			CHRTRACEINFO3("Tunneling disabled by remote endpoint. (%s, %s)\n",
				call->callType, call->callToken);
		}
		CHR_CLRFLAG(call->flags, CHR_M_TUNNELING);
	}

	/* Extract Remote IP address */
	if (!setup->m.sourceCallSignalAddressPresent)
	{
		CHRTRACEWARN3("WARNING:Missing source call signal address in received "
			"setup (%s, %s)\n", call->callType, call->callToken);
	}
	else{

		if (setup->sourceCallSignalAddress.t != T_H225TransportAddress_ipAddress)
		{
			CHRTRACEERR3("ERROR: Source call signalling address type not ip "
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}

		ip = &setup->sourceCallSignalAddress.u.ipAddress->ip;
		sprintf(call->remoteIP, "%d.%d.%d.%d", ip->data[0], ip->data[1],
			ip->data[2], ip->data[3]);
		call->remotePort = setup->sourceCallSignalAddress.u.ipAddress->port;
	}

	/* check for fast start */

	if (setup->m.fastStartPresent)
	{
		if (!CHR_TESTFLAG(gH323ep.flags, CHR_M_FASTSTART))
		{
			CHRTRACEINFO3("Local endpoint does not support fastStart. Ignoring "
				"fastStart. (%s, %s)\n", call->callType, call->callToken);
			CHR_CLRFLAG(call->flags, CHR_M_FASTSTART);
		}
		else if (setup->fastStart.n == 0)
		{
			CHRTRACEINFO3("Empty faststart element received. Ignoring fast start. "
				"(%s, %s)\n", call->callType, call->callToken);
			CHR_CLRFLAG(call->flags, CHR_M_FASTSTART);
		}
		else{
			CHR_SETFLAG(call->flags, CHR_M_FASTSTART);
			CHRTRACEINFO3("FastStart enabled for call(%s, %s)\n", call->callType,
				call->callToken);
		}
	}

	if (CHR_TESTFLAG(call->flags, CHR_M_FASTSTART))
	{
		/* For printing the decoded message to log, initialize handler. */
		initializePrintHandler(&printHandler, "FastStart Elements");

		/* Set print handler */
		setEventHandler(call->pctxt, &printHandler);

		for (i = 0; i<(int)setup->fastStart.n; i++)
		{
			olc = NULL;
			/*         memset(msgbuf, 0, sizeof(msgbuf));*/
			olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt,
				sizeof(H245OpenLogicalChannel));
			if (!olc)
			{
				CHRTRACEERR3("ERROR:Memory - chrOnReceivedSetup - olc (%s, %s)\n",
					call->callType, call->callToken);
				/*Mark call for clearing */
				if (call->callState < CHR_CALL_CLEAR)
				{
					call->callEndReason = CHR_REASON_LOCAL_CLEARED;
					call->callState = CHR_CALL_CLEAR;
				}
				return CHR_FAILED;
			}
			memset(olc, 0, sizeof(H245OpenLogicalChannel));
			memcpy(msgbuf, setup->fastStart.elem[i].data,
				setup->fastStart.elem[i].numocts);

			setPERBuffer(call->pctxt, msgbuf,
				setup->fastStart.elem[i].numocts, 1);
			ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
			if (ret != ASN_OK)
			{
				CHRTRACEERR3("ERROR:Failed to decode fast start olc element "
					"(%s, %s)\n", call->callType, call->callToken);
				/* Mark call for clearing */
				if (call->callState < CHR_CALL_CLEAR)
				{
					call->callEndReason = CHR_REASON_INVALIDMESSAGE;
					call->callState = CHR_CALL_CLEAR;
				}
				return CHR_FAILED;
			}
			/* For now, just add decoded fast start elemts to list. This list
			will be processed at the time of sending CONNECT message. */
			dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);
		}
		finishPrint();
		removeEventHandler(call->pctxt);
	}

	return CHR_OK;
}



int chrOnReceivedCallProceeding(CHRH323CallData *call, Q931Message *q931Msg)
{
	H225CallProceeding_UUIE *callProceeding = NULL;
	H245OpenLogicalChannel* olc;
	ASN1OCTET msgbuf[MAXMSGLEN];
	CHRLogicalChannel * pChannel = NULL;
	H245H2250LogicalChannelParameters * h2250lcp = NULL;
	int i = 0, ret = 0;

	if (!q931Msg->userInfo)
	{
		CHRTRACEERR3("ERROR:No User-User IE in received CallProceeding message."
			" (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	callProceeding =
		q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.callProceeding;
	if (callProceeding == NULL)
	{
		CHRTRACEERR3("Error: Received CallProceeding message does not have "
			"CallProceeding UUIE (%s, %s)\n", call->callType,
			call->callToken);
		/* Mark call for clearing */
		if (call->callState < CHR_CALL_CLEAR)
		{
			call->callEndReason = CHR_REASON_INVALIDMESSAGE;
			call->callState = CHR_CALL_CLEAR;
		}
		return CHR_FAILED;
	}

	/* Handle fast-start */
	if (CHR_TESTFLAG(call->flags, CHR_M_FASTSTART))
	{
		if (callProceeding->m.fastStartPresent)
		{
			/* For printing the decoded message to log, initialize handler. */
			initializePrintHandler(&printHandler, "FastStart Elements");

			/* Set print handler */
			setEventHandler(call->pctxt, &printHandler);

			for (i = 0; i<(int)callProceeding->fastStart.n; i++)
			{
				olc = NULL;

				olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt,
					sizeof(H245OpenLogicalChannel));
				if (!olc)
				{
					CHRTRACEERR3("ERROR:Memory - chrOnReceivedCallProceeding - olc"
						"(%s, %s)\n", call->callType, call->callToken);
					/*Mark call for clearing */
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_LOCAL_CLEARED;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}
				memset(olc, 0, sizeof(H245OpenLogicalChannel));
				memcpy(msgbuf, callProceeding->fastStart.elem[i].data,
					callProceeding->fastStart.elem[i].numocts);
				setPERBuffer(call->pctxt, msgbuf,
					callProceeding->fastStart.elem[i].numocts, 1);
				ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
				if (ret != ASN_OK)
				{
					CHRTRACEERR3("ERROR:Failed to decode fast start olc element "
						"(%s, %s)\n", call->callType, call->callToken);
					/* Mark call for clearing */
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_INVALIDMESSAGE;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}

				dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

				pChannel = chrFindLogicalChannelByOLC(call, olc);
				if (!pChannel)
				{
					CHRTRACEERR4("ERROR: Logical Channel %d not found, fast start. "
						"(%s, %s)\n",
						olc->forwardLogicalChannelNumber, call->callType,
						call->callToken);
					return CHR_FAILED;
				}
				if (pChannel->channelNo != olc->forwardLogicalChannelNumber)
				{
					CHRTRACEINFO5("Remote endpoint changed forwardLogicalChannel"
						"Number from %d to %d (%s, %s)\n",
						pChannel->channelNo,
						olc->forwardLogicalChannelNumber, call->callType,
						call->callToken);
					pChannel->channelNo = olc->forwardLogicalChannelNumber;
				}
				if (!strcmp(pChannel->dir, "transmit"))
				{
					if (olc->forwardLogicalChannelParameters.multiplexParameters.t !=
						T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
					{
						CHRTRACEERR4("ERROR:Unknown multiplex parameter type for "
							"channel %d (%s, %s)\n",
							olc->forwardLogicalChannelNumber, call->callType,
							call->callToken);
						continue;
					}

					/* Extract the remote media endpoint address */
					h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
					if (!h2250lcp)
					{
						CHRTRACEERR3("ERROR:Invalid OLC received in fast start. No "
							"forward Logical Channel Parameters found. "
							"(%s, %s)\n", call->callType, call->callToken);
						return CHR_FAILED;
					}
					if (!h2250lcp->m.mediaChannelPresent)
					{
						CHRTRACEERR3("ERROR:Invalid OLC received in fast start. No "
							"reverse media channel information found."
							"(%s, %s)\n", call->callType, call->callToken);
						return CHR_FAILED;
					}
					ret = chrGetIpPortFromH245TransportAddress(call,
						&h2250lcp->mediaChannel, pChannel->remoteIP,
						&pChannel->remoteMediaPort);

					if (ret != CHR_OK)
					{
						CHRTRACEERR3("ERROR:Unsupported media channel address type "
							"(%s, %s)\n", call->callType, call->callToken);
						return CHR_FAILED;
					}

					if (!pChannel->chanCap->startTransmitChannel)
					{
						CHRTRACEERR3("ERROR:No callback registered to start transmit "
							"channel (%s, %s)\n", call->callType,
							call->callToken);
						return CHR_FAILED;
					}
					pChannel->chanCap->startTransmitChannel(call, pChannel);
				}
				/* Mark the current channel as established and close all other
				logical channels with same session id and in same direction.
				*/
				chrOnLogicalChannelEstablished(call, pChannel);
			}
			finishPrint();
			removeEventHandler(call->pctxt);
			CHR_SETFLAG(call->flags, CHR_M_FASTSTARTANSWERED);
		}

	}

	/* Retrieve the H.245 control channel address from the connect msg */
	if (q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
		q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
		callProceeding->m.h245AddressPresent) {
		CHRTRACEINFO3("Tunneling and h245address provided."
			"Using Tunneling for H.245 messages (%s, %s)\n",
			call->callType, call->callToken);
	}
	else if (callProceeding->m.h245AddressPresent)
	{
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
		{
			CHR_CLRFLAG(call->flags, CHR_M_TUNNELING);
			CHRTRACEINFO3("Tunneling is disabled for call as H245 address is "
				"provided in callProceeding message (%s, %s)\n",
				call->callType, call->callToken);
		}
		ret = chrH323GetIpPortFromH225TransportAddress(call,
			&callProceeding->h245Address, call->remoteIP,
			&call->remoteH245Port);
		if (ret != CHR_OK)
		{
			CHRTRACEERR3("Error: Unknown H245 address type in received "
				"CallProceeding message (%s, %s)", call->callType,
				call->callToken);
			/* Mark call for clearing */
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_INVALIDMESSAGE;
				call->callState = CHR_CALL_CLEAR;
			}
			return CHR_FAILED;
		}
	}
	return CHR_OK;
}


int chrOnReceivedAlerting(CHRH323CallData *call, Q931Message *q931Msg)
{
	H225Alerting_UUIE *alerting = NULL;
	H245OpenLogicalChannel* olc;
	ASN1OCTET msgbuf[MAXMSGLEN];
	CHRLogicalChannel * pChannel = NULL;
	H245H2250LogicalChannelParameters * h2250lcp = NULL;
	int i = 0, ret = 0;


	if (!q931Msg->userInfo)
	{
		CHRTRACEERR3("ERROR:No User-User IE in received Alerting message."
			" (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	alerting = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.alerting;
	if (alerting == NULL)
	{
		CHRTRACEERR3("Error: Received Alerting message does not have "
			"alerting UUIE (%s, %s)\n", call->callType,
			call->callToken);
		/* Mark call for clearing */
		if (call->callState < CHR_CALL_CLEAR)
		{
			call->callEndReason = CHR_REASON_INVALIDMESSAGE;
			call->callState = CHR_CALL_CLEAR;
		}
		return CHR_FAILED;
	}
	/*Handle fast-start */
	if (CHR_TESTFLAG(call->flags, CHR_M_FASTSTART) &&
		!CHR_TESTFLAG(call->flags, CHR_M_FASTSTARTANSWERED))
	{
		if (alerting->m.fastStartPresent)
		{
			/* For printing the decoded message to log, initialize handler. */
			initializePrintHandler(&printHandler, "FastStart Elements");

			/* Set print handler */
			setEventHandler(call->pctxt, &printHandler);

			for (i = 0; i<(int)alerting->fastStart.n; i++)
			{
				olc = NULL;

				olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt,
					sizeof(H245OpenLogicalChannel));
				if (!olc)
				{
					CHRTRACEERR3("ERROR:Memory - chrOnReceivedAlerting - olc"
						"(%s, %s)\n", call->callType, call->callToken);
					/*Mark call for clearing */
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_LOCAL_CLEARED;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}
				memset(olc, 0, sizeof(H245OpenLogicalChannel));
				memcpy(msgbuf, alerting->fastStart.elem[i].data,
					alerting->fastStart.elem[i].numocts);
				setPERBuffer(call->pctxt, msgbuf,
					alerting->fastStart.elem[i].numocts, 1);
				ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
				if (ret != ASN_OK)
				{
					CHRTRACEERR3("ERROR:Failed to decode fast start olc element "
						"(%s, %s)\n", call->callType, call->callToken);
					/* Mark call for clearing */
					if (call->callState < CHR_CALL_CLEAR)
					{
						call->callEndReason = CHR_REASON_INVALIDMESSAGE;
						call->callState = CHR_CALL_CLEAR;
					}
					return CHR_FAILED;
				}

				dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

				pChannel = chrFindLogicalChannelByOLC(call, olc);
				if (!pChannel)
				{
					CHRTRACEERR4("ERROR: Logical Channel %d not found, fast start. "
						"(%s, %s)\n",
						olc->forwardLogicalChannelNumber, call->callType,
						call->callToken);
					return CHR_FAILED;
				}
				if (pChannel->channelNo != olc->forwardLogicalChannelNumber)
				{
					CHRTRACEINFO5("Remote endpoint changed forwardLogicalChannel"
						"Number from %d to %d (%s, %s)\n",
						pChannel->channelNo,
						olc->forwardLogicalChannelNumber, call->callType,
						call->callToken);
					pChannel->channelNo = olc->forwardLogicalChannelNumber;
				}
				if (!strcmp(pChannel->dir, "transmit"))
				{
					if (olc->forwardLogicalChannelParameters.multiplexParameters.t !=
						T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
					{
						CHRTRACEERR4("ERROR:Unknown multiplex parameter type for "
							"channel %d (%s, %s)\n",
							olc->forwardLogicalChannelNumber, call->callType,
							call->callToken);
						continue;
					}

					/* Extract the remote media endpoint address */
					h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
					if (!h2250lcp)
					{
						CHRTRACEERR3("ERROR:Invalid OLC received in fast start. No "
							"forward Logical Channel Parameters found. "
							"(%s, %s)\n", call->callType, call->callToken);
						return CHR_FAILED;
					}
					if (!h2250lcp->m.mediaChannelPresent)
					{
						CHRTRACEERR3("ERROR:Invalid OLC received in fast start. No "
							"reverse media channel information found."
							"(%s, %s)\n", call->callType, call->callToken);
						return CHR_FAILED;
					}
					ret = chrGetIpPortFromH245TransportAddress(call,
						&h2250lcp->mediaChannel, pChannel->remoteIP,
						&pChannel->remoteMediaPort);

					if (ret != CHR_OK)
					{
						CHRTRACEERR3("ERROR:Unsupported media channel address type "
							"(%s, %s)\n", call->callType, call->callToken);
						return CHR_FAILED;
					}

					if (!pChannel->chanCap->startTransmitChannel)
					{
						CHRTRACEERR3("ERROR:No callback registered to start transmit "
							"channel (%s, %s)\n", call->callType,
							call->callToken);
						return CHR_FAILED;
					}
					pChannel->chanCap->startTransmitChannel(call, pChannel);
				}
				/* Mark the current channel as established and close all other
				logical channels with same session id and in same direction.
				*/
				chrOnLogicalChannelEstablished(call, pChannel);
			}
			finishPrint();
			removeEventHandler(call->pctxt);
			CHR_SETFLAG(call->flags, CHR_M_FASTSTARTANSWERED);
		}

	}

	/* Retrieve the H.245 control channel address from the connect msg */
	if (q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
		q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
		alerting->m.h245AddressPresent) {
		CHRTRACEINFO3("Tunneling and h245address provided."
			"Giving preference to Tunneling (%s, %s)\n",
			call->callType, call->callToken);
	}
	else if (alerting->m.h245AddressPresent)
	{
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
		{
			CHR_CLRFLAG(call->flags, CHR_M_TUNNELING);
			CHRTRACEINFO3("Tunneling is disabled for call as H245 address is "
				"provided in Alerting message (%s, %s)\n",
				call->callType, call->callToken);
		}
		ret = chrH323GetIpPortFromH225TransportAddress(call,
			&alerting->h245Address, call->remoteIP,
			&call->remoteH245Port);
		if (ret != CHR_OK)
		{
			CHRTRACEERR3("Error: Unknown H245 address type in received "
				"Alerting message (%s, %s)", call->callType,
				call->callToken);
			/* Mark call for clearing */
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_INVALIDMESSAGE;
				call->callState = CHR_CALL_CLEAR;
			}
			return CHR_FAILED;
		}
	}
	return CHR_OK;
}


int chrOnReceivedSignalConnect(CHRH323CallData* call, Q931Message *q931Msg)
{
	int ret, i;
	H225Connect_UUIE *connect;
	H245OpenLogicalChannel* olc;
	ASN1OCTET msgbuf[MAXMSGLEN];
	CHRLogicalChannel * pChannel = NULL;
	H245H2250LogicalChannelParameters * h2250lcp = NULL;

	if (!q931Msg->userInfo)
	{
		CHRTRACEERR3("Error: UUIE not found in received H.225 Connect message"
			" (%s, %s)\n", call->callType, call->callToken);
		/* Mark call for clearing */
		if (call->callState < CHR_CALL_CLEAR)
		{
			call->callEndReason = CHR_REASON_INVALIDMESSAGE;
			call->callState = CHR_CALL_CLEAR;
		}
		return CHR_FAILED;
	}
	/* Retrieve the connect message from the user-user IE & Q.931 header */
	connect = q931Msg->userInfo->h323_uu_pdu.h323_message_body.u.connect;
	if (connect == NULL)
	{
		CHRTRACEERR3("Error: Received Connect message does not have Connect UUIE"
			" (%s, %s)\n", call->callType, call->callToken);
		/* Mark call for clearing */
		if (call->callState < CHR_CALL_CLEAR)
		{
			call->callEndReason = CHR_REASON_INVALIDMESSAGE;
			call->callState = CHR_CALL_CLEAR;
		}
		return CHR_FAILED;
	}

	/*Handle fast-start */
	if (CHR_TESTFLAG(call->flags, CHR_M_FASTSTART) &&
		!CHR_TESTFLAG(call->flags, CHR_M_FASTSTARTANSWERED))
	{
		if (!connect->m.fastStartPresent)
		{
			CHRTRACEINFO3("Remote endpoint has rejected fastStart. (%s, %s)\n",
				call->callType, call->callToken);
			/* Clear all channels we might have created */
			chrClearAllLogicalChannels(call);
			CHR_CLRFLAG(call->flags, CHR_M_FASTSTART);
		}
	}

	if (connect->m.fastStartPresent &&
		!CHR_TESTFLAG(call->flags, CHR_M_FASTSTARTANSWERED))
	{
		/* For printing the decoded message to log, initialize handler. */
		initializePrintHandler(&printHandler, "FastStart Elements");

		/* Set print handler */
		setEventHandler(call->pctxt, &printHandler);

		for (i = 0; i<(int)connect->fastStart.n; i++)
		{
			olc = NULL;
			/* memset(msgbuf, 0, sizeof(msgbuf));*/
			olc = (H245OpenLogicalChannel*)memAlloc(call->pctxt,
				sizeof(H245OpenLogicalChannel));
			if (!olc)
			{
				CHRTRACEERR3("ERROR:Memory - chrOnReceivedSignalConnect - olc"
					"(%s, %s)\n", call->callType, call->callToken);
				/*Mark call for clearing */
				if (call->callState < CHR_CALL_CLEAR)
				{
					call->callEndReason = CHR_REASON_LOCAL_CLEARED;
					call->callState = CHR_CALL_CLEAR;
				}
				finishPrint();
				removeEventHandler(call->pctxt);
				return CHR_FAILED;
			}
			memset(olc, 0, sizeof(H245OpenLogicalChannel));
			memcpy(msgbuf, connect->fastStart.elem[i].data,
				connect->fastStart.elem[i].numocts);
			setPERBuffer(call->pctxt, msgbuf,
				connect->fastStart.elem[i].numocts, 1);
			ret = asn1PD_H245OpenLogicalChannel(call->pctxt, olc);
			if (ret != ASN_OK)
			{
				CHRTRACEERR3("ERROR:Failed to decode fast start olc element "
					"(%s, %s)\n", call->callType, call->callToken);
				/* Mark call for clearing */
				if (call->callState < CHR_CALL_CLEAR)
				{
					call->callEndReason = CHR_REASON_INVALIDMESSAGE;
					call->callState = CHR_CALL_CLEAR;
				}
				finishPrint();
				removeEventHandler(call->pctxt);
				return CHR_FAILED;
			}

			dListAppend(call->pctxt, &call->remoteFastStartOLCs, olc);

			pChannel = chrFindLogicalChannelByOLC(call, olc);
			if (!pChannel)
			{
				CHRTRACEERR4("ERROR: Logical Channel %d not found, fasts start "
					"answered. (%s, %s)\n",
					olc->forwardLogicalChannelNumber, call->callType,
					call->callToken);
				finishPrint();
				removeEventHandler(call->pctxt);
				return CHR_FAILED;
			}
			if (pChannel->channelNo != olc->forwardLogicalChannelNumber)
			{
				CHRTRACEINFO5("Remote endpoint changed forwardLogicalChannelNumber"
					"from %d to %d (%s, %s)\n", pChannel->channelNo,
					olc->forwardLogicalChannelNumber, call->callType,
					call->callToken);
				pChannel->channelNo = olc->forwardLogicalChannelNumber;
			}
			if (!strcmp(pChannel->dir, "transmit"))
			{
				if (olc->forwardLogicalChannelParameters.multiplexParameters.t !=
					T_H245OpenLogicalChannel_forwardLogicalChannelParameters_multiplexParameters_h2250LogicalChannelParameters)
				{
					CHRTRACEERR4("ERROR:Unknown multiplex parameter type for channel"
						" %d (%s, %s)\n", olc->forwardLogicalChannelNumber,
						call->callType, call->callToken);
					continue;
				}

				/* Extract the remote media endpoint address */
				h2250lcp = olc->forwardLogicalChannelParameters.multiplexParameters.u.h2250LogicalChannelParameters;
				if (!h2250lcp)
				{
					CHRTRACEERR3("ERROR:Invalid OLC received in fast start. No "
						"forward Logical Channel Parameters found. (%s, %s)"
						"\n", call->callType, call->callToken);
					finishPrint();
					removeEventHandler(call->pctxt);
					return CHR_FAILED;
				}
				if (!h2250lcp->m.mediaChannelPresent)
				{
					CHRTRACEERR3("ERROR:Invalid OLC received in fast start. No "
						"reverse media channel information found. (%s, %s)"
						"\n", call->callType, call->callToken);
					finishPrint();
					removeEventHandler(call->pctxt);
					return CHR_FAILED;
				}

				ret = chrGetIpPortFromH245TransportAddress(call,
					&h2250lcp->mediaChannel, pChannel->remoteIP,
					&pChannel->remoteMediaPort);
				if (ret != CHR_OK)
				{
					CHRTRACEERR3("ERROR:Unsupported media channel address type "
						"(%s, %s)\n", call->callType, call->callToken);
					finishPrint();
					removeEventHandler(call->pctxt);
					return CHR_FAILED;
				}
				if (!pChannel->chanCap->startTransmitChannel)
				{
					CHRTRACEERR3("ERROR:No callback registered to start transmit "
						"channel (%s, %s)\n", call->callType, call->callToken);
					finishPrint();
					removeEventHandler(call->pctxt);
					return CHR_FAILED;
				}
				pChannel->chanCap->startTransmitChannel(call, pChannel);
			}
			/* Mark the current channel as established and close all other
			logical channels with same session id and in same direction.
			*/
			chrOnLogicalChannelEstablished(call, pChannel);
		}
		finishPrint();
		removeEventHandler(call->pctxt);
		CHR_SETFLAG(call->flags, CHR_M_FASTSTARTANSWERED);
	}

	/* Retrieve the H.245 control channel address from the connect msg */
	if (q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent &&
		q931Msg->userInfo->h323_uu_pdu.h245Tunneling &&
		connect->m.h245AddressPresent) {
		CHRTRACEINFO3("Tunneling and h245address provided."
			"Giving preference to Tunneling (%s, %s)\n",
			call->callType, call->callToken);
	}
	else if (connect->m.h245AddressPresent)
	{
		if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
		{
			CHR_CLRFLAG(call->flags, CHR_M_TUNNELING);
			CHRTRACEINFO3("Tunneling is disabled for call as H245 address is "
				"provided in connect message (%s, %s)\n",
				call->callType, call->callToken);
		}
		ret = chrH323GetIpPortFromH225TransportAddress(call,
			&connect->h245Address, call->remoteIP, &call->remoteH245Port);
		if (ret != CHR_OK)
		{
			CHRTRACEERR3("Error: Unknown H245 address type in received Connect "
				"message (%s, %s)", call->callType, call->callToken);
			/* Mark call for clearing */
			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_INVALIDMESSAGE;
				call->callState = CHR_CALL_CLEAR;
			}
			return CHR_FAILED;
		}
	}

	if (call->remoteH245Port != 0)
	{
		/* Create an H.245 connection.
		*/
		if (chrCreateH245Connection(call) == CHR_FAILED)
		{
			CHRTRACEERR3("Error: H.245 channel creation failed (%s, %s)\n",
				call->callType, call->callToken);

			if (call->callState < CHR_CALL_CLEAR)
			{
				call->callEndReason = CHR_REASON_TRANSPORTFAILURE;
				call->callState = CHR_CALL_CLEAR;
			}
			return CHR_FAILED;
		}
	}

	if (q931Msg->userInfo->h323_uu_pdu.m.h245TunnelingPresent)
	{
		if (!q931Msg->userInfo->h323_uu_pdu.h245Tunneling)
		{
			if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
			{
				CHR_CLRFLAG(call->flags, CHR_M_TUNNELING);
				CHRTRACEINFO3("Tunneling is disabled by remote endpoint.(%s, %s)\n",
					call->callType, call->callToken);
			}
		}
	}
	if (CHR_TESTFLAG(call->flags, CHR_M_TUNNELING))
	{
		CHRTRACEDBG3("Handling tunneled messages in CONNECT. (%s, %s)\n",
			call->callType, call->callToken);
		ret = chrHandleTunneledH245Messages
			(call, &q931Msg->userInfo->h323_uu_pdu);
		CHRTRACEDBG3("Finished tunneled messages in Connect. (%s, %s)\n",
			call->callType, call->callToken);

		/*
		Send TCS as call established and no capability exchange has yet
		started. This will be true only when separate h245 connection is not
		established and tunneling is being used.
		*/
		if (call->localTermCapState == CHR_LocalTermCapExchange_Idle)
		{
			/*Start terminal capability exchange and master slave determination */
			ret = chrSendTermCapMsg(call);
			if (ret != CHR_OK)
			{
				CHRTRACEERR3("ERROR:Sending Terminal capability message (%s, %s)\n",
					call->callType, call->callToken);
				return ret;
			}
		}
		if (call->masterSlaveState == CHR_MasterSlave_Idle)
		{
			ret = chrSendMasterSlaveDetermination(call);
			if (ret != CHR_OK)
			{
				CHRTRACEERR3("ERROR:Sending Master-slave determination message "
					"(%s, %s)\n", call->callType, call->callToken);
				return ret;
			}
		}

	}
	return CHR_OK;
}

int chrHandleH2250Message(CHRH323CallData *call, Q931Message *q931Msg)
{
	int ret = CHR_OK;
	ASN1UINT i;
	DListNode *pNode = NULL;
	CHRTimer *pTimer = NULL;
	int type = q931Msg->messageType;
	switch (type)
	{
	case Q931SetupMsg: /* SETUP message is received */
		CHRTRACEINFO3("Received SETUP message (%s, %s)\n", call->callType,
			call->callToken);
		chrOnReceivedSetup(call, q931Msg);

		/* H225 message callback */
		if (gH323ep.h225Callbacks.onReceivedSetup)
			gH323ep.h225Callbacks.onReceivedSetup(call, q931Msg);

		/* Free up the mem used by the received message, as it's processing
		is done.
		*/
		chrFreeQ931Message(q931Msg);

		chrSendCallProceeding(call);/* Send call proceeding message*/


		ret = chrH323CallAdmitted(call);
		break;


	case Q931CallProceedingMsg: /* CALL PROCEEDING message is received */
		CHRTRACEINFO3("H.225 Call Proceeding message received (%s, %s)\n",
			call->callType, call->callToken);
		chrOnReceivedCallProceeding(call, q931Msg);

		chrFreeQ931Message(q931Msg);
		break;

	case Q931AlertingMsg:/* Alerting message received */
		CHRTRACEINFO3("H.225 Alerting message received (%s, %s)\n",
			call->callType, call->callToken);

		chrOnReceivedAlerting(call, q931Msg);

		if (gH323ep.h323Callbacks.onAlerting && call->callState<CHR_CALL_CLEAR)
			gH323ep.h323Callbacks.onAlerting(call);
		chrFreeQ931Message(q931Msg);
		break;

	case Q931ConnectMsg:/* Connect message received */
		CHRTRACEINFO3("H.225 Connect message received (%s, %s)\n",
			call->callType, call->callToken);

		/* Disable call establishment timer */
		for (i = 0; i<call->timerList.count; i++)
		{
			pNode = dListFindByIndex(&call->timerList, i);
			pTimer = (CHRTimer*)pNode->data;
			if (((chrTimerCallback*)pTimer->cbData)->timerType &
				CHR_CALLESTB_TIMER)
			{
				memFreePtr(call->pctxt, pTimer->cbData);
				chrTimerDelete(call->pctxt, &call->timerList, pTimer);
				CHRTRACEDBG3("Deleted CallESTB timer. (%s, %s)\n",
					call->callType, call->callToken);
				break;
			}
		}
		ret = chrOnReceivedSignalConnect(call, q931Msg);
		if (ret != CHR_OK)
			CHRTRACEERR3("Error:Invalid Connect message received. (%s, %s)\n",
			call->callType, call->callToken);
		else{
			/* H225 message callback */
			if (gH323ep.h225Callbacks.onReceivedConnect)
				gH323ep.h225Callbacks.onReceivedConnect(call, q931Msg);

			if (gH323ep.h323Callbacks.onCallEstablished)
				gH323ep.h323Callbacks.onCallEstablished(call);
		}
		chrFreeQ931Message(q931Msg);
		break;

	case Q931InformationMsg:
		CHRTRACEINFO3("H.225 Information msg received (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeQ931Message(q931Msg);
		break;

	case Q931ReleaseCompleteMsg:/* Release complete message received */
		CHRTRACEINFO3("H.225 Release Complete message received (%s, %s)\n",
			call->callType, call->callToken);

		chrOnReceivedReleaseComplete(call, q931Msg);

		chrFreeQ931Message(q931Msg);
		break;

	case Q931ProgressMsg:
		CHRTRACEINFO3("H.225 Progress message received (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeQ931Message(q931Msg);
		break;

	case Q931StatusMsg:
		CHRTRACEINFO3("H.225 Status message received (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeQ931Message(q931Msg);
		break;

	case Q931StatusEnquiryMsg:
		CHRTRACEINFO3("H.225 Status Inquiry message Received (%s, %s)\n",
			call->callType, call->callToken);

		chrFreeQ931Message(q931Msg);

		/* Send status response */
		chrSendStatus(call);  /* Send status message */

		break;

	case Q931SetupAckMsg:
		CHRTRACEINFO3("H.225 Setup Ack message received (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeQ931Message(q931Msg);
		break;

	case Q931NotifyMsg:
		CHRTRACEINFO3("H.225 Notify message Received (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeQ931Message(q931Msg);
		break;

	default:
		CHRTRACEWARN3("Invalid H.225 message type received (%s, %s)\n",
			call->callType, call->callToken);
		chrFreeQ931Message(q931Msg);
	}
	return ret;
}


int chrHandleStartH245FacilityMessage
(CHRH323CallData *call, H225Facility_UUIE *facility)
{
	H225TransportAddress_ipAddress *ipAddress = NULL;
	int ret;

	/* Extract H245 address */
	if (!facility->m.h245AddressPresent)
	{
		CHRTRACEERR3("ERROR: startH245 facility message received with no h245 "
			"address (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	if (facility->h245Address.t != T_H225TransportAddress_ipAddress)
	{
		CHRTRACEERR3("ERROR:Unknown H245 address type in received startH245 "
			"facility message (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}
	ipAddress = facility->h245Address.u.ipAddress;
	if (!ipAddress)
	{
		CHRTRACEERR3("ERROR:Invalid startH245 facility message. No H245 ip "
			"address found. (%s, %s)\n", call->callType, call->callToken);
		return CHR_FAILED;
	}

	sprintf(call->remoteIP, "%d.%d.%d.%d", ipAddress->ip.data[0],
		ipAddress->ip.data[1],
		ipAddress->ip.data[2],
		ipAddress->ip.data[3]);
	call->remoteH245Port = ipAddress->port;

	/* disable tunneling for this call */
	CHR_CLRFLAG(call->flags, CHR_M_TUNNELING);

	/*Establish an H.245 connection */
	ret = chrCreateH245Connection(call);
	if (ret != CHR_OK)
	{
		CHRTRACEERR3("ERROR: Failed to establish an H.245 connection with remote"
			" endpoint (%s, %s)\n", call->callType, call->callToken);
		return ret;
	}
	return CHR_OK;
}

int chrHandleTunneledH245Messages
(CHRH323CallData *call, H225H323_UU_PDU * pH323UUPdu)
{
	H245Message *pmsg;
	OOCTXT *pctxt = &gH323ep.msgctxt;
	int ret = 0, i = 0;

	CHRTRACEDBG3("Checking for tunneled H.245 messages (%s, %s)\n",
		call->callType, call->callToken);

	/* Check whether there are tunneled messages */
	if (pH323UUPdu->m.h245TunnelingPresent)
	{
		if (pH323UUPdu->h245Tunneling)
		{
			CHRTRACEDBG4("Total number of tunneled H245 messages are %d.(%s, %s)"
				"\n", (int)pH323UUPdu->h245Control.n, call->callType,
				call->callToken);
			for (i = 0; i< (int)pH323UUPdu->h245Control.n; i++)
			{
				CHRTRACEDBG5("Retrieving %d of %d tunneled H.245 messages."
					"(%s, %s)\n", i + 1, pH323UUPdu->h245Control.n,
					call->callType, call->callToken);
				pmsg = (H245Message*)memAlloc(pctxt, sizeof(H245Message));
				if (!pmsg)
				{
					CHRTRACEERR3("Error:Memory - chrHandleH245TunneledMessages - pmsg"
						"(%s, %s)\n", call->callType, call->callToken);
					return CHR_FAILED;
				}

				setPERBuffer(pctxt,
					(ASN1OCTET*)pH323UUPdu->h245Control.elem[i].data,
					pH323UUPdu->h245Control.elem[i].numocts, 1);

				initializePrintHandler(&printHandler, "Tunneled H.245 Message");
				memset(pmsg, 0, sizeof(H245Message));
				/* Set event handler */
				setEventHandler(pctxt, &printHandler);
				CHRTRACEDBG4("Decoding %d tunneled H245 message. (%s, %s)\n",
					i + 1, call->callType, call->callToken);
				ret = asn1PD_H245MultimediaSystemControlMessage(pctxt,
					&(pmsg->h245Msg));
				if (ret != ASN_OK)
				{
					CHRTRACEERR3("Error decoding H245 message (%s, %s)\n",
						call->callType, call->callToken);
					chrFreeH245Message(call, pmsg);
					return CHR_FAILED;
				}
				finishPrint();
				removeEventHandler(pctxt);
				chrHandleH245Message(call, pmsg);
				memFreePtr(pctxt, pmsg);
				pmsg = NULL;
			}/* End of For lchrp */
		}/* End of if(h245Tunneling) */
	}
	return CHR_OK;
}

int chrH323RetrieveAliases
(CHRH323CallData *call, H225_SeqOfH225AliasAddress *pAddresses,
CHRAliases **aliasList)
{
	int i = 0, j = 0, k = 0;
	DListNode* pNode = NULL;
	H225AliasAddress *pAliasAddress = NULL;
	CHRAliases *newAlias = NULL;
	H225TransportAddress *pTransportAddrss = NULL;

	if (!pAddresses)
	{
		CHRTRACEWARN3("Warn:No Aliases present (%s, %s)\n", call->callType,
			call->callToken);
		return CHR_OK;
	}
	/* check for aliases */
	if (pAddresses->count <= 0)
		return CHR_OK;

	for (i = 0; i<(int)pAddresses->count; i++)
	{
		pNode = dListFindByIndex(pAddresses, i);

		if (!pNode)
			continue;

		pAliasAddress = (H225AliasAddress*)pNode->data;

		if (!pAliasAddress)
			continue;

		newAlias = (CHRAliases*)memAlloc(call->pctxt, sizeof(CHRAliases));
		if (!newAlias)
		{
			CHRTRACEERR3("ERROR:Memory - chrH323RetrieveAliases - newAlias "
				"(%s, %s)\n", call->callType, call->callToken);
			return CHR_FAILED;
		}
		memset(newAlias, 0, sizeof(CHRAliases));
		switch (pAliasAddress->t)
		{
		case T_H225AliasAddress_dialedDigits:
			newAlias->type = T_H225AliasAddress_dialedDigits;
			newAlias->value = (char*)memAlloc(call->pctxt,
				strlen(pAliasAddress->u.dialedDigits)*sizeof(char)+1);
			if (!newAlias->value)
			{
				CHRTRACEERR3("ERROR:Memory - chrH323RetrieveAliases - "
					"newAlias->value(dialedDigits) (%s, %s)\n",
					call->callType, call->callToken);
				memFreePtr(call->pctxt, newAlias);
				return CHR_FAILED;
			}

			memcpy(newAlias->value, pAliasAddress->u.dialedDigits,
				strlen(pAliasAddress->u.dialedDigits)*sizeof(char));
			newAlias->value[strlen(pAliasAddress->u.dialedDigits)*sizeof(char)] = '\0';
			break;
		case T_H225AliasAddress_h323_ID:
			newAlias->type = T_H225AliasAddress_h323_ID;
			newAlias->value = (char*)memAlloc(call->pctxt,
				(pAliasAddress->u.h323_ID.nchars + 1)*sizeof(char)+1);
			if (!newAlias->value)
			{
				CHRTRACEERR3("ERROR:Memory - chrH323RetrieveAliases - "
					"newAlias->value(h323id) (%s, %s)\n", call->callType,
					call->callToken);
				memFreePtr(call->pctxt, newAlias);
				return CHR_FAILED;
			}

			for (j = 0, k = 0; j<(int)pAliasAddress->u.h323_ID.nchars; j++)
			{
				if (pAliasAddress->u.h323_ID.data[j] < 256)
				{
					newAlias->value[k++] = (char)pAliasAddress->u.h323_ID.data[j];
				}
			}
			newAlias->value[k] = '\0';
			break;
		case T_H225AliasAddress_url_ID:
			newAlias->type = T_H225AliasAddress_url_ID;
			newAlias->value = (char*)memAlloc(call->pctxt,
				strlen(pAliasAddress->u.url_ID)*sizeof(char)+1);
			if (!newAlias->value)
			{
				CHRTRACEERR3("ERROR:Memory - chrH323RetrieveAliases - "
					"newAlias->value(urlid) (%s, %s)\n", call->callType,
					call->callToken);
				memFreePtr(call->pctxt, newAlias);
				return CHR_FAILED;
			}

			memcpy(newAlias->value, pAliasAddress->u.url_ID,
				strlen(pAliasAddress->u.url_ID)*sizeof(char));
			newAlias->value[strlen(pAliasAddress->u.url_ID)*sizeof(char)] = '\0';
			break;
		case T_H225AliasAddress_transportID:
			newAlias->type = T_H225AliasAddress_transportID;
			pTransportAddrss = pAliasAddress->u.transportID;
			if (pTransportAddrss->t != T_H225TransportAddress_ipAddress)
			{
				CHRTRACEERR3("Error:Alias transportID not an IP address"
					"(%s, %s)\n", call->callType, call->callToken);
				memFreePtr(call->pctxt, newAlias);
				break;
			}
			/* hopefully ip:port value can't exceed more than 30
			characters */
			newAlias->value = (char*)memAlloc(call->pctxt,
				30 * sizeof(char));
			sprintf(newAlias->value, "%d.%d.%d.%d:%d",
				pTransportAddrss->u.ipAddress->ip.data[0],
				pTransportAddrss->u.ipAddress->ip.data[1],
				pTransportAddrss->u.ipAddress->ip.data[2],
				pTransportAddrss->u.ipAddress->ip.data[3],
				pTransportAddrss->u.ipAddress->port);
			break;
		case T_H225AliasAddress_email_ID:
			newAlias->type = T_H225AliasAddress_email_ID;
			newAlias->value = (char*)memAlloc(call->pctxt,
				strlen(pAliasAddress->u.email_ID)*sizeof(char)+1);
			if (!newAlias->value)
			{
				CHRTRACEERR3("ERROR:Memory - chrH323RetrieveAliases - "
					"newAlias->value(emailid) (%s, %s)\n", call->callType,
					call->callToken);
				memFreePtr(call->pctxt, newAlias);
				return CHR_FAILED;
			}

			memcpy(newAlias->value, pAliasAddress->u.email_ID,
				strlen(pAliasAddress->u.email_ID)*sizeof(char));
			newAlias->value[strlen(pAliasAddress->u.email_ID)*sizeof(char)] = '\0';
			break;
		default:
			CHRTRACEERR3("Error:Unhandled Alias type (%s, %s)\n",
				call->callType, call->callToken);
			memFreePtr(call->pctxt, newAlias);
			continue;
		}

		newAlias->next = *aliasList;
		*aliasList = newAlias;

		newAlias = NULL;

		pAliasAddress = NULL;
		pNode = NULL;
	}/* endof: for */
	return CHR_OK;
}


int chrPopulateAliasList(OOCTXT *pctxt, CHRAliases *pAliases,
	H225_SeqOfH225AliasAddress *pAliasList)
{
	H225AliasAddress *pAliasEntry = NULL;
	CHRAliases * pAlias = NULL;
	ASN1BOOL bValid = FALSE;
	int i = 0;

	dListInit(pAliasList);
	if (pAliases)
	{
		pAlias = pAliases;
		while (pAlias)
		{
			pAliasEntry = (H225AliasAddress*)memAlloc(pctxt,
				sizeof(H225AliasAddress));
			if (!pAliasEntry)
			{
				CHRTRACEERR1("ERROR:Memory - chrPopulateAliasList - pAliasEntry\n");
				return CHR_FAILED;
			}
			switch (pAlias->type)
			{
			case T_H225AliasAddress_dialedDigits:
				pAliasEntry->t = T_H225AliasAddress_dialedDigits;
				pAliasEntry->u.dialedDigits = (ASN1IA5String)memAlloc(pctxt,
					strlen(pAlias->value) + 1);
				if (!pAliasEntry->u.dialedDigits)
				{
					CHRTRACEERR1("ERROR:Memory - chrPopulateAliasList - "
						"dialedDigits\n");
					memFreePtr(pctxt, pAliasEntry);
					return CHR_FAILED;
				}
				strcpy((char*)pAliasEntry->u.dialedDigits, pAlias->value);
				bValid = TRUE;
				break;
			case T_H225AliasAddress_h323_ID:
				pAliasEntry->t = T_H225AliasAddress_h323_ID;
				pAliasEntry->u.h323_ID.nchars = strlen(pAlias->value);
				pAliasEntry->u.h323_ID.data = (ASN116BITCHAR*)memAllocZ
					(pctxt, strlen(pAlias->value)*sizeof(ASN116BITCHAR));

				if (!pAliasEntry->u.h323_ID.data)
				{
					CHRTRACEERR1("ERROR:Memory - chrPopulateAliasList - h323_id\n");
					memFreePtr(pctxt, pAliasEntry);
					return CHR_FAILED;
				}
				for (i = 0; *(pAlias->value + i) != '\0'; i++)
					pAliasEntry->u.h323_ID.data[i] = (ASN116BITCHAR)pAlias->value[i];
				bValid = TRUE;
				break;
			case T_H225AliasAddress_url_ID:
				pAliasEntry->t = T_H225AliasAddress_url_ID;
				pAliasEntry->u.url_ID = (ASN1IA5String)memAlloc(pctxt,
					strlen(pAlias->value) + 1);
				if (!pAliasEntry->u.url_ID)
				{
					CHRTRACEERR1("ERROR:Memory - chrPopulateAliasList - url_id\n");
					memFreePtr(pctxt, pAliasEntry);
					return CHR_FAILED;
				}
				strcpy((char*)pAliasEntry->u.url_ID, pAlias->value);
				bValid = TRUE;
				break;
			case T_H225AliasAddress_email_ID:
				pAliasEntry->t = T_H225AliasAddress_email_ID;
				pAliasEntry->u.email_ID = (ASN1IA5String)memAlloc(pctxt,
					strlen(pAlias->value) + 1);
				if (!pAliasEntry->u.email_ID)
				{
					CHRTRACEERR1("ERROR: Failed to allocate memory for EmailID "
						"alias entry \n");
					return CHR_FAILED;
				}
				strcpy((char*)pAliasEntry->u.email_ID, pAlias->value);
				bValid = TRUE;
				break;
			default:
				CHRTRACEERR1("ERROR: Unhandled alias type\n");
				bValid = FALSE;
			}

			if (bValid)
				dListAppend(pctxt, pAliasList, (void*)pAliasEntry);
			else
				memFreePtr(pctxt, pAliasEntry);

			pAlias = pAlias->next;
		}
	}
	return CHR_OK;
}


CHRAliases* chrH323GetAliasFromList(CHRAliases *aliasList, int type, char *value)
{

	CHRAliases *pAlias = NULL;

	if (!aliasList)
	{
		CHRTRACEDBG1("No alias List to search\n");
		return NULL;
	}

	pAlias = aliasList;

	while (pAlias)
	{
		if (type != 0 && value) { /* Search by type and value */
			if (pAlias->type == type && !strcmp(pAlias->value, value))
			{
				return pAlias;
			}
		}
		else if (type != 0 && !value) {/* search by type */
			if (pAlias->type == type)
				return pAlias;
		}
		else if (type == 0 && value) {/* search by value */
			if (!strcmp(pAlias->value, value))
				return pAlias;
		}
		else {
			CHRTRACEDBG1("No criteria to search the alias list\n");
			return NULL;
		}
		pAlias = pAlias->next;
	}

	return NULL;
}

CHRAliases* chrH323AddAliasToList
(CHRAliases **pAliasList, OOCTXT *pctxt, H225AliasAddress *pAliasAddress)
{
	int j = 0, k = 0;
	CHRAliases *newAlias = NULL;
	H225TransportAddress *pTransportAddrss = NULL;

	newAlias = (CHRAliases*)memAlloc(pctxt, sizeof(CHRAliases));
	if (!newAlias)
	{
		CHRTRACEERR1("Error: Failed to allocate memory for new alias to be added to the alias list\n");
		return NULL;
	}
	memset(newAlias, 0, sizeof(CHRAliases));

	switch (pAliasAddress->t)
	{
	case T_H225AliasAddress_dialedDigits:
		newAlias->type = T_H225AliasAddress_dialedDigits;
		newAlias->value = (char*)memAlloc(pctxt, strlen(pAliasAddress->u.dialedDigits)*sizeof(char)+1);
		strcpy(newAlias->value, pAliasAddress->u.dialedDigits);
		break;
	case T_H225AliasAddress_h323_ID:
		newAlias->type = T_H225AliasAddress_h323_ID;
		newAlias->value = (char*)memAlloc(pctxt,
			(pAliasAddress->u.h323_ID.nchars + 1)*sizeof(char)+1);

		for (j = 0, k = 0; j<(int)pAliasAddress->u.h323_ID.nchars; j++)
		{
			if (pAliasAddress->u.h323_ID.data[j] < 256)
			{
				newAlias->value[k++] = (char)pAliasAddress->u.h323_ID.data[j];
			}
		}
		newAlias->value[k] = '\0';
		break;
	case T_H225AliasAddress_url_ID:
		newAlias->type = T_H225AliasAddress_url_ID;
		newAlias->value = (char*)memAlloc(pctxt,
			strlen(pAliasAddress->u.url_ID)*sizeof(char)+1);

		strcpy(newAlias->value, pAliasAddress->u.url_ID);
		break;
	case T_H225AliasAddress_transportID:
		newAlias->type = T_H225AliasAddress_transportID;
		pTransportAddrss = pAliasAddress->u.transportID;
		if (pTransportAddrss->t != T_H225TransportAddress_ipAddress)
		{
			CHRTRACEERR1("Error:Alias transportID not an IP address\n");
			memFreePtr(pctxt, newAlias);
			return NULL;
		}
		/* hopefully ip:port value can't exceed more than 30
		characters */
		newAlias->value = (char*)memAlloc(pctxt,
			30 * sizeof(char));
		sprintf(newAlias->value, "%d.%d.%d.%d:%d",
			pTransportAddrss->u.ipAddress->ip.data[0],
			pTransportAddrss->u.ipAddress->ip.data[1],
			pTransportAddrss->u.ipAddress->ip.data[2],
			pTransportAddrss->u.ipAddress->ip.data[3],
			pTransportAddrss->u.ipAddress->port);
		break;
	case T_H225AliasAddress_email_ID:
		newAlias->type = T_H225AliasAddress_email_ID;
		newAlias->value = (char*)memAlloc(pctxt,
			strlen(pAliasAddress->u.email_ID)*sizeof(char)+1);

		strcpy(newAlias->value, pAliasAddress->u.email_ID);
		break;
	default:
		CHRTRACEERR1("Error:Unhandled Alias type \n");
		memFreePtr(pctxt, newAlias);
		return NULL;

	}
	newAlias->next = *pAliasList;
	*pAliasList = newAlias;
	return newAlias;
}

int chrH323GetIpPortFromH225TransportAddress(struct CHRH323CallData *call,
	H225TransportAddress *h225Address, char *ip, int *port)
{
	if (h225Address->t != T_H225TransportAddress_ipAddress)
	{
		CHRTRACEERR3("Error: Unknown H225 address type. (%s, %s)", call->callType,
			call->callToken);
		return CHR_FAILED;
	}
	sprintf(ip, "%d.%d.%d.%d",
		h225Address->u.ipAddress->ip.data[0],
		h225Address->u.ipAddress->ip.data[1],
		h225Address->u.ipAddress->ip.data[2],
		h225Address->u.ipAddress->ip.data[3]);
	*port = h225Address->u.ipAddress->port;
	return CHR_OK;
}
