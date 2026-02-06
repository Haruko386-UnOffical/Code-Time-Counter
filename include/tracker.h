#pragma once
#include <string>
#include <vector>
#include <atomic>
#include <mutex> // 新增
#include <windows.h>

class CodeTracker {
public:
    static CodeTracker& Get() {
        static CodeTracker instance;
        return instance;
    }

    void Init();
    void StartLoop(); // 修改：启动后台线程循环
    void StopLoop();  // 新增：停止循环

    // Getters (需要加锁)
    long long GetTotalTime();
    long long GetSessionTime();
    bool IsStrictMode();
    bool IsTracking();
    std::string GetCurrentApp();

    // Setters
    void SetStrictMode(bool strict);

private:
    CodeTracker() {}
    
    void Loop(); // 实际的线程函数
    void LoadConfig();
    void SaveConfig();
    void LoadData();
    void SaveData();
    void UpdateBadge();
    
    bool CheckUserActivity(); 
    bool GetActiveWindowTitle(std::string& outTitle);

    long long m_totalSeconds = 0;
    long long m_sessionSeconds = 0;
    bool m_strictMode = false;
    bool m_isTracking = false;
    std::string m_currentApp;
    
    // 线程控制
    std::atomic<bool> m_running{false};
    std::mutex m_mutex; // 数据互斥锁

    const std::string DATA_FILE = "code_time.dat";
    const std::string CONFIG_FILE = "config.ini";
    const std::string BADGE_FILE = "badge.md";
};