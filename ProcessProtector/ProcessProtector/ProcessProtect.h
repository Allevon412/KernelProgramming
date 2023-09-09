#pragma once

#define DRIVER_PREFIX "ProcessProtect "

#define PROCESS_TERMINATE 1

#include "FastMutex.h"

const int MaxPids = 256;

struct Globals
{
	int PidsCount; //current protected process count
	ULONG Pids[MaxPids]; //protected PIDs
	FastMutex lock;
	PVOID RegHandle; //registration cookie

	void Init()
	{
		lock.Init();
	}
};


