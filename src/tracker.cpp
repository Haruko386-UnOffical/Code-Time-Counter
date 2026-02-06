#include "../include/tracker.h"
#include <fstream>
#include <algorithm>
#include <thread>
#include <chrono>
#include <shlwapi.h> 
#include <windows.h> 

// 1. 严格模式白名单：只有IDE (用于 Strict 模式)
const std::vector<std::string> STRICT_APPS = {
    "Visual Studio", "Visual Studio Code", "IntelliJ", "PyCharm", "CLion", 
    "Eclipse", "Sublime Text", "Vim", "Neovim", "Atom", "Dev-C++", 
    "Qt Creator", "Android Studio", "Cursor", "Code Tracker"
};

// 2. 宽松模式白名单：IDE + 浏览器 + 常用工具 (用于 Loose 模式)
const std::vector<std::string> GENERAL_APPS = {
    // 包含所有 IDE
    "Visual Studio", "Code", "IntelliJ", "PyCharm", "CLion", "Eclipse", "Android Studio", "Cursor",
    // 浏览器
    "Google Chrome", "Microsoft Edge", "Firefox", "Brave",
    // 数据库与工具
    "DBeaver", "Navicat", "Postman", "Fiddler", "Wireshark", 
    // 终端
    "PowerShell", "cmd.exe", "Windows Terminal", "Git Bash"
};

// --- 辅助函数 ---
std::string ToUtf8(const std::wstring& wstr) {
    if (wstr.empty()) return "";
    int size_needed = WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), NULL, 0, NULL, NULL);
    std::string strTo(size_needed, 0);
    WideCharToMultiByte(CP_UTF8, 0, &wstr[0], (int)wstr.size(), &strTo[0], size_needed, NULL, NULL);
    return strTo;
}

std::string GetAbsolutePath(const std::string& filename) {
    char buffer[MAX_PATH];
    GetModuleFileNameA(NULL, buffer, MAX_PATH);
    PathRemoveFileSpecA(buffer); 
    std::string path = std::string(buffer) + "\\" + filename;
    return path;
}

// 检查标题是否匹配列表中的任意关键字
bool MatchesList(const std::string& title, const std::vector<std::string>& list) {
    std::string lowerTitle = title;
    std::transform(lowerTitle.begin(), lowerTitle.end(), lowerTitle.begin(), ::tolower);

    for (const auto& keyword : list) {
        std::string lowerKw = keyword;
        std::transform(lowerKw.begin(), lowerKw.end(), lowerKw.begin(), ::tolower);
        if (lowerTitle.find(lowerKw) != std::string::npos) return true;
    }
    return false;
}

void CodeTracker::Init() {
    LoadData();
    LoadConfig();
}

void CodeTracker::StartLoop() {
    if (m_running) return;
    m_running = true;
    std::thread([this]() { this->Loop(); }).detach();
}

void CodeTracker::StopLoop() {
    m_running = false;
    std::lock_guard<std::mutex> lock(m_mutex);
    SaveData();
    UpdateBadge();
}

void CodeTracker::Loop() {
    while (m_running) {
        std::string title; 
        bool hasWindow = GetActiveWindowTitle(title); // 获取当前窗口标题
        
        bool shouldCount = false;

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            
            if (!hasWindow) {
                 m_currentApp = "Idle";
            } else {
                m_currentApp = title;
                
                if (m_strictMode) {
                    // 【严格模式】：必须是 IDE + 有键盘鼠标活动
                    if (MatchesList(title, STRICT_APPS)) {
                         shouldCount = CheckUserActivity();
                    }
                } else {
                    // 【宽松模式】：是 IDE 或 浏览器/工具 即可，无需一直动键盘
                    if (MatchesList(title, GENERAL_APPS)) {
                        shouldCount = true;
                    }
                }
            }

            m_isTracking = shouldCount;

            if (shouldCount) {
                m_totalSeconds++;
                m_sessionSeconds++;
                if (m_totalSeconds % 5 == 0) {
                    SaveData();
                    UpdateBadge();
                }
            }
        }

        std::this_thread::sleep_for(std::chrono::seconds(1));
    }
}

// 获取窗口标题的实现提取出来
bool CodeTracker::GetActiveWindowTitle(std::string& outTitle) {
    HWND hwnd = GetForegroundWindow();
    if (!hwnd) return false;
    wchar_t wnd_title[256];
    if (GetWindowTextW(hwnd, wnd_title, sizeof(wnd_title)/sizeof(wchar_t)) == 0) return false;
    outTitle = ToUtf8(std::wstring(wnd_title)); 
    return true;
}

// Getters 保持不变
long long CodeTracker::GetTotalTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_totalSeconds;
}

long long CodeTracker::GetSessionTime() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_sessionSeconds;
}

bool CodeTracker::IsStrictMode() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_strictMode;
}

bool CodeTracker::IsTracking() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_isTracking;
}

std::string CodeTracker::GetCurrentApp() {
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_currentApp;
}

void CodeTracker::SetStrictMode(bool strict) {
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        m_strictMode = strict;
    }
    SaveConfig(); 
}

bool CodeTracker::CheckUserActivity() {
    LASTINPUTINFO lii;
    lii.cbSize = sizeof(LASTINPUTINFO);
    if (GetLastInputInfo(&lii)) {
        DWORD currentTick = GetTickCount();
        return (currentTick - lii.dwTime) < 60000;
    }
    return false;
}

void CodeTracker::LoadData() {
    std::string path = GetAbsolutePath(DATA_FILE);
    std::ifstream in(path);
    if (in >> m_totalSeconds) {
        // 读取成功
    } 
}

void CodeTracker::SaveData() {
    std::ofstream out(GetAbsolutePath(DATA_FILE));
    out << m_totalSeconds;
}

void CodeTracker::LoadConfig() {
    char buf[16];
    GetPrivateProfileStringA("Settings", "StrictMode", "0", buf, 16, GetAbsolutePath(CONFIG_FILE).c_str());
    m_strictMode = (std::string(buf) == "1");
}

void CodeTracker::SaveConfig() {
    WritePrivateProfileStringA("Settings", "StrictMode", m_strictMode ? "1" : "0", GetAbsolutePath(CONFIG_FILE).c_str());
}

void CodeTracker::UpdateBadge() {
    long long totalHours = m_totalSeconds / 3600;
    long long minutes = (m_totalSeconds % 3600) / 60;
    long long seconds = m_totalSeconds % 60;

    char timeStr[64];
    sprintf(timeStr, "%lldh%%20%02lldm%%20%02llds", totalHours, minutes, seconds);

    std::string badgeUrl = "https://img.shields.io/badge/Code%20Time-" + std::string(timeStr) + "-blue?style=flat";
    std::string markdown = "![Code Time](" + badgeUrl + ")";
    
    std::ofstream out(GetAbsolutePath(BADGE_FILE));
    out << markdown;
}