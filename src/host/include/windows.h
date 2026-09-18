/*
 * Thin Win32 API surface for %C — declaration-only; links kernel32.
 * Not a full Windows SDK. ANSI (*A) only; packages use path_to_sys.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

typedef int BOOL;
typedef unsigned int DWORD;
typedef void *HANDLE;

#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#define INVALID_FILE_ATTRIBUTES 0xffffffffu

#define GENERIC_READ 0x80000000u
#define GENERIC_WRITE 0x40000000u

#define FILE_SHARE_READ 0x00000001u
#define FILE_SHARE_WRITE 0x00000002u
#define FILE_SHARE_DELETE 0x00000004u

#define CREATE_ALWAYS 2u
#define OPEN_EXISTING 3u
#define OPEN_ALWAYS 4u
#define TRUNCATE_EXISTING 5u

#define FILE_ATTRIBUTE_NORMAL 0x00000080u
#define FILE_ATTRIBUTE_DIRECTORY 0x00000010u
#define FILE_FLAG_BACKUP_SEMANTICS 0x02000000u

#define FILE_BEGIN 0u
#define FILE_CURRENT 1u
#define FILE_END 2u

#define FILE_TYPE_DISK 1u
#define FILE_TYPE_CHAR 2u
#define FILE_TYPE_PIPE 3u

#define MOVEFILE_REPLACE_EXISTING 1u

#define ERROR_NO_MORE_FILES 18u
#define ERROR_ENVVAR_NOT_FOUND 203u

#define STD_INPUT_HANDLE ((DWORD)(0xfffffff6u))  /* (DWORD)-10 */
#define STD_OUTPUT_HANDLE ((DWORD)(0xfffffff5u)) /* (DWORD)-11 */
#define STD_ERROR_HANDLE ((DWORD)(0xfffffff4u))  /* (DWORD)-12 */

#define ENABLE_PROCESSED_INPUT 0x0001u
#define ENABLE_LINE_INPUT 0x0002u
#define ENABLE_ECHO_INPUT 0x0004u
#define ENABLE_WINDOW_INPUT 0x0008u
#define ENABLE_MOUSE_INPUT 0x0010u
#define ENABLE_PROCESSED_OUTPUT 0x0001u
#define ENABLE_WRAP_AT_EOL_OUTPUT 0x0002u
#define ENABLE_VIRTUAL_TERMINAL_PROCESSING 0x0004u

#define STARTF_USESTDHANDLES 0x00000100u
#define CREATE_NO_WINDOW 0x08000000u
#define HANDLE_FLAG_INHERIT 0x00000001u
#define INFINITE 0xffffffffu
#define WAIT_OBJECT_0 0u
#define WAIT_TIMEOUT 258u
#define WAIT_FAILED 0xffffffffu

typedef struct _FILETIME {
	DWORD dwLowDateTime;
	DWORD dwHighDateTime;
} FILETIME;

typedef struct _WIN32_FIND_DATAA {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD dwReserved0;
	DWORD dwReserved1;
	char cFileName[260];
	char cAlternateFileName[14];
} WIN32_FIND_DATAA;

typedef struct _BY_HANDLE_FILE_INFORMATION {
	DWORD dwFileAttributes;
	FILETIME ftCreationTime;
	FILETIME ftLastAccessTime;
	FILETIME ftLastWriteTime;
	DWORD dwVolumeSerialNumber;
	DWORD nFileSizeHigh;
	DWORD nFileSizeLow;
	DWORD nNumberOfLinks;
	DWORD nFileIndexHigh;
	DWORD nFileIndexLow;
} BY_HANDLE_FILE_INFORMATION;

typedef struct _COORD {
	short X;
	short Y;
} COORD;

typedef struct _SMALL_RECT {
	short Left;
	short Top;
	short Right;
	short Bottom;
} SMALL_RECT;

typedef struct _CONSOLE_SCREEN_BUFFER_INFO {
	COORD dwSize;
	COORD dwCursorPosition;
	unsigned short wAttributes;
	SMALL_RECT srWindow;
	COORD dwMaximumWindowSize;
} CONSOLE_SCREEN_BUFFER_INFO;

typedef struct _SECURITY_ATTRIBUTES {
	DWORD nLength;
	void *lpSecurityDescriptor;
	BOOL bInheritHandle;
} SECURITY_ATTRIBUTES;

typedef struct _STARTUPINFOA {
	DWORD cb;
	char *lpReserved;
	char *lpDesktop;
	char *lpTitle;
	DWORD dwX;
	DWORD dwY;
	DWORD dwXSize;
	DWORD dwYSize;
	DWORD dwXCountChars;
	DWORD dwYCountChars;
	DWORD dwFillAttribute;
	DWORD dwFlags;
	unsigned short wShowWindow;
	unsigned short cbReserved2;
	unsigned char *lpReserved2;
	HANDLE hStdInput;
	HANDLE hStdOutput;
	HANDLE hStdError;
} STARTUPINFOA;

typedef struct _PROCESS_INFORMATION {
	HANDLE hProcess;
	HANDLE hThread;
	DWORD dwProcessId;
	DWORD dwThreadId;
} PROCESS_INFORMATION;

HANDLE CreateFileA(const char *name, DWORD access, DWORD share, void *sec,
    DWORD disp, DWORD flags, HANDLE tmpl);
BOOL CloseHandle(HANDLE h);
BOOL ReadFile(HANDLE h, void *buf, DWORD n, DWORD *got, void *ov);
BOOL WriteFile(HANDLE h, const void *buf, DWORD n, DWORD *got, void *ov);
BOOL SetFilePointerEx(HANDLE h, int64_t dist, int64_t *neu, DWORD method);
DWORD GetFileType(HANDLE h);
BOOL GetFileInformationByHandle(HANDLE h, BY_HANDLE_FILE_INFORMATION *info);
BOOL DeleteFileA(const char *name);
BOOL RemoveDirectoryA(const char *name);
BOOL MoveFileExA(const char *from, const char *to, DWORD flags);
BOOL CreateDirectoryA(const char *name, void *sec);
DWORD GetFileAttributesA(const char *name);
HANDLE FindFirstFileA(const char *pat, WIN32_FIND_DATAA *data);
BOOL FindNextFileA(HANDLE hf, WIN32_FIND_DATAA *data);
BOOL FindClose(HANDLE hf);
DWORD GetLastError(void);

DWORD GetEnvironmentVariableA(const char *name, char *buf, DWORD n);
BOOL SetEnvironmentVariableA(const char *name, const char *val);
DWORD GetCurrentDirectoryA(DWORD n, char *buf);
BOOL SetCurrentDirectoryA(const char *path);
void Sleep(DWORD ms);
BOOL QueryPerformanceCounter(int64_t *c);
BOOL QueryPerformanceFrequency(int64_t *f);

HANDLE GetStdHandle(DWORD n);
BOOL GetConsoleMode(HANDLE h, DWORD *mode);
BOOL SetConsoleMode(HANDLE h, DWORD mode);
BOOL GetConsoleScreenBufferInfo(HANDLE h, CONSOLE_SCREEN_BUFFER_INFO *info);

BOOL CreatePipe(HANDLE *rd, HANDLE *wr, SECURITY_ATTRIBUTES *sa, DWORD size);
BOOL SetHandleInformation(HANDLE h, DWORD mask, DWORD flags);
BOOL CreateProcessA(const char *app, char *cmd, void *pa, void *ta,
    BOOL inherit, DWORD flags, void *env, const char *cwd, STARTUPINFOA *si,
    PROCESS_INFORMATION *pi);
DWORD WaitForSingleObject(HANDLE h, DWORD ms);
DWORD WaitForMultipleObjects(DWORD n, const HANDLE *hs, BOOL wait_all, DWORD ms);
BOOL GetExitCodeProcess(HANDLE h, DWORD *code);
BOOL TerminateProcess(HANDLE h, unsigned int code);
BOOL PeekNamedPipe(HANDLE h, void *buf, DWORD n, DWORD *read, DWORD *avail,
    DWORD *left);
DWORD SearchPathA(const char *path, const char *name, const char *ext,
    DWORD buflen, char *buf, char **filepart);
char *GetEnvironmentStringsA(void);
BOOL FreeEnvironmentStringsA(char *block);
