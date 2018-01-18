/**
* @file chrH323Endpoint.h
* @author chenhaoran
* Application Interface
*/

#ifndef _CHRH323ENDPOINT_H_
#define _CHRH323ENDPOINT_H_

#include "ooConfig.h"
#include "chrSocket.h"
#include "chrCapability.h"
#include "chrCallSession.h"
#include "chrH323Protocol.h"

#define DEFAULT_TRACEFILE "trace.log"
#define DEFAULT_TERMTYPE 50
#define DEFAULT_PRODUCTID  "objsys"
#define DEFAULT_CALLERID   "objsyscall"
#define DEFAULT_T35COUNTRYCODE 0xB5  /* US */
#define DEFAULT_T35EXTENSION 0
#define DEFAULT_MANUFACTURERCODE 0x0036 /* Objective Systems */
#define DEFAULT_H245CONNECTION_RETRYTIMEOUT 2
#define DEFAULT_CALLESTB_TIMEOUT 60
#define DEFAULT_MSD_TIMEOUT 30
#define DEFAULT_TCS_TIMEOUT 30
#define DEFAULT_LOGICALCHAN_TIMEOUT 30
#define DEFAULT_ENDSESSION_TIMEOUT 15
#define DEFAULT_H323PORT 1720

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif /* EXTERN */

struct CHRCapPrefs;

#define TCPPORTSSTART 12030  /*!< Starting TCP port number */
#define TCPPORTSEND   12230  /*!< Ending TCP port number   */
#define UDPPORTSSTART 13030  /*!< Starting UDP port number */
#define UDPPORTSEND   13230  /*!< Ending UDP port number   */
#define RTPPORTSSTART 14030  /*!< Starting RTP port number */
#define RTPPORTSEND   14230  /*!< Ending RTP port number   */

typedef struct CHRH323Ports 
{
		int start;    /*!< Starting port number. */
		int max;      /*!< Maximum port number.  */
		int current;  /*!< Current port number.  */
} CHRH323Ports;

typedef struct CHRH323EndPoint 
{
		OOCTXT ctxt; /**allocation of memory */
		OOCTXT msgctxt; /**memory for message*/

		char   traceFile[MAXFILENAME];
		FILE * fptraceFile;

		CHRH323Ports tcpPorts;
		CHRH323Ports udpPorts;
		CHRH323Ports rtpPorts;

		ASN1UINT  flags;

		int termType; /* 50 - Terminal entity with No MC,
					  60 - Gateway entity with no MC,
					  70 - Terminal Entity with MC, but no MP
					  120 - MCU*/
		int t35CountryCode;
		int t35Extension;
		int manufacturerCode;
		const char *productID;
		const char *versionID;
		const char *callerid;
		char callingPartyNumber[50];
		CHRSOCKET *stackSocket;
		CHRAliases *aliases;

		int callType;

		struct chrH323EpCapability *myCaps;
		CHRCapPrefs     capPrefs;
		int noOfCaps;
		CHRH225MsgCallbacks h225Callbacks;
		CHRH323CALLBACKS h323Callbacks;
		char signallingIP[20];
		int listenPort;
		CHRSOCKET *listener; /** 1720 */
		CHRH323CallData *callList;

		CHRCallMode callMode; /* audio/audiorx/audiotx/video/fax */
		ASN1UINT callEstablishmentTimeout;
		ASN1UINT msdTimeout;
		ASN1UINT tcsTimeout;
		ASN1UINT logicalChannelTimeout;
		ASN1UINT sessionTimeout;
		/* int cmdPipe[2]; */
		struct ooGkClient *gkClient;

		CHRInterface *ifList; /* interface list for the host we are running on*/
		CHRSOCKET cmdListener;
		CHRSOCKET cmdSock;
#ifdef _WIN32
		int cmdPort; /* default 7575 */
#endif
		/**
		* Configured Q.931 transport capability is used to set the Q.931
		* bearer capability IE.
		*/
		enum Q931InformationTransferCapability bearercap;

	} CHRH323EndPoint;

#define chrEndPoint CHRH323EndPoint

EXTERN int chrH323EpInitialize(enum CHRCallMode callMode, const char* tracefile, int commandPort);
EXTERN int chrH323EpDestroy(void);

#ifdef _WIN32
EXTERN int chrH323EpCreateCmdListener(int cmdPort);
#endif

EXTERN int chrH323EpSetLocalAddress(const char* localip, int listenport);
EXTERN int chrH323EpSetTCPPortRange(int base, int max);
EXTERN int chrH323EpSetUDPPortRange(int base, int max);
EXTERN int chrH323EpSetRTPPortRange(int base, int max);
EXTERN int chrH323EpSetTraceLevel(int traceLevel);
EXTERN int chrH323EpAddAliasH323ID(const char* h323id);
EXTERN int chrH323EpAddAliasDialedDigits(const char* dialedDigits);
EXTERN int chrH323EpAddAliasTransportID(const char* ipaddress);
EXTERN int chrH323EpClearAllAliases(void);

EXTERN int chrH323EpSetH225MsgCallbacks(CHRH225MsgCallbacks h225Callbacks);
EXTERN int chrH323EpSetH323Callbacks(CHRH323CALLBACKS h323Callbacks);

EXTERN int chrH323EpEnableMediaWaitForConnect(void);
EXTERN int chrH323EpDisableMediaWaitForConnect(void);

EXTERN int chrH323EpSetProductID(const char * productID);
EXTERN int chrH323EpSetVersionID(const char * versionID);
EXTERN int chrH323EpSetCallerID(const char * callerID);
EXTERN int chrH323EpSetCallingPartyNumber(const char * number);

EXTERN int chrH323EpAddG711Capability
		(int cap, int txframes, int rxframes, int dir,
		cb_StartReceiveChannel startReceiveChannel,
		cb_StartTransmitChannel startTransmitChannel,
		cb_StopReceiveChannel stopReceiveChannel,
		cb_StopTransmitChannel stopTransmitChannel);
EXTERN int chrH323EpAddH264VideoCapability(int cap,
		CHRH264CapParams *capParams, int dir,
		cb_StartReceiveChannel startReceiveChannel,
		cb_StartTransmitChannel startTransmitChannel,
		cb_StopReceiveChannel stopReceiveChannel,
		cb_StopTransmitChannel stopTransmitChannel);

EXTERN int chrH323EpSetBearerCap(const char* configText);

#ifdef __cplusplus
}
#endif


#endif /**_CHRH323ENDPOINT_H_*/
