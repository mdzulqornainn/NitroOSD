// ============================================================================
//  NITRO OSD  -  OSD untuk Acer Nitro Lite 16
//  Caps Lock, Num Lock, mute mikrofon, touchpad, backlight keyboard (Off/1/2)
//  C++17 Win32 + GDI+ (layered window, per-pixel alpha). Ubin ikon putih + jendela pengaturan gelap.
// ============================================================================
#define UNICODE
#define _UNICODE
#define NOMINMAX
#include <windows.h>
#include <windowsx.h>
#include <objidl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <shellapi.h>
#include <setupapi.h>
#include <cfgmgr32.h>
#include <wbemidl.h>
#include <mmdeviceapi.h>
#include <endpointvolume.h>
#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

using namespace Gdiplus;
using std::vector;
using std::wstring;

// ---------------------------------------------------------------- util
static wstring Fmt(const wchar_t* f, ...) {
    wchar_t b[512];
    va_list a;
    va_start(a, f);
    _vsnwprintf(b, 511, f, a);
    va_end(a);
    b[511] = 0;
    return b;
}
static wstring ExePath() {
    wchar_t p[MAX_PATH * 2];
    GetModuleFileNameW(nullptr, p, MAX_PATH * 2);
    return p;
}
static wstring DataDir() {
    wchar_t p[MAX_PATH] = {};
    GetEnvironmentVariableW(L"APPDATA", p, MAX_PATH);
    wstring d = wstring(p) + L"\\NitroOSD";
    CreateDirectoryW(d.c_str(), nullptr);
    return d;
}
static void LogTouch(const wchar_t* f, ...) {
    wchar_t b[512];
    va_list a;
    va_start(a, f);
    _vsnwprintf(b, 511, f, a);
    va_end(a);
    b[511] = 0;
    wstring p = DataDir() + L"\\touch.log";
    FILE* fp = _wfopen(p.c_str(), L"a, ccs=UTF-8");
    if (!fp) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(fp, L"%02d:%02d:%02d.%03d  %ls\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, b);
    fclose(fp);
}
static int Run(const wstring& cmd, std::string* out = nullptr, DWORD timeout = 20000) {
    SECURITY_ATTRIBUTES sa{sizeof sa, nullptr, TRUE};
    HANDLE rd = nullptr, wr = nullptr;
    if (!CreatePipe(&rd, &wr, &sa, 0)) return -1;
    SetHandleInformation(rd, HANDLE_FLAG_INHERIT, 0);
    STARTUPINFOW si{};
    si.cb = sizeof si;
    si.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    si.wShowWindow = SW_HIDE;
    si.hStdOutput = wr;
    si.hStdError = wr;
    PROCESS_INFORMATION pi{};
    wstring c = cmd;
    BOOL ok = CreateProcessW(nullptr, &c[0], nullptr, nullptr, TRUE, CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
    CloseHandle(wr);
    if (!ok) { CloseHandle(rd); return -1; }
    char buf[512];
    DWORD n;
    while (ReadFile(rd, buf, sizeof buf, &n, nullptr) && n)
        if (out) out->append(buf, n);
    WaitForSingleObject(pi.hProcess, timeout);
    DWORD code = (DWORD)-1;
    GetExitCodeProcess(pi.hProcess, &code);
    CloseHandle(pi.hProcess);
    CloseHandle(pi.hThread);
    CloseHandle(rd);
    return (int)code;
}
static std::string B64(const unsigned char* d, size_t n) {
    static const char* t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string o;
    for (size_t i = 0; i < n; i += 3) {
        unsigned v = d[i] << 16;
        if (i + 1 < n) v |= d[i + 1] << 8;
        if (i + 2 < n) v |= d[i + 2];
        o += t[(v >> 18) & 63];
        o += t[(v >> 12) & 63];
        o += (i + 1 < n) ? t[(v >> 6) & 63] : '=';
        o += (i + 2 < n) ? t[v & 63] : '=';
    }
    return o;
}
static int RunPS(const wstring& script, DWORD timeout = 30000) {
    std::string b = B64((const unsigned char*)script.data(), script.size() * 2);
    return Run(L"powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand " + wstring(b.begin(), b.end()), nullptr, timeout);
}

// ---------------------------------------------------------------- pengaturan
enum { A_MIC = 0, A_TOUCH = 1, A_BL = 2 };
struct Settings {
    int vk[3] = {0, 0, 0}, scan[3] = {0, 0, 0};
    int wb1[3] = {0, 131, 0}, wb2[3] = {0, 0, 0};  // event WMI Acer (APGeEvent): byte[1], byte[2]
    int durMs = 1500;
    bool posTop = false;
    int bl = 0;  // 0 mati, 1, 2
    wstring cmd[3];
    bool swallow = true;
} gS;
static const wchar_t* BIND_NAME[3] = {L"Mic", L"Touchpad", L"Backlight"};
static wstring IniPath() { return DataDir() + L"\\settings.ini"; }
static void LoadSettings() {
    wstring p = IniPath();
    for (int i = 0; i < 3; i++) {
        gS.vk[i] = GetPrivateProfileIntW(L"Keys", Fmt(L"%s_vk", BIND_NAME[i]).c_str(), 0, p.c_str());
        gS.scan[i] = GetPrivateProfileIntW(L"Keys", Fmt(L"%s_scan", BIND_NAME[i]).c_str(), 0, p.c_str());
        gS.wb1[i] = GetPrivateProfileIntW(L"WMI", Fmt(L"%s_b1", BIND_NAME[i]).c_str(), i == A_TOUCH ? 131 : 0, p.c_str());
        gS.wb2[i] = GetPrivateProfileIntW(L"WMI", Fmt(L"%s_b2", BIND_NAME[i]).c_str(), 0, p.c_str());
        wchar_t b[1024] = {};
        GetPrivateProfileStringW(L"Backlight", Fmt(L"Cmd%d", i).c_str(), L"", b, 1024, p.c_str());
        gS.cmd[i] = b;
    }
    gS.durMs = GetPrivateProfileIntW(L"OSD", L"DurasiMs", 1500, p.c_str());
    gS.posTop = GetPrivateProfileIntW(L"OSD", L"PosisiAtas", 0, p.c_str()) != 0;
    gS.swallow = GetPrivateProfileIntW(L"Keys", L"Swallow", 1, p.c_str()) != 0;
    gS.bl = std::clamp((int)GetPrivateProfileIntW(L"Backlight", L"Level", 0, p.c_str()), 0, 2);
}
static void SaveSettings() {
    wstring p = IniPath();
    for (int i = 0; i < 3; i++) {
        WritePrivateProfileStringW(L"Keys", Fmt(L"%s_vk", BIND_NAME[i]).c_str(), Fmt(L"%d", gS.vk[i]).c_str(), p.c_str());
        WritePrivateProfileStringW(L"Keys", Fmt(L"%s_scan", BIND_NAME[i]).c_str(), Fmt(L"%d", gS.scan[i]).c_str(), p.c_str());
        WritePrivateProfileStringW(L"WMI", Fmt(L"%s_b1", BIND_NAME[i]).c_str(), Fmt(L"%d", gS.wb1[i]).c_str(), p.c_str());
        WritePrivateProfileStringW(L"WMI", Fmt(L"%s_b2", BIND_NAME[i]).c_str(), Fmt(L"%d", gS.wb2[i]).c_str(), p.c_str());
        WritePrivateProfileStringW(L"Backlight", Fmt(L"Cmd%d", i).c_str(), gS.cmd[i].c_str(), p.c_str());
    }
    WritePrivateProfileStringW(L"OSD", L"DurasiMs", Fmt(L"%d", gS.durMs).c_str(), p.c_str());
    WritePrivateProfileStringW(L"OSD", L"PosisiAtas", gS.posTop ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Keys", L"Swallow", gS.swallow ? L"1" : L"0", p.c_str());
    WritePrivateProfileStringW(L"Backlight", L"Level", Fmt(L"%d", gS.bl).c_str(), p.c_str());
}

// ---------------------------------------------------------------- mikrofon (Core Audio)
static const GUID K_CLSID_MMDevEnum = {0xBCDE0395, 0xE52F, 0x467C, {0x8E, 0x3D, 0xC4, 0x57, 0x92, 0x91, 0x69, 0x2E}};
static const GUID K_IID_MMDevEnum = {0xA95664D2, 0x9614, 0x4F35, {0xA7, 0x46, 0xDE, 0x8D, 0xB6, 0x36, 0x17, 0xE6}};
static const GUID K_IID_EPVol = {0x5CDF2C82, 0x841E, 0x4546, {0x97, 0x22, 0x0C, 0xF7, 0x40, 0x78, 0x22, 0x9A}};

// mode 0: baca (return -1 tidak ada mic, 0 hidup, 1 semua mute); mode 1: mute; mode 2: unmute
static int MicOp(int mode) {
    IMMDeviceEnumerator* en = nullptr;
    if (FAILED(CoCreateInstance(K_CLSID_MMDevEnum, nullptr, CLSCTX_ALL, K_IID_MMDevEnum, (void**)&en)) || !en) return -1;
    IMMDeviceCollection* col = nullptr;
    int count = 0, muted = 0;
    if (SUCCEEDED(en->EnumAudioEndpoints(eCapture, DEVICE_STATE_ACTIVE, &col)) && col) {
        UINT n = 0;
        col->GetCount(&n);
        for (UINT i = 0; i < n; i++) {
            IMMDevice* d = nullptr;
            if (FAILED(col->Item(i, &d)) || !d) continue;
            IAudioEndpointVolume* v = nullptr;
            if (SUCCEEDED(d->Activate(K_IID_EPVol, CLSCTX_ALL, nullptr, (void**)&v)) && v) {
                if (mode == 1) v->SetMute(TRUE, nullptr);
                else if (mode == 2) v->SetMute(FALSE, nullptr);
                BOOL m = FALSE;
                if (SUCCEEDED(v->GetMute(&m))) { count++; if (m) muted++; }
                v->Release();
            }
            d->Release();
        }
        col->Release();
    }
    en->Release();
    if (!count) return -1;
    return muted == count ? 1 : 0;
}

// ---------------------------------------------------------------- touchpad (PnP)
static vector<wstring> gTouchIds;
static std::atomic<bool> gTouchBusy{false};
static bool ContainsI(const wstring& h, const wchar_t* n) {
    wstring a = h, b = n;
    for (auto& c : a) c = (wchar_t)towlower(c);
    for (auto& c : b) c = (wchar_t)towlower(c);
    return a.find(b) != wstring::npos;
}
static void FindTouchpads() {
    gTouchIds.clear();
    HDEVINFO h = SetupDiGetClassDevsW(nullptr, L"HID", nullptr, DIGCF_PRESENT | DIGCF_ALLCLASSES);
    if (h == INVALID_HANDLE_VALUE) return;
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof d;
    for (DWORD i = 0; SetupDiEnumDeviceInfo(h, i, &d); i++) {
        bool hit = false;
        wchar_t hw[4096] = {};
        if (SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_HARDWAREID, nullptr, (PBYTE)hw, sizeof hw - 4, nullptr))
            for (wchar_t* p = hw; *p; p += wcslen(p) + 1)
                if (ContainsI(p, L"UP:000D_U:0005")) hit = true;
        wchar_t nm[512] = {};
        if (!hit && SetupDiGetDeviceRegistryPropertyW(h, &d, SPDRP_DEVICEDESC, nullptr, (PBYTE)nm, sizeof nm - 2, nullptr))
            if (ContainsI(nm, L"touch pad") || ContainsI(nm, L"touchpad")) hit = true;
        if (hit) {
            wchar_t id[MAX_DEVICE_ID_LEN] = {};
            if (SetupDiGetDeviceInstanceIdW(h, &d, id, MAX_DEVICE_ID_LEN, nullptr)) gTouchIds.push_back(id);
        }
    }
    SetupDiDestroyDeviceInfoList(h);
}
// API resmi Windows (SPI_GET/SETTOUCHPADPARAMETERS) - sama dengan yang dipakai app Settings, efeknya instan dan sinkron
static UINT gTpSize = 0;
static BYTE gTpBuf[512];
static bool TpSpiGet() {
    memset(gTpBuf, 0, sizeof gTpBuf);
    ((UINT*)gTpBuf)[0] = 1;  // versionNumber
    return gTpSize && SystemParametersInfoW(0x00AE, gTpSize, gTpBuf, 0);
}
static bool TpSpiProbe() {
    for (UINT sz = 32; sz <= 200; sz += 4) {
        memset(gTpBuf, 0, sizeof gTpBuf);
        ((UINT*)gTpBuf)[0] = 1;
        if (SystemParametersInfoW(0x00AE, sz, gTpBuf, 0)) {
            UINT bits = ((UINT*)gTpBuf)[3];
            bool present = (bits & 1) != 0;
            LogTouch(L"SPI touchpad: size=%u present=%d enabled=%d", sz, present, (bits >> 3) & 1);
            if (!present) return false;
            gTpSize = sz;
            return true;
        }
    }
    LogTouch(L"SPI touchpad: tidak tersedia");
    return false;
}
static bool TpSpiSet(bool enable) {
    if (!TpSpiGet()) return false;
    UINT* w = &((UINT*)gTpBuf)[3];
    if (enable) *w |= (1u << 3); else *w &= ~(1u << 3);
    BOOL ok = SystemParametersInfoW(0x00AF, gTpSize, gTpBuf, SPIF_UPDATEINIFILE | SPIF_SENDCHANGE);
    LogTouch(L"SPI set enable=%d -> %d (err=%lu)", enable, ok, ok ? 0 : GetLastError());
    return ok != 0;
}
// -1 tidak ada, 0 aktif, 1 nonaktif
static std::atomic<int> gSpiOk{-1};  // -1 belum dicoba, 1 bekerja (bacaan ikut berubah), 0 tidak bekerja di perangkat ini
static bool DeviceDisabled() {
    for (auto& id : gTouchIds) {
        DEVINST inst = 0;
        if (CM_Locate_DevNodeW(&inst, (DEVINSTID_W)id.c_str(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) continue;
        ULONG st = 0, pr = 0;
        if (CM_Get_DevNode_Status(&st, &pr, inst, 0) == CR_SUCCESS && (st & DN_HAS_PROBLEM) && pr == CM_PROB_DISABLED) return true;
    }
    return false;
}
// -1 tidak ada, 0 aktif, 1 nonaktif
static int TouchState() {
    if (gTouchIds.empty() && !gTpSize) return -1;
    if (DeviceDisabled()) return 1;
    if (gTpSize && gSpiOk != 0 && TpSpiGet()) return (((UINT*)gTpBuf)[3] >> 3) & 1 ? 0 : 1;
    return 0;
}
// Cepat (tanpa proses pnputil): DIF_PROPERTYCHANGE lewat SetupAPI. true jika semua perangkat berhasil.
static bool TouchSetFast(bool enable) {
    int ok = 0;
    for (auto& id : gTouchIds) {
        HDEVINFO s = SetupDiCreateDeviceInfoList(nullptr, nullptr);
        if (s == INVALID_HANDLE_VALUE) continue;
        SP_DEVINFO_DATA d{};
        d.cbSize = sizeof d;
        if (SetupDiOpenDeviceInfoW(s, id.c_str(), nullptr, 0, &d)) {
            SP_PROPCHANGE_PARAMS pc{};
            pc.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
            pc.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
            pc.StateChange = enable ? DICS_ENABLE : DICS_DISABLE;
            pc.Scope = DICS_FLAG_GLOBAL;
            pc.HwProfile = 0;
            if (SetupDiSetClassInstallParamsW(s, &d, &pc.ClassInstallHeader, sizeof pc) && SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, s, &d)) ok++;
        }
        SetupDiDestroyDeviceInfoList(s);
    }
    return ok == (int)gTouchIds.size() && ok > 0;
}
static void TouchSetBlocking(bool enable);
static bool DeviceStarted() {
    for (auto& id : gTouchIds) {
        DEVINST inst = 0;
        if (CM_Locate_DevNodeW(&inst, (DEVINSTID_W)id.c_str(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) return false;
        ULONG st = 0, pr = 0;
        if (CM_Get_DevNode_Status(&st, &pr, inst, 0) != CR_SUCCESS || !(st & DN_STARTED) || (st & DN_HAS_PROBLEM)) return false;
    }
    return !gTouchIds.empty();
}
static std::atomic<int> gRawOk{-1};  // -1 belum diketahui, 1 sentuhan terbaca di Raw Input saat aktif, 0 tidak bisa dipakai sebagai sinyal
static bool RawTouchpadPresent() {  // perangkat HID Precision Touchpad (0x0D/0x05) terdaftar di Raw Input = benar-benar hidup
    UINT n = 0;
    if (GetRawInputDeviceList(nullptr, &n, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1 || !n) return false;
    std::vector<RAWINPUTDEVICELIST> l(n);
    if (GetRawInputDeviceList(l.data(), &n, sizeof(RAWINPUTDEVICELIST)) == (UINT)-1) return false;
    for (UINT i = 0; i < n; i++) {
        if (l[i].dwType != RIM_TYPEHID) continue;
        RID_DEVICE_INFO info{};
        info.cbSize = sizeof info;
        UINT cb = sizeof info;
        if (GetRawInputDeviceInfoW(l[i].hDevice, RIDI_DEVICEINFO, &info, &cb) == (UINT)-1) continue;
        if (info.hid.usUsagePage == 0x0D && info.hid.usUsage == 0x05) return true;
    }
    return false;
}
static bool WaitRaw(int ms) {
    for (int t = 0; t < ms; t += 50) { if (RawTouchpadPresent()) return true; Sleep(50); }
    return RawTouchpadPresent();
}
static bool PropChange(const wstring& id) {  // restart satu devnode
    HDEVINFO s = SetupDiCreateDeviceInfoList(nullptr, nullptr);
    if (s == INVALID_HANDLE_VALUE) return false;
    bool ok = false;
    SP_DEVINFO_DATA d{};
    d.cbSize = sizeof d;
    if (SetupDiOpenDeviceInfoW(s, id.c_str(), nullptr, 0, &d)) {
        SP_PROPCHANGE_PARAMS pc{};
        pc.ClassInstallHeader.cbSize = sizeof(SP_CLASSINSTALL_HEADER);
        pc.ClassInstallHeader.InstallFunction = DIF_PROPERTYCHANGE;
        pc.StateChange = DICS_PROPCHANGE;
        pc.Scope = DICS_FLAG_GLOBAL;
        ok = SetupDiSetClassInstallParamsW(s, &d, &pc.ClassInstallHeader, sizeof pc) && SetupDiCallClassInstaller(DIF_PROPERTYCHANGE, s, &d);
    }
    SetupDiDestroyDeviceInfoList(s);
    return ok;
}
static wstring ParentId(const wstring& id) {
    DEVINST inst = 0, par = 0;
    wchar_t buf[MAX_DEVICE_ID_LEN] = {};
    if (CM_Locate_DevNodeW(&inst, (DEVINSTID_W)id.c_str(), CM_LOCATE_DEVNODE_NORMAL) != CR_SUCCESS) return L"";
    if (CM_Get_Parent(&par, inst, 0) != CR_SUCCESS) return L"";
    if (CM_Get_Device_IDW(par, buf, MAX_DEVICE_ID_LEN, 0) != CR_SUCCESS) return L"";
    return buf;
}
static std::atomic<bool> gTouchOsdLate{false};
static ULONGLONG gTpEnableTick = 0, gTpLastRpt = 0;  // untuk log: kapan touchpad benar-benar mengirim data
static bool TpRegSet(bool enable) {  // sama seperti toggle Touchpad di Settings Windows
    HKEY k = nullptr;
    if (RegCreateKeyExW(HKEY_CURRENT_USER, L"Software\\Microsoft\\Windows\\CurrentVersion\\PrecisionTouchPad\\Status", 0, nullptr, 0, KEY_SET_VALUE, nullptr, &k, nullptr) != ERROR_SUCCESS) return false;
    DWORD v = enable ? 1 : 0;
    LONG r = RegSetValueExW(k, L"Enabled", 0, REG_DWORD, (const BYTE*)&v, sizeof v);
    RegCloseKey(k);
    LogTouch(L"registry Enabled=%d (r=%ld)", (int)v, r);
    return r == ERROR_SUCCESS;
}
static bool SpiWait(int wantBit) {  // tunggu bacaan SPI sama dengan yang diinginkan (maks 300 ms)
    for (int i = 0; i < 6; i++) {
        if (TpSpiGet() && (int)((((UINT*)gTpBuf)[3] >> 3) & 1) == wantBit) return true;
        Sleep(50);
    }
    return false;
}
static void TouchApply(bool enable) {
    ULONGLONG t0 = GetTickCount64();
    if (enable) {
        LogTouch(L"sebelum enable: Raw Input ada=%d, device nonaktif=%d", (int)RawTouchpadPresent(), (int)DeviceDisabled());
        if (DeviceDisabled()) {  // perangkat sempat dimatikan lewat SetupAPI: nyalakan dulu, lalu pulihkan toggle Windows
            bool ok = TouchSetFast(true);
            if (!ok) TouchSetBlocking(true);
            LogTouch(L"device enable ok=%d (%llu ms)", ok, GetTickCount64() - t0);
            for (int i = 0; i < 40 && !TpSpiProbe(); i++) Sleep(100);
            for (int i = 0; i < 100 && !DeviceStarted(); i++) Sleep(50);  // tunggu driver benar-benar jalan
            LogTouch(L"driver siap=%d (%llu ms)", DeviceStarted(), GetTickCount64() - t0);
            // Raw Input sudah terdaftar < 100 ms tapi touchpad belum tentu hidup: restart induk (I2C HID) supaya hardware di-init ulang
            for (auto& id : gTouchIds) {
                wstring p = ParentId(id);
                if (p.empty()) continue;
                bool k = PropChange(p);
                Sleep(300);
                for (int i = 0; i < 80 && !DeviceStarted(); i++) Sleep(50);
                LogTouch(L"restart induk (%s) ok=%d, anak siap=%d (%llu ms)", p.c_str(), k, DeviceStarted(), GetTickCount64() - t0);
            }
            if (gRawOk == -1) gRawOk = WaitRaw(1500) ? 1 : 0;
            if (gRawOk == 1) {  // pastikan touchpad benar-benar hidup (terdaftar di Raw Input); kalau tidak, restart device lalu induknya
                bool alive = WaitRaw(1200);
                LogTouch(L"touchpad terbaca Raw Input=%d (%llu ms)", alive, GetTickCount64() - t0);
                for (auto& id : gTouchIds) {
                    if (alive) break;
                    bool k = PropChange(id);
                    alive = WaitRaw(2000);
                    LogTouch(L"restart device ok=%d -> hidup=%d (%llu ms)", k, alive, GetTickCount64() - t0);
                    if (alive) break;
                    wstring p = ParentId(id);
                    if (!p.empty()) {
                        k = PropChange(p);
                        alive = WaitRaw(3000);
                        LogTouch(L"restart induk ok=%d -> hidup=%d (%llu ms)", k, alive, GetTickCount64() - t0);
                    }
                }
            }
        }
        if (gTpSize && gSpiOk != 0) {
            TpSpiSet(true);
            bool r = SpiWait(1);
            if (!r) { TpRegSet(true); r = SpiWait(1); }
            if (r && gSpiOk < 0) gSpiOk = 1;
            LogTouch(L"toggle Windows aktif terbaca=%d (%llu ms)", r, GetTickCount64() - t0);
        }
        gTpEnableTick = GetTickCount64();
        gTpLastRpt = 0;
        return;
    }
    if (gTpSize && gSpiOk != 0) {
        TpSpiSet(false);
        bool r = SpiWait(0);
        if (!r) { TpRegSet(false); r = SpiWait(0); }
        if (r) { gSpiOk = 1; LogTouch(L"toggle Windows nonaktif ok (%llu ms)", GetTickCount64() - t0); return; }
        gSpiOk = 0;
        TpSpiSet(true);
        TpRegSet(true);
        LogTouch(L"toggle Windows tidak mengubah bacaan -> pakai cara device");
    }
    bool ok = TouchSetFast(false);
    LogTouch(L"SetupAPI disable -> %d (%llu ms)", ok, GetTickCount64() - t0);
    if (!ok) { TouchSetBlocking(false); LogTouch(L"pnputil selesai (%llu ms)", GetTickCount64() - t0); }
    for (int i = 0; i < 12 && TouchState() != 1; i++) Sleep(50);
    LogTouch(L"status sekarang=%d", TouchState());
    if (gRawOk == 1) {  // pastikan touchpad benar-benar hilang dari Raw Input; kalau tidak, matikan lewat pnputil
        bool gone = false;
        for (int i = 0; i < 20 && !(gone = !RawTouchpadPresent()); i++) Sleep(50);
        LogTouch(L"setelah nonaktif: Raw Input hilang=%d (%llu ms)", gone, GetTickCount64() - t0);
        if (!gone) {
            TouchSetBlocking(false);
            gone = WaitRaw(0) ? false : true;
            for (int i = 0; i < 20 && !(gone = !RawTouchpadPresent()); i++) Sleep(50);
            LogTouch(L"pnputil disable -> hilang=%d (%llu ms)", gone, GetTickCount64() - t0);
        }
    }
}
static void TouchSetBlocking(bool enable) {
    for (auto& id : gTouchIds) Run(wstring(L"pnputil /") + (enable ? L"enable" : L"disable") + L"-device \"" + id + L"\"", nullptr, 30000);
}

// ---------------------------------------------------------------- gambar (GDI+)
#define WM_TRAY (WM_APP + 1)
#define WM_ACT (WM_APP + 2)
#define WM_KEYST (WM_APP + 3)
#define WM_TPDONE (WM_APP + 4)
#define WM_LEARNED (WM_APP + 5)
#define WM_DIAG (WM_APP + 6)
#define WM_WMIKEY (WM_APP + 7)
#define WM_REFRESH (WM_APP + 8)
#define WM_TPOBS (WM_APP + 9)
enum { K_CAPS, K_NUM, K_MIC, K_TOUCH, K_BL, K_MSG };
enum : DWORD {
    C_BG = 0xFF1C1C1E, C_W = 0xFFFFFFFF, C_DIM = 0xFF8E8E93, C_LN = 0xFF2C2C2E, C_BD = 0xFF48484A, C_HOV = 0xFF2C2C2E,
    C_OFF = 0x80FFFFFF, C_SUB = 0x99FFFFFF
};

static HWND gMain = nullptr, gOsd = nullptr, gSet = nullptr;
static float gSc = 1.f;
static FontFamily* gFam = nullptr;
static HDC gOsdDC = nullptr;
static HBITMAP gOsdBmp = nullptr, gOsdOld = nullptr;
static int gOsdW = 0, gOsdH = 0, gOsdX = 0, gOsdY = 0;
static ULONGLONG gOsdStart = 0;
static int gOsdDur = 1500;

struct OsdInfo {
    int kind = K_MSG;
    wstring name, state;
    bool on = true;
    int level = -1;  // backlight 0..2
    int dur = 1500;
};

static Color Col(DWORD c) { return Color(c); }
static void RRPath(GraphicsPath& p, float x, float y, float w, float h, float r) {
    float d = r * 2;
    p.AddArc(x, y, d, d, 180, 90);
    p.AddArc(x + w - d, y, d, d, 270, 90);
    p.AddArc(x + w - d, y + h - d, d, d, 0, 90);
    p.AddArc(x, y + h - d, d, d, 90, 90);
    p.CloseFigure();
}
static void FillRR(Graphics& g, DWORD c, float x, float y, float w, float h, float r) {
    GraphicsPath p;
    RRPath(p, x, y, w, h, r);
    SolidBrush b(Col(c));
    g.FillPath(&b, &p);
}
static void StrokeRR(Graphics& g, DWORD c, float sw, float x, float y, float w, float h, float r) {
    GraphicsPath p;
    RRPath(p, x, y, w, h, r);
    Pen pn(Col(c), sw);
    g.DrawPath(&pn, &p);
}
static void FillR(Graphics& g, DWORD c, float x, float y, float w, float h) {
    SolidBrush b(Col(c));
    g.FillRectangle(&b, x, y, w, h);
}
static Font* Fnt(float px, bool bold) {
    static vector<std::pair<int, Font*>> cache;
    int k = (int)(px * 10) * 2 + (bold ? 1 : 0);
    for (auto& p : cache) if (p.first == k) return p.second;
    Font* f = new Font(gFam, px, bold ? FontStyleBold : FontStyleRegular, UnitPixel);
    cache.push_back({k, f});
    return f;
}
static float TextW(Graphics& g, const wstring& s, float px, bool bold) {
    RectF r;
    g.MeasureString(s.c_str(), -1, Fnt(px, bold), PointF(0, 0), &r);
    return r.Width;
}
// ha: 0 kiri, 1 tengah, 2 kanan; selalu rata tengah vertikal di dalam kotak h
static void Txt(Graphics& g, const wstring& s, float px, bool bold, DWORD c, float x, float y, float w, float h, int ha = 0) {
    StringFormat sf(StringFormatFlagsNoWrap | StringFormatFlagsNoClip);
    sf.SetAlignment(ha == 0 ? StringAlignmentNear : ha == 1 ? StringAlignmentCenter : StringAlignmentFar);
    sf.SetLineAlignment(StringAlignmentCenter);
    SolidBrush b(Col(c));
    g.DrawString(s.c_str(), -1, Fnt(px, bold), RectF(x, y, w, h), &sf, &b);
}
// logo keycap: bingkai tuts bulat + permukaan atas yang masuk
static void DrawKeycap(Graphics& g, float x, float y, float s, DWORD outer, DWORD inner) {
    FillRR(g, outer, x, y, s, s, s * 0.27f);
    StrokeRR(g, inner, s * 0.07f, x + s * 0.2f, y + s * 0.16f, s * 0.6f, s * 0.52f, s * 0.14f);
}

// ---------------------------------------------------------------- ikon ubin OSD (kotak 44x44)
static void Slash(Graphics& g, float bx, float by) {
    Pen k(Col(C_BG), 8.f), w(Col(C_W), 3.2f);
    k.SetStartCap(LineCapRound); k.SetEndCap(LineCapRound);
    w.SetStartCap(LineCapRound); w.SetEndCap(LineCapRound);
    g.DrawLine(&k, bx + 6, by + 4, bx + 38, by + 40);
    g.DrawLine(&w, bx + 6, by + 4, bx + 38, by + 40);
}
static void Glyph(Graphics& g, const OsdInfo& o, float bx, float by) {
    DWORD fg = o.on ? C_W : C_OFF;
    Pen p(Col(fg), 2.6f);
    p.SetStartCap(LineCapRound); p.SetEndCap(LineCapRound); p.SetLineJoin(LineJoinRound);
    SolidBrush fb(Col(fg));
    switch (o.kind) {
        case K_CAPS:  // tuts huruf: On = "A" di tuts putih, Off = "a" di tuts kosong
            if (o.on) {
                FillRR(g, C_W, bx + 2, by + 2, 40, 40, 10);
                Txt(g, L"A", 27, true, C_BG, bx + 2, by + 4, 40, 40, 1);
                SolidBrush d(Col(C_BG));
                g.FillEllipse(&d, bx + 31.f, by + 8.f, 5.f, 5.f);
            } else {
                StrokeRR(g, C_OFF, 2.4f, bx + 3.2f, by + 3.2f, 37.6f, 37.6f, 9);
                Txt(g, L"a", 27, true, C_OFF, bx + 2, by + 3, 40, 40, 1);
                Pen d(Col(C_OFF), 1.6f);
                g.DrawEllipse(&d, bx + 31.f, by + 8.f, 5.f, 5.f);
            }
            break;
        case K_NUM: {  // keypad 3x3: On = terisi, Off = kosong + coret
            for (int r = 0; r < 3; r++)
                for (int c = 0; c < 3; c++) {
                    float x = bx + 4 + c * 14.f, y = by + 4 + r * 14.f;
                    if (o.on) FillRR(g, C_W, x, y, 10, 10, 2.6f);
                    else StrokeRR(g, C_OFF, 1.8f, x + 0.9f, y + 0.9f, 8.2f, 8.2f, 2.f);
                }
            if (!o.on) Slash(g, bx, by);
            break;
        }
        case K_MIC: {
            GraphicsPath gp;
            RRPath(gp, bx + 15, by + 2, 14, 24, 7);
            g.FillPath(&fb, &gp);
            g.DrawArc(&p, bx + 8.f, by + 12.f, 28.f, 24.f, 0, 180);
            g.DrawLine(&p, bx + 22, by + 36, bx + 22, by + 41);
            g.DrawLine(&p, bx + 15, by + 41, bx + 29, by + 41);
            if (!o.on) Slash(g, bx, by);
            break;
        }
        case K_TOUCH: {
            StrokeRR(g, fg, 2.6f, bx + 4, by + 7, 36, 30, 6);
            g.DrawLine(&p, bx + 4, by + 28, bx + 40, by + 28);
            g.DrawLine(&p, bx + 22, by + 28, bx + 22, by + 37);
            if (!o.on) Slash(g, bx, by);
            break;
        }
        case K_BL: {
            StrokeRR(g, fg, 2.6f, bx + 3, by + 9, 38, 26, 6);
            for (int i = 0; i < 5; i++) FillR(g, fg, bx + 9 + i * 6.4f, by + 15, 3.6f, 3.6f);
            for (int i = 0; i < 4; i++) FillR(g, fg, bx + 12.f + i * 6.4f, by + 21.5f, 3.6f, 3.6f);
            FillR(g, fg, bx + 13, by + 28, 18, 3.4f);
            if (o.level == 0) Slash(g, bx, by);
            break;
        }
        default: DrawKeycap(g, bx + 2, by + 2, 40, C_W, C_BG); break;
    }
}

// ---------------------------------------------------------------- jendela OSD
static void RenderOsd(const OsdInfo& o) {
    HDC scr = GetDC(nullptr);
    float tw = 0;
    {
        Graphics gm(scr);
        tw = std::max(TextW(gm, o.name, 14, true), TextW(gm, o.state, 13, false));
    }
    float lw = std::max(120.f, tw + 40.f), lh = 120.f;
    int W = (int)(lw * gSc), H = (int)(lh * gSc);
    if (gOsdDC) { SelectObject(gOsdDC, gOsdOld); DeleteObject(gOsdBmp); DeleteDC(gOsdDC); }
    gOsdDC = CreateCompatibleDC(scr);
    BITMAPINFO bi{};
    bi.bmiHeader = {sizeof(BITMAPINFOHEADER), W, -H, 1, 32, BI_RGB, 0, 0, 0, 0, 0};
    void* bits = nullptr;
    gOsdBmp = CreateDIBSection(scr, &bi, DIB_RGB_COLORS, &bits, nullptr, 0);
    ReleaseDC(nullptr, scr);
    gOsdOld = (HBITMAP)SelectObject(gOsdDC, gOsdBmp);
    gOsdW = W; gOsdH = H;
    {
        Bitmap bmp(W, H, W * 4, PixelFormat32bppPARGB, (BYTE*)bits);
        Graphics g(&bmp);
        g.Clear(Color(0, 0, 0, 0));
        g.SetSmoothingMode(SmoothingModeAntiAlias);
        g.SetTextRenderingHint(TextRenderingHintAntiAliasGridFit);
        g.ScaleTransform(gSc, gSc);
        FillRR(g, 0xF21C1C1E, 0, 0, lw, lh, 22);
        float ix = (lw - 44) / 2;
        bool bl = o.kind == K_BL;
        Glyph(g, o, ix, 14);
        DWORD nameC = o.on ? C_W : C_OFF;
        if (bl) {
            Txt(g, o.name, 14, true, nameC, 0, 60, lw, 20, 1);
            for (int i = 0; i < 2; i++) {  // dua titik level
                float dx = lw / 2 - 11 + i * 14.f, dy = 84;
                if (o.level > i) { SolidBrush f(Col(C_W)); g.FillEllipse(&f, dx, dy, 8.f, 8.f); }
                else { Pen pn(Col(C_OFF), 1.5f); g.DrawEllipse(&pn, dx + 0.75f, dy + 0.75f, 6.5f, 6.5f); }
            }
            Txt(g, o.state, 13, false, C_SUB, 0, 94, lw, 18, 1);
        } else {
            Txt(g, o.name, 14, true, nameC, 0, 66, lw, 20, 1);
            Txt(g, o.state, 13, false, C_SUB, 0, 87, lw, 18, 1);
        }
    }
    GdiFlush();
    HMONITOR mon = MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
    MONITORINFO mi{sizeof mi};
    GetMonitorInfoW(mon, &mi);
    gOsdX = mi.rcWork.left + ((mi.rcWork.right - mi.rcWork.left) - W) / 2;
    gOsdY = gS.posTop ? mi.rcWork.top + (int)(60 * gSc) : mi.rcWork.bottom - H - (int)(60 * gSc);
}
static void PushOsd(BYTE alpha) {
    POINT pos{gOsdX, gOsdY}, src{0, 0};
    SIZE sz{gOsdW, gOsdH};
    BLENDFUNCTION bf{AC_SRC_OVER, 0, alpha, AC_SRC_ALPHA};
    UpdateLayeredWindow(gOsd, nullptr, &pos, &sz, gOsdDC, &src, 0, &bf, ULW_ALPHA);
}
static void ShowOsd(const OsdInfo& o) {
    RenderOsd(o);
    gOsdDur = o.dur;
    gOsdStart = GetTickCount64();
    PushOsd(255);
    ShowWindow(gOsd, SW_SHOWNOACTIVATE);
    SetWindowPos(gOsd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE);
    SetTimer(gMain, 2, 16, nullptr);
}
static void FadeTick() {
    ULONGLONG el = GetTickCount64() - gOsdStart;
    if (el < (ULONGLONG)gOsdDur) return;
    ULONGLONG f = el - gOsdDur;
    if (f >= 220) { KillTimer(gMain, 2); ShowWindow(gOsd, SW_HIDE); return; }
    PushOsd((BYTE)(255 - f * 255 / 220));
}

static void OsdLock(int kind, bool on) {
    OsdInfo o;
    o.kind = kind;
    o.name = kind == K_CAPS ? L"Caps Lock" : L"Num Lock";
    o.state = on ? L"On" : L"Off";
    o.on = on;
    o.dur = gS.durMs;
    ShowOsd(o);
}
static void OsdMic(int st) {
    OsdInfo o;
    o.kind = K_MIC;
    o.name = L"Microphone";
    o.on = st == 0;
    o.state = st < 0 ? L"Not found" : (st == 1 ? L"Off" : L"On");
    o.dur = gS.durMs;
    ShowOsd(o);
}
static void OsdTouch(int st) {  // st: -1 tidak ada, 0 aktif, 1 nonaktif
    OsdInfo o;
    o.kind = K_TOUCH;
    o.name = L"Touchpad";
    o.on = st == 0;
    o.state = st < 0 ? L"Not found" : (st == 1 ? L"Off" : L"On");
    o.dur = gS.durMs;
    ShowOsd(o);
}
static void OsdBl(int lv) {
    OsdInfo o;
    o.kind = K_BL;
    o.name = L"Backlight";
    o.level = lv;
    o.on = lv > 0;
    o.state = lv == 0 ? L"Off" : (lv == 1 ? L"Level 1" : L"Level 2");
    o.dur = gS.durMs;
    ShowOsd(o);
}
static void OsdMsg(const wstring& name, const wstring& state, int dur) {
    OsdInfo o;
    o.kind = K_MSG;
    o.name = name;
    o.state = state;
    o.dur = dur;
    ShowOsd(o);
}

// ---------------------------------------------------------------- aksi
static int gLastMic = -2, gLastTouch = -2;
static std::atomic<int> gTouchWant{0};  // 1 = ingin nonaktif, 0 = ingin aktif
static ULONGLONG gTouchQuiet = 0, gTouchLastTrig = 0;
static void BacklightApply(int lv) {
    if (gS.cmd[lv].empty()) return;
    CloseHandle(CreateThread(nullptr, 0, [](LPVOID p) -> DWORD {
        wstring* s = (wstring*)p;
        RunPS(*s, 20000);
        delete s;
        return 0;
    }, new wstring(gS.cmd[lv]), 0, nullptr));
}
static DWORD WINAPI TouchThread(LPVOID) {
    int rounds = 0;
    for (;;) {
        for (int tries = 0; tries < 3; tries++) {
            int want = gTouchWant, st = TouchState();
            if (st < 0 || st == want) break;
            TouchApply(want == 0);
        }
        gTouchBusy = false;
        int st = TouchState();
        if (st < 0 || st == gTouchWant) break;
        if (++rounds >= 3) break;
        bool exp = false;
        if (!gTouchBusy.compare_exchange_strong(exp, true)) break;  // pemicu baru sudah memulai thread lain
    }
    PostMessageW(gMain, WM_TPDONE, 0, 0);
    return 0;
}

// ---- Mode amati: F10 sudah mematikan/menyalakan touchpad lewat firmware (tanpa delay). Aplikasi tidak ikut menyentuh perangkat; hanya membaca hasilnya dan menampilkan OSD.
static const bool kTouchControl = false;
struct TpSig { int raw, spi, cm; };
static std::atomic<int> gTpObs{0};       // 0 aktif, 1 nonaktif (perkiraan terbaik)
static std::atomic<bool> gTpObsBusy{false};
static TpSig TpSample() {
    TpSig g{};
    g.raw = RawTouchpadPresent() ? 1 : 0;
    g.spi = -1;
    if (gTpSize && TpSpiGet()) g.spi = (int)((((UINT*)gTpBuf)[3] >> 3) & 1);
    g.cm = DeviceDisabled() ? 1 : 0;
    return g;
}
static DWORD WINAPI TouchObserve(LPVOID p) {
    TpSig b = *(TpSig*)p;
    delete (TpSig*)p;
    int ns = -1;
    const wchar_t* why = L"";
    TpSig a = b;
    ULONGLONG t0 = GetTickCount64();
    for (int i = 0; i < 14 && ns < 0; i++) {
        Sleep(i == 0 ? 150 : 100);
        a = TpSample();
        if (a.spi >= 0 && b.spi >= 0 && a.spi != b.spi) { ns = a.spi ? 0 : 1; why = L"spi"; }
        else if (a.raw != b.raw) { ns = a.raw ? 0 : 1; why = L"raw"; }
        else if (a.cm != b.cm) { ns = a.cm ? 1 : 0; why = L"cm"; }
    }
    if (ns < 0) { ns = gTpObs ? 0 : 1; why = L"balik-sendiri"; }
    LogTouch(L"amati: sebelum raw=%d spi=%d cm=%d | sesudah raw=%d spi=%d cm=%d | terdeteksi lewat %s -> %s (%llu ms)", b.raw, b.spi, b.cm, a.raw, a.spi, a.cm, why, ns ? L"NONAKTIF" : L"AKTIF", GetTickCount64() - t0);
    gTpObs = ns;
    gTpObsBusy = false;
    PostMessageW(gMain, WM_TPOBS, (WPARAM)ns, 0);
    return 0;
}
static void DoAction(int a) {
    if (a == A_MIC) {
        int st = MicOp(0);
        if (st >= 0) st = MicOp(st == 1 ? 2 : 1);
        gLastMic = st;
        OsdMic(st);
    } else if (a == A_TOUCH && !kTouchControl) {
        OsdTouch(gTpObs);  // F10 dikendalikan firmware; hotkey/tray hanya menampilkan keadaan terakhir yang diketahui
    } else if (a == A_TOUCH) {
        ULONGLONG now = GetTickCount64();
        if (now - gTouchLastTrig < 250) { LogTouch(L"event diabaikan (debounce %llu ms)", now - gTouchLastTrig); return; }  // satu tekan = beberapa event
        gTouchLastTrig = now;
        int cur = gTouchBusy ? (int)gTouchWant : TouchState();
        if (cur < 0) { LogTouch(L"event diabaikan: touchpad tidak ditemukan (ids=%d spi=%u)", (int)gTouchIds.size(), gTpSize); OsdTouch(-1); return; }
        gTouchWant = cur == 1 ? 0 : 1;
        LogTouch(L"pemicu: state=%d -> want=%d", cur, (int)gTouchWant);
        bool exp = false;
        if (gTouchBusy.compare_exchange_strong(exp, true)) CloseHandle(CreateThread(nullptr, 0, TouchThread, nullptr, 0, nullptr));
        if (gTouchWant == 1) OsdTouch(1);  // nonaktif: langsung. Aktif: OSD muncul setelah driver benar-benar siap
        else gTouchOsdLate = true;
    } else if (a == A_BL) {
        gS.bl = (gS.bl + 1) % 3;
        SaveSettings();
        BacklightApply(gS.bl);
        OsdBl(gS.bl);
    }
}

// ---------------------------------------------------------------- startup (Task Scheduler)
static bool StartupExists() { return Run(L"schtasks /query /tn NitroOSD") == 0; }
static void SetStartup(bool on) {
    if (on)
        RunPS(L"$a=New-ScheduledTaskAction -Execute '" + ExePath() + L"' -Argument '--tray'; $t=New-ScheduledTaskTrigger -AtLogOn -User $env:USERNAME; "
              L"$s=New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit ([TimeSpan]::Zero); "
              L"$p=New-ScheduledTaskPrincipal -UserId $env:USERNAME -RunLevel Highest; "
              L"Register-ScheduledTask -TaskName NitroOSD -Action $a -Trigger $t -Settings $s -Principal $p -Force", 60000);
    else Run(L"schtasks /delete /tn NitroOSD /f");
}

// ---------------------------------------------------------------- event WMI Acer (APGeEvent): tombol Fn yang ditangani firmware
static HWND gSinkTarget = nullptr;
class WmiSink : public IWbemObjectSink {
    LONG ref_ = 1;
public:
    ULONG STDMETHODCALLTYPE AddRef() override { return InterlockedIncrement(&ref_); }
    ULONG STDMETHODCALLTYPE Release() override { LONG r = InterlockedDecrement(&ref_); if (!r) delete this; return r; }
    HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** pp) override {
        if (riid == IID_IUnknown || riid == IID_IWbemObjectSink) { *pp = static_cast<IWbemObjectSink*>(this); AddRef(); return S_OK; }
        *pp = nullptr;
        return E_NOINTERFACE;
    }
    HRESULT STDMETHODCALLTYPE Indicate(LONG n, IWbemClassObject** objs) override {
        for (LONG i = 0; i < n; i++) {
            VARIANT v;
            VariantInit(&v);
            if (SUCCEEDED(objs[i]->Get(L"EventDetail", 0, &v, nullptr, nullptr)) && (v.vt & VT_ARRAY) && v.parray) {
                SAFEARRAY* sa = v.parray;
                LONG lb = 0, ub = -1;
                SafeArrayGetLBound(sa, 1, &lb);
                SafeArrayGetUBound(sa, 1, &ub);
                UINT es = SafeArrayGetElemsize(sa);
                unsigned char b[8] = {};
                void* data = nullptr;
                if (SUCCEEDED(SafeArrayAccessData(sa, &data)) && data) {
                    for (LONG k = 0; k <= ub - lb && k < 8; k++) {
                        unsigned long long x = 0;
                        memcpy(&x, (unsigned char*)data + (size_t)k * es, es > 8 ? 8 : es);
                        b[k] = (unsigned char)x;
                    }
                    SafeArrayUnaccessData(sa);
                }
                PostMessageW(gSinkTarget, WM_WMIKEY, (WPARAM)(b[0] | (b[1] << 8) | (b[2] << 16) | ((unsigned)b[3] << 24)), (LPARAM)(b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24)));
            }
            VariantClear(&v);
        }
        return WBEM_S_NO_ERROR;
    }
    HRESULT STDMETHODCALLTYPE SetStatus(LONG, HRESULT, BSTR, IWbemClassObject*) override { return WBEM_S_NO_ERROR; }
};
static const GUID K_CLSID_WbemLocator = {0x4590F811, 0x1D3A, 0x11D0, {0x89, 0x1F, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24}};
static const GUID K_IID_WbemLocator = {0xDC12A687, 0x737F, 0x11CF, {0x88, 0x4D, 0x00, 0xAA, 0x00, 0x4B, 0x2E, 0x24}};
static const GUID K_CLSID_UnsecApt = {0x49BD2028, 0x1523, 0x11D1, {0xAD, 0x79, 0x00, 0xC0, 0x4F, 0xD8, 0xFD, 0xFF}};
static const GUID K_IID_UnsecApt = {0x1CFABA8C, 0x1523, 0x11D1, {0xAD, 0x79, 0x00, 0xC0, 0x4F, 0xD8, 0xFD, 0xFF}};
static bool StartWmiEvents(HWND target) {
    gSinkTarget = target;
    IWbemLocator* loc = nullptr;
    if (FAILED(CoCreateInstance(K_CLSID_WbemLocator, nullptr, CLSCTX_INPROC_SERVER, K_IID_WbemLocator, (void**)&loc)) || !loc) return false;
    IWbemServices* svc = nullptr;
    BSTR ns = SysAllocString(L"ROOT\\WMI");
    HRESULT hr = loc->ConnectServer(ns, nullptr, nullptr, nullptr, 0, nullptr, nullptr, &svc);
    SysFreeString(ns);
    loc->Release();
    if (FAILED(hr) || !svc) return false;
    CoSetProxyBlanket(svc, RPC_C_AUTHN_WINNT, RPC_C_AUTHZ_NONE, nullptr, RPC_C_AUTHN_LEVEL_CALL, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE);
    IUnsecuredApartment* ua = nullptr;
    IWbemObjectSink* stub = nullptr;
    WmiSink* sink = new WmiSink();
    if (SUCCEEDED(CoCreateInstance(K_CLSID_UnsecApt, nullptr, CLSCTX_LOCAL_SERVER, K_IID_UnsecApt, (void**)&ua)) && ua) {
        IUnknown* u = nullptr;
        if (SUCCEEDED(ua->CreateObjectStub(sink, &u)) && u) { u->QueryInterface(IID_IWbemObjectSink, (void**)&stub); u->Release(); }
        ua->Release();
    }
    BSTR ql = SysAllocString(L"WQL"), q = SysAllocString(L"SELECT * FROM APGeEvent");
    hr = svc->ExecNotificationQueryAsync(ql, q, WBEM_FLAG_SEND_STATUS, nullptr, stub ? stub : sink);
    SysFreeString(ql);
    SysFreeString(q);
    return SUCCEEDED(hr);
}

// ---------------------------------------------------------------- hook keyboard
static HHOOK gHook = nullptr;
static int gLearn = -1;
static std::atomic<bool> gDiag{false};
static int gLearnVk = 0, gLearnScan = 0;
static DWORD gSwallowVk = 0;
static bool IsModVk(DWORD v) { return (v >= 0x10 && v <= 0x12) || (v >= 0xA0 && v <= 0xA5) || v == 0x5B || v == 0x5C; }
static LRESULT CALLBACK KbProc(int code, WPARAM w, LPARAM l) {
    if (code == HC_ACTION) {
        auto* k = (KBDLLHOOKSTRUCT*)l;
        bool down = (w == WM_KEYDOWN || w == WM_SYSKEYDOWN);
        bool up = (w == WM_KEYUP || w == WM_SYSKEYUP);
        bool inj = (k->flags & LLKHF_INJECTED) != 0;
        if (up && gSwallowVk && k->vkCode == gSwallowVk) { gSwallowVk = 0; return 1; }
        if (down && !inj) {
            if (gDiag) PostMessageW(gMain, WM_DIAG, k->vkCode, (LPARAM)(k->scanCode | (k->flags << 16)));
            if (gLearn >= 0 && !IsModVk(k->vkCode)) {
                gLearnVk = (int)k->vkCode;
                gLearnScan = (int)k->scanCode;
                gSwallowVk = k->vkCode;
                PostMessageW(gMain, WM_LEARNED, 0, 0);
                return 1;
            }
            if (k->vkCode == VK_CAPITAL || k->vkCode == VK_NUMLOCK) {
                PostMessageW(gMain, WM_KEYST, k->vkCode, 0);
            } else {
                bool mods = (GetAsyncKeyState(VK_MENU) | GetAsyncKeyState(VK_CONTROL) | GetAsyncKeyState(VK_LWIN) | GetAsyncKeyState(VK_RWIN)) & 0x8000;
                if (!mods)
                    for (int i = 0; i < 3; i++)
                        if (gS.vk[i] && (DWORD)gS.vk[i] == k->vkCode && (!gS.scan[i] || (DWORD)gS.scan[i] == k->scanCode)) {
                            PostMessageW(gMain, WM_ACT, i, 0);
                            bool can = i != A_BL || !gS.cmd[A_BL].empty();
                            if (gS.swallow && can) { gSwallowVk = k->vkCode; return 1; }
                            break;
                        }
            }
        }
    }
    return CallNextHookEx(gHook, code, w, l);
}

// ---------------------------------------------------------------- jendela pengaturan (gelap monokrom)
struct Hit { RectF r; int id; };
static vector<Hit> gHits;
static int gHover = 0;
static bool gStartupOn = false;
static HDC gSetDC = nullptr;
static HBITMAP gSetBmp = nullptr, gSetOld = nullptr;
static int gSetPxW = 0, gSetPxH = 0;
enum { H_LEARN = 10, H_RESET = 13, H_DUR = 20, H_POS = 30, H_START = 40, H_SWALLOW = 41, H_DIAG = 42, H_TEST = 50 };
static const float SET_W = 440.f, SET_H = 590.f;

static wstring KeyDesc(int i) {
    if (gLearn == i) return L"Press the key...";
    if (gS.vk[i]) return Fmt(L"Key 0x%02X", gS.vk[i]);
    if (gS.wb1[i]) return L"Fn key (WMI)";
    return i == A_BL ? L"Not detected" : L"Not set";
}
static void AddHit(float x, float y, float w, float h, int id) { gHits.push_back({RectF(x, y, w, h), id}); }
static void Btn(Graphics& g, const wstring& t, float x, float y, float w, float h, int id) {
    if (gHover == id) FillRR(g, C_HOV, x, y, w, h, 6);
    StrokeRR(g, C_BD, 1.f, x + 0.5f, y + 0.5f, w - 1, h - 1, 6);
    Txt(g, t, 12, false, C_W, x, y, w, h, 1);
    AddHit(x, y, w, h, id);
}
static void Seg(Graphics& g, const wchar_t* const* labels, int n, int active, float xr, float y, int baseId) {
    float sw = 56, total = n * sw, x0 = xr - total;
    StrokeRR(g, C_BD, 1.f, x0 + 0.5f, y + 0.5f, total - 1, 25, 7);
    for (int i = 0; i < n; i++) {
        float x = x0 + i * sw;
        if (active == i) FillRR(g, C_W, x + 2, y + 2, sw - 4, 21, 5.5f);
        else if (gHover == baseId + i) FillRR(g, C_HOV, x + 2, y + 2, sw - 4, 21, 5.5f);
        Txt(g, labels[i], 12, active == i, active == i ? C_BG : C_W, x, y, sw, 26, 1);
        AddHit(x, y, sw, 26, baseId + i);
    }
}
static void Toggle(Graphics& g, bool on, float xr, float y, int id) {
    float x = xr - 38;
    FillRR(g, on ? C_W : 0xFF3A3A3C, x, y, 38, 22, 11);
    SolidBrush k(Col(on ? C_BG : C_DIM));
    g.FillEllipse(&k, on ? x + 38 - 19.f : x + 3.f, y + 3.f, 16.f, 16.f);
    AddHit(x - 4, y - 8, 46, 38, id);
}
static void Sec(Graphics& g, const wstring& t, float y) { Txt(g, t, 11, true, C_DIM, 20, y, 300, 16); }
static void RowLabel(Graphics& g, const wstring& t, float y, bool first) {
    if (!first) FillR(g, C_LN, 20, y, SET_W - 40, 1);
    Txt(g, t, 14, false, C_W, 20, y, 220, 44);
}
static void DrawSettings(Graphics& g) {
    gHits.clear();
    g.Clear(Col(C_BG));
    DrawKeycap(g, 20, 16, 32, C_W, C_BG);
    Txt(g, L"Nitro OSD", 17, true, C_W, 64, 14, 300, 24);
    Txt(g, L"Running in tray", 12, false, C_DIM, 64, 38, 300, 16);

    Sec(g, L"KEYS", 76);
    static const wchar_t* names[3] = {L"Microphone", L"Touchpad", L"Backlight"};
    for (int i = 0; i < 3; i++) {
        float y = 98 + i * 44.f;
        RowLabel(g, names[i], y, i == 0);
        Txt(g, KeyDesc(i), 13, false, gLearn == i ? C_W : C_DIM, 200, y, 130, 44, 2);
        Btn(g, gLearn == i ? L"Cancel" : L"Learn", 344, y + 9, 76, 26, H_LEARN + i);
    }
    RowLabel(g, L"Learned keys", 98 + 3 * 44.f, false);
    Btn(g, L"Reset", 344, 98 + 3 * 44.f + 9, 76, 26, H_RESET);

    Sec(g, L"OSD", 286);
    float y = 308;
    RowLabel(g, L"Duration", y, true);
    static const wchar_t* dl[3] = {L"1 s", L"1.5 s", L"2 s"};
    Seg(g, dl, 3, gS.durMs <= 1000 ? 0 : gS.durMs <= 1500 ? 1 : 2, SET_W - 20, y + 9, H_DUR);
    y += 44;
    RowLabel(g, L"Position", y, false);
    static const wchar_t* pl[2] = {L"Top", L"Bottom"};
    Seg(g, pl, 2, gS.posTop ? 0 : 1, SET_W - 20, y + 9, H_POS);
    y += 44;
    RowLabel(g, L"Start at login", y, false);
    Toggle(g, gStartupOn, SET_W - 20, y + 11, H_START);
    y += 44;
    RowLabel(g, L"Block learned keys", y, false);
    Toggle(g, gS.swallow, SET_W - 20, y + 11, H_SWALLOW);
    y += 44;
    RowLabel(g, L"Key diagnostics", y, false);
    Toggle(g, gDiag, SET_W - 20, y + 11, H_DIAG);

    FillR(g, C_LN, 20, 540, SET_W - 40, 1);
    Btn(g, L"Test OSD", 20, 552, 110, 28, H_TEST);
    Txt(g, L"v1.0", 12, false, C_DIM, SET_W - 120, 552, 100, 28, 2);
}
static void RefreshSet() { if (gSet) InvalidateRect(gSet, nullptr, FALSE); }
static void DarkTitle(HWND h) {
    HMODULE d = LoadLibraryW(L"dwmapi.dll");
    if (!d) return;
    auto f = (HRESULT(WINAPI*)(HWND, DWORD, LPCVOID, DWORD))GetProcAddress(d, "DwmSetWindowAttribute");
    BOOL v = TRUE;
    if (f) { f(h, 20, &v, sizeof v); f(h, 19, &v, sizeof v); }
}
static int gDemo = -1;
static void DemoStep() {
    switch (gDemo) {
        case 0: OsdLock(K_CAPS, true); break;
        case 1: OsdLock(K_CAPS, false); break;
        case 2: OsdLock(K_NUM, true); break;
        case 3: OsdLock(K_NUM, false); break;
        case 4: OsdMic(0); break;
        case 5: OsdMic(1); break;
        case 6: OsdTouch(0); break;
        case 7: OsdTouch(1); break;
        case 8: OsdBl(0); break;
        case 9: OsdBl(1); break;
        case 10: OsdBl(2); break;
        case 11: OsdMsg(L"Microphone", L"Press the key...", 1200); break;
        default: KillTimer(gMain, 5); gDemo = -1; return;
    }
    gDemo++;
}
static void StartDemo(int stepMs) { gDemo = 0; SetTimer(gMain, 5, stepMs, nullptr); }

static void OnSetClick(int id) {
    if (id >= H_LEARN && id < H_LEARN + 3) {
        int i = id - H_LEARN;
        if (gLearn == i) { gLearn = -1; KillTimer(gMain, 4); }
        else {
            gLearn = i;
            SetTimer(gMain, 4, 10000, nullptr);
            OsdMsg(i == 0 ? L"Microphone" : i == 1 ? L"Touchpad" : L"Backlight", L"Press the key...", 3000);
        }
    } else if (id == H_RESET) {
        for (int i = 0; i < 3; i++) { gS.vk[i] = gS.scan[i] = 0; gS.wb1[i] = (i == A_TOUCH) ? 131 : 0; gS.wb2[i] = 0; }
        gLearn = -1;
        SaveSettings();
    } else if (id >= H_DUR && id < H_DUR + 3) {
        gS.durMs = id == H_DUR ? 1000 : id == H_DUR + 1 ? 1500 : 2000;
        SaveSettings();
        OsdLock(K_CAPS, true);
    } else if (id == H_POS || id == H_POS + 1) {
        gS.posTop = id == H_POS;
        SaveSettings();
        OsdLock(K_CAPS, true);
    } else if (id == H_START) {
        CloseHandle(CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
            bool now = StartupExists();
            SetStartup(!now);
            gStartupOn = StartupExists();
            PostMessageW(gMain, WM_REFRESH, 0, 0);
            return 0;
        }, nullptr, 0, nullptr));
    } else if (id == H_SWALLOW) { gS.swallow = !gS.swallow; SaveSettings(); }
    else if (id == H_DIAG) { gDiag = !gDiag; OsdMsg(L"Key diagnostics", gDiag ? L"On" : L"Off", 1200); }
    else if (id == H_TEST) StartDemo(1100);
    RefreshSet();
}
static int SetHit(LPARAM l) {
    PointF p(GET_X_LPARAM(l) / gSc, GET_Y_LPARAM(l) / gSc);
    for (int i = (int)gHits.size() - 1; i >= 0; i--)
        if (gHits[i].r.Contains(p)) return gHits[i].id;
    return 0;
}
static LRESULT CALLBACK SetProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_ERASEBKGND: return 1;
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(h, &ps);
            RECT rc;
            GetClientRect(h, &rc);
            int W = rc.right, H = rc.bottom;
            if (W != gSetPxW || H != gSetPxH || !gSetDC) {
                if (gSetDC) { SelectObject(gSetDC, gSetOld); DeleteObject(gSetBmp); DeleteDC(gSetDC); }
                gSetDC = CreateCompatibleDC(dc);
                gSetBmp = CreateCompatibleBitmap(dc, W, H);
                gSetOld = (HBITMAP)SelectObject(gSetDC, gSetBmp);
                gSetPxW = W; gSetPxH = H;
            }
            {
                Graphics g(gSetDC);
                g.SetSmoothingMode(SmoothingModeAntiAlias);
                g.SetTextRenderingHint(TextRenderingHintClearTypeGridFit);
                g.ScaleTransform(gSc, gSc);
                DrawSettings(g);
            }
            BitBlt(dc, 0, 0, W, H, gSetDC, 0, 0, SRCCOPY);
            EndPaint(h, &ps);
            return 0;
        }
        case WM_MOUSEMOVE: {
            int hv = SetHit(l);
            if (hv != gHover) { gHover = hv; InvalidateRect(h, nullptr, FALSE); }
            TRACKMOUSEEVENT t{sizeof t, TME_LEAVE, h, 0};
            TrackMouseEvent(&t);
            return 0;
        }
        case WM_MOUSELEAVE: gHover = 0; InvalidateRect(h, nullptr, FALSE); return 0;
        case WM_SETCURSOR:
            if (LOWORD(l) == HTCLIENT) { SetCursor(LoadCursor(nullptr, gHover ? IDC_HAND : IDC_ARROW)); return TRUE; }
            break;
        case WM_LBUTTONUP: {
            int id = SetHit(l);
            if (id) OnSetClick(id);
            return 0;
        }
        case WM_CLOSE: ShowWindow(h, SW_HIDE); return 0;
    }
    return DefWindowProcW(h, m, w, l);
}
static void ShowSettings(HINSTANCE hi) {
    if (!gSet) {
        RECT r{0, 0, (LONG)(SET_W * gSc), (LONG)(SET_H * gSc)};
        DWORD st = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
        AdjustWindowRect(&r, st, FALSE);
        int sx = GetSystemMetrics(SM_CXSCREEN), sy = GetSystemMetrics(SM_CYSCREEN);
        int ww = r.right - r.left, wh = r.bottom - r.top;
        gSet = CreateWindowExW(0, L"NitroOSDSettings", L"Nitro OSD", st, (sx - ww) / 2, std::max(20, (sy - wh) / 2), ww, wh, nullptr, nullptr, hi, nullptr);
        DarkTitle(gSet);
    }
    CloseHandle(CreateThread(nullptr, 0, [](LPVOID) -> DWORD {
        gStartupOn = StartupExists();
        PostMessageW(gMain, WM_REFRESH, 0, 0);
        return 0;
    }, nullptr, 0, nullptr));
    ShowWindow(gSet, SW_SHOW);
    SetForegroundWindow(gSet);
    RefreshSet();
}

// ---------------------------------------------------------------- tray
enum { M_OPEN = 1, M_MIC, M_TOUCH, M_TEST, M_EXIT };
static NOTIFYICONDATAW gNid{};
static void AddTray() {
    gNid.cbSize = sizeof gNid;
    gNid.hWnd = gMain;
    gNid.uID = 1;
    gNid.uFlags = NIF_ICON | NIF_MESSAGE | NIF_TIP;
    gNid.uCallbackMessage = WM_TRAY;
    gNid.hIcon = (HICON)LoadImageW(GetModuleHandleW(nullptr), MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    wcscpy(gNid.szTip, L"Nitro OSD");
    Shell_NotifyIconW(NIM_ADD, &gNid);
}
static void TrayMenu(HINSTANCE hi) {
    HMENU m = CreatePopupMenu();
    int mic = MicOp(0), tp = TouchState();
    AppendMenuW(m, MF_STRING, M_OPEN, L"Open settings");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING | (mic == 1 ? MF_CHECKED : 0) | (mic < 0 ? MF_GRAYED : 0), M_MIC, L"Microphone off");
    AppendMenuW(m, MF_STRING | (tp == 1 ? MF_CHECKED : 0) | (tp < 0 ? MF_GRAYED : 0), M_TOUCH, L"Touchpad off");
    AppendMenuW(m, MF_SEPARATOR, 0, nullptr);
    AppendMenuW(m, MF_STRING, M_TEST, L"Test OSD");
    AppendMenuW(m, MF_STRING, M_EXIT, L"Exit");
    POINT pt;
    GetCursorPos(&pt);
    SetForegroundWindow(gMain);
    int c = TrackPopupMenu(m, TPM_RETURNCMD | TPM_RIGHTBUTTON | TPM_BOTTOMALIGN, pt.x, pt.y, 0, gMain, nullptr);
    DestroyMenu(m);
    switch (c) {
        case M_OPEN: ShowSettings(hi); break;
        case M_MIC: DoAction(A_MIC); break;
        case M_TOUCH: DoAction(A_TOUCH); break;
        case M_TEST: StartDemo(1100); break;
        case M_EXIT: PostMessageW(gMain, WM_CLOSE, 0, 0); break;
    }
}

// ---------------------------------------------------------------- jendela utama (pesan)
static int gPendingVk = 0;
static HINSTANCE gInst = nullptr;
static void LogKey(int vk, int scan, int flags) {
    wstring p = DataDir() + L"\\keys.log";
    FILE* f = _wfopen(p.c_str(), L"a");
    if (!f) return;
    SYSTEMTIME st;
    GetLocalTime(&st);
    fwprintf(f, L"%02d:%02d:%02d.%03d vk=0x%02X scan=0x%02X flags=0x%02X\n", st.wHour, st.wMinute, st.wSecond, st.wMilliseconds, vk, scan, flags);
    fclose(f);
}
static LRESULT CALLBACK MainProc(HWND h, UINT m, WPARAM w, LPARAM l) {
    switch (m) {
        case WM_TIMER:
            if (w == 2) FadeTick();
            else if (w == 5) DemoStep();
            else if (w == 3) {
                KillTimer(h, 3);
                if (gPendingVk) OsdLock(gPendingVk == VK_CAPITAL ? K_CAPS : K_NUM, (GetKeyState(gPendingVk) & 1) != 0);
                gPendingVk = 0;
            } else if (w == 4) {
                KillTimer(h, 4);
                if (gLearn >= 0) { gLearn = -1; OsdMsg(L"Learn key", L"No key detected", 2200); RefreshSet(); }
            } else if (w == 1) {  // pantau perubahan dari luar
                int mic = MicOp(0);
                if (gLastMic != -2 && mic != gLastMic) OsdMic(mic);
                gLastMic = mic;
                if (kTouchControl && !gTouchBusy && GetTickCount64() > gTouchQuiet) {
                    int tp = TouchState();
                    if (gLastTouch != -2 && tp != gLastTouch) OsdTouch(tp);
                    gLastTouch = tp;
                }
            }
            return 0;
        case WM_KEYST:
            gPendingVk = (int)w;
            SetTimer(h, 3, 45, nullptr);
            return 0;
        case WM_ACT: DoAction((int)w); return 0;
        case WM_HOTKEY: DoAction((int)w - 1); return 0;
        case WM_REFRESH: RefreshSet(); return 0;
        case WM_INPUT: {
            ULONGLONG n = GetTickCount64();
            RAWINPUTHEADER hd{};
            UINT cb = sizeof hd;
            if (GetRawInputData((HRAWINPUT)l, RID_HEADER, &hd, &cb, sizeof hd) != (UINT)-1 && hd.dwType == RIM_TYPEHID && n - gTpLastRpt > 2500) {
                if (gTpEnableTick) LogTouch(L"touchpad kirim data: %llu ms setelah dinyalakan", n - gTpEnableTick);
                else LogTouch(L"touchpad kirim data");
                gTpLastRpt = n;
            }
            return DefWindowProcW(h, m, w, l);
        }
        case WM_TPOBS:
            gLastTouch = (int)w;
            gTouchWant = (int)w;
            OsdTouch((int)w);
            return 0;
        case WM_TPDONE: {
            gTouchQuiet = GetTickCount64() + 1500;
            int st = TouchState();
            if (gTouchOsdLate.exchange(false) && !gTouchBusy && st == 0) { OsdTouch(0); gLastTouch = 0; return 0; }
            if (!gTouchBusy && st >= 0 && st != gTouchWant) { gTouchWant = st; OsdTouch(st); }  // gagal: tampilkan keadaan sebenarnya
            gLastTouch = st;
            return 0;
        }
        case WM_LEARNED:
            if (gLearn >= 0) {
                KillTimer(h, 4);
                int i = gLearn;
                gS.vk[i] = gLearnVk;
                gS.scan[i] = gLearnScan;
                SaveSettings();
                gLearn = -1;
                OsdMsg(i == 0 ? L"Microphone" : i == 1 ? L"Touchpad" : L"Backlight", L"Key saved", 1800);
                RefreshSet();
            }
            return 0;
        case WM_WMIKEY: {
            int b0 = (int)(w & 0xFF), b1 = (int)((w >> 8) & 0xFF), b2 = (int)((w >> 16) & 0xFF), b3 = (int)((w >> 24) & 0xFF);
            if (gDiag) {
                LogKey(-1, b1, b2 | (b0 << 8));
                OsdMsg(L"WMI event", Fmt(L"%d,%d,%d,%d", b0, b1, b2, b3), 1400);
            }
            if (b0 == 1 && b1 == 131) LogTouch(L"event WMI %d,%d,%d,%d", b0, b1, b2, b3);
            if (b0 != 1) return 0;
            if (gLearn >= 0) {
                KillTimer(h, 4);
                int i = gLearn;
                gS.wb1[i] = b1;
                gS.wb2[i] = b2;
                gS.vk[i] = 0;
                SaveSettings();
                gLearn = -1;
                OsdMsg(i == 0 ? L"Microphone" : i == 1 ? L"Touchpad" : L"Backlight", L"Key saved", 1800);
                RefreshSet();
                return 0;
            }
            if (!kTouchControl && gS.wb1[A_TOUCH] && gS.wb1[A_TOUCH] == b1 && (b2 == 0 || b2 == 2)) {
                // EventDetail[2] membawa keadaan: 0 = touchpad MATI, 2 = touchpad NYALA (firmware sudah menjalankannya; aplikasi hanya menampilkan)
                static ULONGLONG lastEv = 0;
                ULONGLONG n = GetTickCount64();
                int st = (b2 == 0) ? 1 : 0;
                if (n - lastEv > 120 || st != (int)gTpObs) {
                    lastEv = n;
                    gTpObs = st;
                    gLastTouch = st;
                    gTouchWant = st;
                    LogTouch(L"touchpad %s (event WMI)", st ? L"NONAKTIF" : L"AKTIF");
                    OsdTouch(st);
                }
                return 0;
            }
            for (int i = 0; i < 3; i++)
                if (gS.wb1[i] && gS.wb1[i] == b1 && gS.wb2[i] == b2) { DoAction(i); break; }
            return 0;
        }
        case WM_DIAG:
            LogKey((int)w, (int)(l & 0xFFFF), (int)((l >> 16) & 0xFF));
            OsdMsg(L"Key code", Fmt(L"VK 0x%02X  SC 0x%02X", (int)w, (int)(l & 0xFFFF)), 1200);
            return 0;
        case WM_TRAY:
            if (LOWORD(l) == WM_LBUTTONUP) ShowSettings(gInst);
            else if (LOWORD(l) == WM_RBUTTONUP) TrayMenu(gInst);
            return 0;
        case WM_CLOSE:
            Shell_NotifyIconW(NIM_DELETE, &gNid);
            if (gHook) UnhookWindowsHookEx(gHook);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProcW(h, m, w, l);
}

int WINAPI wWinMain(HINSTANCE hi, HINSTANCE, PWSTR cmd, int) {
    CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    CoInitializeSecurity(nullptr, -1, nullptr, nullptr, RPC_C_AUTHN_LEVEL_DEFAULT, RPC_C_IMP_LEVEL_IMPERSONATE, nullptr, EOAC_NONE, nullptr);
    LoadSettings();
    if (cmd && (wcsstr(cmd, L"--task-on") || wcsstr(cmd, L"--task-off") || wcsstr(cmd, L"--restore"))) {
        if (wcsstr(cmd, L"--task-on")) SetStartup(true);
        else if (wcsstr(cmd, L"--task-off")) SetStartup(false);
        else { FindTouchpads(); TouchSetFast(true); TouchSetBlocking(true); MicOp(2); }
        return 0;
    }
    HANDLE mtx = CreateMutexW(nullptr, TRUE, L"Global\\NitroOSD_Single");
    if (GetLastError() == ERROR_ALREADY_EXISTS) return 0;
    gInst = hi;

    GdiplusStartupInput gi;
    ULONG_PTR tok;
    GdiplusStartup(&tok, &gi, nullptr);
    gFam = new FontFamily(L"Segoe UI");
    HDC sdc = GetDC(nullptr);
    gSc = GetDeviceCaps(sdc, LOGPIXELSX) / 96.f;
    ReleaseDC(nullptr, sdc);

    HICON big = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON), 0);
    HICON small = (HICON)LoadImageW(hi, MAKEINTRESOURCEW(1), IMAGE_ICON, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON), 0);
    WNDCLASSW wc{};
    wc.lpfnWndProc = MainProc;
    wc.hInstance = hi;
    wc.lpszClassName = L"NitroOSDMain";
    RegisterClassW(&wc);
    WNDCLASSEXW ws{sizeof ws};
    ws.lpfnWndProc = SetProc;
    ws.hInstance = hi;
    ws.lpszClassName = L"NitroOSDSettings";
    ws.hIcon = big;
    ws.hIconSm = small;
    ws.hCursor = LoadCursor(nullptr, IDC_ARROW);
    RegisterClassExW(&ws);
    WNDCLASSW wo{};
    wo.lpfnWndProc = DefWindowProcW;
    wo.hInstance = hi;
    wo.lpszClassName = L"NitroOSDWnd";
    RegisterClassW(&wo);
    gMain = CreateWindowExW(0, L"NitroOSDMain", L"NitroOSD", 0, 0, 0, 0, 0, HWND_MESSAGE, nullptr, hi, nullptr);
    gOsd = CreateWindowExW(WS_EX_LAYERED | WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE | WS_EX_TRANSPARENT, L"NitroOSDWnd", L"", WS_POPUP,
                           0, 0, 10, 10, nullptr, nullptr, hi, nullptr);

    FindTouchpads();
    TpSpiProbe();
    LogTouch(L"start: perangkat=%d spi=%u", (int)gTouchIds.size(), gTpSize);
    gRawOk = (!DeviceDisabled() && RawTouchpadPresent()) ? 1 : (DeviceDisabled() ? -1 : 0);
    LogTouch(L"Raw Input touchpad=%d", (int)gRawOk);
    if (!kTouchControl) {  // versi lama aplikasi bisa meninggalkan device dalam keadaan nonaktif: nyalakan agar firmware yang memegang kendali
        if (DeviceDisabled()) { bool ok = TouchSetFast(true); LogTouch(L"device sebelumnya nonaktif -> dinyalakan ok=%d", ok); for (int i = 0; i < 40 && !TpSpiProbe(); i++) Sleep(100); }
        gTpObs = RawTouchpadPresent() ? 0 : 1;
        LogTouch(L"mode event: keadaan touchpad dibaca dari event WMI (0=mati, 2=nyala); keadaan awal diperkirakan %s", gTpObs ? L"NONAKTIF" : L"AKTIF");
    }
    if (gTpSize && TouchState() == 1 && !(GetFileAttributesW(L"C:\\Windows") == INVALID_FILE_ATTRIBUTES)) {}
    if (gTpSize && TpSpiGet() && !((((UINT*)gTpBuf)[3] >> 3) & 1)) { bool devOff = false; for (auto& id : gTouchIds) { DEVINST in = 0; ULONG st = 0, pr = 0; if (CM_Locate_DevNodeW(&in, (DEVINSTID_W)id.c_str(), CM_LOCATE_DEVNODE_NORMAL) == CR_SUCCESS && CM_Get_DevNode_Status(&st, &pr, in, 0) == CR_SUCCESS && (st & DN_HAS_PROBLEM) && pr == CM_PROB_DISABLED) devOff = true; } if (!devOff) TpSpiSet(true); }
    gLastMic = MicOp(0);
    gLastTouch = TouchState();
    gTouchWant = gLastTouch == 1 ? 1 : 0;
    AddTray();
    StartWmiEvents(gMain);
    { RAWINPUTDEVICE rid{0x0D, 0x05, RIDEV_INPUTSINK, gMain}; if (!RegisterRawInputDevices(&rid, 1, sizeof rid)) LogTouch(L"RegisterRawInputDevices gagal err=%lu", GetLastError()); }
    gHook = SetWindowsHookExW(WH_KEYBOARD_LL, KbProc, GetModuleHandleW(nullptr), 0);
    RegisterHotKey(gMain, 1, MOD_CONTROL | MOD_ALT, 'M');  // mic
    RegisterHotKey(gMain, 2, MOD_CONTROL | MOD_ALT, 'T');  // touchpad
    RegisterHotKey(gMain, 3, MOD_CONTROL | MOD_ALT, 'B');  // backlight
    SetTimer(gMain, 1, 500, nullptr);
    if (cmd && wcsstr(cmd, L"--demo")) StartDemo(1300);
    if (!(cmd && wcsstr(cmd, L"--tray")) || (cmd && wcsstr(cmd, L"--settings"))) ShowSettings(hi);

    MSG msg;
    while (GetMessageW(&msg, nullptr, 0, 0) > 0) {
        TranslateMessage(&msg);
        DispatchMessageW(&msg);
    }
    GdiplusShutdown(tok);
    CloseHandle(mtx);
    return 0;
}
