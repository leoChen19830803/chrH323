/**
* @file chrCapability.h
* @author chenhaoran
* Media capability abstraction, called for H323Endpoint 
*/
#ifndef _CHRCAPABILITY_H_
#define _CHRCAPABILITY_H_

#include "chrTypes.h"
#include "ooasn1.h"

#define CHRRX      (1<<0) /* ??? */
#define CHRTX      (1<<1) /* ??? */
#define CHRRXANDTX (1<<2) /* ??? */
#define CHRRXTX    (1<<3) /* For symmetric capabilities */

typedef enum CHRCapabilities
{
	CHR_CAP_AUDIO_BASE = 0,
	CHR_G711ALAW64K = 1,
	CHR_G711ULAW64K = 2,
	CHR_CAP_VIDEO_BASE = 27,
	CHR_H264VIDEO = 32
} CHRCapabilities;

/** preference order*/
typedef struct CHRCapPrefs 
{
	int order[20];
	int index;
}CHRCapPrefs;

typedef struct CHRCapParams 
{
	int txframes;  /*!< Number of frames per packet for transmission */
	int rxframes;  /*!< Number of frames per packet for reception */
	OOBOOL silenceSuppression;
} CHRCapParams;

typedef struct CHRH264CapParams 
{
		unsigned maxBitRate; /* !< Maximum bit rate for transmission/reception in units of 100 bits/sec */
		unsigned profile;
		unsigned constaint;
		unsigned level;
		unsigned char send_pt;
		unsigned char recv_pt;
} CHRH264CapParams;

typedef struct CHRGenericCapParams
{
	int type;
	unsigned maxBitRate; /* !< Maximum bit rate for transmission/reception in units of 100 bits/sec */
}CHRGenericCapParams;

struct CHRH323CallData; /** !< Call parameters */
struct CHRLogicalChannel; /** !< Rtp channels */

#ifdef __cplusplus
	extern "C" {
#endif

/** Callbacks to Application Layer*/
typedef int(*cb_StartReceiveChannel)(struct CHRH323CallData *call, struct CHRLogicalChannel *pChannel);
typedef int(*cb_StartTransmitChannel)(struct CHRH323CallData *call, struct CHRLogicalChannel *pChannel);
typedef int(*cb_StopReceiveChannel)(struct CHRH323CallData *call, struct CHRLogicalChannel *pChannel);
typedef int(*cb_StopTransmitChannel)(struct CHRH323CallData *call, struct CHRLogicalChannel *pChannel);

typedef enum CHRCapType 
{
	CHR_CAP_TYPE_AUDIO,
	CHR_CAP_TYPE_VIDEO,
	CHR_CAP_TYPE_DATA
} CHRCapType;

typedef struct chrH323EpCapability
{
	int dir;
	int cap;
	CHRCapType capType;
	void *params;
	cb_StartReceiveChannel startReceiveChannel;
	cb_StartTransmitChannel startTransmitChannel;
	cb_StopReceiveChannel stopReceiveChannel;
	cb_StopTransmitChannel stopTransmitChannel;
	struct chrH323EpCapability *next; /** for iterator */
} chrH323EpCapability;




#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif /* EXTERN */

/** add */	
EXTERN int chrCapabilityAddSimpleCapability
			(struct CHRH323CallData *call, int cap, int txframes, int rxframes,
			OOBOOL silenceSuppression, int dir,
			cb_StartReceiveChannel startReceiveChannel,
			cb_StartTransmitChannel startTransmitChannel,
			cb_StopReceiveChannel stopReceiveChannel,
			cb_StopTransmitChannel stopTransmitChannel,
			OOBOOL remote);
EXTERN int chrCapabilityAddH264VideoCapability(struct CHRH323CallData *call,
			CHRH264CapParams *capParams, int dir,
			cb_StartReceiveChannel startReceiveChannel,
			cb_StartTransmitChannel startTransmitChannel,
			cb_StopReceiveChannel stopReceiveChannel,
			cb_StopTransmitChannel stopTransmitChannel,
			OOBOOL remote);
int chrAddRemoteAudioCapability(struct CHRH323CallData *call,H245AudioCapability *audioCap, int dir);
int chrAddRemoteCapability(struct CHRH323CallData *call, H245Capability *cap);
/** joint */
EXTERN int chrCapabilityUpdateJointCapabilities(struct CHRH323CallData* call, H245Capability *cap);
EXTERN int chrCapabilityUpdateJointCapabilitiesVideo(struct CHRH323CallData *call, H245VideoCapability *videoCap, int dir);
EXTERN int chrCapabilityUpdateJointCapabilitiesVideoH264(struct CHRH323CallData *call, H245GenericCapability *pH264Cap, int dir);
/** check */
ASN1BOOL chrCapabilityCheckCompatibility_Audio(struct CHRH323CallData *call, chrH323EpCapability *epCap, H245AudioCapability *dataType, int dir);
OOBOOL chrCapabilityCheckCompatibility_Video(struct CHRH323CallData *call, chrH323EpCapability* epCap, H245VideoCapability* videoCap, int dir);
OOBOOL chrCapabilityCheckCompatibility(struct CHRH323CallData *call, chrH323EpCapability* epCap, H245DataType* dataType, int dir);

struct H245AudioCapability* chrCapabilityCreateAudioCapability(chrH323EpCapability* epCap, OOCTXT *pctxt, int dir);
struct H245VideoCapability* chrCapabilityCreateVideoCapability(chrH323EpCapability *epCap, OOCTXT *pctxt, int dir);
struct H245AudioCapability* chrCapabilityCreateG711AudioCapability(chrH323EpCapability *epCap, OOCTXT* pctxt, int dir);
struct H245VideoCapability* chrCapabilityCreateH264VideoCapability(chrH323EpCapability *epCap, OOCTXT* pctxt, int dir);

chrH323EpCapability* chrIsAudioDataTypeSupported(struct CHRH323CallData *call, H245AudioCapability* audioCap, int dir);
chrH323EpCapability* chrIsVideoDataTypeSupported(struct CHRH323CallData *call, H245VideoCapability* pVideoCap, int dir);
chrH323EpCapability* chrIsDataTypeSupported(struct CHRH323CallData *call, H245DataType *data, int dir);

/** CapPrefs */
EXTERN  int chrResetCapPrefs(struct CHRH323CallData *call);
EXTERN  int chrRemoveCapFromCapPrefs(struct CHRH323CallData *call, int cap);
EXTERN void chrAppendCapToCapList(chrH323EpCapability** pphead, chrH323EpCapability* pcap);
EXTERN int chrAppendCapToCapPrefs(struct CHRH323CallData *call, int cap);
EXTERN int chrChangeCapPrefOrder(struct CHRH323CallData *call, int cap, int pos);
EXTERN int chrPreppendCapToCapPrefs(struct CHRH323CallData *call, int cap);

/** tools*/
EXTERN const char* chrGetCapTypeText(CHRCapabilities cap);
EXTERN void chrCapabilityDiagPrint(const chrH323EpCapability* pvalue);


#ifdef __cplusplus
}
#endif /** __cplusplus */

#endif /**_CHRCAPABILITY_H_ */