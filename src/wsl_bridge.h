#pragma once
#include <string>
#include <vector>
#include <windows.h>

struct CommandResult {
    int exitCode = -1;
    std::wstring output;
    std::wstring error;
    bool success() const { return exitCode == 0; }
};

class WSLBridge {
public:
    WSLBridge() = default;
    ~WSLBridge() { CloseSession(); }
    CommandResult RunProcess(const std::wstring& commandLine,
                             int timeoutMs = 30000,
                             const std::string& stdinData = "",
                             bool utf16leOutput = false);

    void setLogWindow(HWND hw) { m_logHwnd = hw; }

    CommandResult runPowerShell(const std::wstring& script, int timeoutMs = 30000);
    CommandResult runWSL(const std::wstring& command,
                         const std::wstring& distro = L"",
                         const std::wstring& user = L"",
                         int timeoutMs = 30000,
                         const std::string& stdinData = "");
    CommandResult runWSLRoot(const std::wstring& command,
                         const std::wstring& distro = L"",
                         int timeoutMs = 30000,
                         const std::string& stdinData = "");
    CommandResult runWSLMount(const std::vector<std::wstring>& args, int timeoutMs = 30000);

    bool checkWSLInstalled(std::wstring& msg);
    std::vector<std::pair<std::wstring, bool>> getDistros();

    static std::string  WideToUTF8(const std::wstring& w);
    static std::wstring UTF8ToWide(const std::string& u);

    void setWslPassword(const std::wstring& p) { m_wslUserPassword = p; m_sudoAuthorized = false; }
    const std::wstring& wslPassword() const { return m_wslUserPassword; }

    bool AuthorizeSudo(const std::wstring& distro = L"");

private:
    HWND m_logHwnd = nullptr;
    std::wstring m_wslUserPassword;
    bool m_sudoAuthorized = false;
    void log(const std::wstring& msg);

    struct Session {
        HANDLE hProcess = nullptr;
        HANDLE hIn = nullptr;
        HANDLE hOut = nullptr;
        std::wstring distro;
        bool authorized = false;
    };
    Session* m_session = nullptr;

    bool EnsureSession(const std::wstring& distro);
    void CloseSession();
    CommandResult runInSession(const std::wstring& command, int timeoutMs, const std::string& stdinData = "");
};
