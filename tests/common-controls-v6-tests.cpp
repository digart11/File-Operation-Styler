// Isolated manifest-free host. Failure injection affects this translation unit
// only; WH_EDITING keeps Windhawk APIs inert. Never touches Explorer.
#include <windows.h>
#include <commctrl.h>
#include <shlwapi.h>

HANDLE WINAPI TestCreateActCtxW(PCACTCTXW);
BOOL WINAPI TestActivateActCtx(HANDLE, ULONG_PTR *);
HMODULE WINAPI TestLoadLibraryExW(LPCWSTR, HANDLE, DWORD);
FARPROC WINAPI TestGetProcAddress(HMODULE, LPCSTR);
#define CreateActCtxW TestCreateActCtxW
#define ActivateActCtx TestActivateActCtx
#define LoadLibraryExW TestLoadLibraryExW
#define GetProcAddress TestGetProcAddress
#include "../file-operation-styler.wh.cpp"
#undef CreateActCtxW
#undef ActivateActCtx
#undef LoadLibraryExW
#undef GetProcAddress
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace
{
    enum class Failure { None, Context, Activation, Load, Version, Export };
    Failure failure = Failure::None;
    PCSTR missingExport = nullptr;
    int checks = 0;
    int callbacks = 0;
    constexpr UINT kTestMessage = WM_APP + 1;

    void Check(bool value, PCSTR message)
    {
        ++checks;
        if (!value)
        {
            std::fprintf(stderr, "FAIL: %s (error=%lu)\n", message, GetLastError());
            std::exit(1);
        }
    }

    HRESULT CALLBACK VersionFive(DLLVERSIONINFO *version)
    {
        version->dwMajorVersion = 5;
        version->dwMinorVersion = 82;
        return S_OK;
    }

    void CheckEmptyApi()
    {
        Check(!g_commonControlsV6Module && !SetWindowSubclass &&
              !GetWindowSubclass && !RemoveWindowSubclass && !DefSubclassProc,
              "failure/shutdown leaves no partial API");
        Check(!g_circleClassAtom && !g_infoPanelClassAtom &&
              !g_footerOverlayClassAtom && !g_gdiplusToken &&
              !g_ownedShell32Module && !g_ownedDui70Module &&
              g_subclassedHosts.empty() && g_circles.empty(),
              "compatibility failure precedes UI/skin initialization");
    }

    void CheckContextRestored(HANDLE expected)
    {
        HANDLE actual = nullptr;
        Check(GetCurrentActCtx(&actual), "read activation context after initialization");
        Check(actual == expected, "previous thread activation context restored");
        if (actual)
            ReleaseActCtx(actual);
    }

    LRESULT CALLBACK TestSubclass(HWND window, UINT message, WPARAM wParam,
                                  LPARAM lParam, UINT_PTR id, DWORD_PTR data)
    {
        if (message == kTestMessage)
        {
            ++callbacks;
            Check(data == 456, "updated subclass reference data delivered");
        }
        if (message == WM_NCDESTROY)
            Check(RemoveWindowSubclass(window, TestSubclass, id),
                  "owning-thread subclass teardown");
        return DefSubclassProc(window, message, wParam, lParam);
    }
}

HANDLE WINAPI TestCreateActCtxW(PCACTCTXW info)
{
    if (failure == Failure::Context)
    {
        SetLastError(ERROR_RESOURCE_NAME_NOT_FOUND);
        return INVALID_HANDLE_VALUE;
    }
    return CreateActCtxW(info);
}

BOOL WINAPI TestActivateActCtx(HANDLE context, ULONG_PTR *cookie)
{
    if (failure == Failure::Activation)
    {
        SetLastError(ERROR_NOT_ENOUGH_MEMORY);
        return FALSE;
    }
    return ActivateActCtx(context, cookie);
}

HMODULE WINAPI TestLoadLibraryExW(LPCWSTR path, HANDLE file, DWORD flags)
{
    if (failure == Failure::Load && lstrcmpiW(path, L"comctl32.dll") == 0)
    {
        SetLastError(ERROR_MOD_NOT_FOUND);
        return nullptr;
    }
    return LoadLibraryExW(path, file, flags);
}

FARPROC WINAPI TestGetProcAddress(HMODULE module, LPCSTR name)
{
    if (!IS_INTRESOURCE(name))
    {
        if (failure == Failure::Version && std::strcmp(name, "DllGetVersion") == 0)
            return reinterpret_cast<FARPROC>(VersionFive);
        if (failure == Failure::Export && std::strcmp(name, missingExport) == 0)
        {
            SetLastError(ERROR_PROC_NOT_FOUND);
            return nullptr;
        }
    }
    return GetProcAddress(module, name);
}

namespace
{
void RunCompatibilityChecks()
{
    // No v6 executable manifest: deliberately retain v5 before initializing.
    HMODULE legacy = LoadLibraryExW(L"comctl32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
    Check(legacy != nullptr, "load legacy Common Controls");
    auto legacyVersion = reinterpret_cast<HRESULT(CALLBACK *)(DLLVERSIONINFO *)>(
        GetProcAddress(legacy, "DllGetVersion"));
    DLLVERSIONINFO version{sizeof(version)};
    Check(legacyVersion && SUCCEEDED(legacyVersion(&version)) &&
          version.dwMajorVersion == 5, "test starts in a v5 host");
    std::printf("Host Common Controls: %lu.%lu\n", version.dwMajorVersion, version.dwMinorVersion);
    std::printf("Host named SetWindowSubclass export: %s\n",
                GetProcAddress(legacy, "SetWindowSubclass") ? "present" : "absent");

    HANDLE previous = nullptr;
    Check(GetCurrentActCtx(&previous), "read original thread activation context");
    for (auto mode : {Failure::Context, Failure::Activation, Failure::Load,
                      Failure::Version, Failure::Export})
    {
        failure = mode;
        for (PCSTR name : {"DllGetVersion", "SetWindowSubclass", "GetWindowSubclass",
                           "RemoveWindowSubclass", "DefSubclassProc"})
        {
            missingExport = name;
            Check(!Wh_ModInit(), "initialization fails closed");
            CheckEmptyApi();
            CheckContextRestored(previous);
            ShutdownCommonControlsSubclassApi();
            if (mode != Failure::Export)
                break;
        }
    }
    failure = Failure::None;
    for (int pass = 0; pass < 2; ++pass)
    {
        Check(InitializeCommonControlsSubclassApi(), "initialize independently of host manifest");
        HMODULE selected = g_commonControlsV6Module;
        Check(selected != legacy, "v6 and v5 coexist as distinct modules");
        Check(InitializeCommonControlsSubclassApi() && g_commonControlsV6Module == selected,
              "initialization is idempotent");
        CheckContextRestored(previous);
        auto selectedVersion = reinterpret_cast<HRESULT(CALLBACK *)(DLLVERSIONINFO *)>(
            GetProcAddress(selected, "DllGetVersion"));
        Check(selectedVersion && SUCCEEDED(selectedVersion(&version)) &&
              version.dwMajorVersion == 6, "selected module is v6 after context release");
        wchar_t path[MAX_PATH]{};
        Check(GetModuleFileNameW(selected, path, ARRAYSIZE(path)) != 0, "read selected DLL path");
        std::printf("Selected Common Controls: %lu.%lu build %lu\n", version.dwMajorVersion,
                    version.dwMinorVersion, version.dwBuildNumber);
        std::wprintf(L"Selected DLL: %ls\n", path);

        HWND window = CreateWindowExW(WS_EX_TOOLWINDOW, L"STATIC", L"native text",
                                     WS_POPUP, 0, 0, 1, 1, nullptr, nullptr,
                                     GetModuleHandleW(nullptr), nullptr);
        Check(window != nullptr, "create disposable hidden window");
        Check(SetWindowSubclass(window, TestSubclass, 123, 123), "install subclass");
        Check(SetWindowSubclass(window, TestSubclass, 123, 456), "update existing subclass");
        DWORD_PTR data = 0;
        Check(GetWindowSubclass(window, TestSubclass, 123, &data) && data == 456,
              "query subclass data");
        int before = callbacks;
        SendMessageW(window, kTestMessage, 0, 0);
        Check(callbacks == before + 1, "subclass dispatches after context is gone");
        wchar_t text[32]{};
        Check(GetWindowTextW(window, text, ARRAYSIZE(text)) &&
              lstrcmpW(text, L"native text") == 0, "DefSubclassProc preserves native behavior");
        Check(RemoveWindowSubclass(window, TestSubclass, 123), "explicit subclass removal");
        Check(!GetWindowSubclass(window, TestSubclass, 123, &data), "removal confirmed");
        Check(SetWindowSubclass(window, TestSubclass, 123, 456), "reinstall for destruction");
        Check(DestroyWindow(window), "destroy with owning-thread teardown");
        ShutdownCommonControlsSubclassApi();
        ShutdownCommonControlsSubclassApi();
        CheckEmptyApi();
    }
    if (previous)
        ReleaseActCtx(previous);
    FreeLibrary(legacy);
    std::printf("PASS: %d Common Controls compatibility checks\n", checks);
}
}

int main()
{
    RunCompatibilityChecks();
}
