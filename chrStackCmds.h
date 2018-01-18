/**
 * @file chrStackCmds.h
 * 1. Build Command and Send to Command Process Loop
 */

#ifndef _CHR_STACKCMDS_H_
#define _CHR_STACKCMDS_H_

#include "chrTypes.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif /* EXTERN */

#define CHR_DEFAULT_CMDLISTENER_PORT 7575
/** Command Statistics */
typedef enum CHRStkCmdStat{
  CHR_STKCMD_SUCCESS,
  CHR_STKCMD_MEMERR,
  CHR_STKCMD_INVALIDPARAM,
  CHR_STKCMD_WRITEERR,
  CHR_STKCMD_CONNECTIONERR
}CHRStkCmdStat;
/** Command ID */
typedef enum CHRStackCmdID {
   CHR_CMD_NOOP,
   CHR_CMD_MAKECALL,          /*!< Make call */
   CHR_CMD_ANSCALL,           /*!< Answer call */
   CHR_CMD_HANGCALL,          /*!< Terminate call */
   CHR_CMD_VIDEOFASTUPDATE,   /*!< Send Video Fast Update */
   CHR_CMD_STOPMONITOR        /*!< Stop the event monitor */

} CHRStackCmdID;
/** Command Format 3 param most */
typedef struct CHRStackCommand {
   CHRStackCmdID type;
   void* param1;
   void* param2;
   void* param3;
} CHRStackCommand;

#define chrCommand CHRStackCommand;
/** API for application */
EXTERN CHRStkCmdStat chrMakeCall(const char* dest, char *callToken, size_t bufsiz, chrCallOptions *opts);
EXTERN CHRStkCmdStat chrAnswerCall(const char *callToken);
EXTERN CHRStkCmdStat chrHangCall(const char* callToken, CHRCallClearReason reason);
EXTERN CHRStkCmdStat chrSendVideoFastUpdate(const char *callToken);
EXTERN CHRStkCmdStat chrStopMonitor(void);
/** Socket(Windows) process or Pipe(Linux) process */
EXTERN const char* chrGetStkCmdStatusCodeTxt(CHRStkCmdStat stat);
#ifdef _WIN32
EXTERN int chrCreateCmdListener();
EXTERN int chrAcceptCmdConnection();
#endif
EXTERN int chrCreateCmdConnection();
EXTERN int chrCloseCmdConnection();
EXTERN int chrWriteStackCommand(CHRStackCommand *cmd);
EXTERN int chrProcessStackCommand(CHRStackCommand* pcmd);


#ifdef __cplusplus
}
#endif

#endif
