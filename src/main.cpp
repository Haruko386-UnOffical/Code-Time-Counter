#include "../include/tracker.h"
#include "../include/webview.h"
#include <windows.h>
#include <shellapi.h>
#include <commdlg.h>
#include <sstream>
#include <string>
#include <dwmapi.h>

#ifndef DWMWA_WINDOW_CORNER_PREFERENCE
#define DWMWA_WINDOW_CORNER_PREFERENCE 33
#endif
#ifndef DWMWCP_ROUND
#define DWMWCP_ROUND 2
#endif

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_SHOW 1002
#define IDI_MAIN_ICON 101

HWND g_hMainWnd = NULL;
NOTIFYICONDATAA g_nid;
WNDPROC g_OriginalWndProc = NULL;
std::string GetConfigPath()
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exeDir = path;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    return exeDir + "\\config.ini";
}

std::string GetExeDir()
{
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exeDir = path;
    return exeDir.substr(0, exeDir.find_last_of("\\/"));
}

void SaveUiConfig(const std::string &key, const std::string &value)
{
    WritePrivateProfileStringA("UI", key.c_str(), value.c_str(), GetConfigPath().c_str());
}

std::string LoadUiConfig(const std::string &key, const std::string &defaultVal)
{
    char buf[MAX_PATH];
    GetPrivateProfileStringA("UI", key.c_str(), defaultVal.c_str(), buf, MAX_PATH, GetConfigPath().c_str());
    return std::string(buf);
}

void SaveWindowPos(HWND hwnd)
{
    RECT rect;
    if (GetWindowRect(hwnd, &rect))
    {
        SaveUiConfig("WinX", std::to_string(rect.left));
        SaveUiConfig("WinY", std::to_string(rect.top));
        SaveUiConfig("WinW", std::to_string(rect.right - rect.left));
        SaveUiConfig("WinH", std::to_string(rect.bottom - rect.top));
    }
}

void LoadWindowPos(HWND hwnd)
{
    int x = std::stoi(LoadUiConfig("WinX", "-1"));
    int y = std::stoi(LoadUiConfig("WinY", "-1"));
    int w = std::stoi(LoadUiConfig("WinW", "320"));
    int h = std::stoi(LoadUiConfig("WinH", "380"));

    if (x != -1 && y != -1)
    {
        SetWindowPos(hwnd, NULL, x, y, w, h, SWP_NOZORDER);
    }
}

std::string MakeJsonStats()
{
    auto &tracker = CodeTracker::Get();
    std::stringstream ss;
    ss << "{";
    ss << "\"total\": " << tracker.GetTotalTime() << ",";
    ss << "\"session\": " << tracker.GetSessionTime() << ",";
    ss << "\"isTracking\": " << (tracker.IsTracking() ? "true" : "false") << ",";

    if (g_hMainWnd)
    {
        RECT rc;
        GetWindowRect(g_hMainWnd, &rc);
        ss << "\"winW\": " << (rc.right - rc.left) << ",";
        ss << "\"winH\": " << (rc.bottom - rc.top) << ",";
    }

    std::string app = tracker.GetCurrentApp();
    std::string escapedApp;
    for (char c : app)
    {
        if (c == '\\')
            escapedApp += "\\\\";
        else if (c == '"')
            escapedApp += "\\\"";
        else
            escapedApp += c;
    }
    ss << "\"app\": \"" << escapedApp << "\"";
    ss << "}";
    return ss.str();
}

std::string OpenFileDialog(HWND owner)
{
    OPENFILENAMEA ofn;
    char szFile[MAX_PATH] = {0};
    ZeroMemory(&ofn, sizeof(ofn));
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = owner;
    ofn.lpstrFile = szFile;
    ofn.nMaxFile = sizeof(szFile);
    ofn.lpstrFilter = "Images\0*.jpg;*.jpeg;*.png;*.bmp;*.gif\0All\0*.*\0";
    ofn.nFilterIndex = 1;
    ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST;

    if (GetOpenFileNameA(&ofn) == TRUE)
    {
        return std::string(ofn.lpstrFile);
    }
    return "";
}

void InitTrayIcon(HWND hwnd)
{
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconA(GetModuleHandle(NULL), (LPCSTR)MAKEINTRESOURCE(IDI_MAIN_ICON));
    if (!g_nid.hIcon)
        g_nid.hIcon = LoadIconA(NULL, (LPCSTR)IDI_APPLICATION);
    strcpy(g_nid.szTip, "Code Tracker (Running)");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_NCCALCSIZE:
    {
        if (wParam)
            return 0;
        break;
    }
    case WM_NCHITTEST:
    {
        LRESULT hit = DefWindowProc(hwnd, msg, wParam, lParam); // Get default hit test
        if (hit == HTCLIENT)
        { // Only override if mouse is over client area
            POINT pt = {LOWORD(lParam), HIWORD(lParam)};
            ScreenToClient(hwnd, &pt);

            RECT rc;
            GetClientRect(hwnd, &rc);

            const int grip = 8; // Pixel width of invisible resize grips on edges/corners

            // Detect edges and corners
            if (pt.x <= grip && pt.y <= grip)
                return HTTOPLEFT;
            if (pt.x >= rc.right - grip && pt.y <= grip)
                return HTTOPRIGHT;
            if (pt.x <= grip && pt.y >= rc.bottom - grip)
                return HTBOTTOMLEFT;
            if (pt.x >= rc.right - grip && pt.y >= rc.bottom - grip)
                return HTBOTTOMRIGHT;
            if (pt.x <= grip)
                return HTLEFT;
            if (pt.x >= rc.right - grip)
                return HTRIGHT;
            if (pt.y <= grip)
                return HTTOP;
            if (pt.y >= rc.bottom - grip)
                return HTBOTTOM;
        }
        return hit; // Fallback to default (e.g., for dragging via HTCAPTION)
    }

    case WM_CLOSE:
        SaveWindowPos(hwnd);
        ShowWindow(hwnd, SW_HIDE);
        return 0;

    case WM_TRAYICON:
        if (lParam == WM_LBUTTONDBLCLK)
        {
            ShowWindow(hwnd, SW_SHOW);
            SetForegroundWindow(hwnd);
        }
        else if (lParam == WM_RBUTTONUP)
        {
            POINT pt;
            GetCursorPos(&pt);
            HMENU hMenu = CreatePopupMenu();
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_SHOW, "Show Tracker");
            AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit Completely");
            SetForegroundWindow(hwnd);
            int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
            DestroyMenu(hMenu);
            if (cmd == ID_TRAY_EXIT)
            {
                SaveWindowPos(hwnd);
                Shell_NotifyIconA(NIM_DELETE, &g_nid);
                SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
                DestroyWindow(hwnd);
            }
            else if (cmd == ID_TRAY_SHOW)
            {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            }
        }
        break;

    case WM_DESTROY:
        Shell_NotifyIconA(NIM_DELETE, &g_nid);
        PostQuitMessage(0);
        break;
    }
    return CallWindowProc(g_OriginalWndProc, hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "CodeTrackerSingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS)
        return 0;

    CodeTracker::Get().Init();
    CodeTracker::Get().StartLoop();

    webview::webview w(true, nullptr);
    w.set_title("Code Tracker");
    w.set_size(320, 380, WEBVIEW_HINT_NONE);

    w.bind("get_stats", [&](std::string seq, std::string req, void *arg)
           { w.resolve(seq, 0, MakeJsonStats()); }, nullptr);

    w.bind("pick_bg_image", [&](std::string seq, std::string req, void *arg)
           {
        std::string srcPath = OpenFileDialog(g_hMainWnd);
        std::string finalPath = "";
        if (!srcPath.empty()) {
            std::string exeDir = GetExeDir();
            std::string bgDir = exeDir + "\\bg";
            CreateDirectoryA(bgDir.c_str(), NULL);
            std::string fileName = srcPath.substr(srcPath.find_last_of("\\/") + 1);
            std::string destPath = bgDir + "\\" + fileName;
            if (CopyFileA(srcPath.c_str(), destPath.c_str(), FALSE)) finalPath = destPath;
            else finalPath = srcPath; 
            for (auto& c : finalPath) if (c == '\\') c = '/';
            SaveUiConfig("BgPath", finalPath);
        }
        w.resolve(seq, 0, "\"" + finalPath + "\""); }, nullptr);

    w.bind("save_blur", [&](std::string seq, std::string req, void *arg)
           {
        if (req.size() > 2) {
            std::string val = req.substr(1, req.size()-2);
            SaveUiConfig("BlurVal", val);
        }
        w.resolve(seq, 0, ""); }, nullptr);

    w.bind("get_config", [&](std::string seq, std::string req, void *arg)
           {
        std::string bg = LoadUiConfig("BgPath", "");
        std::string blur = LoadUiConfig("BlurVal", "10");
        std::string json = "{\"bg\":\"" + bg + "\", \"blur\":" + blur + "}";
        w.resolve(seq, 0, json); }, nullptr);

    w.bind("app_minimize", [&](std::string seq, std::string req, void *arg)
           {
        SaveWindowPos(g_hMainWnd);
        ShowWindow(g_hMainWnd, SW_HIDE);
        w.resolve(seq, 0, ""); }, nullptr);

    w.bind("app_exit", [&](std::string seq, std::string req, void *arg)
           {
        SaveWindowPos(g_hMainWnd);
        DestroyWindow(g_hMainWnd); 
        w.resolve(seq, 0, ""); }, nullptr);

    w.bind("drag_window", [&](std::string seq, std::string req, void *arg)
           {
        ReleaseCapture();
        POINT pt; GetCursorPos(&pt);
        PostMessage(g_hMainWnd, WM_NCLBUTTONDOWN, HTCAPTION, MAKELPARAM(pt.x, pt.y));
        w.resolve(seq, 0, ""); }, nullptr);

    w.bind("resize_window", [&](std::string seq, std::string req, void *arg)
           {
        // Remove outer brackets and any whitespace
        req.erase(std::remove_if(req.begin(), req.end(), ::isspace), req.end());
        if (req.size() < 3 || req[0] != '[' || req.back() != ']') {
            w.resolve(seq, 0, "");
            return;
        }
        req = req.substr(1, req.size() - 2);  // Now "width,height"

        size_t comma = req.find(',');
        if (comma == std::string::npos) {
            w.resolve(seq, 0, "");
            return;
        }

        try {
            int width = std::stoi(req.substr(0, comma));
            int height = std::stoi(req.substr(comma + 1));
            if (width > 0 && height > 0) {
                if (width < 250) width = 250; // 最小宽度限制(没招了)
                SetWindowPos(g_hMainWnd, NULL, 0, 0, width, height, SWP_NOMOVE | SWP_NOZORDER);
                SaveWindowPos(g_hMainWnd);
            }
        } catch (...) {}
        w.resolve(seq, 0, ""); }, nullptr);

    w.bind("set_always_on_top", [&](std::string seq, std::string req, void *arg)
           {
        // req是"[true]"或"[false]"，解析为bool
        bool onTop = (req.find("true") != std::string::npos);
        HWND insertAfter = onTop ? HWND_TOPMOST : HWND_NOTOPMOST;
        SetWindowPos(g_hMainWnd, insertAfter, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_SHOWWINDOW);
        SaveUiConfig("AlwaysOnTop", onTop ? "1" : "0");  // 保存到config
        w.resolve(seq, 0, ""); }, nullptr);

    // 在get_config绑定中添加
    w.bind("get_config", [&](std::string seq, std::string req, void *arg)
           {
        std::string bg = LoadUiConfig("BgPath", "");
        std::string blur = LoadUiConfig("BlurVal", "10");
        std::string alwaysOnTop = LoadUiConfig("AlwaysOnTop", "0");
        std::string json = "{\"bg\":\"" + bg + "\", \"blur\":" + blur + ", \"alwaysOnTop\":" + (alwaysOnTop == "1" ? "true" : "false") + "}";
        w.resolve(seq, 0, json); }, nullptr);

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exeDir = path;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    std::string htmlPath = "file:///" + exeDir + "/ui/index.html";
    w.navigate(htmlPath);

    g_hMainWnd = (HWND)w.window().value();

    LONG_PTR style = GetWindowLongPtr(g_hMainWnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_SYSMENU | WS_THICKFRAME | WS_MAXIMIZEBOX);
    // style |= WS_MINIMIZEBOX;
    SetWindowLongPtr(g_hMainWnd, GWL_STYLE, style);

    SetWindowPos(g_hMainWnd, NULL, 0, 0, 0, 0, SWP_FRAMECHANGED | SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);

    int preference = DWMWCP_ROUND;
    DwmSetWindowAttribute(g_hMainWnd, DWMWA_WINDOW_CORNER_PREFERENCE, &preference, sizeof(preference));

    LoadWindowPos(g_hMainWnd);
    InitTrayIcon(g_hMainWnd);
    g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_hMainWnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc);

    w.run();

    CodeTracker::Get().StopLoop();
    if (hMutex)
    {
        ReleaseMutex(hMutex);
        CloseHandle(hMutex);
    }
    return 0;
}