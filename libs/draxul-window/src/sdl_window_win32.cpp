#ifdef _WIN32

#include <draxul/log.h>

// clang-format off
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <shellapi.h>
// clang-format on

#include <functional>
#include <string>

namespace draxul
{

// ---------------------------------------------------------------------------
// System tray icon for Windows (headless / detached mode)
// ---------------------------------------------------------------------------
// When all windows are closed but sessions are alive, a tray icon provides
// "Attach", "New Session", and "Quit" actions via a right-click context menu.

namespace
{

constexpr UINT WM_TRAY_ICON = WM_USER + 1;
constexpr UINT IDM_ATTACH = 1001;
constexpr UINT IDM_QUIT = 1002;

HWND g_tray_hwnd = nullptr;
NOTIFYICONDATAW g_nid = {};
std::function<void()>* g_on_attach = nullptr;
std::function<void()>* g_on_quit = nullptr;

LRESULT CALLBACK tray_wnd_proc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_TRAY_ICON:
        if (LOWORD(lParam) == WM_RBUTTONUP || LOWORD(lParam) == WM_CONTEXTMENU)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU menu = CreatePopupMenu();
            AppendMenuW(menu, MF_STRING, IDM_ATTACH, L"Attach Session");
            AppendMenuW(menu, MF_SEPARATOR, 0, nullptr);
            AppendMenuW(menu, MF_STRING, IDM_QUIT, L"Quit Draxul");
            // Required for the menu to dismiss when clicking elsewhere.
            SetForegroundWindow(hwnd);
            TrackPopupMenu(menu, TPM_RIGHTBUTTON, pt.x, pt.y, 0, hwnd, nullptr);
            DestroyMenu(menu);
            PostMessage(hwnd, WM_NULL, 0, 0);
        }
        else if (LOWORD(lParam) == WM_LBUTTONDBLCLK)
        {
            if (g_on_attach && *g_on_attach)
                (*g_on_attach)();
        }
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wParam))
        {
        case IDM_ATTACH:
            if (g_on_attach && *g_on_attach)
                (*g_on_attach)();
            break;
        case IDM_QUIT:
            if (g_on_quit && *g_on_quit)
                (*g_on_quit)();
            break;
        }
        return 0;

    case WM_DESTROY:
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        g_tray_hwnd = nullptr;
        return 0;

    default:
        return DefWindowProcW(hwnd, msg, wParam, lParam);
    }
}

} // namespace

void create_system_tray_icon(std::function<void()>* on_attach, std::function<void()>* on_quit)
{
    if (g_tray_hwnd)
        return; // Already created.

    g_on_attach = on_attach;
    g_on_quit = on_quit;

    // Register a hidden message-only window class for tray icon messages.
    WNDCLASSEXW wc = {};
    wc.cbSize = sizeof(wc);
    wc.lpfnWndProc = tray_wnd_proc;
    wc.hInstance = GetModuleHandleW(nullptr);
    wc.lpszClassName = L"DraxulTrayWindow";
    RegisterClassExW(&wc);

    g_tray_hwnd = CreateWindowExW(0, L"DraxulTrayWindow", L"Draxul Tray",
        0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, wc.hInstance, nullptr);
    if (!g_tray_hwnd)
    {
        DRAXUL_LOG_ERROR(LogCategory::Window, "Failed to create tray message window");
        return;
    }

    g_nid = {};
    g_nid.cbSize = sizeof(g_nid);
    g_nid.hWnd = g_tray_hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAY_ICON;
    // Use the application's main icon, falling back to a default.
    g_nid.hIcon = LoadIconW(GetModuleHandleW(nullptr), L"IDI_ICON1");
    if (!g_nid.hIcon)
        g_nid.hIcon = LoadIconW(nullptr, MAKEINTRESOURCEW(32512)); // IDI_APPLICATION
    wcscpy_s(g_nid.szTip, L"Draxul — sessions running");

    if (!Shell_NotifyIconW(NIM_ADD, &g_nid))
        DRAXUL_LOG_ERROR(LogCategory::Window, "Shell_NotifyIcon NIM_ADD failed");
}

void destroy_system_tray_icon()
{
    if (g_tray_hwnd)
    {
        Shell_NotifyIconW(NIM_DELETE, &g_nid);
        DestroyWindow(g_tray_hwnd);
        g_tray_hwnd = nullptr;
    }
}

bool has_system_tray_icon()
{
    return g_tray_hwnd != nullptr;
}

void pump_tray_messages()
{
    if (!g_tray_hwnd)
        return;
    MSG msg;
    while (PeekMessageW(&msg, g_tray_hwnd, 0, 0, PM_REMOVE))
    {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
}

} // namespace draxul

#endif // _WIN32
