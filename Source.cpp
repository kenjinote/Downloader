#pragma comment(linker,"\"/manifestdependency:type='win32' name='Microsoft.Windows.Common-Controls' version='6.0.0.0' processorArchitecture='*' publicKeyToken='6595b64144ccf1df' language='*'\"")

#pragma comment(lib, "shlwapi")
#pragma comment(lib, "wininet")

#include <stdio.h>
#include <windows.h>
#include <wininet.h>
#include <shlwapi.h>
#include "resource.h"

TCHAR szClassName[] = TEXT("Window");

#define IDC_LIST 201
#define IDC_EDIT 202
#define SEMAPHORE_NUM 2
#define WM_ENDTHREAD (WM_APP+100)

struct ListItemData
{
	ListItemData() : index(-1), data(0), hThread(0), state(0), url(0) {}
	~ListItemData()
	{
		TerminateThread(hThread, -1);
		WaitForSingleObject(hThread, INFINITE);
		CloseHandle(hThread);
		delete[]url;
	}
	DWORD data;
	int index;
	DWORD state;
	HANDLE hThread;
	TCHAR*url;
	static TCHAR*lpszDirectory;
	static HWND hWnd;
	static HWND hList;
};

TCHAR* ListItemData::lpszDirectory;
HWND ListItemData::hWnd;
HWND ListItemData::hList;

static TCHAR *url2filename(TCHAR *url)
{
	TCHAR *top = url;
	TCHAR *p = url;
	while (*p != L'\0')
	{
		if (*p == L'/')
			top = p + 1;
		if (*p == L'?')
		{
			*p = L'\0';
			break;
		}
		p++;
	}
	return top;
}

DWORD WINAPI ThreadFunc(LPVOID p)
{
	HINTERNET hInternet;
	HINTERNET hFile;
	RECT rect;
	__int64 iTotalFileSize = 0;
	__int64 iDownLoadFileSize = 0;
	TCHAR szBuf[1024], file[1024];
	DWORD BufSizeTextSize = _countof(szBuf);
	ListItemData* pListItemData = (ListItemData*)p;
	pListItemData->state++;
	SendMessage(ListItemData::hList, LB_GETITEMRECT, pListItemData->index, (LPARAM)&rect);
	InvalidateRect(ListItemData::hList, &rect, FALSE);
	hInternet = InternetOpen(0,INTERNET_OPEN_TYPE_PRECONFIG,0,0,0);
	if (hInternet == 0)
	{
		pListItemData->state = -1;
		goto END0;
	}
	hFile = InternetOpenUrl(hInternet,pListItemData->url,0,0,INTERNET_FLAG_RELOAD,0);
	if (hFile == 0)
	{
		pListItemData->state = -2;
		goto END1;
	}
	TCHAR *name = url2filename(pListItemData->url); // url を壊してしまう
	if (name == 0 || name[0] == '\0') { lstrcpy(file, L"index.html"); name = file; }
	TCHAR szFilePath[MAX_PATH];
	lstrcpy(szFilePath, ListItemData::lpszDirectory);
	PathAppend(szFilePath, name);
	HANDLE			FindHandle;
	WIN32_FIND_DATA	FindData;
	FindHandle = FindFirstFile(szFilePath, &FindData);
	if ((FindHandle == INVALID_HANDLE_VALUE))
	{
		::FindClose(FindHandle);
	}
	else
	{
		TCHAR fname[_MAX_FNAME];
		TCHAR ext[_MAX_EXT];
		_wsplitpath_s(szFilePath, 0, 0, 0, 0, fname, _countof(fname), ext, _countof(ext));
		for (DWORD i = 1;; i++)
		{
			wsprintf(szBuf, L"%s%s_%d%s", ListItemData::lpszDirectory, fname, i, ext);
			FindHandle = FindFirstFile(szBuf, &FindData);
			if ((FindHandle == INVALID_HANDLE_VALUE))break;
			FindClose(FindHandle);
		}
		lstrcpy(szFilePath, szBuf);
		FindClose(FindHandle);
	}
	FILE *fno;	
	if (_wfopen_s(&fno, szFilePath, L"wb") == 0)
	{
		HttpQueryInfo(hFile,HTTP_QUERY_CONTENT_LENGTH,szBuf,&BufSizeTextSize,0);
		iTotalFileSize = _wtoi64(szBuf);
		BYTE byBuffer[1024 * 4];
		DWORD dwReadSize;
		for (;;)
		{
			if (InternetReadFile(hFile, byBuffer, _countof(byBuffer), &dwReadSize))
			{
				iDownLoadFileSize += dwReadSize;
				pListItemData->data = (iTotalFileSize == 0) ? 0 : (DWORD)(100 * iDownLoadFileSize / iTotalFileSize);
				SendMessage(ListItemData::hList, LB_GETITEMRECT, pListItemData->index, (LPARAM)&rect);
				InvalidateRect(ListItemData::hList, &rect, FALSE);
				fwrite(byBuffer, 1, dwReadSize, fno);
			}
			if (dwReadSize == 0) break;
		}
		fclose(fno);
	}
	else
	{
		pListItemData->state = -3;
		goto END2;
	}
	pListItemData->state++;
END2:
	InternetCloseHandle(hFile);
END1:
	InternetCloseHandle(hInternet);
END0:
	SendMessage(ListItemData::hList, LB_GETITEMRECT, pListItemData->index, (LPARAM)&rect);
	InvalidateRect(ListItemData::hList, &rect, FALSE);
	PostMessage(ListItemData::hWnd, WM_ENDTHREAD, 0, 0);
	ExitThread(0);
}

bool ListAddText(HWND hList, TCHAR* text)
{
	if (!text) return false;
	TCHAR *p;
	LPWSTR next;
	if (p = wcstok_s(text, L"\r\n", &next))
	{
		do
		{
			DWORD nIndex = (DWORD)SendMessage(hList, LB_GETCOUNT, 0, 0);
			SendMessage(hList, LB_INSERTSTRING, nIndex, (LPARAM)p);
			ListItemData *d = new ListItemData;
			d->index = nIndex;
			const int nSize = lstrlen(p) + 1;
			d->url = new TCHAR[nSize];
			wcscpy_s(d->url, nSize, p);
			SendMessage(hList, LB_SETITEMDATA, nIndex, (LPARAM)d);
			p = wcstok_s(0, L"\r\n", &next);
		} while (p);
	}
	return true;
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	static HWND hEdit;
	static HWND hList;
	static DWORD iNextListItem = 0;
	static DWORD iTotalListItem = 0;
	static DWORD iEndListItem = 0;
	static BOOL bRun = FALSE;
	static TCHAR szDirectory[MAX_PATH];
	switch (msg)
	{
	case WM_CREATE:
		hEdit = CreateWindowEx(WS_EX_CLIENTEDGE, L"EDIT", 0, WS_CHILD | WS_VISIBLE | WS_TABSTOP | ES_AUTOHSCROLL, 0, 0, 0, 0, hWnd, (HMENU)IDC_EDIT, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		{
			TCHAR szModulePath[MAX_PATH];
			GetModuleFileNameW(((LPCREATESTRUCT)lParam)->hInstance, szModulePath, _countof(szModulePath));
			PathRemoveFileSpec(szModulePath);
			PathAddBackslash(szModulePath);
			SetWindowText(hEdit, szModulePath);
		}
		hList = CreateWindowEx(WS_EX_CLIENTEDGE, L"LISTBOX", 0, WS_CHILD | WS_VISIBLE | WS_VSCROLL | WS_TABSTOP | LBS_NOINTEGRALHEIGHT | LBS_OWNERDRAWFIXED | LBS_HASSTRINGS, 0, 0, 0, 0, hWnd, (HMENU)IDC_LIST, ((LPCREATESTRUCT)lParam)->hInstance, 0);
		ListItemData::lpszDirectory = szDirectory;
		ListItemData::hWnd = hWnd;
		ListItemData::hList = hList;
		break;
	case WM_COMMAND:
		switch (LOWORD(wParam))
		{
		case IDC_EDIT:
			if (HIWORD(wParam) == EN_CHANGE)
			{
				GetWindowText(hEdit, szDirectory, _countof(szDirectory));
				PathAddBackslash(szDirectory);
			}
			return 0;
		case ID_RUN:
			if (!bRun && iNextListItem < iTotalListItem)
			{
				bRun = TRUE;
				DWORD i;
				for (i = iNextListItem; i < (iNextListItem + SEMAPHORE_NUM) && i < iTotalListItem; i++)
				{
					ListItemData *p = (ListItemData*)SendMessage(hList, LB_GETITEMDATA, i, 0);
					DWORD dwParam;
					p->hThread = CreateThread(0, 0, ThreadFunc, p, 0, &dwParam);
				}
				iNextListItem = i;
			}
			return 0;
		case ID_PASTE:
			{
				const HWND hTargetCtrl = GetFocus();
				if (HIWORD(wParam) != 0 && hTargetCtrl == hEdit)
				{
					SendMessage(hEdit, WM_PASTE, 0, 0);
				}
				else
				{
					HANDLE hText;
					TCHAR *lpszBuf;
					OpenClipboard(0);
					hText = GetClipboardData(CF_UNICODETEXT);
					if (hText)
					{
						lpszBuf = (TCHAR *)GlobalLock(hText);
						SendMessage(hList, WM_SETREDRAW, FALSE, 0);
						ListAddText(hList, lpszBuf);
						SendMessage(hList, WM_SETREDRAW, TRUE, 0);
						InvalidateRect(hList, 0, TRUE);
						GlobalUnlock(hText);
					}
					CloseClipboard();
					iTotalListItem = (DWORD)SendMessage(hList, LB_GETCOUNT, 0, 0);
				}
			}
			return 0;
		case ID_PASTE_AND_RUN:
			{
				SetFocus(hList);
				SendMessage(hWnd, WM_COMMAND, ID_PASTE, 0);
				SendMessage(hWnd, WM_COMMAND, ID_RUN, 0);
			}
			return 0;
		case ID_CLEAR:
			{
				bRun = FALSE;
				for (DWORD i = 0; i < iTotalListItem; i++)
				{
					ListItemData *p = (ListItemData*)SendMessage(hList, LB_GETITEMDATA, i, 0);
					delete p;
				}
				SendMessage(hList, LB_RESETCONTENT, 0, 0);
				iNextListItem = 0;
				iTotalListItem = 0;
				iEndListItem = 0;
			}
			return 0;
		}
		break;
	case WM_SETFOCUS:
		SetFocus(hList);
		break;
	case WM_DRAWITEM:
		if ((UINT)wParam == IDC_LIST)
		{
			LPDRAWITEMSTRUCT lpdis = (LPDRAWITEMSTRUCT)lParam;
			if (lpdis->itemID == -1)
			{
				break;
			}
			TCHAR szText[MAX_PATH];
			HBRUSH hBrush;
			SendMessage(hList, LB_GETTEXT, lpdis->itemID, (LPARAM)szText);
			if ((lpdis->itemState)&(ODS_SELECTED))
			{
				hBrush = CreateSolidBrush(GetSysColor(COLOR_HIGHLIGHT));
				SetBkColor(lpdis->hDC, GetSysColor(COLOR_HIGHLIGHT));
				SetTextColor(lpdis->hDC, GetSysColor(COLOR_HIGHLIGHTTEXT));
			}
			else
			{
				hBrush = CreateSolidBrush(GetSysColor(COLOR_WINDOW));
				SetBkColor(lpdis->hDC, GetSysColor(COLOR_WINDOW));
				SetTextColor(lpdis->hDC, GetSysColor(COLOR_WINDOWTEXT));
			}
			FillRect(lpdis->hDC, &lpdis->rcItem, hBrush);
			DeleteObject(hBrush);
			DrawText(lpdis->hDC, szText, -1, &lpdis->rcItem, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
			ListItemData *p = (ListItemData*)SendMessage(lpdis->hwndItem, LB_GETITEMDATA, lpdis->itemID, 0);
			TCHAR szTemp[32];
			switch (p->state)
			{
			case -5:SetTextColor(lpdis->hDC, RGB(0xff, 0, 0)); lstrcpy(szTemp, L"エラー(5)"); break;
			case -4:SetTextColor(lpdis->hDC, RGB(0xff, 0, 0)); lstrcpy(szTemp, L"エラー(4)"); break;
			case -3:SetTextColor(lpdis->hDC, RGB(0xff, 0, 0)); lstrcpy(szTemp, L"エラー(3)"); break;
			case -2:SetTextColor(lpdis->hDC, RGB(0xff, 0, 0)); lstrcpy(szTemp, L"エラー(2)"); break;
			case -1:SetTextColor(lpdis->hDC, RGB(0xff, 0, 0)); lstrcpy(szTemp, L"エラー(1)"); break;
			case 0:SetTextColor(lpdis->hDC, RGB(192, 192, 192)); lstrcpy(szTemp, L"待機中"); break;
			case 1:wsprintf(szTemp, L"%d%%", p->data); break;
			case 2:SetTextColor(lpdis->hDC, RGB(0, 192, 0)); lstrcpy(szTemp, L"完了"); break;
			}
			DrawText(lpdis->hDC, szTemp, -1, &lpdis->rcItem, DT_RIGHT | DT_VCENTER | DT_SINGLELINE);
		}
		break;
	case WM_ENDTHREAD:
		iEndListItem++;
		if (iNextListItem < iTotalListItem)
		{
			ListItemData *p = (ListItemData*)SendMessage(hList, LB_GETITEMDATA, iNextListItem++, 0);
			if (p)
			{
				DWORD dwParam;
				p->hThread = CreateThread(0, 0, ThreadFunc, p, 0, &dwParam);
			}
		}
		if (iEndListItem == iTotalListItem)
		{
			bRun = FALSE;
			MessageBox(hWnd, L"ダウンロードが終了しました。", L"確認", 0);
		}
		break;
	case WM_SIZE:
		MoveWindow(hEdit, 4, 4, LOWORD(lParam) - 8, 32, TRUE);
		MoveWindow(hList, 4, 32 + 8, LOWORD(lParam) - 8, HIWORD(lParam) - 32 - 8 - 4, TRUE);
		break;
	case WM_CLOSE:
		DestroyWindow(hWnd);
		break;
	case WM_DESTROY:
		for (DWORD i = 0; i < iTotalListItem; i++)
		{
			ListItemData *p = (ListItemData*)SendMessage(hList, LB_GETITEMDATA, i, 0);
			delete p;
		}
		PostQuitMessage(0);
		break;
	default:
		return DefDlgProc(hWnd, msg, wParam, lParam);
	}
	return 0;
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPreInst, LPSTR pCmdLine, int nCmdShow)
{
	MSG msg;
	WNDCLASS wndclass = {
		0,
		WndProc,
		0,
		DLGWINDOWEXTRA,
		hInstance,
		0,
		LoadCursor(0,IDC_ARROW),
		0,
		MAKEINTRESOURCE(IDR_MENU1),
		szClassName
	};
	RegisterClass(&wndclass);
	HWND hWnd = CreateWindow(
		szClassName,
		TEXT("貼り付けられた URL のファイルをダウンロードする"),
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		0,
		CW_USEDEFAULT,
		0,
		0,
		0,
		hInstance,
		0
	);
	ShowWindow(hWnd, SW_SHOWDEFAULT);
	UpdateWindow(hWnd);
	HACCEL hAccel = LoadAccelerators(hInstance, MAKEINTRESOURCE(IDR_ACCELERATOR1));
	while (GetMessage(&msg, 0, 0, 0))
	{
		if (!TranslateAccelerator(hWnd, hAccel, &msg) && !IsDialogMessage(hWnd, &msg))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}
	}
	return (int)msg.wParam;
}
