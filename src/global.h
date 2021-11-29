#ifndef _GLOBAL_H
#define _GLOBAL_H

#ifdef WIN32
#include <windows.h>
#else
#include <sys/time.h>
#endif

namespace global {

	extern time_t	BuildTime;
	extern int		QuietMode;
	extern int		noXA;
	extern int		NoLimit;
	extern int		trackNum;
};

#endif // _GLOBAL_H
