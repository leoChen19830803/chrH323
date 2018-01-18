
#include "chrTrace.h"
#include "chrCallSession.h"
#include "chrChannels.h"
#include "chrH323Endpoint.h"
#include "chrH323Protocol.h"
#include "chrStackCmds.h"

extern CHRSOCKET gCmdChan;
/** Generate Global CallToken by Caller: chrh323_o_1 */
int chrGenerateOutgoingCallToken (char *callToken, size_t size)
{
   static int counter = 1;
   char aCallToken[200];
   int  ret = 0;


   sprintf (aCallToken, "chrh323_o_%d", counter++);

   if (counter > CHR_MAX_CALL_TOKEN)
      counter = 1;

   if ((strlen(aCallToken)+1) < size)
      strcpy (callToken, aCallToken);
   else {
      ret = CHR_FAILED;
   }

   return ret;
}

/** Build Make Call Command Message and Write to Command Loop*/
CHRStkCmdStat chrMakeCall(const char* dest, char* callToken, size_t bufsiz, chrCallOptions *opts)
{
   CHRStackCommand cmd;

   if(!callToken)
      return CHR_STKCMD_INVALIDPARAM;


   /* Generate call token*/
   if (chrGenerateOutgoingCallToken (callToken, bufsiz) != CHR_OK){
      return CHR_STKCMD_INVALIDPARAM;
   }

   if(gCmdChan == 0)
   {
      if(chrCreateCmdConnection() != CHR_OK)
         return CHR_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(CHRStackCommand));
   cmd.type = CHR_CMD_MAKECALL;
   cmd.param1 = (void*) malloc(strlen(dest)+1);
   if(!cmd.param1)
   {
      return CHR_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, dest);


   cmd.param2 = (void*) malloc(strlen(callToken)+1);
   if(!cmd.param2)
   {
      free(cmd.param1);
      return CHR_STKCMD_MEMERR;
   }

   strcpy((char*)cmd.param2, callToken);

   if(!opts)
   {
      cmd.param3 = 0;
   }
   else {
	   cmd.param3 = (void*)malloc(sizeof(chrCallOptions));
      if(!cmd.param3)
      {
         free(cmd.param1);
         free(cmd.param2);
         return CHR_STKCMD_MEMERR;
      }
	  memcpy((void*)cmd.param3, opts, sizeof(chrCallOptions));
   }

   if(chrWriteStackCommand(&cmd) != CHR_OK)
   {
      free(cmd.param1);
      free(cmd.param2);
      if(cmd.param3) free(cmd.param3);
      return CHR_STKCMD_WRITEERR;
   }

   return CHR_STKCMD_SUCCESS;
}
/** Build Answer Call Command Message and Write to Command Loop*/
CHRStkCmdStat chrAnswerCall(const char *callToken)
{
   CHRStackCommand cmd;

   if(!callToken)
   {
      return CHR_STKCMD_INVALIDPARAM;
   }

   if(gCmdChan == 0)
   {
      if(chrCreateCmdConnection() != CHR_OK)
         return CHR_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(CHRStackCommand));
   cmd.type = CHR_CMD_ANSCALL;

   cmd.param1 = (void*) malloc(strlen(callToken)+1);
   if(!cmd.param1)
   {
      return CHR_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);

   if(chrWriteStackCommand(&cmd) != CHR_OK)
   {
      free(cmd.param1);
      return CHR_STKCMD_WRITEERR;
   }

   return CHR_STKCMD_SUCCESS;
}
/** Build Hang Call Command Message and Write to Command Loop*/
CHRStkCmdStat chrHangCall(const char* callToken, CHRCallClearReason reason)
{
   CHRStackCommand cmd;

   if(!callToken)
   {
      return CHR_STKCMD_INVALIDPARAM;
   }

   if(gCmdChan == 0)
   {
      if(chrCreateCmdConnection() != CHR_OK)
         return CHR_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(CHRStackCommand));
   cmd.type = CHR_CMD_HANGCALL;
   cmd.param1 = (void*) malloc(strlen(callToken)+1);
   cmd.param2 = (void*) malloc(sizeof(CHRCallClearReason));
   if(!cmd.param1 || !cmd.param2)
   {
      if(cmd.param1)   free(cmd.param1); /* Release memory */
      if(cmd.param2)   free(cmd.param2);
      return CHR_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);
   *((CHRCallClearReason*)cmd.param2) = reason;

   if(chrWriteStackCommand(&cmd) != CHR_OK)
   {
      free(cmd.param1);
      free(cmd.param2);
      return CHR_STKCMD_WRITEERR;
   }

   return CHR_STKCMD_SUCCESS;
}

/** Build Stop Monitor Command Message and Write to Command Loop*/
CHRStkCmdStat chrStopMonitor()
{
   CHRStackCommand cmd;

   if(gCmdChan == 0)
   {
      if(chrCreateCmdConnection() != CHR_OK)
         return CHR_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(CHRStackCommand));
   cmd.type = CHR_CMD_STOPMONITOR;

   if(chrWriteStackCommand(&cmd) != CHR_OK)
      return CHR_STKCMD_WRITEERR;

   gCmdChan = 0;

   return CHR_STKCMD_SUCCESS;
}
/** Build Send Video Fast Update Command Message and Write to Command Loop*/
CHRStkCmdStat chrSendVideoFastUpdate(const char *callToken)
{
   CHRStackCommand cmd;

   if(!callToken)
   {
      return CHR_STKCMD_INVALIDPARAM;
   }

   if(gCmdChan == 0)
   {
      if(chrCreateCmdConnection() != CHR_OK)
         return CHR_STKCMD_CONNECTIONERR;
   }

   memset(&cmd, 0, sizeof(CHRStackCommand));
   cmd.type = CHR_CMD_VIDEOFASTUPDATE;

   cmd.param1 = (void*) malloc(strlen(callToken)+1);
   if(!cmd.param1)
   {
      return CHR_STKCMD_MEMERR;
   }
   strcpy((char*)cmd.param1, callToken);

   if(chrWriteStackCommand(&cmd) != CHR_OK)
   {
      free(cmd.param1);
      return CHR_STKCMD_WRITEERR;
   }

   return CHR_STKCMD_SUCCESS;
}

const char* chrGetStkCmdStatusCodeTxt(CHRStkCmdStat stat)
{
   switch(stat)
   {
      case CHR_STKCMD_SUCCESS:
         return "Stack command - successfully issued";

      case CHR_STKCMD_MEMERR:
         return "Stack command - Memory allocation error";

      case CHR_STKCMD_INVALIDPARAM:
         return "Stack command - Invalid parameter";

      case CHR_STKCMD_WRITEERR:
         return "Stack command - write error";

      case CHR_STKCMD_CONNECTIONERR:
         return "Stack command - Failed to create command channel";

      default:
         return "Invalid status code";
   }
}

/** Global endpoint structure */
extern CHRH323EndPoint gH323ep;
CHRSOCKET gCmdChan = 0;
#ifdef _WIN32
char gCmdIP[20];
int gCmdPort;
#else
#include "pthread.h"
pthread_mutex_t gCmdChanLock;
#endif

#ifdef _WIN32
/** Command Server Socket Create Bind Listen */
int chrCreateCmdListener()
{
	int ret = 0;
	CHRIPADDR ipaddrs;

	CHRTRACEINFO3("Creating CMD listener at %s:%d\n",
		gH323ep.signallingIP, gH323ep.cmdPort);
	if ((ret = chrSocketCreate(&gH323ep.cmdListener)) != ASN_OK)
	{
		CHRTRACEERR1("ERROR: Failed to create socket for CMD listener\n");
		return CHR_FAILED;
	}
	ret = chrSocketStrToAddr(gH323ep.signallingIP, &ipaddrs);
	if ((ret = chrSocketBind(gH323ep.cmdListener, ipaddrs,
		gH323ep.cmdPort)) == ASN_OK)
	{
		chrSocketListen(gH323ep.cmdListener, 5); /*listen on socket*/
		CHRTRACEINFO1("CMD listener creation - successful\n");
		return CHR_OK;
	}
	else
	{
		CHRTRACEERR1("ERROR:Failed to create CMD listener\n");
		return CHR_FAILED;
	}

	return CHR_OK;
}
#endif
/** Command Client Socket Create Bind Connect*/
int chrCreateCmdConnection()
{
	int ret = 0;
#ifdef _WIN32
	if ((ret = chrSocketCreate(&gCmdChan)) != ASN_OK)
	{
		return CHR_FAILED;
	}
	else
	{

		//TODO:Need to add support for multihomed to work with channel driver

		/*
		bind socket to a port before connecting. Thus avoiding
		implicit bind done by a connect call. Avoided on windows as
		windows sockets have problem in reusing the addresses even after
		setting SO_REUSEADDR, hence in windows we just allow os to bind
		to any random port.
		*/
		ret = chrBindOSAllocatedPort(gCmdChan, gCmdIP);

		if (ret == CHR_FAILED)
		{
			return CHR_FAILED;
		}


		if ((ret = chrSocketConnect(gCmdChan, gCmdIP,
			gCmdPort)) != ASN_OK)
			return CHR_FAILED;

	}
#else /* Linux/UNIX - use pipe */
	int thePipe[2];

	if ((ret = pipe(thePipe)) == -1) {
		return CHR_FAILED;
	}
	pthread_mutex_init(&gCmdChanLock, NULL);

	gH323ep.cmdSock = dup(thePipe[0]);
	close(thePipe[0]);
	gCmdChan = dup(thePipe[1]);
	close(thePipe[1]);
#endif

	return CHR_OK;
}

/** Command Client Socket Close*/
int chrCloseCmdConnection()
{
#ifdef _WIN32
	chrSocketClose(gH323ep.cmdSock);
#else
	close(gH323ep.cmdSock);
	close(gCmdChan);
	gCmdChan = 0;
	pthread_mutex_destroy(&gCmdChanLock);
#endif
	gH323ep.cmdSock = 0;

	return CHR_OK;
}
/** Command Client Socket Accept*/
#ifdef _WIN32
int chrAcceptCmdConnection()
{
	int ret = chrSocketAccept(gH323ep.cmdListener, &gH323ep.cmdSock, NULL, NULL);

	if (ret != ASN_OK) {
		CHRTRACEERR1("Error:Accepting CMD connection\n");
		return CHR_FAILED;
	}
	CHRTRACEINFO1("Cmd connection accepted\n");
	return CHR_OK;
}
#endif
/** Command Client Socket Send or Pipe Write */
int chrWriteStackCommand(CHRStackCommand *cmd)
{
	int stat;
	CHRTRACEDBG5("write stack cmd: t=%d, p1=%x, p2=%x, p3=%x\n",
		cmd->type, cmd->param1, cmd->param2, cmd->param3);
#ifdef _WIN32
	stat = chrSocketSend
		(gCmdChan, (const ASN1OCTET*)cmd, sizeof(CHRStackCommand));

	if (0 != stat) {
		CHRTRACEERR2("ERROR: write stack command %d\n", stat);
		return CHR_FAILED;
	}
#else
	/* lock and write to pipe */
	pthread_mutex_lock(&gCmdChanLock);

	stat = write(gCmdChan, (char*)cmd, sizeof(CHRStackCommand));

	pthread_mutex_unlock(&gCmdChanLock);

	if (stat < 0) {
		CHRTRACEERR2("ERROR: write stack command %d\n", stat);
		return CHR_FAILED;
	}
#endif
	return CHR_OK;
}
/** Command Process by Switch */
int chrProcessStackCommand(CHRStackCommand* pcmd)
{
	CHRH323CallData *pCall = NULL;

	if (pcmd->type == CHR_CMD_NOOP) return 0;
	switch (pcmd->type) {
	case CHR_CMD_MAKECALL:
		CHRTRACEINFO2("Processing MakeCall command %s\n", (char*)pcmd->param2);

		chrH323MakeCall((char*)pcmd->param1, (char*)pcmd->param2,
			(chrCallOptions*)pcmd->param3);

		break;

	case CHR_CMD_ANSCALL:
		pCall = chrFindCallByToken((char*)pcmd->param1);
		if (!pCall) {
			CHRTRACEINFO2("Call \"%s\" does not exist\n", (char*)pcmd->param1);
			CHRTRACEINFO1("Call might be cleared/closed\n");
		}
		else {
			CHRTRACEINFO2("Processing Answer Call command for %s\n",
				(char*)pcmd->param1);
			chrSendConnect(pCall);
		}
		break;
	case CHR_CMD_HANGCALL:
		CHRTRACEINFO2("Processing hang call command %s\n", (char*)pcmd->param1);

		chrH323HangCall((char*)pcmd->param1, *(CHRCallClearReason*)pcmd->param2);

		break;

	case CHR_CMD_VIDEOFASTUPDATE:
		pCall = chrFindCallByToken((char*)pcmd->param1);
		if (!pCall) {
			CHRTRACEINFO2("Call \"%s\" does not exist\n", (char*)pcmd->param1);
			CHRTRACEINFO1("Call might be cleared/closed\n");
		}
		else {
			CHRTRACEINFO2("Processing Answer Call command for %s\n",
				(char*)pcmd->param1);
			chrSendVideoFastUpdateCommand(pCall);
		}
		break;

	case CHR_CMD_STOPMONITOR:
		CHRTRACEINFO1("Processing StopMonitor command\n");
		chrStopMonitorCalls();
		break;

	default: CHRTRACEERR2("ERROR: unknown command %d\n", pcmd->type);
	}
	return CHR_OK;
}


