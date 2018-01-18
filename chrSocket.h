/**
* @file chrSocket.h
* @author chenhaoran
* Wrapper of socket via Linux and Windows
*/
#ifndef _CHRSOCKET_H_
#define _CHRSOCKET_H_

#if defined(_WIN32) || defined(_WIN64)
/*Windows*/
#define INCL_WINSOCK_API_TYPEDEFS   1  /** redefine socket function name !!! Must before winsock2.h*/
#define INCL_WINSOCK_API_PROTOTYPES 0  /** undefine prototypes !!! Must before winsock2.h*/
#include <winsock2.h>
#include <sys/types.h>
#include <WS2tcpip.h>
#else
/*Linux*/
#include <sys/types.h>
#include "sys/time.h"
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <netinet/in.h>
#include <netdb.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <net/if.h>
#endif

#include "ooasn1.h"
typedef enum CHRH323PortType {
	CHRTCP, CHRUDP, CHRRTP
} CHRH323PortType;

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif /* EXTERN */
//#define EXTERN __declspec(dllimport) /* ???? */

/**
* Basic type of socket
*/
#if defined (_WIN64)
	typedef unsigned __int64 CHRSOCKET; /**< Socket's handle */
#elif defined (_WIN32)
	typedef unsigned int CHRSOCKET; /**< Socket's handle */
	typedef int socklen_t;
#else
	typedef int CHRSOCKET;          /**< Socket's handle */
#endif
typedef unsigned long CHRIPADDR;
#define CHRIPADDR_ANY ((CHRIPADDR)0)
#define CHRIPADDR_LOCAL ((CHRIPADDR)0x7f000001UL) /* 127.0.0.1*/
typedef struct CHRInterface{
	char *name;
	char *addr;
	char *mask;
	struct CHRInterface *next;
}CHRInterface;

#define CHRSOCKET_INVALID ((CHRSOCKET)-1)
/******** Functions *************/
/** Common & Utils */
EXTERN int chrSocketInit(void);
EXTERN int chrSocketCleanup(void);
EXTERN int chrSocketAddrToStr(CHRIPADDR ipAddr, char* pbuf, int bufsize);
EXTERN int chrSocketStrToAddr(const char* pIPAddrStr, CHRIPADDR* pIPAddr);
EXTERN int chrSocketConvertIpToNwAddr(const char* inetIp, ASN1OCTET* netIp, size_t bufsiz);/*???*/
EXTERN int chrGetLocalIPAddress(char * pIPAddrs);
EXTERN int chrSocketGetSockName(CHRSOCKET socket, struct sockaddr_in *name, socklen_t* size);/*???*/
EXTERN long chrSocketHTONL(long val);
EXTERN short chrSocketHTONS(short val);
EXTERN int chrSocketGetIpAndPort(CHRSOCKET socket, char *ip, int len, int *port);
/*EXTERN int chrSocketGetInterfaceList(OOCTXT *pctxt, CHRSOCKET **ifList); */

/** TCP Socket */

EXTERN int chrSocketCreate(CHRSOCKET* psocket);
EXTERN int chrSocketListen(CHRSOCKET socket, int maxConnection);
EXTERN int chrSocketAccept(CHRSOCKET socket, CHRSOCKET *pNewSocket,
	CHRIPADDR* destAddr, int* destPort);
EXTERN int chrSocketBind(CHRSOCKET socket, CHRIPADDR addr, int port);
EXTERN int chrSocketConnect(CHRSOCKET socket, const char* host, int port);
EXTERN int chrSocketRecv(CHRSOCKET socket, ASN1OCTET* pbuf,
	ASN1UINT bufsize);
EXTERN int chrSocketSend(CHRSOCKET socket, const ASN1OCTET* pdata,
	ASN1UINT size);
EXTERN int chrSocketRecvPeek(CHRSOCKET socket, ASN1OCTET* pbuf, ASN1UINT bufsize);
EXTERN int chrSocketSelect(int nfds, fd_set *readfds, fd_set *writefds,
	fd_set *exceptfds, struct timeval * timeout);
EXTERN int chrSocketClose(CHRSOCKET socket);


/** UDP Socket */
EXTERN int chrSocketCreateUDP(CHRSOCKET* psocket);
EXTERN int chrSocketRecvFrom(CHRSOCKET socket, ASN1OCTET* pbuf,
	ASN1UINT bufsize, char * remotehost,
	ASN1UINT hostBufLen, int * remoteport);
EXTERN int chrSocketSendTo(CHRSOCKET socket, const ASN1OCTET* pdata,
	ASN1UINT size, const char* remotehost,
	int remoteport);


#ifdef __cplusplus
}
#endif
#endif
