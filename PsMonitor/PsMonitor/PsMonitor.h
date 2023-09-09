#pragma once
#include "FastMutex.h"

template<typename T>
struct FullItem {
	LIST_ENTRY Entry;
	T Data;
};

struct Globals {
	LIST_ENTRY ItemsHead;
	ULONG ItemCount;
	FastMutex Mutex;
	ULONG MaxItems;
	WCHAR ExcludedPath[256];
	LARGE_INTEGER RegCookie;
};

const ULONG MaxNewProcesses = 32;

struct newProcGlobalList {
	FastMutex Mutex;
	ULONG NewProcesses[MaxNewProcesses];
	ULONG count;
};
