/**
* @file chrTrace.h
* @author chenhaoran
* General trace
*/
#ifndef _CHRTRACE_H_
#define _CHRTRACE_H_
#include "ooCommon.h"

/* tracing */
#define CHRTRCLVLERR  1
#define CHRTRCLVLWARN 2
#define CHRTRCLVLINFO 3
#define CHRTRCLVLDBG 4

#define CHRTRACEERR1(a)        chrTrace(__FILE__, __LINE__, CHRTRCLVLERR,a)
#define CHRTRACEERR2(a,b)      chrTrace(__FILE__, __LINE__, CHRTRCLVLERR,a,b)
#define CHRTRACEERR3(a,b,c)    chrTrace(__FILE__, __LINE__, CHRTRCLVLERR,a,b,c)
#define CHRTRACEERR4(a,b,c,d)  chrTrace(__FILE__, __LINE__, CHRTRCLVLERR,a,b,c,d)
#define CHRTRACEWARN1(a)       chrTrace(__FILE__, __LINE__, CHRTRCLVLWARN,a)
#define CHRTRACEWARN2(a,b)     chrTrace(__FILE__, __LINE__, CHRTRCLVLWARN,a,b)
#define CHRTRACEWARN3(a,b,c)   chrTrace(__FILE__, __LINE__, CHRTRCLVLWARN,a,b,c)
#define CHRTRACEWARN4(a,b,c,d) chrTrace(__FILE__, __LINE__, CHRTRCLVLWARN,a,b,c,d)
#define CHRTRACEINFO1(a)       chrTrace(__FILE__, __LINE__, CHRTRCLVLINFO, a)
#define CHRTRACEINFO2(a,b)     chrTrace(__FILE__, __LINE__, CHRTRCLVLINFO,a,b)
#define CHRTRACEINFO3(a,b,c)   chrTrace(__FILE__, __LINE__, CHRTRCLVLINFO,a,b,c)
#define CHRTRACEINFO4(a,b,c,d) chrTrace(__FILE__, __LINE__, CHRTRCLVLINFO,a,b,c,d)
#define CHRTRACEINFO5(a,b,c,d,e) chrTrace(__FILE__, __LINE__, CHRTRCLVLINFO,a,b,c,d,e)
#define CHRTRACEINFO6(a,b,c,d,e,f) chrTrace(__FILE__, __LINE__, CHRTRCLVLINFO,a,b,c,d,e, f)
#define CHRTRACEDBG1(a)       chrTrace(__FILE__, __LINE__, CHRTRCLVLDBG,a)
#define CHRTRACEDBG2(a,b)     chrTrace(__FILE__, __LINE__, CHRTRCLVLDBG,a,b)
#define CHRTRACEDBG3(a,b,c)   chrTrace(__FILE__, __LINE__, CHRTRCLVLDBG,a,b,c)
#define CHRTRACEDBG4(a,b,c,d) chrTrace(__FILE__, __LINE__, CHRTRCLVLDBG,a,b,c,d)
#define CHRTRACEDBG5(a,b,c,d,e) chrTrace(__FILE__, __LINE__, CHRTRCLVLDBG,a,b,c,d,e)
#define CHRTRACEDBG6(a,b,c,d,e,f) chrTrace(__FILE__, __LINE__, CHRTRCLVLDBG,a,b,c,d,e,f)


#ifdef __cplusplus
extern "C" {
#endif

#ifndef EXTERN
#define EXTERN __declspec(dllexport)
#endif /* EXTERN */


EXTERN void chrSetTraceThreshold(OOUINT32 traceLevel);
EXTERN void chrTrace(char *file, long line, OOUINT32 traceLevel, const char * fmtspec, ...);
#ifdef __cplusplus
}
#endif

#endif /* _CHRTRACE_H_ */
