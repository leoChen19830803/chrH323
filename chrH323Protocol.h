/**
* @file chrH323Protocol.h
* @author chenhaoran
* RAS¡¢Q931¡¢H2250¡¢H245
*/
#ifndef _CHRH323PROTOCOL_H_
#define _CHRH323PROTOCOL_H_

#include "ooasn1.h"
#include "MULTIMEDIA-SYSTEM-CONTROL.h"
#include "H323-MESSAGES.h"
#include "chrTypes.h"
#include "chrSocket.h"
#include "chrCapability.h"
#include "chrChannels.h"


#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif /* EXTERN */

#ifdef _WIN32
#include <process.h>
#define getpid _getpid
#endif

/****************************** RAS ******************************/
//todo:

/****************************** Q931 ******************************/
#define CHR_MAX_NUMBER_LENGTH 50
#define CHR_MAX_CALL_TOKEN 9999 /* Maximum value for a call token identifier */

/** Q931 Message */
#define Q931_E_TOOSHORT         (-1001)/* Q.931 packet must be at least 5 bytes long */
#define Q931_E_INVCALLREF       (-1002)/* callReference field must be 2 bytes long */
#define Q931_E_INVLENGTH        (-1003)/* invalid length of message */
enum Q931MsgTypes 
{
		Q931NationalEscapeMsg = 0x00,
		Q931AlertingMsg = 0x01,
		Q931CallProceedingMsg = 0x02,
		Q931ConnectMsg = 0x07,
		Q931ConnectAckMsg = 0x0f,
		Q931ProgressMsg = 0x03,
		Q931SetupMsg = 0x05,
		Q931SetupAckMsg = 0x0d,
		Q931ResumeMsg = 0x26,
		Q931ResumeAckMsg = 0x2e,
		Q931ResumeRejectMsg = 0x22,
		Q931SuspendMsg = 0x25,
		Q931SuspendAckMsg = 0x2d,
		Q931SuspendRejectMsg = 0x21,
		Q931UserInformationMsg = 0x20,
		Q931DisconnectMsg = 0x45,
		Q931ReleaseMsg = 0x4d,
		Q931ReleaseCompleteMsg = 0x5a,
		Q931RestartMsg = 0x46,
		Q931RestartAckMsg = 0x4e,
		Q931SegmentMsg = 0x60,
		Q931CongestionCtrlMsg = 0x79,
		Q931InformationMsg = 0x7b,
		Q931NotifyMsg = 0x6e,
		Q931StatusMsg = 0x7d,
		Q931StatusEnquiryMsg = 0x75,
		Q931FacilityMsg = 0x62
};

enum Q931IECodes 
{
		Q931BearerCapabilityIE = 0x04,
		Q931CauseIE = 0x08,
		Q931FacilityIE = 0x1c,
		Q931ProgressIndicatorIE = 0x1e,
		Q931CallStateIE = 0x14,
		Q931DisplayIE = 0x28,
		Q931SignalIE = 0x34,
		Q931CallingPartyNumberIE = 0x6c,
		Q931CalledPartyNumberIE = 0x70,
		Q931RedirectingNumberIE = 0x74,
		Q931UserUserIE = 0x7e,
		Q931KeypadIE = 0x2c
};

enum Q931InformationTransferCapability 
{
		Q931TransferSpeech,
		Q931TransferUnrestrictedDigital = 8,
		Q931TransferRestrictedDigital = 9,
		Q931Transfer3_1kHzAudio = 16,
		Q931TrasnferUnrestrictedDigitalWithTones = 17,
		Q931TransferVideo = 24
};

enum Q931CauseValues 
{
		Q931UnallocatedNumber = 0x01,
		Q931NoRouteToNetwork = 0x02,
		Q931NoRouteToDestination = 0x03,
		Q931ChannelUnacceptable = 0x06,
		Q931NormalCallClearing = 0x10,
		Q931UserBusy = 0x11,
		Q931NoResponse = 0x12,
		Q931NoAnswer = 0x13,
		Q931SubscriberAbsent = 0x14,
		Q931CallRejected = 0x15,
		Q931NumberChanged = 0x16,
		Q931Redirection = 0x17,
		Q931DestinationOutOfOrder = 0x1b,
		Q931InvalidNumberFormat = 0x1c,
		Q931NormalUnspecified = 0x1f,
		Q931StatusEnquiryResponse = 0x1e,
		Q931NoCircuitChannelAvailable = 0x22,
		Q931NetworkOutOfOrder = 0x26,
		Q931TemporaryFailure = 0x29,
		Q931Congestion = 0x2a,
		Q931RequestedCircuitUnAvailable = 0x2c,
		Q931ResourcesUnavailable = 0x2f,
		Q931IncompatibleDestination = 0x58,
		Q931ProtocolErrorUnspecified = 0x6f,
		Q931RecoveryOnTimerExpiry = 0x66,
		Q931InvalidCallReference = 0x51,
		Q931ErrorInCauseIE = 0
};

enum Q931SignalInfo 
{
		Q931SignalDialToneOn,
		Q931SignalRingBackToneOn,
		Q931SignalInterceptToneOn,
		Q931SignalNetworkCongestionToneOn,
		Q931SignalBusyToneOn,
		Q931SignalConfirmToneOn,
		Q931SignalAnswerToneOn,
		Q931SignalCallWaitingTone,
		Q931SignalOffhchrkWarningTone,
		Q931SignalPreemptionToneOn,
		Q931SignalTonesOff = 0x3f,
		Q931SignalAlertingPattern0 = 0x40,
		Q931SignalAlertingPattern1,
		Q931SignalAlertingPattern2,
		Q931SignalAlertingPattern3,
		Q931SignalAlertingPattern4,
		Q931SignalAlertingPattern5,
		Q931SignalAlertingPattern6,
		Q931SignalAlertingPattern7,
		Q931SignalAlretingOff = 0x4f,
		Q931SignalErrorInIE = 0x100
};

enum Q931NumberingPlanCodes
{
		Q931UnknownPlan = 0x00,
		Q931ISDNPlan = 0x01,
		Q931DataPlan = 0x03,
		Q931TelexPlan = 0x04,
		Q931NationalStandardPlan = 0x08,
		Q931PrivatePlan = 0x09,
		Q931ReservedPlan = 0x0f
};

enum Q931TypeOfNumberCodes 
{
		Q931UnknownType = 0x00,
		Q931InternationalType = 0x01,
		Q931NationalType = 0x02,
		Q931NetworkSpecificType = 0x03,
		Q931SubscriberType = 0x04,
		Q931AbbreviatedType = 0x06,
		Q931ReservedType = 0x07
};

enum Q931CodingStandard
{
		Q931CCITTStd = 0,
		Q931ReservedInternationalStd,
		Q931NationalStd,
		Q931NetworkStd
};

enum Q931TransferMode 
{
		Q931TransferCircuitMode = 0,   /* 00 */
		Q931TransferPacketMode = 2   /* 10 */
};

enum Q931TransferRate
{
		Q931TransferRatePacketMode = 0x00,  /* 00000 This code shall be used for packet-mode calls */
		Q931TransferRate64Kbps = 0x10,  /* 10000 */
		Q931TransferRate128kbps = 0x11,  /* 10001 */
		Q931TransferRate384kbps = 0x13,  /* 10011 */
		Q931TransferRate1536kbps = 0x15,  /* 10101 */
		Q931TransferRate1920kbps = 0x17,  /* 10111 */
		Q931TransferRateMultirate = 0x18   /* 11000 */
};

enum Q931UserInfoLayer1Protocol
{
		Q931UserInfoLayer1CCITTStdRate = 1,
		Q931UserInfoLayer1G711ULaw,
		Q931UserInfoLayer1G711ALaw,
		Q931UserInfoLayer1G721ADPCM,
		Q931UserInfoLayer1G722G725,
		Q931UserInfoLayer1H261,
		Q931UserInfoLayer1NonCCITTStdRate,
		Q931UserInfoLayer1CCITTStdRateV120,
		Q931UserInfoLayer1X31
};

typedef struct Q931InformationElement 
{
		int discriminator;
		int offset;
		int length;
		ASN1OCTET data[1];
} Q931InformationElement;

typedef struct Q931Message 
{
		ASN1UINT protocolDiscriminator;
		ASN1UINT callReference;
		ASN1BOOL fromDestination;
		ASN1UINT messageType;      /* Q931MsgTypes */
		ASN1UINT tunneledMsgType;  /* The H245 message this message is tunneling*/
		ASN1INT  logicalChannelNo; /* channel number associated with tunneled */
		/* message, 0 if no channel */
		DList ies;
		Q931InformationElement *bearerCapabilityIE;
		Q931InformationElement *callingPartyNumberIE;
		Q931InformationElement *calledPartyNumberIE;
		Q931InformationElement *causeIE;
		Q931InformationElement *keypadIE;
		H225H323_UserInformation *userInfo;
} Q931Message;

typedef struct CHRAliases 
{
		int type;           /*!< H.225 AliasAddress choice option (t value) */
		char *value;        /*!< H.225 AliasAddress value */
		OOBOOL registered;
		struct CHRAliases *next;
} CHRAliases;

#define chrAliases CHRAliases

struct CHRH323CallData;
/** H225 callback functions */
typedef int(*cb_OnReceivedSetup)(struct CHRH323CallData *call, struct Q931Message *pmsg);
typedef int(*cb_OnReceivedConnect)(struct CHRH323CallData *call, struct Q931Message *pmsg);
typedef int(*cb_OnBuiltSetup)(struct CHRH323CallData *call, struct Q931Message *pmsg);
typedef int(*cb_OnBuiltConnect)(struct CHRH323CallData *call, struct Q931Message *pmsg);
typedef struct CHRH225MsgCallbacks 
{
		cb_OnReceivedSetup onReceivedSetup;
		cb_OnReceivedConnect onReceivedConnect;
		cb_OnBuiltSetup onBuiltSetup;
		cb_OnBuiltConnect onBuiltConnect;
} CHRH225MsgCallbacks;
/** Q931Message -> CHRH323CallData */
EXTERN int chrQ931Decode(struct CHRH323CallData *call, Q931Message* msg, int length,ASN1OCTET *data, OOBOOL doCallbacks);
EXTERN Q931InformationElement* chrQ931GetIE(const Q931Message* q931msg, int ieCode);
EXTERN int chrDecodeUUIE(Q931Message *q931Msg);
EXTERN int chrEncodeUUIE(Q931Message *q931msg);

EXTERN void chrQ931Print(const Q931Message* q931msg);
EXTERN int chrCreateQ931Message(Q931Message **msg, int msgType);
EXTERN ASN1USINT chrGenerateCallReference(void);
EXTERN int chrGenerateCallIdentifier(H225CallIdentifier *callid);
EXTERN int chrFreeQ931Message(Q931Message *q931Msg);
EXTERN int chrGetOutgoingQ931Msgbuf(struct CHRH323CallData *call, ASN1OCTET * msgbuf, int* len, int *msgType);
EXTERN int chrSendReleaseComplete(struct CHRH323CallData *call);
EXTERN int chrSendCallProceeding(struct CHRH323CallData *call);
EXTERN int chrSendAlerting(struct CHRH323CallData *call);
EXTERN int chrSendFacility(struct CHRH323CallData *call);
EXTERN int chrSendConnect(struct CHRH323CallData *call);
EXTERN int chrSendStatus(struct CHRH323CallData *call);

EXTERN int chrH323MakeCall(char *dest, char *callToken, chrCallOptions *opts);
int chrH323CallAdmitted(struct CHRH323CallData *call);
EXTERN int chrH323HangCall(char * callToken, CHRCallClearReason reason);
EXTERN int chrAcceptCall(struct CHRH323CallData *call);
EXTERN int chrH323MakeCall_helper(struct CHRH323CallData *call);
int chrParseDestination(struct CHRH323CallData *call, char *dest, char *parsedIP, unsigned len,	CHRAliases** aliasList);
int chrGenerateCallToken(char *callToken, size_t size);
int chrEncodeH225Message(struct CHRH323CallData *call, Q931Message *pq931Msg,ASN1OCTET* msgbuf, size_t size);
int chrCallEstbTimerExpired(void *data);
EXTERN int chrQ931SetKeypadIE(Q931Message *pmsg, const char* data);
EXTERN int chrSetBearerCapabilityIE
		(Q931Message *pmsg, enum Q931CodingStandard codingStandard,
	enum Q931InformationTransferCapability capability,
	enum Q931TransferMode transferMode, enum Q931TransferRate transferRate,
	enum Q931UserInfoLayer1Protocol userInfoLayer1);
EXTERN int chrQ931SetCalledPartyNumberIE
		(Q931Message *pmsg, const char *number, unsigned plan, unsigned type);
EXTERN int chrQ931SetCallingPartyNumberIE
		(Q931Message *pmsg, const char *number, unsigned plan, unsigned type,
		unsigned presentation, unsigned screening);
EXTERN int chrQ931SetCauseIE
		(Q931Message *pmsg, enum Q931CauseValues cause, unsigned coding,
		unsigned location);
EXTERN int chrQ931GetCauseAndReasonCodeFromCallClearReason
		(CHRCallClearReason clearReason, enum Q931CauseValues *cause,
		unsigned *reasonCode);
EXTERN CHRCallClearReason chrGetCallClearReasonFromCauseAndReasonCode
		(enum Q931CauseValues cause, unsigned reasonCode);
EXTERN const char* chrGetMsgTypeText(int msgType);
EXTERN const char* chrGetQ931CauseValueText(int val);

/****************************** H245 ******************************/

struct CHRH323CallData;
typedef struct H245Message {
	H245MultimediaSystemControlMessage h245Msg;
	ASN1UINT msgType;
	ASN1INT  logicalChannelNo;
} H245Message;
EXTERN int chrCreateH245Message(H245Message **msg, int type);
EXTERN int chrFreeH245Message(struct CHRH323CallData *call, H245Message *pmsg);
EXTERN int chrSendH245Msg(struct CHRH323CallData *call, H245Message *msg);
EXTERN int chrGetOutgoingH245Msgbuf(struct CHRH323CallData *call, ASN1OCTET *msgbuf, int *len, int *msgType);
EXTERN int chrSendTermCapMsg(struct CHRH323CallData *call);
EXTERN ASN1UINT chrGenerateStatusDeterminationNumber();
EXTERN int chrHandleMasterSlave(struct CHRH323CallData *call, void * pmsg, int msgType);
EXTERN int chrSendMasterSlaveDetermination(struct CHRH323CallData *call);
EXTERN int chrSendMasterSlaveDeterminationAck(struct CHRH323CallData* call, char * status);
EXTERN int chrSendMasterSlaveDeterminationReject(struct CHRH323CallData* call);
EXTERN int chrHandleMasterSlaveReject(struct CHRH323CallData *call, H245MasterSlaveDeterminationReject* reject);
EXTERN int chrHandleOpenLogicalChannel(struct CHRH323CallData* call, H245OpenLogicalChannel *olc);
EXTERN int chrHandleOpenLogicalChannel_helper(struct CHRH323CallData *call, H245OpenLogicalChannel*olc);
int chrSendOpenLogicalChannelReject(struct CHRH323CallData *call, ASN1UINT channelNum, ASN1UINT cause);
EXTERN int chrOnReceivedOpenLogicalChannelAck(struct CHRH323CallData *call,	H245OpenLogicalChannelAck *olcAck);
int chrOnReceivedOpenLogicalChannelRejected(struct CHRH323CallData *call,H245OpenLogicalChannelReject *olcRejected);
EXTERN int chrSendEndSessionCommand(struct CHRH323CallData *call);
EXTERN int chrSendVideoFastUpdateCommand(struct CHRH323CallData *call);
EXTERN int chrHandleH245Command(struct CHRH323CallData *call, H245CommandMessage *command);
EXTERN int chrOnH245CommandVideoFastUpdate(struct CHRH323CallData *call, H245CommandMessage *command);
EXTERN int chrOnReceivedUserInputIndication(CHRH323CallData *call, H245UserInputIndication *indication);
EXTERN int chrOnReceivedTerminalCapabilitySetAck(struct CHRH323CallData* call);
EXTERN int chrCloseAllLogicalChannels(struct CHRH323CallData *call);
EXTERN int chrSendCloseLogicalChannel(struct CHRH323CallData *call, CHRLogicalChannel *logicalChan);
EXTERN int chrOnReceivedCloseLogicalChannel(struct CHRH323CallData *call,H245CloseLogicalChannel* clc);
EXTERN int chrOnReceivedCloseChannelAck(struct CHRH323CallData* call,H245CloseLogicalChannelAck* clcAck);
EXTERN int chrHandleH245Message(struct CHRH323CallData *call, H245Message * pmsg);
EXTERN int chrOnReceivedTerminalCapabilitySet(struct CHRH323CallData *call, H245Message *pmsg);
EXTERN int chrH245AcknowledgeTerminalCapabilitySet(struct CHRH323CallData *call);
EXTERN int chrOpenLogicalChannels(struct CHRH323CallData *call);
EXTERN int chrOpenLogicalChannel(struct CHRH323CallData *call,enum CHRCapType capType);
EXTERN int chrOpenChannel(struct CHRH323CallData* call, chrH323EpCapability *epCap);
EXTERN int chrSendH245UserInputIndication_alphanumeric(CHRH323CallData *call, const char *data);
EXTERN int chrSendH245UserInputIndication_signal(CHRH323CallData *call, const char *data);
EXTERN int chrSendRequestCloseLogicalChannel(struct CHRH323CallData *call, CHRLogicalChannel *logicalChan);
int chrSendRequestChannelCloseRelease(struct CHRH323CallData *call, int channelNum);
EXTERN int chrOnReceivedRequestChannelClose(struct CHRH323CallData *call, H245RequestChannelClose *rclc);
int chrOnReceivedRequestChannelCloseReject(struct CHRH323CallData *call, H245RequestChannelCloseReject *rccReject);
int chrOnReceivedRequestChannelCloseAck(struct CHRH323CallData *call, H245RequestChannelCloseAck *rccAck);
EXTERN int chrBuildFastStartOLC(struct CHRH323CallData *call,
	H245OpenLogicalChannel *olc,
	chrH323EpCapability *epCap,
	OOCTXT*pctxt, int dir);
EXTERN int chrPrepareFastStartResponseOLC
(CHRH323CallData *call, H245OpenLogicalChannel *olc,
chrH323EpCapability *epCap, OOCTXT*pctxt, int dir);
EXTERN int chrEncodeH245Message
(struct CHRH323CallData *call, H245Message *ph245Msg, ASN1OCTET *msgbuf, size_t size);
int chrSendMasterSlaveDeterminationRelease(struct CHRH323CallData * call);
int chrSendTerminalCapabilitySetReject
(struct CHRH323CallData *call, int seqNo, ASN1UINT cause);
int chrSendTerminalCapabilitySetRelease(struct CHRH323CallData * call);
int chrGetIpPortFromH245TransportAddress(CHRH323CallData *call, H245TransportAddress *h245Address, char *ip,int *port);
int chrMSDTimerExpired(void *data);
int chrTCSTimerExpired(void *data);
int chrOpenLogicalChannelTimerExpired(void *pdata);
int chrCloseLogicalChannelTimerExpired(void *pdata);
int chrRequestChannelCloseTimerExpired(void *pdata);
int chrSessionTimerExpired(void *pdata);
int chrOnReceivedRoundTripDelayRequest(CHRH323CallData *call, H245SequenceNumber sequenceNumber);

/****************************** H2225&H245 ******************************/
EXTERN int chrOnReceivedSetup(struct CHRH323CallData *call, Q931Message *q931Msg);
EXTERN int chrOnReceivedSignalConnect(struct CHRH323CallData* call, Q931Message *q931Msg);
EXTERN int chrHandleH2250Message(struct CHRH323CallData *call, Q931Message *q931Msg);
EXTERN int chrOnReceivedFacility(struct CHRH323CallData *call, Q931Message * pQ931Msg);
EXTERN int chrHandleTunneledH245Messages(struct CHRH323CallData *call, H225H323_UU_PDU * pH323UUPdu);
EXTERN int chrHandleStartH245FacilityMessage(struct CHRH323CallData *call,H225Facility_UUIE *facility);
EXTERN int chrH323RetrieveAliases(struct CHRH323CallData *call, H225_SeqOfH225AliasAddress *pAddresses,CHRAliases **aliasList);
EXTERN int chrPopulateAliasList(OOCTXT *pctxt, CHRAliases *pAliases,H225_SeqOfH225AliasAddress *pAliasList);
EXTERN CHRAliases* chrH323GetAliasFromList(CHRAliases *aliasList, int type, char *value);
EXTERN CHRAliases* chrH323AddAliasToList(CHRAliases **pAliasList, OOCTXT *pctxt, H225AliasAddress *pAliasAddress);
int chrH323GetIpPortFromH225TransportAddress(struct CHRH323CallData *call,H225TransportAddress *h225Address, char *ip, int *port);

#ifdef __cplusplus
}
#endif /** __cplusplus */

#endif /** _CHRH323PROTOCOL_H_ */