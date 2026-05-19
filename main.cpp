#ifndef UNICODE
#define UNICODE
#endif

#ifndef _UNICODE
#define _UNICODE
#endif

#include <windows.h>
#include <commctrl.h>
#include <tlhelp32.h>
#include <shlobj.h>

#include <cwchar>
#include <string>
#include <vector>

#include "resource.h"

#ifndef WC_LISTBOXW
#define WC_LISTBOXW L"LISTBOX"
#endif

#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "comctl32.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(linker, "/MANIFESTUAC:\"level='requireAdministrator' uiAccess='false'\"")

namespace
{
    const wchar_t kApplicationTitle[] = L"AegisCore System Restorer";
    const wchar_t kApplicationSignature[] = L"AegisCore System Restorer";
    const wchar_t kMainWindowClass[] = L"AegisCoreMainWindow";
    const wchar_t kPageWindowClass[] = L"AegisCorePageWindow";

    const COLORREF kColorBackground = RGB(18, 18, 18);
    const COLORREF kColorText = RGB(255, 255, 255);
    const COLORREF kColorAccent = RGB(45, 45, 45);

    WNDPROC g_oldGroupBoxProc = nullptr;
    WNDPROC g_oldTabProc = nullptr;

    const int kClientWidth = 920;
    const int kClientHeight = 600;

    enum class StartupEntryKind
    {
        Registry,
        File
    };

    struct StartupEntry
    {
        StartupEntryKind kind;
        HKEY root;
        REGSAM registryView;
        std::wstring valueName;
        std::wstring subKey;
        std::wstring filePath;
        std::wstring name;
        std::wstring command;
        std::wstring location;
    };

    struct OperationResult
    {
        std::vector<std::wstring> errors;

        void Add(const std::wstring& action, DWORD errorCode)
        {
            LPWSTR buffer = nullptr;
            const DWORD flags = FORMAT_MESSAGE_ALLOCATE_BUFFER |
                                FORMAT_MESSAGE_FROM_SYSTEM |
                                FORMAT_MESSAGE_IGNORE_INSERTS;

            DWORD length = FormatMessageW(flags,
                                          nullptr,
                                          errorCode,
                                          0,
                                          reinterpret_cast<LPWSTR>(&buffer),
                                          0,
                                          nullptr);

            std::wstring message;
            if (length != 0 && buffer != nullptr)
            {
                message.assign(buffer, length);
                while (!message.empty() &&
                       (message.back() == L'\r' ||
                        message.back() == L'\n' ||
                        message.back() == L' ' ||
                        message.back() == L'\t'))
                {
                    message.pop_back();
                }
            }
            else
            {
                message = L"Системное описание ошибки недоступно";
            }

            if (buffer != nullptr)
            {
                LocalFree(buffer);
            }

            errors.push_back(action + L": " + message + L" (код " + std::to_wstring(errorCode) + L")");
        }

        bool Success() const
        {
            return errors.empty();
        }
    };

    struct AppState
    {
        HINSTANCE instance;
        HWND mainWindow;
        HWND tab;
        HWND pages[3];
        HWND processList;
        HWND startupList;
        HWND eventLog;
        HBRUSH backgroundBrush;
        HBRUSH accentBrush;
        HFONT font;
        bool ownsFont;
        std::vector<StartupEntry> startupEntries;

        AppState() :
            instance(nullptr),
            mainWindow(nullptr),
            tab(nullptr),
            processList(nullptr),
            startupList(nullptr),
            eventLog(nullptr),
            backgroundBrush(nullptr),
            accentBrush(nullptr),
            font(nullptr),
            ownsFont(false)
        {
            pages[0] = nullptr;
            pages[1] = nullptr;
            pages[2] = nullptr;
        }
    };

    AppState g_app;

    std::wstring RootName(HKEY root)
    {
        if (root == HKEY_LOCAL_MACHINE)
        {
            return L"HKLM";
        }

        if (root == HKEY_CURRENT_USER)
        {
            return L"HKCU";
        }

        if (root == HKEY_CLASSES_ROOT)
        {
            return L"HKCR";
        }

        return L"Реестр";
    }

    std::wstring RegistryViewName(REGSAM view)
    {
        if ((view & KEY_WOW64_64KEY) == KEY_WOW64_64KEY)
        {
            return L"64-бит";
        }

        if ((view & KEY_WOW64_32KEY) == KEY_WOW64_32KEY)
        {
            return L"32-бит";
        }

        return L"основной";
    }

    std::wstring JoinRegistryLocation(HKEY root, const std::wstring& subKey, REGSAM view)
    {
        std::wstring location = RootName(root) + L"\\" + subKey;
        if (view != 0)
        {
            location += L" (" + RegistryViewName(view) + L")";
        }

        return location;
    }

    std::wstring JoinPath(const std::wstring& left, const std::wstring& right)
    {
        if (left.empty())
        {
            return right;
        }

        if (left.back() == L'\\' || left.back() == L'/')
        {
            return left + right;
        }

        return left + L"\\" + right;
    }

    std::wstring FileNameFromPath(const std::wstring& path)
    {
        const size_t position = path.find_last_of(L"\\/");
        if (position == std::wstring::npos)
        {
            return path;
        }

        return path.substr(position + 1);
    }

    bool HasStartupExtension(const wchar_t* fileName)
    {
        const wchar_t* dot = wcsrchr(fileName, L'.');
        if (dot == nullptr)
        {
            return false;
        }

        return lstrcmpiW(dot, L".exe") == 0 ||
               lstrcmpiW(dot, L".bat") == 0 ||
               lstrcmpiW(dot, L".vbs") == 0 ||
               lstrcmpiW(dot, L".lnk") == 0;
    }

    std::wstring CurrentTimestamp()
    {
        SYSTEMTIME time;
        GetLocalTime(&time);

        wchar_t buffer[32];
        swprintf_s(buffer,
                   L"[%02u:%02u:%02u] ",
                   static_cast<unsigned>(time.wHour),
                   static_cast<unsigned>(time.wMinute),
                   static_cast<unsigned>(time.wSecond));
        return buffer;
    }

    void AppendEventLog(const std::wstring& message)
    {
        std::wstring line = CurrentTimestamp();
        line += message;

        if (g_app.eventLog != nullptr)
        {
            const LRESULT index = SendMessageW(g_app.eventLog, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(line.c_str()));
            const LRESULT count = SendMessageW(g_app.eventLog, LB_GETCOUNT, 0, 0);
            if (count > 0)
            {
                SendMessageW(g_app.eventLog, LB_SETTOPINDEX, static_cast<WPARAM>(count - 1), 0);
            }
            if (index != LB_ERR && index != LB_ERRSPACE)
            {
                return;
            }
        }

        OutputDebugStringW(line.c_str());
        OutputDebugStringW(L"\r\n");
    }

    void ShowResult(HWND owner,
                    const wchar_t* successMessage,
                    const wchar_t* failureTitle,
                    const OperationResult& result)
    {
        UNREFERENCED_PARAMETER(owner);

        if (result.Success())
        {
            if (successMessage != nullptr && successMessage[0] != L'\0')
            {
                AppendEventLog(successMessage);
            }
            else
            {
                AppendEventLog(L"Операция выполнена.");
            }
            return;
        }

        const wchar_t* title = failureTitle != nullptr && failureTitle[0] != L'\0'
            ? failureTitle
            : L"Операция завершена не полностью";

        for (size_t index = 0; index < result.errors.size(); ++index)
        {
            AppendEventLog(std::wstring(title) + L": " + result.errors[index]);
        }

        if (result.errors.empty())
        {
            AppendEventLog(title);
        }
    }

    void ShowWin32Error(HWND owner, const wchar_t* action, DWORD errorCode)
    {
        OperationResult result;
        result.Add(action, errorCode);
        ShowResult(owner, L"", L"Ошибка Win32", result);
    }

    void ShowFatalWin32Error(const wchar_t* action, DWORD errorCode)
    {
        OperationResult result;
        result.Add(action, errorCode);

        std::wstring text = result.errors.empty()
            ? std::wstring(L"Критическая ошибка приложения.")
            : result.errors[0];

        MessageBoxW(nullptr, text.c_str(), kApplicationTitle, MB_OK | MB_ICONERROR);
    }

    void ApplyFont(HWND window)
    {
        if (window != nullptr && g_app.font != nullptr)
        {
            SendMessageW(window, WM_SETFONT, reinterpret_cast<WPARAM>(g_app.font), TRUE);
        }
    }

    LRESULT HandleColorMessage(UINT message, WPARAM wParam)
    {
        HDC dc = reinterpret_cast<HDC>(wParam);

        switch (message)
        {
        case WM_CTLCOLORBTN:
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(45, 45, 45));
            return reinterpret_cast<LRESULT>(g_app.accentBrush);
        case WM_CTLCOLORLISTBOX:
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(18, 18, 18));
            return reinterpret_cast<LRESULT>(g_app.backgroundBrush);
        case WM_CTLCOLORDLG:
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(18, 18, 18));
            return reinterpret_cast<LRESULT>(g_app.backgroundBrush);
        case WM_CTLCOLORSTATIC:
            SetTextColor(dc, RGB(255, 255, 255));
            SetBkColor(dc, RGB(18, 18, 18));
            return reinterpret_cast<LRESULT>(g_app.backgroundBrush);
        default:
            return 0;
        }
    }

    void FillWindowBackground(HWND window, HDC dc)
    {
        RECT clientRect;
        GetClientRect(window, &clientRect);
        FillRect(dc, &clientRect, g_app.backgroundBrush);
    }

    HFONT CreateApplicationFont()
    {
        HDC screenDc = GetDC(nullptr);
        int height = -16;
        if (screenDc != nullptr)
        {
            height = -MulDiv(16, GetDeviceCaps(screenDc, LOGPIXELSY), 72);
            ReleaseDC(nullptr, screenDc);
        }

        return CreateFontW(height,
                           0,
                           0,
                           0,
                           FW_NORMAL,
                           FALSE,
                           FALSE,
                           FALSE,
                           DEFAULT_CHARSET,
                           OUT_DEFAULT_PRECIS,
                           CLIP_DEFAULT_PRECIS,
                           DEFAULT_QUALITY,
                           DEFAULT_PITCH | FF_SWISS,
                           L"Segoe UI");
    }

    LSTATUS CreateRegistryKey(HKEY root,
                              const wchar_t* subKey,
                              REGSAM access,
                              REGSAM view,
                              HKEY* key)
    {
        LSTATUS status = RegCreateKeyExW(root,
                                         subKey,
                                         0,
                                         nullptr,
                                         REG_OPTION_NON_VOLATILE,
                                         access | view,
                                         nullptr,
                                         key,
                                         nullptr);

        if (status == ERROR_INVALID_PARAMETER && view != 0)
        {
            const REGSAM nativeAccess = access & ~(KEY_WOW64_64KEY | KEY_WOW64_32KEY);
            status = RegCreateKeyExW(root,
                                     subKey,
                                     0,
                                     nullptr,
                                     REG_OPTION_NON_VOLATILE,
                                     nativeAccess,
                                     nullptr,
                                     key,
                                     nullptr);
        }

        return status;
    }

    LSTATUS OpenRegistryKey(HKEY root,
                            const wchar_t* subKey,
                            REGSAM access,
                            REGSAM view,
                            HKEY* key)
    {
        LSTATUS status = RegOpenKeyExW(root, subKey, 0, access | view, key);
        if (status == ERROR_INVALID_PARAMETER && view != 0)
        {
            const REGSAM nativeAccess = access & ~(KEY_WOW64_64KEY | KEY_WOW64_32KEY);
            status = RegOpenKeyExW(root, subKey, 0, nativeAccess, key);
        }

        return status;
    }

    void SetRegistryString(OperationResult& result,
                           HKEY root,
                           const wchar_t* subKey,
                           const wchar_t* valueName,
                           const wchar_t* data,
                           REGSAM view)
    {
        HKEY key = nullptr;
        LSTATUS status = CreateRegistryKey(root, subKey, KEY_SET_VALUE, view, &key);
        if (status != ERROR_SUCCESS)
        {
            result.Add(L"Открытие ключа " + JoinRegistryLocation(root, subKey, view), status);
            return;
        }

        const DWORD byteCount = static_cast<DWORD>((wcslen(data) + 1) * sizeof(wchar_t));
        status = RegSetValueExW(key,
                                valueName,
                                0,
                                REG_SZ,
                                reinterpret_cast<const BYTE*>(data),
                                byteCount);
        if (status == ERROR_SUCCESS)
        {
            std::wstring name = valueName != nullptr && valueName[0] != L'\0' ? valueName : L"(по умолчанию)";
            AppendEventLog(L"[OK] Записано значение " + name + L" = \"" + data + L"\"");
        }
        else
        {
            std::wstring name = valueName != nullptr && valueName[0] != L'\0' ? valueName : L"(по умолчанию)";
            result.Add(L"Запись значения " + name + L" в " + JoinRegistryLocation(root, subKey, view), status);
        }

        RegCloseKey(key);
    }

    void DeleteRegistryValueIfExists(OperationResult& result,
                                     HKEY root,
                                     const wchar_t* subKey,
                                     const wchar_t* valueName,
                                     REGSAM view)
    {
        HKEY key = nullptr;
        LSTATUS status = OpenRegistryKey(root, subKey, KEY_SET_VALUE, view, &key);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
        {
            return;
        }

        if (status != ERROR_SUCCESS)
        {
            result.Add(L"Открытие ключа " + JoinRegistryLocation(root, subKey, view), status);
            return;
        }

        status = RegDeleteValueW(key, valueName);
        if (status == ERROR_SUCCESS)
        {
            AppendEventLog(L"[OK] Ограничение " + std::wstring(valueName) + L" успешно удалено.");
        }
        else if (status != ERROR_FILE_NOT_FOUND)
        {
            std::wstring name = valueName != nullptr && valueName[0] != L'\0' ? valueName : L"(по умолчанию)";
            result.Add(L"Удаление значения " + name + L" из " + JoinRegistryLocation(root, subKey, view), status);
        }

        RegCloseKey(key);
    }

    void DeleteRegistryValueWithAccessIfExists(OperationResult& result,
                                               HKEY root,
                                               const wchar_t* subKey,
                                               const wchar_t* valueName,
                                               REGSAM access,
                                               REGSAM view)
    {
        HKEY key = nullptr;
        LSTATUS status = OpenRegistryKey(root, subKey, access, view, &key);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
        {
            return;
        }

        if (status != ERROR_SUCCESS)
        {
            result.Add(L"Открытие ключа " + JoinRegistryLocation(root, subKey, view), status);
            return;
        }

        status = RegDeleteValueW(key, valueName);
        if (status == ERROR_SUCCESS)
        {
            AppendEventLog(L"[OK] Параметр " + std::wstring(valueName) + L" успешно удален.");
        }
        else if (status != ERROR_FILE_NOT_FOUND)
        {
            std::wstring name = valueName != nullptr && valueName[0] != L'\0' ? valueName : L"(по умолчанию)";
            result.Add(L"Удаление значения " + name + L" из " + JoinRegistryLocation(root, subKey, view), status);
        }

        RegCloseKey(key);
    }

    void DeleteRegistryKeyIfExists(OperationResult& result,
                                   HKEY root,
                                   const wchar_t* subKey,
                                   REGSAM view)
    {
        HMODULE advapi = GetModuleHandleW(L"advapi32.dll");
        typedef LSTATUS (APIENTRY *PFN_RegDeleteKeyExW)(HKEY, LPCWSTR, REGSAM, DWORD);
        PFN_RegDeleteKeyExW pRegDeleteKeyExW = nullptr;
        if (advapi)
        {
            pRegDeleteKeyExW = reinterpret_cast<PFN_RegDeleteKeyExW>(GetProcAddress(advapi, "RegDeleteKeyExW"));
        }

        LSTATUS status;
        if (pRegDeleteKeyExW)
        {
            status = pRegDeleteKeyExW(root, subKey, view, 0);
        }
        else
        {
            status = RegDeleteKeyW(root, subKey);
        }

        if (status == ERROR_SUCCESS)
        {
            std::wstring keyName = subKey;
            size_t pos = keyName.find_last_of(L'\\');
            std::wstring leaf = (pos != std::wstring::npos) ? keyName.substr(pos + 1) : keyName;
            AppendEventLog(L"[OK] Ветка IFEO " + leaf + L" уничтожена.");
        }
        else if (status != ERROR_FILE_NOT_FOUND && status != ERROR_PATH_NOT_FOUND)
        {
            result.Add(L"Удаление ключа " + JoinRegistryLocation(root, subKey, view), status);
        }
    }

    bool EnableDebugPrivilege(OperationResult& result)
    {
        HANDLE token = nullptr;
        if (!OpenProcessToken(GetCurrentProcess(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, &token))
        {
            const DWORD error = GetLastError();
            result.Add(L"Открытие токена процесса", error);
            return false;
        }

        LUID luid;
        if (!LookupPrivilegeValueW(nullptr, SE_DEBUG_NAME, &luid))
        {
            const DWORD error = GetLastError();
            result.Add(L"Поиск привилегии SeDebugPrivilege", error);
            CloseHandle(token);
            return false;
        }

        TOKEN_PRIVILEGES privileges;
        ZeroMemory(&privileges, sizeof(privileges));
        privileges.PrivilegeCount = 1;
        privileges.Privileges[0].Luid = luid;
        privileges.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

        SetLastError(ERROR_SUCCESS);
        if (!AdjustTokenPrivileges(token, FALSE, &privileges, sizeof(privileges), nullptr, nullptr))
        {
            const DWORD error = GetLastError();
            result.Add(L"Включение SeDebugPrivilege", error);
            CloseHandle(token);
            return false;
        }

        const DWORD adjustError = GetLastError();
        CloseHandle(token);

        if (adjustError == ERROR_NOT_ALL_ASSIGNED)
        {
            result.Add(L"Включение SeDebugPrivilege", adjustError);
            return false;
        }

        return true;
    }

    bool Is64BitOperatingSystem()
    {
#if defined(_WIN64)
        return true;
#else
        BOOL wow64 = FALSE;
        if (IsWow64Process(GetCurrentProcess(), &wow64))
        {
            return wow64 != FALSE;
        }

        return false;
#endif
    }

    LRESULT CALLBACK TabSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_ERASEBKGND)
        {
            HDC hdc = reinterpret_cast<HDC>(wParam);
            RECT rect;
            GetClientRect(hwnd, &rect);
            FillRect(hdc, &rect, g_app.backgroundBrush);
            return 1;
        }
        return CallWindowProcW(g_oldTabProc, hwnd, message, wParam, lParam);
    }

    LRESULT CALLBACK GroupBoxSubclassProc(HWND hwnd, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (message == WM_PAINT)
        {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);

            RECT rect;
            GetClientRect(hwnd, &rect);

            FillRect(hdc, &rect, g_app.backgroundBrush);

            HPEN borderPen = CreatePen(PS_SOLID, 1, RGB(45, 45, 45));
            HPEN oldPen = static_cast<HPEN>(SelectObject(hdc, borderPen));
            HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(hdc, GetStockObject(NULL_BRUSH)));

            wchar_t text[256] = L"";
            GetWindowTextW(hwnd, text, 256);

            SIZE textSize = {0, 0};
            HFONT oldFont = static_cast<HFONT>(SelectObject(hdc, g_app.font));
            if (wcslen(text) > 0)
            {
                GetTextExtentPoint32W(hdc, text, static_cast<int>(wcslen(text)), &textSize);
            }

            int topOffset = (textSize.cy > 0) ? (textSize.cy / 2) : 8;
            Rectangle(hdc, rect.left, rect.top + topOffset, rect.right, rect.bottom);

            if (wcslen(text) > 0)
            {
                RECT textRect = rect;
                textRect.left += 10;
                textRect.right = textRect.left + textSize.cx + 10;
                textRect.bottom = textRect.top + textSize.cy;

                FillRect(hdc, &textRect, g_app.backgroundBrush);

                SetTextColor(hdc, RGB(255, 255, 255));
                SetBkMode(hdc, TRANSPARENT);
                DrawTextW(hdc, text, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            }

            SelectObject(hdc, oldFont);
            SelectObject(hdc, oldPen);
            SelectObject(hdc, oldBrush);
            DeleteObject(borderPen);

            EndPaint(hwnd, &ps);
            return 0;
        }
        return CallWindowProcW(g_oldGroupBoxProc, hwnd, message, wParam, lParam);
    }

    HWND CreateChildButton(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height)
    {
        HWND button = CreateWindowExW(0,
                                      L"BUTTON",
                                      text,
                                      WS_CHILD | WS_VISIBLE | WS_TABSTOP | BS_OWNERDRAW,
                                      x,
                                      y,
                                      width,
                                      height,
                                      parent,
                                      reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                      g_app.instance,
                                      nullptr);
        ApplyFont(button);
        return button;
    }

    HWND CreateGroupBox(HWND parent, int id, const wchar_t* text, int x, int y, int width, int height)
    {
        HWND groupBox = CreateWindowExW(0,
                                        L"BUTTON",
                                        text,
                                        WS_CHILD | WS_VISIBLE | BS_GROUPBOX,
                                        x,
                                        y,
                                        width,
                                        height,
                                        parent,
                                        reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                        g_app.instance,
                                        nullptr);
        ApplyFont(groupBox);

        WNDPROC oldProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(groupBox, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(GroupBoxSubclassProc)));
        if (g_oldGroupBoxProc == nullptr)
        {
            g_oldGroupBoxProc = oldProc;
        }

        return groupBox;
    }

    HWND CreateEventLogList(HWND parent, int id, int x, int y, int width, int height)
    {
        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE,
                                    WC_LISTBOXW,
                                    L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP | WS_VSCROLL | WS_HSCROLL |
                                    LBS_NOINTEGRALHEIGHT | LBS_DISABLENOSCROLL,
                                    x,
                                    y,
                                    width,
                                    height,
                                    parent,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    g_app.instance,
                                    nullptr);
        ApplyFont(list);
        SendMessageW(list, LB_SETHORIZONTALEXTENT, 2200, 0);
        return list;
    }

    void AddListViewColumn(HWND list, int index, const wchar_t* text, int width)
    {
        LVCOLUMNW column;
        ZeroMemory(&column, sizeof(column));
        column.mask = LVCF_TEXT | LVCF_WIDTH | LVCF_SUBITEM;
        column.pszText = const_cast<LPWSTR>(text);
        column.cx = width;
        column.iSubItem = index;
        ListView_InsertColumn(list, index, &column);
    }

    HWND CreateReportList(HWND parent, int id, int x, int y, int width, int height)
    {
        HWND list = CreateWindowExW(WS_EX_CLIENTEDGE,
                                    WC_LISTVIEWW,
                                    L"",
                                    WS_CHILD | WS_VISIBLE | WS_TABSTOP |
                                    LVS_REPORT | LVS_SINGLESEL | LVS_SHOWSELALWAYS,
                                    x,
                                    y,
                                    width,
                                    height,
                                    parent,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)),
                                    g_app.instance,
                                    nullptr);

        ApplyFont(list);
        ListView_SetExtendedListViewStyle(list, LVS_EX_FULLROWSELECT | LVS_EX_GRIDLINES | LVS_EX_DOUBLEBUFFER);
        ListView_SetBkColor(list, kColorBackground);
        ListView_SetTextBkColor(list, kColorBackground);
        ListView_SetTextColor(list, kColorText);
        return list;
    }

    void RefreshProcessList(HWND owner)
    {
        ListView_DeleteAllItems(g_app.processList);

        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            ShowWin32Error(owner, L"Создание снимка процессов", error);
            return;
        }

        PROCESSENTRY32W entry;
        ZeroMemory(&entry, sizeof(entry));
        entry.dwSize = sizeof(entry);

        if (!Process32FirstW(snapshot, &entry))
        {
            const DWORD error = GetLastError();
            CloseHandle(snapshot);
            ShowWin32Error(owner, L"Чтение первого процесса", error);
            return;
        }

        int row = 0;
        do
        {
            LVITEMW item;
            ZeroMemory(&item, sizeof(item));
            item.mask = LVIF_TEXT | LVIF_PARAM;
            item.iItem = row;
            item.pszText = entry.szExeFile;
            item.lParam = static_cast<LPARAM>(entry.th32ProcessID);

            const int inserted = ListView_InsertItem(g_app.processList, &item);
            if (inserted >= 0)
            {
                std::wstring pid = std::to_wstring(entry.th32ProcessID);
                ListView_SetItemText(g_app.processList, inserted, 1, const_cast<LPWSTR>(pid.c_str()));
            }

            ++row;
        }
        while (Process32NextW(snapshot, &entry));

        const DWORD lastError = GetLastError();
        if (lastError != ERROR_NO_MORE_FILES)
        {
            ShowWin32Error(owner, L"Перечисление процессов", lastError);
        }

        CloseHandle(snapshot);
    }

    bool GetSelectedListParam(HWND list, LPARAM* value)
    {
        const int index = ListView_GetNextItem(list, -1, LVNI_SELECTED);
        if (index < 0)
        {
            return false;
        }

        LVITEMW item;
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_PARAM;
        item.iItem = index;

        if (!ListView_GetItem(list, &item))
        {
            return false;
        }

        *value = item.lParam;
        return true;
    }

    void KillSelectedProcess(HWND owner)
    {
        LPARAM value = 0;
        if (!GetSelectedListParam(g_app.processList, &value))
        {
            AppendEventLog(L"Процесс не выбран.");
            return;
        }

        const DWORD pid = static_cast<DWORD>(value);
        HANDLE process = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
        if (process == nullptr)
        {
            const DWORD error = GetLastError();
            ShowWin32Error(owner, L"Открытие процесса для завершения", error);
            RefreshProcessList(owner);
            return;
        }

        if (!TerminateProcess(process, 0))
        {
            const DWORD error = GetLastError();
            CloseHandle(process);
            ShowWin32Error(owner, L"Завершение процесса", error);
            RefreshProcessList(owner);
            return;
        }

        CloseHandle(process);
        AppendEventLog(L"Процесс завершён: PID " + std::to_wstring(pid) + L".");
        RefreshProcessList(owner);
    }

    void RestoreWinlogon(HWND owner)
    {
        OperationResult result;
        const wchar_t subKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Winlogon";

        HKEY hKey = nullptr;
        LSTATUS status = CreateRegistryKey(HKEY_LOCAL_MACHINE, subKey, KEY_WRITE, KEY_WOW64_64KEY, &hKey);
        if (status == ERROR_SUCCESS)
        {
            AppendEventLog(L"[INFO] Структура раздела Winlogon проверена/воссоздана.");
            RegCloseKey(hKey);
        }
        else
        {
            result.Add(L"Создание ветки Winlogon", status);
        }

        SetRegistryString(result, HKEY_LOCAL_MACHINE, subKey, L"Shell", L"explorer.exe", KEY_WOW64_64KEY);
        SetRegistryString(result, HKEY_LOCAL_MACHINE, subKey, L"Userinit", L"C:\\Windows\\system32\\userinit.exe,", KEY_WOW64_64KEY);

        ShowResult(owner,
                   L"Параметры Shell и Userinit восстановлены.",
                   L"Ошибка восстановления Winlogon",
                   result);
    }

    void RemoveSystemRestrictions(HWND owner)
    {
        OperationResult result;
        const wchar_t* policyKeys[] =
        {
            L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Policies\\System",
            L"SOFTWARE\\Policies\\Microsoft\\Windows\\System"
        };

        const wchar_t* values[] =
        {
            L"DisableTaskMgr",
            L"DisableRegistryTools",
            L"DisableCMD"
        };

        for (size_t keyIndex = 0; keyIndex < sizeof(policyKeys) / sizeof(policyKeys[0]); ++keyIndex)
        {
            // Auto-create parent path if missing
            HKEY hKeyCU = nullptr;
            if (CreateRegistryKey(HKEY_CURRENT_USER, policyKeys[keyIndex], KEY_WRITE, 0, &hKeyCU) == ERROR_SUCCESS)
            {
                RegCloseKey(hKeyCU);
            }
            HKEY hKeyLM = nullptr;
            if (CreateRegistryKey(HKEY_LOCAL_MACHINE, policyKeys[keyIndex], KEY_WRITE, KEY_WOW64_64KEY, &hKeyLM) == ERROR_SUCCESS)
            {
                RegCloseKey(hKeyLM);
            }

            for (size_t valueIndex = 0; valueIndex < sizeof(values) / sizeof(values[0]); ++valueIndex)
            {
                DeleteRegistryValueIfExists(result, HKEY_CURRENT_USER, policyKeys[keyIndex], values[valueIndex], 0);
                DeleteRegistryValueIfExists(result, HKEY_LOCAL_MACHINE, policyKeys[keyIndex], values[valueIndex], KEY_WOW64_64KEY);
            }
        }

        ShowResult(owner,
                   L"Ограничения ОС сняты.",
                   L"Ошибка снятия ограничений",
                   result);
    }

    void ClearIfeoDebuggers(HWND owner)
    {
        OperationResult result;
        const wchar_t baseKey[] = L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion\\Image File Execution Options\\";
        const wchar_t* targets[] =
        {
            L"sethc.exe",
            L"utilman.exe",
            L"taskmgr.exe",
            L"explorer.exe"
        };

        for (size_t index = 0; index < sizeof(targets) / sizeof(targets[0]); ++index)
        {
            std::wstring key = baseKey;
            key += targets[index];
            DeleteRegistryKeyIfExists(result, HKEY_LOCAL_MACHINE, key.c_str(), KEY_WOW64_64KEY);
            if (Is64BitOperatingSystem())
            {
                DeleteRegistryKeyIfExists(result, HKEY_LOCAL_MACHINE, key.c_str(), KEY_WOW64_32KEY);
            }
        }

        ShowResult(owner,
                   L"Параметры Debugger в IFEO очищены.",
                   L"Ошибка очистки IFEO",
                   result);
    }

    void CreateSafeBootEntry(OperationResult& result,
                             const wchar_t* branch,
                             const wchar_t* entryName,
                             const wchar_t* value)
    {
        std::wstring subKey = L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\";
        subKey += branch;
        subKey += L"\\";
        subKey += entryName;

        HKEY key = nullptr;
        LSTATUS status = CreateRegistryKey(HKEY_LOCAL_MACHINE, subKey.c_str(), KEY_SET_VALUE, 0, &key);
        if (status != ERROR_SUCCESS)
        {
            result.Add(L"Создание ключа " + subKey, status);
            return;
        }

        const DWORD byteCount = static_cast<DWORD>((wcslen(value) + 1) * sizeof(wchar_t));
        status = RegSetValueExW(key,
                                nullptr,
                                0,
                                REG_SZ,
                                reinterpret_cast<const BYTE*>(value),
                                byteCount);
        if (status == ERROR_SUCCESS)
        {
            AppendEventLog(L"[OK] SafeBoot (" + std::wstring(branch) + L"): " + std::wstring(entryName) + L" = \"" + value + L"\"");
        }
        else
        {
            result.Add(L"Запись значения SafeBoot для " + subKey, status);
        }

        RegCloseKey(key);
    }

    void RestoreSafeBoot(HWND owner)
    {
        OperationResult result;

        HKEY hKeyMinimal = nullptr;
        LSTATUS statusMin = CreateRegistryKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Minimal", KEY_WRITE, 0, &hKeyMinimal);
        if (statusMin == ERROR_SUCCESS)
        {
            AppendEventLog(L"[INFO] Структура SafeBoot\\Minimal воссоздана.");
            RegCloseKey(hKeyMinimal);
        }
        else
        {
            result.Add(L"Создание SafeBoot\\Minimal", statusMin);
        }

        HKEY hKeyNetwork = nullptr;
        LSTATUS statusNet = CreateRegistryKey(HKEY_LOCAL_MACHINE, L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Network", KEY_WRITE, 0, &hKeyNetwork);
        if (statusNet == ERROR_SUCCESS)
        {
            AppendEventLog(L"[INFO] Структура SafeBoot\\Network воссоздана.");
            RegCloseKey(hKeyNetwork);
        }
        else
        {
            result.Add(L"Создание SafeBoot\\Network", statusNet);
        }

        SetRegistryString(result,
                          HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Minimal",
                          nullptr,
                          L"Safe Mode",
                          0);
        SetRegistryString(result,
                          HKEY_LOCAL_MACHINE,
                          L"SYSTEM\\CurrentControlSet\\Control\\SafeBoot\\Network",
                          nullptr,
                          L"Network",
                          0);

        struct SafeBootItem
        {
            const wchar_t* name;
            const wchar_t* value;
        };

        const SafeBootItem commonItems[] =
        {
            { L"Base", L"Driver Group" },
            { L"Boot Bus Extender", L"Driver Group" },
            { L"Boot file system", L"Driver Group" },
            { L"CryptSvc", L"Service" },
            { L"DcomLaunch", L"Service" },
            { L"File system", L"Driver Group" },
            { L"Filter", L"Driver Group" },
            { L"PCI Configuration", L"Driver Group" },
            { L"PlugPlay", L"Service" },
            { L"Primary disk", L"Driver Group" },
            { L"RpcSs", L"Service" },
            { L"SCSI Class", L"Driver Group" },
            { L"SCSI Miniport", L"Driver Group" },
            { L"System Bus Extender", L"Driver Group" },
            { L"vds", L"Service" },
            { L"WinMgmt", L"Service" },
            { L"{36FC9E60-C465-11CF-8056-444553540000}", L"Driver Group" },
            { L"{4D36E967-E325-11CE-BFC1-08002BE10318}", L"Driver Group" },
            { L"{4D36E96A-E325-11CE-BFC1-08002BE10318}", L"Driver Group" },
            { L"{4D36E96B-E325-11CE-BFC1-08002BE10318}", L"Driver Group" },
            { L"{4D36E96F-E325-11CE-BFC1-08002BE10318}", L"Driver Group" },
            { L"{4D36E97B-E325-11CE-BFC1-08002BE10318}", L"Driver Group" },
            { L"{4D36E97D-E325-11CE-BFC1-08002BE10318}", L"Driver Group" }
        };

        const SafeBootItem networkItems[] =
        {
            { L"AFD", L"Service" },
            { L"Dhcp", L"Service" },
            { L"Dnscache", L"Service" },
            { L"LanmanServer", L"Service" },
            { L"LanmanWorkstation", L"Service" },
            { L"LmHosts", L"Service" },
            { L"NDIS", L"Driver Group" },
            { L"NDIS Wrapper", L"Driver Group" },
            { L"Ndisuio", L"Service" },
            { L"NetBIOS", L"Service" },
            { L"NetBT", L"Service" },
            { L"Netlogon", L"Service" },
            { L"Network", L"Service" },
            { L"NetworkProvider", L"Driver Group" },
            { L"Tcpip", L"Service" },
            { L"TDI", L"Driver Group" }
        };

        for (size_t index = 0; index < sizeof(commonItems) / sizeof(commonItems[0]); ++index)
        {
            CreateSafeBootEntry(result, L"Minimal", commonItems[index].name, commonItems[index].value);
            CreateSafeBootEntry(result, L"Network", commonItems[index].name, commonItems[index].value);
        }

        for (size_t index = 0; index < sizeof(networkItems) / sizeof(networkItems[0]); ++index)
        {
            CreateSafeBootEntry(result, L"Network", networkItems[index].name, networkItems[index].value);
        }

        ShowResult(owner,
                   L"Ключи SafeBoot восстановлены.",
                   L"Ошибка восстановления SafeBoot",
                   result);
    }

    void FixExecutableAssociations(HWND owner)
    {
        OperationResult result;

        HKEY hKeyExe = nullptr;
        if (CreateRegistryKey(HKEY_CLASSES_ROOT, L".exe", KEY_WRITE, 0, &hKeyExe) == ERROR_SUCCESS)
        {
            AppendEventLog(L"[INFO] Раздел реестра .exe воссоздан.");
            RegCloseKey(hKeyExe);
        }

        HKEY hKeyExeCmd = nullptr;
        if (CreateRegistryKey(HKEY_CLASSES_ROOT, L"exefile\\shell\\open\\command", KEY_WRITE, 0, &hKeyExeCmd) == ERROR_SUCCESS)
        {
            AppendEventLog(L"[INFO] Раздел реестра exefile\\shell\\open\\command воссоздан.");
            RegCloseKey(hKeyExeCmd);
        }

        HKEY hKeyLnk = nullptr;
        if (CreateRegistryKey(HKEY_CLASSES_ROOT, L".lnk", KEY_WRITE, 0, &hKeyLnk) == ERROR_SUCCESS)
        {
            AppendEventLog(L"[INFO] Раздел реестра .lnk воссоздан.");
            RegCloseKey(hKeyLnk);
        }

        HKEY hKeyLnkFile = nullptr;
        if (CreateRegistryKey(HKEY_CLASSES_ROOT, L"lnkfile", KEY_WRITE, 0, &hKeyLnkFile) == ERROR_SUCCESS)
        {
            AppendEventLog(L"[INFO] Раздел реестра lnkfile воссоздан.");
            RegCloseKey(hKeyLnkFile);
        }

        SetRegistryString(result, HKEY_CLASSES_ROOT, L".exe", nullptr, L"exefile", 0);
        SetRegistryString(result, HKEY_CLASSES_ROOT, L"exefile\\shell\\open\\command", nullptr, L"\"%1\" %*", 0);
        SetRegistryString(result, HKEY_CLASSES_ROOT, L".lnk", nullptr, L"lnkfile", 0);
        SetRegistryString(result, HKEY_CLASSES_ROOT, L"lnkfile", L"IsShortcut", L"", 0);

        ShowResult(owner,
                   L"Ассоциации .EXE и .LNK восстановлены.",
                   L"Ошибка восстановления ассоциаций",
                   result);
    }

    void RestartExplorer(HWND owner)
    {
        OperationResult result;
        HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
        if (snapshot == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            result.Add(L"Создание снимка процессов", error);
        }
        else
        {
            PROCESSENTRY32W entry;
            ZeroMemory(&entry, sizeof(entry));
            entry.dwSize = sizeof(entry);

            if (Process32FirstW(snapshot, &entry))
            {
                do
                {
                    if (lstrcmpiW(entry.szExeFile, L"explorer.exe") == 0)
                    {
                        HANDLE process = OpenProcess(PROCESS_TERMINATE | SYNCHRONIZE, FALSE, entry.th32ProcessID);
                        if (process == nullptr)
                        {
                            const DWORD error = GetLastError();
                            result.Add(L"Открытие explorer.exe для завершения", error);
                            continue;
                        }

                        if (!TerminateProcess(process, 0))
                        {
                            const DWORD error = GetLastError();
                            result.Add(L"Завершение explorer.exe", error);
                        }
                        else
                        {
                            WaitForSingleObject(process, 1500);
                        }

                        CloseHandle(process);
                    }
                }
                while (Process32NextW(snapshot, &entry));

                const DWORD lastError = GetLastError();
                if (lastError != ERROR_NO_MORE_FILES)
                {
                    result.Add(L"Перечисление процессов explorer.exe", lastError);
                }
            }
            else
            {
                const DWORD error = GetLastError();
                result.Add(L"Чтение списка процессов", error);
            }

            CloseHandle(snapshot);
        }

        wchar_t windowsDirectory[MAX_PATH];
        std::wstring applicationPath;
        const UINT length = GetWindowsDirectoryW(windowsDirectory, MAX_PATH);
        if (length > 0 && length < MAX_PATH)
        {
            applicationPath = JoinPath(windowsDirectory, L"explorer.exe");
        }

        STARTUPINFOW startupInfo;
        PROCESS_INFORMATION processInfo;
        ZeroMemory(&startupInfo, sizeof(startupInfo));
        ZeroMemory(&processInfo, sizeof(processInfo));
        startupInfo.cb = sizeof(startupInfo);

        BOOL created = FALSE;
        if (!applicationPath.empty())
        {
            created = CreateProcessW(applicationPath.c_str(),
                                     nullptr,
                                     nullptr,
                                     nullptr,
                                     FALSE,
                                     0,
                                     nullptr,
                                     nullptr,
                                     &startupInfo,
                                     &processInfo);
        }

        if (!created)
        {
            std::wstring commandLine = L"explorer.exe";
            created = CreateProcessW(nullptr,
                                     &commandLine[0],
                                     nullptr,
                                     nullptr,
                                     FALSE,
                                     0,
                                     nullptr,
                                     nullptr,
                                     &startupInfo,
                                     &processInfo);
        }

        if (!created)
        {
            const DWORD error = GetLastError();
            result.Add(L"Запуск explorer.exe", error);
        }
        else
        {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
        }

        ShowResult(owner,
                   L"Проводник перезапущен.",
                   L"Ошибка перезапуска Проводника",
                   result);
    }

    void LaunchUtility(HWND owner,
                       const wchar_t* command,
                       const wchar_t* successMessage,
                       const wchar_t* errorTitle)
    {
        OperationResult result;
        std::wstring commandLine = command;

        STARTUPINFOW startupInfo;
        PROCESS_INFORMATION processInfo;
        ZeroMemory(&startupInfo, sizeof(startupInfo));
        ZeroMemory(&processInfo, sizeof(processInfo));
        startupInfo.cb = sizeof(startupInfo);

        if (!CreateProcessW(nullptr,
                            &commandLine[0],
                            nullptr,
                            nullptr,
                            FALSE,
                            0,
                            nullptr,
                            nullptr,
                            &startupInfo,
                            &processInfo))
        {
            const DWORD error = GetLastError();
            result.Add(std::wstring(L"Запуск утилиты ") + command, error);
        }
        else
        {
            CloseHandle(processInfo.hThread);
            CloseHandle(processInfo.hProcess);
        }

        ShowResult(owner, successMessage, errorTitle, result);
    }

    void RunLanguageRescue(HWND owner)
    {
        AppendEventLog(L"[INFO] Запуск спасательной службы ввода...");

        // 1. Force load and activate EN keyboard layout
        HKL hkl = LoadKeyboardLayoutW(L"00000409", KLF_ACTIVATE | KLF_SETFORPROCESS);
        if (hkl != nullptr)
        {
            AppendEventLog(L"[OK] Раскладка EN (00000409) загружена в процесс.");
            // Apply to foreground window and broadcast
            HWND fg = GetForegroundWindow();
            if (fg != nullptr)
            {
                PostMessageW(fg, WM_INPUTLANGCHANGEREQUEST, INPUTLANGCHANGE_SYSCHARSET, reinterpret_cast<LPARAM>(hkl));
                PostMessageW(HWND_BROADCAST, WM_INPUTLANGCHANGEREQUEST, INPUTLANGCHANGE_SYSCHARSET, reinterpret_cast<LPARAM>(hkl));
                AppendEventLog(L"[OK] Запрос WM_INPUTLANGCHANGEREQUEST отправлен активным окнам.");
            }
        }
        else
        {
            AppendEventLog(L"[ERROR] Ошибка вызова LoadKeyboardLayoutW: " + std::to_wstring(GetLastError()));
        }

        // 2. Launch osk.exe (On-Screen Keyboard) from System32 with WOW64 redirection disabled
        wchar_t sysDir[MAX_PATH];
        if (GetSystemDirectoryW(sysDir, MAX_PATH) != 0)
        {
            std::wstring oskPath = std::wstring(sysDir) + L"\\osk.exe";
            STARTUPINFOW si;
            PROCESS_INFORMATION pi;
            ZeroMemory(&si, sizeof(si));
            si.cb = sizeof(si);
            ZeroMemory(&pi, sizeof(pi));

            PVOID oldRedirectState = nullptr;
            HMODULE kernel32 = GetModuleHandleW(L"kernel32.dll");
            typedef BOOL (WINAPI *PFN_Wow64DisableWow64FsRedirection)(PVOID*);
            typedef BOOL (WINAPI *PFN_Wow64RevertWow64FsRedirection)(PVOID);
            
            PFN_Wow64DisableWow64FsRedirection pDisable = nullptr;
            PFN_Wow64RevertWow64FsRedirection pRevert = nullptr;
            if (kernel32)
            {
                pDisable = reinterpret_cast<PFN_Wow64DisableWow64FsRedirection>(GetProcAddress(kernel32, "Wow64DisableWow64FsRedirection"));
                pRevert = reinterpret_cast<PFN_Wow64RevertWow64FsRedirection>(GetProcAddress(kernel32, "Wow64RevertWow64FsRedirection"));
            }

            if (pDisable)
            {
                pDisable(&oldRedirectState);
            }

            BOOL success = CreateProcessW(oskPath.c_str(),
                                         nullptr,
                                         nullptr,
                                         nullptr,
                                         FALSE,
                                         0,
                                         nullptr,
                                         nullptr,
                                         &si,
                                         &pi);

            if (pRevert && oldRedirectState)
            {
                pRevert(oldRedirectState);
            }

            if (success)
            {
                AppendEventLog(L"[OK] Экранная клавиатура (osk.exe) успешно запущена.");
                CloseHandle(pi.hProcess);
                CloseHandle(pi.hThread);
            }
            else
            {
                DWORD err = GetLastError();
                AppendEventLog(L"[ERROR] Не удалось запустить osk.exe (код ошибки: " + std::to_wstring(err) + L").");
            }
        }
        else
        {
            AppendEventLog(L"[ERROR] Не удалось получить путь к System32.");
        }
    }

    void AddStartupEntryToList(const StartupEntry& entry, size_t index)
    {
        LVITEMW item;
        ZeroMemory(&item, sizeof(item));
        item.mask = LVIF_TEXT | LVIF_PARAM;
        item.iItem = static_cast<int>(index);
        item.pszText = const_cast<LPWSTR>(entry.name.c_str());
        item.lParam = static_cast<LPARAM>(index);

        const int row = ListView_InsertItem(g_app.startupList, &item);
        if (row >= 0)
        {
            ListView_SetItemText(g_app.startupList, row, 1, const_cast<LPWSTR>(entry.command.c_str()));
            ListView_SetItemText(g_app.startupList, row, 2, const_cast<LPWSTR>(entry.location.c_str()));
        }
    }

    void AppendStartupEntry(const StartupEntry& entry)
    {
        const size_t index = g_app.startupEntries.size();
        g_app.startupEntries.push_back(entry);
        AddStartupEntryToList(g_app.startupEntries.back(), index);
    }

    void EnumerateRegistryStartupKey(OperationResult& result,
                                     HKEY root,
                                     const wchar_t* subKey,
                                     REGSAM view)
    {
        HKEY key = nullptr;
        LSTATUS status = OpenRegistryKey(root, subKey, KEY_QUERY_VALUE, view, &key);
        if (status == ERROR_FILE_NOT_FOUND || status == ERROR_PATH_NOT_FOUND)
        {
            return;
        }

        if (status != ERROR_SUCCESS)
        {
            result.Add(L"Открытие автозагрузки " + JoinRegistryLocation(root, subKey, view), status);
            return;
        }

        DWORD valueCount = 0;
        DWORD maxValueName = 0;
        DWORD maxValueData = 0;
        status = RegQueryInfoKeyW(key,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  nullptr,
                                  &valueCount,
                                  &maxValueName,
                                  &maxValueData,
                                  nullptr,
                                  nullptr);

        if (status != ERROR_SUCCESS)
        {
            result.Add(L"Чтение сведений ключа автозагрузки " + JoinRegistryLocation(root, subKey, view), status);
            RegCloseKey(key);
            return;
        }

        std::vector<wchar_t> valueName(maxValueName + 2);
        std::vector<BYTE> valueData(maxValueData + sizeof(wchar_t) * 2);

        for (DWORD index = 0; index < valueCount; ++index)
        {
            DWORD valueNameLength = static_cast<DWORD>(valueName.size());
            DWORD valueDataLength = static_cast<DWORD>(valueData.size());
            DWORD type = 0;

            ZeroMemory(valueName.data(), valueName.size() * sizeof(wchar_t));
            ZeroMemory(valueData.data(), valueData.size());

            status = RegEnumValueW(key,
                                   index,
                                   valueName.data(),
                                   &valueNameLength,
                                   nullptr,
                                   &type,
                                   valueData.data(),
                                   &valueDataLength);

            if (status != ERROR_SUCCESS)
            {
                result.Add(L"Перечисление значения автозагрузки " + JoinRegistryLocation(root, subKey, view), status);
                continue;
            }

            if (type != REG_SZ && type != REG_EXPAND_SZ)
            {
                continue;
            }

            StartupEntry entry;
            entry.kind = StartupEntryKind::Registry;
            entry.root = root;
            entry.registryView = view;
            entry.valueName.assign(valueName.data(), valueNameLength);
            entry.subKey = subKey;
            entry.filePath.clear();
            entry.name = entry.valueName.empty() ? L"(значение по умолчанию)" : entry.valueName;
            entry.command = reinterpret_cast<const wchar_t*>(valueData.data());
            entry.location = JoinRegistryLocation(root, subKey, view);
            AppendStartupEntry(entry);
        }

        RegCloseKey(key);
    }

    void EnumerateStartupFolder(OperationResult& result, int folderId, const wchar_t* locationName)
    {
        wchar_t folderPath[MAX_PATH];
        HRESULT hr = SHGetFolderPathW(nullptr, folderId, nullptr, SHGFP_TYPE_CURRENT, folderPath);
        if (FAILED(hr))
        {
            return;
        }

        std::wstring searchMask = JoinPath(folderPath, L"*");
        WIN32_FIND_DATAW findData;
        ZeroMemory(&findData, sizeof(findData));

        HANDLE findHandle = FindFirstFileW(searchMask.c_str(), &findData);
        if (findHandle == INVALID_HANDLE_VALUE)
        {
            const DWORD error = GetLastError();
            if (error != ERROR_FILE_NOT_FOUND && error != ERROR_PATH_NOT_FOUND)
            {
                result.Add(std::wstring(L"Поиск файлов в ") + locationName, error);
            }
            return;
        }

        do
        {
            if ((findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0)
            {
                continue;
            }

            if (!HasStartupExtension(findData.cFileName))
            {
                continue;
            }

            StartupEntry entry;
            entry.kind = StartupEntryKind::File;
            entry.root = nullptr;
            entry.registryView = 0;
            entry.valueName.clear();
            entry.subKey.clear();
            entry.filePath = JoinPath(folderPath, findData.cFileName);
            entry.name = findData.cFileName;
            entry.command = entry.filePath;
            entry.location = locationName;
            AppendStartupEntry(entry);
        }
        while (FindNextFileW(findHandle, &findData));

        const DWORD lastError = GetLastError();
        if (lastError != ERROR_NO_MORE_FILES)
        {
            result.Add(std::wstring(L"Перечисление файлов в ") + locationName, lastError);
        }

        FindClose(findHandle);
    }

    void RefreshStartupList(HWND owner)
    {
        ListView_DeleteAllItems(g_app.startupList);
        g_app.startupEntries.clear();

        OperationResult result;
        const wchar_t runKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\Run";
        const wchar_t runOnceKey[] = L"SOFTWARE\\Microsoft\\Windows\\CurrentVersion\\RunOnce";

        EnumerateRegistryStartupKey(result, HKEY_CURRENT_USER, runKey, 0);
        EnumerateRegistryStartupKey(result, HKEY_CURRENT_USER, runOnceKey, 0);

        if (Is64BitOperatingSystem())
        {
            EnumerateRegistryStartupKey(result, HKEY_LOCAL_MACHINE, runKey, KEY_WOW64_64KEY);
            EnumerateRegistryStartupKey(result, HKEY_LOCAL_MACHINE, runOnceKey, KEY_WOW64_64KEY);
            EnumerateRegistryStartupKey(result, HKEY_LOCAL_MACHINE, runKey, KEY_WOW64_32KEY);
            EnumerateRegistryStartupKey(result, HKEY_LOCAL_MACHINE, runOnceKey, KEY_WOW64_32KEY);
        }
        else
        {
            EnumerateRegistryStartupKey(result, HKEY_LOCAL_MACHINE, runKey, 0);
            EnumerateRegistryStartupKey(result, HKEY_LOCAL_MACHINE, runOnceKey, 0);
        }

        EnumerateStartupFolder(result, CSIDL_STARTUP, L"Папка автозагрузки пользователя");
        EnumerateStartupFolder(result, CSIDL_COMMON_STARTUP, L"Общая папка автозагрузки");

        if (!result.Success())
        {
            ShowResult(owner,
                       L"",
                       L"Ошибка чтения автозагрузки",
                       result);
        }
    }

    void DeleteSelectedStartupEntry(HWND owner)
    {
        LPARAM value = 0;
        if (!GetSelectedListParam(g_app.startupList, &value))
        {
            AppendEventLog(L"Элемент автозагрузки не выбран.");
            return;
        }

        const size_t index = static_cast<size_t>(value);
        if (index >= g_app.startupEntries.size())
        {
            AppendEventLog(L"Выбранный элемент больше недоступен. Список обновлён.");
            RefreshStartupList(owner);
            return;
        }

        const StartupEntry entry = g_app.startupEntries[index];
        OperationResult result;
        if (entry.kind == StartupEntryKind::Registry)
        {
            HKEY key = nullptr;
            LSTATUS status = OpenRegistryKey(entry.root,
                                             entry.subKey.c_str(),
                                             KEY_SET_VALUE,
                                             entry.registryView,
                                             &key);
            if (status != ERROR_SUCCESS)
            {
                result.Add(L"Открытие ключа автозагрузки " + entry.location, status);
            }
            else
            {
                const wchar_t* valueName = entry.valueName.empty() ? nullptr : entry.valueName.c_str();
                status = RegDeleteValueW(key, valueName);
                if (status != ERROR_SUCCESS)
                {
                    result.Add(L"Удаление значения автозагрузки " + entry.name, status);
                }

                RegCloseKey(key);
            }
        }
        else
        {
            if (!DeleteFileW(entry.filePath.c_str()))
            {
                const DWORD error = GetLastError();
                result.Add(L"Удаление файла автозагрузки " + entry.filePath, error);
            }
        }

        ShowResult(owner,
                   L"Элемент автозагрузки удалён.",
                   L"Ошибка удаления автозагрузки",
                   result);
        RefreshStartupList(owner);
    }

    void ShowPage(int index)
    {
        for (int page = 0; page < 3; ++page)
        {
            if (page == index)
            {
                ShowWindow(g_app.pages[page], SW_SHOW);
                SetWindowPos(g_app.pages[page], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
            }
            else
            {
                ShowWindow(g_app.pages[page], SW_HIDE);
            }
        }

        if (index == 1)
        {
            RefreshProcessList(g_app.mainWindow);
        }
        else if (index == 2)
        {
            RefreshStartupList(g_app.mainWindow);
        }
    }

    void CreateRestorePage(HWND parent)
    {
        CreateGroupBox(parent, IDC_GRP_CRITICAL, L"Критические параметры", 24, 20, 405, 126);
        CreateChildButton(parent, IDC_BTN_RESTORE_WINLOGON, L"Восстановить Shell и Userinit", 44, 54, 365, 34);
        CreateChildButton(parent, IDC_BTN_RESTORE_SAFEBOOT, L"Восстановить SafeBoot (Безопасный режим)", 44, 96, 365, 34);

        CreateGroupBox(parent, IDC_GRP_SECURITY, L"Безопасность", 455, 20, 405, 126);
        CreateChildButton(parent, IDC_BTN_REMOVE_RESTRICTIONS, L"Снять все ограничения ОС", 475, 54, 365, 34);
        CreateChildButton(parent, IDC_BTN_CLEAR_IFEO, L"Очистить IFEO (Уязвимости)", 475, 96, 365, 34);

        CreateGroupBox(parent, IDC_GRP_ENVIRONMENT, L"Среда ОС", 24, 162, 405, 126);
        CreateChildButton(parent, IDC_BTN_FIX_ASSOCIATIONS, L"Исправить ассоциации (.EXE, .LNK)", 44, 196, 365, 34);
        CreateChildButton(parent, IDC_BTN_RESTART_EXPLORER, L"Перезапустить Проводник", 44, 238, 365, 34);

        CreateGroupBox(parent, IDC_GRP_UTILITIES, L"Утилиты", 455, 162, 405, 126);
        CreateChildButton(parent, IDC_BTN_LAUNCH_CMD, L"Запустить CMD", 475, 184, 365, 26);
        CreateChildButton(parent, IDC_BTN_LAUNCH_REGEDIT, L"Запустить Regedit", 475, 216, 365, 26);
        CreateChildButton(parent, IDC_BTN_LANG_RESCUE, L"Экранная клавиатура / EN Раскладка", 475, 248, 365, 26);

        g_app.eventLog = CreateEventLogList(parent, IDC_EVENT_LOG, 24, 312, 836, 194);
    }

    void CreateProcessPage(HWND parent)
    {
        g_app.processList = CreateReportList(parent, IDC_PROCESS_LIST, 24, 24, 820, 410);
        AddListViewColumn(g_app.processList, 0, L"Имя процесса", 600);
        AddListViewColumn(g_app.processList, 1, L"PID", 160);
        CreateChildButton(parent, IDC_BTN_KILL_PROCESS, L"Завершить процесс", 24, 454, 240, 42);
    }

    void CreateStartupPage(HWND parent)
    {
        g_app.startupList = CreateReportList(parent, IDC_STARTUP_LIST, 24, 24, 820, 410);
        AddListViewColumn(g_app.startupList, 0, L"Имя", 210);
        AddListViewColumn(g_app.startupList, 1, L"Путь/Команда", 390);
        AddListViewColumn(g_app.startupList, 2, L"Расположение", 210);
        CreateChildButton(parent, IDC_BTN_DELETE_STARTUP, L"Удалить автозагрузку", 24, 454, 240, 42);
    }

    void CreateApplicationControls(HWND window)
    {
        g_app.tab = CreateWindowExW(0,
                                    WC_TABCONTROLW,
                                    L"",
                                    WS_CHILD | WS_VISIBLE | WS_CLIPSIBLINGS | WS_TABSTOP | TCS_OWNERDRAWFIXED,
                                    14,
                                    14,
                                    kClientWidth - 28,
                                    kClientHeight - 28,
                                    window,
                                    reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_MAIN_TAB)),
                                    g_app.instance,
                                    nullptr);
        ApplyFont(g_app.tab);

        g_oldTabProc = reinterpret_cast<WNDPROC>(SetWindowLongPtrW(g_app.tab, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(TabSubclassProc)));

        TCITEMW tabItem;
        ZeroMemory(&tabItem, sizeof(tabItem));
        tabItem.mask = TCIF_TEXT;

        tabItem.pszText = const_cast<LPWSTR>(L"Реставрация Системы");
        TabCtrl_InsertItem(g_app.tab, 0, &tabItem);

        tabItem.pszText = const_cast<LPWSTR>(L"Диспетчер Процессов");
        TabCtrl_InsertItem(g_app.tab, 1, &tabItem);

        tabItem.pszText = const_cast<LPWSTR>(L"Менеджер Автозагрузки");
        TabCtrl_InsertItem(g_app.tab, 2, &tabItem);

        RECT tabRect;
        GetWindowRect(g_app.tab, &tabRect);
        MapWindowPoints(nullptr, window, reinterpret_cast<POINT*>(&tabRect), 2);
        TabCtrl_AdjustRect(g_app.tab, FALSE, &tabRect);

        for (int index = 0; index < 3; ++index)
        {
            g_app.pages[index] = CreateWindowExW(WS_EX_CONTROLPARENT,
                                                 kPageWindowClass,
                                                 L"",
                                                 WS_CHILD | WS_VISIBLE | WS_CLIPCHILDREN | WS_CLIPSIBLINGS,
                                                 tabRect.left,
                                                 tabRect.top,
                                                 tabRect.right - tabRect.left,
                                                 tabRect.bottom - tabRect.top,
                                                 window,
                                                 reinterpret_cast<HMENU>(static_cast<INT_PTR>(IDC_PAGE_RESTORE + index)),
                                                 g_app.instance,
                                                 nullptr);
            ApplyFont(g_app.pages[index]);
            SetWindowPos(g_app.pages[index], HWND_TOP, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
        }

        CreateRestorePage(g_app.pages[0]);
        CreateProcessPage(g_app.pages[1]);
        CreateStartupPage(g_app.pages[2]);
        ShowPage(0);
    }

    void HandleCommand(HWND window, WPARAM wParam)
    {
        switch (LOWORD(wParam))
        {
        case IDC_BTN_RESTORE_WINLOGON:
            RestoreWinlogon(window);
            break;
        case IDC_BTN_REMOVE_RESTRICTIONS:
            RemoveSystemRestrictions(window);
            break;
        case IDC_BTN_CLEAR_IFEO:
            ClearIfeoDebuggers(window);
            break;
        case IDC_BTN_RESTORE_SAFEBOOT:
            RestoreSafeBoot(window);
            break;
        case IDC_BTN_FIX_ASSOCIATIONS:
            FixExecutableAssociations(window);
            break;
        case IDC_BTN_RESTART_EXPLORER:
            RestartExplorer(window);
            break;
        case IDC_BTN_LAUNCH_CMD:
            LaunchUtility(window, L"cmd.exe", L"Командная строка запущена.", L"Ошибка запуска CMD");
            break;
        case IDC_BTN_LAUNCH_REGEDIT:
            LaunchUtility(window, L"regedit.exe", L"Редактор реестра запущен.", L"Ошибка запуска Regedit");
            break;
        case IDC_BTN_LANG_RESCUE:
            RunLanguageRescue(window);
            break;
        case IDC_BTN_KILL_PROCESS:
            KillSelectedProcess(window);
            break;
        case IDC_BTN_DELETE_STARTUP:
            DeleteSelectedStartupEntry(window);
            break;
        default:
            break;
        }
    }

    LRESULT CALLBACK PageWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_COMMAND:
        case WM_NOTIFY:
            return SendMessageW(GetParent(window), message, wParam, lParam);
        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX:
            return HandleColorMessage(message, wParam);
        case WM_ERASEBKGND:
            FillWindowBackground(window, reinterpret_cast<HDC>(wParam));
            return 1;
        case WM_DRAWITEM:
        {
            LPDRAWITEMSTRUCT lpDrawItem = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            if (lpDrawItem->CtlType == ODT_BUTTON)
            {
                HBRUSH brush = g_app.accentBrush;
                bool deleteBrush = false;

                if (lpDrawItem->itemState & ODS_SELECTED)
                {
                    brush = CreateSolidBrush(RGB(30, 30, 30));
                    deleteBrush = true;
                }

                FillRect(lpDrawItem->hDC, &lpDrawItem->rcItem, brush);

                HPEN borderPen = nullptr;
                if (lpDrawItem->itemState & ODS_FOCUS)
                {
                    borderPen = CreatePen(PS_SOLID, 1, RGB(255, 255, 255));
                }
                else
                {
                    borderPen = CreatePen(PS_SOLID, 1, RGB(70, 70, 70));
                }

                HPEN oldPen = static_cast<HPEN>(SelectObject(lpDrawItem->hDC, borderPen));
                HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(lpDrawItem->hDC, GetStockObject(NULL_BRUSH)));
                Rectangle(lpDrawItem->hDC, lpDrawItem->rcItem.left, lpDrawItem->rcItem.top, lpDrawItem->rcItem.right, lpDrawItem->rcItem.bottom);
                SelectObject(lpDrawItem->hDC, oldPen);
                SelectObject(lpDrawItem->hDC, oldBrush);
                DeleteObject(borderPen);

                if (deleteBrush)
                {
                    DeleteObject(brush);
                }

                wchar_t text[256];
                GetWindowTextW(lpDrawItem->hwndItem, text, 256);

                SetTextColor(lpDrawItem->hDC, RGB(255, 255, 255));
                SetBkMode(lpDrawItem->hDC, TRANSPARENT);

                HFONT oldFont = static_cast<HFONT>(SelectObject(lpDrawItem->hDC, g_app.font));
                DrawTextW(lpDrawItem->hDC, text, -1, &lpDrawItem->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(lpDrawItem->hDC, oldFont);

                return TRUE;
            }
            break;
        }
        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    LRESULT CALLBACK MainWindowProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        switch (message)
        {
        case WM_CREATE:
            g_app.mainWindow = window;
            SetWindowLongPtrW(window, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(&g_app));
            CreateApplicationControls(window);
            return 0;

        case WM_COMMAND:
            HandleCommand(window, wParam);
            return 0;

        case WM_NOTIFY:
        {
            NMHDR* header = reinterpret_cast<NMHDR*>(lParam);
            if (header != nullptr)
            {
                if (header->hwndFrom == g_app.tab && header->code == TCN_SELCHANGE)
                {
                    const int selected = TabCtrl_GetCurSel(g_app.tab);
                    if (selected >= 0 && selected < 3)
                    {
                        ShowPage(selected);
                    }
                }
                else if (header->code == NM_CUSTOMDRAW)
                {
                    LPNMCUSTOMDRAW lpnmcd = reinterpret_cast<LPNMCUSTOMDRAW>(lParam);
                    wchar_t className[64] = L"";
                    GetClassNameW(lpnmcd->hdr.hwndFrom, className, 64);
                    if (wcscmp(className, L"SysHeader32") == 0)
                    {
                        switch (lpnmcd->dwDrawStage)
                        {
                        case CDDS_PREPAINT:
                        {
                            RECT rect;
                            GetClientRect(lpnmcd->hdr.hwndFrom, &rect);
                            FillRect(lpnmcd->hdc, &rect, g_app.accentBrush);
                            return CDRF_NOTIFYITEMDRAW;
                        }
                        case CDDS_ITEMPREPAINT:
                        {
                            FillRect(lpnmcd->hdc, &lpnmcd->rc, g_app.accentBrush);

                            HWND listHwnd = GetParent(lpnmcd->hdr.hwndFrom);
                            LVCOLUMNW col;
                            ZeroMemory(&col, sizeof(col));
                            col.mask = LVCF_TEXT;
                            wchar_t buf[256] = L"";
                            col.pszText = buf;
                            col.cchTextMax = 256;
                            ListView_GetColumn(listHwnd, lpnmcd->dwItemSpec, &col);

                            SetTextColor(lpnmcd->hdc, RGB(255, 255, 255));
                            SetBkMode(lpnmcd->hdc, TRANSPARENT);
                            HFONT oldFont = static_cast<HFONT>(SelectObject(lpnmcd->hdc, g_app.font));

                            RECT textRect = lpnmcd->rc;
                            textRect.left += 6;
                            DrawTextW(lpnmcd->hdc, buf, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                            SelectObject(lpnmcd->hdc, oldFont);

                            HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
                            HPEN oldPen = static_cast<HPEN>(SelectObject(lpnmcd->hdc, pen));
                            MoveToEx(lpnmcd->hdc, lpnmcd->rc.right - 1, lpnmcd->rc.top, nullptr);
                            LineTo(lpnmcd->hdc, lpnmcd->rc.right - 1, lpnmcd->rc.bottom);
                            SelectObject(lpnmcd->hdc, oldPen);
                            DeleteObject(pen);

                            return CDRF_SKIPDEFAULT;
                        }
                        }
                    }
                }
            }
            return 0;
        }

        case WM_DRAWITEM:
        {
            LPDRAWITEMSTRUCT lpDrawItem = reinterpret_cast<LPDRAWITEMSTRUCT>(lParam);
            if (lpDrawItem->CtlType == ODT_TAB)
            {
                bool selected = (TabCtrl_GetCurSel(g_app.tab) == static_cast<int>(lpDrawItem->itemID));
                HBRUSH brush = selected ? g_app.accentBrush : CreateSolidBrush(RGB(28, 28, 28));
                FillRect(lpDrawItem->hDC, &lpDrawItem->rcItem, brush);
                if (!selected)
                {
                    DeleteObject(brush);
                }

                HPEN pen = CreatePen(PS_SOLID, 1, RGB(60, 60, 60));
                HPEN oldPen = static_cast<HPEN>(SelectObject(lpDrawItem->hDC, pen));
                HBRUSH oldBrush = static_cast<HBRUSH>(SelectObject(lpDrawItem->hDC, GetStockObject(NULL_BRUSH)));
                Rectangle(lpDrawItem->hDC, lpDrawItem->rcItem.left, lpDrawItem->rcItem.top, lpDrawItem->rcItem.right, lpDrawItem->rcItem.bottom);
                SelectObject(lpDrawItem->hDC, oldPen);
                SelectObject(lpDrawItem->hDC, oldBrush);
                DeleteObject(pen);

                TCITEMW tcItem;
                ZeroMemory(&tcItem, sizeof(tcItem));
                tcItem.mask = TCIF_TEXT;
                wchar_t text[128] = L"";
                tcItem.pszText = text;
                tcItem.cchTextMax = 128;
                TabCtrl_GetItem(g_app.tab, lpDrawItem->itemID, &tcItem);

                SetTextColor(lpDrawItem->hDC, RGB(255, 255, 255));
                SetBkMode(lpDrawItem->hDC, TRANSPARENT);
                HFONT oldFont = static_cast<HFONT>(SelectObject(lpDrawItem->hDC, g_app.font));
                DrawTextW(lpDrawItem->hDC, text, -1, &lpDrawItem->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                SelectObject(lpDrawItem->hDC, oldFont);

                return TRUE;
            }
            break;
        }

        case WM_CTLCOLORDLG:
        case WM_CTLCOLORSTATIC:
        case WM_CTLCOLORBTN:
        case WM_CTLCOLORLISTBOX:
            return HandleColorMessage(message, wParam);

        case WM_ERASEBKGND:
            FillWindowBackground(window, reinterpret_cast<HDC>(wParam));
            return 1;

        case WM_CLOSE:
            DestroyWindow(window);
            return 0;

        case WM_DESTROY:
            g_app.startupEntries.clear();
            if (g_app.backgroundBrush != nullptr)
            {
                DeleteObject(g_app.backgroundBrush);
                g_app.backgroundBrush = nullptr;
            }
            if (g_app.accentBrush != nullptr)
            {
                DeleteObject(g_app.accentBrush);
                g_app.accentBrush = nullptr;
            }
            if (g_app.font != nullptr && g_app.ownsFont)
            {
                DeleteObject(g_app.font);
                g_app.font = nullptr;
                g_app.ownsFont = false;
            }
            PostQuitMessage(0);
            return 0;

        default:
            return DefWindowProcW(window, message, wParam, lParam);
        }
    }

    bool RegisterApplicationClasses(HINSTANCE instance)
    {
        WNDCLASSEXW mainClass;
        ZeroMemory(&mainClass, sizeof(mainClass));
        mainClass.cbSize = sizeof(mainClass);
        mainClass.style = CS_HREDRAW | CS_VREDRAW;
        mainClass.lpfnWndProc = MainWindowProc;
        mainClass.hInstance = instance;
        mainClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        mainClass.hbrBackground = g_app.backgroundBrush;
        mainClass.lpszClassName = kMainWindowClass;
        mainClass.hIcon = LoadIconW(nullptr, IDI_APPLICATION);
        mainClass.hIconSm = LoadIconW(nullptr, IDI_APPLICATION);

        if (RegisterClassExW(&mainClass) == 0)
        {
            return false;
        }

        WNDCLASSEXW pageClass;
        ZeroMemory(&pageClass, sizeof(pageClass));
        pageClass.cbSize = sizeof(pageClass);
        pageClass.style = CS_HREDRAW | CS_VREDRAW;
        pageClass.lpfnWndProc = PageWindowProc;
        pageClass.hInstance = instance;
        pageClass.hCursor = LoadCursorW(nullptr, IDC_ARROW);
        pageClass.hbrBackground = g_app.backgroundBrush;
        pageClass.lpszClassName = kPageWindowClass;

        return RegisterClassExW(&pageClass) != 0;
    }
}

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR, int commandShow)
{
    OperationResult privilegeResult;
    EnableDebugPrivilege(privilegeResult);

    g_app.instance = instance;
    g_app.backgroundBrush = CreateSolidBrush(kColorBackground);
    g_app.accentBrush = CreateSolidBrush(kColorAccent);
    g_app.font = CreateApplicationFont();
    g_app.ownsFont = g_app.font != nullptr;
    if (g_app.font == nullptr)
    {
        g_app.font = reinterpret_cast<HFONT>(GetStockObject(SYSTEM_FONT));
    }

    INITCOMMONCONTROLSEX controls;
    ZeroMemory(&controls, sizeof(controls));
    controls.dwSize = sizeof(controls);
    controls.dwICC = ICC_TAB_CLASSES | ICC_LISTVIEW_CLASSES;
    InitCommonControlsEx(&controls);

    if (!RegisterApplicationClasses(instance))
    {
        const DWORD error = GetLastError();
        ShowFatalWin32Error(L"Регистрация оконных классов", error);
        return 1;
    }

    DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_CLIPCHILDREN;
    DWORD exStyle = 0;

    RECT windowRect;
    windowRect.left = 0;
    windowRect.top = 0;
    windowRect.right = kClientWidth;
    windowRect.bottom = kClientHeight;
    AdjustWindowRectEx(&windowRect, style, FALSE, exStyle);

    HWND window = CreateWindowExW(exStyle,
                                  kMainWindowClass,
                                  kApplicationSignature,
                                  style,
                                  CW_USEDEFAULT,
                                  CW_USEDEFAULT,
                                  windowRect.right - windowRect.left,
                                  windowRect.bottom - windowRect.top,
                                  nullptr,
                                  nullptr,
                                  instance,
                                  nullptr);

    if (window == nullptr)
    {
        const DWORD error = GetLastError();
        ShowFatalWin32Error(L"Создание главного окна", error);
        return 1;
    }

    if (!privilegeResult.Success())
    {
        ShowResult(window,
                   L"",
                   L"Предупреждение привилегий",
                   privilegeResult);
    }

    ShowWindow(window, commandShow);
    UpdateWindow(window);

    MSG message;
    while (GetMessageW(&message, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&message);
        DispatchMessageW(&message);
    }

    return static_cast<int>(message.wParam);
}
