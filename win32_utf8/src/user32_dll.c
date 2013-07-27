/**
  * Win32 UTF-8 wrapper
  *
  * ----
  *
  * user32.dll functions.
  */

#include "win32_utf8.h"

#define RegisterClassBaseConvert(wcNew, lpwcOld) \
	size_t lpszClassName_len = strlen(lpwcOld->lpszClassName) + 1; \
	size_t lpszMenuName_len = strlen(lpwcOld->lpszMenuName) + 1; \
	VLA(wchar_t, lpszClassName_w, lpszClassName_len); \
	VLA(wchar_t, lpszMenuName_w, lpszMenuName_len); \
	\
	lpszClassName_w = StringToUTF16_VLA(lpszClassName_w, lpwcOld->lpszClassName, lpszClassName_len); \
	lpszMenuName_w = StringToUTF16_VLA(lpszMenuName_w, lpwcOld->lpszMenuName, lpszMenuName_len); \
	\
	wcNew.style = lpwcOld->style; \
	wcNew.lpfnWndProc = lpwcOld->lpfnWndProc; \
	wcNew.cbClsExtra = lpwcOld->cbClsExtra; \
	wcNew.cbWndExtra = lpwcOld->cbWndExtra; \
	wcNew.hInstance = lpwcOld->hInstance; \
	wcNew.hIcon = lpwcOld->hIcon; \
	wcNew.hCursor = lpwcOld->hCursor; \
	wcNew.hbrBackground = lpwcOld->hbrBackground; \
	wcNew.lpszClassName = lpszClassName_w; \
	wcNew.lpszMenuName = lpszMenuName_w;

#define RegisterClassBaseClean() \
	VLA_FREE(lpszClassName_w); \
	VLA_FREE(lpszMenuName_w)

ATOM WINAPI RegisterClassU(
	__in CONST WNDCLASSA *lpWndClass
)
{
	ATOM ret;
	WNDCLASSW WndClassW;
	RegisterClassBaseConvert(WndClassW, lpWndClass);
	ret = RegisterClassW(&WndClassW);
	RegisterClassBaseClean();
	return ret;
}

ATOM WINAPI RegisterClassExU(
	__in CONST WNDCLASSEXA *lpWndClass
)
{
	ATOM ret;
	WNDCLASSEXW WndClassW;
	RegisterClassBaseConvert(WndClassW, lpWndClass);
	WndClassW.cbSize = lpWndClass->cbSize;
	WndClassW.hIconSm = lpWndClass->hIconSm;
	ret = RegisterClassExW(&WndClassW);
	RegisterClassBaseClean();
	return ret;
}

HWND WINAPI CreateWindowExU(
	__in DWORD dwExStyle,
	__in_opt LPCSTR lpClassName,
	__in_opt LPCSTR lpWindowName,
	__in DWORD dwStyle,
	__in int X,
	__in int Y,
	__in int nWidth,
	__in int nHeight,
	__in_opt HWND hWndParent,
	__in_opt HMENU hMenu,
	__in_opt HINSTANCE hInstance,
	__in_opt LPVOID lpParam
)
{
	HWND ret;
	WCHAR_T_DEC(lpClassName);
	WCHAR_T_DEC(lpWindowName);
	lpClassName_w = StringToUTF16_VLA(lpClassName_w, lpClassName, lpClassName_len);
	lpWindowName_w = StringToUTF16_VLA(lpWindowName_w, lpWindowName, lpWindowName_len);

	ret = CreateWindowExW(
		dwExStyle, lpClassName_w, lpWindowName_w, dwStyle, X, Y, nWidth, nHeight, hWndParent, hMenu,
		hInstance, lpParam
	);
	VLA_FREE(lpClassName_w);
	VLA_FREE(lpWindowName_w);
	return ret;
}

BOOL WINAPI SetWindowTextU(
	__in HWND hWnd,
	__in_opt LPCSTR lpString
)
{
	BOOL ret;
	WCHAR_T_DEC(lpString);
	lpString_w = StringToUTF16_VLA(lpString_w, lpString, lpString_len);
	ret = SetWindowTextW(hWnd, lpString_w);
	VLA_FREE(lpString_w);
	return ret;
}

int WINAPI MessageBoxU(
	__in_opt HWND hWnd,
	__in_opt LPCSTR lpText,
	__in_opt LPCSTR lpCaption,
	__in UINT uType
)
{
	int ret;
	WCHAR_T_DEC(lpText);
	WCHAR_T_DEC(lpCaption);
	lpText_w = StringToUTF16_VLA(lpText_w, lpText, lpText_len);
	lpCaption_w = StringToUTF16_VLA(lpCaption_w, lpCaption, lpCaption_len);
	ret = MessageBoxW(hWnd, lpText_w, lpCaption_w, uType);
	VLA_FREE(lpText_w);
	VLA_FREE(lpCaption_w);
	return ret;
}

LPSTR WINAPI CharNextU(
	__in LPSTR lpsz
)
{
	LPSTR ret;
	extern UINT fallback_codepage;

	if(lpsz == NULL || *lpsz == '\0') {
		ret = lpsz;
	}
	else if(IsDBCSLeadByteEx(fallback_codepage, lpsz[0])) {
		int lpsz_len = strlen(lpsz);
		if(lpsz_len < 2) {
			ret = lpsz + 1;
		} else {
			ret = lpsz + 2;
		}
	}
	else {
		ret = lpsz + 1;
		if(!IsDBCSLeadByteEx(fallback_codepage, ret[0])) {
			// Get next UTF-8 char
			while((*ret & 0xc0) == 0x80)
				++ret;
		}
	}
	return ret;
}