/**
  * Win32 UTF-8 wrapper
  *
  * ----
  *
  * kernel32.dll functions.
  */

#include "win32_utf8.h"

// GetStartupInfo
// --------------
static char *startupinfo_desktop = NULL;
static char *startupinfo_title = NULL;
// --------------

BOOL WINAPI CreateDirectoryU(
	__in LPCSTR lpPathName,
	__in_opt LPSECURITY_ATTRIBUTES lpSecurityAttributes
)
{
	// Hey, let's make this recursive while we're at it.
	BOOL ret;
	size_t i;
	WCHAR_T_DEC(lpPathName);
	StringToUTF16(lpPathName_w, lpPathName, lpPathName_len);

	for(i = 0; i < wcslen(lpPathName_w); i++) {
		if(lpPathName_w[i] == L'\\' || lpPathName_w[i] == L'/') {
			wchar_t old_c = lpPathName_w[i + 1];
			lpPathName_w[i + 1] = L'\0';
			lpPathName_w[i] = L'/';
			ret = CreateDirectoryW(lpPathName_w, NULL);
			lpPathName_w[i + 1] = old_c;
		}
	}
	VLA_FREE(lpPathName_w);
	return ret;
}

HANDLE WINAPI CreateFileU(
	__in     LPCSTR lpFileName,
	__in     DWORD dwDesiredAccess,
	__in     DWORD dwShareMode,
	__in_opt LPSECURITY_ATTRIBUTES lpSecurityAttributes,
	__in     DWORD dwCreationDisposition,
	__in     DWORD dwFlagsAndAttributes,
	__in_opt HANDLE hTemplateFile
)
{
	HANDLE ret;
	WCHAR_T_DEC(lpFileName);
	StringToUTF16(lpFileName_w, lpFileName, lpFileName_len);
	// log_printf("CreateFileU(\"%s\")\n", lpFileName);
	ret = CreateFileW(
		lpFileName_w, dwDesiredAccess, dwShareMode | FILE_SHARE_READ, lpSecurityAttributes,
		dwCreationDisposition, dwFlagsAndAttributes, hTemplateFile
	);
	VLA_FREE(lpFileName_w);
	return ret;
}

BOOL WINAPI CreateProcessU(
	__in_opt    LPCSTR lpAppName,
	__inout_opt LPSTR lpCmdLine,
	__in_opt    LPSECURITY_ATTRIBUTES lpProcessAttributes,
	__in_opt    LPSECURITY_ATTRIBUTES lpThreadAttributes,
	__in        BOOL bInheritHandles,
	__in        DWORD dwCreationFlags,
	__in_opt    LPVOID lpEnvironment,
	__in_opt    LPCSTR lpCurrentDirectory,
	__in        LPSTARTUPINFOA lpSI,
	__out       LPPROCESS_INFORMATION lpProcessInformation
)
{
	BOOL ret;
	STARTUPINFOW lpSI_w;
	WCHAR_T_DEC(lpAppName);
	WCHAR_T_DEC(lpCmdLine);
	WCHAR_T_DEC(lpCurrentDirectory);

	lpAppName_w = StringToUTF16_VLA(lpAppName_w, lpAppName, lpAppName_len);
	lpCmdLine_w = StringToUTF16_VLA(lpCmdLine_w, lpCmdLine, lpCmdLine_len);
	lpCurrentDirectory_w = StringToUTF16_VLA(lpCurrentDirectory_w, lpCurrentDirectory, lpCurrentDirectory_len);

	if(lpSI) {
		size_t si_lpDesktop_len = strlen(lpSI->lpDesktop) + 1;
		VLA(wchar_t, si_lpDesktopW, si_lpDesktop_len);
		size_t si_lpTitle_len = strlen(lpSI->lpTitle) + 1;
		VLA(wchar_t, si_lpTitleW, si_lpTitle_len);

		// At least the structure sizes are identical here
		memcpy(&lpSI_w, lpSI, sizeof(STARTUPINFOW));
		si_lpDesktopW = StringToUTF16_VLA(si_lpDesktopW, lpSI->lpDesktop, si_lpDesktop_len);
		si_lpTitleW = StringToUTF16_VLA(si_lpTitleW, lpSI->lpTitle, si_lpTitle_len);

		lpSI_w.lpDesktop = si_lpDesktopW;
		lpSI_w.lpTitle = si_lpTitleW;
	} else {
		ZeroMemory(&lpSI_w, sizeof(STARTUPINFOW));
	}
	// "Set this member to NULL before passing the structure to CreateProcess,"
	// MSDN says.
	lpSI_w.lpReserved = NULL;
	ret = CreateProcessW(
		lpAppName_w,
		lpCmdLine_w,
		lpProcessAttributes,
		lpThreadAttributes,
		bInheritHandles,
		dwCreationFlags,
		lpEnvironment,
		lpCurrentDirectory_w,
		&lpSI_w,
		lpProcessInformation
	);
	VLA_FREE(lpSI_w.lpDesktop);
	VLA_FREE(lpSI_w.lpTitle);
	VLA_FREE(lpAppName_w);
	VLA_FREE(lpCmdLine_w);
	VLA_FREE(lpCurrentDirectory_w);
	return ret;
}

DWORD WINAPI GetModuleFileNameU(
	__in_opt HMODULE hModule,
	__out_ecount_part(nSize, return + 1) LPSTR lpFilename,
	__in DWORD nSize
)
{
	VLA(wchar_t, lpFilename_w, nSize);
	DWORD ret = GetModuleFileNameW(hModule, lpFilename_w, nSize);
	StringToUTF8(lpFilename, lpFilename_w, nSize);
	// log_printf("GetModuleFileNameU() -> %s\n", lpFilename);
	VLA_FREE(lpFilename_w);
	return ret;
}

BOOL WINAPI SetCurrentDirectoryU(
	__in LPCSTR lpPathName
)
{
	BOOL ret;
	WCHAR_T_DEC(lpPathName);
	StringToUTF16(lpPathName_w, lpPathName, lpPathName_len);
	// log_printf("SetCurrentDirectoryU(\"%s\")\n", lpPathName);
	ret = SetCurrentDirectoryW(lpPathName_w);
	VLA_FREE(lpPathName_w);
	return ret;
}

DWORD WINAPI GetCurrentDirectoryU(
	__in DWORD nBufferLength,
	__out_ecount_part_opt(nBufferLength, return + 1) LPSTR lpBuffer
)
{
	DWORD ret;
	VLA(wchar_t, lpBuffer_w, nBufferLength);

	if(!lpBuffer) {
		lpBuffer_w = NULL;
	}
	ret = GetCurrentDirectoryW(nBufferLength, lpBuffer_w);
	if(lpBuffer) {
		StringToUTF8(lpBuffer, lpBuffer_w, nBufferLength);
	} else {
		// Hey, let's be nice and return the _actual_ length.
		VLA(wchar_t, lpBufferReal_w, ret);
		GetCurrentDirectoryW(ret, lpBufferReal_w);
		ret = StringToUTF8(NULL, lpBufferReal_w, 0);
		VLA_FREE(lpBufferReal_w);
	}
	VLA_FREE(lpBuffer_w);
	return ret;
}

VOID WINAPI GetStartupInfoU(
	__out LPSTARTUPINFOA lpSI
)
{
	STARTUPINFOW si_w;
	GetStartupInfoW(&si_w);

	// I would have put this code into kernel32_init, but apparently
	// GetStartupInfoW is "not safe to be called inside DllMain".
	// So unsafe in fact that Wine segfaults when I tried it
	if(!startupinfo_desktop) {
		size_t lpDesktop_len = wcslen(si_w.lpDesktop) + 1;
		startupinfo_desktop = (char*)malloc(lpDesktop_len * UTF8_MUL * sizeof(char));
		StringToUTF8(startupinfo_desktop, si_w.lpDesktop, lpDesktop_len);
	}
	if(!startupinfo_title) {
		size_t lpTitle_len = wcslen(si_w.lpTitle) + 1;
		startupinfo_title = (char*)malloc(lpTitle_len * UTF8_MUL * sizeof(char));
		StringToUTF8(startupinfo_title, si_w.lpTitle, lpTitle_len);
	}
	memcpy(lpSI, &si_w, sizeof(STARTUPINFOA));
	lpSI->lpDesktop = startupinfo_desktop;
	lpSI->lpTitle = startupinfo_title;
}

static void CopyFindDataWToA(
	__out LPWIN32_FIND_DATAA w32fd_a,
	__in LPWIN32_FIND_DATAW w32fd_w
	)
{
	w32fd_a->dwFileAttributes = w32fd_w->dwFileAttributes;
	w32fd_a->ftCreationTime = w32fd_w->ftCreationTime;
	w32fd_a->ftLastAccessTime = w32fd_w->ftLastAccessTime;
	w32fd_a->ftLastWriteTime = w32fd_w->ftLastWriteTime;
	w32fd_a->nFileSizeHigh = w32fd_w->nFileSizeHigh;
	w32fd_a->nFileSizeLow = w32fd_w->nFileSizeLow;
	w32fd_a->dwReserved0 = w32fd_w->dwReserved0;
	w32fd_a->dwReserved1 = w32fd_w->dwReserved1;
	// We can't use StringToUTF8 for constant memory due to UTF8_MUL, so...
	WideCharToMultiByte(CP_UTF8, 0, w32fd_w->cFileName, -1, w32fd_a->cFileName, MAX_PATH, NULL, NULL);
	WideCharToMultiByte(CP_UTF8, 0, w32fd_w->cAlternateFileName, -1, w32fd_a->cAlternateFileName, 14, NULL, NULL);
#ifdef _MAC
	w32fd_a->dwFileType = w32fd_w->dwReserved1;
	w32fd_a->dwCreatorType = w32fd_w->dwCreatorType;
	w32fd_a->wFinderFlags = w32fd_w->wFinderFlags;
#endif
}

HANDLE WINAPI FindFirstFileU(
	__in  LPCSTR lpFileName,
	__out LPWIN32_FIND_DATAA lpFindFileData
)
{
	HANDLE ret;
	DWORD last_error;
	WIN32_FIND_DATAW lpFindFileDataW;

	WCHAR_T_DEC(lpFileName);
	StringToUTF16(lpFileName_w, lpFileName, lpFileName_len);
	ret = FindFirstFileW(lpFileName_w, &lpFindFileDataW);
	last_error = GetLastError();
	CopyFindDataWToA(lpFindFileData, &lpFindFileDataW);
	SetLastError(last_error);
	VLA_FREE(lpFileName_w);
	return ret;
}

BOOL WINAPI FindNextFileU(
	__in  HANDLE hFindFile,
	__out LPWIN32_FIND_DATAA lpFindFileData
)
{
	BOOL ret;
	DWORD last_error;
	WIN32_FIND_DATAW lpFindFileDataW;

	ret = FindNextFileW(hFindFile, &lpFindFileDataW);
	last_error = GetLastError();
	CopyFindDataWToA(lpFindFileData, &lpFindFileDataW);
	SetLastError(last_error);
	return ret;
}

HMODULE WINAPI LoadLibraryU(
	__in LPCSTR lpLibFileName
)
{
	HMODULE ret;
	WCHAR_T_DEC(lpLibFileName);
	StringToUTF16(lpLibFileName_w, lpLibFileName, lpLibFileName_len);
	ret = LoadLibraryW(lpLibFileName_w);
	VLA_FREE(lpLibFileName_w);
	return ret;
}

DWORD WINAPI FormatMessageU(
	__in DWORD dwFlags,
	__in_opt LPCVOID lpSource,
	__in DWORD dwMessageId,
	__in DWORD dwLanguageId,
	__out LPSTR lpBuffer,
	__in DWORD nSize,
	__in_opt va_list *Arguments
)
{
	wchar_t *lpBufferW = NULL;

	DWORD ret = FormatMessageW(
		FORMAT_MESSAGE_ALLOCATE_BUFFER | dwFlags, lpSource,
		dwMessageId, dwLanguageId, (LPTSTR)&lpBufferW, nSize, Arguments
	);
	if(!ret) {
		return ret;
	}
	if(dwFlags & FORMAT_MESSAGE_ALLOCATE_BUFFER) {
		LPSTR* lppBuffer = (LPSTR*)lpBuffer;

		ret = max(ret, nSize);

		*lppBuffer = LocalAlloc(0, ret * sizeof(char) * UTF8_MUL);
		lpBuffer = *lppBuffer;
	} else {
		ret = min(ret, nSize);
	}
	ret = StringToUTF8(lpBuffer, lpBufferW, ret);
	LocalFree(lpBufferW);
	return ret;
}

// Patcher functions
// -----------------
int kernel32_init(HMODULE hMod)
{
	return 0;
}

void kernel32_exit()
{
	SAFE_FREE(startupinfo_desktop);
	SAFE_FREE(startupinfo_title);
}