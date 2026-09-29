#include <algorithm>
#include <cmath>
#include <chrono>
#include <windows.h>
#include <commctrl.h>
#include <commdlg.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shellapi.h>
#include <shlobj.h>
#include <tlhelp32.h>
#include <regex>
#include <filesystem>
#include <fstream>
#include <string>
#include "LauncherSettings.h"
#include "BuildFeatures.h"
#include "RenderResolution.h"
#include "DesktopMirrorResolution.h"
#include "hands/HandWeaponProfile.h"
#include "hands/CalibrationDraft.h"
#include <vector>
int runArgentVrIntro(const wchar_t* loaderPath);

namespace {
using argent::cleanRelease;

void configureStereoDlss(std::map<std::wstring,std::wstring>& env,int mode){
    // DLSS can be toggled in-game. Every stereo runtime needs both histories,
    // including SteamVR; override stale inherited startup-workaround values.
    env[L"ARGENT_DLSS_STEREO"]=mode==1?L"1":L"0";
}

std::filesystem::path configureShaderCapture(std::map<std::wstring,std::wstring>& env,
    bool enabled,const std::filesystem::path& package,const std::wstring& session){
    // Explicitly mask an inherited capture directory when the checkbox is off.
    env[L"ARGENT_CAPTURE_DIRECTORY"]=L"";
    env[L"ARGENT_WATER_INPUT_CAPTURE"]=enabled?L"1":L"0";
    env[L"ARGENT_WATER_GPU_CAPTURE"]=enabled?L"1":L"0";
    env[L"ARGENT_DECAL_GPU_CAPTURE"]=L"0";
    if(!enabled)return {};
    const auto parent=package/L"captures";
    std::filesystem::create_directories(parent);
    const auto capture=parent/session;
    if(!std::filesystem::create_directory(capture))throw std::runtime_error("Capture session already exists");
    std::ofstream metadata(capture/L"capture-session.txt",std::ios::binary);
    metadata<<"Native launcher shader capture\nOriginal SPIR-V: spirv/\nPipeline identities: graphics.tsv and compute.tsv\n";
    metadata.close();
    if(!metadata)throw std::runtime_error("Cannot write capture metadata");
    if(std::filesystem::exists(package/L"build-info.json"))
        std::filesystem::copy_file(package/L"build-info.json",capture/L"build-info.json");
    env[L"ARGENT_CAPTURE_DIRECTORY"]=capture.wstring();
    return capture;
}

enum : int {
    IdLaunch = 100, IdDiagnose, IdLogs, IdBrowse, IdTabs, IdGamePath,
    IdRenderer, IdCaptureShaders, IdSimulator, IdTurnMode, IdSnapAngle,
    IdMovement, IdDominantHand, IdLeftHandSwap, IdShowIntro, IdTwoHand, IdDisableAa, IdRenderScale,
    IdFsr, IdSmoothSpeed, IdPhysicalKill, IdKillSpeed, IdKillHands, IdAutomatic, IdSync, IdTraversal, IdShoulder,
    IdHandsJump, IdVirtualGunstock, IdBhaptics, IdPsvr2Triggers,
    IdExtendedLogging, IdShowHands, IdCalibrationMode, IdCalibrationProfile, IdApplyCalibration, IdCalibrationFiles, IdPivotForward, IdPivotLeft, IdPivotUp,
    IdCinematics3d, IdGpuDiagnostics, IdDisableVrIntro, IdRenderingLogging, IdCredit, IdDesktopMirror, IdMirrorResolution, IdTabBase = 200
};

HWND tabButtons[7]{}, gameEdit{}, statusText{}, launchButton{};
HWND renderScale{}, disableAa{}, captureShaders{}, simulator{}, turnMode{}, snapAngle{}, movement{}, dominantHand{},leftHandSwap{},leftHandLayoutLabel{};
HWND showIntro{}, twoHand{}, virtualGunstock{}, bhaptics{}, psvr2Triggers{}, tabPages[7]{};
HWND fsr{},smoothSpeed{},physicalKill{},killSpeed{},killHands{};
HWND automatic{},syncImmersive{},traversal{},shoulder{};
HWND extendedLogging{},showHands{},handsJump{},calibrationMode{},calibrationProfile{},weaponPivot[3]{};
argent::calibration::ApplyCommand calibrationApplyCommand;bool calibrationApplyWaiting{};
HWND cinematics3d{},gpuDiagnostics{};
HWND disableVrIntro{}, renderingLogging{}, desktopMirror{}, mirrorResolution{};
bool advancedTabs{};
bool vrIntroStarted{},launchInProgress{},introCancelled{};
DWORD introThreadId{};
std::vector<std::string> calibrationProfiles(kharvox::hands::handProfileKeys.begin(),kharvox::hands::handProfileKeys.end());
const char* calibrationModes[]{"off","hands","weapon","support","hud"};
HANDLE worker{};
HFONT uiFont{}, titleFont{};
HBRUSH backgroundBrush{}, panelBrush{}, fieldBrush{};
Gdiplus::Image* logo{};
ULONG_PTR gdiplusToken{};
std::filesystem::path root;
int activeTab{};
std::map<HWND,bool> checks;

constexpr COLORREF Background = RGB(0, 0, 0);
constexpr COLORREF Panel = RGB(0, 0, 0);
constexpr COLORREF Field = RGB(0, 0, 0);
constexpr COLORREF Border = RGB(62, 62, 66);
constexpr COLORREF Accent = RGB(255, 61, 12);
constexpr COLORREF Text = RGB(245, 245, 245);
constexpr COLORREF Muted = RGB(155, 155, 160);

std::wstring quote(const std::wstring& text) {
    std::wstring out = L"\"";
    size_t slashes = 0;
    for (wchar_t c : text) {
        if (c == L'\\') { ++slashes; continue; }
        if (c == L'"') { out.append(slashes * 2 + 1, L'\\'); out += c; }
        else { out.append(slashes, L'\\'); out += c; }
        slashes = 0;
    }
    out.append(slashes * 2, L'\\');
    return out + L"\"";
}

int selected(HWND hwnd) { return static_cast<int>(SendMessageW(hwnd, CB_GETCURSEL, 0, 0)); }
bool isChecked(HWND hwnd) { auto i=checks.find(hwnd);return i!=checks.end()&&i->second; }
void setChecked(HWND hwnd,bool value){
    checks[hwnd]=value;InvalidateRect(hwnd,nullptr,TRUE);
    if(hwnd&&(hwnd==extendedLogging||hwnd==renderingLogging)){
        HWND other=hwnd==extendedLogging?renderingLogging:extendedLogging;
        if(other){checks[other]=value;InvalidateRect(other,nullptr,TRUE);}
    }

}
void setStatus(const wchar_t* text) { SetWindowTextW(statusText, text); }
std::wstring lastLauncherError() {
    std::wifstream file(root / L"logs/launcher-output.txt");
    if (!file) return {};
    std::wstring line, last;
    while (std::getline(file, line)) {
        if (!line.empty() && line.back() == L'\r') line.pop_back();
        if (!line.empty()) last = line;
    }
    if (last.size() > 180) last = last.substr(0, 177) + L"...";
    return last;
}
std::wstring timestampName() {
    SYSTEMTIME t{};
    GetSystemTime(&t);
    wchar_t text[64]{};
    swprintf_s(text, L"%04u%02u%02u-%02u%02u%02u-%03u",
        t.wYear, t.wMonth, t.wDay, t.wHour, t.wMinute, t.wSecond, t.wMilliseconds);
    return text;
}
std::wstring processOutput(const std::filesystem::path& exe, const std::map<std::wstring,std::wstring>& env) {
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    HANDLE read{}, write{};
    if (!CreatePipe(&read, &write, &sa, 0)) return L"";
    SetHandleInformation(read, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    si.dwFlags = STARTF_USESTDHANDLES;
    si.hStdOutput = write;
    si.hStdError = write;
    si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
    std::wstring cmd = quote(exe.wstring());
    auto block = GetEnvironmentStringsW();
    std::wstring environment;
    if (block) {
        for (auto p = block; *p; p += wcslen(p) + 1) {
            std::wstring row(p);
            auto eq = row.find(L'=');
            auto key = eq == std::wstring::npos ? row : row.substr(0, eq);
            bool replaced = false;
            for (const auto& entry : env)
                if (_wcsicmp(key.c_str(), entry.first.c_str()) == 0) { replaced = true; break; }
            if (!replaced) { environment += row; environment.push_back(L'\0'); }
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& entry : env) {
        environment += entry.first + L"=" + entry.second;
        environment.push_back(L'\0');
    }
    environment.push_back(L'\0');
    PROCESS_INFORMATION pi{};
    BOOL ok = CreateProcessW(exe.c_str(), cmd.data(), nullptr, nullptr, TRUE,
        CREATE_NO_WINDOW | CREATE_UNICODE_ENVIRONMENT,
        environment.data(), root.c_str(), &si, &pi);
    CloseHandle(write);
    std::wstring output;
    if (ok) {
        char buffer[1024];
        DWORD readBytes{};
        while (ReadFile(read, buffer, sizeof(buffer), &readBytes, nullptr) && readBytes)
            output.append(buffer, buffer + readBytes);
        WaitForSingleObject(pi.hProcess, 15000);
        CloseHandle(pi.hThread);
        CloseHandle(pi.hProcess);
    } else {
        output = L"PROBE_START_FAILED win32=" + std::to_wstring(GetLastError()) + L"\n";
    }
    CloseHandle(read);
    return output;
}
bool containsLine(const std::wstring& text, const wchar_t* prefix) {
    return text.find(prefix) != std::wstring::npos;
}
int maxEyeValue(const std::wstring& text, bool width) {
    std::wregex pattern(L"XR_EYE[01]_RECOMMENDED=(\\d+)x(\\d+)");
    int result = 0;
    for (std::wsregex_iterator i(text.begin(), text.end(), pattern), end; i != end; ++i)
        result = std::max(result, _wtoi((*i)[width ? 1 : 2].str().c_str()));
    return result;
}
std::wstring hexToken() {
    GUID guid{};
    CoCreateGuid(&guid);
    wchar_t text[40]{};
    StringFromGUID2(guid, text, 40);
    std::wstring token(text);
    token.erase(std::remove_if(token.begin(), token.end(), [](wchar_t c){return c==L'{'||c==L'}'||c==L'-';}), token.end());
    return token;
}
bool isolatedSettings{};
bool settingsReady{};
std::filesystem::path isolatedSettingsRoot;
std::filesystem::path launcherSettingsPath(){
    if(isolatedSettings)return isolatedSettingsRoot/L"local-settings/launcher.ini";
    wchar_t folder[MAX_PATH]{};
    if(FAILED(SHGetFolderPathW(nullptr,CSIDL_LOCAL_APPDATA,nullptr,SHGFP_TYPE_CURRENT,folder)))
        throw std::runtime_error("Cannot locate local application data");
    return std::filesystem::path(folder)/L"KHARVOX ARGENT/launcher.ini";
}
bool validGameExecutable(const std::filesystem::path& path){
    std::error_code ec;
    return _wcsicmp(path.filename().c_str(),L"DOOMEternalx64vk.exe")==0&&std::filesystem::is_regular_file(path,ec);
}
bool microsoftStoreGame(const std::filesystem::path& game){
    std::ifstream config(game.parent_path()/L"MicrosoftGame.Config");
    if(!config)return false;
    const std::string text((std::istreambuf_iterator<char>(config)),{});
    return text.find("BethesdaSoftworks.DOOMEternal-PC")!=std::string::npos;
}
bool processElevated(HANDLE process, bool& known) {
    known = false;
    HANDLE token{};
    if (!OpenProcessToken(process, TOKEN_QUERY, &token)) return false;
    TOKEN_ELEVATION elevation{};
    DWORD bytes{};
    const bool ok = GetTokenInformation(token, TokenElevation, &elevation, sizeof(elevation), &bytes);
    CloseHandle(token);
    known = ok;
    return ok && elevation.TokenIsElevated != 0;
}
bool currentProcessElevated(bool& known) {
    return processElevated(GetCurrentProcess(), known);
}
bool runningSteamElevated(bool& known) {
    known = true;
    HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) { known = false; return false; }
    PROCESSENTRY32W entry{sizeof(entry)};
    bool elevated = false;
    if (Process32FirstW(snapshot, &entry)) do {
        if (_wcsicmp(entry.szExeFile, L"steam.exe") != 0) continue;
        HANDLE process = OpenProcess(PROCESS_QUERY_LIMITED_INFORMATION, FALSE, entry.th32ProcessID);
        if (!process) { known = false; continue; }
        bool oneKnown{};
        elevated |= processElevated(process, oneKnown);
        known &= oneKnown;
        CloseHandle(process);
    } while (Process32NextW(snapshot, &entry));
    CloseHandle(snapshot);
    return elevated;
}
bool doomRunAsAdmin(const std::wstring& gameExe, std::wstring& location, bool& known) {
    known = true;
    constexpr const wchar_t* subkey = L"Software\\Microsoft\\Windows NT\\CurrentVersion\\AppCompatFlags\\Layers";
    for (auto hive : {HKEY_CURRENT_USER, HKEY_LOCAL_MACHINE}) for (auto view : {KEY_WOW64_64KEY, KEY_WOW64_32KEY}) {
        HKEY key{};
        if (RegOpenKeyExW(hive, subkey, 0, KEY_QUERY_VALUE | view, &key) != ERROR_SUCCESS) continue;
        DWORD type{}, bytes{};
        LONG sizeResult = RegQueryValueExW(key, gameExe.c_str(), nullptr, &type, nullptr, &bytes);
        if (sizeResult == ERROR_ACCESS_DENIED) known = false;
        if (sizeResult == ERROR_SUCCESS && (type == REG_SZ || type == REG_EXPAND_SZ)) {
            std::wstring value(bytes / sizeof(wchar_t) + 1, L'\0');
            if (RegQueryValueExW(key, gameExe.c_str(), nullptr, &type, reinterpret_cast<BYTE*>(value.data()), &bytes) == ERROR_SUCCESS) {
                value.resize(wcslen(value.c_str()));
                auto upper = value;
                std::transform(upper.begin(), upper.end(), upper.begin(), towupper);
                if (upper.find(L"RUNASADMIN") != std::wstring::npos) {
                    location = (hive == HKEY_CURRENT_USER ? L"HKCU " : L"HKLM ") + std::wstring(view == KEY_WOW64_64KEY ? L"Registry64\\" : L"Registry32\\") + subkey;
                    RegCloseKey(key);
                    return true;
                }
            }
        }
        RegCloseKey(key);
    }
    return false;
}
std::filesystem::path findMicrosoftStoreGame(const std::filesystem::path& xboxGames){
    std::error_code ec;
    for(std::filesystem::directory_iterator it(xboxGames,ec),end;!ec&&it!=end;it.increment(ec)){
        if(!it->is_directory(ec))continue;
        const auto game=it->path()/L"Content/DOOMEternalx64vk.exe";
        if(validGameExecutable(game)&&microsoftStoreGame(game))return game;
    }
    return {};
}
std::wstring findGameExecutable() {
    std::vector<std::filesystem::path> libraries;
    for(auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(auto view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}){
        HKEY key{};
        if(RegOpenKeyExW(hive,L"SOFTWARE\\Valve\\Steam",0,KEY_READ|view,&key)!=ERROR_SUCCESS)continue;
        for(auto name:{L"SteamPath",L"InstallPath",L"SteamExe"}){
            wchar_t text[32768]{};DWORD size=sizeof(text),type{};
            if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(text),&size)==ERROR_SUCCESS&&type==REG_SZ&&text[0]){
                std::filesystem::path path(text);
                libraries.push_back(std::wstring(name)==L"SteamExe"?path.parent_path():path);
            }
        }
        RegCloseKey(key);
    }
    for(auto id:{CSIDL_PROGRAM_FILESX86,CSIDL_PROGRAM_FILES}){
        wchar_t folder[MAX_PATH]{};
        if(SUCCEEDED(SHGetFolderPathW(nullptr,id,nullptr,SHGFP_TYPE_CURRENT,folder)))libraries.push_back(std::filesystem::path(folder)/L"Steam");
    }
    auto roots=libraries;
    const std::regex pathEntry(R"vdf("path"\s+"([^"]+)")vdf");
    for(const auto& steam:roots){
        std::ifstream file(steam/L"steamapps/libraryfolders.vdf");
        std::string contents((std::istreambuf_iterator<char>(file)),{});
        for(std::sregex_iterator i(contents.begin(),contents.end(),pathEntry),end;i!=end;++i){
            auto value=(*i)[1].str();
            for(size_t at=0;(at=value.find("\\\\",at))!=std::string::npos;++at)value.replace(at,2,"\\");
            libraries.push_back(std::filesystem::u8path(value));
        }
    }
    std::vector<std::filesystem::path> candidates{root/L"DOOMEternalx64vk.exe",root.parent_path()/L"DOOMEternalx64vk.exe"};
    for(const auto& library:libraries)candidates.push_back(library/L"steamapps/common/DOOMEternal/DOOMEternalx64vk.exe");
    wchar_t drives[512]{};
    const DWORD driveChars=GetLogicalDriveStringsW(DWORD(std::size(drives)),drives);
    if(driveChars&&driveChars<std::size(drives))for(const wchar_t* drive=drives;*drive;drive+=wcslen(drive)+1){
        const auto xbox=findMicrosoftStoreGame(std::filesystem::path(drive)/L"XboxGames");
        if(!xbox.empty())candidates.push_back(xbox);
    }
    for(const auto& candidate:candidates)if(validGameExecutable(candidate))return candidate.wstring();
    return {};
}
std::wstring activeOpenXRManifestW() {
    DWORD size = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", nullptr, 0);
    if (size > 1) {
        std::wstring value(size, L'\0');
        DWORD copied = GetEnvironmentVariableW(L"XR_RUNTIME_JSON", value.data(), size);
        if (copied > 0 && copied < size) { value.resize(copied); return value; }
    }
    HKEY key{};
    if (RegOpenKeyExW(HKEY_LOCAL_MACHINE, L"SOFTWARE\\Khronos\\OpenXR\\1", 0,
        KEY_QUERY_VALUE | KEY_WOW64_64KEY, &key) != ERROR_SUCCESS) return {};
    DWORD type{}, bytes{};
    if (RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type, nullptr, &bytes) != ERROR_SUCCESS
        || (type != REG_SZ && type != REG_EXPAND_SZ)) { RegCloseKey(key); return {}; }
    std::wstring value(bytes / sizeof(wchar_t) + 1, L'\0');
    LONG result = RegQueryValueExW(key, L"ActiveRuntime", nullptr, &type,
        reinterpret_cast<BYTE*>(value.data()), &bytes);
    RegCloseKey(key);
    if (result != ERROR_SUCCESS) return {};
    value.resize(wcslen(value.c_str()));
    if (type == REG_EXPAND_SZ) {
        DWORD expandedSize = ExpandEnvironmentStringsW(value.c_str(), nullptr, 0);
        if (expandedSize > 1) {
            std::wstring expanded(expandedSize, L'\0');
            if (ExpandEnvironmentStringsW(value.c_str(), expanded.data(), expandedSize)) {
                expanded.resize(expandedSize - 1);
                value = std::move(expanded);
            }
        }
    }
    return value;
}
bool usesSteamVrRuntime() {
    auto manifest = activeOpenXRManifestW();
    std::transform(manifest.begin(), manifest.end(), manifest.begin(),
        [](wchar_t c) { return static_cast<wchar_t>(towlower(c)); });
    return manifest.find(L"steamvr") != std::wstring::npos
        || manifest.find(L"steamxr") != std::wstring::npos;
}
std::filesystem::path steamVrRootFromManifest(const std::wstring& manifest) {
    if (manifest.empty()) return {};
    auto path = std::filesystem::path(manifest).parent_path();
    if (_wcsicmp(path.filename().c_str(), L"bin") == 0) path = path.parent_path();
    return path;
}
void addSteamVrPathEnvironment(std::map<std::wstring,std::wstring>& env, const std::wstring& manifest) {
    auto steamVrRoot = steamVrRootFromManifest(manifest);
    if (!steamVrRoot.empty()) env[L"VR_OVERRIDE"] = steamVrRoot.wstring();
    wchar_t local[MAX_PATH]{};
    if (SUCCEEDED(SHGetFolderPathW(nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, local))) {
        auto pathRegistry = std::filesystem::path(local) / L"openvr/openvrpaths.vrpath";
        if (std::filesystem::exists(pathRegistry)) env[L"VR_PATHREG_OVERRIDE"] = pathRegistry.wstring();
    }
}
std::wstring findSteamExecutable() {
    for(auto hive:{HKEY_CURRENT_USER,HKEY_LOCAL_MACHINE})for(auto view:{KEY_WOW64_32KEY,KEY_WOW64_64KEY}){
        HKEY key{};
        if(RegOpenKeyExW(hive,L"SOFTWARE\\Valve\\Steam",0,KEY_READ|view,&key)!=ERROR_SUCCESS)continue;
        for(auto name:{L"SteamExe",L"SteamPath",L"InstallPath"}){
            wchar_t text[32768]{};DWORD size=sizeof(text),type{};
            if(RegQueryValueExW(key,name,nullptr,&type,reinterpret_cast<BYTE*>(text),&size)==ERROR_SUCCESS&&type==REG_SZ&&text[0]){
                RegCloseKey(key);
                return std::wstring(name)==L"SteamExe" ? std::filesystem::path(text).wstring()
                    : (std::filesystem::path(text)/L"steam.exe").wstring();
            }
        }
        RegCloseKey(key);
    }
    return {};
}
void ensureSteamVrArgentSettings(std::wofstream& log) {
    auto steam = findSteamExecutable();
    if (steam.empty()) return;
    auto settings = std::filesystem::path(steam).parent_path() / L"config/steamvr.vrsettings";
    std::error_code ec;
    std::filesystem::create_directories(settings.parent_path(), ec);
    std::ifstream input(settings, std::ios::binary);
    std::string text((std::istreambuf_iterator<char>(input)), {});
    auto updated = text;
    if (updated.empty()) updated = "{\r\n   \"steam.app.782330\" : {\r\n      \"disableAsync\" : false\r\n   }\r\n}\r\n";
    else if (updated.find("\"steam.app.782330\"") == std::string::npos) {
        auto close = updated.find_last_of('}');
        if (close != std::string::npos)
            updated.insert(close, std::string(updated.find(':') == std::string::npos ? "\r\n" : ",\r\n") +
                "   \"steam.app.782330\" : {\r\n      \"disableAsync\" : false\r\n   }\r\n");
    } else {
        std::regex blockPattern(R"(("steam\.app\.782330"\s*:\s*\{[^}]*))", std::regex::icase);
        std::smatch match;
        if (std::regex_search(updated, match, blockPattern)) {
            std::string block = match.str(1);
            std::regex disablePattern(R"(("disableAsync"\s*:\s*)(true|false))", std::regex::icase);
            std::string replacement = std::regex_search(block, disablePattern)
                ? std::regex_replace(block, disablePattern, "$1false")
                : block + ",\r\n      \"disableAsync\" : false";
            updated.replace(static_cast<size_t>(match.position(1)), static_cast<size_t>(match.length(1)), replacement);
        }
    }
    if (updated != text) {
        if (!text.empty() && !std::filesystem::exists(settings.wstring() + L".argent-backup", ec))
            std::filesystem::copy_file(settings, settings.wstring() + L".argent-backup", ec);
        std::ofstream output(settings, std::ios::binary | std::ios::trunc);
        output << updated;
        if (log) log << L"SteamVR settings: disableAsync=false for steam.app.782330\n";
    } else if (log) log << L"SteamVR settings: ARGENT already configured\n";
}

HWND addControl(HWND parent, const wchar_t* type, const wchar_t* text, DWORD style,
    int x, int y, int w, int h, int id) {
    HWND c = CreateWindowW(type, text, WS_CHILD | WS_VISIBLE | style, x, y, w, h,
        parent, reinterpret_cast<HMENU>(static_cast<INT_PTR>(id)), nullptr, nullptr);
    SendMessageW(c, WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    return c;
}

HWND addLabel(HWND parent, const wchar_t* text, int x, int y, int w, int h, bool muted = false) {
    HWND label = addControl(parent, L"STATIC", text, 0, x, y, w, h, 0);
    SetWindowLongPtrW(label, GWLP_USERDATA, muted ? 1 : 0);
    return label;
}

HWND addCheck(HWND parent, const wchar_t* text, int x, int y, int w, int h, int id, bool on = false) {
    HWND c = addControl(parent, L"BUTTON", text, BS_OWNERDRAW | WS_TABSTOP, x, y, w, h, id);
    setChecked(c,on);
    return c;
}

HWND addCombo(HWND parent, int x, int y, int w, int h, int id,
    std::initializer_list<const wchar_t*> items, int index) {
    HWND c = addControl(parent, L"COMBOBOX", L"", CBS_DROPDOWNLIST | CBS_HASSTRINGS | CBS_OWNERDRAWFIXED | CBS_DISABLENOSCROLL | WS_VSCROLL | WS_TABSTOP, x, y, w, h, id);
    for (auto item : items) SendMessageW(c, CB_ADDSTRING, 0, reinterpret_cast<LPARAM>(item));
    SendMessageW(c, CB_SETCURSEL, index, 0);
    return c;
}

bool extraCheck(HWND h){return h==handsJump||h==dominantHand||h==desktopMirror||h==renderingLogging||h==disableVrIntro||h==cinematics3d||h==fsr||h==physicalKill||h==virtualGunstock||h==bhaptics||h==psvr2Triggers||h==extendedLogging||h==gpuDiagnostics||h==showHands;}
void configureGpuDiagnostics(std::map<std::wstring,std::wstring>& env,bool enabled){env[L"ARGENT_GPU_DIAGNOSTICS"]=enabled?L"1":L"0";}
std::wstring loggingArgument(){return isChecked(extendedLogging)?L" -ExtendedLogging":L"";}
std::map<std::string,std::string> controlsSettings() {
    wchar_t angle[16]{},speed[16]{},kill[16]{};GetWindowTextW(snapAngle,angle,16);GetWindowTextW(smoothSpeed,speed,16);GetWindowTextW(killSpeed,kill,16);
    std::map<std::string,std::string> values{
      {"dominant",isChecked(dominantHand)?"left":"right"},
      {"left_hand_swap",selected(leftHandSwap)==1?"buttons-and-sticks":"buttons"},
      {"show_hands",isChecked(showHands)?"1":"0"},
      {"laser_sight","0"},
      {"hands_jump",isChecked(handsJump)?"1":"0"},
      {"cinematics_3d",isChecked(cinematics3d)?"1":"0"},
      {"weapon_pivot",std::to_string((selected(weaponPivot[0])-60)*.005f-.15f)+" "+std::to_string((selected(weaponPivot[1])-60)*.005f)+" "+std::to_string((selected(weaponPivot[2])-60)*.005f)},
      {"calibration_mode",calibrationModes[std::clamp(selected(calibrationMode),0,4)]},
      {"calibration_apply",std::to_string(calibrationApplyCommand.revision)},
      {"calibration_apply_profile",calibrationApplyCommand.profile},
      {"calibration_apply_mode",calibrationApplyCommand.mode},
      {"calibration_apply_left",calibrationApplyCommand.left?"1":"0"},
      {"profile",calibrationProfiles.at(std::max(0,selected(calibrationProfile)))},
      {"movement",selected(movement)==1?"offhand":"head"},
      {"turn",selected(turnMode)==1?"snap "+std::to_string(_wtoi(angle)):"smooth"},
      {"smooth_turn_speed",std::to_string(_wtoi(speed))},
      {"snap_turn_angle",std::to_string(_wtoi(angle))},
      {"physical_glory_kill",isChecked(physicalKill)?"1":"0"},
      {"physical_glory_kill_speed",std::string(kill,kill+wcslen(kill))},
      {"physical_glory_kill_hands",selected(killHands)==0?"left":selected(killHands)==1?"right":"both"},
      {"controller_layout","argent"},
      {"automatic_presentation","1"},
      {"cinematics_quad","1"},
      {"sync_immersive","1"},
      {"movement_immersive","1"},
      {"shoulder_chainsaw","1"},
      {"virtual_gunstock",isChecked(virtualGunstock)?"1":"0"},
      {"bhaptics_enabled",isChecked(bhaptics)?"1":"0"},
      {"psvr2_adaptive_triggers",isChecked(psvr2Triggers)?"1":"0"},
      {"crouch_enabled","0"},
      {"two_hand_enabled",isChecked(virtualGunstock)?"1":"0"}};
    return values;
}
std::map<std::wstring,HWND> launcherChecks(){
    if constexpr(cleanRelease)return {{L"desktopMirror",desktopMirror},{L"fsr",fsr},{L"extendedLoggingOptIn",extendedLogging},{L"captureShaders",captureShaders},{L"gpuCrashDiagnosticsOptIn",gpuDiagnostics}};
    return {{L"desktopMirror",desktopMirror},{L"fsr",fsr},{L"extendedLogging",extendedLogging},{L"disableAa",disableAa},
        {L"captureShaders",captureShaders},{L"simulator",simulator},{L"gpuCrashDiagnosticsOptIn",gpuDiagnostics}};
}
void saveSettings(){
    const auto settings=launcherSettingsPath();std::filesystem::create_directories(settings.parent_path());
    const auto temporary=settings.wstring()+L".tmp";
    // Preserve unknown keys; replace the complete snapshot only after all writes succeed.
    if(std::filesystem::exists(settings))std::filesystem::copy_file(settings,temporary,std::filesystem::copy_options::overwrite_existing);
    else {std::ofstream fresh(temporary,std::ios::binary|std::ios::trunc);fresh.write("\xff\xfe",2);fresh.close();if(!fresh)throw std::runtime_error("Cannot create settings");}
    auto put=[&](const wchar_t* section,const wchar_t* key,const std::wstring& value){
        if(!WritePrivateProfileStringW(section,key,value.c_str(),temporary.c_str()))throw std::runtime_error("Cannot save settings");
    };
    put(L"launcher",L"renderer",L"1");
    put(L"launcher",L"mirrorResolution",std::to_wstring(selected(mirrorResolution)));
    put(L"launcher",L"vrIntroStarted",vrIntroStarted?L"1":L"0");
    put(L"launcher",L"disableVrIntro",isChecked(disableVrIntro)?L"1":L"0");
    wchar_t scaleLabel[16]{};GetWindowTextW(renderScale,scaleLabel,16);
    put(L"launcher",L"renderScalePercent",std::to_wstring(_wtoi(scaleLabel)));
    for(const auto& setting:launcherChecks())put(L"launcher",setting.first.c_str(),isChecked(setting.second)?L"1":L"0");
    wchar_t gamePath[32768]{};GetWindowTextW(gameEdit,gamePath,32768);put(L"launcher",L"game",gamePath);
    for(const auto& setting:controlsSettings()){
        const std::wstring key(setting.first.begin(),setting.first.end()),value(setting.second.begin(),setting.second.end());
        put(L"controls",key.c_str(),value);
    }
    WritePrivateProfileStringW(nullptr,nullptr,nullptr,temporary.c_str());
    if(!MoveFileExW(temporary.c_str(),settings.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))throw std::runtime_error("Cannot replace settings");
}
bool autoSaveSettings(){
    if(!settingsReady)return true;
    try{saveSettings();return true;}catch(...){setStatus(L"Settings could not be saved. Check access to LocalAppData/KHARVOX ARGENT.");return false;}
}
void writeControlsConfig() {
    saveSettings();
    auto assets=root/L"assets";std::filesystem::create_directories(assets);
    auto path=assets/L"argent_controls.cfg";std::ifstream old(path);
    auto contents=argent::launcher::updateSettings(old,controlsSettings());old.close();
    auto temporary=assets/L"argent_controls.cfg.tmp";
    std::ofstream output(temporary);output<<contents;output.close();
    if(!output||!MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH))
        throw std::runtime_error("Cannot save controls");
}
std::filesystem::path legacyControlsPath(){
    const auto fallback=root/L"assets/argent_controls.cfg";
    const auto ini=launcherSettingsPath();
    if(GetPrivateProfileIntW(L"controls",L"automatic_presentation",-1,ini.c_str())!=-1)return fallback;
    std::error_code ec;const auto stamp=std::filesystem::last_write_time(ini,ec);if(ec)return fallback;
    // Older launchers wrote their local controls immediately before the shared
    // INI. Only import a sibling release whose save timestamp matches that INI.
    auto result=fallback;auto closest=std::chrono::duration_cast<std::filesystem::file_time_type::duration>(std::chrono::seconds(2));
    for(const auto& entry:std::filesystem::directory_iterator(root.parent_path(),ec)){
        if(!std::regex_match(entry.path().filename().string(),std::regex("ARGENT-Alpha-Test-r[0-9]{3}")))continue;
        const auto path=entry.path()/L"assets/argent_controls.cfg";
        const auto time=std::filesystem::last_write_time(path,ec);if(ec){ec.clear();continue;}
        const auto difference=time>stamp?time-stamp:stamp-time;
        if(difference<closest){closest=difference;result=path;}
    }
    return result;
}
void updateLeftHandLayout(){
    const bool on=isChecked(dominantHand);
    EnableWindow(leftHandSwap,on);
    ShowWindow(leftHandSwap,on?SW_SHOW:SW_HIDE);
    ShowWindow(leftHandLayoutLabel,on?SW_SHOW:SW_HIDE);
}
void loadSettings(){
    struct Restore {bool previous=settingsReady;Restore(){settingsReady=false;}~Restore(){settingsReady=previous;}} restore;
    auto ini=launcherSettingsPath().wstring();
    SendMessageW(mirrorResolution,CB_SETCURSEL,std::clamp(int(GetPrivateProfileIntW(L"launcher",L"mirrorResolution",1,ini.c_str())),0,3),0);
    vrIntroStarted=GetPrivateProfileIntW(L"launcher",L"vrIntroStarted",0,ini.c_str())!=0;
    setChecked(disableVrIntro,GetPrivateProfileIntW(L"launcher",L"disableVrIntro",0,ini.c_str())!=0);
    ShowWindow(disableVrIntro,vrIntroStarted?SW_SHOW:SW_HIDE);
    int scale=GetPrivateProfileIntW(L"launcher",L"renderScale",5,ini.c_str());
    int percent=GetPrivateProfileIntW(L"launcher",L"renderScalePercent",scale>=0&&scale<=5?50+scale*10:100,ini.c_str());
    SendMessageW(renderScale,CB_SETCURSEL,(std::clamp(percent,40,200)-40+2)/5,0);
    for(const auto& setting:launcherChecks())setChecked(setting.second,GetPrivateProfileIntW(L"launcher",setting.first.c_str(),0,ini.c_str())!=0);
    wchar_t game[32768]{};GetPrivateProfileStringW(L"launcher",L"game",L"",game,32768,ini.c_str());
    if(game[0]&&validGameExecutable(game))SetWindowTextW(gameEdit,game);
    else SetWindowTextW(gameEdit,findGameExecutable().c_str());
    std::ifstream defaults(legacyControlsPath());std::stringstream file;file<<defaults.rdbuf();file.clear();file<<'\n';
    // Release defaults and legacy package preferences are fallback only.
    // Central values win when a new release ships a fresh controls file.
    for(const auto& setting:controlsSettings()){
        const std::wstring key(setting.first.begin(),setting.first.end());wchar_t value[2048]{};
        if(GetPrivateProfileStringW(L"controls",key.c_str(),L"",value,2048,ini.c_str()))
            file<<setting.first<<' '<<std::string(value,value+wcslen(value))<<'\n';
    }
    std::string line;
    while(std::getline(file,line)){
        std::istringstream row(line);std::string key,value;row>>key>>value;
        for(auto setting:{std::pair{"virtual_gunstock",virtualGunstock},{"two_hand_enabled",virtualGunstock},{"bhaptics_enabled",bhaptics},{"psvr2_adaptive_triggers",psvr2Triggers},{"physical_glory_kill",physicalKill}})
            if(key==setting.first&&(value=="0"||value=="1"))setChecked(setting.second,value=="1");
        if(key=="weapon_pivot"){
            std::istringstream pivotRow(line.substr(key.size()));float v[3]{};
            if(pivotRow>>v[0]>>v[1]>>v[2])for(int i=0;i<3;++i)if(std::isfinite(v[i])&&v[i]>=(i==0?-.45f:-.3f)&&v[i]<=(i==0?.15f:.3f))SendMessageW(weaponPivot[i],CB_SETCURSEL,int(std::lround((v[i]+(i==0?.15f:0.f))/.005f))+60,0);
        }
        if(key=="show_hands")setChecked(showHands,value=="1");
        if(key=="hands_jump")setChecked(handsJump,value=="1");
        if(key=="cinematics_3d")setChecked(cinematics3d,value=="1");
        if(key=="calibration_mode"){for(int i=0;i<5;++i)if(value==calibrationModes[i])SendMessageW(calibrationMode,CB_SETCURSEL,i,0);}
        if(key=="profile"){
            auto it=std::find(calibrationProfiles.begin(),calibrationProfiles.end(),value);
            if(it==calibrationProfiles.end()){calibrationProfiles.push_back(value);std::wstring name(value.begin(),value.end());SendMessageW(calibrationProfile,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(name.c_str()));it=calibrationProfiles.end()-1;}
            SendMessageW(calibrationProfile,CB_SETCURSEL,it-calibrationProfiles.begin(),0);
        }
        if(key=="smooth_turn_speed"){int v=atoi(value.c_str());SendMessageW(smoothSpeed,CB_SETCURSEL,(std::clamp(v,200,300)-200+2)/5,0);}
        if(key=="snap_turn_angle"){int v=atoi(value.c_str());SendMessageW(snapAngle,CB_SETCURSEL,(std::clamp(v,45,90)-45+2)/5,0);}
        if(key=="physical_glory_kill_speed"){float v=float(atof(value.c_str()));if(v>=1&&v<=4)SendMessageW(killSpeed,CB_SETCURSEL,int((v-1)*5+.5f),0);}
        if(key=="physical_glory_kill_hands")SendMessageW(killHands,CB_SETCURSEL,value=="left"?0:value=="right"?1:2,0);
        if(key=="dominant")setChecked(dominantHand,value=="left");
        if(key=="left_hand_swap")SendMessageW(leftHandSwap,CB_SETCURSEL,value=="buttons-and-sticks",0);
        if(key=="movement")SendMessageW(movement,CB_SETCURSEL,value=="offhand",0);
        if(key=="turn"){
            SendMessageW(turnMode,CB_SETCURSEL,value=="snap",0);
            if(value=="snap"){int degrees{};row>>degrees;SendMessageW(snapAngle,CB_SETCURSEL,(std::clamp(degrees,45,90)-45+2)/5,0);}
        }
    }
    EnableWindow(snapAngle,TRUE);EnableWindow(smoothSpeed,TRUE);updateLeftHandLayout();
}

bool playNativeVrIntro(std::wofstream& log,const wchar_t* argument=L"--vr-intro"){
    if(vrIntroStarted&&isChecked(disableVrIntro))return true;
    wchar_t executable[32768]{};GetModuleFileNameW(nullptr,executable,32768);
    std::wstring command=quote(executable)+L" "+argument;
    STARTUPINFOW si{sizeof(si)};PROCESS_INFORMATION pi{};
    if(!CreateProcessW(executable,command.data(),nullptr,nullptr,FALSE,0,nullptr,root.c_str(),&si,&pi)){
        setStatus(L"Cannot start native VR intro. Doom was not started.");return false;
    }
    CloseHandle(pi.hThread);introThreadId=pi.dwThreadId;introCancelled=false;
    vrIntroStarted=true;ShowWindow(disableVrIntro,SW_SHOW);autoSaveSettings();
    setStatus(L"VR intro running. Press a controller button to continue to Doom Eternal.");
    if(log)log<<L"Native VR intro started: PID "<<pi.dwProcessId<<L". Waiting for complete OpenXR shutdown.\n"<<std::flush;
    bool finished=false;
    while(!finished){
        const auto result=MsgWaitForMultipleObjects(1,&pi.hProcess,FALSE,100,QS_ALLINPUT);
        if(result==WAIT_OBJECT_0){finished=true;break;}
        if(result==WAIT_FAILED)break;
        MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){
            if(message.message==WM_QUIT){introCancelled=true;PostThreadMessageW(introThreadId,WM_QUIT,0,0);continue;}
            TranslateMessage(&message);DispatchMessageW(&message);
        }
        if(introCancelled)PostThreadMessageW(introThreadId,WM_QUIT,0,0);
    }
    DWORD code=1;if(finished)GetExitCodeProcess(pi.hProcess,&code);CloseHandle(pi.hProcess);introThreadId=0;
    if(log)log<<L"Native VR intro exited: "<<code<<L" cancelled="<<introCancelled<<L"\n"<<std::flush;
    if(introCancelled){DestroyWindow(GetParent(statusText));return false;}
    if(!finished||code!=0){setStatus(L"VR intro did not complete. Retry, or enable Disable VR Intro under Rendering.");return false;}
    setStatus(L"VR intro finished. Starting Doom Eternal...");return true;
}
void runLauncher(bool launch) {
    if(launchInProgress)return;
    struct Busy {Busy(){launchInProgress=true;}~Busy(){launchInProgress=false;}} busy;
    wchar_t gamePath[32768]{};
    GetWindowTextW(gameEdit, gamePath, 32768);
    if (!std::filesystem::is_regular_file(gamePath)) {
        setStatus(L"Eternal executable not found. Select DOOMEternalx64vk.exe.");
        return;
    }
    const bool storeGame=microsoftStoreGame(gamePath);
    bool elevationKnown{};
    if (currentProcessElevated(elevationKnown)) {
        setStatus(storeGame ? L"ARGENT is running as administrator. Start ARGENT and DOOM normally."
                            : L"ARGENT is running as administrator. Start ARGENT, Steam, and DOOM normally.");
        return;
    }
    if (!elevationKnown) {
        setStatus(storeGame ? L"Cannot verify launcher elevation. Start ARGENT and DOOM normally."
                            : L"Cannot verify launcher elevation. Start ARGENT, Steam, and DOOM normally.");
        return;
    }
    std::wstring runAsLocation;
    bool runAsKnown{};
    if (doomRunAsAdmin(gamePath, runAsLocation, runAsKnown)) {
        setStatus(L"DOOM Eternal is configured to run as administrator. Disable that Compatibility setting.");
        return;
    }
    if (!runAsKnown) {
        setStatus(L"Cannot verify DOOM administrator Compatibility settings.");
        return;
    }
    if(!storeGame){
        bool steamKnown{};
        if (runningSteamElevated(steamKnown)) {
            setStatus(L"Steam is running as administrator. Exit Steam and start it normally.");
            return;
        }
        if (!steamKnown) {
            setStatus(L"Cannot verify Steam elevation. Start Steam normally, not as administrator.");
            return;
        }
    }

    try{writeControlsConfig();}catch(...){setStatus(L"Cannot save settings. Check write access to the package folder.");return;}
    std::filesystem::create_directories(root / L"logs");
    std::wofstream log(root / L"logs/launcher-output.txt", std::ios::trunc);
    if(log)log<<L"Game distribution: "<<(storeGame?L"Microsoft Store":L"Steam/other")<<L"; executable="<<gamePath<<L"\n";
    auto fail = [&](const wchar_t* message){ if(log)log<<message<<L"\n"; setStatus(message); };
    const auto runtime = std::filesystem::exists(root / L"ArgentLayer.dll") ? root : root / L"build/Release";
    for (auto name : {L"ArgentRuntimeProbe.exe", L"ArgentLayer.dll", L"ArgentLayer.json", L"openxr_loader.dll"}) {
        if (!std::filesystem::exists(runtime / name)) { fail(L"Required ARGENT runtime file missing."); return; }
    }
    constexpr int mode = 1; // Stereo VR is the only launcher rendering mode.
    const auto runtimeManifest = activeOpenXRManifestW();
    const bool steamVr = usesSteamVrRuntime();
    std::map<std::wstring,std::wstring> probeEnv;
    if (isChecked(simulator)) probeEnv[L"XR_RUNTIME_JSON"] = (root / L"logs/simulator-runtime.json").wstring();
    else if (steamVr) addSteamVrPathEnvironment(probeEnv, runtimeManifest);
    setStatus(launch ? L"Checking runtime and starting DOOM Eternal..." : L"Checking Vulkan, OpenXR and headset...");
    auto probe = processOutput(runtime / L"ArgentRuntimeProbe.exe", probeEnv);
    if (log) log << probe << L"\n";
    if (!launch && mode == 1 && !containsLine(probe, L"XR_EYE0_RECOMMENDED=")) { fail(L"VR diagnostics failed. Connect the headset and start its OpenXR runtime."); return; }
    if (!launch) { setStatus(L"Runtime check complete. See Development logs."); return; }
    if (steamVr) ensureSteamVrArgentSettings(log);
    wchar_t scaleText[16]{};
    GetWindowTextW(renderScale, scaleText, 16);
    const double scale = steamVr ? 1.0 : std::clamp(_wtoi(scaleText) / 100.0, .4, 2.);
    const bool useFsr = mode==1 && !steamVr && isChecked(fsr);
    const double engineScale = useFsr && scale<1.0 ? 1.0 : scale;
    wchar_t scaleEnv[32]{};
    swprintf_s(scaleEnv, L"%.2f", engineScale);
    std::map<std::wstring,std::wstring> env;
    env[L"VK_LAYER_PATH"] = runtime.wstring();
    env[L"VK_INSTANCE_LAYERS"] = L"VK_LAYER_ARGENT_OPENXR";
    env[L"ARGENT_ENABLE_LAYER"] = L"1";
    env[L"ARGENT_LOG"] = (root / L"logs" / (L"quad-" + timestampName() + L".log")).wstring();
    try {
        const auto capture=configureShaderCapture(env,isChecked(captureShaders),root,
            timestampName()+L"-"+std::to_wstring(GetCurrentProcessId()));
        if(log)log<<(capture.empty()?L"Shader capture: disabled":L"Shader capture: "+capture.wstring())<<L"\n";
    } catch(const std::exception&) {
        fail(L"Cannot create shader capture. Check write access to the package captures folder.");
        return;
    }
    if(!storeGame){
        env[L"SteamAppId"] = L"782330";
        env[L"SteamGameId"] = L"782330";
    }else{
        env[L"SteamAppId"] = L"";
        env[L"SteamGameId"] = L"";
    }
    if (steamVr) addSteamVrPathEnvironment(env, runtimeManifest);
    env[L"ARGENT_RENDER_SCALE"] = scaleEnv;
    env[L"ARGENT_FSR1"] = useFsr ? L"1" : L"0";
    configureStereoDlss(env,mode);
    if(log&&mode==1)log<<L"Stereo DLSS: independent eye histories enabled; engine selection is respected when FSR 1 is off.\n";
    const bool performanceDiagnostics=!cleanRelease&&std::filesystem::exists(root/L"assets/performance-diagnostics.cfg");
    env[L"ARGENT_PERFORMANCE_DIAGNOSTICS"] = performanceDiagnostics ? L"1" : L"0";
    env[L"ARGENT_DISABLE_AA"] = L"0";
    env[L"ARGENT_EXTENDED_LOGGING"] = performanceDiagnostics||isChecked(extendedLogging) ? L"1" : L"0";
    env[L"ARGENT_SFS_PROFILE_TIMING"] = !performanceDiagnostics&&isChecked(extendedLogging) ? L"1" : L"0";
    configureGpuDiagnostics(env,isChecked(gpuDiagnostics));
    if(log)log<<L"Clean release: "<<cleanRelease<<L". Native TAA blocked; DLSS allowed; engine setter/readback. Extended logging: "<<isChecked(extendedLogging)<<L"; GPU crash diagnostics: "<<isChecked(gpuDiagnostics)<<L"; continuous profiling: "<<performanceDiagnostics<<L"\n";
    env[L"ARGENT_GPU_TIMING"] = L"0";
    env[L"ARGENT_DESKTOP_MIRROR"] = isChecked(desktopMirror) ? L"1" : L"0";
    env[L"ARGENT_MIRROR_MAX_FPS"] = L"60";
    const int mirrorHeights[]{720,1080,1440,2160};
    const auto desktop=argent::desktopMirrorExtent(isChecked(desktopMirror),mirrorHeights[std::clamp(selected(mirrorResolution),0,3)]);
    env[L"ARGENT_MIRROR_HEIGHT"] = std::to_wstring(desktop.height);
    for (auto key : {L"DISABLE_VK_LAYER_VALVE_steam_overlay_1",L"DISABLE_VK_LAYER_VALVE_steam_fossilize_1",L"DISABLE_VULKAN_OBS_CAPTURE",L"EOS_OVERLAY_DISABLE_VULKAN_WIN64",L"DISABLE_VK_LAYER_RealVR_1",
        L"DISABLE_XR_APILAYER_VIRTUALDESKTOP_OCULUS_COMPATIBILITY"})
        env[key] = L"1";
    std::wstring args = L"+com_skipIntroVideo 1 +r_swapInterval 0 +r_filmGrainRatio 0 +r_motionblur 0 +g_autoMotionBlurOnGK 0 +r_chromaticAberration 0";
    args += L" +r_skipFlares 1 +r_lensFlaresRatio 0 +r_cineLensflaresEnabled 0 +view_skipShakes 1 +view_mpViewKick 0 +g_setting_hands_bob 0 +pm_noBob 1";
    wchar_t scaleArg[96]{};
    swprintf_s(scaleArg, L" +r_enableResolutionScale 1 +rs_enable 0 +rs_forceResolution %.2f", engineScale);
    args += scaleArg;
    if(useFsr)args += L" +r_antialiasing 0 +r_TAASafeMode 1";
    if (mode == 1 || mode == 2) {
        env[L"ARGENT_SFS_NATIVE_PROBE"] = L"1";
        env[L"ARGENT_SFS_NATIVE_VR"] = mode == 1 ? L"1" : L"0";
        if (steamVr && mode == 1) {
            // KHARVOX's working SteamXR path lets SteamVR create the Vulkan
            // device through XR_KHR_vulkan_enable2. Explicit zeroes also mask
            // stale process-wide overrides left by older ARGENT builds.
            env[L"ARGENT_STEAMVR_DEFER_XR_DEVICE"] = L"0";
            env[L"ARGENT_STEAMVR_FORCE_ENABLE1"] = L"0";
            if (log) log << L"SteamVR SFS: native VR enabled with KHARVOX runtime-managed XR device creation.\n";
            if (log) log << L"SteamVR SFS: using XR_KHR_vulkan_enable2 when exposed by SteamVR.\n";
        }
        if (mode == 1) {
            int eyeWidth = maxEyeValue(probe, true);
            int eyeHeight = maxEyeValue(probe, false);
            if ((eyeWidth <= 0 || eyeHeight <= 0) && steamVr) {
                eyeWidth = 1844;
                eyeHeight = 1972;
                if (log) log << L"SteamVR probe did not report eye size; using startup fallback 1844x1972 and allowing in-game OpenXR initialization.\n";
            }
            if (eyeWidth <= 0 || eyeHeight <= 0) {
                fail(L"VR diagnostics failed. Connect the headset and start its OpenXR runtime.");
                return;
            }
            const auto resolution=argent::renderResolution(uint32_t(eyeWidth),uint32_t(eyeHeight),float(scale),useFsr);
            if(log)log<<L"Render source: "<<resolution.sourceWidth<<L"x"<<resolution.sourceHeight<<L"; headset: "<<eyeWidth<<L"x"<<eyeHeight<<L"; engine scale: "<<engineScale<<L"; FSR upscale: "<<resolution.fsrUpscale<<L"\n";
            eyeWidth=int(resolution.sourceWidth);eyeHeight=int(resolution.sourceHeight);
            env[L"ARGENT_SFS_SOURCE_RING"] = L"1";
            env[L"ARGENT_EYE_WIDTH"] = std::to_wstring(eyeWidth);
            env[L"ARGENT_EYE_HEIGHT"] = std::to_wstring(eyeHeight);
            if (steamVr && log) {
                log << L"SteamVR SFS: KHARVOX-style source ring and render-extent hooks enabled for 2-layer stereo images.\n";
            }
            if(!storeGame)CopyFileW((root / L"assets/argent_vr.cfg").c_str(), (std::filesystem::path(gamePath).parent_path() / L"base/argent_vr.cfg").c_str(), FALSE);
            args += L" +r_fullscreen 0 +r_windowWidth "+std::to_wstring(desktop.width)+L" +r_windowHeight "+std::to_wstring(desktop.height)+L" +g_fov 90 +hands_fovScale 1 +in_joystick 1 +in_mouse 0 +in_MarkJoystickInactiveOnMouseInput 0 +g_reticleMode 2";
            if(!storeGame)args+=L" +exec argent_vr.cfg";
        }
        args += L" +r_enableRayTracing 0 +r_enableResolutionScale 1 +r_hdrDisplay 0 +r_presentFromAsync 1 +r_SSR 0";
        // Restore the values observed before r182, including persisted settings.
        args += L" +r_waterInterleaveUpdates 1 +r_waterGridTAA 1 +r_waterHalfRes 1";
        if(useFsr)args += L" +r_antialiasing 0 +r_TAASafeMode 1";
    }
    std::wstring bhPipe, bhToken, psPipe, psToken;
    if (isChecked(bhaptics) && std::filesystem::exists(runtime / L"KharvoxBhapticsBridge.exe") && std::filesystem::exists(runtime / L"bhaptics_library.dll")) {
        bhPipe = L"KharvoxBhaptics_" + hexToken(); bhToken = hexToken() + hexToken();
        env[L"KHARVOX_BHAPTICS_PIPE_NAME"] = bhPipe; env[L"KHARVOX_BHAPTICS_SESSION_TOKEN"] = bhToken;
    }
    if (isChecked(psvr2Triggers) && std::filesystem::exists(runtime / L"KharvoxPsvr2Bridge.exe") && std::filesystem::exists(runtime / L"psvr2_toolkit_capi_loader.dll")) {
        psPipe = L"KharvoxPsvr2_" + hexToken(); psToken = hexToken() + hexToken();
        env[L"KHARVOX_USE_PSVR2_TOOLKIT"] = L"1"; env[L"KHARVOX_PSVR2_PIPE_NAME"] = psPipe; env[L"KHARVOX_PSVR2_SESSION_TOKEN"] = psToken;
    }
    if(mode!=2&&!playNativeVrIntro(log))return;
    auto block = GetEnvironmentStringsW();
    std::wstring environment;
    if (block) {
        for (auto p = block; *p; p += wcslen(p) + 1) {
            std::wstring row(p); auto eq = row.find(L'='); auto key = eq == std::wstring::npos ? row : row.substr(0, eq);
            bool replaced=false; for (const auto& entry:env) if (_wcsicmp(key.c_str(),entry.first.c_str())==0) replaced=true;
            if(!replaced){environment+=row;environment.push_back(L'\0');}
        }
        FreeEnvironmentStringsW(block);
    }
    for (const auto& entry:env){environment+=entry.first+L"="+entry.second;environment.push_back(L'\0');}
    environment.push_back(L'\0');
    STARTUPINFOW si{};
    si.cb = sizeof(si);
    PROCESS_INFORMATION pi{};
    std::wstring command = quote(gamePath) + L" " + args;
    BOOL ok = CreateProcessW(gamePath, command.data(), nullptr, nullptr, FALSE,
        CREATE_UNICODE_ENVIRONMENT, environment.data(), std::filesystem::path(gamePath).parent_path().c_str(), &si, &pi);
    if (!ok) {
        const auto error = GetLastError();
        auto message = L"Cannot start DOOM Eternal. win32=" + std::to_wstring(error);
        if (log) log << message << L"\n";
        setStatus(message.c_str());
        return;
    }
    auto startBridge=[&](const wchar_t* exe,const std::map<std::wstring,std::wstring>& extra){
        if(!std::filesystem::exists(runtime/exe))return;
        auto bridgeEnv=env; for(auto& e:extra)bridgeEnv[e.first]=e.second;
        bridgeEnv[L"KHARVOX_BHAPTICS_PARENT_PID"]=std::to_wstring(pi.dwProcessId);
        bridgeEnv[L"KHARVOX_PSVR2_PARENT_PID"]=std::to_wstring(pi.dwProcessId);
        bridgeEnv[L"KHARVOX_EXTENDED_LOGGING"]=isChecked(extendedLogging)?L"1":L"0";
        auto bridgeBlock=GetEnvironmentStringsW(); std::wstring be;
        if(bridgeBlock){for(auto p=bridgeBlock;*p;p+=wcslen(p)+1){std::wstring row(p);auto eq=row.find(L'=');auto key=eq==std::wstring::npos?row:row.substr(0,eq);bool rep=false;for(auto& e:bridgeEnv)if(_wcsicmp(key.c_str(),e.first.c_str())==0)rep=true;if(!rep){be+=row;be.push_back(L'\0');}}FreeEnvironmentStringsW(bridgeBlock);}
        for(auto& e:bridgeEnv){be+=e.first+L"="+e.second;be.push_back(L'\0');}be.push_back(L'\0');
        STARTUPINFOW bsi{};bsi.cb=sizeof(bsi);PROCESS_INFORMATION bpi{};
        std::wstring bcmd=quote((runtime/exe).wstring());
        if(CreateProcessW((runtime/exe).c_str(),bcmd.data(),nullptr,nullptr,FALSE,
            CREATE_NO_WINDOW|CREATE_UNICODE_ENVIRONMENT,be.data(),runtime.c_str(),&bsi,&bpi)){
            if(log)log<<L"Bridge started: "<<exe<<L" PID="<<bpi.dwProcessId<<L"\n";
            CloseHandle(bpi.hThread);CloseHandle(bpi.hProcess);
        }else if(log)log<<L"Bridge start failed: "<<exe<<L" win32="<<GetLastError()<<L"\n";
    };
    if(!bhPipe.empty())startBridge(L"KharvoxBhapticsBridge.exe",{{L"KHARVOX_BHAPTICS_PIPE_NAME",bhPipe},{L"KHARVOX_BHAPTICS_SESSION_TOKEN",bhToken}});
    if(!psPipe.empty())startBridge(L"KharvoxPsvr2Bridge.exe",{{L"KHARVOX_USE_PSVR2_TOOLKIT",L"1"},{L"KHARVOX_PSVR2_PIPE_NAME",psPipe},{L"KHARVOX_PSVR2_SESSION_TOKEN",psToken}});
    if(log)log<<L"Eternal launched: PID "<<pi.dwProcessId<<L"\n"<<args<<L"\n";
    setStatus(L"DOOM Eternal launched; watching startup...");
    const DWORD early = WaitForSingleObject(pi.hProcess, 30000);
    if (early == WAIT_OBJECT_0) {
        DWORD code{};
        GetExitCodeProcess(pi.hProcess, &code);
        if (log) log << L"DOOM Eternal exited during startup. exitCode=0x" << std::hex << code << std::dec << L"\n";
        wchar_t status[128]{};
        swprintf_s(status, L"DOOM Eternal exited during startup. code=0x%08X", code);
        setStatus(status);
    } else {
        setStatus(L"DOOM Eternal launched.");
    }
    CloseHandle(pi.hThread);
    CloseHandle(pi.hProcess);
}

#include "LauncherHelp.inc"
void showTab(int index) {
    if(index<0||index>=7||(!advancedTabs&&(index==3||index==4)))return;
    activeTab = index;
    if(index>=5)SetFocus(tabPages[index]);
    for (int i = 0; i < 7; ++i) ShowWindow(tabPages[i], i == index ? SW_SHOW : SW_HIDE);
    for (auto button : tabButtons) if (button) InvalidateRect(button, nullptr, TRUE);
}

void layoutTabs(bool advanced) {
    advancedTabs=advanced;
    if(!advanced&&(activeTab==3||activeTab==4))showTab(0);
    int x=42;
    for(int i:{0,1,2,5,6,3,4}){
        const bool visible=advanced||i<3||i>4;
        ShowWindow(tabButtons[i],visible?SW_SHOW:SW_HIDE);
        if(visible){const int width=advanced?(i==5?162:109):(i==5?216:150);MoveWindow(tabButtons[i],x,212,width,40,TRUE);x+=width;}
    }
}
LRESULT CALLBACK pageProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX:
    case WM_DRAWITEM:
    case WM_MEASUREITEM:
    case WM_COMMAND:
    case WM_NOTIFY:
        return SendMessageW(GetParent(hwnd), msg, wp, lp);
    case WM_ERASEBKGND: {
        RECT area{}; GetClientRect(hwnd, &area);
        FillRect(reinterpret_cast<HDC>(wp), &area, backgroundBrush);
        return TRUE;
    }
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

void createTabs(HWND hwnd) {
    const wchar_t* labels[]{ L"Rendering", L"Movement", L"VR Options", L"Development", L"Calibration", L"Controller Binding", L"Instructions" };
    for (int i = 0; i < 7; ++i)
        tabButtons[i] = addControl(hwnd, L"BUTTON", labels[i], BS_OWNERDRAW | WS_TABSTOP,
            42 + i * 163, 212, 163, 40, IdTabBase + i);
    for (int i = 0; i < 5; ++i) {
        tabPages[i] = CreateWindowW(L"ArgentTabPage", L"", WS_CHILD | WS_VISIBLE, 55, 264, 790, 264,
            hwnd, nullptr, nullptr, nullptr);
        SendMessageW(tabPages[i], WM_SETFONT, reinterpret_cast<WPARAM>(uiFont), TRUE);
    }

    for(int i=5;i<7;++i)tabPages[i]=CreateWindowW(L"ArgentHelpPage",L"",WS_CHILD|WS_VSCROLL|WS_TABSTOP,55,264,790,264,hwnd,nullptr,nullptr,&helpStates[i-5]);
    layoutTabs(false);
    addLabel(tabPages[0], L"RenderScale", 24, 24, 190, 24);
    renderScale = addCombo(tabPages[0], 250, 18, 180, 180, IdRenderScale,
        {}, 0);
    for(int v=40;v<=200;v+=5){auto t=std::to_wstring(v)+L" %";SendMessageW(renderScale,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));}
    SendMessageW(renderScale,CB_SETCURSEL,12,0);
    addLabel(tabPages[0], L"FSR is disabled at 100% res or higher. Turn off for DLSS usage.", 24, 58, 730, 24, true);

    fsr=addCheck(tabPages[0],L"FSR Upscaling",450,16,315,32,IdFsr);
    disableVrIntro=addCheck(tabPages[0],L"Disable VR Intro",395,98,330,32,IdDisableVrIntro);
    ShowWindow(disableVrIntro,SW_HIDE);
    cinematics3d=addCheck(tabPages[0],L"3D Cinematics",24,98,330,32,IdCinematics3d);
    renderingLogging=addCheck(tabPages[0],L"Extended Logging",24,138,330,32,IdRenderingLogging);
    desktopMirror=addCheck(tabPages[0],L"Desktop Mirror (Right Eye)",24,178,360,32,IdDesktopMirror);
    mirrorResolution=addCombo(tabPages[0],420,178,320,160,IdMirrorResolution,{L"720p (1280 x 720)",L"1080p (1920 x 1080)",L"2K / 1440p (2560 x 1440)",L"4K (3840 x 2160)"},1);
    addLabel(tabPages[0],L"Shows one eye in the game window at up to 60 FPS. Applies on next game start.",24,216,730,24,true);

    addLabel(tabPages[1], L"Turning", 24, 18, 180, 24);
    turnMode = addCombo(tabPages[1], 250, 14, 230, 160, IdTurnMode, { L"Smooth", L"Snap" }, 1);
    addLabel(tabPages[1], L"Snap angle", 510, 18, 120, 24);
    snapAngle = addCombo(tabPages[1], 642, 14, 100, 160, IdSnapAngle,
        {},0);
    for(int v=45;v<=90;v+=5){auto t=std::to_wstring(v);SendMessageW(snapAngle,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));}
    SendMessageW(snapAngle,CB_SETCURSEL,0,0);
    addLabel(tabPages[1], L"Smooth speed (deg/s)",24,60,190,24);
    smoothSpeed=addCombo(tabPages[1],250,56,230,250,IdSmoothSpeed,{},0);
    for(int v=200;v<=300;v+=5){auto t=std::to_wstring(v);SendMessageW(smoothSpeed,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t.c_str()));}
    SendMessageW(smoothSpeed,CB_SETCURSEL,6,0);
    addLabel(tabPages[1], L"Movement direction", 24, 102, 190, 24);
    movement = addCombo(tabPages[1], 250, 98, 300, 160, IdMovement, { L"Head direction", L"Off-hand direction" }, 0);
    dominantHand = addCheck(tabPages[1],L"Left Handed Mode",24,140,330,32,IdDominantHand);
    leftHandLayoutLabel=addLabel(tabPages[1],L"Left-hand layout",24,186,210,24);
    leftHandSwap=addCombo(tabPages[1],250,182,330,160,IdLeftHandSwap,{L"Button Swap",L"Button and Stick Swap"},0);
    updateLeftHandLayout();

    virtualGunstock=addCheck(tabPages[2],L"Virtual Gunstock",24,10,330,32,IdVirtualGunstock,true);
    bhaptics=addCheck(tabPages[2],L"bHaptics",395,10,330,32,IdBhaptics);
    psvr2Triggers=addCheck(tabPages[2],L"PSVR2 Adaptive Triggers",24,54,370,32,IdPsvr2Triggers);
    physicalKill=addCheck(tabPages[2],L"Physical Glory Kill / melee",24,98,375,32,IdPhysicalKill,true);
    addLabel(tabPages[2],L"Punch speed (m/s)",24,160,180,24);
    killSpeed=addCombo(tabPages[2],205,152,100,220,IdKillSpeed,{},0);
    for(int v=0;v<=15;++v){wchar_t t[16]{};swprintf_s(t,L"%.1f",1.+v*.2);SendMessageW(killSpeed,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(t));}
    SendMessageW(killSpeed,CB_SETCURSEL,9,0);
    addLabel(tabPages[2],L"Punch hand",395,160,130,24);
    killHands=addCombo(tabPages[2],540,152,210,150,IdKillHands,{L"Left",L"Right",L"Both"},2);
    handsJump=addCheck(tabPages[2],L"Hands Jump",395,98,330,32,IdHandsJump,true);

    if(cleanRelease){
    extendedLogging = addCheck(tabPages[3], L"Extended Logging", 24, 18, 350, 32, IdExtendedLogging);
    captureShaders = addCheck(tabPages[3], L"Capture shaders", 24, 60, 350, 32, IdCaptureShaders);
    gpuDiagnostics = addCheck(tabPages[3], L"GPU crash diagnostics", 430, 18, 330, 32, IdGpuDiagnostics);
    addLabel(tabPages[3], L"Extra driver instrumentation. May affect timing. Restart the game after changing.", 430, 56, 310, 54, true);
    addLabel(tabPages[3], L"Enable before PLAY. F8 captures water/depth pairs or both scene eyes when no water pass is active. Wait for completion tone; may stutter.", 24, 106, 380, 110, true);
    addControl(tabPages[3], L"BUTTON", L"RUN DIAGNOSTICS", BS_OWNERDRAW | WS_TABSTOP, 430, 116, 290, 42, IdDiagnose);
    addControl(tabPages[3], L"BUTTON", L"OPEN LOG FOLDER", BS_OWNERDRAW | WS_TABSTOP, 430, 174, 290, 42, IdLogs);
    }else{
    disableAa = addCheck(tabPages[3], L"Disable all AA (including DLSS)", 24, 18, 340, 32, IdDisableAa);
    captureShaders = addCheck(tabPages[3], L"Capture shaders", 24, 62, 300, 32, IdCaptureShaders);
    simulator = addCheck(tabPages[3], L"Use OpenXR simulator", 24, 106, 300, 32, IdSimulator);
    extendedLogging = addCheck(tabPages[3], L"Extended Logging", 24, 150, 350, 32, IdExtendedLogging);
    addLabel(tabPages[3], L"CPU/GPU timings in .\\logs; profiling may affect performance.", 24, 194, 730, 24, true);
    addControl(tabPages[3], L"BUTTON", L"RUN DIAGNOSTICS", BS_OWNERDRAW | WS_TABSTOP, 430, 18, 290, 42, IdDiagnose);
    addControl(tabPages[3], L"BUTTON", L"OPEN LOG FOLDER", BS_OWNERDRAW | WS_TABSTOP, 430, 78, 290, 42, IdLogs);
    gpuDiagnostics = addCheck(tabPages[3], L"GPU crash diagnostics", 430, 142, 330, 32, IdGpuDiagnostics);
    }
    showHands=addCheck(tabPages[4],L"Show VR hands",24,6,300,32,IdShowHands,true);
    addLabel(tabPages[4],L"Calibration",24,52,110,24);
    calibrationMode=addCombo(tabPages[4],140,46,225,170,IdCalibrationMode,{L"Off (gameplay)",L"Hands",L"Weapon pose",L"Support point",L"Offhand HUD (live)"},0);
    addLabel(tabPages[4],L"Edit profile",375,52,100,24);
    calibrationProfile=addCombo(tabPages[4],480,46,265,220,IdCalibrationProfile,{},0);
    for(const auto& key:calibrationProfiles){std::wstring text(key.begin(),key.end());SendMessageW(calibrationProfile,CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text.c_str()));}
    SendMessageW(calibrationProfile,CB_SETCURSEL,0,0);
    addControl(tabPages[4],L"BUTTON",L"APPLY",BS_OWNERDRAW|WS_TABSTOP,24,90,270,38,IdApplyCalibration);
    addControl(tabPages[4],L"BUTTON",L"OPEN SAVED FILES",BS_OWNERDRAW|WS_TABSTOP,320,90,260,38,IdCalibrationFiles);
    const wchar_t* pivotLabels[]{L"Pivot: back (-) / forward (+)",L"Pivot: right (-) / left (+)",L"Pivot: down (-) / up (+)"};
    for(int i=0;i<3;++i){
        addLabel(tabPages[4],pivotLabels[i],24+i*248,138,240,24,true);
        weaponPivot[i]=addCombo(tabPages[4],24+i*248,164,225,220,IdPivotForward+i,{},60);
        for(int step=-60;step<=60;++step){wchar_t text[32];swprintf_s(text,L"%+.1f cm",step*.5);SendMessageW(weaponPivot[i],CB_ADDSTRING,0,reinterpret_cast<LPARAM>(text));}
        SendMessageW(weaponPivot[i],CB_SETCURSEL,60,0);
    }
    addLabel(tabPages[4],L"Weapon-local pivot, both hands. APPLY to update live. 0 cm = calibrated grip (-15 cm forward).",24,202,740,24,true);
    addLabel(tabPages[4],L"HUD live: Num0 selects (blink), Num+ position/rotation, Num . artwork pivot.",24,224,740,20,true);
    addLabel(tabPages[4],L"HUD: Num4/6, 8/2, 7/9 adjust; * / size; Shift fine; Num5 reset. Auto-save.",24,244,740,20,true);
    addLabel(tabPages[4],L"Hands / Weapon pose: numpad previews; APPLY saves this profile and handedness.",24,264,740,20,true);
    showTab(0);
}

LRESULT CALLBACK windowProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
    switch (msg) {
    case WM_CREATE: {
        uiFont = CreateFontW(-18, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        titleFont = CreateFontW(-23, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE, DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS, CLEARTYPE_QUALITY, DEFAULT_PITCH, L"Segoe UI");
        addLabel(hwnd, L"A DOOM ETERNAL VR CONVERSION MOD", 42, 166, 650, 26, true);
        addLabel(hwnd, L"Game location", 42, 530, 180, 22);
        const auto gamePath = findGameExecutable();
        gameEdit = addControl(hwnd, L"EDIT", gamePath.c_str(),
            WS_BORDER | ES_AUTOHSCROLL | WS_TABSTOP, 42, 558, 650, 32, IdGamePath);
        addControl(hwnd, L"BUTTON", L"BROWSE", BS_OWNERDRAW | WS_TABSTOP, 708, 557, 150, 34, IdBrowse);
        createTabs(hwnd);
        loadSettings();
        launchButton = addControl(hwnd, L"BUTTON", L"PLAY", BS_OWNERDRAW | WS_TABSTOP,
            350, 614, 200, 48, IdLaunch);
        statusText = addLabel(hwnd, L"Ready", 42, 676, 635, 28, true);
        addControl(hwnd,L"BUTTON",L"Made by Cactus",BS_OWNERDRAW|WS_TABSTOP,700,676,158,28,IdCredit);
        settingsReady=true;
        SetTimer(hwnd, 1, 500, nullptr);
        return 0;
    }
    case WM_PAINT: {
        PAINTSTRUCT ps{};
        HDC hdc = BeginPaint(hwnd, &ps);
        RECT banner{ 42, 18, 858, 158 };
        FillRect(hdc, &banner, static_cast<HBRUSH>(GetStockObject(BLACK_BRUSH)));
        if (logo && logo->GetLastStatus() == Gdiplus::Ok) {
            Gdiplus::Graphics g(hdc);
            g.SetInterpolationMode(Gdiplus::InterpolationModeHighQualityBicubic);
            const auto sourceWidth = logo->GetWidth();
            const auto sourceHeight = logo->GetHeight();
            g.DrawImage(logo, Gdiplus::Rect(banner.left, banner.top, banner.right - banner.left, banner.bottom - banner.top),
                0, sourceHeight / 4, sourceWidth, sourceHeight / 2, Gdiplus::UnitPixel);
        }
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_CTLCOLORSTATIC:
    case WM_CTLCOLOREDIT:
    case WM_CTLCOLORBTN:
    case WM_CTLCOLORLISTBOX: {
        HDC hdc = reinterpret_cast<HDC>(wp);
        SetTextColor(hdc, msg == WM_CTLCOLORSTATIC && GetWindowLongPtrW(reinterpret_cast<HWND>(lp), GWLP_USERDATA) ? Muted : Text);
        const bool input = msg == WM_CTLCOLOREDIT || msg == WM_CTLCOLORLISTBOX;
        SetBkColor(hdc, input ? Field : Panel);
        return reinterpret_cast<LRESULT>(input ? fieldBrush : panelBrush);
    }
    case WM_MEASUREITEM: {
        auto* measure=reinterpret_cast<MEASUREITEMSTRUCT*>(lp);
        if(measure->CtlType==ODT_COMBOBOX){measure->itemHeight=28;return TRUE;}
        break;
    }
    case WM_DRAWITEM: {
        auto* dis = reinterpret_cast<DRAWITEMSTRUCT*>(lp);
        if (dis->CtlType == ODT_BUTTON) {
            wchar_t label[256]{}; GetWindowTextW(dis->hwndItem, label, 256);
            const int controlId = GetDlgCtrlID(dis->hwndItem);
            if (controlId >= IdTabBase && controlId < IdTabBase + 7) {
                const bool active = controlId - IdTabBase == activeTab;
                FillRect(dis->hDC, &dis->rcItem, backgroundBrush);
                SetBkMode(dis->hDC, TRANSPARENT);
                SetTextColor(dis->hDC, Text);
                DrawTextW(dis->hDC, label, -1, &dis->rcItem, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
                if (active) {
                    RECT line = dis->rcItem; line.top = line.bottom - 3;
                    HBRUSH accentBrush = CreateSolidBrush(Accent);
                    FillRect(dis->hDC, &line, accentBrush); DeleteObject(accentBrush);
                }
                return TRUE;
            }
            if(controlId==IdCredit){FillRect(dis->hDC,&dis->rcItem,backgroundBrush);SetBkMode(dis->hDC,TRANSPARENT);SetTextColor(dis->hDC,Muted);DrawTextW(dis->hDC,label,-1,&dis->rcItem,DT_RIGHT|DT_VCENTER|DT_SINGLELINE);if(dis->itemState&ODS_FOCUS)DrawFocusRect(dis->hDC,&dis->rcItem);return TRUE;}
            const bool check = dis->hwndItem == disableAa || dis->hwndItem == captureShaders ||
                dis->hwndItem == simulator || extraCheck(dis->hwndItem);
            HBRUSH brush = CreateSolidBrush(check ? Panel : ((dis->itemState & ODS_SELECTED) ? RGB(210, 45, 8) : Accent));
            FillRect(dis->hDC, &dis->rcItem, brush); DeleteObject(brush);
            SetBkMode(dis->hDC, TRANSPARENT); SetTextColor(dis->hDC, Text);
            RECT textRect = dis->rcItem;
            if (check) {
                RECT box{ textRect.left + 2, textRect.top + 6, textRect.left + 22, textRect.top + 26 };
                HBRUSH boxBrush = CreateSolidBrush(isChecked(dis->hwndItem) ? Accent : Field);
                FillRect(dis->hDC, &box, boxBrush); DeleteObject(boxBrush);
                HBRUSH borderBrush = CreateSolidBrush(isChecked(dis->hwndItem) ? Accent : Border);
                FrameRect(dis->hDC, &box, borderBrush); DeleteObject(borderBrush);
                if (isChecked(dis->hwndItem)) {
                    HPEN pen = CreatePen(PS_SOLID, 2, Text);
                    HGDIOBJ previous = SelectObject(dis->hDC, pen);
                    MoveToEx(dis->hDC, box.left + 4, box.top + 10, nullptr);
                    LineTo(dis->hDC, box.left + 9, box.top + 15);
                    LineTo(dis->hDC, box.left + 17, box.top + 5);
                    SelectObject(dis->hDC, previous); DeleteObject(pen);
                }
                textRect.left += 34;
                DrawTextW(dis->hDC, label, -1, &textRect, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
            } else DrawTextW(dis->hDC, label, -1, &textRect, DT_CENTER | DT_VCENTER | DT_SINGLELINE);
            return TRUE;
        }
        if (dis->CtlType == ODT_COMBOBOX) {
            HBRUSH brush = CreateSolidBrush(Field); FillRect(dis->hDC, &dis->rcItem, brush); DeleteObject(brush);
            if (dis->itemID != static_cast<UINT>(-1)) {
                wchar_t label[256]{}; SendMessageW(dis->hwndItem, CB_GETLBTEXT, dis->itemID, reinterpret_cast<LPARAM>(label));
                SetBkMode(dis->hDC, TRANSPARENT); SetTextColor(dis->hDC, Text);
                RECT r = dis->rcItem; r.left += 10; DrawTextW(dis->hDC, label, -1, &r, DT_LEFT | DT_VCENTER | DT_SINGLELINE);
                if (dis->itemState & ODS_SELECTED) {
                    RECT line = dis->rcItem; line.right = line.left + 3;
                    HBRUSH accentBrush = CreateSolidBrush(Accent);
                    FillRect(dis->hDC, &line, accentBrush); DeleteObject(accentBrush);
                }
            }
            return TRUE;
        }
        break;
    }
    case WM_COMMAND:
        if(introThreadId)return 0;
        if(LOWORD(wp)==IdCredit){if(HIWORD(wp)==BN_CLICKED&&(GetKeyState(VK_SHIFT)&0x8000))layoutTabs(!advancedTabs);return 0;}
        if (LOWORD(wp) >= IdTabBase && LOWORD(wp) < IdTabBase + 7) {
            showTab(LOWORD(wp) - IdTabBase);
            return 0;
        }
        if (HIWORD(wp) == CBN_SELCHANGE) {
            EnableWindow(snapAngle,TRUE);EnableWindow(smoothSpeed,TRUE);updateLeftHandLayout();
            autoSaveSettings();
            if(reinterpret_cast<HWND>(lp)==calibrationMode||reinterpret_cast<HWND>(lp)==calibrationProfile||reinterpret_cast<HWND>(lp)==dominantHand){
                try{writeControlsConfig();setStatus(L"Calibration target selected. Adjust in game, then APPLY to save.");}
                catch(...){setStatus(L"Cannot update calibration target.");}
            }
            return 0;
        }
        if(LOWORD(wp)==IdGamePath&&HIWORD(wp)==EN_CHANGE){autoSaveSettings();return 0;}
        if (HIWORD(wp) == BN_CLICKED) {
            HWND control = reinterpret_cast<HWND>(lp);
            const bool check = control == disableAa || control == captureShaders || control == simulator ||
                extraCheck(control);
            if (check) {
                setChecked(control,!isChecked(control));
                if(control==dominantHand)updateLeftHandLayout();
                InvalidateRect(control, nullptr, TRUE);
                autoSaveSettings();
                return 0;
            }
        }
        switch (LOWORD(wp)) {
        case IdLaunch: runLauncher(true); break;
        case IdDiagnose: runLauncher(false); break;
        case IdLogs:
            std::filesystem::create_directories(root / L"logs");
            ShellExecuteW(hwnd, L"open", (root / L"logs").c_str(), nullptr, nullptr, SW_SHOWNORMAL);
            break;
        case IdApplyCalibration:
            try{
                const bool pose=selected(calibrationMode)==1||selected(calibrationMode)==2;
                if(pose){
                    calibrationApplyCommand.revision=std::max(calibrationApplyCommand.revision+1,uint64_t(GetTickCount64()));
                    calibrationApplyCommand.mode=calibrationModes[selected(calibrationMode)];
                    calibrationApplyCommand.profile=calibrationProfiles.at(std::max(0,selected(calibrationProfile)));
                    calibrationApplyCommand.left=isChecked(dominantHand);calibrationApplyWaiting=true;
                }
                writeControlsConfig();setStatus(pose?L"Apply requested. Return to the running game to finish saving.":L"Calibration settings applied.");
            }catch(...){calibrationApplyWaiting=false;setStatus(L"Cannot save calibration settings.");}
            break;
        case IdCalibrationFiles:
            ShellExecuteW(hwnd,L"open",root.c_str(),nullptr,nullptr,SW_SHOWNORMAL);
            break;
        case IdBrowse: {
            BROWSEINFOW browse{};
            browse.hwndOwner=hwnd;
            browse.lpszTitle=L"Select the DOOM Eternal folder (the folder containing DOOMEternalx64vk.exe)";
            browse.ulFlags=BIF_RETURNONLYFSDIRS|BIF_NEWDIALOGSTYLE;
            if(auto selection=SHBrowseForFolderW(&browse)){
                wchar_t folder[MAX_PATH]{};
                if(SHGetPathFromIDListW(selection,folder)){
                    const auto chosen=std::filesystem::path(folder);
                    for(const auto& candidate:{chosen/L"DOOMEternalx64vk.exe",chosen/L"Content/DOOMEternalx64vk.exe",findMicrosoftStoreGame(chosen)}){
                        if(validGameExecutable(candidate)){SetWindowTextW(gameEdit,candidate.c_str());break;}
                    }
                }
                CoTaskMemFree(selection);
            }
            break;
        }
        }
        return 0;
    case WM_TIMER:
        if(calibrationApplyWaiting){
            std::ifstream status(root/L"calibration_apply_status.txt");uint64_t revision{};int success{};
            if(status>>revision>>success&&revision==calibrationApplyCommand.revision){
                calibrationApplyWaiting=false;
                std::string target;std::getline(status,target);
                const std::wstring message=success?L"Saved:"+std::wstring(target.begin(),target.end()):L"Pose not saved. Check the selected target/folder and retry APPLY.";
                setStatus(message.c_str());
            }
        }
        if (worker && WaitForSingleObject(worker, 0) == WAIT_OBJECT_0) {
            DWORD code{};
            GetExitCodeProcess(worker, &code);
            CloseHandle(worker);
            worker = nullptr;
            EnableWindow(launchButton, TRUE);
            if (code == 0) setStatus(L"Ready");
            else {
                const auto error = lastLauncherError();
                setStatus(error.empty() ? L"Startup check failed. Details are available in Development." : error.c_str());
            }
        }
        return 0;
    case WM_QUERYENDSESSION:
        return autoSaveSettings()?TRUE:FALSE;
    case WM_CLOSE:
        if(introThreadId){introCancelled=true;PostThreadMessageW(introThreadId,WM_QUIT,0,0);setStatus(L"Closing VR intro; game launch cancelled.");return 0;}
        if(autoSaveSettings())DestroyWindow(hwnd);
        return 0;
    case WM_DESTROY:
        settingsReady=false;
        if (worker) CloseHandle(worker);
        delete controllerBindingImage;
        delete logo;
        if (uiFont) DeleteObject(uiFont);
        if (titleFont) DeleteObject(titleFont);
        if (backgroundBrush) DeleteObject(backgroundBrush);
        if (panelBrush) DeleteObject(panelBrush);
        if (fieldBrush) DeleteObject(fieldBrush);
        if (gdiplusToken) Gdiplus::GdiplusShutdown(gdiplusToken);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

}

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, LPWSTR arguments, int show) {
    wchar_t path[32768]{};
    GetModuleFileNameW(nullptr, path, 32768);
    root = std::filesystem::path(path).parent_path();
    if(arguments&&std::wstring(arguments)==L"--intro-self-test")return 0;
    if(arguments&&std::wstring(arguments)==L"--intro-self-test-fail")return 1;
    if(arguments&&std::wstring(arguments)==L"--vr-intro")return runArgentVrIntro((root/L"openxr_loader.dll").c_str());
    const bool selfTest=arguments&&std::wstring(arguments)==L"--self-test";
    isolatedSettings=selfTest;
    if(selfTest){root=std::filesystem::temp_directory_path()/(L"ArgentLauncher-Test-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));std::filesystem::create_directories(root/L"assets");
      std::ofstream(root/L"assets/argent_controls.cfg")<<"hand left 0.1 0 0 0 0 0\nweapon default 0 0 0 0 0 0 0 0 -0.3 0.14 1 1\n";}
    isolatedSettingsRoot=root;


    Gdiplus::GdiplusStartupInput gdiplusInput;
    Gdiplus::GdiplusStartup(&gdiplusToken, &gdiplusInput, nullptr);
    auto logoPath = root / L"assets" / L"kharvox-argent-logo.png";
    if (!std::filesystem::exists(logoPath)) logoPath = root.parent_path() / L"assets" / L"kharvox-argent-logo.png";
    if (std::filesystem::exists(logoPath)) logo = Gdiplus::Image::FromFile(logoPath.c_str(), FALSE);

    INITCOMMONCONTROLSEX icc{ sizeof(icc), ICC_TAB_CLASSES | ICC_STANDARD_CLASSES };
    InitCommonControlsEx(&icc);
    backgroundBrush = CreateSolidBrush(Background);
    panelBrush = CreateSolidBrush(Panel);
    fieldBrush = CreateSolidBrush(Field);

    WNDCLASSW pageClass{};
    pageClass.lpfnWndProc = pageProc;
    pageClass.hInstance = instance;
    pageClass.lpszClassName = L"ArgentTabPage";
    pageClass.hCursor = LoadCursor(nullptr, IDC_ARROW);
    pageClass.hbrBackground = backgroundBrush;
    RegisterClassW(&pageClass);
    pageClass.lpfnWndProc=helpProc;pageClass.lpszClassName=L"ArgentHelpPage";RegisterClassW(&pageClass);
    auto bindingPath=root/L"assets/argent-controller-bindings.png";
    if(!std::filesystem::exists(bindingPath))bindingPath=std::filesystem::path(__FILE__).parent_path().parent_path()/L"assets/argent-controller-bindings.png";
    if(std::filesystem::exists(bindingPath))controllerBindingImage=Gdiplus::Image::FromFile(bindingPath.c_str(),FALSE);

    WNDCLASSW wc{};
    wc.lpfnWndProc = windowProc;
    wc.hInstance = instance;
    wc.lpszClassName = L"ArgentLauncher";
    wc.hCursor = LoadCursor(nullptr, IDC_ARROW);
    wc.hIcon = static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(201),IMAGE_ICON,
        GetSystemMetrics(SM_CXICON),GetSystemMetrics(SM_CYICON),LR_SHARED));
    wc.hbrBackground = backgroundBrush;
    RegisterClassW(&wc);

    HWND hwnd = CreateWindowW(wc.lpszClassName, L"KHARVOX: ARGENT Launcher",
        WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX,
        CW_USEDEFAULT, CW_USEDEFAULT, 916, 760, nullptr, nullptr, instance, nullptr);
    SendMessageW(hwnd,WM_SETICON,ICON_BIG,reinterpret_cast<LPARAM>(wc.hIcon));
    SendMessageW(hwnd,WM_SETICON,ICON_SMALL,reinterpret_cast<LPARAM>(LoadImageW(instance,
        MAKEINTRESOURCEW(201),IMAGE_ICON,GetSystemMetrics(SM_CXSMICON),GetSystemMetrics(SM_CYSMICON),LR_SHARED)));
    if(selfTest){
      settingsReady=false;
      int result=0;
      try{
        auto checked=[](bool b,int line){if(!b)throw std::runtime_error("launcher integration test failed at line "+std::to_string(line));};
#define require(...) checked((__VA_ARGS__),__LINE__)
        require(SendMessageW(hwnd,WM_GETICON,ICON_BIG,0)&&SendMessageW(hwnd,WM_GETICON,ICON_SMALL,0));
        for(int size:{16,20,24,32,40,48,64,128,256}){
            auto icon=static_cast<HICON>(LoadImageW(instance,MAKEINTRESOURCEW(201),IMAGE_ICON,size,size,0));
            require(icon!=nullptr);ICONINFO info{};require(GetIconInfo(icon,&info));
            BITMAP bitmap{};require(GetObjectW(info.hbmColor,sizeof(bitmap),&bitmap));
            require(bitmap.bmWidth==size&&bitmap.bmHeight==size);
            DeleteObject(info.hbmColor);DeleteObject(info.hbmMask);DestroyIcon(icon);
        }
        require(!advancedTabs&&!(GetWindowLongPtrW(tabButtons[3],GWL_STYLE)&WS_VISIBLE)&&!(GetWindowLongPtrW(tabButtons[4],GWL_STYLE)&WS_VISIBLE));
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdCredit,BN_CLICKED),0);require(!advancedTabs);
        BYTE keyboard[256]{};GetKeyboardState(keyboard);BYTE shifted[256];CopyMemory(shifted,keyboard,256);shifted[VK_SHIFT]|=0x80;SetKeyboardState(shifted);
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdCredit,BN_CLICKED),0);SetKeyboardState(keyboard);require(advancedTabs);
        showTab(4);layoutTabs(false);require(activeTab==0);
        for(int i=5;i<7;++i){showTab(i);SendMessageW(tabPages[i],WM_KEYDOWN,VK_END,0);require(helpStates[i-5].scroll>0);SendMessageW(tabPages[i],WM_KEYDOWN,VK_HOME,0);require(helpStates[i-5].scroll==0);}showTab(0);
        require(controllerBindingImage&&controllerBindingImage->GetLastStatus()==Gdiplus::Ok);
        // Render the actual native help layout as an inspection artifact.
        for(int i=0;i<2;++i){
            HDC dc=GetDC(hwnd);HDC memory=CreateCompatibleDC(dc);HBITMAP bitmap=CreateCompatibleBitmap(dc,773,900);auto old=SelectObject(memory,bitmap);
            RECT area{0,0,773,900};FillRect(memory,&area,backgroundBrush);paintHelp(memory,773,helpStates[i],true);
            {Gdiplus::Bitmap output(bitmap,nullptr);CLSID encoder{0x557cf406,0x1a04,0x11d3,{0x9a,0x73,0x00,0x00,0xf8,0x1e,0xf3,0x2e}};
             output.Save((std::filesystem::current_path()/(i?L"launcher-instructions-preview.png":L"launcher-bindings-preview.png")).c_str(),&encoder,nullptr);}
            SelectObject(memory,old);DeleteObject(bitmap);DeleteDC(memory);ReleaseDC(hwnd,dc);
        }
        require(GetParent(desktopMirror)==tabPages[0]&&!isChecked(desktopMirror));
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdDesktopMirror,BN_CLICKED),reinterpret_cast<LPARAM>(desktopMirror));require(isChecked(desktopMirror));
        SendMessageW(mirrorResolution,CB_SETCURSEL,3,0);
        saveSettings();setChecked(desktopMirror,false);SendMessageW(mirrorResolution,CB_SETCURSEL,0,0);loadSettings();require(isChecked(desktopMirror)&&selected(mirrorResolution)==3);
        SendMessageW(mirrorResolution,CB_SETCURSEL,1,0);
        setChecked(desktopMirror,false);saveSettings();
        require(!isChecked(renderingLogging)&&!isChecked(extendedLogging));
        require(GetParent(captureShaders)==tabPages[3]&&!isChecked(captureShaders));
        setChecked(renderingLogging,true);require(isChecked(extendedLogging)&&!isChecked(captureShaders));
        setChecked(extendedLogging,false);require(!isChecked(renderingLogging));

        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdRenderingLogging,BN_CLICKED),reinterpret_cast<LPARAM>(renderingLogging));require(isChecked(extendedLogging)&&isChecked(renderingLogging));
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdExtendedLogging,BN_CLICKED),reinterpret_cast<LPARAM>(extendedLogging));require(!isChecked(extendedLogging)&&!isChecked(renderingLogging));
        require(GetParent(disableVrIntro)==tabPages[0]&&!vrIntroStarted&&!isChecked(disableVrIntro));
        require(!(GetWindowLongPtrW(disableVrIntro,GWL_STYLE)&WS_VISIBLE));
        for(int id:{101,102,104,105,106,107})require(FindResourceW(instance,MAKEINTRESOURCEW(id),RT_RCDATA)!=nullptr);
        {std::wofstream introLog(root/L"intro-test.log");settingsReady=true;
         require(!playNativeVrIntro(introLog,L"--intro-self-test-fail"));
         require(vrIntroStarted&&(GetWindowLongPtrW(disableVrIntro,GWL_STYLE)&WS_VISIBLE)&&!introThreadId);
         require(playNativeVrIntro(introLog,L"--intro-self-test"));
         SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdDisableVrIntro,BN_CLICKED),reinterpret_cast<LPARAM>(disableVrIntro));
         require(isChecked(disableVrIntro));
         vrIntroStarted=false;setChecked(disableVrIntro,false);loadSettings();
         require(vrIntroStarted&&isChecked(disableVrIntro));
         require(playNativeVrIntro(introLog,L"--intro-self-test-fail")); // Disabled: no child launched.
         settingsReady=false;
        }
        {
            std::map<std::wstring,std::wstring> env{{L"ARGENT_CAPTURE_DIRECTORY",L"inherited"}};
            const auto package=root/L"capture-test";
            require(configureShaderCapture(env,false,package,L"off").empty());
            require(env.at(L"ARGENT_CAPTURE_DIRECTORY").empty()&&!std::filesystem::exists(package));
            require(env.at(L"ARGENT_WATER_GPU_CAPTURE")==L"0");
            std::filesystem::create_directories(package);
            std::ofstream(package/L"build-info.json")<<"{\"release\":\"test\"}";
            const auto capture=configureShaderCapture(env,true,package,L"on");
            require(env.at(L"ARGENT_CAPTURE_DIRECTORY")==capture.wstring());
            require(env.at(L"ARGENT_WATER_GPU_CAPTURE")==L"1"&&env.at(L"ARGENT_DECAL_GPU_CAPTURE")==L"0");
            require(std::filesystem::exists(capture/L"capture-session.txt")&&std::filesystem::exists(capture/L"build-info.json"));
            bool rejected=false;
            try{configureShaderCapture(env,true,package,L"on");}catch(const std::exception&){rejected=true;}
            require(rejected&&env.at(L"ARGENT_CAPTURE_DIRECTORY").empty());
            const auto blocked=root/L"capture-blocked";
            std::filesystem::create_directories(blocked);std::ofstream(blocked/L"captures").put('x');
            rejected=false;
            try{configureShaderCapture(env,true,blocked,L"failed");}catch(const std::exception&){rejected=true;}
            require(rejected&&env.at(L"ARGENT_CAPTURE_DIRECTORY").empty());
            configureShaderCapture(env,false,package,L"off-again");
            require(env.at(L"ARGENT_CAPTURE_DIRECTORY").empty());
        }
        for(int mode:{0,1,2})for(const auto* inherited:{L"0",L"1"}){
            std::map<std::wstring,std::wstring> env{{L"ARGENT_DLSS_STEREO",inherited},{L"XR_RUNTIME_JSON",L"steamxr_win64.json"}};
            configureStereoDlss(env,mode);
            require(env.at(L"ARGENT_DLSS_STEREO")== (mode==1?L"1":L"0"));
            require(env.at(L"XR_RUNTIME_JSON")==L"steamxr_win64.json");
        }
        require(launcherSettingsPath().parent_path()==root/L"local-settings");
        const auto fixture=root/L"test-install/DOOMEternalx64vk.exe";
        std::filesystem::create_directories(fixture.parent_path());std::ofstream(fixture).put('x');
        require(!microsoftStoreGame(fixture));
        {std::ofstream config(fixture.parent_path()/L"MicrosoftGame.Config");config<<"<Identity Name=\"BethesdaSoftworks.DOOMEternal-PC\"/>";}
        require(microsoftStoreGame(fixture));
        std::filesystem::remove(fixture.parent_path()/L"MicrosoftGame.Config");
        const auto xboxFixture=root/L"XboxGames/Arbitrary Install Name/Content/DOOMEternalx64vk.exe";
        std::filesystem::create_directories(xboxFixture.parent_path());std::ofstream(xboxFixture).put('x');
        {std::ofstream config(xboxFixture.parent_path()/L"MicrosoftGame.Config");config<<"<Identity Name=\"BethesdaSoftworks.DOOMEternal-PC\"/>";}
        require(findMicrosoftStoreGame(root/L"XboxGames")==xboxFixture);
        const auto portableIni=(root/L"assets/launcher.ini").wstring();
        WritePrivateProfileStringW(L"launcher",L"game",fixture.c_str(),portableIni.c_str());
        loadSettings();wchar_t selectedPath[32768]{};GetWindowTextW(gameEdit,selectedPath,32768);
        require(std::wstring(selectedPath)!=fixture.wstring()); // Never import another tester's path.
        SetWindowTextW(gameEdit,fixture.c_str());writeControlsConfig();SetWindowTextW(gameEdit,L"");loadSettings();
        GetWindowTextW(gameEdit,selectedPath,32768);require(std::wstring(selectedPath)==fixture.wstring());
        std::filesystem::remove(fixture);loadSettings();GetWindowTextW(gameEdit,selectedPath,32768);
        require(std::wstring(selectedPath)!=fixture.wstring()); // Stale saved paths trigger discovery.
        require(GetParent(extendedLogging)==tabPages[3]&&!isChecked(extendedLogging)&&loggingArgument().empty());
        require(gpuDiagnostics&&GetParent(gpuDiagnostics)==tabPages[3]&&!isChecked(gpuDiagnostics));
        std::map<std::wstring,std::wstring> diagnosticEnv;
        setChecked(extendedLogging,true);configureGpuDiagnostics(diagnosticEnv,isChecked(gpuDiagnostics));require(diagnosticEnv[L"ARGENT_GPU_DIAGNOSTICS"]==L"0");
        setChecked(extendedLogging,false);configureGpuDiagnostics(diagnosticEnv,true);require(diagnosticEnv[L"ARGENT_GPU_DIAGNOSTICS"]==L"1");
        if(cleanRelease)require(captureShaders&&!disableAa&&!simulator&&tabButtons[3]);
        require(GetParent(cinematics3d)==tabPages[0]&&!isChecked(cinematics3d));
        wchar_t cinematicLabel[32]{};GetWindowTextW(cinematics3d,cinematicLabel,32);require(std::wstring(cinematicLabel)==L"3D Cinematics");
        wchar_t label[64]{};GetWindowTextW(renderScale,label,64);require(std::wstring(label)==L"100 %");
        GetWindowTextW(extendedLogging,label,64);require(std::wstring(label)==L"Extended Logging");
        auto checkRange=[&](HWND combo,int minimum,int maximum){
          COMBOBOXINFO info{sizeof(info)};require(GetComboBoxInfo(combo,&info)!=FALSE);
          require((GetWindowLongPtrW(info.hwndList,GWL_STYLE)&WS_VSCROLL)!=0);
          require(IsWindowEnabled(combo));
          require(SendMessageW(combo,CB_GETCOUNT,0,0)==(maximum-minimum)/5+1);
          for(int i=0;i<=(maximum-minimum)/5;++i){wchar_t text[32]{};SendMessageW(combo,CB_GETLBTEXT,i,reinterpret_cast<LPARAM>(text));require(_wtoi(text)==minimum+i*5);}
        };
        require(GetParent(calibrationMode)==tabPages[4]&&isChecked(showHands));
        require(calibrationProfiles.back()=="sentinel_hammer"&&SendMessageW(calibrationProfile,CB_GETCOUNT,0,0)==calibrationProfiles.size());
        require(GetParent(handsJump)==tabPages[2]&&isChecked(handsJump));
        setChecked(handsJump,false);writeControlsConfig();setChecked(handsJump,true);loadSettings();require(!isChecked(handsJump));
        setChecked(handsJump,true);writeControlsConfig();
        {std::ofstream stale(root/L"assets/argent_controls.cfg",std::ios::app);stale<<"\nlaser_sight 1\n";}
        loadSettings();require(controlsSettings().at("laser_sight")=="0");writeControlsConfig();
        for(auto pivot:weaponPivot)require(GetParent(pivot)==tabPages[4]&&SendMessageW(pivot,CB_GETCOUNT,0,0)==121);
        SendMessageW(weaponPivot[0],CB_SETCURSEL,40,0);SendMessageW(weaponPivot[1],CB_SETCURSEL,65,0);SendMessageW(weaponPivot[2],CB_SETCURSEL,57,0);
        writeControlsConfig();for(auto pivot:weaponPivot)SendMessageW(pivot,CB_SETCURSEL,60,0);
        loadSettings();require(selected(weaponPivot[0])==40&&selected(weaponPivot[1])==65&&selected(weaponPivot[2])==57);
        SendMessageW(weaponPivot[1],CB_SETCURSEL,60,0);SendMessageW(weaponPivot[2],CB_SETCURSEL,60,0);

        SendMessageW(calibrationMode,CB_SETCURSEL,1,0);SendMessageW(calibrationProfile,CB_SETCURSEL,1,0);
        writeControlsConfig();SendMessageW(calibrationMode,CB_SETCURSEL,0,0);loadSettings();require(selected(calibrationMode)==1&&selected(calibrationProfile)==1);
        SendMessageW(calibrationMode,CB_SETCURSEL,0,0);SendMessageW(calibrationProfile,CB_SETCURSEL,0,0);writeControlsConfig();
        // Selecting a target starts preview; APPLY snapshots its identity and
        // reports success only after the runtime acknowledges the request.
        SendMessageW(calibrationMode,CB_SETCURSEL,2,0);SendMessageW(calibrationProfile,CB_SETCURSEL,4,0);
        setChecked(dominantHand,false);
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdApplyCalibration,BN_CLICKED),reinterpret_cast<LPARAM>(GetDlgItem(tabPages[4],IdApplyCalibration)));
        require(calibrationApplyWaiting&&calibrationApplyCommand.mode=="weapon"&&calibrationApplyCommand.profile=="rocket_launcher"&&!calibrationApplyCommand.left);
        require(controlsSettings().at("calibration_apply_profile")=="rocket_launcher");
        {std::ofstream ack(root/L"calibration_apply_status.txt");ack<<calibrationApplyCommand.revision<<" 1 weapon rocket_launcher_right";}
        SendMessageW(hwnd,WM_TIMER,1,0);require(!calibrationApplyWaiting);
        SendMessageW(calibrationMode,CB_SETCURSEL,0,0);SendMessageW(calibrationProfile,CB_SETCURSEL,0,0);writeControlsConfig();
        checkRange(renderScale,40,200);checkRange(smoothSpeed,200,300);checkRange(snapAngle,45,90);
        const auto testIni=launcherSettingsPath().wstring();
        WritePrivateProfileStringW(L"launcher",L"renderScalePercent",nullptr,testIni.c_str());
        WritePrivateProfileStringW(L"launcher",L"renderScale",L"3",testIni.c_str());loadSettings();
        GetWindowTextW(renderScale,label,64);require(std::wstring(label)==L"80 %");

        SendMessageW(renderScale,CB_SETCURSEL,2,0);setChecked(fsr,true);
        SendMessageW(turnMode,CB_SETCURSEL,0,0);SendMessageW(smoothSpeed,CB_SETCURSEL,20,0);
        setChecked(dominantHand,true);SendMessageW(killSpeed,CB_SETCURSEL,9,0);SendMessageW(killHands,CB_SETCURSEL,0,0);setChecked(physicalKill,true);
        writeControlsConfig();
        std::ifstream config(root/L"assets/argent_controls.cfg");std::string content((std::istreambuf_iterator<char>(config)),{});config.close();
        for(auto expected:{"hand left 0.1 0 0 0 0 0","dominant left","turn smooth","smooth_turn_speed 300","physical_glory_kill_speed 2.8","physical_glory_kill_hands left","crouch_enabled 0","automatic_presentation 1","cinematics_quad 1","sync_immersive 1","movement_immersive 1","shoulder_chainsaw 1","virtual_gunstock 1","bhaptics_enabled 0","psvr2_adaptive_triggers 0"})require(content.find(expected)!=std::string::npos);
        setChecked(fsr,false);SendMessageW(renderScale,CB_SETCURSEL,5,0);setChecked(dominantHand,false);loadSettings();
        require(isChecked(fsr)&&selected(renderScale)==2&&isChecked(dominantHand)&&selected(smoothSpeed)==20&&selected(killHands)==0);
        require(GetParent(leftHandSwap)==tabPages[1]&&IsWindowEnabled(leftHandSwap)&&selected(leftHandSwap)==0&&(GetWindowLongPtrW(leftHandSwap,GWL_STYLE)&WS_VISIBLE)&&(GetWindowLongPtrW(leftHandLayoutLabel,GWL_STYLE)&WS_VISIBLE));
        SendMessageW(leftHandSwap,CB_SETCURSEL,1,0);writeControlsConfig();SendMessageW(leftHandSwap,CB_SETCURSEL,0,0);loadSettings();require(selected(leftHandSwap)==1);
        setChecked(dominantHand,false);writeControlsConfig();loadSettings();require(!IsWindowEnabled(leftHandSwap)&&selected(leftHandSwap)==1&&!(GetWindowLongPtrW(leftHandSwap,GWL_STYLE)&WS_VISIBLE)&&!(GetWindowLongPtrW(leftHandLayoutLabel,GWL_STYLE)&WS_VISIBLE));
        setChecked(dominantHand,true);SendMessageW(leftHandSwap,CB_SETCURSEL,0,0);writeControlsConfig();loadSettings();
        SendMessageW(turnMode,CB_SETCURSEL,1,0);SendMessageW(snapAngle,CB_SETCURSEL,9,0);writeControlsConfig();SendMessageW(snapAngle,CB_SETCURSEL,0,0);loadSettings();require(selected(snapAngle)==9&&selected(turnMode)==1);
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdExtendedLogging,BN_CLICKED),reinterpret_cast<LPARAM>(extendedLogging));
        require(isChecked(extendedLogging)&&loggingArgument()==L" -ExtendedLogging");
        writeControlsConfig();setChecked(extendedLogging,false);loadSettings();require(isChecked(extendedLogging));
        SendMessageW(hwnd,WM_COMMAND,MAKEWPARAM(IdExtendedLogging,BN_CLICKED),reinterpret_cast<LPARAM>(extendedLogging));
        writeControlsConfig();setChecked(extendedLogging,true);loadSettings();require(!isChecked(extendedLogging)&&loggingArgument().empty());
        settingsReady=true;
        // Real notifications must persist every control without PLAY or APPLY.
        for(auto control:{dominantHand,handsJump,cinematics3d,fsr,physicalKill,virtualGunstock,bhaptics,psvr2Triggers,extendedLogging,gpuDiagnostics,showHands,disableAa,captureShaders,simulator}){
            if(!control)continue;
            for(int repeat=0;repeat<2;++repeat){
                const bool expected=!isChecked(control);
                SendMessageW(GetParent(control),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(control),BN_CLICKED),reinterpret_cast<LPARAM>(control));
                setChecked(control,!expected);loadSettings();require(isChecked(control)==expected);
            }
        }
        for(auto control:{renderScale,turnMode,snapAngle,movement,leftHandSwap,smoothSpeed,killSpeed,killHands,calibrationMode,calibrationProfile,weaponPivot[0],weaponPivot[1],weaponPivot[2]}){
            const int expected=(selected(control)+1)%int(SendMessageW(control,CB_GETCOUNT,0,0));
            SendMessageW(control,CB_SETCURSEL,expected,0);
            SendMessageW(GetParent(control),WM_COMMAND,MAKEWPARAM(GetDlgCtrlID(control),CBN_SELCHANGE),reinterpret_cast<LPARAM>(control));
            SendMessageW(control,CB_SETCURSEL,0,0);loadSettings();require(selected(control)==expected);
        }
        std::ofstream(fixture).put('x');SetWindowTextW(gameEdit,fixture.c_str());
        settingsReady=false;SetWindowTextW(gameEdit,L"");settingsReady=true;loadSettings();
        GetWindowTextW(gameEdit,selectedPath,32768);require(std::wstring(selectedPath)==fixture.wstring());
        const auto expectedControls=controlsSettings();const int expectedScale=selected(renderScale);
        const auto originalRoot=root;
        root=originalRoot/L"release-switch/ARGENT-Alpha-Test-r103";std::filesystem::create_directories(root/L"assets");
        std::ofstream(root/L"assets/argent_controls.cfg")<<"dominant right\nturn snap 45\nbhaptics_enabled 0\nshow_hands 0\n";
        const auto sharedIni=launcherSettingsPath();require(sharedIni==originalRoot/L"local-settings/launcher.ini");
        loadSettings();require(controlsSettings()==expectedControls&&selected(renderScale)==expectedScale);
        writeControlsConfig();
        // Migrate only the old release saved together with the shared INI.
        WritePrivateProfileStringW(L"controls",nullptr,nullptr,sharedIni.c_str());
        const auto prior=root.parent_path()/L"ARGENT-Alpha-Test-r102/assets/argent_controls.cfg";
        std::filesystem::create_directories(prior.parent_path());std::ofstream(prior)<<"dominant left\nbhaptics_enabled 1\n";
        std::filesystem::last_write_time(prior,std::filesystem::last_write_time(sharedIni));
        require(legacyControlsPath()==prior);loadSettings();require(isChecked(dominantHand)&&isChecked(bhaptics));
        std::filesystem::last_write_time(prior,std::filesystem::last_write_time(sharedIni)-std::chrono::minutes(1));
        std::filesystem::last_write_time(root/L"assets/argent_controls.cfg",std::filesystem::last_write_time(sharedIni)-std::chrono::minutes(1));
        require(legacyControlsPath()==root/L"assets/argent_controls.cfg");
        saveSettings();
        WritePrivateProfileStringW(L"future",L"keep",L"unchanged",sharedIni.c_str());
        const HANDLE locked=CreateFileW(sharedIni.c_str(),GENERIC_READ,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
        require(locked!=INVALID_HANDLE_VALUE);const bool blockedSave=autoSaveSettings();CloseHandle(locked);require(!blockedSave);
        require(autoSaveSettings());wchar_t retained[32]{};
        GetPrivateProfileStringW(L"future",L"keep",L"",retained,32,sharedIni.c_str());require(std::wstring(retained)==L"unchanged");
        // Closing also saves state that did not issue a notification.
        WritePrivateProfileStringW(L"launcher",L"renderer",L"0",sharedIni.c_str());
        SendMessageW(hwnd,WM_CLOSE,0,0);
        require(!IsWindow(hwnd)&&GetPrivateProfileIntW(L"launcher",L"renderer",-1,sharedIni.c_str())==1);
#undef require
      }catch(const std::exception& error){std::ofstream(isolatedSettingsRoot/L"selftest-failure.txt")<<error.what();result=1;}
       catch(...){result=1;}
      if(IsWindow(hwnd))DestroyWindow(hwnd);return result;
    }
    if(arguments&&std::wstring(arguments)==L"--startup-check"){
        runLauncher(true);
        DestroyWindow(hwnd);
        return 0;
    }
    ShowWindow(hwnd, show);
    UpdateWindow(hwnd);

    MSG msg{};
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    return 0;
}
