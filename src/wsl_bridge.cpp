#include "wsl_bridge.h"
#include "resource.h"
#include <thread>
#include <algorithm>
#include <sstream>
#include <wincrypt.h>

// ── Helpers ──────────────────────────────────────────────

std::string WSLBridge::WideToUTF8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), nullptr, 0, nullptr, nullptr);
    std::string s(n, 0);
    WideCharToMultiByte(CP_UTF8, 0, w.c_str(), (int)w.size(), s.data(), n, nullptr, nullptr);
    return s;
}

std::wstring WSLBridge::UTF8ToWide(const std::string& u) {
    if (u.empty()) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), nullptr, 0);
    std::wstring w(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, u.c_str(), (int)u.size(), w.data(), n);
    return w;
}

// Decode raw bytes as UTF-16LE (used for wsl.exe native commands)
static std::wstring DecodeUTF16LE(const std::string& bytes) {
    if (bytes.size() < 2) return L"";
    const wchar_t* start = (const wchar_t*)bytes.data();
    size_t len = bytes.size() / 2;
    // Skip BOM if present
    if (len > 0 && start[0] == 0xFEFF) { start++; len--; }
    std::wstring r(start, len);
    // Remove embedded nulls
    r.erase(std::remove(r.begin(), r.end(), L'\0'), r.end());
    return r;
}

// Decode raw bytes as UTF-8 (used for PowerShell and WSL bash commands)
static std::wstring DecodeUTF8(const std::string& bytes) {
    if (bytes.empty()) return L"";
    int n = MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), nullptr, 0);
    if (n > 0) {
        std::wstring w(n, 0);
        MultiByteToWideChar(CP_UTF8, 0, bytes.c_str(), (int)bytes.size(), w.data(), n);
        w.erase(std::remove(w.begin(), w.end(), L'\0'), w.end());
        return w;
    }
    return L"";
}

static std::wstring Base64Encode(const void* data, size_t size) {
    DWORD len = 0;
    CryptBinaryToStringW((const BYTE*)data, (DWORD)size,
                          CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, nullptr, &len);
    std::wstring out(len, 0);
    CryptBinaryToStringW((const BYTE*)data, (DWORD)size,
                          CRYPT_STRING_BASE64 | CRYPT_STRING_NOCRLF, out.data(), &len);
    out.resize(len);
    while (!out.empty() && (out.back() == L'\r' || out.back() == L'\n' || out.back() == 0))
        out.pop_back();
    return out;
}

// ── RunProcess ───────────────────────────────────────────

CommandResult WSLBridge::RunProcess(const std::wstring& commandLine,
                                    int timeoutMs,
                                    const std::string& stdinData,
                                    bool utf16leOutput) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};

    HANDLE hOutR = nullptr, hOutW = nullptr;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0))
        return {-1, L"", L"CreatePipe failed"};
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);

    HANDLE hInR = nullptr, hInW = nullptr;
    if (!stdinData.empty()) {
        if (!CreatePipe(&hInR, &hInW, &sa, 0)) {
            CloseHandle(hOutR);
            return {-1, L"", L"CreatePipe(In) failed"};
        }
        SetHandleInformation(hInW, HANDLE_FLAG_INHERIT, 0);
        SetHandleInformation(hInR, HANDLE_FLAG_INHERIT, HANDLE_FLAG_INHERIT);
    }

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdOutput = hOutW;
    si.hStdError  = hOutW;
    si.hStdInput  = hInR ? hInR : INVALID_HANDLE_VALUE;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring cmd = commandLine;
    log(L"> " + cmd);

    BOOL ok = CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE,
                              CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(hOutW);
    if (hInR) CloseHandle(hInR);

    if (!ok) {
        CloseHandle(hOutR);
        if (hInW) CloseHandle(hInW);
        log(L"! CreateProcess failed");
        return {-1, L"", L"CreateProcess failed"};
    }

    // Write stdin
    if (hInW && !stdinData.empty()) {
        log(L"< Sending stdin: " + std::to_wstring(stdinData.size()) + L" bytes");
        DWORD written;
        WriteFile(hInW, stdinData.c_str(), (DWORD)stdinData.size(), &written, nullptr);
        CloseHandle(hInW);
        hInW = nullptr;
    }
    if (hInW) CloseHandle(hInW);

    // Read stdout in thread
    std::string rawOutput;
    std::thread reader([&]() {
        char buf[4096];
        DWORD bytesRead;
        while (ReadFile(hOutR, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0)
            rawOutput.append(buf, bytesRead);
    });

    DWORD wait = WaitForSingleObject(pi.hProcess, (DWORD)timeoutMs);
    if (wait == WAIT_TIMEOUT) {
        TerminateProcess(pi.hProcess, (UINT)-1);
        log(L"! Process timed out");
    }

    reader.join();
    CloseHandle(hOutR);

    DWORD exitCode = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &exitCode);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    std::wstring decoded = utf16leOutput ? DecodeUTF16LE(rawOutput) : DecodeUTF8(rawOutput);
    if (!decoded.empty()) log(L"OUT: " + decoded);
    log(L"# Exit code: " + std::to_wstring(exitCode));
    return {(int)exitCode, decoded, L""};
}

void WSLBridge::log(const std::wstring& msg) {
    if (m_logHwnd) {
        std::wstring* p = new std::wstring(msg + L"\r\n");
        PostMessageW(m_logHwnd, WM_APP_COMMAND_LOG, 0, (LPARAM)p);
    }
}

// ── PowerShell ───────────────────────────────────────────

CommandResult WSLBridge::runPowerShell(const std::wstring& script, int timeoutMs) {
    // Force UTF-8 output from PowerShell
    std::wstring fullScript =
        L"[Console]::OutputEncoding = [System.Text.Encoding]::UTF8; " + script;
    std::wstring b64 = Base64Encode(fullScript.c_str(), fullScript.size() * sizeof(wchar_t));
    std::wstring cmd = L"powershell.exe -NoProfile -NonInteractive -EncodedCommand " + b64;
    return RunProcess(cmd, timeoutMs, "", false); // UTF-8
}

// ── runWSL ───────────────────────────────────────────────

static std::wstring bashEsc(const std::wstring& s) {
    std::wstring res;
    for (wchar_t c : s) {
        if (c == L'\'') res += L"'\\''";
        else res += c;
    }
    return res;
}

CommandResult WSLBridge::runWSL(const std::wstring& command,
                                 const std::wstring& distro,
                                 const std::wstring& user,
                                 int timeoutMs,
                                 const std::string& stdinData) {
    if (user.empty()) {
        if (EnsureSession(distro)) {
            return runInSession(command, timeoutMs, stdinData);
        }
    }

    std::wstring cmd = L"wsl.exe";
    if (!distro.empty()) cmd += L" -d " + distro;
    if (!user.empty()) cmd += L" -u " + user;
    cmd += L" -- bash -c '" + bashEsc(command) + L"'";
    return RunProcess(cmd, timeoutMs, stdinData, false);
}

bool WSLBridge::AuthorizeSudo(const std::wstring& distro) {
    if (m_wslUserPassword.empty()) return false;
    if (!EnsureSession(distro)) return false;

    log(L"Authorizing sudo session...");
    std::string pwd = WideToUTF8(m_wslUserPassword) + "\n";
    std::string mark = "__AUTH_DONE__";
    std::string fullCmd = "sudo -v -S -p 'SUDO_PROMPT' ; echo " + mark + " $?\n";
    
    DWORD written;
    if (!WriteFile(m_session->hIn, fullCmd.data(), (DWORD)fullCmd.size(), &written, nullptr)) {
        log(L"! Failed to write to session stdin");
        return false;
    }

    std::string output;
    char buf[1024];
    DWORD bytesRead;
    bool pwdSent = false;
    bool finished = false;
    int exitCode = -1;
    size_t promptScanPos = 0;
    ULONGLONG start = GetTickCount64();

    while (GetTickCount64() - start < 15000) {
        DWORD available = 0;
        if (PeekNamedPipe(m_session->hOut, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            if (ReadFile(m_session->hOut, buf, sizeof(buf) - 1, &bytesRead, nullptr) && bytesRead > 0) {
                output.append(buf, bytesRead);

                size_t promptPos = output.find("SUDO_PROMPT", promptScanPos);
                if (promptPos != std::string::npos) {
                    if (!pwdSent) {
                        WriteFile(m_session->hIn, pwd.data(), (DWORD)pwd.size(), &written, nullptr);
                        pwdSent = true;
                        promptScanPos = promptPos + 11;
                        log(L"Sudo: password sent.");
                    } else {
                        // A second prompt means the password was rejected;
                        // kill the session so the pending prompt can't desync it
                        log(L"! Sudo rejected the password");
                        CloseSession();
                        return false;
                    }
                }

                size_t pos = output.find(mark);
                if (pos != std::string::npos) {
                    std::stringstream ss(output.substr(pos + mark.size()));
                    ss >> exitCode;
                    finished = true;
                    break;
                }
            }
        } else {
            Sleep(50);
        }
    }

    if (!finished) {
        log(L"! Sudo authorization timed out");
        CloseSession(); // leftover output would desync the session
        return false;
    }

    m_session->authorized = (exitCode == 0);
    m_sudoAuthorized = (exitCode == 0);
    
    if (m_sudoAuthorized) log(L"Sudo session authorized successfully.");
    else log(L"Sudo authorization failed (exit code " + std::to_wstring(exitCode) + L").");

    return m_sudoAuthorized;
}

bool WSLBridge::EnsureSession(const std::wstring& distro) {
    if (m_session && m_session->distro == distro) {
        // Check if still alive
        DWORD exitCode;
        if (GetExitCodeProcess(m_session->hProcess, &exitCode) && exitCode == STILL_ACTIVE)
            return true;
        CloseSession();
    }
    
    CloseSession();
    log(L"Opening persistent WSL session (" + distro + L")...");
    
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE hInR, hInW, hOutR, hOutW;
    if (!CreatePipe(&hInR, &hInW, &sa, 0)) return false;
    if (!CreatePipe(&hOutR, &hOutW, &sa, 0)) {
        CloseHandle(hInR); CloseHandle(hInW);
        return false;
    }
    SetHandleInformation(hInW, HANDLE_FLAG_INHERIT, 0);
    SetHandleInformation(hOutR, HANDLE_FLAG_INHERIT, 0);

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.hStdInput = hInR;
    si.hStdOutput = hOutW;
    si.hStdError = hOutW;
    si.wShowWindow = SW_HIDE;

    PROCESS_INFORMATION pi{};
    std::wstring cmd = L"wsl.exe";
    if (!distro.empty()) cmd += L" -d " + distro;
    cmd += L" -- bash";

    if (!CreateProcessW(nullptr, cmd.data(), nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi)) {
        CloseHandle(hInR); CloseHandle(hInW); CloseHandle(hOutR); CloseHandle(hOutW);
        return false;
    }
    CloseHandle(hInR); CloseHandle(hOutW);
    CloseHandle(pi.hThread);

    m_session = new Session{pi.hProcess, hInW, hOutR, distro, false};
    
    // Small wait for bash to start
    Sleep(200);
    return true;
}

void WSLBridge::CloseSession() {
    if (!m_session) return;
    TerminateProcess(m_session->hProcess, 0);
    CloseHandle(m_session->hProcess);
    CloseHandle(m_session->hIn);
    CloseHandle(m_session->hOut);
    delete m_session;
    m_session = nullptr;
    m_sudoAuthorized = false;
}

CommandResult WSLBridge::runInSession(const std::wstring& command, int timeoutMs, const std::string& stdinData) {
    if (!m_session) return {-1, L"", L"No session"};
    
    std::string mark = "__B_OUT_END__";
    std::wstring cmdW;
    std::string dataPart;

    if (stdinData.empty()) {
        cmdW = command + L" ; echo " + UTF8ToWide(mark) + L" $?\n";
    } else {
        // Wrap command to read from heredoc
        cmdW = L"base64 -d << 'EOF_B64' | (" + command + L") ; echo " + UTF8ToWide(mark) + L" $?\n";
        dataPart = WideToUTF8(Base64Encode(stdinData.data(), stdinData.size())) + "\nEOF_B64\n";
    }
    
    std::string cmdA = WideToUTF8(cmdW);
    
    DWORD written;
    WriteFile(m_session->hIn, cmdA.c_str(), (DWORD)cmdA.size(), &written, nullptr);
    if (!dataPart.empty()) {
        WriteFile(m_session->hIn, dataPart.c_str(), (DWORD)dataPart.size(), &written, nullptr);
    }
    
    std::string output;
    char buf[4096];
    DWORD bytesRead;
    ULONGLONG start = GetTickCount64();
    
    while (GetTickCount64() - start < (ULONGLONG)timeoutMs) {
        DWORD available = 0;
        if (PeekNamedPipe(m_session->hOut, nullptr, 0, nullptr, &available, nullptr) && available > 0) {
            if (ReadFile(m_session->hOut, buf, sizeof(buf)-1, &bytesRead, nullptr) && bytesRead > 0) {
                output.append(buf, bytesRead);
                size_t pos = output.find(mark);
                if (pos != std::string::npos) {
                    // Extract exit code
                    std::string tail = output.substr(pos + mark.size());
                    output.erase(pos);
                    // Trim output
                    while(!output.empty()&&(output.back()=='\r'||output.back()=='\n')) output.pop_back();
                    
                    int ec = -1;
                    std::stringstream ss(tail);
                    ss >> ec;
                    return {ec, UTF8ToWide(output), L""};
                }
            }
        } else {
            Sleep(50);
        }
    }
    
    // Leftover output/marker in the pipe would corrupt the next command — drop the session
    CloseSession();
    return {-1, UTF8ToWide(output), L"Timeout waiting for command completion"};
}

CommandResult WSLBridge::runWSLRoot(const std::wstring& command,
                                     const std::wstring& distro,
                                     int timeoutMs,
                                     const std::string& stdinData) {
    // 1. If not authorized, try to authorize
    if (!m_sudoAuthorized && !m_wslUserPassword.empty()) {
        AuthorizeSudo(distro);
    }

    // 2. Try sudo -n (non-interactive) first
    std::wstring n_cmd = L"sudo -n " + command;
    CommandResult r_n = runWSL(n_cmd, distro, L"", timeoutMs, stdinData);

    if (r_n.success()) {
        m_sudoAuthorized = true;
        return r_n;
    }

    // 3. If failed but we have password, it might be that sudo -n failed (session expired?)
    // In session, we can just use regular sudo if it's authorized.
    // If we're here and authorized but it failed, maybe it's NOT a password issue.
    
    return r_n;
}

CommandResult WSLBridge::runWSLMount(const std::vector<std::wstring>& args, int timeoutMs) {
    std::wstring cmd = L"wsl.exe";
    for (auto& a : args) cmd += L" " + a;
    return RunProcess(cmd, timeoutMs, "", true); // wsl.exe native output is UTF-16LE
}

// ── Checks ───────────────────────────────────────────────

bool WSLBridge::checkWSLInstalled(std::wstring& msg) {
    auto r = RunProcess(L"wsl.exe -l -v", 10000, "", true); // UTF-16LE
    if (!r.success()) {
        msg = L"WSL2 is not installed or not configured";
        return false;
    }
    msg = r.output;
    return true;
}

std::vector<std::pair<std::wstring, bool>> WSLBridge::getDistros() {
    std::vector<std::pair<std::wstring, bool>> distros;
    auto r = RunProcess(L"wsl.exe -l -v", 10000, "", true); // UTF-16LE
    if (r.output.empty()) return distros;

    std::wstring line;
    std::wistringstream ss(r.output);
    bool first = true;
    while (std::getline(ss, line)) {
        // Clean nulls and \r
        line.erase(std::remove(line.begin(), line.end(), L'\0'), line.end());
        while (!line.empty() && (line.back() == L'\r' || line.back() == L' '))
            line.pop_back();
        if (line.empty() || first) { first = false; continue; }

        // Format: [ *] NAME   STATE   VERSION
        bool isDefault = false;
        if (!line.empty() && line[0] == L'*') {
            isDefault = true;
            line = line.substr(1);
        }

        // Extract name: first non-space token
        auto start = line.find_first_not_of(L" \t");
        if (start == std::wstring::npos) continue;
        auto end = line.find_first_of(L" \t", start);
        std::wstring name = (end == std::wstring::npos)
            ? line.substr(start) : line.substr(start, end - start);

        if (!name.empty()) distros.push_back({name, isDefault});
    }
    return distros;
}
