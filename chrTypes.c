
#include <ctype.h>
#include <stdlib.h>
#include <string.h>
#include "chrTypes.h"
#include "ooCommon.h"
#include "ooDateTime.h"

const char* chrUtilsGetText (OOUINT32 idx, const char** table, size_t tabsiz)
{
   return (idx < tabsiz) ? table[idx] : "?";
}

OOBOOL chrUtilsIsStrEmpty (const char* str)
{
   return (str == NULL || *str =='\0');
}


OOBOOL chrIsDialedDigit (const char* str)
{
   if(str == NULL || *str =='\0') { return FALSE; }
   while(*str != '\0')
   {
      if(!isdigit(*str) &&
         *str != '#' && *str != '*' && *str != ',') { return FALSE; }
      str++;
   }
   return TRUE;
}

OOINT32 lookupEnum(const char* strValue, size_t strValueSize, const OOEnumItem enumTable[], OOUINT16 enumTableSize)
{
   size_t lower = 0;
   size_t upper = enumTableSize - 1;
   size_t middle;
   int    cmpRes;

   if (strValueSize == (size_t)-1) {
      strValueSize = strlen (strValue);
   }

   while (lower < upper && upper != (size_t)-1) {
      middle = (lower + upper)/2;

      cmpRes = strncmp (enumTable[middle].name, strValue, strValueSize);

      if (cmpRes == 0)
         cmpRes = (int)enumTable[middle].namelen - (int)strValueSize;

      if (cmpRes == 0) { /* equal */
         return (int)middle;
      }
      if (cmpRes < 0)
         lower = middle+1;
      else
         upper = middle-1;
   }

   if (lower == upper && (size_t)enumTable[lower].namelen == strValueSize &&
       strncmp (enumTable[lower].name, strValue, strValueSize) == 0) {
      return (int)lower;
   }

   return ASN_E_INVENUM;
}

int chrUtilsTextToBool (const char* str, OOBOOL* pbool)
{
   if (0 == pbool) return CHR_FAILED;

   if (!strcasecmp (str, "true") ||
       !strcasecmp (str, "yes")  ||
       !strcasecmp (str, "1")) {
      *pbool = TRUE;
      return CHR_OK;
   }
   else if (!strcasecmp (str, "false") ||
            !strcasecmp (str, "no")  ||
            !strcasecmp (str, "0")) {
      *pbool = FALSE;
      return CHR_OK;
   }
   else {
      return CHR_FAILED;
   }
}


#define USECS_IN_SECS 1000000
#define NSECS_IN_USECS 1000

#ifndef MICROSEC
#define MICROSEC USECS_IN_SECS
#endif

/**
* This is a timer list used by test application chansetup only.
*/

DList g_TimerList;

CHRTimer* chrTimerCreate
(OOCTXT* pctxt, DList *pList, CHRTimerCbFunc cb, OOUINT32 deltaSecs, void *data,
OOBOOL reRegister)
{
	CHRTimer* pTimer = (CHRTimer*)memAlloc(pctxt, sizeof(CHRTimer));
	if (0 == pTimer) return 0;

	memset(pTimer, 0, (sizeof(CHRTimer)));
	pTimer->timeoutCB = cb;
	pTimer->cbData = data;
	pTimer->reRegister = reRegister;
	pTimer->timeout.tv_sec = deltaSecs;
	pTimer->timeout.tv_usec = 0;

	/* Compute the absolute time at which this timer should expire */

	chrTimerComputeExpireTime(pTimer);

	/* Insert this timer into the complete list */
	if (pList)
		chrTimerInsertEntry(pctxt, pList, pTimer);
	else
		chrTimerInsertEntry(pctxt, &g_TimerList, pTimer);

	return pTimer;
}

void chrTimerComputeExpireTime(CHRTimer* pTimer)
{
	struct timeval tv;
	ooGetTimeOfDay(&tv, 0);

	/* Compute delta time to expiration */

	pTimer->expireTime.tv_usec = tv.tv_usec + pTimer->timeout.tv_usec;
	pTimer->expireTime.tv_sec = tv.tv_sec + pTimer->timeout.tv_sec;

	while (pTimer->expireTime.tv_usec >= MICROSEC) {
		pTimer->expireTime.tv_usec -= MICROSEC;
		pTimer->expireTime.tv_sec++;
	}
}

void chrTimerDelete(OOCTXT* pctxt, DList *pList, CHRTimer* pTimer)
{
	dListFindAndRemove(pList, pTimer);
	memFreePtr(pctxt, pTimer);
}

OOBOOL chrTimerExpired(CHRTimer* pTimer)
{
	struct timeval tvstr;
	ooGetTimeOfDay(&tvstr, 0);

	if (tvstr.tv_sec > pTimer->expireTime.tv_sec)
		return TRUE;

	if ((tvstr.tv_sec == pTimer->expireTime.tv_sec) &&
		(tvstr.tv_usec > pTimer->expireTime.tv_usec))
		return TRUE;

	return FALSE;
}

void chrTimerFireExpired(OOCTXT* pctxt, DList *pList)
{
	CHRTimer* pTimer;
	int stat;

	while (pList->count > 0) {
		pTimer = (CHRTimer*)pList->head->data;

		if (chrTimerExpired(pTimer)) {
			/*
			* Re-register before calling callback function in case it is
			* a long duration callback.
			*/
			if (pTimer->reRegister) chrTimerReset(pctxt, pList, pTimer);

			stat = (*pTimer->timeoutCB)(pTimer->cbData);

			if (0 != stat || !pTimer->reRegister) {
				chrTimerDelete(pctxt, pList, pTimer);
			}
		}
		else break;
	}
}

int chrTimerInsertEntry(OOCTXT* pctxt, DList *pList, CHRTimer* pTimer)
{
	DListNode* pNode;
	CHRTimer* p;
	int i = 0;

	for (pNode = pList->head; pNode != 0; pNode = pNode->next) {
		p = (CHRTimer*)pNode->data;
		if (pTimer->expireTime.tv_sec  <  p->expireTime.tv_sec) break;
		if (pTimer->expireTime.tv_sec == p->expireTime.tv_sec &&
			pTimer->expireTime.tv_usec <= p->expireTime.tv_usec) break;
		i++;
	}

	dListInsertBefore(pctxt, pList, pNode, pTimer);

	return i;
}

struct timeval* chrTimerNextTimeout(DList *pList, struct timeval* ptimeout)
{
	CHRTimer* ptimer;
	struct timeval tvstr;

	if (pList->count == 0) return 0;
	ptimer = (CHRTimer*)pList->head->data;

	ooGetTimeOfDay(&tvstr, 0);

	ptimeout->tv_sec =
		OOMAX((int)0, (int)(ptimer->expireTime.tv_sec - tvstr.tv_sec));

	ptimeout->tv_usec = ptimer->expireTime.tv_usec - tvstr.tv_usec;

	while (ptimeout->tv_usec < 0) {
		ptimeout->tv_sec--;
		ptimeout->tv_usec += USECS_IN_SECS;
	}

	if (ptimeout->tv_sec < 0)
		ptimeout->tv_sec = ptimeout->tv_usec = 0;

	return (ptimeout);
}

/*
* Reregister a timer entry.  This function is responsible for moving
* the current pointer in the timer list to the next element to be
* processed..
*/
void chrTimerReset(OOCTXT* pctxt, DList *pList, CHRTimer* pTimer)
{
	if (pTimer->reRegister) {
		dListFindAndRemove(pList, pTimer);
		chrTimerComputeExpireTime(pTimer);
		chrTimerInsertEntry(pctxt, pList, pTimer);
	}
	else
		chrTimerDelete(pctxt, pList, pTimer);
}

int chrCompareTimeouts(struct timeval *to1, struct timeval *to2)
{

	if (to1->tv_sec > to2->tv_sec)   return 1;
	if (to1->tv_sec < to2->tv_sec)   return -1;

	if (to1->tv_usec > to2->tv_usec)   return 1;
	if (to1->tv_usec < to2->tv_usec)   return -1;

	return 0;
}
