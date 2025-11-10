#pragma once

//#include <windows.h>
//#include <stdio.h>

#include "tnetwork.h"

class TScript {
public:
	TScript(TNetwork &RefNetwork)
		: fp(NULL)
		, script(NULL)
		, Network(RefNetwork)
	{
	}

	~TScript() {}
	BOOL processScript(char *data);
	void initScript(char *filename);

private:
	FILE *fp;
	char *script;
	TNetwork &Network;
};
