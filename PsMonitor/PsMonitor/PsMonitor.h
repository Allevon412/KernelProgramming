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
};