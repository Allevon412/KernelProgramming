#pragma once


enum class ItemType : short {
	None,
	ProcessCreate,
	ProcessExit
};

struct ItemHeader {
	ItemType Type;
	USHORT size;
	LARGE_INTEGER Time;
};

struct ProcessExitInfo : ItemHeader {
	ULONG ProcessId; // ulong is used because kernel and user headers a like use ulong type. instead of dword in user.
}; 

struct ProcessCreateInfo : ItemHeader {
	ULONG ProcessId;
	ULONG ParentProcessId;
	USHORT CommandLineLength;
	USHORT CommandLineOffset;
	USHORT ImageFileNameLength;
	USHORT ImageFileNameOffset;
};



