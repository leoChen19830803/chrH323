/**
* @file chrTypes.h
* @author chenhaoran
* General enum
*/
#ifndef _CHRTYPES_H_
#define _CHRTYPES_H_

#include "chrSocket.h"
#include "MULTIMEDIA-SYSTEM-CONTROL.h"
#include "H323-MESSAGES.h"
#include "ooasn1.h"



#define CHRH323_VERSION "v0.1"

#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif /* EXTERN */

/* Unix return value */
#define CHR_FAILED       -1
#define CHR_OK           0

/**
* States defined for master/slave determination procedure.
*/
typedef enum CHRMasterSlaveState {
	CHR_MasterSlave_Idle,
	CHR_MasterSlave_DetermineSent,
	CHR_MasterSlave_AckReceived,
	CHR_MasterSlave_Master,
	CHR_MasterSlave_Slave
} CHRMasterSlaveState;

/**
* States defined for the capability exchange procedure.
*/
typedef enum {
	CHR_LocalTermCapExchange_Idle,
	CHR_LocalTermCapSetSent,
	CHR_LocalTermCapSetAckRecvd,
	CHR_RemoteTermCapExchange_Idle,
	CHR_RemoteTermCapSetRecvd,
	CHR_RemoteTermCapSetAckSent
} CHRCapExchangeState;

/**
* Call clear reason codes.
*/
typedef enum CHRCallClearReason {
	CHR_REASON_UNKNOWN = 0,
	CHR_REASON_INVALIDMESSAGE,
	CHR_REASON_TRANSPORTFAILURE,
	CHR_REASON_NOROUTE,
	CHR_REASON_NOUSER,
	CHR_REASON_NOBW,
	CHR_REASON_GK_NOCALLEDUSER,
	CHR_REASON_GK_NOCALLERUSER,
	CHR_REASON_GK_NORESOURCES,
	CHR_REASON_GK_UNREACHABLE,
	CHR_REASON_GK_CLEARED,
	CHR_REASON_NOCOMMON_CAPABILITIES,
	CHR_REASON_REMOTE_FWDED,
	CHR_REASON_LOCAL_FWDED,
	CHR_REASON_REMOTE_CLEARED,
	CHR_REASON_LOCAL_CLEARED,
	CHR_REASON_REMOTE_BUSY,
	CHR_REASON_LOCAL_BUSY,
	CHR_REASON_REMOTE_NOANSWER,
	CHR_REASON_LOCAL_NOTANSWERED,
	CHR_REASON_REMOTE_REJECTED,
	CHR_REASON_LOCAL_REJECTED,
	CHR_REASON_REMOTE_CONGESTED,
	CHR_REASON_LOCAL_CONGESTED
} CHRCallClearReason;

/** Terminal type of the endpoint. Terminal is 60. MCU is 120*/
#define CHRTERMTYPE 60
#define MAX_IP_LENGTH 15
#define MAXLOGMSGLEN 2048
#define DEFAULT_MAX_RETRIES 3

/**
Various message types for H225 and H245 messages
*/
#define CHR_MSGTYPE_MIN                     101
#define CHRQ931MSG                          101
#define CHRH245MSG                          102
#define CHRSetup                            103
#define CHRCallProceeding                   104
#define CHRAlert                            105
#define CHRConnect                          106
#define CHRReleaseComplete                  107
#define CHRFacility                         108
#define CHRInformationMessage               109
#define CHRMasterSlaveDetermination         110
#define CHRMasterSlaveAck                   111
#define CHRMasterSlaveReject                112
#define CHRMasterSlaveRelease               113
#define CHRTerminalCapabilitySet            114
#define CHRTerminalCapabilitySetAck         115
#define CHRTerminalCapabilitySetReject      116
#define CHRTerminalCapabilitySetRelease     117
#define CHROpenLogicalChannel               118
#define CHROpenLogicalChannelAck            119
#define CHROpenLogicalChannelReject         120
#define CHROpenLogicalChannelRelease        121
#define CHROpenLogicalChannelConfirm        122
#define CHRCloseLogicalChannel              123
#define CHRCloseLogicalChannelAck           124
#define CHRRequestChannelClose              125
#define CHRRequestChannelCloseAck           126
#define CHRRequestChannelCloseReject        127
#define CHRRequestChannelCloseRelease       128
#define CHREndSessionCommand                129
#define CHRUserInputIndication              130
#define CHRRequestDelayResponse             131
#define CHRStatus                           132
#define CHR_MSGTYPE_MAX                     132

/* Timer types */
#define CHR_CALLESTB_TIMER  (1<<0)
#define CHR_MSD_TIMER       (1<<1)
#define CHR_TCS_TIMER       (1<<2)
#define CHR_OLC_TIMER       (1<<3)
#define CHR_CLC_TIMER       (1<<4)
#define CHR_RCC_TIMER       (1<<5)
#define CHR_SESSION_TIMER   (1<<6)
#define CHR_H245CONNECT_TIMER (1<<7)

#define MAXMSGLEN 16384
#define MAXFILENAME 256


typedef enum CHRCallMode {
	CHR_CALLMODE_AUDIOCALL,   /*!< Audio call */
	CHR_CALLMODE_AUDIORX,     /*!< Audio call - receive only */
	CHR_CALLMODE_AUDIOTX,     /*!< Audio call - transmit only */
	CHR_CALLMODE_VIDEOCALL,   /*!< Video call */
	CHR_CALLMODE_FAX          /*!< Fax transmission */
} CHRCallMode;

/*
* Flag macros - these operate on bit mask flags using mask values
*/
#define CHR_SETFLAG(flags,mask) (flags |= (ASN1UINT)mask)
#define CHR_CLRFLAG(flags,mask) (flags &= ~(ASN1UINT)mask)
#define CHR_TESTFLAG(flags,mask) (((ASN1UINT)flags & (ASN1UINT)mask) != 0)

typedef struct chrCallOptions {
	OOBOOL fastStart;    /*!< Use FastStart signaling */
	OOBOOL tunneling;    /*!< Use H.245 tunneling */
	OOBOOL disableGk;    /*!< Disable use of gatekeeper */
	CHRCallMode callMode; /*!< Type of channel to setup with FastStart */
	void *usrData;
}chrCallOptions;


struct CHRH323CallData;

/** ??? */
typedef struct chrTimerCallback{
	struct CHRH323CallData* call;
	ASN1UINT    timerType;
	ASN1UINT    channelNumber;
} chrTimerCallback;

EXTERN const char* chrUtilsGetText(OOUINT32 idx, const char** table, size_t tabsiz);
EXTERN OOBOOL chrUtilsIsStrEmpty(const char * str);
EXTERN OOBOOL chrIsDialedDigit(const char* str);
EXTERN int chrUtilsTextToBool(const char* str, OOBOOL* pbool);

struct _CHRTimer;

typedef int(*CHRTimerCbFunc)(void *data);

typedef struct _CHRTimer {
	struct timeval expireTime, timeout;
	void*        cbData;
	OOBOOL       reRegister;

	/* Callback functions */
	CHRTimerCbFunc timeoutCB;
} CHRTimer;
EXTERN void chrTimerComputeExpireTime(CHRTimer* pTimer);
EXTERN CHRTimer* chrTimerCreate(OOCTXT* pctxt, DList *pList, CHRTimerCbFunc cb, OOUINT32 deltaSecs, void *data,
OOBOOL reRegister);
EXTERN void chrTimerDelete(OOCTXT* pctxt, DList* pList, CHRTimer* pTimer);
EXTERN OOBOOL chrTimerExpired(CHRTimer* pTimer);
EXTERN void chrTimerFireExpired(OOCTXT* pctxt, DList* pList);
EXTERN int chrTimerInsertEntry(OOCTXT* pctxt, DList* pList, CHRTimer* pTimer);
EXTERN struct timeval* chrTimerNextTimeout(DList* pList, struct timeval* ptimeout);
EXTERN void chrTimerReset(OOCTXT* pctxt, DList* pList, CHRTimer* pTimer);
int chrCompareTimeouts(struct timeval *to1, struct timeval *to2);

#endif

