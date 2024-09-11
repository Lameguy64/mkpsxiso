#ifndef _GLOBAL_H
#define _GLOBAL_H

#include <ctime>
#include <optional>

namespace global {

	extern time_t				BuildTime;
	extern bool					xa_edc;
	extern int					QuietMode;
	extern int					trackNum;
	extern int					noXA;
	extern std::optional<bool>	new_type;
};

#endif // _GLOBAL_H
