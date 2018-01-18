/**
* @file chrCallSession.h
* @author chenhaoran
* Call Sessions
*/
#ifndef _CHRCALLSESSION_H_
#define _CHRCALLSESSION_H_

//#include "chrSocket.h"
#include "chrCapability.h"
#include "chrLogicalChannels.h"
#include "chrChannels.h"

#ifdef __cplusplus
	extern "C" {
#endif

/* Flag mask values */
#define CHR_M_ENDSESSION_BUILT   ASN1UINTCNT(0x00800000)
#define CHR_M_RELEASE_BUILT      ASN1UINTCNT(0x00400000)
#define CHR_M_FASTSTARTANSWERED  ASN1UINTCNT(0x04000000)

#define CHR_M_ENDPOINTCREATED    ASN1UINTCNT(0x00010000)
#define CHR_M_GKROUTED           ASN1UINTCNT(0x00200000)
#define CHR_M_AUTOANSWER         ASN1UINTCNT(0x00100000)
#define CHR_M_TUNNELING          ASN1UINTCNT(0x08000000)
#define CHR_M_MEDIAWAITFORCONN   ASN1UINTCNT(0x20000000)
#define CHR_M_FASTSTART          ASN1UINTCNT(0x02000000)
#define CHR_M_DISABLEGK          ASN1UINTCNT(0x01000000)
#define CHR_M_MANUALRINGBACK     ASN1UINTCNT(0x10000000)


/**
* Call Session states.
*/
typedef enum 
{
	CHR_CALL_CREATED,               /*!< Call created. */
	CHR_CALL_WAITING_ADMISSION,     /*!< Call waiting for admission by GK ????? */
	CHR_CALL_CONNECTING,            /*!< Call in process of connecting */
	CHR_CALL_CONNECTED,             /*!< Call currently connected. */
	CHR_CALL_PAUSED,                /*!< Call Paused for hold/transfer. ????? */
	CHR_CALL_CLEAR,                 /*!< Call marked for clearing */
	CHR_CALL_CLEAR_RELEASERECVD,    /*!< Release command received. ????? */
	CHR_CALL_CLEAR_RELEASESENT,     /*!< Release sent ????? */
	CHR_CALL_CLEARED                /*!< Call cleared */
} CHRCallState;

/**
* H.245 session states.
*/
typedef enum 
{
	CHR_H245SESSION_IDLE,
	CHR_H245SESSION_PAUSED,
	CHR_H245SESSION_ACTIVE,
	CHR_H245SESSION_ENDSENT,  /** maybe half connect*/
	CHR_H245SESSION_ENDRECVD, /** maybe half connect*/
	CHR_H245SESSION_CLOSED
} CHRH245SessionState;


typedef struct CHRMediaInfo
{
	char   dir[15]; /* transmit/receive*/
	int   cap;
	int   lMediaPort;
	int   lMediaCntrlPort;
	char  lMediaIP[20];
	struct CHRMediaInfo *next;
} CHRMediaInfo;

#define chrMediaInfo CHRMediaInfo

struct CHRAliases;

typedef struct CHRH323Channel 
{
	CHRSOCKET sock;      /*!< Socket connection for the channel */
	int      port;      /*!< Port assigned to the channel */
	DList    outQueue;  /*!< Output message queue */
} CHRH323Channel;

typedef struct CHRH323CallData 
{
	/** call param */
	OOCTXT               *pctxt; /** point to context*/
	char                 callToken[20]; /* ex: ooh323c_call_1 */
	char                 callType[10]; /* incoming/outgoing */
	CHRCallMode           callMode; /** ??? */
	ASN1USINT            callReference;
	char                 ourCallerId[256];
	char                 localIP[20];/* Local IP address */
	char                 *remoteDisplayName;
	struct CHRAliases     *remoteAliases;
	struct CHRAliases     *ourAliases; /*aliases used in the call for us */
	/** H225 */
	H225CallIdentifier   callIdentifier;/* H.323 protocol The call identifier for the active call. */
	char                 *callingPartyNumber;
	char                 *calledPartyNumber;
	H225ConferenceIdentifier confIdentifier; /* H.323 protocol */
	ASN1UINT             flags; /** ??? */
	CHRCallState          callState;
	CHRCallClearReason    callEndReason;
	CHRH323Channel*       pH225Channel;
	/** H245 */
	unsigned             h245ConnectionAttempts;
	CHRH245SessionState   h245SessionState;
	CHRMediaInfo          *mediaInfo;
	CHRH323Channel*       pH245Channel;
	CHRSOCKET             *h245listener;
	int                  *h245listenport;
	char                 remoteIP[20];/* Remote IP address */
	int                  remotePort;
	int                  remoteH245Port;
	CHRMasterSlaveState   masterSlaveState;   /*!< Master-Slave state */
	ASN1UINT             statusDeterminationNumber;
	CHRCapExchangeState   localTermCapState;
	CHRCapExchangeState   remoteTermCapState;
	struct chrH323EpCapability* ourCaps;
	struct chrH323EpCapability* remoteCaps; /* TODO: once we start using jointCaps, get rid of remoteCaps*/
	struct chrH323EpCapability* jointCaps;
	DList                remoteFastStartOLCs;
	ASN1UINT8            remoteTermCapSeqNo;
	ASN1UINT8            localTermCapSeqNo;
	CHRCapPrefs           capPrefs;
	CHRLogicalChannel*    logicalChans;
	int                  noOfLogicalChannels;
	int                  logicalChanNoBase;
	int                  logicalChanNoMax;
	int                  logicalChanNoCur;
	unsigned             nextSessionID; /* Note by default 1 is audio session, 2 is video and 3 is data, from 3 onwards master decides*/
	DList                timerList;
	ASN1UINT             msdRetries;
	/** others */
	void                 *usrData; /*!<User can set this to user specific data*/
	struct CHRH323CallData* next;
	struct CHRH323CallData* prev;
} CHRH323CallData;

#define chrCallData CHRH323CallData

/** callback functions */
typedef int(*cb_OnNewCallCreated)(CHRH323CallData* call);
typedef int(*cb_OnAlerting)(CHRH323CallData * call);
typedef int(*cb_OnIncomingCall)(CHRH323CallData* call);
typedef int(*cb_OnOutgoingCall)(CHRH323CallData* call);
typedef int(*cb_OnCallEstablished)(struct CHRH323CallData* call);
typedef int(*cb_OnCallCleared)(struct CHRH323CallData* call);
typedef int(*cb_OpenLogicalChannels)(struct CHRH323CallData* call);
typedef int(*cb_OnReceivedCommand)(struct CHRH323CallData *call, H245CommandMessage *command); /**H245 message */
typedef int(*cb_OnReceivedVideoFastUpdate)(struct CHRH323CallData *call, int channelNo);

typedef struct CHRH323CALLBACKS 
{
	cb_OnNewCallCreated onNewCallCreated;
	cb_OnAlerting onAlerting;
	cb_OnIncomingCall onIncomingCall;
	cb_OnOutgoingCall onOutgoingCall;
	cb_OnCallEstablished onCallEstablished;
	cb_OnCallCleared onCallCleared;
	cb_OpenLogicalChannels openLogicalChannels;
	cb_OnReceivedCommand onReceivedCommand;
	cb_OnReceivedVideoFastUpdate onReceivedVideoFastUpdate;
} CHRH323CALLBACKS;

/** call relevent */
EXTERN CHRH323CallData* chrCreateCall(char *type, char *callToken, void *usrData);
EXTERN int chrEndCall(CHRH323CallData *call);
EXTERN int chrCleanCall(CHRH323CallData *call);

EXTERN int chrAddCallToList(CHRH323CallData *call); /** maybe for multicall */
EXTERN int chrRemoveCallFromList(CHRH323CallData *call); /** maybe for multicall */
/** alias e164 h323id*/
EXTERN int chrCallSetCallerId(CHRH323CallData* call, const char* callerid);
EXTERN int chrCallSetCallingPartyNumber(CHRH323CallData *call, const char *number);
EXTERN int chrCallGetCallingPartyNumber(CHRH323CallData *call, char *buffer, int len);
EXTERN int chrCallGetCalledPartyNumber(CHRH323CallData *call, char *buffer, int len);
EXTERN int chrCallSetCalledPartyNumber(CHRH323CallData *call, const char *number);

EXTERN int chrCallClearAliases(CHRH323CallData *call);
EXTERN int chrCallAddAliasH323ID(CHRH323CallData *call, const char* h323id);
EXTERN int chrCallAddAliasDialedDigits(CHRH323CallData *call, const char* dialedDigits);
int chrCallAddAlias(CHRH323CallData *call, int aliasType, const char *value, OOBOOL local);
EXTERN int chrCallAddRemoteAliasDialedDigits(CHRH323CallData *call, const char* dialedDigits);
EXTERN int chrCallAddRemoteAliasH323ID(CHRH323CallData *call, const char* h323id);
/** capability */
EXTERN int chrCallAddG711Capability(CHRH323CallData *call, int cap, int txframes,
			int rxframes, int dir,
			cb_StartReceiveChannel startReceiveChannel,
			cb_StartTransmitChannel startTransmitChannel,
			cb_StopReceiveChannel stopReceiveChannel,
			cb_StopTransmitChannel stopTransmitChannel);
EXTERN int chrCallAddH264VideoCapability(CHRH323CallData *call, int cap,
			CHRH264CapParams *params, int dir,
			cb_StartReceiveChannel startReceiveChannel,
			cb_StartTransmitChannel startTransmitChannel,
			cb_StopReceiveChannel stopReceiveChannel,
			cb_StopTransmitChannel stopTransmitChannel);
EXTERN CHRH323CallData* chrFindCallByToken(char *callToken);


/** tools */
EXTERN ASN1BOOL chrIsSessionEstablished(CHRH323CallData *call, int sessionID, char* dir);
EXTERN int chrAddMediaInfo(CHRH323CallData *call, CHRMediaInfo mediaInfo);
EXTERN unsigned chrCallGenerateSessionID(CHRH323CallData *call, CHRCapType type, char *dir);
EXTERN int chrCallH245ConnectionRetryTimerExpired(void *data);
EXTERN const char* chrGetReasonCodeText(OOUINT32 code);
EXTERN const char* chrGetCallStateText(CHRCallState callState);

#ifdef __cplusplus
}
#endif /** __cplusplus */

#endif /** _CHRCALLSESSION_H_ */
