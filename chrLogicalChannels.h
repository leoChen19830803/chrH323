#ifndef _CHRLogicalChannelS_H_
#define _CHRLogicalChannelS_H_

#include "chrTypes.h"
#include "chrCapability.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif


/** logicalchannel */
struct chrH323EpCapability;
struct CHRH323CallData;

/**
* Logical channel states.
*/
typedef enum {
	CHR_LOGICAL_CHAN_UNKNOWN,
	CHR_LOGICALCHAN_IDLE,
	CHR_LOGICALCHAN_PROPOSED,
	CHR_LOGICALCHAN_ESTABLISHED
} CHRLogicalChannelState;

/**
* Structure to store information on logical channels for a call.
*/
typedef struct CHRLogicalChannel {
	int  channelNo;
	int  sessionID;
	enum CHRCapType type;
	char dir[10];  /* receive/transmit */
	char remoteIP[20];
	int  remoteMediaPort;
	int  remoteMediaControlPort;
	int  localRtpPort;
	int  localRtcpPort;
	char localIP[20];
	CHRLogicalChannelState state;
	struct chrH323EpCapability *chanCap;
	struct CHRLogicalChannel *next; /** for iterator */
} CHRLogicalChannel;




EXTERN CHRLogicalChannel* chrAddNewLogicalChannel(struct CHRH323CallData *call, int channelNo, int sessionID,
	char *dir, chrH323EpCapability *epCap);
EXTERN int chrRemoveLogicalChannel(struct CHRH323CallData *call, int ChannelNo);

EXTERN int chrOnLogicalChannelEstablished(struct CHRH323CallData *call, CHRLogicalChannel * pChannel);
EXTERN CHRLogicalChannel* chrGetLogicalChannel(struct CHRH323CallData *call, int sessionID, char *dir);


EXTERN int chrClearLogicalChannel(struct CHRH323CallData *call, int channelNo);
EXTERN int chrClearAllLogicalChannels(struct CHRH323CallData *call);

EXTERN CHRLogicalChannel * chrFindLogicalChannelByOLC(struct CHRH323CallData *call, H245OpenLogicalChannel *olc);
EXTERN CHRLogicalChannel* chrFindLogicalChannelByLogicalChannelNo(struct CHRH323CallData *call, int channelNo);
EXTERN CHRLogicalChannel * chrFindLogicalChannel(struct CHRH323CallData* call, int sessionID, char *dir, H245DataType* dataType);

#ifdef __cplusplus
}
#endif /** __cplusplus */


#endif