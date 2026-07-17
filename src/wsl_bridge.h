#pragma once
#include <string>
#include <vector>
#include <functional>
#include <windows.h>

struct CommandResult {
    int exitCode = -1;
    std::wstring output;
    std::wstring error;
    bool success() const { return exitCode == 0; }
};

// Output encoding of a child process
enum class OutEnc {
    UTF8,     // PowerShell (forced), WSL bash
    UTF16LE,  // wsl.exe native messages
    OEM       // console tools like diskpart (CP866 on Russian Windows)
};

class WSLBridge {
public:
    WSLBridge() = default;
    ~WSLBridge() { CloseSession(); }
    // streamLog: post each output line to the log window as it arrives
    // (progress of long-running tools; not supported for UTF16LE)
    CommandResult RunProcess(const std::wstring& commandLine,
                             int timeoutMs = 30000,
                             const std::string& stdinData = "",
                             OutEnc enc = OutEnc::UTF8,
                             bool streamLog = false);

    void setLogWindow(HWND hw) { m_logHwnd = hw; }
    void note(const std::wstring& msg) { log(msg); } // progress notes from Operations

    // Called with 0..100 when a streamed line ends with a percentage.
    // Set/clear from the same thread that calls RunProcess.
    void setProgressCallback(std::function<void(int)> cb) { m_progressCb = std::move(cb); }

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

    bool checkWSLInstalled();
    std::vector<std::pair<std::wstring, bool>> getDistros();

    static std::string  WideToUTF8(const std::wstring& w);
    static std::wstring UTF8ToWide(const std::string& u);

    // Absolute quoted path to a System32 binary — avoids PATH/CWD hijacking
    static std::wstring SysExe(const std::wstring& relPath);

    void setWslPassword(const std::wstring& p) { m_wslUserPassword = p; m_sudoAuthorized = false; }
    const std::wstring& wslPassword() const { return m_wslUserPassword; }

    bool AuthorizeSudo(const std::wstring& distro = L"");
    void CloseSession();

private:
    HWND m_logHwnd = nullptr;
    std::function<void(int)> m_progressCb;
    std::wstring m_wslUserPassword;
    bool m_sudoAuthorized = false;
    void log(const std::wstring& msg);

    struct Session {
        HANDLE hProcess = nullptr;
        HANDLE hIn = nullptr;
        HANDLE hOut = nullptr;
        std::wstring distro;
    };
    Session* m_session = nullptr;

    bool EnsureSession(const std::wstring& distro);
    CommandResult runInSession(const std::wstring& command, int timeoutMs, const std::string& stdinData = "");
};
