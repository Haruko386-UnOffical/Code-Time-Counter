#include "../include/tracker.h"
#include "../include/webview.h" 
#include <windows.h>
#include <shellapi.h>
#include <sstream>
#include <string>

#define WM_TRAYICON (WM_USER + 1)
#define ID_TRAY_EXIT 1001
#define ID_TRAY_SHOW 1002
#define IDI_MAIN_ICON 101 

HWND g_hMainWnd = NULL;
NOTIFYICONDATAA g_nid;
WNDPROC g_OriginalWndProc = NULL;

// 获取配置文件路径
std::string GetConfigPath() {
    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exeDir = path;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    return exeDir + "\\config.ini";
}

// 保存窗口位置
void SaveWindowPos(HWND hwnd) {
    RECT rect;
    if (GetWindowRect(hwnd, &rect)) {
        std::string path = GetConfigPath();
        WritePrivateProfileStringA("Window", "X", std::to_string(rect.left).c_str(), path.c_str());
        WritePrivateProfileStringA("Window", "Y", std::to_string(rect.top).c_str(), path.c_str());
    }
}

// 加载窗口位置
void LoadWindowPos(HWND hwnd) {
    std::string path = GetConfigPath();
    int x = GetPrivateProfileIntA("Window", "X", -1, path.c_str());
    int y = GetPrivateProfileIntA("Window", "Y", -1, path.c_str());
    
    // 如果读取到了有效坐标 (-1 表示没存过)
    if (x != -1 && y != -1) {
        // SWP_NOSIZE: 保持大小不变, SWP_NOZORDER: 保持层级不变
        SetWindowPos(hwnd, NULL, x, y, 0, 0, SWP_NOSIZE | SWP_NOZORDER);
    }
}

// JSON 生成
std::string MakeJsonStats() {
    auto& tracker = CodeTracker::Get();
    std::stringstream ss;
    ss << "{";
    ss << "\"total\": " << tracker.GetTotalTime() << ",";
    ss << "\"session\": " << tracker.GetSessionTime() << ",";
    ss << "\"isStrict\": " << (tracker.IsStrictMode() ? "true" : "false") << ",";
    ss << "\"isTracking\": " << (tracker.IsTracking() ? "true" : "false") << ",";
    
    std::string app = tracker.GetCurrentApp();
    std::string escapedApp;
    for (char c : app) {
        if (c == '\\') escapedApp += "\\\\";
        else if (c == '"') escapedApp += "\\\"";
        else escapedApp += c;
    }
    ss << "\"app\": \"" << escapedApp << "\""; 
    ss << "}";
    return ss.str();
}

void InitTrayIcon(HWND hwnd) {
    g_nid.cbSize = sizeof(NOTIFYICONDATAA);
    g_nid.hWnd = hwnd;
    g_nid.uID = 1;
    g_nid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    g_nid.uCallbackMessage = WM_TRAYICON;
    g_nid.hIcon = LoadIconA(GetModuleHandle(NULL), (LPCSTR)MAKEINTRESOURCE(IDI_MAIN_ICON)); 
    if (!g_nid.hIcon) g_nid.hIcon = LoadIcon(NULL, IDI_APPLICATION);
    strcpy(g_nid.szTip, "Code Tracker (Running)");
    Shell_NotifyIconA(NIM_ADD, &g_nid);
}

void RemoveTrayIcon() {
    Shell_NotifyIconA(NIM_DELETE, &g_nid);
}

// 窗口过程回调 (处理拖拽和托盘)
LRESULT CALLBACK SubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
    switch (msg) {
        // 【核心修复1】：手动处理无边框窗口的拖拽
        case WM_NCHITTEST: {
            // 获取鼠标位置 (屏幕坐标)
            POINT pt = { LOWORD(lParam), HIWORD(lParam) };
            // 转换为窗口客户区坐标
            ScreenToClient(hwnd, &pt);

            // 如果鼠标在顶部 32px 区域内 (模拟标题栏)
            if (pt.y < 32) {
                RECT rcClient;
                GetClientRect(hwnd, &rcClient);
                
                // 【重要】：排除右上角关闭按钮区域 (假设右侧 50px 为按钮区)
                // 如果不排除，点击关闭按钮会被系统认为是“拖拽”，导致按钮点不动
                if (pt.x > (rcClient.right - 50)) {
                    return HTCLIENT; // 这里的点击交给网页处理 (关闭按钮)
                }
                
                return HTCAPTION; // 其他顶部区域告诉系统这是“标题栏”，允许拖拽
            }
            break; // 其他区域交给默认处理
        }

        case WM_CLOSE:
            SaveWindowPos(hwnd); // 隐藏前保存位置
            ShowWindow(hwnd, SW_HIDE);
            return 0;

        case WM_TRAYICON:
            if (lParam == WM_LBUTTONDBLCLK) {
                ShowWindow(hwnd, SW_SHOW);
                SetForegroundWindow(hwnd);
            } else if (lParam == WM_RBUTTONUP) {
                POINT pt;
                GetCursorPos(&pt);
                HMENU hMenu = CreatePopupMenu();
                AppendMenuA(hMenu, MF_STRING, ID_TRAY_SHOW, "Show Tracker");
                AppendMenuA(hMenu, MF_STRING, ID_TRAY_EXIT, "Exit Completely");
                SetForegroundWindow(hwnd); 
                int cmd = TrackPopupMenu(hMenu, TPM_RETURNCMD | TPM_BOTTOMALIGN, pt.x, pt.y, 0, hwnd, NULL);
                DestroyMenu(hMenu);

                if (cmd == ID_TRAY_EXIT) {
                    SaveWindowPos(hwnd); // 退出前保存位置
                    RemoveTrayIcon();
                    SetWindowLongPtr(hwnd, GWLP_WNDPROC, (LONG_PTR)g_OriginalWndProc);
                    DestroyWindow(hwnd); 
                } else if (cmd == ID_TRAY_SHOW) {
                    ShowWindow(hwnd, SW_SHOW);
                    SetForegroundWindow(hwnd);
                }
            }
            break;
            
        case WM_DESTROY:
            RemoveTrayIcon();
            PostQuitMessage(0);
            break;
    }
    return CallWindowProc(g_OriginalWndProc, hwnd, msg, wParam, lParam);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    HANDLE hMutex = CreateMutexA(NULL, TRUE, "CodeTrackerSingleInstanceMutex");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;

    CodeTracker::Get().Init();
    CodeTracker::Get().StartLoop(); 

    webview::webview w(true, nullptr); 
    w.set_title("Code Tracker");
    w.set_size(320, 380, WEBVIEW_HINT_NONE);

    w.bind("get_stats", [&](std::string seq, std::string req, void* arg) {
        w.resolve(seq, 0, MakeJsonStats());
    }, nullptr);

    w.bind("set_strict", [&](std::string seq, std::string req, void* arg) {
        bool strict = (req.find("true") != std::string::npos);
        CodeTracker::Get().SetStrictMode(strict);
        w.resolve(seq, 0, "");
    }, nullptr);

    w.bind("close_app", [&](std::string seq, std::string req, void* arg) {
        SaveWindowPos(g_hMainWnd); // JS 触发关闭时保存
        ShowWindow(g_hMainWnd, SW_HIDE);
        w.resolve(seq, 0, "");
    }, nullptr);

    char path[MAX_PATH];
    GetModuleFileNameA(NULL, path, MAX_PATH);
    std::string exeDir = path;
    exeDir = exeDir.substr(0, exeDir.find_last_of("\\/"));
    std::string htmlPath = "file:///" + exeDir + "/ui/index.html";
    w.navigate(htmlPath);

    g_hMainWnd = (HWND)w.window().value(); 

    // 移除标题栏样式
    LONG_PTR style = GetWindowLongPtr(g_hMainWnd, GWL_STYLE);
    style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU);
    SetWindowLongPtr(g_hMainWnd, GWL_STYLE, style);
    SetWindowPos(g_hMainWnd, NULL, 0, 0, 0, 0, 
        SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_FRAMECHANGED);

    // 【核心修复2】：在显示窗口前，加载上次的位置
    LoadWindowPos(g_hMainWnd);

    InitTrayIcon(g_hMainWnd);
    g_OriginalWndProc = (WNDPROC)SetWindowLongPtr(g_hMainWnd, GWLP_WNDPROC, (LONG_PTR)SubclassProc);

    w.run();

    CodeTracker::Get().StopLoop(); 
    if (hMutex) { ReleaseMutex(hMutex); CloseHandle(hMutex); }
    return 0;
}