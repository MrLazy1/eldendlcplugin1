// ===========================================================================
//  eldendlc.cpp -- hardened Shadow of the Erdtree unlock for uc-online2
//  Works on UnionCrax v1.7.0a and on the LukeWarmSodas fork.
//
//  The emulator answers "do you own AppId X?" but not "list your DLC".
//  ELDEN RING enumerates (GetDLCCount / BGetDLCDataByIndex), so it saw
//  nothing. This hooks the full ISteamApps DLC surface + the ISteamUser
//  licence check, then self-tests and logs the result.
//
//  Build x64:
//    cl /LD /MT /O2 /EHsc /std:c++17 eldendlc.cpp /link /OUT:10_eldendlc.dll
//
//  Install: <ELDEN RING>\Game\plugins\10_eldendlc.dll
//  Log:     %TEMP%\eldendlc.log  AND  <game>\eldendlc.log
// ===========================================================================

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <stdio.h>
#include <stdarg.h>
#include <string.h>

// ---------------------------------------------------------------------------
// Config
// ---------------------------------------------------------------------------
static const uint32_t kBase    = 1245620;
static const uint32_t kDlc[]   = { 2778580 };
static const char*    kNames[] = { "ELDEN RING - Shadow of the Erdtree" };
static const int      kDlcN    = (int)(sizeof(kDlc) / sizeof(kDlc[0]));

static bool IsOurs(uint32_t id) {
    for (int i = 0; i < kDlcN; ++i) if (kDlc[i] == id) return true;
    return false;
}

// ---------------------------------------------------------------------------
// Logging -> %TEMP%\eldendlc.log AND <game>\eldendlc.log
// ---------------------------------------------------------------------------
static void AppendFile(const char* dir, const char* line) {
    char path[MAX_PATH];
    _snprintf_s(path, MAX_PATH, _TRUNCATE, "%s\\eldendlc.log", dir);
    FILE* f = nullptr;
    if (fopen_s(&f, path, "a") != 0 || !f) return;
    fputs(line, f); fputc('\n', f); fclose(f);
}

static void Log(const char* fmt, ...) {
    char body[768] = { 0 };
    va_list ap; va_start(ap, fmt);
    _vsnprintf_s(body, sizeof(body), _TRUNCATE, fmt, ap);
    va_end(ap);

    SYSTEMTIME st; GetLocalTime(&st);
    char line[896];
    _snprintf_s(line, sizeof(line), _TRUNCATE, "[%02d:%02d:%02d] %s",
                st.wHour, st.wMinute, st.wSecond, body);

    char tmp[MAX_PATH] = { 0 };
    if (GetEnvironmentVariableA("TEMP", tmp, MAX_PATH) && tmp[0]) AppendFile(tmp, line);

    char exe[MAX_PATH] = { 0 };
    GetModuleFileNameA(nullptr, exe, MAX_PATH);
    char* s = strrchr(exe, '\\');
    if (s) *(s + 1) = 0; else exe[0] = 0;
    if (exe[0]) AppendFile(exe, line);
}

// ---------------------------------------------------------------------------
// Signatures
// ---------------------------------------------------------------------------
typedef bool     (*tBIsSubscribedApp)(void*, uint32_t);
typedef bool     (*tBIsDlcInstalled)(void*, uint32_t);
typedef uint32_t (*tGetEarliestPurchase)(void*, uint32_t);
typedef int      (*tGetDLCCount)(void*);
typedef bool     (*tBGetDLCDataByIndex)(void*, int, uint32_t*, bool*, char*, int);
typedef uint32_t (*tGetAppInstallDir)(void*, uint32_t, char*, uint32_t);
typedef int      (*tUserHasLicenseForApp)(void*, uint64_t, uint32_t);

static tBIsSubscribedApp     o_BIsSubscribedApp     = nullptr;
static tBIsDlcInstalled      o_BIsDlcInstalled      = nullptr;
static tGetEarliestPurchase  o_GetEarliestPurchase  = nullptr;
static tGetDLCCount          o_GetDLCCount          = nullptr;
static tBGetDLCDataByIndex   o_BGetDLCDataByIndex   = nullptr;
static tGetAppInstallDir     o_GetAppInstallDir     = nullptr;
static tUserHasLicenseForApp o_UserHasLicenseForApp = nullptr;

// ---------------------------------------------------------------------------
// Hooks
// ---------------------------------------------------------------------------
static bool H_BIsSubscribedApp(void* self, uint32_t id) {
    if (id == kBase || IsOurs(id)) return true;
    return o_BIsSubscribedApp ? o_BIsSubscribedApp(self, id) : true;
}

static bool H_BIsDlcInstalled(void* self, uint32_t id) {
    if (id == kBase || IsOurs(id)) return true;
    return o_BIsDlcInstalled ? o_BIsDlcInstalled(self, id) : true;
}

static uint32_t H_GetEarliestPurchase(void* self, uint32_t id) {
    if (IsOurs(id)) return 1609459200u;
    return o_GetEarliestPurchase ? o_GetEarliestPurchase(self, id) : 0u;
}

// Ours occupy [0 .. kDlcN); anything the emulator reports follows.
static int H_GetDLCCount(void* self) {
    int n = o_GetDLCCount ? o_GetDLCCount(self) : 0;
    return kDlcN + n;
}

static bool H_BGetDLCDataByIndex(void* self, int i, uint32_t* pApp, bool* pAvail,
                                 char* pName, int cch) {
    if (i >= 0 && i < kDlcN) {
        if (pApp)   *pApp = kDlc[i];
        if (pAvail) *pAvail = true;
        if (pName && cch > 0) strncpy_s(pName, (size_t)cch, kNames[i], _TRUNCATE);
        return true;
    }
    if (o_BGetDLCDataByIndex)
        return o_BGetDLCDataByIndex(self, i - kDlcN, pApp, pAvail, pName, cch);
    return false;
}

static uint32_t H_GetAppInstallDir(void* self, uint32_t id, char* folder, uint32_t cch) {
    uint32_t r = o_GetAppInstallDir ? o_GetAppInstallDir(self, id, folder, cch) : 0;
    if (r == 0 && IsOurs(id) && o_GetAppInstallDir)
        r = o_GetAppInstallDir(self, kBase, folder, cch);
    return r;
}

static int H_UserHasLicenseForApp(void* self, uint64_t sid, uint32_t id) {
    if (id == kBase || IsOurs(id)) return 1;   // k_EResultOK
    return o_UserHasLicenseForApp ? o_UserHasLicenseForApp(self, sid, id) : 1;
}

// ---------------------------------------------------------------------------
// Slot table (re-armable: the emulator may overwrite our writes at startup)
// ---------------------------------------------------------------------------
struct Slot { void** pslot; void* hook; void* original; const char* name; };
static Slot  g_slots[8];
static int   g_slotN = 0;
static HMODULE g_steam = nullptr;
static volatile LONG g_state = 0;

static Slot* FindSlot(void** pslot) {
    for (int i = 0; i < g_slotN; ++i) if (g_slots[i].pslot == pslot) return &g_slots[i];
    return nullptr;
}

static void Arm(void* obj, int index, void* hook, const char* name) {
    if (!obj) return;
    void** vt = *reinterpret_cast<void***>(obj);
    if (!vt) return;
    void** pslot = &vt[index];

    Slot* s = FindSlot(pslot);
    if (!s) {
        if (g_slotN >= 8) return;
        s = &g_slots[g_slotN++];
        s->pslot = pslot; s->hook = hook; s->original = nullptr; s->name = name;
    }

    DWORD old = 0;
    if (!VirtualProtect(pslot, sizeof(void*), PAGE_READWRITE, &old)) {
        Log("ARM FAILED %s (VirtualProtect)", name); return;
    }
    void* cur = *pslot;
    if (cur != hook) {
        if (!s->original) s->original = cur;
        *pslot = hook;
    }
    VirtualProtect(pslot, sizeof(void*), old, &old);
}

static void Rearm() {
    for (int i = 0; i < g_slotN; ++i) {
        DWORD old = 0;
        if (!VirtualProtect(g_slots[i].pslot, sizeof(void*), PAGE_READWRITE, &old)) continue;
        *g_slots[i].pslot = g_slots[i].hook;
        VirtualProtect(g_slots[i].pslot, sizeof(void*), old, &old);
    }
}

static void Verify() {
    for (int i = 0; i < g_slotN; ++i)
        Log("  slot %-24s %s", g_slots[i].name,
            (*g_slots[i].pslot == g_slots[i].hook) ? "ARMED" : "CLOBBERED");
}

static void* Resolve(const char* const* names, int n) {
    for (int i = 0; i < n; ++i) {
        FARPROC p = GetProcAddress(g_steam, names[i]);
        if (p) { typedef void* (*fn)(); return reinterpret_cast<fn>(p)(); }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Install
// ---------------------------------------------------------------------------
static void Install() {
    if (InterlockedCompareExchange(&g_state, 1, 0) != 0) return;

    g_steam = GetModuleHandleA("steam_api64.dll");
    if (!g_steam) { Log("steam_api64.dll not loaded yet"); g_state = 0; return; }

    static const char* appsN[] = { "SteamAPI_SteamApps_v008", "SteamAPI_SteamApps_v007" };
    static const char* userN[] = { "SteamAPI_SteamUser_v023", "SteamAPI_SteamUser_v022",
                                   "SteamAPI_SteamUser_v021" };
    void* apps = Resolve(appsN, 2);
    void* user = Resolve(userN, 3);

    if (!apps) { Log("FAILED: ISteamApps not resolved"); g_state = 0; return; }

    // ---- self-test BEFORE touching anything -------------------------------
    void** avt = *reinterpret_cast<void***>(apps);
    Log("BEFORE  BIsDlcInstalled(%u)=%d  GetDLCCount()=%d",
        kDlc[0], (int)((tBIsDlcInstalled)avt[7])(apps, kDlc[0]),
        (int)((tGetDLCCount)avt[10])(apps));

    // ---- arm ---------------------------------------------------------------
    Arm(apps,  6, (void*)&H_BIsSubscribedApp,     "ISteamApps::BIsSubscribedApp");
    Arm(apps,  7, (void*)&H_BIsDlcInstalled,      "ISteamApps::BIsDlcInstalled");
    Arm(apps,  8, (void*)&H_GetEarliestPurchase,  "ISteamApps::GetEarliestPurchase");
    Arm(apps, 10, (void*)&H_GetDLCCount,          "ISteamApps::GetDLCCount");
    Arm(apps, 11, (void*)&H_BGetDLCDataByIndex,   "ISteamApps::BGetDLCDataByIndex");
    Arm(apps, 18, (void*)&H_GetAppInstallDir,     "ISteamApps::GetAppInstallDir");
    if (user) Arm(user, 16, (void*)&H_UserHasLicenseForApp, "ISteamUser::UserHasLicenseForApp");
    else      Log("ISteamUser not resolved - licence check left to the emulator");

    // capture originals for pass-through
    o_BIsSubscribedApp    = (tBIsSubscribedApp)   g_slots[0].original;
    o_BIsDlcInstalled     = (tBIsDlcInstalled)    g_slots[1].original;
    o_GetEarliestPurchase = (tGetEarliestPurchase)g_slots[2].original;
    o_GetDLCCount         = (tGetDLCCount)        g_slots[3].original;
    o_BGetDLCDataByIndex  = (tBGetDLCDataByIndex) g_slots[4].original;
    o_GetAppInstallDir    = (tGetAppInstallDir)   g_slots[5].original;

    Log("armed %d slots", g_slotN);
    Verify();

    // ---- self-test AFTER ---------------------------------------------------
    Log("AFTER   BIsDlcInstalled(%u)=%d  GetDLCCount()=%d",
        kDlc[0], (int)((tBIsDlcInstalled)avt[7])(apps, kDlc[0]),
        (int)((tGetDLCCount)avt[10])(apps));

    g_state = 2;
}

static void Uninstall() {
    for (int i = 0; i < g_slotN; ++i) {
        DWORD old = 0;
        if (!VirtualProtect(g_slots[i].pslot, sizeof(void*), PAGE_READWRITE, &old)) continue;
        if (g_slots[i].original) *g_slots[i].pslot = g_slots[i].original;
        VirtualProtect(g_slots[i].pslot, sizeof(void*), old, &old);
    }
    Log("uninstalled");
}

// ---------------------------------------------------------------------------
// Entry points
// ---------------------------------------------------------------------------
static DWORD WINAPI Worker(LPVOID) {
    for (int i = 0; i < 300; ++i) {                 // ~30 s
        Install();
        if (g_state == 2) break;
        Sleep(100);
    }
    if (g_state != 2) { Log("gave up after 30s"); return 0; }

    // the emulator installs its own hooks during startup and can clobber
    // our vtable writes - re-assert twice, then report
    Sleep(2000); Rearm(); Log("re-arm @2s:"); Verify();
    Sleep(3000); Rearm(); Log("re-arm @5s:"); Verify();
    Log("ready");
    return 0;
}

extern "C" __declspec(dllexport) int  UCO_PluginInit(const void* /*ctx*/) {
    Log("UCO_PluginInit called");
    Install();
    return 0;
}
extern "C" __declspec(dllexport) void UCO_PluginShutdown(void) { Uninstall(); }

BOOL WINAPI DllMain(HMODULE h, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(h);
        Log("DllMain: plugin loaded");          // proves the loader found us
        Install();                               // synchronous first attempt
        HANDLE t = CreateThread(nullptr, 0, Worker, nullptr, 0, nullptr);
        if (t) CloseHandle(t);
    }
    return TRUE;
}
