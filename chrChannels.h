/**
* @file chrChannels.h
* @author chenhaoran
* 1. H.225 H.245 Signal Channels
* 2. Manager loop
*/
#ifndef _CHRCHANNELS_H_
#define _CHRCHANNELS_H_

#include "H323-MESSAGES.h"
#include "MULTIMEDIA-SYSTEM-CONTROL.h"
#include "chrTypes.h"
#include "chrSocket.h"
#include "chrCallSession.h" /** Must be this sequence  "CHRH323CallData" */



#define CHRRECEIVER 1
#define CHRTRANSMITTER 2
#define CHRDUPLEX 3

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif

struct CHRH323CallData;
struct Q931Message;

/** H.225 */
EXTERN int chrCreateH225Listener(void); /** called */
EXTERN int chrCloseH225Listener(void);
EXTERN int chrCreateH225Connection(struct CHRH323CallData *call); /** caller */
EXTERN int chrAcceptH225Connection(void); /** called */
EXTERN int chrCloseH225Connection(struct CHRH323CallData *call);
EXTERN int chrReceiveH225Msg(struct CHRH323CallData *call); /** receive ASN1 to Q931 to queue*/
EXTERN int chrSendH225MsgtoQueue(struct CHRH323CallData *call, struct Q931Message *msg); /** Q931 to ASN1 & send */

/** H.245 */
EXTERN int chrCreateH245Listener(struct CHRH323CallData *call); /** called */
EXTERN int chrCloseH245Listener(struct CHRH323CallData *call); 
EXTERN int chrCreateH245Connection(struct CHRH323CallData *call); /** caller */
EXTERN int chrAcceptH245Connection(struct CHRH323CallData *call); /** called */
EXTERN int chrCloseH245Connection(struct CHRH323CallData *call);
EXTERN int chrReceiveH245Msg(struct CHRH323CallData *call); /** receive ASN1 to H.245 */
EXTERN int chrSendH245MsgtoQueue(struct CHRH323CallData *call); /** H.245 to ASN1 & send to queue*/

/** Manager loop */
EXTERN int chrMonitorChannels(void);
EXTERN int chrStopMonitorCalls(void);
EXTERN int chrSendMsg(struct CHRH323CallData *call, int type); /** send H225 H245 message to channel*/
EXTERN int chrOnSendMsg(struct CHRH323CallData *call, int msgType, int tunneledMsgType, int associatedChan);
//EXTERN OOBOOL chrChannelsIsConnectionOK(CHRH323CallData *call, CHRSOCKET sock); /** some strange questions*/

/** Port relevate*/
EXTERN int chrGetNextPort(CHRH323PortType type);
EXTERN int chrBindPort(CHRH323PortType type, CHRSOCKET socket, char *ip);
#ifdef _WIN32
EXTERN int chrBindOSAllocatedPort(CHRSOCKET socket, char *ip);
#endif


#ifdef __cplusplus
}
#endif /** __cplusplus */

#endif /** _CHRCHANNELS_H_ */