#pragma once

#define MAXIMUM_PATH_LENGTH 256

enum class ItemType : short {
	None,
	ProcessCreate,
	ProcessExit,
	ThreadCreate,
	ThreadExit,
	ImageLoad
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

struct ThreadCreateExitInfo : ItemHeader
{
	ULONG ProcessId;
	ULONG ThreadId;
	bool remote;
	ULONG CreateProcessId;
	ULONG CreatorThreadId;
};

struct ImageLoadInfo : ItemHeader
{
	ULONG ProcessId;
	PVOID ImageBase;
	USHORT ImageNameLength;
	USHORT ImageNameOffset;
};