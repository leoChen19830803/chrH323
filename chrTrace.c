#include <ctype.h>
#include <stdarg.h>
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

#include "chrTypes.h"
#include "chrTrace.h"
#include "ooCommon.h"

static OOUINT32 chr_traceLevel = TRACELVL;
static OOBOOL   chr_printTime = TRUE;


static void logDateTime()
{
	char timeString[100];
	char currtime[3];
	static int lasttime = 25;
	int printDate = 0;

#ifdef _WIN32
	SYSTEMTIME systemTime;
	GetLocalTime(&systemTime);
	GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, &systemTime, "HH':'mm':'ss",
		timeString, 100);
	GetTimeFormat(LOCALE_SYSTEM_DEFAULT, 0, &systemTime, "H", currtime, 3);
	if (lasttime> atoi(currtime))
		printDate = 1;
	lasttime = atoi(currtime);

#else
	struct tm *ptime;
	char dateString[10];
	time_t t = time(NULL);
	ptime = localtime(&t);
	strftime(timeString, 100, "%H:%M:%S", ptime);
	strftime(currtime, 3, "%H", ptime);
	if (lasttime>atoi(currtime))
		printDate = 1;
	lasttime = atoi(currtime);
/*
	   if (printDate)
	   {
	      printDate = 0;
	      strftime(dateString, 10, "%D", ptime);
	      fprintf(stdout, "---------Date %s---------\n",
	              dateString);
	   }
	   struct timeval systemTime;
	   gettimeofday(&systemTime, NULL);
	   fprintf (stdout, "%s:%03ld  ", timeString,
	               (long)systemTime.tv_usec/1000);*/
#endif
}

void chrSetTraceThreshold(OOUINT32 traceLevel)
{
	chr_traceLevel = traceLevel;
}


void chrTrace(char *file, long line, OOUINT32 traceLevel, const char * fmtspec, ...)
{
	char *filename;
	if (traceLevel > chr_traceLevel) return;

	logDateTime();

	if (chr_printTime) {
		filename = strrchr(file, '/');

		if (filename) {
			filename++;
		}
		else {
			filename = file;
		}

	}


	chr_printTime = (OOBOOL)(0 != strchr(fmtspec, '\n'));
}
