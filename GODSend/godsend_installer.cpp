#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <winhttp.h>
#include <shlobj.h>
#include <winuser.h>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include <fstream>

#pragma comment(lib, "winhttp.lib")
#pragma comment(lib, "shell32.lib")
#pragma comment(lib, "advapi32.lib")
#pragma comment(lib, "user32.lib")

namespace fs = std::filesystem;

// ==========================================
// Configuration
// ==========================================
const wchar_t* REPO_URL = L"https://gitgud.io/Nesquin/godsend-homelab-edition.git";

// ==========================================
// Console Color Functions
// ==========================================
enum class Color {
    Green = FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    Yellow = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
    Cyan = FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    Red = FOREGROUND_RED | FOREGROUND_INTENSITY,
    White = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
    Gray = FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE
};

void SetConsoleColor(Color color) {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), static_cast<WORD>(color));
}

void ResetConsoleColor() {
    SetConsoleTextAttribute(GetStdHandle(STD_OUTPUT_HANDLE), 
        FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE);
}

void WriteInfo(const std::wstring& msg) {
    SetConsoleColor(Color::Cyan);
    std::wcout << L"[*] " << msg << std::endl;
    ResetConsoleColor();
}

void WriteOk(const std::wstring& msg) {
    SetConsoleColor(Color::Green);
    std::wcout << L"[+] " << msg << std::endl;
    ResetConsoleColor();
}

void WriteWarn(const std::wstring& msg) {
    SetConsoleColor(Color::Yellow);
    std::wcout << L"[!] " << msg << std::endl;
    ResetConsoleColor();
}

void WriteFail(const std::wstring& msg) {
    SetConsoleColor(Color::Red);
    std::wcout << L"[x] " << msg << std::endl;
    ResetConsoleColor();
}

// ==========================================
// Admin Check Functions
// ==========================================
bool IsRunningAsAdmin() {
    BOOL isAdmin = FALSE;
    PSID adminGroup = NULL;
    SID_IDENTIFIER_AUTHORITY ntAuthority = SECURITY_NT_AUTHORITY;

    if (AllocateAndInitializeSid(&ntAuthority, 2, SECURITY_BUILTIN_DOMAIN_RID,
        DOMAIN_ALIAS_RID_ADMINS, 0, 0, 0, 0, 0, 0, &adminGroup)) {
        CheckTokenMembership(NULL, adminGroup, &isAdmin);
        FreeSid(adminGroup);
    }
    return isAdmin == TRUE;
}

bool ElevateProcess() {
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);

    SHELLEXECUTEINFOW sei = { sizeof(sei) };
    sei.lpVerb = L"runas";
    sei.lpFile = exePath;
    sei.hwnd = NULL;
    sei.nShow = SW_NORMAL;

    if (!ShellExecuteExW(&sei)) {
        DWORD error = GetLastError();
        if (error == ERROR_CANCELLED) {
            WriteFail(L"User declined the elevation request.");
        }
        return false;
    }
    return true;
}

// ==========================================
// File Download Functions
// ==========================================
bool DownloadFile(const std::wstring& url, const std::wstring& outputPath, int retries = 3) {
    for (int attempt = 1; attempt <= retries; ++attempt) {
        try {
            WriteInfo(L"Downloading: " + fs::path(outputPath).filename().wstring());
            std::wcout << L"    From: " << url << std::endl;

            URL_COMPONENTS urlComp = { sizeof(urlComp) };
            wchar_t hostName[256], urlPath[2048];
            urlComp.lpszHostName = hostName;
            urlComp.dwHostNameLength = sizeof(hostName) / sizeof(wchar_t);
            urlComp.lpszUrlPath = urlPath;
            urlComp.dwUrlPathLength = sizeof(urlPath) / sizeof(wchar_t);

            if (!WinHttpCrackUrl(url.c_str(), 0, 0, &urlComp)) {
                throw std::runtime_error("Failed to parse URL");
            }

            HINTERNET hSession = WinHttpOpen(L"GODSend Installer/1.0",
                WINHTTP_ACCESS_TYPE_DEFAULT_PROXY, NULL, NULL, 0);
            if (!hSession) throw std::runtime_error("WinHttpOpen failed");

            HINTERNET hConnect = WinHttpConnect(hSession, urlComp.lpszHostName,
                urlComp.nPort, 0);
            if (!hConnect) {
                WinHttpCloseHandle(hSession);
                throw std::runtime_error("WinHttpConnect failed");
            }

            DWORD flags = (urlComp.nScheme == INTERNET_SCHEME_HTTPS) ? WINHTTP_FLAG_SECURE : 0;
            HINTERNET hRequest = WinHttpOpenRequest(hConnect, L"GET", urlComp.lpszUrlPath,
                NULL, WINHTTP_NO_REFERER, WINHTTP_DEFAULT_ACCEPT_TYPES, flags);
            if (!hRequest) {
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                throw std::runtime_error("WinHttpOpenRequest failed");
            }

            if (!WinHttpSendRequest(hRequest, WINHTTP_NO_ADDITIONAL_HEADERS, 0,
                WINHTTP_NO_REQUEST_DATA, 0, 0, 0)) {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                throw std::runtime_error("WinHttpSendRequest failed");
            }

            if (!WinHttpReceiveResponse(hRequest, NULL)) {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                throw std::runtime_error("WinHttpReceiveResponse failed");
            }

            fs::create_directories(fs::path(outputPath).parent_path());
            std::ofstream outFile(outputPath, std::ios::binary);
            if (!outFile) {
                WinHttpCloseHandle(hRequest);
                WinHttpCloseHandle(hConnect);
                WinHttpCloseHandle(hSession);
                throw std::runtime_error("Failed to create output file");
            }

            DWORD bytesRead = 0;
            DWORD totalBytes = 0;
            BYTE buffer[8192];

            while (WinHttpReadData(hRequest, buffer, sizeof(buffer), &bytesRead) && bytesRead > 0) {
                outFile.write(reinterpret_cast<char*>(buffer), bytesRead);
                totalBytes += bytesRead;
            }

            outFile.close();
            WinHttpCloseHandle(hRequest);
            WinHttpCloseHandle(hConnect);
            WinHttpCloseHandle(hSession);

            if (totalBytes < 100) {
                throw std::runtime_error("Downloaded file too small");
            }

            std::wcout << L"    Downloaded: " << (totalBytes / 1024.0) << L" KB" << std::endl;
            return true;

        } catch (const std::exception& e) {
            if (attempt == retries) {
                WriteFail(L"Download failed after " + std::to_wstring(retries) + L" attempts");
                return false;
            }
            WriteWarn(L"Download failed (attempt " + std::to_wstring(attempt) + L"/" +
                std::to_wstring(retries) + L"). Retrying in " +
                std::to_wstring(attempt * 2) + L" seconds...");
            Sleep(attempt * 2000);
        }
    }
    return false;
}

// ==========================================
// Process Execution Functions
// ==========================================
int ExecuteCommand(const std::wstring& command, const std::wstring& workingDir = L"") {
    STARTUPINFOW si = { sizeof(si) };
    PROCESS_INFORMATION pi = { 0 };
    si.dwFlags = STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;

    std::wstring cmdLine = command;
    std::vector<wchar_t> cmdBuffer(cmdLine.begin(), cmdLine.end());
    cmdBuffer.push_back(L'\0');

    const wchar_t* workDir = workingDir.empty() ? NULL : workingDir.c_str();

    if (!CreateProcessW(NULL, cmdBuffer.data(), NULL, NULL, FALSE,
        CREATE_NO_WINDOW, NULL, workDir, &si, &pi)) {
        return -1;
    }

    WaitForSingleObject(pi.hProcess, INFINITE);

    DWORD exitCode;
    GetExitCodeProcess(pi.hProcess, &exitCode);

    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);

    return exitCode;
}

bool TestGitInstalled() {
    return ExecuteCommand(L"git --version") == 0;
}

std::wstring FindGitDirectory() {
    std::vector<std::wstring> searchPaths = {
        L"C:\\Program Files\\Git\\cmd",
        L"C:\\Program Files\\Git\\bin",
        L"C:\\Program Files (x86)\\Git\\cmd",
        L"C:\\Program Files (x86)\\Git\\bin"
    };
    
    wchar_t* username = _wgetenv(L"USERNAME");
    if (username) {
        searchPaths.push_back(L"C:\\Users\\" + std::wstring(username) + L"\\AppData\\Local\\Programs\\Git\\cmd");
    }
    
    for (const auto& path : searchPaths) {
        fs::path gitExe = fs::path(path) / L"git.exe";
        if (fs::exists(gitExe)) {
            return path;
        }
    }
    
    return L"";
}

bool UpdateSystemPathAndReload(const std::wstring& gitDir) {
    HKEY hKey;
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE,
        L"SYSTEM\\CurrentControlSet\\Control\\Session Manager\\Environment",
        0, KEY_READ | KEY_SET_VALUE, &hKey) != ERROR_SUCCESS) {
        return false;
    }
    
    wchar_t currentPath[32767];
    DWORD pathSize = sizeof(currentPath);
    DWORD type;
    
    if (RegQueryValueExW(hKey, L"Path", NULL, &type, (LPBYTE)currentPath, &pathSize) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }
    
    std::wstring pathStr(currentPath);
    
    // Check if already in PATH
    if (pathStr.find(gitDir) != std::wstring::npos) {
        RegCloseKey(hKey);
        // Still need to update current process
        SetEnvironmentVariableW(L"PATH", pathStr.c_str());
        return true;
    }
    
    // Add to PATH
    std::wstring newPath = pathStr + L";" + gitDir;
    
    if (RegSetValueExW(hKey, L"Path", 0, REG_EXPAND_SZ,
        (BYTE*)newPath.c_str(), (newPath.length() + 1) * sizeof(wchar_t)) != ERROR_SUCCESS) {
        RegCloseKey(hKey);
        return false;
    }
    
    RegCloseKey(hKey);
    
    // Broadcast environment change
    SendMessageTimeoutW(HWND_BROADCAST, WM_SETTINGCHANGE, 0,
        reinterpret_cast<LPARAM>(L"Environment"), SMTO_ABORTIFHUNG, 5000, nullptr);
    
    // Update current process PATH
    SetEnvironmentVariableW(L"PATH", newPath.c_str());
    
    return true;
}

// ==========================================
// Main Installation Logic
// ==========================================
int wmain(int argc, wchar_t* argv[]) {
    SetConsoleOutputCP(CP_UTF8);
    
    bool forceClean = false;
    
    for (int i = 1; i < argc; i++) {
        if (std::wstring(argv[i]) == L"-ForceClean") {
            forceClean = true;
        }
    }

    if (!IsRunningAsAdmin()) {
        WriteWarn(L"This installer requires Administrator privileges.");
        WriteInfo(L"Requesting elevation...");
        if (ElevateProcess()) {
            return 0;
        } else {
            WriteFail(L"Failed to elevate privileges.");
            std::wcout << L"Press any key to exit..." << std::endl;
            _getwch();
            return 1;
        }
    }

    WriteOk(L"Running with Administrator privileges.");

    std::wcout << std::endl;
    SetConsoleColor(Color::Cyan);
    std::wcout << L"========================================" << std::endl;
    std::wcout << L"  GODSend Installation Script" << std::endl;
    SetConsoleColor(Color::Green);
    std::wcout << L"  Running as Administrator" << std::endl;
    SetConsoleColor(Color::Cyan);
    std::wcout << L"========================================" << std::endl;
    ResetConsoleColor();
    std::wcout << std::endl;

    // ==========================================
    // GIT INSTALLATION CHECK & AUTO-INSTALL
    // ==========================================
    
    if (!TestGitInstalled()) {
        WriteFail(L"Git is not installed or not in PATH.");
        std::wcout << std::endl;
        
        WriteInfo(L"Checking for Windows Package Manager (winget)...");
        int wingetResult = ExecuteCommand(L"winget --version");
        
        if (wingetResult == 0) {
            WriteOk(L"Winget is available. Installing Git automatically...");
            std::wcout << std::endl;
            
            WriteInfo(L"This may take 1-2 minutes. Please wait...");
            std::wstring wingetCmd = L"winget install --id Git.Git --source winget "
                L"--accept-package-agreements --accept-source-agreements --silent";
            
            int installResult = ExecuteCommand(wingetCmd);
            
            if (installResult == 0 || installResult == -1978335189 || installResult == static_cast<int>(0x8A15002B)) {
                WriteOk(L"Git installation completed.");
                std::wcout << std::endl;
                
                WriteInfo(L"Locating Git installation...");
                Sleep(3000); // Wait for files to be written
                
                std::wstring gitDir = FindGitDirectory();
                
                if (!gitDir.empty()) {
                    WriteOk(L"Found Git at: " + gitDir);
                    WriteInfo(L"Adding Git to system PATH...");
                    
                    if (UpdateSystemPathAndReload(gitDir)) {
                        WriteOk(L"PATH updated successfully.");
                        std::wcout << std::endl;
                        
                        WriteInfo(L"Verifying Git is accessible...");
                        Sleep(1000);
                        
                        if (TestGitInstalled()) {
                            WriteOk(L"Git is now working via PATH! Continuing installation...");
                            std::wcout << std::endl;
                        } else {
                            WriteWarn(L"Git PATH updated but not yet active in this session.");
                            WriteWarn(L"This is a Windows limitation. Please close and rerun this installer.");
                            std::wcout << std::endl << L"Press any key to exit..." << std::endl;
                            _getwch();
                            return 0;
                        }
                    } else {
                        WriteFail(L"Failed to update system PATH.");
                        WriteWarn(L"Please add Git to PATH manually and rerun installer.");
                        std::wcout << std::endl << L"Press any key to exit..." << std::endl;
                        _getwch();
                        return 1;
                    }
                } else {
                    WriteFail(L"Git installed but could not locate installation directory.");
                    WriteWarn(L"Please reboot and run this installer again.");
                    std::wcout << std::endl << L"Press any key to exit..." << std::endl;
                    _getwch();
                    return 1;
                }
            } else {
                WriteFail(L"Failed to install Git (exit code: " + std::to_wstring(installResult) + L")");
                WriteWarn(L"Please install Git manually: https://git-scm.com/download/win");
                std::wcout << std::endl << L"Press any key to exit..." << std::endl;
                _getwch();
                return 1;
            }
        } else {
            WriteFail(L"Windows Package Manager (winget) is not available.");
            std::wcout << std::endl;
            WriteWarn(L"To enable automatic Git installation:");
            WriteWarn(L"  1. Install 'App Installer' from Microsoft Store (includes winget)");
            WriteWarn(L"  OR");
            WriteWarn(L"  2. Install Git manually from: https://git-scm.com/download/win");
            std::wcout << std::endl << L"Press any key to exit..." << std::endl;
            _getwch();
            return 1;
        }
    } else {
        WriteOk(L"Git is installed and ready.");
    }

    // Determine installation directory
    wchar_t exePath[MAX_PATH];
    GetModuleFileNameW(NULL, exePath, MAX_PATH);
    fs::path scriptDir = fs::path(exePath).parent_path();
    fs::path installDir = scriptDir / L"godsend";

    WriteInfo(L"Installation directory: " + installDir.wstring());

    if (forceClean && fs::exists(installDir)) {
        WriteWarn(L"ForceClean enabled: removing existing installation...");
        try {
            fs::remove_all(installDir);
            WriteOk(L"Previous installation removed.");
        } catch (const std::exception& e) {
            WriteFail(L"Could not remove existing directory.");
            std::wcout << L"Press any key to exit..." << std::endl;
            _getwch();
            return 1;
        }
    }

    fs::path readyDir = installDir / L"Ready";
    fs::path tempDir = installDir / L"Temp";
    fs::path xboxDir = installDir / L"MOVE_THESE_FILES_TO_XBOX";
    fs::path repoCloneDir = tempDir / L"repo-clone";

    fs::create_directories(installDir);
    fs::create_directories(readyDir);
    fs::create_directories(tempDir);
    fs::create_directories(xboxDir);

    WriteOk(L"Directory structure created.");
    std::wcout << std::endl;

    std::vector<std::wstring> installErrors;

    // Clone Repository
    SetConsoleColor(Color::Yellow);
    std::wcout << L"[0/4] Cloning GODSend Repository" << std::endl;
    SetConsoleColor(Color::Gray);
    std::wcout << L"----------------------------------------" << std::endl;
    ResetConsoleColor();

    WriteInfo(L"Cloning repository...");
    try {
        if (fs::exists(repoCloneDir)) {
            fs::remove_all(repoCloneDir);
        }

        std::wstring gitCmd = L"git clone --depth 1 " + std::wstring(REPO_URL) + L" \"" + repoCloneDir.wstring() + L"\"";
        int result = ExecuteCommand(gitCmd);
        if (result != 0) {
            throw std::runtime_error("Git clone failed");
        }
        WriteOk(L"Repository cloned successfully.");
    } catch (const std::exception&) {
        WriteFail(L"Failed to clone repository.");
        installErrors.push_back(L"Repository Clone");
    }

    std::wcout << std::endl;

    // 1) 7-Zip Installation
    SetConsoleColor(Color::Yellow);
    std::wcout << L"[1/4] 7-Zip Installation" << std::endl;
    SetConsoleColor(Color::Gray);
    std::wcout << L"----------------------------------------" << std::endl;
    ResetConsoleColor();

    fs::path sevenZipExe = installDir / L"7za.exe";

    if (fs::exists(sevenZipExe)) {
        WriteOk(L"7-Zip already installed (skipping).");
    } else {
        try {
            WriteInfo(L"Downloading 7-Zip Extra package...");

            fs::path sevenZipArchive = tempDir / L"7z1900-extra.7z";
            fs::path sevenZrExe = tempDir / L"7zr.exe";

            if (!DownloadFile(L"https://7-zip.org/a/7z1900-extra.7z", sevenZipArchive.wstring())) {
                throw std::runtime_error("Download failed");
            }

            WriteInfo(L"Downloading 7zr.exe to extract the package...");
            if (!DownloadFile(L"https://www.7-zip.org/a/7zr.exe", sevenZrExe.wstring())) {
                throw std::runtime_error("Download failed");
            }

            WriteInfo(L"Extracting 7-Zip files to install directory...");
            std::wstring extractCmd = L"\"" + sevenZrExe.wstring() + L"\" e \"" +
                sevenZipArchive.wstring() + L"\" -o\"" + installDir.wstring() + L"\" x64/*.* -y";
            
            int result = ExecuteCommand(extractCmd);
            if (result != 0) {
                throw std::runtime_error("Extraction failed");
            }

            if (!fs::exists(sevenZipExe)) {
                throw std::runtime_error("7za.exe not found after extraction");
            }

            WriteOk(L"7-Zip installed successfully.");
        } catch (const std::exception&) {
            WriteFail(L"7-Zip installation failed.");
            installErrors.push_back(L"7-Zip");
        }
    }

    std::wcout << std::endl;

    // 2) iso2god-rs Installation
    SetConsoleColor(Color::Yellow);
    std::wcout << L"[2/4] iso2god-rs Installation" << std::endl;
    SetConsoleColor(Color::Gray);
    std::wcout << L"----------------------------------------" << std::endl;
    ResetConsoleColor();

    fs::path iso2godPath = installDir / L"iso2god.exe";

    if (fs::exists(iso2godPath)) {
        WriteOk(L"iso2god-rs already installed (skipping).");
    } else {
        try {
            WriteInfo(L"Downloading iso2god-rs v1.8.1...");
            if (!DownloadFile(L"https://github.com/iliazeus/iso2god-rs/releases/download/v1.8.1/iso2god-x86_64-windows.exe",
                iso2godPath.wstring())) {
                throw std::runtime_error("Download failed");
            }
            WriteOk(L"iso2god-rs installed successfully.");
        } catch (const std::exception&) {
            WriteFail(L"iso2god-rs installation failed.");
            installErrors.push_back(L"iso2god-rs");
        }
    }

    std::wcout << std::endl;

    // 3) GODSend Backend Installation
    SetConsoleColor(Color::Yellow);
    std::wcout << L"[3/4] GODSend Backend Installation" << std::endl;
    SetConsoleColor(Color::Gray);
    std::wcout << L"----------------------------------------" << std::endl;
    ResetConsoleColor();

    fs::path godsendExe = installDir / L"godsend.exe";

    if (fs::exists(godsendExe)) {
        WriteOk(L"GODSend backend already installed (skipping).");
    } else {
        try {
            WriteInfo(L"Installing GODSend backend from repository...");

            fs::path godsendSrc = repoCloneDir / L"source-control" / L"godsend_windows.exe";

            if (fs::exists(godsendSrc)) {
                fs::copy_file(godsendSrc, godsendExe);
                WriteOk(L"GODSend backend installed successfully.");
            } else {
                throw std::runtime_error("GODSend binary not found");
            }
        } catch (const std::exception&) {
            WriteFail(L"GODSend backend installation failed.");
            installErrors.push_back(L"GODSend Backend");
        }
    }

    std::wcout << std::endl;

    // 4) Xbox Client Files Installation
    SetConsoleColor(Color::Yellow);
    std::wcout << L"[4/4] Xbox Client Files Installation" << std::endl;
    SetConsoleColor(Color::Gray);
    std::wcout << L"----------------------------------------" << std::endl;
    ResetConsoleColor();

    fs::path clientScriptsDir = repoCloneDir / L"client-scripts";

    if (fs::exists(clientScriptsDir)) {
        WriteInfo(L"Copying Xbox client files...");
        try {
            fs::copy(clientScriptsDir, xboxDir, 
                fs::copy_options::recursive | fs::copy_options::overwrite_existing);
            WriteOk(L"Xbox client files installed successfully.");
        } catch (const std::exception&) {
            WriteFail(L"Failed to copy Xbox client files.");
            installErrors.push_back(L"Xbox Client Files");
        }
    } else {
        WriteFail(L"client-scripts directory not found.");
        installErrors.push_back(L"Xbox Client Files");
    }

    std::wcout << std::endl;

    // Cleanup
    WriteInfo(L"Cleaning up...");
    if (fs::exists(repoCloneDir)) {
        fs::remove_all(repoCloneDir);
    }
    
    for (const auto& entry : fs::directory_iterator(tempDir)) {
        if (entry.is_regular_file()) {
            fs::remove(entry.path());
        }
    }

    WriteOk(L"Cleanup complete. Temp folder preserved for application use.");

    // Installation Summary
    std::wcout << std::endl;
    SetConsoleColor(Color::Cyan);
    std::wcout << L"========================================" << std::endl;
    std::wcout << L"  Installation Summary" << std::endl;
    std::wcout << L"========================================" << std::endl;
    ResetConsoleColor();
    std::wcout << std::endl;

    if (installErrors.empty()) {
        SetConsoleColor(Color::Green);
        std::wcout << L"[+] All components installed successfully!" << std::endl;
        ResetConsoleColor();
    } else {
        SetConsoleColor(Color::Yellow);
        std::wcout << L"[!] Installation completed with warnings:" << std::endl;
        for (const auto& err : installErrors) {
            std::wcout << L"    - " << err << std::endl;
        }
        ResetConsoleColor();
    }

    std::wcout << std::endl;
    SetConsoleColor(Color::Cyan);
    std::wcout << L"Installation Location:" << std::endl;
    SetConsoleColor(Color::White);
    std::wcout << L"  " << installDir.wstring() << std::endl;
    ResetConsoleColor();
    std::wcout << std::endl;

    SetConsoleColor(Color::Cyan);
    std::wcout << L"Installed Components:" << std::endl;
    ResetConsoleColor();
    if (fs::exists(sevenZipExe)) {
        SetConsoleColor(Color::Green);
        std::wcout << L"  [+] 7-Zip" << std::endl;
        ResetConsoleColor();
    }
    if (fs::exists(iso2godPath)) {
        SetConsoleColor(Color::Green);
        std::wcout << L"  [+] iso2god-rs" << std::endl;
        ResetConsoleColor();
    }
    if (fs::exists(godsendExe)) {
        SetConsoleColor(Color::Green);
        std::wcout << L"  [+] GODSend Backend" << std::endl;
        ResetConsoleColor();
    }
    if (fs::exists(xboxDir) && !fs::is_empty(xboxDir)) {
        SetConsoleColor(Color::Green);
        std::wcout << L"  [+] Xbox Client Files" << std::endl;
        ResetConsoleColor();
    }

    std::wcout << std::endl;
    SetConsoleColor(Color::Cyan);
    std::wcout << L"========================================" << std::endl;
    std::wcout << L"  Next Steps" << std::endl;
    std::wcout << L"========================================" << std::endl;
    ResetConsoleColor();
    std::wcout << std::endl;

    if (fs::exists(godsendExe)) {
        SetConsoleColor(Color::Yellow);
        std::wcout << L"1. Start the GODSend server:" << std::endl;
        SetConsoleColor(Color::White);
        std::wcout << L"   cd \"" << installDir.wstring() << L"\"" << std::endl;
        std::wcout << L"   .\\godsend.exe" << std::endl;
        ResetConsoleColor();
        std::wcout << std::endl;
    }

    if (fs::exists(xboxDir) && !fs::is_empty(xboxDir)) {
        SetConsoleColor(Color::Yellow);
        std::wcout << L"2. Copy Xbox client files to your Xbox:" << std::endl;
        SetConsoleColor(Color::White);
        std::wcout << L"   From: \"" << xboxDir.wstring() << L"\"" << std::endl;
        std::wcout << L"   To: Your Aurora/Scripts folder on Xbox" << std::endl;
        ResetConsoleColor();
        std::wcout << std::endl;
    }

    SetConsoleColor(Color::Cyan);
    std::wcout << L"Tips:" << std::endl;
    SetConsoleColor(Color::Gray);
    std::wcout << L"  - Re-run with -ForceClean to completely reinstall" << std::endl;
    std::wcout << L"  - Check the 'Ready' folder for converted games" << std::endl;
    ResetConsoleColor();
    std::wcout << std::endl;

    std::wcout << L"Press any key to exit..." << std::endl;
    _getwch();

    return 0;
}

//Credits to Nesquin and the team for this software
