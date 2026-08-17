// ==WindhawkMod==
// @id              file-operation-styler
// @name            File Operation Styler
// @description     Experimental dark skin for native file-operation tiles.
// @version         0.10.41
// @author          digART
// @license         GPL-3.0
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -lgdi32 -lgdiplus -lshlwapi
// ==/WindhawkMod==

// Experimental first-pass skin for the native DirectUI file-operation tile.
// File-operation behavior and the native progress, chart, and button controls
// remain unchanged.

#include <windhawk_utils.h>

#include <commctrl.h>
#include <gdiplus.h>
#include <shlwapi.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cwchar>
#include <limits>
#include <mutex>
#include <string>
#include <vector>

struct COperationStatusTile;
struct COperationStatusTileRateCalculator;
struct OperationTileElement;

namespace DirectUI
{
    struct DUIXmlParser;
    struct Element;
    struct PropertyInfo;
    struct Value;
} // namespace DirectUI

namespace
{

    static_assert(sizeof(void *) == 8);
    static_assert(sizeof(unsigned long) == sizeof(ULONG));
    static_assert(sizeof(ATOM) == sizeof(unsigned short));

    struct SkinState
    {
        bool active;
        bool collecting;
        DirectUI::Element *operationTileRoot;
        DirectUI::Element *tileHeaderRoot;
    };

    thread_local SkinState g_skinState{};
    std::atomic<unsigned long long> g_skinEventSequence{};
    std::atomic<unsigned long long> g_displayTransitionSequence{};

    void ClearSkinState()
    {
        ZeroMemory(&g_skinState, sizeof(g_skinState));
    }

    class ScopedTileSkin
    {
    public:
        ScopedTileSkin()
        {
            auto &state = g_skinState;
            if (state.active)
            {
                nestedCall_ = true;
                previousCollecting_ = state.collecting;
                state.collecting = false;
                return;
            }

            ClearSkinState();
            state.active = true;
            state.collecting = true;
            ownsSkin_ = true;
        }

        ~ScopedTileSkin()
        {
            if (ownsSkin_)
            {
                ClearSkinState();
            }
            else if (nestedCall_)
            {
                g_skinState.collecting = previousCollecting_;
            }
        }

        ScopedTileSkin(ScopedTileSkin const &) = delete;
        ScopedTileSkin &operator=(ScopedTileSkin const &) = delete;

        bool OwnsSkin() const
        {
            return ownsSkin_;
        }

    private:
        bool ownsSkin_ = false;
        bool nestedCall_ = false;
        bool previousCollecting_ = false;
    };

    // Exact verified x64 member-function ABI for the dui70.dll export:
    // ?CreateElement@DUIXmlParser@DirectUI@@QEAAJPEBGPEAVElement@2@1PEAKPEAPEAV32@@Z
    // The raw detour receives the implicit DUIXmlParser "this" pointer first.
    using DUIXmlParser_CreateElement_t = HRESULT(__cdecl *)(
        DirectUI::DUIXmlParser *parser,
        PCWSTR resourceName,
        DirectUI::Element *parent,
        DirectUI::Element *insertBefore,
        ULONG *deferCookie,
        DirectUI::Element **createdElement);
    DUIXmlParser_CreateElement_t DUIXmlParser_CreateElement_Original;

    HRESULT __cdecl DUIXmlParser_CreateElement_Hook(
        DirectUI::DUIXmlParser *parser,
        PCWSTR resourceName,
        DirectUI::Element *parent,
        DirectUI::Element *insertBefore,
        ULONG *deferCookie,
        DirectUI::Element **createdElement)
    {
        auto &state = g_skinState;
        if (!state.active || !state.collecting)
        {
            return DUIXmlParser_CreateElement_Original(
                parser, resourceName, parent, insertBefore, deferCookie,
                createdElement);
        }

        bool isOperationTile = false;
        bool isTileHeader = false;
        if (resourceName && !IS_INTRESOURCE(resourceName))
        {
            isOperationTile = lstrcmpW(resourceName, L"idOperationTile") == 0;
            isTileHeader = lstrcmpW(resourceName, L"idTileHeader") == 0;
        }

        HRESULT result = DUIXmlParser_CreateElement_Original(
            parser, resourceName, parent, insertBefore, deferCookie,
            createdElement);

        if (SUCCEEDED(result) && createdElement && *createdElement)
        {
            if (isOperationTile)
            {
                state.operationTileRoot = *createdElement;
            }
            else if (isTileHeader)
            {
                state.tileHeaderRoot = *createdElement;
            }
        }

        return result;
    }

    // Verified DUI70!StrToID ABI. The returned atom is used only for the immediate
    // lookup and is never retained or hard-coded.
    using StrToID_t = ATOM(WINAPI *)(PCWSTR resourceName);
    StrToID_t StrToID_Original;

    // Exact verified x64 member-function ABI for:
    // ?FindDescendent@Element@DirectUI@@QEAAPEAV12@G@Z
    // The raw call includes the implicit DirectUI::Element "this" pointer first.
    using Element_FindDescendent_t = DirectUI::Element *(__cdecl *)(DirectUI::Element * thisPtr,
                                                                    unsigned short id);
    Element_FindDescendent_t Element_FindDescendent_Original;

    // Verified dui70.dll export:
    // ?GetParent@Element@DirectUI@@QEAAPEAV12@XZ
    using Element_GetParent_t = DirectUI::Element *(__cdecl *)(DirectUI::Element * thisPtr);
    Element_GetParent_t Element_GetParent_Original;

    using Element_SetBackgroundColor_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        COLORREF color);
    Element_SetBackgroundColor_t Element_SetBackgroundColor_Original;

    // Exact verified x64 member-function ABI for:
    // ?SetForegroundColor@Element@DirectUI@@QEAAJK@Z
    using Element_SetForegroundColor_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        COLORREF color);
    Element_SetForegroundColor_t Element_SetForegroundColor_Original;

    using Element_SetFontFace_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        PCWSTR fontFace);
    Element_SetFontFace_t Element_SetFontFace_Original;

    using Element_SetFontSize_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        int fontSize);
    Element_SetFontSize_t Element_SetFontSize_Original;

    using Element_SetFontWeight_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        int fontWeight);
    Element_SetFontWeight_t Element_SetFontWeight_Original;

    // Exact verified x64 member-function ABIs exported by dui70.dll.
    using Element_SetRelPixWidth_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        int width);
    Element_SetRelPixWidth_t Element_SetRelPixWidth_Original;

    using Element_SetMargin_t = HRESULT(__cdecl *)(DirectUI::Element *thisPtr,
                                                   int left,
                                                   int top,
                                                   int right,
                                                   int bottom);
    Element_SetMargin_t Element_SetMargin_Original;

    using Element_SetPadding_t = HRESULT(__cdecl *)(DirectUI::Element *thisPtr,
                                                    int left,
                                                    int top,
                                                    int right,
                                                    int bottom);
    Element_SetPadding_t Element_SetPadding_Original;

    using Element_SetBorderColor_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        COLORREF color);
    Element_SetBorderColor_t Element_SetBorderColor_Original;

    using Element_SetBorderThickness_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        int left,
        int top,
        int right,
        int bottom);
    Element_SetBorderThickness_t Element_SetBorderThickness_Original;

    using Element_SetVisible_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        bool visible);
    Element_SetVisible_t Element_SetVisible_Original;

    // Verified dui70.dll export on the Windows 11 24H2 diagnostic target:
    // ?GetVisible@Element@DirectUI@@QEAA_NXZ
    using Element_GetVisible_t = bool(__cdecl *)(
        DirectUI::Element *thisPtr);
    Element_GetVisible_t Element_GetVisible_Original;

    using Element_SetRelPixHeight_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        int height);
    Element_SetRelPixHeight_t Element_SetRelPixHeight_Original;

    using Element_SetContentString_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        PCWSTR content);
    Element_SetContentString_t Element_SetContentString_Original;

    using Element_GetRootRelativeBounds_t = HRESULT(__cdecl *)(
        DirectUI::Element *thisPtr,
        RECT *bounds);
    Element_GetRootRelativeBounds_t Element_GetRootRelativeBounds_Original;

    // Exact build-matched x64 ABIs from shell32.pdb and DUI70 exports. The
    // OnPropertyChanged disassembly reads the second Value* with Value::GetInt,
    // then sends that integer to the native progress HWND with PBM_SETPOS.
    using OperationTileElement_ProgressPositionProp_t =
        DirectUI::PropertyInfo const *(__cdecl *)();
    OperationTileElement_ProgressPositionProp_t
        OperationTileElement_ProgressPositionProp_Original;

    using OperationTileElement_GetProgressHWND_t = HWND(__cdecl *)(
        OperationTileElement *thisPtr);
    OperationTileElement_GetProgressHWND_t
        OperationTileElement_GetProgressHWND_Original;

    using OperationTileElement_OnPropertyChanged_t = void(__cdecl *)(
        OperationTileElement *thisPtr,
        DirectUI::PropertyInfo const *property,
        int propertyIndex,
        DirectUI::Value *oldValue,
        DirectUI::Value *newValue);
    OperationTileElement_OnPropertyChanged_t
        OperationTileElement_OnPropertyChanged_Original;

    using OperationTileElement_Destructor_t = void(__cdecl *)(
        OperationTileElement *thisPtr);
    OperationTileElement_Destructor_t OperationTileElement_Destructor_Original;

    using COperationStatusTile_UpdateRemainingItemsAndSize_t =
        HRESULT(__cdecl *)(COperationStatusTile *thisPtr,
                           unsigned long long completedItems,
                           unsigned long long totalItems,
                           unsigned long long completedBytes,
                           unsigned long long totalBytes);
    COperationStatusTile_UpdateRemainingItemsAndSize_t
        COperationStatusTile_UpdateRemainingItemsAndSize_Original;

    using COperationStatusTile_UpdateSummary_t = HRESULT(__cdecl *)(
        COperationStatusTile *thisPtr,
        PCWSTR summary);
    COperationStatusTile_UpdateSummary_t COperationStatusTile_UpdateSummary_Original;

    using COperationStatusTile_SetTileDisplayMode_t = HRESULT(__cdecl *)(
        COperationStatusTile *thisPtr,
        bool expanded);
    COperationStatusTile_SetTileDisplayMode_t
        COperationStatusTile_SetTileDisplayMode_Original;

    using COperationStatusTileRateCalculator_CalculateRate_t = double(__cdecl *)(
        COperationStatusTileRateCalculator *thisPtr,
        unsigned long long value1,
        unsigned long long value2,
        unsigned long long value3,
        unsigned long long value4,
        unsigned long long value5,
        unsigned long long value6,
        unsigned long long value7,
        double *secondaryRate);
    COperationStatusTileRateCalculator_CalculateRate_t
        COperationStatusTileRateCalculator_CalculateRate_Original;

    // Windows 11 24H2 shell32.dll 10.0.26100.8972, PDB
    // 4907816C-76AB-D628-8BBE-01D3E8033EE9 age 1:
    // - COperationStatusTile's constructor writes the
    //   IOperationStatusTilePriv vftable at complete-object offset 0x18.
    // - SetTileDisplayMode is at RVA 0x3943A0. Its entry is a full method
    //   body, not an adjustor thunk, and it explicitly computes the complete
    //   object with LEA reg,[this-18h] before internal tile calls.
    constexpr ULONG_PTR kSetTileDisplayModeThisAdjustment = 0x18;

    struct TransferSummaryState
    {
        COperationStatusTile *owner;
        OperationTileElement *tile;
        DirectUI::Element *operationTileRoot;
        DirectUI::Element *tileHeaderRoot;
        unsigned long long completedBytes;
        unsigned long long totalBytes;
        bool bytesValid;
        std::wstring nativeSummary;
        bool displayModeKnown;
        bool expanded;
        double nativeDisplayRate = 0.0;
        bool nativeDisplayRateValid = false;
        unsigned long long completedItems = 0;
        unsigned long long totalItems = 0;
        bool itemsValid = false;
        std::vector<double> nativeRateHistory;
    };

    std::mutex g_transferSummaryMutex;
    std::vector<TransferSummaryState> g_transferSummaries;

    constexpr COLORREF kBackgroundColor = RGB(44, 44, 44);
    constexpr COLORREF kPrimaryTextColor = RGB(242, 244, 247);
    constexpr COLORREF kSecondaryTextColor = RGB(154, 163, 174);
    constexpr int kRequestedTileWidth = 550;
    constexpr int kReservedLeftWidth = 156;
    constexpr int kContentTopPadding = 4;
    constexpr int kContentRightPadding = 10;
    constexpr int kContentBottomPadding = 4;
    constexpr int kTileVerticalMargin = 0;
    constexpr int kCircleColumnWidth = 156;
    constexpr int kCircleWindowHeight = 130;
    constexpr int kCircleDiameter = 118;
    constexpr int kCircleTop = 4;
    // Keep the proven ring geometry. The right-side layout is being tightened
    // independently so the graph can remain useful without making the whole
    // window excessively wide.
    constexpr int kCircleHostY = 4;
    constexpr int kCircleStrokeWidth = 7;
    constexpr COLORREF kInactiveRingColor = RGB(58, 65, 74);
    constexpr COLORREF kAccentRingColor = RGB(21, 151, 229);
    constexpr COLORREF kGraphSurfaceColor = RGB(36, 40, 46);
    constexpr COLORREF kActionSurfaceColor = RGB(52, 57, 64);

    // Windows 11 DWM attributes. Resolve DwmSetWindowAttribute dynamically so
    // the mod doesn't need an additional import library. Making the caption
    // use the same color as the client area removes the stock gray "tab"
    // look and makes OperationStatusWindow read as one continuous card.
    constexpr DWORD kDwmwaUseImmersiveDarkMode = 20;
    constexpr DWORD kDwmwaBorderColor = 34;
    constexpr DWORD kDwmwaCaptionColor = 35;
    constexpr DWORD kDwmwaTextColor = 36;
    constexpr COLORREF kDwmColorDefault = 0xFFFFFFFF;

    using DwmSetWindowAttribute_t = HRESULT(WINAPI *)(
        HWND hwnd, DWORD attribute, LPCVOID value, DWORD valueSize);

    DwmSetWindowAttribute_t GetDwmSetWindowAttribute()
    {
        static DwmSetWindowAttribute_t function = []() -> DwmSetWindowAttribute_t
        {
            HMODULE module = LoadLibraryExW(
                L"dwmapi.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
            if (!module)
            {
                return nullptr;
            }
            return reinterpret_cast<DwmSetWindowAttribute_t>(
                GetProcAddress(module, "DwmSetWindowAttribute"));
        }();
        return function;
    }

    void ApplyUnifiedHostChrome(HWND hostWindow)
    {
        DwmSetWindowAttribute_t setAttribute = GetDwmSetWindowAttribute();
        if (!setAttribute || !hostWindow || !IsWindow(hostWindow))
        {
            return;
        }

        BOOL darkMode = TRUE;
        COLORREF captionColor = kBackgroundColor;
        COLORREF textColor = kPrimaryTextColor;
        COLORREF borderColor = kInactiveRingColor;
        setAttribute(hostWindow, kDwmwaUseImmersiveDarkMode,
                     &darkMode, sizeof(darkMode));
        setAttribute(hostWindow, kDwmwaCaptionColor,
                     &captionColor, sizeof(captionColor));
        setAttribute(hostWindow, kDwmwaTextColor,
                     &textColor, sizeof(textColor));
        setAttribute(hostWindow, kDwmwaBorderColor,
                     &borderColor, sizeof(borderColor));
    }

    void ResetUnifiedHostChrome(HWND hostWindow)
    {
        DwmSetWindowAttribute_t setAttribute = GetDwmSetWindowAttribute();
        if (!setAttribute || !hostWindow || !IsWindow(hostWindow))
        {
            return;
        }

        COLORREF defaultColor = kDwmColorDefault;
        setAttribute(hostWindow, kDwmwaCaptionColor,
                     &defaultColor, sizeof(defaultColor));
        setAttribute(hostWindow, kDwmwaTextColor,
                     &defaultColor, sizeof(defaultColor));
        setAttribute(hostWindow, kDwmwaBorderColor,
                     &defaultColor, sizeof(defaultColor));
    }

    constexpr int kGraphHeight = 52;
    constexpr int kChartAreaHeight = 60;
    constexpr int kChartAreaTopMargin = 4;
    constexpr int kChartAreaBottomMargin = 3;
    constexpr int kDisplayModeFooterReserveHeight = 28;
    constexpr int kCustomCommonClientHeight =
        kCircleHostY + kCircleWindowHeight +
        kDisplayModeFooterReserveHeight;
    constexpr int kExpandedChartSectionHeight =
        kChartAreaTopMargin + kChartAreaHeight +
        kChartAreaBottomMargin;
    constexpr wchar_t kCircleWindowClass[] =
        L"Windhawk.FileOperationStyler.ProgressCircle.0.10.3";
    constexpr wchar_t kInfoPanelWindowClass[] =
        L"Windhawk.FileOperationStyler.InfoPanel.0.10.41";
    constexpr int kInfoPanelTop = 52;
    constexpr int kInfoPanelCommonHeight = 58;
    constexpr int kInfoPanelExpandedHeight = 130;
    constexpr int kInfoPanelTextRowHeight = 18;
    constexpr int kInfoPanelProgressTop = 43;
    constexpr int kInfoPanelProgressHeight = 8;
    constexpr int kInfoPanelChartTop = 68;
    constexpr int kInfoPanelChartHeight = 60;
    constexpr int kCompactRegularTileHeight = 118;
    constexpr int kExpandedRegularTileHeight = 185;
    constexpr size_t kInfoPanelRateHistorySamples = 72;
    constexpr UINT_PTR kHostWindowSubclassId = 0xF0510010;
    constexpr UINT_PTR kProgressWindowSubclassId = 0xF0510011;

    struct CircleState
    {
        OperationTileElement *tile;
        HWND circleWindow;
        HWND infoWindow;
        HWND progressWindow;
        HWND hostWindow;
        int progressPercent;
        int progressRangeLow;
        int progressRangeHigh;
        bool progressRangeInitialized;
        bool progressRangeValid;
        unsigned long long eventId;
        int positionX;
        int positionY;
        int positionWidth;
        int positionHeight;
        bool positionValid;
    };

    struct HostPositionRequest
    {
        HWND hostWindow;
        PCWSTR reason;
    };

    struct DeferredDisplaySnapshot
    {
        COperationStatusTile *owner;
        OperationTileElement *tile;
        DirectUI::Element *operationTileRoot;
        HWND hostWindow;
        DWORD uiThreadId;
        unsigned long long transitionId;
        bool requestedExpanded;
    };

    std::mutex g_circleMutex;
    std::vector<CircleState> g_circles;
    std::vector<HWND> g_subclassedHosts;
    std::vector<HostPositionRequest> g_hostPositionRequests;
    std::mutex g_displayDiagnosticMutex;
    std::vector<DeferredDisplaySnapshot> g_deferredDisplaySnapshots;
    HINSTANCE g_circleClassInstance;
    ATOM g_circleClassAtom;
    ATOM g_infoPanelClassAtom;
    ULONG_PTR g_gdiplusToken;
    UINT g_removeHostSubclassMessage;
    UINT g_positionCirclesMessage;
    UINT g_logDisplayStateMessage;

    void LogSetterFailure(unsigned long long eventId,
                          PCWSTR target,
                          PCWSTR property,
                          HRESULT result)
    {
        Wh_Log(L"eventId=%llu skin setter-failed target=%s property=%s "
               L"result=0x%08X",
               eventId, target, property, static_cast<unsigned int>(result));
    }

    DirectUI::Element *FindSkinElement(DirectUI::Element *tileRoot,
                                       DirectUI::Element *tileHeaderRoot,
                                       PCWSTR name,
                                       bool allowHeaderFallback)
    {
        ATOM id = StrToID_Original(name);
        if (!id)
        {
            return nullptr;
        }

        DirectUI::Element *element =
            Element_FindDescendent_Original(tileRoot, id);
        if (!element && allowHeaderFallback && tileHeaderRoot)
        {
            element = Element_FindDescendent_Original(tileHeaderRoot, id);
        }

        return element;
    }

    struct TextSkinResult
    {
        bool foregroundApplied;
        bool fontApplied;
    };

    TextSkinResult ApplyTextSkin(unsigned long long eventId,
                                 DirectUI::Element *tileRoot,
                                 DirectUI::Element *tileHeaderRoot,
                                 PCWSTR name,
                                 COLORREF color,
                                 bool allowHeaderFallback)
    {
        DirectUI::Element *element = FindSkinElement(
            tileRoot, tileHeaderRoot, name, allowHeaderFallback);
        if (!element)
        {
            return {};
        }

        HRESULT foregroundResult =
            Element_SetForegroundColor_Original(element, color);
        if (FAILED(foregroundResult))
        {
            LogSetterFailure(eventId, name, L"foreground", foregroundResult);
        }

        HRESULT fontResult =
            Element_SetFontFace_Original(element, L"Segoe UI Variable");
        if (FAILED(fontResult))
        {
            LogSetterFailure(eventId, name, L"font-face", fontResult);
        }

        return {
            SUCCEEDED(foregroundResult),
            SUCCEEDED(fontResult),
        };
    }

    struct WindowLookupContext
    {
        unsigned long long eventId;
        HWND operationStatusWindow;
    };

    BOOL CALLBACK FindOperationStatusWindow(HWND window, LPARAM parameter)
    {
        auto *context = reinterpret_cast<WindowLookupContext *>(parameter);
        wchar_t className[64];
        int classNameLength =
            GetClassNameW(window, className, ARRAYSIZE(className));
        if (!classNameLength)
        {
            Wh_Log(L"eventId=%llu base-layout GetClassNameW failed hwnd=%p "
                   L"error=%lu",
                   context->eventId, reinterpret_cast<void *>(window),
                   GetLastError());
            return TRUE;
        }

        if (lstrcmpW(className, L"OperationStatusWindow") == 0)
        {
            context->operationStatusWindow = window;
            return FALSE;
        }

        return TRUE;
    }

    int ScaleForDpi(int value, UINT dpi)
    {
        return MulDiv(value, static_cast<int>(dpi), USER_DEFAULT_SCREEN_DPI);
    }

    HWND FindCurrentThreadOperationStatusWindow(unsigned long long eventId)
    {
        WindowLookupContext context{eventId, nullptr};
        SetLastError(ERROR_SUCCESS);
        BOOL enumResult = EnumThreadWindows(
            GetCurrentThreadId(), FindOperationStatusWindow,
            reinterpret_cast<LPARAM>(&context));
        if (!context.operationStatusWindow)
        {
            DWORD error = enumResult ? ERROR_NOT_FOUND : GetLastError();
            if (error == ERROR_SUCCESS)
            {
                error = ERROR_NOT_FOUND;
            }
            Wh_Log(L"eventId=%llu circle OperationStatusWindow lookup failed "
                   L"error=%lu",
                   eventId, error);
        }

        return context.operationStatusWindow;
    }

    void DestroyProgressCircle(OperationTileElement *tile);
    void PositionProgressCirclesForHost(HWND hostWindow, PCWSTR reason);
    void PositionInfoPanel(OperationTileElement *tile);
    void InvalidateInfoPanelForTile(OperationTileElement *tile);
    void ScheduleProgressCirclePosition(HWND hostWindow, PCWSTR reason);
    void HandleDeferredDisplaySnapshot(HWND hostWindow,
                                       unsigned long long transitionId);
    void LogFinalDisplayInvariant(COperationStatusTile *owner,
                                  HWND hostWindow,
                                  bool expanded,
                                  bool applyResult,
                                  unsigned long long transitionId);
    bool ApplyDisplayMode(COperationStatusTile *owner,
                          bool applyFinalHostGeometry,
                          unsigned long long transitionId = 0);
    void InitializeRegisteredDisplayMode(COperationStatusTile *owner);
    bool GetVerifiedCustomHostWindowHeight(HWND hostWindow,
                                           int nativeWindowHeight,
                                           int *targetWindowHeight);
    bool IsSingleNormalProgressTileForHost(OperationTileElement *tile,
                                           HWND hostWindow);
    void ApplyNativeDisplayRatesForHost(HWND hostWindow);
    void CancelDeferredDisplaySnapshotsForHost(HWND hostWindow);
    void CancelDeferredDisplaySnapshotsForTile(OperationTileElement *tile);
    LRESULT CALLBACK NativeProgressWindowSubclassProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData);

    PCWSTR WindowMessageName(UINT message)
    {
        switch (message)
        {
        case WM_SIZE:
            return L"WM_SIZE";
        case WM_SHOWWINDOW:
            return L"WM_SHOWWINDOW";
        case WM_WINDOWPOSCHANGING:
            return L"WM_WINDOWPOSCHANGING";
        case WM_WINDOWPOSCHANGED:
            return L"WM_WINDOWPOSCHANGED";
        case WM_NCDESTROY:
            return L"WM_NCDESTROY";
        default:
            return L"UNKNOWN";
        }
    }

    void LogTargetWindowMessage(PCWSTR target,
                                PCWSTR phase,
                                HWND window,
                                UINT message,
                                WPARAM wParam,
                                LPARAM lParam)
    {
        if (message == WM_WINDOWPOSCHANGING ||
            message == WM_WINDOWPOSCHANGED)
        {
            auto *windowPosition = reinterpret_cast<WINDOWPOS *>(lParam);
            if (windowPosition)
            {
                Wh_Log(L"MODE_EVENT target=%s phase=%s message=%s hwnd=%p "
                       L"thread=%lu insertAfter=%p x=%d y=%d cx=%d cy=%d "
                       L"flags=0x%08X",
                       target, phase, WindowMessageName(message),
                       reinterpret_cast<void *>(window),
                       GetCurrentThreadId(),
                       reinterpret_cast<void *>(windowPosition->hwndInsertAfter),
                       windowPosition->x, windowPosition->y,
                       windowPosition->cx, windowPosition->cy,
                       windowPosition->flags);
                return;
            }
        }

        Wh_Log(L"MODE_EVENT target=%s phase=%s message=%s hwnd=%p "
               L"thread=%lu wParam=0x%llX lParam=0x%llX isWindow=%s "
               L"visible=%s",
               target, phase, WindowMessageName(message),
               reinterpret_cast<void *>(window), GetCurrentThreadId(),
               static_cast<unsigned long long>(wParam),
               static_cast<unsigned long long>(lParam),
               IsWindow(window) ? L"yes" : L"no",
               IsWindow(window) && IsWindowVisible(window) ? L"yes" : L"no");
    }

    int GetCircleProgress(HWND circleWindow)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        for (auto const &state : g_circles)
        {
            if (state.circleWindow == circleWindow)
            {
                return state.progressPercent;
            }
        }
        return 0;
    }

    void DrawProgressCircleFrame(HWND circleWindow,
                                 HDC deviceContext,
                                 RECT const &clientRect)
    {
        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.top;
        UINT dpi = GetDpiForWindow(circleWindow);
        if (!dpi)
        {
            dpi = USER_DEFAULT_SCREEN_DPI;
        }

        Gdiplus::Graphics graphics(deviceContext);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetTextRenderingHint(Gdiplus::TextRenderingHintClearTypeGridFit);

        Gdiplus::SolidBrush backgroundBrush(Gdiplus::Color(
            255, GetRValue(kBackgroundColor), GetGValue(kBackgroundColor),
            GetBValue(kBackgroundColor)));
        graphics.FillRectangle(&backgroundBrush, 0, 0, width, height);

        Gdiplus::REAL diameter =
            static_cast<Gdiplus::REAL>(ScaleForDpi(kCircleDiameter, dpi));
        Gdiplus::REAL ringLeft =
            (static_cast<Gdiplus::REAL>(width) - diameter) / 2.0f;
        Gdiplus::REAL ringTop =
            static_cast<Gdiplus::REAL>(ScaleForDpi(kCircleTop, dpi));
        Gdiplus::REAL strokeWidth = static_cast<Gdiplus::REAL>(
            ScaleForDpi(kCircleStrokeWidth, dpi));
        Gdiplus::RectF ringBounds(ringLeft + strokeWidth / 2.0f,
                                  ringTop + strokeWidth / 2.0f,
                                  diameter - strokeWidth,
                                  diameter - strokeWidth);

        Gdiplus::Pen inactivePen(
            Gdiplus::Color(255, GetRValue(kInactiveRingColor),
                           GetGValue(kInactiveRingColor),
                           GetBValue(kInactiveRingColor)),
            strokeWidth);
        graphics.DrawEllipse(&inactivePen, ringBounds);

        int nativeProgress = GetCircleProgress(circleWindow);
        int displayProgress = std::clamp(nativeProgress, 0, 100);
        if (displayProgress > 0)
        {
            Gdiplus::Pen accentPen(
                Gdiplus::Color(255, GetRValue(kAccentRingColor),
                               GetGValue(kAccentRingColor),
                               GetBValue(kAccentRingColor)),
                strokeWidth);
            accentPen.SetStartCap(Gdiplus::LineCapRound);
            accentPen.SetEndCap(Gdiplus::LineCapRound);
            graphics.DrawArc(&accentPen, ringBounds, -90.0f,
                             static_cast<Gdiplus::REAL>(displayProgress) *
                                 3.6f);
        }

        wchar_t percentageText[16];
        wsprintfW(percentageText, L"%d%%", displayProgress);
        Gdiplus::Font percentageFont(
            L"Segoe UI Variable Display",
            static_cast<Gdiplus::REAL>(ScaleForDpi(31, dpi)),
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font percentageFallback(
            L"Segoe UI",
            static_cast<Gdiplus::REAL>(ScaleForDpi(31, dpi)),
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font labelFont(
            L"Segoe UI Variable",
            static_cast<Gdiplus::REAL>(ScaleForDpi(11, dpi)),
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font labelFallback(
            L"Segoe UI",
            static_cast<Gdiplus::REAL>(ScaleForDpi(11, dpi)),
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font *selectedPercentageFont =
            percentageFont.GetLastStatus() == Gdiplus::Ok
                ? &percentageFont
                : &percentageFallback;
        Gdiplus::Font *selectedLabelFont =
            labelFont.GetLastStatus() == Gdiplus::Ok ? &labelFont
                                                     : &labelFallback;

        Gdiplus::SolidBrush primaryBrush(Gdiplus::Color(
            255, GetRValue(kPrimaryTextColor), GetGValue(kPrimaryTextColor),
            GetBValue(kPrimaryTextColor)));
        Gdiplus::SolidBrush secondaryBrush(Gdiplus::Color(
            255, GetRValue(kSecondaryTextColor),
            GetGValue(kSecondaryTextColor), GetBValue(kSecondaryTextColor)));
        Gdiplus::StringFormat centeredText;
        centeredText.SetAlignment(Gdiplus::StringAlignmentCenter);
        centeredText.SetLineAlignment(Gdiplus::StringAlignmentCenter);

        Gdiplus::RectF percentageBounds(
            ringLeft, ringTop + diameter * 0.29f, diameter,
            static_cast<Gdiplus::REAL>(ScaleForDpi(42, dpi)));
        Gdiplus::RectF labelBounds(
            ringLeft, ringTop + diameter * 0.59f, diameter,
            static_cast<Gdiplus::REAL>(ScaleForDpi(24, dpi)));
        graphics.DrawString(percentageText, -1, selectedPercentageFont,
                            percentageBounds, &centeredText, &primaryBrush);
        graphics.DrawString(L"Complete", -1, selectedLabelFont, labelBounds,
                            &centeredText, &secondaryBrush);
    }

    void PaintProgressCircle(HWND circleWindow)
    {
        PAINTSTRUCT paint;
        HDC paintDeviceContext = BeginPaint(circleWindow, &paint);
        if (!paintDeviceContext)
        {
            return;
        }

        RECT clientRect;
        if (!GetClientRect(circleWindow, &clientRect))
        {
            EndPaint(circleWindow, &paint);
            return;
        }
        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.top;
        if (width <= 0 || height <= 0)
        {
            EndPaint(circleWindow, &paint);
            return;
        }

        HDC memoryDeviceContext = CreateCompatibleDC(paintDeviceContext);
        HBITMAP backBuffer = memoryDeviceContext
                                 ? CreateCompatibleBitmap(
                                       paintDeviceContext, width, height)
                                 : nullptr;
        HGDIOBJ previousBitmap = backBuffer
                                     ? SelectObject(memoryDeviceContext,
                                                    backBuffer)
                                     : nullptr;
        if (memoryDeviceContext && backBuffer && previousBitmap &&
            previousBitmap != HGDI_ERROR)
        {
            DrawProgressCircleFrame(circleWindow, memoryDeviceContext,
                                    clientRect);
            if (!BitBlt(paintDeviceContext, 0, 0, width, height,
                        memoryDeviceContext, 0, 0, SRCCOPY))
            {
                DrawProgressCircleFrame(circleWindow, paintDeviceContext,
                                        clientRect);
            }
            SelectObject(memoryDeviceContext, previousBitmap);
        }
        else
        {
            DrawProgressCircleFrame(circleWindow, paintDeviceContext,
                                    clientRect);
        }

        if (backBuffer)
        {
            DeleteObject(backBuffer);
        }
        if (memoryDeviceContext)
        {
            DeleteDC(memoryDeviceContext);
        }

        EndPaint(circleWindow, &paint);
    }

    struct InfoPanelSnapshot
    {
        int percent = 0;
        unsigned long long completedBytes = 0;
        unsigned long long totalBytes = 0;
        unsigned long long completedItems = 0;
        unsigned long long totalItems = 0;
        bool bytesValid = false;
        bool itemsValid = false;
        double nativeRate = 0.0;
        bool nativeRateValid = false;
        bool expanded = false;
        bool displayModeKnown = false;
        std::vector<double> rateHistory;
    };

    bool GetInfoPanelSnapshot(HWND infoWindow,
                              InfoPanelSnapshot *snapshot)
    {
        OperationTileElement *tile = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [infoWindow](CircleState const &state)
                { return state.infoWindow == infoWindow; });
            if (it == g_circles.end())
            {
                return false;
            }
            tile = it->tile;
            snapshot->percent = std::clamp(it->progressPercent, 0, 100);
        }

        std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
        auto it = std::find_if(
            g_transferSummaries.begin(), g_transferSummaries.end(),
            [tile](TransferSummaryState const &state)
            { return state.tile == tile; });
        if (it == g_transferSummaries.end())
        {
            return false;
        }

        snapshot->completedBytes = it->completedBytes;
        snapshot->totalBytes = it->totalBytes;
        snapshot->completedItems = it->completedItems;
        snapshot->totalItems = it->totalItems;
        snapshot->bytesValid = it->bytesValid;
        snapshot->itemsValid = it->itemsValid;
        snapshot->nativeRate = it->nativeDisplayRate;
        snapshot->nativeRateValid = it->nativeDisplayRateValid;
        snapshot->expanded = it->expanded;
        snapshot->displayModeKnown = it->displayModeKnown;
        snapshot->rateHistory = it->nativeRateHistory;
        return true;
    }

    void FormatRemainingTime(InfoPanelSnapshot const &snapshot,
                             wchar_t *buffer,
                             size_t bufferLength)
    {
        if (!buffer || !bufferLength)
        {
            return;
        }

        if (!snapshot.bytesValid || !snapshot.nativeRateValid ||
            snapshot.nativeRate < 1.0 ||
            snapshot.totalBytes < snapshot.completedBytes)
        {
            lstrcpynW(buffer, L"Calculating...",
                      static_cast<int>(bufferLength));
            return;
        }

        double secondsDouble =
            static_cast<double>(snapshot.totalBytes -
                                snapshot.completedBytes) /
            snapshot.nativeRate;
        unsigned long long seconds =
            static_cast<unsigned long long>(
                std::max(0.0, std::ceil(secondsDouble)));

        if (seconds < 60)
        {
            std::swprintf(buffer, bufferLength, L"%llus", seconds);
        }
        else if (seconds < 3600)
        {
            unsigned long long minutes = seconds / 60;
            unsigned long long remainder = seconds % 60;
            if (remainder)
            {
                std::swprintf(buffer, bufferLength, L"%llum %llus",
                              minutes, remainder);
            }
            else
            {
                std::swprintf(buffer, bufferLength, L"%llum", minutes);
            }
        }
        else
        {
            unsigned long long hours = seconds / 3600;
            unsigned long long minutes = (seconds % 3600) / 60;
            std::swprintf(buffer, bufferLength, L"%lluh %llum",
                          hours, minutes);
        }
    }

    void DrawInfoPanelFrame(HWND infoWindow,
                            HDC deviceContext,
                            RECT const &clientRect)
    {
        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.top;
        if (width <= 0 || height <= 0)
        {
            return;
        }

        UINT dpi = GetDpiForWindow(infoWindow);
        if (!dpi)
        {
            dpi = USER_DEFAULT_SCREEN_DPI;
        }

        InfoPanelSnapshot snapshot{};
        GetInfoPanelSnapshot(infoWindow, &snapshot);

        Gdiplus::Graphics graphics(deviceContext);
        graphics.SetSmoothingMode(Gdiplus::SmoothingModeAntiAlias);
        graphics.SetPixelOffsetMode(Gdiplus::PixelOffsetModeHighQuality);
        graphics.SetTextRenderingHint(
            Gdiplus::TextRenderingHintClearTypeGridFit);

        Gdiplus::SolidBrush backgroundBrush(Gdiplus::Color(
            255, GetRValue(kBackgroundColor), GetGValue(kBackgroundColor),
            GetBValue(kBackgroundColor)));
        graphics.FillRectangle(&backgroundBrush, 0, 0, width, height);

        Gdiplus::Font detailFont(
            L"Segoe UI Variable",
            static_cast<Gdiplus::REAL>(ScaleForDpi(12, dpi)),
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font detailFallback(
            L"Segoe UI",
            static_cast<Gdiplus::REAL>(ScaleForDpi(12, dpi)),
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font *selectedFont =
            detailFont.GetLastStatus() == Gdiplus::Ok
                ? &detailFont
                : &detailFallback;

        Gdiplus::SolidBrush primaryBrush(Gdiplus::Color(
            255, GetRValue(kPrimaryTextColor), GetGValue(kPrimaryTextColor),
            GetBValue(kPrimaryTextColor)));
        Gdiplus::SolidBrush secondaryBrush(Gdiplus::Color(
            255, GetRValue(kSecondaryTextColor),
            GetGValue(kSecondaryTextColor), GetBValue(kSecondaryTextColor)));

        wchar_t rateSize[64]{};
        wchar_t timeText[64]{};
        wchar_t speedTimeText[192]{};
        if (snapshot.nativeRateValid)
        {
            unsigned long long roundedRate =
                static_cast<unsigned long long>(
                    std::max(0.0, snapshot.nativeRate) + 0.5);
            StrFormatByteSizeW(static_cast<LONGLONG>(roundedRate),
                               rateSize, ARRAYSIZE(rateSize));
        }
        else
        {
            lstrcpynW(rateSize, L"\x2014", ARRAYSIZE(rateSize));
        }
        FormatRemainingTime(snapshot, timeText, ARRAYSIZE(timeText));
        std::swprintf(speedTimeText, ARRAYSIZE(speedTimeText),
                      L"Speed: %s/s  \x2022  Time remaining: %s",
                      rateSize, timeText);

        Gdiplus::RectF speedBounds(
            0.0f, 0.0f, static_cast<Gdiplus::REAL>(width),
            static_cast<Gdiplus::REAL>(
                ScaleForDpi(kInfoPanelTextRowHeight, dpi)));
        graphics.DrawString(speedTimeText, -1, selectedFont, speedBounds,
                            nullptr, &primaryBrush);

        wchar_t remainingSize[64]{};
        wchar_t itemsText[160]{};
        unsigned long long remainingItems = 0;
        unsigned long long remainingBytes = 0;
        if (snapshot.itemsValid &&
            snapshot.totalItems >= snapshot.completedItems)
        {
            remainingItems = snapshot.totalItems - snapshot.completedItems;
        }
        if (snapshot.bytesValid &&
            snapshot.totalBytes >= snapshot.completedBytes)
        {
            remainingBytes = snapshot.totalBytes - snapshot.completedBytes;
            StrFormatByteSizeW(static_cast<LONGLONG>(remainingBytes),
                               remainingSize, ARRAYSIZE(remainingSize));
        }

        if (snapshot.itemsValid && snapshot.bytesValid)
        {
            std::swprintf(itemsText, ARRAYSIZE(itemsText),
                          L"Items remaining: %llu (%s)", remainingItems,
                          remainingSize);
        }
        else if (snapshot.itemsValid)
        {
            std::swprintf(itemsText, ARRAYSIZE(itemsText),
                          L"Items remaining: %llu", remainingItems);
        }
        else
        {
            lstrcpynW(itemsText, L"Items remaining: Calculating...",
                      ARRAYSIZE(itemsText));
        }

        Gdiplus::RectF itemsBounds(
            0.0f,
            static_cast<Gdiplus::REAL>(
                ScaleForDpi(kInfoPanelTextRowHeight + 2, dpi)),
            static_cast<Gdiplus::REAL>(width),
            static_cast<Gdiplus::REAL>(
                ScaleForDpi(kInfoPanelTextRowHeight, dpi)));
        graphics.DrawString(itemsText, -1, selectedFont, itemsBounds,
                            nullptr, &secondaryBrush);

        Gdiplus::REAL progressTop = static_cast<Gdiplus::REAL>(
            ScaleForDpi(kInfoPanelProgressTop, dpi));
        Gdiplus::REAL progressHeight = static_cast<Gdiplus::REAL>(
            ScaleForDpi(kInfoPanelProgressHeight, dpi));
        Gdiplus::REAL progressWidth =
            static_cast<Gdiplus::REAL>(std::max(width, 1));
        Gdiplus::SolidBrush progressTrack(Gdiplus::Color(
            255, GetRValue(kInactiveRingColor),
            GetGValue(kInactiveRingColor), GetBValue(kInactiveRingColor)));
        Gdiplus::SolidBrush progressFill(Gdiplus::Color(
            255, GetRValue(kAccentRingColor),
            GetGValue(kAccentRingColor), GetBValue(kAccentRingColor)));
        graphics.FillRectangle(&progressTrack, 0.0f, progressTop,
                               progressWidth, progressHeight);
        Gdiplus::REAL completedWidth =
            progressWidth *
            static_cast<Gdiplus::REAL>(
                std::clamp(snapshot.percent, 0, 100)) /
            100.0f;
        if (completedWidth > 0.0f)
        {
            graphics.FillRectangle(&progressFill, 0.0f, progressTop,
                                   completedWidth, progressHeight);
        }

        if (!snapshot.expanded)
        {
            return;
        }

        int chartTop = ScaleForDpi(kInfoPanelChartTop, dpi);
        int chartHeight = ScaleForDpi(kInfoPanelChartHeight, dpi);
        int chartWidth = width;
        if (chartTop + chartHeight > height || chartWidth <= 0)
        {
            return;
        }

        Gdiplus::SolidBrush chartBackground(Gdiplus::Color(
            255, GetRValue(kGraphSurfaceColor),
            GetGValue(kGraphSurfaceColor), GetBValue(kGraphSurfaceColor)));
        graphics.FillRectangle(&chartBackground, 0, chartTop,
                               chartWidth, chartHeight);

        Gdiplus::Pen gridPen(Gdiplus::Color(70, 120, 128, 138), 1.0f);
        for (int row = 1; row < 4; ++row)
        {
            Gdiplus::REAL y = static_cast<Gdiplus::REAL>(
                chartTop + (chartHeight * row) / 4);
            graphics.DrawLine(&gridPen, 0.0f, y,
                              static_cast<Gdiplus::REAL>(chartWidth), y);
        }
        for (int column = 1; column < 6; ++column)
        {
            Gdiplus::REAL x = static_cast<Gdiplus::REAL>(
                (chartWidth * column) / 6);
            graphics.DrawLine(&gridPen, x,
                              static_cast<Gdiplus::REAL>(chartTop), x,
                              static_cast<Gdiplus::REAL>(
                                  chartTop + chartHeight));
        }

        if (snapshot.rateHistory.empty())
        {
            return;
        }

        double maximumRate = 1.0;
        for (double rate : snapshot.rateHistory)
        {
            if (std::isfinite(rate))
            {
                maximumRate = std::max(maximumRate, rate);
            }
        }

        size_t sampleCount = snapshot.rateHistory.size();
        std::vector<Gdiplus::PointF> linePoints;
        linePoints.reserve(sampleCount);
        for (size_t index = 0; index < sampleCount; ++index)
        {
            double rate = std::isfinite(snapshot.rateHistory[index])
                              ? std::max(0.0, snapshot.rateHistory[index])
                              : 0.0;
            Gdiplus::REAL x = sampleCount > 1
                                  ? static_cast<Gdiplus::REAL>(
                                        (static_cast<double>(index) /
                                         static_cast<double>(
                                             sampleCount - 1)) *
                                        (chartWidth - 1))
                                  : 0.0f;
            Gdiplus::REAL normalized =
                static_cast<Gdiplus::REAL>(rate / maximumRate);
            Gdiplus::REAL y =
                static_cast<Gdiplus::REAL>(chartTop + chartHeight - 1) -
                normalized *
                    static_cast<Gdiplus::REAL>(chartHeight - 2);
            linePoints.emplace_back(x, y);
        }

        if (linePoints.size() >= 2)
        {
            std::vector<Gdiplus::PointF> fillPoints;
            fillPoints.reserve(linePoints.size() + 2);
            fillPoints.emplace_back(
                linePoints.front().X,
                static_cast<Gdiplus::REAL>(chartTop + chartHeight));
            fillPoints.insert(fillPoints.end(), linePoints.begin(),
                              linePoints.end());
            fillPoints.emplace_back(
                linePoints.back().X,
                static_cast<Gdiplus::REAL>(chartTop + chartHeight));

            Gdiplus::SolidBrush chartFill(Gdiplus::Color(
                150, GetRValue(kAccentRingColor),
                GetGValue(kAccentRingColor), GetBValue(kAccentRingColor)));
            Gdiplus::Pen chartLine(Gdiplus::Color(
                255, GetRValue(kAccentRingColor),
                GetGValue(kAccentRingColor), GetBValue(kAccentRingColor)),
                1.5f);
            graphics.FillPolygon(&chartFill, fillPoints.data(),
                                 static_cast<INT>(fillPoints.size()));
            graphics.DrawLines(&chartLine, linePoints.data(),
                               static_cast<INT>(linePoints.size()));
        }
    }

    void PaintInfoPanel(HWND infoWindow)
    {
        PAINTSTRUCT paint{};
        HDC paintDeviceContext = BeginPaint(infoWindow, &paint);
        if (!paintDeviceContext)
        {
            return;
        }

        RECT clientRect{};
        if (!GetClientRect(infoWindow, &clientRect))
        {
            EndPaint(infoWindow, &paint);
            return;
        }

        int width = clientRect.right - clientRect.left;
        int height = clientRect.bottom - clientRect.top;
        HDC memoryDeviceContext =
            width > 0 && height > 0
                ? CreateCompatibleDC(paintDeviceContext)
                : nullptr;
        HBITMAP backBuffer =
            memoryDeviceContext
                ? CreateCompatibleBitmap(paintDeviceContext, width, height)
                : nullptr;
        HGDIOBJ previousBitmap =
            backBuffer
                ? SelectObject(memoryDeviceContext, backBuffer)
                : nullptr;

        if (memoryDeviceContext && backBuffer && previousBitmap &&
            previousBitmap != HGDI_ERROR)
        {
            DrawInfoPanelFrame(infoWindow, memoryDeviceContext, clientRect);
            BitBlt(paintDeviceContext, 0, 0, width, height,
                   memoryDeviceContext, 0, 0, SRCCOPY);
            SelectObject(memoryDeviceContext, previousBitmap);
        }
        else
        {
            DrawInfoPanelFrame(infoWindow, paintDeviceContext, clientRect);
        }

        if (backBuffer)
        {
            DeleteObject(backBuffer);
        }
        if (memoryDeviceContext)
        {
            DeleteDC(memoryDeviceContext);
        }
        EndPaint(infoWindow, &paint);
    }

    LRESULT CALLBACK InfoPanelWindowProc(HWND window,
                                         UINT message,
                                         WPARAM wParam,
                                         LPARAM lParam)
    {
        switch (message)
        {
        case WM_PAINT:
            PaintInfoPanel(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }


    void ForgetCircleWindow(HWND circleWindow)
    {
        HWND progressWindow = nullptr;
        HWND infoWindow = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [circleWindow](CircleState const &state)
                {
                    return state.circleWindow == circleWindow;
                });
            if (it == g_circles.end())
            {
                return;
            }
            progressWindow = it->progressWindow;
            infoWindow = it->infoWindow;
            g_circles.erase(it);
        }

        if (progressWindow && IsWindow(progressWindow))
        {
            RemoveWindowSubclass(progressWindow,
                                 NativeProgressWindowSubclassProc,
                                 kProgressWindowSubclassId);
        }
        if (infoWindow && IsWindow(infoWindow))
        {
            DestroyWindow(infoWindow);
        }
    }

    LRESULT CALLBACK ProgressCircleWindowProc(HWND window,
                                              UINT message,
                                              WPARAM wParam,
                                              LPARAM lParam)
    {
        switch (message)
        {
        case WM_PAINT:
            PaintProgressCircle(window);
            return 0;
        case WM_ERASEBKGND:
            return 1;
        case WM_MOUSEACTIVATE:
            return MA_NOACTIVATE;
        case WM_NCHITTEST:
            return HTTRANSPARENT;
        case WM_NCDESTROY:
            ForgetCircleWindow(window);
            break;
        }

        return DefWindowProcW(window, message, wParam, lParam);
    }

    void RemoveHostSubclassRecord(HWND hostWindow)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        auto it = std::find(g_subclassedHosts.begin(),
                            g_subclassedHosts.end(), hostWindow);
        if (it != g_subclassedHosts.end())
        {
            g_subclassedHosts.erase(it);
        }
        auto requestIt = std::find_if(
            g_hostPositionRequests.begin(), g_hostPositionRequests.end(),
            [hostWindow](HostPositionRequest const &request)
            {
                return request.hostWindow == hostWindow;
            });
        if (requestIt != g_hostPositionRequests.end())
        {
            g_hostPositionRequests.erase(requestIt);
        }
    }

    PCWSTR TakeProgressCirclePositionReason(HWND hostWindow)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        auto it = std::find_if(
            g_hostPositionRequests.begin(), g_hostPositionRequests.end(),
            [hostWindow](HostPositionRequest const &request)
            {
                return request.hostWindow == hostWindow;
            });
        if (it == g_hostPositionRequests.end())
        {
            return nullptr;
        }
        PCWSTR reason = it->reason;
        g_hostPositionRequests.erase(it);
        return reason;
    }

    void DestroyProgressCirclesForHost(HWND hostWindow)
    {
        std::vector<CircleState> removed;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = g_circles.begin();
            while (it != g_circles.end())
            {
                if (it->hostWindow == hostWindow)
                {
                    removed.push_back(*it);
                    it = g_circles.erase(it);
                }
                else
                {
                    ++it;
                }
            }
        }

        for (auto const &state : removed)
        {
            if (state.progressWindow && IsWindow(state.progressWindow))
            {
                RemoveWindowSubclass(state.progressWindow,
                                     NativeProgressWindowSubclassProc,
                                     kProgressWindowSubclassId);
            }
            if (state.infoWindow && IsWindow(state.infoWindow))
            {
                DestroyWindow(state.infoWindow);
            }
            if (state.circleWindow && IsWindow(state.circleWindow))
            {
                DestroyWindow(state.circleWindow);
            }
        }
    }

    void CancelDeferredDisplaySnapshotsForHost(HWND hostWindow)
    {
        std::lock_guard<std::mutex> lock(g_displayDiagnosticMutex);
        g_deferredDisplaySnapshots.erase(
            std::remove_if(
                g_deferredDisplaySnapshots.begin(),
                g_deferredDisplaySnapshots.end(),
                [hostWindow](DeferredDisplaySnapshot const &snapshot)
                { return snapshot.hostWindow == hostWindow; }),
            g_deferredDisplaySnapshots.end());
    }

    void CancelDeferredDisplaySnapshotsForTile(OperationTileElement *tile)
    {
        std::lock_guard<std::mutex> lock(g_displayDiagnosticMutex);
        g_deferredDisplaySnapshots.erase(
            std::remove_if(
                g_deferredDisplaySnapshots.begin(),
                g_deferredDisplaySnapshots.end(),
                [tile](DeferredDisplaySnapshot const &snapshot)
                { return snapshot.tile == tile; }),
            g_deferredDisplaySnapshots.end());
    }

    int GetSingleCirclePercentForHost(HWND hostWindow)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        int percent = -1;
        int matches = 0;
        for (auto const &state : g_circles)
        {
            if (state.hostWindow != hostWindow)
            {
                continue;
            }
            percent = std::clamp(state.progressPercent, 0, 100);
            ++matches;
            if (matches > 1)
            {
                return -1;
            }
        }
        return matches == 1 ? percent : -1;
    }

    bool LooksLikeNativeProgressCaption(PCWSTR text)
    {
        return text && *text && wcsstr(text, L"%") != nullptr &&
               wcsstr(text, L" / ") == nullptr;
    }

    bool LooksLikeTransferSummaryCaption(PCWSTR text)
    {
        if (!text || !*text)
        {
            return false;
        }

        // The custom body summary is a transferred/total pair. Explorer can
        // mirror either the full "x / y (p%)" form or a transient "x / y"
        // form into the top-level caption. Suppress both so the title remains
        // Explorer's native progress caption instead of flashing byte counts.
        return wcsstr(text, L" / ") != nullptr;
    }

    void SyncHostCaptionFromCircle(OperationTileElement *tile, int percent)
    {
        HWND hostWindow = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [tile](CircleState const &state)
                { return state.tile == tile; });
            if (it != g_circles.end())
            {
                hostWindow = it->hostWindow;
            }
        }

        if (!hostWindow || !IsWindow(hostWindow) ||
            GetSingleCirclePercentForHost(hostWindow) < 0)
        {
            return;
        }

        wchar_t currentCaption[160]{};
        GetWindowTextW(hostWindow, currentCaption, ARRAYSIZE(currentCaption));

        wchar_t synchronizedCaption[80]{};
        bool paused = wcsstr(currentCaption, L"Paused") != nullptr;
        wsprintfW(synchronizedCaption,
                  paused ? L"Paused - %d%% complete" : L"%d%% complete",
                  std::clamp(percent, 0, 100));
        SetWindowTextW(hostWindow, synchronizedCaption);
    }

    LRESULT CALLBACK OperationStatusWindowSubclassProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR)
    {
        bool logLayoutMessage =
            message == WM_SIZE || message == WM_WINDOWPOSCHANGING ||
            message == WM_WINDOWPOSCHANGED;
        if (logLayoutMessage)
        {
            LogTargetWindowMessage(L"OperationStatusWindow", L"BEFORE_DEF",
                                   window, message, wParam, lParam);
        }

        if (message == WM_SETTEXT)
        {
            PCWSTR caption = reinterpret_cast<PCWSTR>(lParam);
            if (LooksLikeTransferSummaryCaption(caption))
            {
                // Explorer can publish a transient byte-summary caption before
                // the progress caption. Replace it with the same normalized
                // percentage used by the circle instead of merely suppressing
                // the update and leaving an older byte caption frozen.
                int percent = GetSingleCirclePercentForHost(window);
                if (percent >= 0)
                {
                    wchar_t synchronizedCaption[80]{};
                    bool paused = wcsstr(caption, L"Paused") != nullptr;
                    wsprintfW(synchronizedCaption,
                              paused ? L"Paused - %d%% complete"
                                     : L"%d%% complete",
                              percent);
                    return DefSubclassProc(
                        window, message, wParam,
                        reinterpret_cast<LPARAM>(synchronizedCaption));
                }
                return TRUE;
            }

            // With one operation tile, keep the title percentage synchronized
            // to the same normalized progress used by the circle/body. Explorer
            // can otherwise publish a visibly different percentage. For
            // multi-tile hosts, leave Explorer's aggregate caption untouched.
            int percent = GetSingleCirclePercentForHost(window);
            if (percent >= 0 && LooksLikeNativeProgressCaption(caption))
            {
                wchar_t synchronizedCaption[80]{};
                bool paused = wcsstr(caption, L"Paused") != nullptr;
                wsprintfW(synchronizedCaption,
                          paused ? L"Paused - %d%% complete"
                                 : L"%d%% complete",
                          percent);
                return DefSubclassProc(window, message, wParam,
                                       reinterpret_cast<LPARAM>(synchronizedCaption));
            }
        }

        if (message == g_removeHostSubclassMessage)
        {
            CancelDeferredDisplaySnapshotsForHost(window);
            RemoveHostSubclassRecord(window);
            RemoveWindowSubclass(window, OperationStatusWindowSubclassProc,
                                 subclassId);
            return 0;
        }

        if (message == g_positionCirclesMessage)
        {
            PCWSTR reason = TakeProgressCirclePositionReason(window);
            if (reason)
            {
                PositionProgressCirclesForHost(window, reason);
                ApplyNativeDisplayRatesForHost(window);
            }
            return 0;
        }

        if (message == g_logDisplayStateMessage)
        {
            HandleDeferredDisplaySnapshot(
                window, static_cast<unsigned long long>(wParam));
            return 0;
        }

        if (message == WM_NCDESTROY)
        {
            CancelDeferredDisplaySnapshotsForHost(window);
            DestroyProgressCirclesForHost(window);
            RemoveHostSubclassRecord(window);
            RemoveWindowSubclass(window, OperationStatusWindowSubclassProc,
                                 subclassId);
        }

        int requestedCy = 0;
        if (message == WM_WINDOWPOSCHANGING && lParam)
        {
            requestedCy =
                reinterpret_cast<WINDOWPOS *>(lParam)->cy;
        }

        LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (logLayoutMessage)
        {
            LogTargetWindowMessage(L"OperationStatusWindow", L"AFTER_DEF",
                                   window, message, wParam, lParam);
        }

        if (message == WM_WINDOWPOSCHANGING && lParam)
        {
            auto *windowPosition = reinterpret_cast<WINDOWPOS *>(lParam);
            int nativeCy = windowPosition->cy;
            int customCy = 0;
            static thread_local bool applyingHeightOverride;
            if (!(windowPosition->flags & SWP_NOSIZE) &&
                !applyingHeightOverride)
            {
                applyingHeightOverride = true;
                bool verified = GetVerifiedCustomHostWindowHeight(
                    window, nativeCy, &customCy);
                applyingHeightOverride = false;
                if (verified)
                {
                    windowPosition->cy = customCy;
                    Wh_Log(L"CUSTOM_HEIGHT host=%p requestedCy=%d "
                           L"nativeCy=%d finalCy=%d result=overridden",
                           reinterpret_cast<void *>(window), requestedCy,
                           nativeCy, windowPosition->cy);
                }
            }
        }
        if (message == WM_SIZE)
        {
            ScheduleProgressCirclePosition(window, L"host-size");
        }
        else if (message == WM_WINDOWPOSCHANGED)
        {
            ScheduleProgressCirclePosition(window, L"host-position");
        }
        else if (message == WM_DPICHANGED)
        {
            ScheduleProgressCirclePosition(window, L"dpi-change");
        }
        return result;
    }

    void DetachCircleFromProgressWindow(HWND progressWindow)
    {
        HWND hostWindow = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [progressWindow](CircleState const &state)
                { return state.progressWindow == progressWindow; });
            if (it == g_circles.end())
            {
                return;
            }

            hostWindow = it->hostWindow;
            it->progressWindow = nullptr;
            it->positionValid = false;
        }

        // More/Fewer Details can replace the native progress HWND while the
        // OperationTileElement itself stays alive. The circle belongs to the
        // tile, not to that temporary HWND. Keep the circle VISIBLE during
        // the handoff; hiding it here caused it to disappear permanently when
        // Explorer created the replacement progress HWND after our deferred
        // reposition message had already run.
        //
        // The tile-relative bounds remain valid across the transition, so the
        // existing circle can stay on screen and will rebind to the new native
        // progress HWND on the next layout/progress notification.
        if (hostWindow && IsWindow(hostWindow))
        {
            ScheduleProgressCirclePosition(hostWindow,
                                           L"progress-hwnd-recreated");
        }
    }

    LRESULT CALLBACK NativeProgressWindowSubclassProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR)
    {
        bool logTransitionMessage =
            message == WM_SHOWWINDOW || message == WM_WINDOWPOSCHANGING ||
            message == WM_WINDOWPOSCHANGED || message == WM_NCDESTROY;
        if (logTransitionMessage)
        {
            LogTargetWindowMessage(L"NativeProgressHWND", L"BEFORE_DEF",
                                   window, message, wParam, lParam);
        }

        LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (logTransitionMessage)
        {
            LogTargetWindowMessage(L"NativeProgressHWND", L"AFTER_DEF",
                                   window, message, wParam, lParam);
        }
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, NativeProgressWindowSubclassProc,
                                 subclassId);
            DetachCircleFromProgressWindow(window);
        }
        else if (message == WM_WINDOWPOSCHANGED)
        {
            HWND hostWindow = GetAncestor(window, GA_ROOT);
            if (hostWindow)
            {
                ScheduleProgressCirclePosition(hostWindow, L"tile-layout");
            }
        }
        return result;
    }

    bool GetCirclePlacement(CircleState const &state,
                            int *x,
                            int *y,
                            int *width,
                            int *height)
    {
        UINT dpi = GetDpiForWindow(state.hostWindow);
        if (!dpi)
        {
            return false;
        }

        *width = ScaleForDpi(kCircleColumnWidth, dpi);
        *height = ScaleForDpi(kCircleWindowHeight, dpi);
        auto *tileRoot =
            reinterpret_cast<DirectUI::Element *>(state.tile);

        // Use an explicit host-relative target position. Earlier attempts
        // derived Y from DirectUI bounds, but those bounds did not produce a
        // visible movement in Explorer. Keep a real tile-bounds fallback for
        // unusual hosts, but make the normal presentation deterministic.
        *x = 0;
        *y = ScaleForDpi(kCircleHostY, dpi);
        if (IsWindow(state.hostWindow))
        {
            return true;
        }

        RECT tileBounds{};
        HRESULT boundsResult = Element_GetRootRelativeBounds_Original(
            tileRoot, &tileBounds);
        if (SUCCEEDED(boundsResult) && tileBounds.bottom > tileBounds.top)
        {
            *x = std::max(static_cast<int>(tileBounds.left), 0);
            *y = std::max(static_cast<int>(tileBounds.top) -
                              ScaleForDpi(28, dpi),
                          ScaleForDpi(34, dpi));
            return true;
        }

        if (state.progressWindow && IsWindow(state.progressWindow))
        {
            RECT progressRect;
            if (GetWindowRect(state.progressWindow, &progressRect))
            {
                MapWindowPoints(HWND_DESKTOP, state.hostWindow,
                                reinterpret_cast<POINT *>(&progressRect), 2);
                *x = 0;
                *y = std::max(static_cast<int>(progressRect.top) -
                                  ScaleForDpi(100, dpi),
                              0);
                return true;
            }
        }

        return false;
    }


    HWND GetInfoPanelWindowForTile(OperationTileElement *tile)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        auto it = std::find_if(
            g_circles.begin(), g_circles.end(),
            [tile](CircleState const &state)
            { return state.tile == tile; });
        return it != g_circles.end() ? it->infoWindow : nullptr;
    }

    void InvalidateInfoPanelForTile(OperationTileElement *tile)
    {
        HWND infoWindow = GetInfoPanelWindowForTile(tile);
        if (infoWindow && IsWindow(infoWindow))
        {
            InvalidateRect(infoWindow, nullptr, FALSE);
        }
    }

    void PositionInfoPanel(OperationTileElement *tile)
    {
        HWND infoWindow = nullptr;
        HWND hostWindow = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [tile](CircleState const &state)
                { return state.tile == tile; });
            if (it == g_circles.end())
            {
                return;
            }
            infoWindow = it->infoWindow;
            hostWindow = it->hostWindow;
        }

        if (!infoWindow || !IsWindow(infoWindow) ||
            !hostWindow || !IsWindow(hostWindow))
        {
            return;
        }

        bool expanded = false;
        bool modeKnown = false;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [tile](TransferSummaryState const &state)
                { return state.tile == tile; });
            if (it != g_transferSummaries.end())
            {
                expanded = it->expanded;
                modeKnown = it->displayModeKnown;
            }
        }

        UINT dpi = GetDpiForWindow(hostWindow);
        RECT clientRect{};
        if (!dpi || !GetClientRect(hostWindow, &clientRect))
        {
            return;
        }

        int x = ScaleForDpi(kReservedLeftWidth, dpi);
        int y = ScaleForDpi(kInfoPanelTop, dpi);
        int rightPadding = ScaleForDpi(kContentRightPadding, dpi);
        int width = std::max(static_cast<int>(clientRect.right) - x - rightPadding, 1);
        int logicalHeight =
            modeKnown && expanded ? kInfoPanelExpandedHeight
                                  : kInfoPanelCommonHeight;
        int height = ScaleForDpi(logicalHeight, dpi);

        if (SetWindowPos(infoWindow, HWND_TOP, x, y, width, height,
                         SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                             SWP_SHOWWINDOW))
        {
            InvalidateRect(infoWindow, nullptr, FALSE);
        }
    }

    void PositionProgressCircle(OperationTileElement *tile, PCWSTR reason)
    {
        HWND circleWindow = nullptr;
        HWND hostWindow = nullptr;
        unsigned long long eventId = 0;
        int x = 0;
        int y = 0;
        int width = 0;
        int height = 0;
        bool placementAvailable = false;
        bool positionChanged = false;
        bool initialPlacement = false;

        // A compact/expanded transition can swap the progress HWND without
        // changing the tile. Re-resolve it here, including while paused when
        // no progress-position notification may arrive.
        HWND latestProgressWindow =
            OperationTileElement_GetProgressHWND_Original(tile);
        if (latestProgressWindow && !IsWindow(latestProgressWindow))
        {
            latestProgressWindow = nullptr;
        }

        // The custom info panel now renders the completion bar. Keep the
        // native HWND only as the authoritative progress data source and hide
        // its visual presentation whenever Explorer recreates/rebinds it.
        if (latestProgressWindow && IsWindowVisible(latestProgressWindow))
        {
            ShowWindow(latestProgressWindow, SW_HIDE);
        }

        HWND progressWindowToSubclass = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto bindingIt = std::find_if(
                g_circles.begin(), g_circles.end(),
                [tile](CircleState const &state)
                { return state.tile == tile; });
            if (bindingIt != g_circles.end() && latestProgressWindow &&
                bindingIt->progressWindow != latestProgressWindow)
            {
                bindingIt->progressWindow = latestProgressWindow;
                bindingIt->positionValid = false;
                progressWindowToSubclass = latestProgressWindow;
            }
        }

        if (progressWindowToSubclass)
        {
            SetWindowSubclass(progressWindowToSubclass,
                              NativeProgressWindowSubclassProc,
                              kProgressWindowSubclassId, 0);
        }

        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [tile](CircleState const &state)
                { return state.tile == tile; });
            if (it == g_circles.end())
            {
                return;
            }
            circleWindow = it->circleWindow;
            hostWindow = it->hostWindow;
            eventId = it->eventId;
            placementAvailable =
                GetCirclePlacement(*it, &x, &y, &width, &height);
            initialPlacement = !it->positionValid;
            positionChanged = initialPlacement || it->positionX != x ||
                              it->positionY != y ||
                              it->positionWidth != width ||
                              it->positionHeight != height;
        }

        bool visibilityChanged =
            circleWindow && IsWindow(circleWindow) &&
            !IsWindowVisible(circleWindow);
        if (!placementAvailable || !circleWindow || !IsWindow(circleWindow))
        {
            return;
        }

        // More/Fewer Details rearranges native child HWNDs and can place the
        // OperationTileHost back above our circle in sibling Z-order without
        // changing the circle's coordinates or WS_VISIBLE state. The previous
        // early-out therefore left a perfectly live circle hidden behind the
        // native host. Every deferred layout notification is a safe point to
        // reassert HWND_TOP. If geometry didn't change, do it without moving or
        // resizing the circle.
        UINT positionFlags =
            SWP_NOACTIVATE | SWP_NOOWNERZORDER | SWP_SHOWWINDOW;
        if (!positionChanged)
        {
            positionFlags |= SWP_NOMOVE | SWP_NOSIZE;
        }

        if (!SetWindowPos(circleWindow, HWND_TOP, x, y, width, height,
                          positionFlags))
        {
            Wh_Log(L"eventId=%llu circle SetWindowPos failed tile=%p "
                   L"host=%p error=%lu",
                   eventId, reinterpret_cast<void *>(tile),
                   reinterpret_cast<void *>(hostWindow), GetLastError());
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [tile](CircleState const &state)
                { return state.tile == tile; });
            if (it != g_circles.end() &&
                it->circleWindow == circleWindow)
            {
                it->positionX = x;
                it->positionY = y;
                it->positionWidth = width;
                it->positionHeight = height;
                it->positionValid = true;
            }
        }

        Wh_Log(L"circle position tile=%p x=%d y=%d width=%d height=%d "
               L"reason=%s geometryChanged=%s visibilityChanged=%s",
               reinterpret_cast<void *>(tile), x, y, width, height,
               initialPlacement ? L"initial" : reason,
               positionChanged ? L"yes" : L"no",
               visibilityChanged ? L"yes" : L"no");
        PositionInfoPanel(tile);
    }

    void PositionProgressCirclesForHost(HWND hostWindow, PCWSTR reason)
    {
        std::vector<OperationTileElement *> tiles;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            for (auto const &state : g_circles)
            {
                if (state.hostWindow == hostWindow)
                {
                    tiles.push_back(state.tile);
                }
            }
        }
        for (OperationTileElement *tile : tiles)
        {
            PositionProgressCircle(tile, reason);
        }
    }

    void ScheduleProgressCirclePosition(HWND hostWindow, PCWSTR reason)
    {
        if (!hostWindow || !g_positionCirclesMessage)
        {
            return;
        }

        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_hostPositionRequests.begin(),
                g_hostPositionRequests.end(),
                [hostWindow](HostPositionRequest const &request)
                {
                    return request.hostWindow == hostWindow;
                });
            if (it != g_hostPositionRequests.end())
            {
                it->reason = reason;
                return;
            }
            g_hostPositionRequests.push_back({hostWindow, reason});
        }

        if (!PostMessageW(hostWindow, g_positionCirclesMessage, 0, 0))
        {
            DWORD error = GetLastError();
            TakeProgressCirclePositionReason(hostWindow);
            Wh_Log(L"Circle position scheduling failed host=%p error=%lu",
                   reinterpret_cast<void *>(hostWindow), error);
        }
    }

    bool EnsureHostSubclass(HWND hostWindow, unsigned long long eventId)
    {
        ApplyUnifiedHostChrome(hostWindow);

        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            if (std::find(g_subclassedHosts.begin(), g_subclassedHosts.end(),
                          hostWindow) != g_subclassedHosts.end())
            {
                return true;
            }
        }

        if (!SetWindowSubclass(hostWindow, OperationStatusWindowSubclassProc,
                               kHostWindowSubclassId, 0))
        {
            Wh_Log(L"eventId=%llu circle SetWindowSubclass host failed "
                   L"error=%lu",
                   eventId, GetLastError());
            return false;
        }

        std::lock_guard<std::mutex> lock(g_circleMutex);
        g_subclassedHosts.push_back(hostWindow);
        return true;
    }

    HWND GetOperationStatusWindowForTile(HWND progressWindow,
                                         unsigned long long eventId)
    {
        HWND hostWindow = progressWindow
                              ? GetAncestor(progressWindow, GA_ROOT)
                              : nullptr;
        wchar_t className[64];
        if (hostWindow &&
            GetClassNameW(hostWindow, className, ARRAYSIZE(className)) &&
            lstrcmpW(className, L"OperationStatusWindow") == 0)
        {
            return hostWindow;
        }
        return FindCurrentThreadOperationStatusWindow(eventId);
    }

    bool CopyRegisteredTransferState(COperationStatusTile *owner,
                                     TransferSummaryState *state)
    {
        std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
        auto it = std::find_if(
            g_transferSummaries.begin(), g_transferSummaries.end(),
            [owner](TransferSummaryState const &candidate)
            { return candidate.owner == owner; });
        if (it == g_transferSummaries.end())
        {
            return false;
        }
        *state = *it;
        return true;
    }

    HWND GetRegisteredCircleHost(OperationTileElement *tile)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        auto it = std::find_if(
            g_circles.begin(), g_circles.end(),
            [tile](CircleState const &state)
            { return state.tile == tile; });
        return it != g_circles.end() ? it->hostWindow : nullptr;
    }

    bool GetUniqueRegisteredCircleHost(OperationTileElement *tile,
                                       HWND *hostWindow)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        HWND match = nullptr;
        size_t matches = 0;
        for (CircleState const &state : g_circles)
        {
            if (state.tile == tile)
            {
                match = state.hostWindow;
                ++matches;
            }
        }
        if (matches != 1)
        {
            return false;
        }
        *hostWindow = match;
        return true;
    }

    bool IsLiveDisplayModeState(TransferSummaryState const &state)
    {
        if (!state.tile || !state.operationTileRoot)
        {
            return false;
        }

        HWND hostWindow = nullptr;
        if (!GetUniqueRegisteredCircleHost(state.tile, &hostWindow) ||
            !hostWindow || !IsWindow(hostWindow) ||
            GetWindowThreadProcessId(hostWindow, nullptr) !=
                GetCurrentThreadId())
        {
            return false;
        }

        wchar_t className[64]{};
        return GetClassNameW(hostWindow, className, ARRAYSIZE(className)) &&
               lstrcmpW(className, L"OperationStatusWindow") == 0;
    }

    bool ResolveDisplayModeOwner(COperationStatusTile *requestedOwner,
                                 unsigned long long transitionId,
                                 COperationStatusTile **canonicalOwner,
                                 bool logResult = true)
    {
        std::vector<TransferSummaryState> states;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            states = g_transferSummaries;
        }

        auto resolveCandidate = [&states](COperationStatusTile *candidate)
            -> bool
        {
            TransferSummaryState match{};
            size_t matches = 0;
            for (TransferSummaryState const &state : states)
            {
                if (state.owner == candidate)
                {
                    match = state;
                    ++matches;
                }
            }
            return matches == 1 && IsLiveDisplayModeState(match);
        };

        if (resolveCandidate(requestedOwner))
        {
            *canonicalOwner = requestedOwner;
            if (logResult)
            {
                Wh_Log(L"MODE[%llu] OWNER_RESOLVE requested=%p canonical=%p "
                       L"adjustment=0x0 result=matched",
                       transitionId, reinterpret_cast<void *>(requestedOwner),
                       reinterpret_cast<void *>(requestedOwner));
            }
            return true;
        }

        ULONG_PTR requestedAddress =
            reinterpret_cast<ULONG_PTR>(requestedOwner);
        if (requestedAddress >= kSetTileDisplayModeThisAdjustment)
        {
            auto *adjustedCandidate = reinterpret_cast<COperationStatusTile *>(
                requestedAddress - kSetTileDisplayModeThisAdjustment);
            if (resolveCandidate(adjustedCandidate))
            {
                *canonicalOwner = adjustedCandidate;
                if (logResult)
                {
                    Wh_Log(
                        L"MODE[%llu] OWNER_RESOLVE requested=%p canonical=%p "
                        L"adjustment=0x%llX result=matched",
                        transitionId,
                        reinterpret_cast<void *>(requestedOwner),
                        reinterpret_cast<void *>(adjustedCandidate),
                        static_cast<unsigned long long>(
                            kSetTileDisplayModeThisAdjustment));
                }
                return true;
            }
        }

        *canonicalOwner = nullptr;
        if (logResult)
        {
            Wh_Log(L"MODE[%llu] OWNER_RESOLVE requested=%p canonical=%p "
                   L"adjustment=0x%llX result=failed",
                   transitionId, reinterpret_cast<void *>(requestedOwner),
                   nullptr,
                   static_cast<unsigned long long>(
                       kSetTileDisplayModeThisAdjustment));
        }
        return false;
    }

    void LogRegisteredTransferStatesForDisplayMode(
        COperationStatusTile *requestedOwner,
        unsigned long long transitionId)
    {
        std::vector<TransferSummaryState> states;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            states = g_transferSummaries;
        }

        Wh_Log(L"MODE[%llu] REGISTRY requestedOwner=%p entries=%llu "
               L"thread=%lu",
               transitionId, reinterpret_cast<void *>(requestedOwner),
               static_cast<unsigned long long>(states.size()),
               GetCurrentThreadId());

        ULONG_PTR requestedAddress =
            reinterpret_cast<ULONG_PTR>(requestedOwner);
        for (size_t index = 0; index < states.size(); ++index)
        {
            TransferSummaryState const &state = states[index];
            HWND hostWindow = state.tile
                                  ? GetRegisteredCircleHost(state.tile)
                                  : nullptr;
            ULONG_PTR registeredAddress =
                reinterpret_cast<ULONG_PTR>(state.owner);
            long long ownerDelta =
                static_cast<long long>(requestedAddress) -
                static_cast<long long>(registeredAddress);
            Wh_Log(L"MODE[%llu] REGISTRY[%llu] owner=%p tile=%p "
                   L"operationRoot=%p host=%p hostIsWindow=%s "
                   L"ownerDelta=%lld displayModeKnown=%s expanded=%s",
                   transitionId, static_cast<unsigned long long>(index),
                   reinterpret_cast<void *>(state.owner),
                   reinterpret_cast<void *>(state.tile),
                   reinterpret_cast<void *>(state.operationTileRoot),
                   reinterpret_cast<void *>(hostWindow),
                   hostWindow && IsWindow(hostWindow) ? L"yes" : L"no",
                   ownerDelta,
                   state.displayModeKnown ? L"true" : L"false",
                   state.expanded ? L"true" : L"false");
        }
    }

    void LogDisplayElement(unsigned long long transitionId,
                           PCWSTR phase,
                           PCWSTR name,
                           DirectUI::Element *element,
                           bool logVisibility)
    {
        RECT bounds{};
        HRESULT boundsResult = element
                                   ? Element_GetRootRelativeBounds_Original(
                                         element, &bounds)
                                   : E_POINTER;
        bool visible = element && logVisibility
                           ? Element_GetVisible_Original(element)
                           : false;
        Wh_Log(L"MODE[%llu] %s element=%s exists=%s ptr=%p visibility=%s "
               L"boundsResult=0x%08X bounds=[%ld,%ld,%ld,%ld]",
               transitionId, phase, name, element ? L"yes" : L"no",
               reinterpret_cast<void *>(element),
               logVisibility ? (element ? (visible ? L"visible" : L"hidden")
                                        : L"unavailable")
                             : L"not-requested",
               static_cast<unsigned int>(boundsResult), bounds.left,
               bounds.top, bounds.right, bounds.bottom);
    }

    void LogDisplayRow(unsigned long long transitionId,
                       PCWSTR phase,
                       PCWSTR name,
                       DirectUI::Element *first,
                       DirectUI::Element *second)
    {
        RECT firstBounds{};
        RECT secondBounds{};
        HRESULT firstResult = first
                                  ? Element_GetRootRelativeBounds_Original(
                                        first, &firstBounds)
                                  : E_POINTER;
        HRESULT secondResult = second
                                   ? Element_GetRootRelativeBounds_Original(
                                         second, &secondBounds)
                                   : E_POINTER;
        RECT rowBounds{};
        bool valid = SUCCEEDED(firstResult) && SUCCEEDED(secondResult);
        if (valid)
        {
            rowBounds.left = std::min(firstBounds.left, secondBounds.left);
            rowBounds.top = std::min(firstBounds.top, secondBounds.top);
            rowBounds.right = std::max(firstBounds.right, secondBounds.right);
            rowBounds.bottom = std::max(firstBounds.bottom,
                                        secondBounds.bottom);
        }
        Wh_Log(L"MODE[%llu] %s row=%s first=%p second=%p valid=%s "
               L"bounds=[%ld,%ld,%ld,%ld]",
               transitionId, phase, name,
               reinterpret_cast<void *>(first),
               reinterpret_cast<void *>(second),
               valid ? L"yes" : L"no", rowBounds.left, rowBounds.top,
               rowBounds.right, rowBounds.bottom);
    }

    thread_local bool g_displaySnapshotActive;

    void LogDisplayState(COperationStatusTile *owner,
                         unsigned long long transitionId,
                         PCWSTR phase,
                         bool requestedExpanded,
                         DeferredDisplaySnapshot const *expected = nullptr)
    {
        if (g_displaySnapshotActive)
        {
            Wh_Log(L"MODE[%llu] %s snapshot-skipped reason=reentrant",
                   transitionId, phase);
            return;
        }

        g_displaySnapshotActive = true;
        struct SnapshotGuard
        {
            ~SnapshotGuard()
            {
                g_displaySnapshotActive = false;
            }
        } guard;

        TransferSummaryState state{};
        bool registered = CopyRegisteredTransferState(owner, &state);
        DWORD currentThreadId = GetCurrentThreadId();
        if (expected &&
            (!registered || state.tile != expected->tile ||
             state.operationTileRoot != expected->operationTileRoot ||
             currentThreadId != expected->uiThreadId))
        {
            Wh_Log(L"MODE[%llu] %s requestedExpanded=%s owner=%p thread=%lu "
                   L"snapshot-skipped reason=stale-registration",
                   transitionId, phase,
                   requestedExpanded ? L"true" : L"false",
                   reinterpret_cast<void *>(owner), currentThreadId);
            return;
        }

        OperationTileElement *tile = registered ? state.tile : nullptr;
        DirectUI::Element *tileRoot =
            registered ? state.operationTileRoot : nullptr;
        HWND progressWindow = nullptr;
        if (tile && tileRoot)
        {
            progressWindow =
                OperationTileElement_GetProgressHWND_Original(tile);
        }

        bool progressIsWindow = progressWindow && IsWindow(progressWindow);
        HWND hostWindow = progressIsWindow
                              ? GetAncestor(progressWindow, GA_ROOT)
                              : GetRegisteredCircleHost(tile);
        if (expected && hostWindow != expected->hostWindow)
        {
            Wh_Log(L"MODE[%llu] %s requestedExpanded=%s owner=%p tile=%p "
                   L"host=%p expectedHost=%p thread=%lu snapshot-skipped "
                   L"reason=host-changed",
                   transitionId, phase,
                   requestedExpanded ? L"true" : L"false",
                   reinterpret_cast<void *>(owner),
                   reinterpret_cast<void *>(tile),
                   reinterpret_cast<void *>(hostWindow),
                   reinterpret_cast<void *>(expected->hostWindow),
                   currentThreadId);
            return;
        }

        Wh_Log(L"MODE[%llu] %s requestedExpanded=%s storedKnown=%s "
               L"storedExpanded=%s owner=%p tile=%p host=%p thread=%lu",
               transitionId, phase,
               requestedExpanded ? L"true" : L"false",
               registered && state.displayModeKnown ? L"true" : L"false",
               registered && state.expanded ? L"true" : L"false",
               reinterpret_cast<void *>(owner),
               reinterpret_cast<void *>(tile),
               reinterpret_cast<void *>(hostWindow), currentThreadId);

        if (!tileRoot)
        {
            Wh_Log(L"MODE[%llu] %s elements-unavailable "
                   L"reason=tile-root-not-registered",
                   transitionId, phase);
            return;
        }

        DirectUI::Element *details = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltDetails", false);
        DirectUI::Element *descriptionHeader = state.tileHeaderRoot;
        DirectUI::Element *summary = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltSummary", false);
        DirectUI::Element *speedLabel = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltItemNameLabel", false);
        DirectUI::Element *speedValue = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltItemName", false);
        DirectUI::Element *timeRemainingLabel = FindSkinElement(
            tileRoot, state.tileHeaderRoot,
            L"eltTimeRemainingLabel", false);
        DirectUI::Element *timeRemaining = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltTimeRemaining", false);
        DirectUI::Element *itemsRemainingLabel = FindSkinElement(
            tileRoot, state.tileHeaderRoot,
            L"eltItemsRemainingLabel", false);
        DirectUI::Element *itemsRemaining = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltItemsRemaining", false);
        DirectUI::Element *chartArea = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltChartArea", false);
        DirectUI::Element *rateChart = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltRateChart_New", false);
        DirectUI::Element *progressBarContainer = FindSkinElement(
            tileRoot, state.tileHeaderRoot,
            L"eltProgressBarContainer", false);
        DirectUI::Element *progressBar = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltProgressBar", false);
        DirectUI::Element *regularTile = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltRegularTile", false);
        DirectUI::Element *displayModeButton = FindSkinElement(
            tileRoot, state.tileHeaderRoot, L"eltDisplayModeBtn", true);

        LogDisplayElement(transitionId, phase, L"descriptionHeader",
                          descriptionHeader, true);
        LogDisplayElement(transitionId, phase, L"eltSummary", summary, true);
        LogDisplayElement(transitionId, phase, L"speedElement", speedValue,
                          true);
        LogDisplayRow(transitionId, phase, L"speedRow", speedLabel,
                      speedValue);
        LogDisplayRow(transitionId, phase, L"timeRow", timeRemainingLabel,
                      timeRemaining);
        LogDisplayRow(transitionId, phase, L"itemsRow", itemsRemainingLabel,
                      itemsRemaining);
        LogDisplayElement(transitionId, phase, L"eltDetails", details, true);
        LogDisplayElement(transitionId, phase, L"eltTimeRemaining",
                          timeRemaining, true);
        LogDisplayElement(transitionId, phase, L"eltItemsRemaining",
                          itemsRemaining, true);
        LogDisplayElement(transitionId, phase, L"eltChartArea", chartArea,
                          true);
        LogDisplayElement(transitionId, phase, L"eltRateChart_New", rateChart,
                          true);
        LogDisplayElement(transitionId, phase, L"eltProgressBarContainer",
                          progressBarContainer, true);
        LogDisplayElement(transitionId, phase, L"eltProgressBar", progressBar,
                          true);
        LogDisplayElement(transitionId, phase, L"eltRegularTile", regularTile,
                          false);
        LogDisplayElement(transitionId, phase, L"eltDisplayModeBtn",
                          displayModeButton, true);
        Wh_Log(L"MODE[%llu] %s element=eltDisplayModeBtn "
               L"textState=not-logged-no-verified-getter",
               transitionId, phase);

        RECT progressScreenRect{};
        RECT progressHostRect{};
        bool progressRectValid =
            progressIsWindow &&
            GetWindowRect(progressWindow, &progressScreenRect);
        bool progressHostRectValid = false;
        if (progressRectValid && hostWindow && IsWindow(hostWindow))
        {
            progressHostRect = progressScreenRect;
            SetLastError(ERROR_SUCCESS);
            int mapResult = MapWindowPoints(
                HWND_DESKTOP, hostWindow,
                reinterpret_cast<POINT *>(&progressHostRect), 2);
            progressHostRectValid =
                mapResult != 0 || GetLastError() == ERROR_SUCCESS;
        }
        Wh_Log(L"MODE[%llu] %s nativeProgress hwnd=%p isWindow=%s "
               L"visible=%s screenRectValid=%s screen=[%ld,%ld,%ld,%ld] "
               L"hostRectValid=%s hostRelative=[%ld,%ld,%ld,%ld]",
               transitionId, phase,
               reinterpret_cast<void *>(progressWindow),
               progressIsWindow ? L"yes" : L"no",
               progressIsWindow && IsWindowVisible(progressWindow) ? L"yes"
                                                                   : L"no",
               progressRectValid ? L"yes" : L"no",
               progressScreenRect.left, progressScreenRect.top,
               progressScreenRect.right, progressScreenRect.bottom,
               progressHostRectValid ? L"yes" : L"no",
               progressHostRect.left, progressHostRect.top,
               progressHostRect.right, progressHostRect.bottom);

        RECT clientRect{};
        RECT windowRect{};
        bool hostIsWindow = hostWindow && IsWindow(hostWindow);
        bool clientValid = hostIsWindow && GetClientRect(hostWindow, &clientRect);
        bool windowValid = hostIsWindow && GetWindowRect(hostWindow, &windowRect);
        Wh_Log(L"MODE[%llu] %s OperationStatusWindow hwnd=%p isWindow=%s "
               L"clientValid=%s client=%ldx%ld windowValid=%s window=%ldx%ld",
               transitionId, phase,
               reinterpret_cast<void *>(hostWindow),
               hostIsWindow ? L"yes" : L"no",
               clientValid ? L"yes" : L"no",
               clientRect.right - clientRect.left,
               clientRect.bottom - clientRect.top,
               windowValid ? L"yes" : L"no",
               windowRect.right - windowRect.left,
               windowRect.bottom - windowRect.top);
    }

    void ScheduleDeferredDisplaySnapshot(COperationStatusTile *owner,
                                         unsigned long long transitionId,
                                         bool requestedExpanded)
    {
        if (!g_logDisplayStateMessage)
        {
            return;
        }

        TransferSummaryState state{};
        if (!CopyRegisteredTransferState(owner, &state) || !state.tile ||
            !state.operationTileRoot)
        {
            Wh_Log(L"MODE[%llu] DEFERRED schedule-skipped "
                   L"reason=tile-not-registered",
                   transitionId);
            return;
        }

        HWND progressWindow =
            OperationTileElement_GetProgressHWND_Original(state.tile);
        HWND hostWindow = progressWindow && IsWindow(progressWindow)
                              ? GetAncestor(progressWindow, GA_ROOT)
                              : GetRegisteredCircleHost(state.tile);
        DWORD currentThreadId = GetCurrentThreadId();
        if (!hostWindow || !IsWindow(hostWindow) ||
            GetWindowThreadProcessId(hostWindow, nullptr) != currentThreadId)
        {
            Wh_Log(L"MODE[%llu] DEFERRED schedule-skipped "
                   L"reason=invalid-or-cross-thread-host host=%p thread=%lu",
                   transitionId, reinterpret_cast<void *>(hostWindow),
                   currentThreadId);
            return;
        }

        DeferredDisplaySnapshot snapshot{
            owner, state.tile, state.operationTileRoot, hostWindow,
            currentThreadId, transitionId, requestedExpanded};
        {
            std::lock_guard<std::mutex> lock(g_displayDiagnosticMutex);
            g_deferredDisplaySnapshots.push_back(snapshot);
        }

        if (!PostMessageW(hostWindow, g_logDisplayStateMessage,
                          static_cast<WPARAM>(transitionId), 0))
        {
            DWORD error = GetLastError();
            std::lock_guard<std::mutex> lock(g_displayDiagnosticMutex);
            g_deferredDisplaySnapshots.erase(
                std::remove_if(
                    g_deferredDisplaySnapshots.begin(),
                    g_deferredDisplaySnapshots.end(),
                    [transitionId](DeferredDisplaySnapshot const &candidate)
                    { return candidate.transitionId == transitionId; }),
                g_deferredDisplaySnapshots.end());
            Wh_Log(L"MODE[%llu] DEFERRED PostMessage failed error=%lu",
                   transitionId, error);
        }
    }

    void HandleDeferredDisplaySnapshot(HWND hostWindow,
                                       unsigned long long transitionId)
    {
        DeferredDisplaySnapshot snapshot{};
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(g_displayDiagnosticMutex);
            auto it = std::find_if(
                g_deferredDisplaySnapshots.begin(),
                g_deferredDisplaySnapshots.end(),
                [hostWindow, transitionId](
                    DeferredDisplaySnapshot const &candidate)
                {
                    return candidate.hostWindow == hostWindow &&
                           candidate.transitionId == transitionId;
                });
            if (it != g_deferredDisplaySnapshots.end())
            {
                snapshot = *it;
                g_deferredDisplaySnapshots.erase(it);
                found = true;
            }
        }

        if (!found)
        {
            Wh_Log(L"MODE[%llu] DEFERRED snapshot-skipped "
                   L"reason=request-not-found host=%p",
                   transitionId, reinterpret_cast<void *>(hostWindow));
            return;
        }

        TransferSummaryState currentState{};
        HWND currentHostWindow = nullptr;
        bool currentRegistration =
            CopyRegisteredTransferState(snapshot.owner, &currentState) &&
            currentState.tile == snapshot.tile &&
            currentState.operationTileRoot == snapshot.operationTileRoot &&
            currentState.displayModeKnown &&
            currentState.expanded == snapshot.requestedExpanded &&
            GetCurrentThreadId() == snapshot.uiThreadId &&
            GetUniqueRegisteredCircleHost(currentState.tile,
                                          &currentHostWindow) &&
            currentHostWindow == hostWindow && IsWindow(hostWindow) &&
            GetWindowThreadProcessId(hostWindow, nullptr) ==
                snapshot.uiThreadId;
        if (!currentRegistration)
        {
            Wh_Log(L"MODE[%llu] DEFERRED layout-skipped "
                   L"reason=stale-registration host=%p",
                   transitionId, reinterpret_cast<void *>(hostWindow));
            return;
        }

        bool applyResult =
            ApplyDisplayMode(snapshot.owner, true, transitionId);
        LogDisplayState(snapshot.owner, transitionId, L"DEFERRED",
                        snapshot.requestedExpanded, &snapshot);
        LogFinalDisplayInvariant(snapshot.owner, hostWindow,
                                 snapshot.requestedExpanded, applyResult,
                                 transitionId);
    }

    int GetStoredCirclePercent(OperationTileElement *tile)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        auto it = std::find_if(
            g_circles.begin(), g_circles.end(),
            [tile](CircleState const &state)
            { return state.tile == tile; });
        return it != g_circles.end() ? it->progressPercent : -1;
    }

    void RegisterTransferSummary(COperationStatusTile *owner,
                                 OperationTileElement *tile,
                                 DirectUI::Element *operationTileRoot,
                                 DirectUI::Element *tileHeaderRoot)
    {
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [owner](TransferSummaryState const &state)
                { return state.owner == owner; });
            if (it == g_transferSummaries.end())
            {
                g_transferSummaries.push_back(
                    {owner, tile, operationTileRoot, tileHeaderRoot, 0, 0, false, {}, false, false});
            }
            else
            {
                it->tile = tile;
                it->operationTileRoot = operationTileRoot;
                it->tileHeaderRoot = tileHeaderRoot;
            }
        }

        HWND hostWindow = tile ? GetRegisteredCircleHost(tile) : nullptr;
        Wh_Log(L"REGISTER owner=%p tile=%p operationRoot=%p headerRoot=%p "
               L"host=%p thread=%lu",
               reinterpret_cast<void *>(owner),
               reinterpret_cast<void *>(tile),
               reinterpret_cast<void *>(operationTileRoot),
               reinterpret_cast<void *>(tileHeaderRoot),
               reinterpret_cast<void *>(hostWindow), GetCurrentThreadId());

        // Reapply only state already associated with this exact live owner.
        // Unregistered display-mode subobject pointers are never retained.
        InitializeRegisteredDisplayMode(owner);
    }

    void RemoveTransferSummary(OperationTileElement *tile)
    {
        std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
        g_transferSummaries.erase(
            std::remove_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [tile](TransferSummaryState const &state)
                { return state.tile == tile; }),
            g_transferSummaries.end());
    }

    void ApplyTransferSummary(COperationStatusTile *owner)
    {
        TransferSummaryState state{};
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [owner](TransferSummaryState const &candidate)
                { return candidate.owner == owner; });
            if (it == g_transferSummaries.end() || !it->bytesValid ||
                !it->operationTileRoot)
            {
                return;
            }
            state = *it;
        }

        DirectUI::Element *summary = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltSummary", false);
        if (!summary)
        {
            return;
        }

        wchar_t completedText[64]{};
        wchar_t totalText[64]{};
        wchar_t combinedText[176]{};
        if (!StrFormatByteSizeW(static_cast<LONGLONG>(state.completedBytes),
                                completedText, ARRAYSIZE(completedText)) ||
            !StrFormatByteSizeW(static_cast<LONGLONG>(state.totalBytes),
                                totalText, ARRAYSIZE(totalText)))
        {
            return;
        }

        int percent = GetStoredCirclePercent(state.tile);
        if (percent >= 0)
        {
            wsprintfW(combinedText, L"%s / %s (%d%%)", completedText,
                      totalText, percent);
        }
        else
        {
            wsprintfW(combinedText, L"%s / %s", completedText, totalText);
        }

        Element_SetContentString_Original(summary, combinedText);

        // OperationStatusWindowSubclassProc suppresses the synthetic
        // transferred/total caption update, so Explorer's native title remains
        // stable while eltSummary keeps the custom body text.
    }

    void ApplyNativeDisplayRate(COperationStatusTile *owner)
    {
        TransferSummaryState state{};
        if (!CopyRegisteredTransferState(owner, &state) ||
            !state.nativeDisplayRateValid || !state.tile)
        {
            return;
        }

        // The custom info panel renders speed from the native Shell rate.
        // Do not repurpose Explorer's item-name link/value pair anymore.
        InvalidateInfoPanelForTile(state.tile);
    }

    void ApplyNativeDisplayRatesForHost(HWND hostWindow)
    {
        std::vector<TransferSummaryState> states;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            states = g_transferSummaries;
        }
        for (TransferSummaryState const &state : states)
        {
            HWND registeredHost = nullptr;
            if (state.nativeDisplayRateValid && state.owner && state.tile &&
                GetUniqueRegisteredCircleHost(state.tile, &registeredHost) &&
                registeredHost == hostWindow)
            {
                ApplyNativeDisplayRate(state.owner);
            }
        }
    }

    void RecordNativeDisplayRateForCurrentThread(double nativeDisplayRate)
    {
        std::vector<TransferSummaryState> states;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            states = g_transferSummaries;
        }

        COperationStatusTile *owner = nullptr;
        OperationTileElement *tile = nullptr;
        HWND matchedHostWindow = nullptr;
        size_t matches = 0;
        DWORD currentThreadId = GetCurrentThreadId();
        for (TransferSummaryState const &state : states)
        {
            HWND hostWindow = nullptr;
            if (!state.owner || !state.tile || !state.operationTileRoot ||
                !GetUniqueRegisteredCircleHost(state.tile, &hostWindow) ||
                !hostWindow || !IsWindow(hostWindow) ||
                GetWindowThreadProcessId(hostWindow, nullptr) !=
                    currentThreadId ||
                !IsSingleNormalProgressTileForHost(state.tile, hostWindow))
            {
                continue;
            }
            owner = state.owner;
            tile = state.tile;
            matchedHostWindow = hostWindow;
            ++matches;
        }
        if (matches != 1)
        {
            return;
        }

        bool recorded = false;
        bool firstNativeRate = false;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [owner, tile](TransferSummaryState const &candidate)
                {
                    return candidate.owner == owner &&
                           candidate.tile == tile;
                });
            if (it != g_transferSummaries.end())
            {
                firstNativeRate = !it->nativeDisplayRateValid;
                it->nativeDisplayRate = nativeDisplayRate;
                it->nativeDisplayRateValid = true;
                it->nativeRateHistory.push_back(nativeDisplayRate);
                if (it->nativeRateHistory.size() >
                    kInfoPanelRateHistorySamples)
                {
                    it->nativeRateHistory.erase(
                        it->nativeRateHistory.begin(),
                        it->nativeRateHistory.begin() +
                            (it->nativeRateHistory.size() -
                             kInfoPanelRateHistorySamples));
                }
                recorded = true;
            }
        }
        if (recorded)
        {
            if (firstNativeRate)
            {
                Wh_Log(L"NATIVE_RATE owner=%p tile=%p bytesPerSecond=%llu "
                       L"source=COperationStatusTileRateCalculator::_CalculateRate",
                       reinterpret_cast<void *>(owner),
                       reinterpret_cast<void *>(tile),
                       static_cast<unsigned long long>(
                           nativeDisplayRate + 0.5));
            }
            ScheduleProgressCirclePosition(matchedHostWindow,
                                           L"native-rate");
        }
    }

    struct NormalProgressLayoutElements
    {
        DirectUI::Element *descriptionHeader;
        DirectUI::Element *summary;
        DirectUI::Element *details;
        DirectUI::Element *speedLabel;
        DirectUI::Element *speedValue;
        DirectUI::Element *timeRemainingLabel;
        DirectUI::Element *timeRemaining;
        DirectUI::Element *itemsRemainingLabel;
        DirectUI::Element *itemsRemaining;
        DirectUI::Element *chartArea;
        DirectUI::Element *rateChart;
        DirectUI::Element *progressBarContainer;
        DirectUI::Element *progressBar;
        DirectUI::Element *regularTile;
    };

    bool DiscoverNormalProgressLayout(
        TransferSummaryState const &state,
        NormalProgressLayoutElements *elements)
    {
        elements->descriptionHeader = state.tileHeaderRoot;
        elements->summary = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltSummary", false);
        elements->details = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltDetails", false);
        elements->speedLabel = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltItemNameLabel", false);
        elements->speedValue = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltItemName", false);
        elements->timeRemainingLabel = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltTimeRemainingLabel", false);
        elements->timeRemaining = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltTimeRemaining", false);
        elements->itemsRemainingLabel = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltItemsRemainingLabel", false);
        elements->itemsRemaining = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltItemsRemaining", false);
        elements->chartArea = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltChartArea", false);
        elements->rateChart = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltRateChart_New", false);
        elements->progressBarContainer = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltProgressBarContainer", false);
        elements->progressBar = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltProgressBar", false);
        elements->regularTile = FindSkinElement(
            state.operationTileRoot, state.tileHeaderRoot,
            L"eltRegularTile", false);
        return elements->descriptionHeader && elements->summary &&
               elements->details && elements->speedLabel &&
               elements->speedValue && elements->timeRemainingLabel &&
               elements->timeRemaining && elements->itemsRemainingLabel &&
               elements->itemsRemaining && elements->chartArea &&
               elements->rateChart && elements->progressBarContainer &&
               elements->progressBar && elements->regularTile;
    }

    bool IsElementDescendantOf(DirectUI::Element *element,
                               DirectUI::Element *ancestor)
    {
        constexpr unsigned int kMaximumAncestryDepth = 32;
        DirectUI::Element *visited[kMaximumAncestryDepth]{};

        if (!element || !ancestor)
        {
            return false;
        }

        for (unsigned int depth = 0; depth < kMaximumAncestryDepth; ++depth)
        {
            if (!element)
            {
                return false;
            }
            for (unsigned int index = 0; index < depth; ++index)
            {
                if (visited[index] == element)
                {
                    return false;
                }
            }
            visited[depth] = element;

            if (element == ancestor)
            {
                return true;
            }
            element = Element_GetParent_Original(element);
        }
        return false;
    }

    bool ValidateNormalProgressHierarchy(
        NormalProgressLayoutElements const &elements,
        unsigned long long transitionId,
        bool logResult = true)
    {
        auto getParent = [](DirectUI::Element *element)
        {
            return element ? Element_GetParent_Original(element) : nullptr;
        };

        DirectUI::Element *headerParent = getParent(elements.descriptionHeader);
        DirectUI::Element *summaryParent = getParent(elements.summary);
        DirectUI::Element *detailsParent = getParent(elements.details);
        DirectUI::Element *speedLabelParent = getParent(elements.speedLabel);
        DirectUI::Element *speedValueParent = getParent(elements.speedValue);
        DirectUI::Element *timeLabelParent =
            getParent(elements.timeRemainingLabel);
        DirectUI::Element *timeValueParent =
            getParent(elements.timeRemaining);
        DirectUI::Element *itemsLabelParent =
            getParent(elements.itemsRemainingLabel);
        DirectUI::Element *itemsValueParent =
            getParent(elements.itemsRemaining);
        DirectUI::Element *chartAreaParent = getParent(elements.chartArea);
        DirectUI::Element *rateChartParent = getParent(elements.rateChart);
        DirectUI::Element *progressContainerParent =
            getParent(elements.progressBarContainer);
        DirectUI::Element *progressBarParent = getParent(elements.progressBar);

        bool headerUnderRegular = IsElementDescendantOf(
            elements.descriptionHeader, elements.regularTile);
        bool summaryUnderRegular = IsElementDescendantOf(
            elements.summary, elements.regularTile);
        bool detailsUnderRegular = IsElementDescendantOf(
            elements.details, elements.regularTile);
        bool speedLabelUnderDetails = IsElementDescendantOf(
            elements.speedLabel, elements.details);
        bool speedValueUnderDetails = IsElementDescendantOf(
            elements.speedValue, elements.details);
        bool timeLabelUnderDetails = IsElementDescendantOf(
            elements.timeRemainingLabel, elements.details);
        bool timeValueUnderDetails = IsElementDescendantOf(
            elements.timeRemaining, elements.details);
        bool itemsLabelUnderDetails = IsElementDescendantOf(
            elements.itemsRemainingLabel, elements.details);
        bool itemsValueUnderDetails = IsElementDescendantOf(
            elements.itemsRemaining, elements.details);
        bool chartAreaUnderDetails = IsElementDescendantOf(
            elements.chartArea, elements.details);
        bool rateChartUnderChartArea = IsElementDescendantOf(
            elements.rateChart, elements.chartArea);
        bool progressContainerUnderRegular = IsElementDescendantOf(
            elements.progressBarContainer, elements.regularTile);
        bool progressBarUnderContainer = IsElementDescendantOf(
            elements.progressBar, elements.progressBarContainer);

        bool valid = headerUnderRegular && summaryUnderRegular &&
                     detailsUnderRegular && speedLabelUnderDetails &&
                     speedValueUnderDetails && timeLabelUnderDetails &&
                     timeValueUnderDetails && itemsLabelUnderDetails &&
                     itemsValueUnderDetails && chartAreaUnderDetails &&
                     rateChartUnderChartArea &&
                     progressContainerUnderRegular &&
                     progressBarUnderContainer;

        if (logResult)
        {
            auto passFail = [](bool passed)
            { return passed ? L"PASS" : L"FAIL"; };
            Wh_Log(
                L"MODE[%llu] HIERARCHY "
                L"headerUnderRegular=%s parent=%p "
                L"summaryUnderRegular=%s parent=%p "
                L"detailsUnderRegular=%s parent=%p "
                L"speedLabelUnderDetails=%s parent=%p "
                L"speedValueUnderDetails=%s parent=%p "
                L"timeLabelUnderDetails=%s parent=%p "
                L"timeValueUnderDetails=%s parent=%p "
                L"itemsLabelUnderDetails=%s parent=%p "
                L"itemsValueUnderDetails=%s parent=%p "
                L"chartAreaUnderDetails=%s parent=%p "
                L"rateChartUnderChartArea=%s parent=%p "
                L"progressContainerUnderRegular=%s parent=%p "
                L"progressBarUnderContainer=%s parent=%p result=%s",
                transitionId, passFail(headerUnderRegular),
                reinterpret_cast<void *>(headerParent),
                passFail(summaryUnderRegular),
                reinterpret_cast<void *>(summaryParent),
                passFail(detailsUnderRegular),
                reinterpret_cast<void *>(detailsParent),
                passFail(speedLabelUnderDetails),
                reinterpret_cast<void *>(speedLabelParent),
                passFail(speedValueUnderDetails),
                reinterpret_cast<void *>(speedValueParent),
                passFail(timeLabelUnderDetails),
                reinterpret_cast<void *>(timeLabelParent),
                passFail(timeValueUnderDetails),
                reinterpret_cast<void *>(timeValueParent),
                passFail(itemsLabelUnderDetails),
                reinterpret_cast<void *>(itemsLabelParent),
                passFail(itemsValueUnderDetails),
                reinterpret_cast<void *>(itemsValueParent),
                passFail(chartAreaUnderDetails),
                reinterpret_cast<void *>(chartAreaParent),
                passFail(rateChartUnderChartArea),
                reinterpret_cast<void *>(rateChartParent),
                passFail(progressContainerUnderRegular),
                reinterpret_cast<void *>(progressContainerParent),
                passFail(progressBarUnderContainer),
                reinterpret_cast<void *>(progressBarParent),
                valid ? L"PASS" : L"FAIL");

            auto logFailedChain = [transitionId](PCWSTR relationship,
                                                  DirectUI::Element *element,
                                                  bool passed)
            {
                if (passed)
                {
                    return;
                }

                constexpr unsigned int kMaximumAncestryDepth = 32;
                DirectUI::Element *visited[kMaximumAncestryDepth]{};
                std::wstring chain;
                for (unsigned int depth = 0;
                     depth < kMaximumAncestryDepth; ++depth)
                {
                    if (!element)
                    {
                        chain += chain.empty() ? L"null" : L" -> null";
                        break;
                    }

                    bool repeated = false;
                    for (unsigned int index = 0; index < depth; ++index)
                    {
                        if (visited[index] == element)
                        {
                            repeated = true;
                            break;
                        }
                    }
                    if (repeated)
                    {
                        chain += L" -> LOOP";
                        break;
                    }
                    visited[depth] = element;

                    wchar_t pointerText[32]{};
                    std::swprintf(pointerText, ARRAYSIZE(pointerText),
                                  L"%p",
                                  reinterpret_cast<void *>(element));
                    if (!chain.empty())
                    {
                        chain += L" -> ";
                    }
                    chain += pointerText;
                    element = Element_GetParent_Original(element);
                }

                Wh_Log(L"MODE[%llu] HIERARCHY_CHAIN relationship=%s "
                       L"chain=%s",
                       transitionId, relationship, chain.c_str());
            };

            logFailedChain(L"headerUnderRegular",
                           elements.descriptionHeader,
                           headerUnderRegular);
            logFailedChain(L"summaryUnderRegular", elements.summary,
                           summaryUnderRegular);
            logFailedChain(L"detailsUnderRegular", elements.details,
                           detailsUnderRegular);
            logFailedChain(L"speedLabelUnderDetails", elements.speedLabel,
                           speedLabelUnderDetails);
            logFailedChain(L"speedValueUnderDetails", elements.speedValue,
                           speedValueUnderDetails);
            logFailedChain(L"timeLabelUnderDetails",
                           elements.timeRemainingLabel,
                           timeLabelUnderDetails);
            logFailedChain(L"timeValueUnderDetails", elements.timeRemaining,
                           timeValueUnderDetails);
            logFailedChain(L"itemsLabelUnderDetails",
                           elements.itemsRemainingLabel,
                           itemsLabelUnderDetails);
            logFailedChain(L"itemsValueUnderDetails", elements.itemsRemaining,
                           itemsValueUnderDetails);
            logFailedChain(L"chartAreaUnderDetails", elements.chartArea,
                           chartAreaUnderDetails);
            logFailedChain(L"rateChartUnderChartArea", elements.rateChart,
                           rateChartUnderChartArea);
            logFailedChain(L"progressContainerUnderRegular",
                           elements.progressBarContainer,
                           progressContainerUnderRegular);
            logFailedChain(L"progressBarUnderContainer", elements.progressBar,
                           progressBarUnderContainer);
        }
        return valid;
    }

    bool IsElementEffectivelyVisible(DirectUI::Element *element)
    {
        constexpr unsigned int kMaximumAncestryDepth = 32;
        DirectUI::Element *visited[kMaximumAncestryDepth]{};

        if (!element)
        {
            return false;
        }

        for (unsigned int depth = 0; depth < kMaximumAncestryDepth; ++depth)
        {
            for (unsigned int index = 0; index < depth; ++index)
            {
                if (visited[index] == element)
                {
                    return false;
                }
            }
            visited[depth] = element;

            if (!Element_GetVisible_Original(element))
            {
                return false;
            }

            DirectUI::Element *parent =
                Element_GetParent_Original(element);
            if (!parent)
            {
                return true;
            }
            element = parent;
        }

        return false;
    }

    struct ElementInvariantState
    {
        bool effectiveVisible;
        bool boundsValid;
        RECT bounds;
    };

    ElementInvariantState CaptureElementInvariant(
        DirectUI::Element *element)
    {
        ElementInvariantState state{};
        if (!element)
        {
            return state;
        }

        state.effectiveVisible = IsElementEffectivelyVisible(element);
        state.boundsValid = SUCCEEDED(
            Element_GetRootRelativeBounds_Original(element, &state.bounds));
        return state;
    }

    bool HasNonzeroBounds(ElementInvariantState const &state)
    {
        return state.boundsValid &&
               state.bounds.right > state.bounds.left &&
               state.bounds.bottom > state.bounds.top;
    }

    struct RowInvariantState
    {
        bool effectiveVisible;
        bool boundsValid;
        bool leavesNonzero;
        RECT bounds;
    };

    RowInvariantState CaptureRowInvariant(DirectUI::Element *label,
                                          DirectUI::Element *value)
    {
        ElementInvariantState labelState = CaptureElementInvariant(label);
        ElementInvariantState valueState = CaptureElementInvariant(value);
        RowInvariantState row{};
        row.effectiveVisible = labelState.effectiveVisible &&
                               valueState.effectiveVisible;
        row.boundsValid = labelState.boundsValid && valueState.boundsValid;
        row.leavesNonzero = HasNonzeroBounds(labelState) &&
                            HasNonzeroBounds(valueState);
        if (row.boundsValid)
        {
            row.bounds.left = std::min(labelState.bounds.left,
                                       valueState.bounds.left);
            row.bounds.top = std::min(labelState.bounds.top,
                                      valueState.bounds.top);
            row.bounds.right = std::max(labelState.bounds.right,
                                        valueState.bounds.right);
            row.bounds.bottom = std::max(labelState.bounds.bottom,
                                         valueState.bounds.bottom);
        }
        return row;
    }

    bool IsSingleNormalProgressTileForHost(OperationTileElement *tile,
                                           HWND hostWindow)
    {
        std::lock_guard<std::mutex> lock(g_circleMutex);
        size_t hostTiles = 0;
        size_t matchingTiles = 0;
        for (CircleState const &circle : g_circles)
        {
            if (circle.hostWindow == hostWindow)
            {
                ++hostTiles;
                if (circle.tile == tile)
                {
                    ++matchingTiles;
                }
            }
        }
        return hostTiles == 1 && matchingTiles == 1;
    }

    bool CalculateCustomHostWindowHeight(HWND hostWindow,
                                         bool expanded,
                                         int *targetWindowHeight,
                                         int *targetClientHeight = nullptr)
    {
        RECT windowRect{};
        RECT clientRect{};
        UINT dpi = GetDpiForWindow(hostWindow);
        if (!dpi || !GetWindowRect(hostWindow, &windowRect) ||
            !GetClientRect(hostWindow, &clientRect))
        {
            return false;
        }

        int logicalClientHeight = kCustomCommonClientHeight +
                                  (expanded
                                       ? kExpandedChartSectionHeight
                                       : 0);
        int scaledClientHeight = ScaleForDpi(logicalClientHeight, dpi);
        int windowHeight = windowRect.bottom - windowRect.top;
        int clientHeight = clientRect.bottom - clientRect.top;
        *targetWindowHeight =
            scaledClientHeight + (windowHeight - clientHeight);
        if (targetClientHeight)
        {
            *targetClientHeight = scaledClientHeight;
        }
        return true;
    }

    bool GetVerifiedCustomHostWindowHeight(HWND hostWindow,
                                           int nativeWindowHeight,
                                           int *targetWindowHeight)
    {
        (void)nativeWindowHeight;

        std::vector<TransferSummaryState> states;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            states = g_transferSummaries;
        }

        TransferSummaryState match{};
        size_t matches = 0;
        for (TransferSummaryState const &state : states)
        {
            if (!state.displayModeKnown || !state.tile ||
                !state.operationTileRoot)
            {
                continue;
            }

            HWND registeredHost = nullptr;
            if (GetUniqueRegisteredCircleHost(state.tile, &registeredHost) &&
                registeredHost == hostWindow)
            {
                match = state;
                ++matches;
            }
        }
        if (matches != 1 ||
            !IsSingleNormalProgressTileForHost(match.tile, hostWindow))
        {
            return false;
        }

        NormalProgressLayoutElements elements{};
        if (!DiscoverNormalProgressLayout(match, &elements) ||
            !ValidateNormalProgressHierarchy(elements, 0, false))
        {
            return false;
        }

        return CalculateCustomHostWindowHeight(
            hostWindow, match.expanded, targetWindowHeight);
    }

    bool ResizeOperationStatusWindowForMode(
        HWND hostWindow,
        bool expanded,
        unsigned long long transitionId)
    {
        RECT windowRect{};
        RECT clientRect{};
        int targetWindowHeight = 0;
        int targetClientHeight = 0;
        if (!GetWindowRect(hostWindow, &windowRect) ||
            !GetClientRect(hostWindow, &clientRect) ||
            !CalculateCustomHostWindowHeight(
                hostWindow, expanded, &targetWindowHeight,
                &targetClientHeight))
        {
            Wh_Log(L"MODE[%llu] CUSTOM_LAYOUT resize-failed host=%p "
                   L"reason=window-rect error=%lu",
                   transitionId, reinterpret_cast<void *>(hostWindow),
                   GetLastError());
            return false;
        }

        int windowWidth = windowRect.right - windowRect.left;
        int windowHeight = windowRect.bottom - windowRect.top;

        if (windowHeight != targetWindowHeight &&
            !SetWindowPos(hostWindow, nullptr, 0, 0, windowWidth,
                          targetWindowHeight,
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
        {
            Wh_Log(L"MODE[%llu] CUSTOM_LAYOUT resize-failed host=%p "
                   L"targetClientHeight=%d targetWindowHeight=%d error=%lu",
                   transitionId, reinterpret_cast<void *>(hostWindow),
                   targetClientHeight, targetWindowHeight, GetLastError());
            return false;
        }

        RECT windowAfter{};
        RECT clientAfter{};
        if (!GetWindowRect(hostWindow, &windowAfter) ||
            !GetClientRect(hostWindow, &clientAfter))
        {
            return false;
        }
        int actualWindowHeight = windowAfter.bottom - windowAfter.top;
        int actualClientHeight = clientAfter.bottom - clientAfter.top;
        bool sizeVerified = actualWindowHeight == targetWindowHeight &&
                            actualClientHeight == targetClientHeight;

        Wh_Log(L"MODE[%llu] CUSTOM_LAYOUT host=%p expanded=%s "
               L"targetClientHeight=%d actualClientHeight=%d "
               L"targetWindowHeight=%d actualWindowHeight=%d result=%s",
               transitionId, reinterpret_cast<void *>(hostWindow),
               expanded ? L"true" : L"false",
               targetClientHeight, actualClientHeight,
               targetWindowHeight, actualWindowHeight,
               sizeVerified ? L"verified" : L"rejected");
        return sizeVerified;
    }

    bool ApplyDisplayMode(COperationStatusTile *owner,
                          bool applyFinalHostGeometry,
                          unsigned long long transitionId)
    {
        TransferSummaryState state{};
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [owner](TransferSummaryState const &candidate)
                { return candidate.owner == owner; });
            if (it == g_transferSummaries.end() || !it->displayModeKnown ||
                !it->tile || !it->operationTileRoot)
            {
                return false;
            }
            state = *it;
        }

        HWND hostWindow = nullptr;
        if (!GetUniqueRegisteredCircleHost(state.tile, &hostWindow) ||
            !hostWindow || !IsWindow(hostWindow) ||
            GetWindowThreadProcessId(hostWindow, nullptr) !=
                GetCurrentThreadId() ||
            !IsSingleNormalProgressTileForHost(state.tile, hostWindow))
        {
            Wh_Log(L"MODE[%llu] CUSTOM_LAYOUT owner=%p result=skipped "
                   L"reason=invalid-or-multiple-tile-host",
                   transitionId, reinterpret_cast<void *>(owner));
            return false;
        }

        NormalProgressLayoutElements elements{};
        if (!DiscoverNormalProgressLayout(state, &elements) ||
            !ValidateNormalProgressHierarchy(elements, transitionId))
        {
            Wh_Log(L"MODE[%llu] CUSTOM_LAYOUT owner=%p result=skipped "
                   L"reason=not-normal-progress-structure",
                   transitionId, reinterpret_cast<void *>(owner));
            return false;
        }

        bool visibilityApplied = true;
        auto setVisible = [&visibilityApplied](DirectUI::Element *element,
                                               bool visible)
        {
            HRESULT result = Element_SetVisible_Original(element, visible);
            visibilityApplied =
                SUCCEEDED(result) && visibilityApplied;
            return result;
        };

        // Keep the native header/summary and action controls, but stop asking
        // DirectUI to host the common status rows. A transparent custom child
        // window renders Speed/Time/Items/completion progress using data from
        // Explorer's own progress/byte/rate callbacks. Expanded mode only
        // changes that custom panel by adding the rate-history chart.
        HRESULT descriptionResult =
            setVisible(elements.descriptionHeader, true);
        HRESULT summaryResult = setVisible(elements.summary, true);
        HRESULT detailsResult = setVisible(elements.details, false);
        HRESULT progressContainerResult =
            setVisible(elements.progressBarContainer, false);
        HRESULT progressResult = setVisible(elements.progressBar, false);
        HRESULT chartAreaResult = setVisible(elements.chartArea, false);
        HRESULT rateChartResult = setVisible(elements.rateChart, false);
        HRESULT regularHeightResult =
            Element_SetRelPixHeight_Original(
                elements.regularTile,
                state.expanded ? kExpandedRegularTileHeight
                               : kCompactRegularTileHeight);

        HWND progressWindow =
            OperationTileElement_GetProgressHWND_Original(state.tile);
        if (progressWindow && IsWindow(progressWindow) &&
            IsWindowVisible(progressWindow))
        {
            ShowWindow(progressWindow, SW_HIDE);
        }
        bool nativeProgressHidden =
            !progressWindow || !IsWindow(progressWindow) ||
            !IsWindowVisible(progressWindow);

        bool geometryApplied =
            !applyFinalHostGeometry ||
            ResizeOperationStatusWindowForMode(
                hostWindow, state.expanded, transitionId);

        PositionInfoPanel(state.tile);
        HWND infoWindow = GetInfoPanelWindowForTile(state.tile);
        bool infoVisible = infoWindow && IsWindow(infoWindow) &&
                           IsWindowVisible(infoWindow);
        if (infoWindow && IsWindow(infoWindow))
        {
            InvalidateRect(infoWindow, nullptr, FALSE);
        }

        bool nativeDetailsHidden =
            !Element_GetVisible_Original(elements.details);
        bool nativeProgressContainerHidden =
            !Element_GetVisible_Original(elements.progressBarContainer);
        bool nativeProgressHiddenElement =
            !Element_GetVisible_Original(elements.progressBar);
        bool nativeChartHidden =
            !Element_GetVisible_Original(elements.chartArea) &&
            !Element_GetVisible_Original(elements.rateChart);

        bool success =
            visibilityApplied && SUCCEEDED(regularHeightResult) &&
            geometryApplied && infoVisible &&
            nativeDetailsHidden && nativeProgressContainerHidden &&
            nativeProgressHiddenElement && nativeProgressHidden &&
            nativeChartHidden;

        Wh_Log(L"MODE[%llu] CUSTOM_PANEL owner=%p expanded=%s "
               L"descriptionSet=0x%08X summarySet=0x%08X "
               L"detailsSet=0x%08X progressContainerSet=0x%08X "
               L"progressSet=0x%08X chartSet=0x%08X rateChartSet=0x%08X "
               L"regularHeightSet=0x%08X infoHwnd=%p infoVisible=%s "
               L"nativeProgressHidden=%s finalGeometry=%s result=%s",
               transitionId, reinterpret_cast<void *>(owner),
               state.expanded ? L"true" : L"false",
               static_cast<unsigned int>(descriptionResult),
               static_cast<unsigned int>(summaryResult),
               static_cast<unsigned int>(detailsResult),
               static_cast<unsigned int>(progressContainerResult),
               static_cast<unsigned int>(progressResult),
               static_cast<unsigned int>(chartAreaResult),
               static_cast<unsigned int>(rateChartResult),
               static_cast<unsigned int>(regularHeightResult),
               reinterpret_cast<void *>(infoWindow),
               infoVisible ? L"yes" : L"no",
               nativeProgressHidden ? L"yes" : L"no",
               applyFinalHostGeometry ? L"yes" : L"no",
               success ? L"success" : L"failure");

        return success;
    }

    void LogFinalDisplayInvariant(COperationStatusTile *owner,
                                  HWND hostWindow,
                                  bool expanded,
                                  bool applyResult,
                                  unsigned long long transitionId)
    {
        TransferSummaryState state{};
        NormalProgressLayoutElements elements{};
        bool stateValid = CopyRegisteredTransferState(owner, &state) &&
                          state.tile && state.operationTileRoot;
        bool elementsFound = stateValid &&
                             DiscoverNormalProgressLayout(state, &elements);
        bool hierarchyValid = elementsFound &&
                              ValidateNormalProgressHierarchy(
                                  elements, transitionId, false);

        HWND infoWindow =
            stateValid ? GetInfoPanelWindowForTile(state.tile) : nullptr;
        RECT infoRect{};
        bool infoVisible =
            infoWindow && IsWindow(infoWindow) &&
            IsWindowVisible(infoWindow);
        bool infoRectValid =
            infoVisible && GetWindowRect(infoWindow, &infoRect);
        if (infoRectValid && hostWindow && IsWindow(hostWindow))
        {
            SetLastError(ERROR_SUCCESS);
            int mapResult = MapWindowPoints(
                HWND_DESKTOP, hostWindow,
                reinterpret_cast<POINT *>(&infoRect), 2);
            infoRectValid =
                mapResult != 0 || GetLastError() == ERROR_SUCCESS;
        }

        RECT windowBounds{};
        RECT clientBounds{};
        bool windowBoundsValid = hostWindow && IsWindow(hostWindow) &&
                                 GetWindowRect(hostWindow, &windowBounds) &&
                                 GetClientRect(hostWindow, &clientBounds);
        int actualWindowWidth = windowBoundsValid
                                    ? windowBounds.right - windowBounds.left
                                    : 0;
        int actualWindowHeight = windowBoundsValid
                                     ? windowBounds.bottom - windowBounds.top
                                     : 0;
        int actualClientWidth = windowBoundsValid
                                    ? clientBounds.right - clientBounds.left
                                    : 0;
        int actualClientHeight = windowBoundsValid
                                     ? clientBounds.bottom - clientBounds.top
                                     : 0;

        int targetWindowHeight = 0;
        int targetClientHeight = 0;
        bool targetHeightValid = windowBoundsValid &&
                                 CalculateCustomHostWindowHeight(
                                     hostWindow, expanded,
                                     &targetWindowHeight,
                                     &targetClientHeight);
        bool customHeightActive =
            targetHeightValid &&
            actualWindowHeight == targetWindowHeight &&
            actualClientHeight == targetClientHeight;

        bool headerVisible = elementsFound &&
                             IsElementEffectivelyVisible(
                                 elements.descriptionHeader);
        bool summaryVisible = elementsFound &&
                              IsElementEffectivelyVisible(elements.summary);
        bool nativeDetailsHidden =
            !elementsFound ||
            !IsElementEffectivelyVisible(elements.details);
        bool nativeProgressHidden =
            !elementsFound ||
            (!IsElementEffectivelyVisible(elements.progressBarContainer) &&
             !IsElementEffectivelyVisible(elements.progressBar));
        bool nativeChartHidden =
            !elementsFound ||
            (!IsElementEffectivelyVisible(elements.chartArea) &&
             !IsElementEffectivelyVisible(elements.rateChart));

        UINT dpi = hostWindow && IsWindow(hostWindow)
                       ? GetDpiForWindow(hostWindow)
                       : USER_DEFAULT_SCREEN_DPI;
        int expectedInfoHeight = ScaleForDpi(
            expanded ? kInfoPanelExpandedHeight
                     : kInfoPanelCommonHeight,
            dpi ? dpi : USER_DEFAULT_SCREEN_DPI);
        bool infoGeometryValid =
            infoRectValid &&
            infoRect.right > infoRect.left &&
            infoRect.bottom - infoRect.top == expectedInfoHeight;

        bool pass = applyResult && hierarchyValid && headerVisible &&
                    summaryVisible && infoVisible && infoGeometryValid &&
                    nativeDetailsHidden && nativeProgressHidden &&
                    nativeChartHidden && customHeightActive;

        Wh_Log(
            L"MODE[%llu] FINAL_INVARIANT expanded=%s applyResult=%s "
            L"hierarchyValid=%s header=%s summary=%s "
            L"customPanel=%s rectValid=%s rect=[%ld,%ld,%ld,%ld] "
            L"nativeDetailsHidden=%s nativeProgressHidden=%s "
            L"nativeChartHidden=%s window=%dx%d client=%dx%d "
            L"targetWindowHeight=%d targetClientHeight=%d "
            L"customHeight=%s result=%s",
            transitionId, expanded ? L"true" : L"false",
            applyResult ? L"success" : L"failure",
            hierarchyValid ? L"yes" : L"no",
            headerVisible ? L"visible" : L"hidden",
            summaryVisible ? L"visible" : L"hidden",
            infoVisible ? L"visible" : L"hidden",
            infoRectValid ? L"yes" : L"no",
            infoRect.left, infoRect.top, infoRect.right, infoRect.bottom,
            nativeDetailsHidden ? L"yes" : L"no",
            nativeProgressHidden ? L"yes" : L"no",
            nativeChartHidden ? L"yes" : L"no",
            actualWindowWidth, actualWindowHeight,
            actualClientWidth, actualClientHeight,
            targetWindowHeight, targetClientHeight,
            customHeightActive ? L"active" : L"inactive",
            pass ? L"PASS" : L"FAIL");
    }

    void InitializeRegisteredDisplayMode(COperationStatusTile *owner)
    {
        TransferSummaryState state{};
        if (!CopyRegisteredTransferState(owner, &state) || !state.tile ||
            !state.operationTileRoot)
        {
            return;
        }

        NormalProgressLayoutElements elements{};
        if (!DiscoverNormalProgressLayout(state, &elements))
        {
            return;
        }

        bool detailsVisible = Element_GetVisible_Original(elements.details);
        bool progressBarVisible =
            Element_GetVisible_Original(elements.progressBar);
        // The transition diagnostics established this paired native signature
        // on the target build. Do not infer initial mode from host geometry or
        // from chart visibility, which this mod itself intentionally changes.
        bool nativeCompact = !detailsVisible && progressBarVisible;
        bool nativeExpanded = detailsVisible && !progressBarVisible;
        if (!nativeCompact && !nativeExpanded)
        {
            Wh_Log(L"INITIAL_MODE owner=%p result=skipped "
                   L"reason=unrecognized-native-visibility",
                   reinterpret_cast<void *>(owner));
            return;
        }

        bool expanded = nativeExpanded;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [owner](TransferSummaryState const &candidate)
                { return candidate.owner == owner; });
            if (it == g_transferSummaries.end() || it->tile != state.tile ||
                it->operationTileRoot != state.operationTileRoot)
            {
                return;
            }
            it->displayModeKnown = true;
            it->expanded = expanded;
        }

        unsigned long long transitionId = ++g_displayTransitionSequence;
        Wh_Log(L"MODE[%llu] INITIAL_MODE owner=%p expanded=%s result=matched",
               transitionId, reinterpret_cast<void *>(owner),
               expanded ? L"true" : L"false");
        ApplyDisplayMode(owner, false, transitionId);
        ScheduleDeferredDisplaySnapshot(owner, transitionId, expanded);
    }

    void RecordDisplayMode(COperationStatusTile *owner,
                           bool expanded,
                           unsigned long long transitionId)
    {
        bool recorded = false;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [owner](TransferSummaryState const &state)
                { return state.owner == owner; });
            if (it != g_transferSummaries.end() && it->tile &&
                it->operationTileRoot)
            {
                it->displayModeKnown = true;
                it->expanded = expanded;
                recorded = true;
            }
        }
        if (recorded)
        {
            ApplyDisplayMode(owner, false, transitionId);
        }
    }

    void RecordTransferBytes(COperationStatusTile *owner,
                             unsigned long long completedItems,
                             unsigned long long totalItems,
                             unsigned long long completedBytes,
                             unsigned long long totalBytes)
    {
        OperationTileElement *tile = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [owner](TransferSummaryState const &state)
                { return state.owner == owner; });
            if (it == g_transferSummaries.end())
            {
                return;
            }
            it->completedItems = completedItems;
            it->totalItems = totalItems;
            it->itemsValid = true;
            it->completedBytes = completedBytes;
            it->totalBytes = totalBytes;
            it->bytesValid = true;
            tile = it->tile;
        }
        ApplyTransferSummary(owner);
        if (tile)
        {
            InvalidateInfoPanelForTile(tile);
        }
    }

    void RecordNativeSummary(COperationStatusTile *owner, PCWSTR summary)
    {
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [owner](TransferSummaryState const &state)
                { return state.owner == owner; });
            if (it == g_transferSummaries.end())
            {
                return;
            }
            it->nativeSummary = summary ? summary : L"";
        }
        ApplyTransferSummary(owner);
    }

    void RefreshTransferSummaryForTile(OperationTileElement *tile)
    {
        COperationStatusTile *owner = nullptr;
        {
            std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
            auto it = std::find_if(
                g_transferSummaries.begin(), g_transferSummaries.end(),
                [tile](TransferSummaryState const &state)
                { return state.tile == tile; });
            if (it != g_transferSummaries.end())
            {
                owner = it->owner;
            }
        }
        if (owner)
        {
            ApplyTransferSummary(owner);
        }
    }

    struct NativeProgressSnapshot
    {
        int position;
        int rangeLow;
        int rangeHigh;
        int percent;
        bool rangeValid;
    };

    NativeProgressSnapshot ReadNativeProgress(HWND progressWindow,
                                              int fallbackPercent)
    {
        NativeProgressSnapshot snapshot{};
        snapshot.percent = std::clamp(fallbackPercent, 0, 100);
        if (!progressWindow)
        {
            return snapshot;
        }

        snapshot.position = static_cast<int>(SendMessageW(
            progressWindow, PBM_GETPOS, 0, 0));
        PBRANGE range{};
        SendMessageW(progressWindow, PBM_GETRANGE, FALSE,
                     reinterpret_cast<LPARAM>(&range));
        snapshot.rangeLow = range.iLow;
        snapshot.rangeHigh = range.iHigh;
        snapshot.rangeValid = snapshot.rangeHigh > snapshot.rangeLow;
        if (snapshot.rangeValid)
        {
            long long numerator =
                (static_cast<long long>(snapshot.position) -
                 snapshot.rangeLow) *
                100;
            long long denominator =
                static_cast<long long>(snapshot.rangeHigh) -
                snapshot.rangeLow;
            snapshot.percent = static_cast<int>(std::clamp(
                numerator / denominator, 0LL, 100LL));
        }
        return snapshot;
    }

    bool EnsureProgressCircle(OperationTileElement *tile,
                              unsigned long long eventId,
                              NativeProgressSnapshot const &progress)
    {
        if (!tile || !g_circleClassAtom || !g_infoPanelClassAtom)
        {
            return false;
        }

        HWND progressWindow =
            OperationTileElement_GetProgressHWND_Original(tile);
        HWND hostWindow =
            GetOperationStatusWindowForTile(progressWindow, eventId);
        if (!hostWindow)
        {
            return false;
        }

        HWND existingCircleWindow = nullptr;
        bool circleExists = false;
        bool repaintNeeded = false;
        bool progressSubclassNeeded = false;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [tile](CircleState const &state)
                { return state.tile == tile; });
            if (it != g_circles.end())
            {
                circleExists = true;
                bool percentageChanged =
                    it->progressPercent != progress.percent;
                bool rangeChanged =
                    !it->progressRangeInitialized ||
                    it->progressRangeLow != progress.rangeLow ||
                    it->progressRangeHigh != progress.rangeHigh;
                it->progressPercent = progress.percent;
                it->progressRangeLow = progress.rangeLow;
                it->progressRangeHigh = progress.rangeHigh;
                it->progressRangeInitialized = true;
                it->progressRangeValid = progress.rangeValid;
                if (!it->progressWindow && progressWindow)
                {
                    it->progressWindow = progressWindow;
                    progressSubclassNeeded = true;
                }
                if (!it->eventId)
                {
                    it->eventId = eventId;
                }
                existingCircleWindow = it->circleWindow;
                repaintNeeded = percentageChanged;
                if (rangeChanged)
                {
                    Wh_Log(L"tile=%p progressRange low=%d high=%d "
                           L"position=%d percent=%d",
                           reinterpret_cast<void *>(tile), progress.rangeLow,
                           progress.rangeHigh, progress.position,
                           progress.percent);
                }
            }
        }
        if (circleExists)
        {
            if (progressSubclassNeeded &&
                !SetWindowSubclass(
                    progressWindow, NativeProgressWindowSubclassProc,
                    kProgressWindowSubclassId, 0) &&
                eventId)
            {
                Wh_Log(L"eventId=%llu circle SetWindowSubclass progress failed "
                       L"error=%lu",
                       eventId, GetLastError());
            }
            if (repaintNeeded && existingCircleWindow &&
                IsWindow(existingCircleWindow))
            {
                // Percent changes are infrequent (at most 100 visible steps).
                // Paint the already-buffered circle immediately so the ring
                // doesn't sit one percentage point behind Explorer's title/body
                // text while a fast NVMe copy is advancing quickly.
                RedrawWindow(existingCircleWindow, nullptr, nullptr,
                             RDW_INVALIDATE | RDW_UPDATENOW);
            }
            return true;
        }

        HWND circleWindow = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_NOPARENTNOTIFY,
            kCircleWindowClass, nullptr, WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1,
            hostWindow, nullptr, g_circleClassInstance, nullptr);
        if (!circleWindow)
        {
            Wh_Log(L"eventId=%llu circle CreateWindowExW failed error=%lu",
                   eventId, GetLastError());
            return false;
        }

        HWND infoWindow = CreateWindowExW(
            WS_EX_NOACTIVATE | WS_EX_TRANSPARENT | WS_EX_NOPARENTNOTIFY,
            kInfoPanelWindowClass, nullptr, WS_CHILD | WS_CLIPSIBLINGS,
            0, 0, 1, 1, hostWindow, nullptr, g_circleClassInstance, nullptr);
        if (!infoWindow)
        {
            Wh_Log(L"eventId=%llu info-panel CreateWindowExW failed error=%lu",
                   eventId, GetLastError());
            DestroyWindow(circleWindow);
            return false;
        }

        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            g_circles.push_back(
                {tile, circleWindow, infoWindow, progressWindow, hostWindow,
                 progress.percent, progress.rangeLow, progress.rangeHigh,
                 true, progress.rangeValid, eventId, 0, 0, 0, 0, false});
        }

        Wh_Log(L"tile=%p progressRange low=%d high=%d position=%d percent=%d",
               reinterpret_cast<void *>(tile), progress.rangeLow,
               progress.rangeHigh, progress.position, progress.percent);

        bool hostSubclassed = EnsureHostSubclass(hostWindow, eventId);
        bool progressSubclassed = true;
        if (progressWindow)
        {
            progressSubclassed = SetWindowSubclass(
                progressWindow, NativeProgressWindowSubclassProc,
                kProgressWindowSubclassId, 0);
            if (!progressSubclassed)
            {
                Wh_Log(L"eventId=%llu circle SetWindowSubclass progress failed "
                       L"error=%lu",
                       eventId, GetLastError());
            }
        }

        ScheduleProgressCirclePosition(hostWindow, L"tile-added");
        InvalidateRect(circleWindow, nullptr, FALSE);
        InvalidateRect(infoWindow, nullptr, FALSE);
        Wh_Log(L"eventId=%llu circle created tile=%p host=%p progressHwnd=%p "
               L"infoHwnd=%p hostSubclass=%s progressSubclass=%s result=%s",
               eventId, reinterpret_cast<void *>(tile),
               reinterpret_cast<void *>(hostWindow),
               reinterpret_cast<void *>(progressWindow),
               reinterpret_cast<void *>(infoWindow),
               hostSubclassed ? L"yes" : L"no",
               progressSubclassed ? L"yes" : L"no",
               hostSubclassed && progressSubclassed ? L"success" : L"failure");
        return true;
    }

    void UpdateProgressCircle(OperationTileElement *tile)
    {
        int fallbackPercent = 0;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [tile](CircleState const &state)
                { return state.tile == tile; });
            if (it != g_circles.end())
            {
                fallbackPercent = it->progressPercent;
            }
        }

        HWND progressWindow =
            OperationTileElement_GetProgressHWND_Original(tile);
        NativeProgressSnapshot progress =
            ReadNativeProgress(progressWindow, fallbackPercent);
        EnsureProgressCircle(tile, 0, progress);
        RefreshTransferSummaryForTile(tile);
        InvalidateInfoPanelForTile(tile);
        SyncHostCaptionFromCircle(tile, progress.percent);
    }

    void DestroyProgressCircle(OperationTileElement *tile)
    {
        CircleState removed{};
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [tile](CircleState const &state)
                { return state.tile == tile; });
            if (it != g_circles.end())
            {
                removed = *it;
                g_circles.erase(it);
                found = true;
            }
        }
        if (!found)
        {
            return;
        }

        if (removed.progressWindow && IsWindow(removed.progressWindow))
        {
            RemoveWindowSubclass(removed.progressWindow,
                                 NativeProgressWindowSubclassProc,
                                 kProgressWindowSubclassId);
        }
        if (removed.infoWindow && IsWindow(removed.infoWindow))
        {
            DestroyWindow(removed.infoWindow);
        }
        if (removed.circleWindow && IsWindow(removed.circleWindow))
        {
            DestroyWindow(removed.circleWindow);
        }
        if (removed.hostWindow && IsWindow(removed.hostWindow))
        {
            ScheduleProgressCirclePosition(removed.hostWindow,
                                           L"tile-removed");
        }
    }

    void DestroyAllProgressCircles()
    {
        std::vector<HWND> circleWindows;
        std::vector<HWND> infoWindows;
        std::vector<HWND> hosts;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            for (auto const &state : g_circles)
            {
                circleWindows.push_back(state.circleWindow);
                infoWindows.push_back(state.infoWindow);
            }
            hosts = g_subclassedHosts;
        }

        for (HWND infoWindow : infoWindows)
        {
            if (infoWindow && IsWindow(infoWindow))
            {
                DWORD windowThread =
                    GetWindowThreadProcessId(infoWindow, nullptr);
                if (windowThread == GetCurrentThreadId())
                {
                    DestroyWindow(infoWindow);
                }
                else
                {
                    DWORD_PTR ignored;
                    SendMessageTimeoutW(
                        infoWindow, WM_CLOSE, 0, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored);
                }
            }
        }

        for (HWND circleWindow : circleWindows)
        {
            if (!circleWindow || !IsWindow(circleWindow))
            {
                continue;
            }
            DWORD windowThread =
                GetWindowThreadProcessId(circleWindow, nullptr);
            if (windowThread == GetCurrentThreadId())
            {
                DestroyWindow(circleWindow);
            }
            else
            {
                DWORD_PTR ignored;
                if (!SendMessageTimeoutW(
                        circleWindow, WM_CLOSE, 0, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored))
                {
                    Wh_Log(L"Circle cleanup: WM_CLOSE failed hwnd=%p "
                           L"error=%lu",
                           reinterpret_cast<void *>(circleWindow),
                           GetLastError());
                }
            }
        }

        for (HWND hostWindow : hosts)
        {
            if (!hostWindow || !IsWindow(hostWindow))
            {
                continue;
            }
            ResetUnifiedHostChrome(hostWindow);
            DWORD windowThread =
                GetWindowThreadProcessId(hostWindow, nullptr);
            if (windowThread == GetCurrentThreadId())
            {
                RemoveWindowSubclass(hostWindow,
                                     OperationStatusWindowSubclassProc,
                                     kHostWindowSubclassId);
            }
            else
            {
                DWORD_PTR ignored;
                if (!SendMessageTimeoutW(
                        hostWindow, g_removeHostSubclassMessage, 0, 0,
                        SMTO_ABORTIFHUNG | SMTO_BLOCK, 1000, &ignored))
                {
                    Wh_Log(L"Circle cleanup: host subclass removal failed "
                           L"hwnd=%p error=%lu",
                           reinterpret_cast<void *>(hostWindow),
                           GetLastError());
                }
            }
        }
        std::lock_guard<std::mutex> lock(g_circleMutex);
        g_circles.clear();
        g_subclassedHosts.clear();
        g_hostPositionRequests.clear();
    }

    bool InitializeProgressCircleUi()
    {
        Gdiplus::GdiplusStartupInput startupInput;
        if (Gdiplus::GdiplusStartup(&g_gdiplusToken, &startupInput, nullptr) !=
            Gdiplus::Ok)
        {
            Wh_Log(L"Circle setup failed: GdiplusStartup failed");
            return false;
        }

        g_removeHostSubclassMessage = RegisterWindowMessageW(
            L"Windhawk.FileOperationStyler.RemoveHostSubclass.0.10.3");
        if (!g_removeHostSubclassMessage)
        {
            Wh_Log(L"Circle setup failed: RegisterWindowMessageW error=%lu",
                   GetLastError());
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            g_removeHostSubclassMessage = 0;
            g_positionCirclesMessage = 0;
            g_logDisplayStateMessage = 0;
            return false;
        }
        g_positionCirclesMessage = RegisterWindowMessageW(
            L"Windhawk.FileOperationStyler.PositionCircles.0.10.3");
        if (!g_positionCirclesMessage)
        {
            Wh_Log(L"Circle setup failed: position RegisterWindowMessageW "
                   L"error=%lu",
                   GetLastError());
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            g_removeHostSubclassMessage = 0;
            return false;
        }

        g_logDisplayStateMessage = RegisterWindowMessageW(
            L"Windhawk.FileOperationStyler.LogDisplayState.0.10.41");
        if (!g_logDisplayStateMessage)
        {
            Wh_Log(L"Diagnostic setup failed: display-state "
                   L"RegisterWindowMessageW error=%lu",
                   GetLastError());
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            g_removeHostSubclassMessage = 0;
            g_positionCirclesMessage = 0;
            return false;
        }

        HMODULE circleModule = nullptr;
        if (!GetModuleHandleExW(
                GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                    GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<PCWSTR>(ProgressCircleWindowProc),
                &circleModule))
        {
            Wh_Log(L"Circle setup failed: GetModuleHandleExW error=%lu",
                   GetLastError());
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            g_removeHostSubclassMessage = 0;
            g_positionCirclesMessage = 0;
            g_logDisplayStateMessage = 0;
            return false;
        }
        g_circleClassInstance = circleModule;
        WNDCLASSEXW windowClass{};
        windowClass.cbSize = sizeof(windowClass);
        windowClass.style = CS_HREDRAW | CS_VREDRAW;
        windowClass.lpfnWndProc = ProgressCircleWindowProc;
        windowClass.hInstance = g_circleClassInstance;
        windowClass.lpszClassName = kCircleWindowClass;
        g_circleClassAtom = RegisterClassExW(&windowClass);
        if (!g_circleClassAtom)
        {
            Wh_Log(L"Circle setup failed: RegisterClassExW error=%lu",
                   GetLastError());
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            g_removeHostSubclassMessage = 0;
            g_positionCirclesMessage = 0;
            g_logDisplayStateMessage = 0;
            return false;
        }

        WNDCLASSEXW infoClass{};
        infoClass.cbSize = sizeof(infoClass);
        infoClass.style = CS_HREDRAW | CS_VREDRAW;
        infoClass.lpfnWndProc = InfoPanelWindowProc;
        infoClass.hInstance = g_circleClassInstance;
        infoClass.lpszClassName = kInfoPanelWindowClass;
        g_infoPanelClassAtom = RegisterClassExW(&infoClass);
        if (!g_infoPanelClassAtom)
        {
            Wh_Log(L"Info-panel setup failed: RegisterClassExW error=%lu",
                   GetLastError());
            UnregisterClassW(kCircleWindowClass, g_circleClassInstance);
            g_circleClassAtom = 0;
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
            g_removeHostSubclassMessage = 0;
            g_positionCirclesMessage = 0;
            g_logDisplayStateMessage = 0;
            return false;
        }

        Wh_Log(L"circle renderer=memory-dc info-panel=memory-dc");
        return true;
    }

    void ShutdownProgressCircleUi()
    {
        DestroyAllProgressCircles();
        if (g_infoPanelClassAtom)
        {
            if (!UnregisterClassW(kInfoPanelWindowClass,
                                  g_circleClassInstance))
            {
                Wh_Log(L"Info-panel cleanup: UnregisterClassW failed error=%lu",
                       GetLastError());
            }
            g_infoPanelClassAtom = 0;
        }
        if (g_circleClassAtom)
        {
            if (!UnregisterClassW(kCircleWindowClass,
                                  g_circleClassInstance))
            {
                Wh_Log(L"Circle cleanup: UnregisterClassW failed error=%lu",
                       GetLastError());
            }
            g_circleClassAtom = 0;
        }
        if (g_gdiplusToken)
        {
            Gdiplus::GdiplusShutdown(g_gdiplusToken);
            g_gdiplusToken = 0;
        }
        g_removeHostSubclassMessage = 0;
        g_positionCirclesMessage = 0;
        g_logDisplayStateMessage = 0;
        {
            std::lock_guard<std::mutex> lock(g_displayDiagnosticMutex);
            g_deferredDisplaySnapshots.clear();
        }
    }

    void __cdecl OperationTileElement_OnPropertyChanged_Hook(
        OperationTileElement *thisPtr,
        DirectUI::PropertyInfo const *property,
        int propertyIndex,
        DirectUI::Value *oldValue,
        DirectUI::Value *newValue)
    {
        bool isProgressPosition =
            property &&
            property == OperationTileElement_ProgressPositionProp_Original();

        OperationTileElement_OnPropertyChanged_Original(
            thisPtr, property, propertyIndex, oldValue, newValue);

        if (!isProgressPosition)
        {
            return;
        }

        HWND progressWindow =
            OperationTileElement_GetProgressHWND_Original(thisPtr);
        if (!progressWindow)
        {
            return;
        }

        UpdateProgressCircle(thisPtr);
    }

    HRESULT __cdecl COperationStatusTile_UpdateRemainingItemsAndSize_Hook(
        COperationStatusTile *thisPtr,
        unsigned long long completedItems,
        unsigned long long totalItems,
        unsigned long long completedBytes,
        unsigned long long totalBytes)
    {
        HRESULT result =
            COperationStatusTile_UpdateRemainingItemsAndSize_Original(
                thisPtr, completedItems, totalItems, completedBytes,
                totalBytes);
        if (SUCCEEDED(result))
        {
            RecordTransferBytes(thisPtr, completedItems, totalItems,
                                completedBytes, totalBytes);
        }
        return result;
    }

    HRESULT __cdecl COperationStatusTile_UpdateSummary_Hook(
        COperationStatusTile *thisPtr,
        PCWSTR summary)
    {
        HRESULT result = COperationStatusTile_UpdateSummary_Original(
            thisPtr, summary);
        if (SUCCEEDED(result))
        {
            RecordNativeSummary(thisPtr, summary);
        }
        return result;
    }

    double __cdecl COperationStatusTileRateCalculator_CalculateRate_Hook(
        COperationStatusTileRateCalculator *thisPtr,
        unsigned long long value1,
        unsigned long long value2,
        unsigned long long value3,
        unsigned long long value4,
        unsigned long long value5,
        unsigned long long value6,
        unsigned long long value7,
        double *secondaryRate)
    {
        double result = COperationStatusTileRateCalculator_CalculateRate_Original(
            thisPtr, value1, value2, value3, value4, value5, value6,
            value7, secondaryRate);
        if (std::isfinite(result) && result >= 0.0 &&
            result <= static_cast<double>(
                          std::numeric_limits<LONGLONG>::max()))
        {
            RecordNativeDisplayRateForCurrentThread(result);
        }
        return result;
    }

    HRESULT __cdecl COperationStatusTile_SetTileDisplayMode_Hook(
        COperationStatusTile *thisPtr,
        bool expanded)
    {
        unsigned long long transitionId = ++g_displayTransitionSequence;
        LogRegisteredTransferStatesForDisplayMode(thisPtr, transitionId);

        COperationStatusTile *canonicalOwner = nullptr;
        bool ownerResolved = ResolveDisplayModeOwner(
            thisPtr, transitionId, &canonicalOwner);
        COperationStatusTile *snapshotOwner =
            ownerResolved ? canonicalOwner : thisPtr;
        LogDisplayState(snapshotOwner, transitionId, L"BEFORE_NATIVE",
                        expanded);

        HRESULT result = COperationStatusTile_SetTileDisplayMode_Original(
            thisPtr, expanded);

        COperationStatusTile *postNativeOwner = nullptr;
        bool ownerStillResolved =
            ownerResolved &&
            ResolveDisplayModeOwner(thisPtr, transitionId,
                                    &postNativeOwner, false) &&
            postNativeOwner == canonicalOwner;
        snapshotOwner = ownerStillResolved ? canonicalOwner : thisPtr;
        LogDisplayState(snapshotOwner, transitionId, L"AFTER_NATIVE",
                        expanded);

        if (SUCCEEDED(result) && ownerStillResolved)
        {
            RecordDisplayMode(canonicalOwner, expanded, transitionId);
        }
        LogDisplayState(snapshotOwner, transitionId, L"AFTER_MOD", expanded);
        if (ownerStillResolved)
        {
            ScheduleDeferredDisplaySnapshot(canonicalOwner, transitionId,
                                            expanded);
        }
        else
        {
            Wh_Log(L"MODE[%llu] DEFERRED schedule-skipped "
                   L"reason=owner-resolution-failed",
                   transitionId);
        }
        return result;
    }

    void __cdecl OperationTileElement_Destructor_Hook(
        OperationTileElement *thisPtr)
    {
        CancelDeferredDisplaySnapshotsForTile(thisPtr);
        RemoveTransferSummary(thisPtr);
        DestroyProgressCircle(thisPtr);
        OperationTileElement_Destructor_Original(thisPtr);
    }

    struct WindowResizeResult
    {
        int beforeWidth;
        int beforeHeight;
        int afterWidth;
        int afterHeight;
        bool hostResized;
        bool success;
    };

    WindowResizeResult EnsureOperationStatusWindowWidth(
        unsigned long long eventId)
    {
        WindowResizeResult resizeResult{};
        WindowLookupContext context{eventId, nullptr};
        SetLastError(ERROR_SUCCESS);
        BOOL enumResult = EnumThreadWindows(
            GetCurrentThreadId(), FindOperationStatusWindow,
            reinterpret_cast<LPARAM>(&context));
        if (!context.operationStatusWindow)
        {
            DWORD error = enumResult ? ERROR_NOT_FOUND : GetLastError();
            if (error == ERROR_SUCCESS)
            {
                error = ERROR_NOT_FOUND;
            }
            Wh_Log(L"eventId=%llu base-layout OperationStatusWindow lookup "
                   L"failed error=%lu",
                   eventId, error);
            return resizeResult;
        }

        RECT windowRect;
        if (!GetWindowRect(context.operationStatusWindow, &windowRect))
        {
            Wh_Log(L"eventId=%llu base-layout GetWindowRect before failed "
                   L"error=%lu",
                   eventId, GetLastError());
            return resizeResult;
        }

        resizeResult.beforeWidth = windowRect.right - windowRect.left;
        resizeResult.beforeHeight = windowRect.bottom - windowRect.top;
        resizeResult.afterWidth = resizeResult.beforeWidth;
        resizeResult.afterHeight = resizeResult.beforeHeight;

        RECT clientRect;
        if (!GetClientRect(context.operationStatusWindow, &clientRect))
        {
            Wh_Log(L"eventId=%llu base-layout GetClientRect before failed "
                   L"error=%lu",
                   eventId, GetLastError());
            return resizeResult;
        }

        UINT dpi = GetDpiForWindow(context.operationStatusWindow);
        if (!dpi)
        {
            Wh_Log(L"eventId=%llu base-layout GetDpiForWindow failed dpi=0",
                   eventId);
            return resizeResult;
        }

        int targetClientWidth =
            MulDiv(kRequestedTileWidth, static_cast<int>(dpi),
                   USER_DEFAULT_SCREEN_DPI);
        int currentClientWidth = clientRect.right - clientRect.left;
        if (currentClientWidth >= targetClientWidth)
        {
            resizeResult.success = true;
            return resizeResult;
        }

        int nonClientWidth = resizeResult.beforeWidth - currentClientWidth;
        int targetWindowWidth = targetClientWidth + nonClientWidth;
        if (!SetWindowPos(context.operationStatusWindow, nullptr, 0, 0,
                          targetWindowWidth, resizeResult.beforeHeight,
                          SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE))
        {
            Wh_Log(L"eventId=%llu base-layout SetWindowPos failed "
                   L"requestedWidth=%d error=%lu",
                   eventId, targetWindowWidth, GetLastError());
            return resizeResult;
        }

        resizeResult.hostResized = true;
        if (!GetWindowRect(context.operationStatusWindow, &windowRect))
        {
            Wh_Log(L"eventId=%llu base-layout GetWindowRect after failed "
                   L"error=%lu",
                   eventId, GetLastError());
            return resizeResult;
        }

        resizeResult.afterWidth = windowRect.right - windowRect.left;
        resizeResult.afterHeight = windowRect.bottom - windowRect.top;
        if (!GetClientRect(context.operationStatusWindow, &clientRect))
        {
            Wh_Log(L"eventId=%llu base-layout GetClientRect after failed "
                   L"error=%lu",
                   eventId, GetLastError());
            return resizeResult;
        }

        resizeResult.success =
            clientRect.right - clientRect.left >= targetClientWidth &&
            resizeResult.afterHeight == resizeResult.beforeHeight;
        return resizeResult;
    }

    bool SetElementRelPixWidth(unsigned long long eventId,
                               DirectUI::Element *element,
                               PCWSTR elementName)
    {
        if (!element)
        {
            Wh_Log(L"eventId=%llu base-layout element-not-found target=%s",
                   eventId, elementName);
            return false;
        }

        HRESULT result = Element_SetRelPixWidth_Original(
            element, kRequestedTileWidth);
        if (FAILED(result))
        {
            LogSetterFailure(eventId, elementName, L"relative-pixel-width",
                             result);
        }
        return SUCCEEDED(result);
    }

    void ApplyBaseLayout(unsigned long long eventId,
                         DirectUI::Element *parentElement,
                         DirectUI::Element *operationTileRoot,
                         DirectUI::Element *tileHeaderRoot)
    {
        DirectUI::Element *tileContents = FindSkinElement(
            operationTileRoot, tileHeaderRoot, L"eltTileContents", false);
        DirectUI::Element *regularTile = FindSkinElement(
            operationTileRoot, tileHeaderRoot, L"eltRegularTile", false);

        bool widthsApplied =
            SetElementRelPixWidth(eventId, parentElement, L"parentElement");
        widthsApplied = SetElementRelPixWidth(
                            eventId, operationTileRoot, L"idOperationTile") &&
                        widthsApplied;
        widthsApplied = SetElementRelPixWidth(
                            eventId, tileContents, L"eltTileContents") &&
                        widthsApplied;
        widthsApplied = SetElementRelPixWidth(
                            eventId, regularTile, L"eltRegularTile") &&
                        widthsApplied;

        bool paddingApplied = false;
        if (regularTile)
        {
            HRESULT paddingResult = Element_SetPadding_Original(
                regularTile, kReservedLeftWidth, kContentTopPadding,
                kContentRightPadding, kContentBottomPadding);
            paddingApplied = SUCCEEDED(paddingResult);
            if (FAILED(paddingResult))
            {
                LogSetterFailure(eventId, L"eltRegularTile", L"padding",
                                 paddingResult);
            }
        }

        HRESULT marginResult = Element_SetMargin_Original(
            operationTileRoot, 0, kTileVerticalMargin, 0, kTileVerticalMargin);
        if (FAILED(marginResult))
        {
            LogSetterFailure(eventId, L"idOperationTile", L"margin",
                             marginResult);
        }

        WindowResizeResult windowResult =
            EnsureOperationStatusWindowWidth(eventId);
        bool success = widthsApplied && paddingApplied &&
                       SUCCEEDED(marginResult) && windowResult.success;
        Wh_Log(L"eventId=%llu base-layout windowBefore=%dx%d "
               L"windowAfter=%dx%d contentWidth=%d leftInset=%d "
               L"hostResize=%s result=%s",
               eventId, windowResult.beforeWidth, windowResult.beforeHeight,
               windowResult.afterWidth, windowResult.afterHeight,
               kRequestedTileWidth, kReservedLeftWidth,
               windowResult.hostResized ? L"yes" : L"no",
               success ? L"success" : L"failure");
    }

    // Exact verified x64 ABI encoded by the Microsoft public symbol:
    // ?_CreateTileElement@COperationStatusTile@@AEAAJKKPEAVElement@DirectUI@@@Z
    // The raw detour receives the implicit COperationStatusTile "this" pointer
    // first, followed by the two 32-bit unsigned-long arguments and Element*.
    using COperationStatusTile_CreateTileElement_t = HRESULT(__cdecl *)(
        COperationStatusTile *thisPtr,
        unsigned long arg1,
        unsigned long arg2,
        DirectUI::Element *parentElement);
    COperationStatusTile_CreateTileElement_t
        COperationStatusTile_CreateTileElement_Original;

    HRESULT __cdecl COperationStatusTile_CreateTileElement_Hook(
        COperationStatusTile *thisPtr,
        unsigned long arg1,
        unsigned long arg2,
        DirectUI::Element *parentElement)
    {
        ScopedTileSkin skinScope;
        if (!skinScope.OwnsSkin())
        {
            return COperationStatusTile_CreateTileElement_Original(
                thisPtr, arg1, arg2, parentElement);
        }

        unsigned long long eventId = ++g_skinEventSequence;
        HRESULT result = COperationStatusTile_CreateTileElement_Original(
            thisPtr, arg1, arg2, parentElement);

        auto &state = g_skinState;
        state.collecting = false;

        if (FAILED(result))
        {
            Wh_Log(L"eventId=%llu skin skipped reason=CreateTileElement-failed "
                   L"result=0x%08X",
                   eventId, static_cast<unsigned int>(result));
            return result;
        }

        DirectUI::Element *operationTileRoot = state.operationTileRoot;
        if (!operationTileRoot)
        {
            Wh_Log(L"eventId=%llu skin skipped "
                   L"reason=idOperationTile-root-not-found",
                   eventId);
            return result;
        }

        HRESULT backgroundResult = Element_SetBackgroundColor_Original(
            operationTileRoot, kBackgroundColor);
        if (FAILED(backgroundResult))
        {
            LogSetterFailure(eventId, L"idOperationTile", L"background",
                             backgroundResult);
        }

        // The circle already paints kBackgroundColor. The remaining visible
        // gray seam comes from the native parent/header surfaces rather than
        // circle geometry, so color those known surfaces instead of changing
        // the circle placement again.
        if (parentElement)
        {
            HRESULT parentBackgroundResult =
                Element_SetBackgroundColor_Original(parentElement,
                                                    kBackgroundColor);
            if (FAILED(parentBackgroundResult))
            {
                LogSetterFailure(eventId, L"parentElement",
                                 L"background-surface",
                                 parentBackgroundResult);
            }
        }
        if (state.tileHeaderRoot)
        {
            HRESULT headerBackgroundResult =
                Element_SetBackgroundColor_Original(state.tileHeaderRoot,
                                                    kBackgroundColor);
            if (FAILED(headerBackgroundResult))
            {
                LogSetterFailure(eventId, L"idTileHeader",
                                 L"background-surface",
                                 headerBackgroundResult);
            }
        }

        ApplyBaseLayout(eventId, parentElement, operationTileRoot,
                        state.tileHeaderRoot);

        auto *operationTile =
            reinterpret_cast<OperationTileElement *>(operationTileRoot);
        HWND nativeProgressWindow =
            OperationTileElement_GetProgressHWND_Original(operationTile);
        NativeProgressSnapshot initialProgress =
            ReadNativeProgress(nativeProgressWindow, 0);
        EnsureProgressCircle(operationTile, eventId, initialProgress);
        RegisterTransferSummary(thisPtr, operationTile, operationTileRoot,
                                state.tileHeaderRoot);

        DirectUI::Element *summary =
            FindSkinElement(operationTileRoot, state.tileHeaderRoot,
                            L"eltSummary", false);
        bool summaryApplied = false;
        bool allFontsApplied = summary != nullptr;
        if (summary)
        {
            HRESULT foregroundResult = Element_SetForegroundColor_Original(
                summary, kPrimaryTextColor);
            if (FAILED(foregroundResult))
            {
                LogSetterFailure(eventId, L"eltSummary", L"foreground",
                                 foregroundResult);
            }

            HRESULT fontFaceResult = Element_SetFontFace_Original(
                summary, L"Segoe UI Variable Display");
            if (FAILED(fontFaceResult))
            {
                LogSetterFailure(eventId, L"eltSummary", L"font-face",
                                 fontFaceResult);
            }
            allFontsApplied = SUCCEEDED(fontFaceResult);

            HRESULT fontSizeResult =
                Element_SetFontSize_Original(summary, 26);
            if (FAILED(fontSizeResult))
            {
                LogSetterFailure(eventId, L"eltSummary", L"font-size",
                                 fontSizeResult);
            }

            HRESULT fontWeightResult =
                Element_SetFontWeight_Original(summary, 600);
            if (FAILED(fontWeightResult))
            {
                LogSetterFailure(eventId, L"eltSummary", L"font-weight",
                                 fontWeightResult);
            }

            HRESULT marginResult =
                Element_SetMargin_Original(summary, 0, 6, 0, 8);
            if (FAILED(marginResult))
            {
                LogSetterFailure(eventId, L"eltSummary", L"margin",
                                 marginResult);
            }

            summaryApplied = SUCCEEDED(foregroundResult) &&
                             SUCCEEDED(fontFaceResult) &&
                             SUCCEEDED(fontSizeResult) &&
                             SUCCEEDED(fontWeightResult) &&
                             SUCCEEDED(marginResult);
        }

        struct TextTarget
        {
            PCWSTR name;
            bool allowHeaderFallback;
        };

        constexpr TextTarget primaryTargets[] = {
            {L"eltItemName", false},
            {L"eltTimeRemaining", false},
            {L"eltItemsRemaining", false},
            {L"eltFirstLocation", true},
            {L"eltSecondLocation", true},
        };
        constexpr TextTarget secondaryTargets[] = {
            {L"eltItemNameLabel", false},
            {L"eltTimeRemainingLabel", false},
            {L"eltItemsRemainingLabel", false},
            {L"eltStartText", true},
            {L"eltMiddleText", true},
            {L"eltEndText", true},
        };

        unsigned int primaryApplied = 0;
        for (auto const &target : primaryTargets)
        {
            TextSkinResult textResult = ApplyTextSkin(
                eventId, operationTileRoot, state.tileHeaderRoot, target.name,
                kPrimaryTextColor, target.allowHeaderFallback);
            primaryApplied += textResult.foregroundApplied ? 1 : 0;
            allFontsApplied = allFontsApplied && textResult.fontApplied;
        }

        unsigned int secondaryApplied = 0;
        for (auto const &target : secondaryTargets)
        {
            TextSkinResult textResult = ApplyTextSkin(
                eventId, operationTileRoot, state.tileHeaderRoot, target.name,
                kSecondaryTextColor, target.allowHeaderFallback);
            secondaryApplied += textResult.foregroundApplied ? 1 : 0;
            allFontsApplied = allFontsApplied && textResult.fontApplied;
        }

        // Keep the native description structure, but make the operation line
        // a deliberate visual header. Locations retain the blue accent while
        // the connecting text uses the primary foreground.
        struct DescriptionTarget
        {
            PCWSTR name;
            COLORREF color;
        };
        constexpr DescriptionTarget descriptionTargets[] = {
            {L"eltStartText", kSecondaryTextColor},
            {L"eltFirstLocation", kAccentRingColor},
            {L"eltMiddleText", kSecondaryTextColor},
            {L"eltSecondLocation", kAccentRingColor},
            {L"eltEndText", kSecondaryTextColor},
        };

        for (auto const &target : descriptionTargets)
        {
            DirectUI::Element *element = FindSkinElement(
                operationTileRoot, state.tileHeaderRoot, target.name, true);
            if (!element)
            {
                continue;
            }

            HRESULT colorResult =
                Element_SetForegroundColor_Original(element, target.color);
            if (FAILED(colorResult))
            {
                LogSetterFailure(eventId, target.name, L"foreground-header",
                                 colorResult);
            }

            HRESULT sizeResult = Element_SetFontSize_Original(element, 15);
            if (FAILED(sizeResult))
            {
                LogSetterFailure(eventId, target.name, L"font-size-header",
                                 sizeResult);
            }

            HRESULT weightResult = Element_SetFontWeight_Original(element, 400);
            if (FAILED(weightResult))
            {
                LogSetterFailure(eventId, target.name, L"font-weight-header",
                                 weightResult);
            }
        }

        // Unify the native tile surfaces so the custom circle and the
        // DirectUI content read as one intentional dark card instead of
        // separate Windows-gray panels. Parentage/order remain untouched.
        for (PCWSTR name : {L"eltTileContents", L"eltRegularTile",
                            L"eltRegularTileHeader", L"eltDetails"})
        {
            DirectUI::Element *element = FindSkinElement(
                operationTileRoot, state.tileHeaderRoot, name, false);
            if (!element)
            {
                continue;
            }

            HRESULT surfaceResult =
                Element_SetBackgroundColor_Original(element, kBackgroundColor);
            if (FAILED(surfaceResult))
            {
                LogSetterFailure(eventId, name, L"background-surface",
                                 surfaceResult);
            }
        }

        // Remove DirectUI frame edges from the known body/chart surfaces.
        // This specifically targets the stray horizontal separator and the
        // bright chart-edge artifacts without changing parentage or order.
        for (DirectUI::Element *element : {operationTileRoot,
                                           FindSkinElement(operationTileRoot, state.tileHeaderRoot, L"eltTileContents", false),
                                           FindSkinElement(operationTileRoot, state.tileHeaderRoot, L"eltRegularTile", false),
                                           FindSkinElement(operationTileRoot, state.tileHeaderRoot, L"eltDetails", false),
                                           FindSkinElement(operationTileRoot, state.tileHeaderRoot, L"eltChartArea", false),
                                           FindSkinElement(operationTileRoot, state.tileHeaderRoot, L"eltRateChart_New", false)})
        {
            if (!element)
            {
                continue;
            }
            Element_SetBorderColor_Original(element, kBackgroundColor);
            Element_SetBorderThickness_Original(element, 0, 0, 0, 0);
        }

        DirectUI::Element *chartArea = FindSkinElement(
            operationTileRoot, state.tileHeaderRoot, L"eltChartArea", false);
        if (chartArea)
        {
            HRESULT backgroundResult =
                Element_SetBackgroundColor_Original(chartArea,
                                                    kGraphSurfaceColor);
            HRESULT heightResult = Element_SetRelPixHeight_Original(
                chartArea, kChartAreaHeight);
            HRESULT marginResult =
                Element_SetMargin_Original(
                    chartArea, 0, kChartAreaTopMargin, 0,
                    kChartAreaBottomMargin);
            if (FAILED(backgroundResult))
            {
                LogSetterFailure(eventId, L"eltChartArea", L"background",
                                 backgroundResult);
            }
            if (FAILED(heightResult))
            {
                LogSetterFailure(eventId, L"eltChartArea",
                                 L"relative-pixel-height", heightResult);
            }
            if (FAILED(marginResult))
            {
                LogSetterFailure(eventId, L"eltChartArea", L"margin",
                                 marginResult);
            }
        }

        DirectUI::Element *rateChart = FindSkinElement(
            operationTileRoot, state.tileHeaderRoot,
            L"eltRateChart_New", false);
        if (rateChart)
        {
            HRESULT heightResult = Element_SetRelPixHeight_Original(
                rateChart, kGraphHeight);
            HRESULT backgroundResult =
                Element_SetBackgroundColor_Original(rateChart,
                                                    kGraphSurfaceColor);
            HRESULT marginResult =
                Element_SetMargin_Original(rateChart, 0, 0, 0, 0);
            if (FAILED(heightResult))
            {
                LogSetterFailure(eventId, L"eltRateChart_New",
                                 L"relative-pixel-height", heightResult);
            }
            if (FAILED(backgroundResult))
            {
                LogSetterFailure(eventId, L"eltRateChart_New",
                                 L"background", backgroundResult);
            }
            if (FAILED(marginResult))
            {
                LogSetterFailure(eventId, L"eltRateChart_New", L"margin",
                                 marginResult);
            }
        }

        // Visibility is applied by ApplyDisplayMode after Explorer records its
        // native compact/expanded state. Keep the shared linear-bar styling
        // here so both custom modes use identical upper-area geometry.
        DirectUI::Element *progressBar = FindSkinElement(
            operationTileRoot, state.tileHeaderRoot, L"eltProgressBar", false);
        if (progressBar)
        {
            HRESULT heightResult =
                Element_SetRelPixHeight_Original(progressBar, 8);
            HRESULT marginResult =
                Element_SetMargin_Original(progressBar, 0, 10, 0, 6);
            if (FAILED(heightResult))
            {
                LogSetterFailure(eventId, L"eltProgressBar",
                                 L"relative-pixel-height=8", heightResult);
            }
            if (FAILED(marginResult))
            {
                LogSetterFailure(eventId, L"eltProgressBar",
                                 L"compact-target-margin", marginResult);
            }
        }

        // Keep the shared details group compact. ApplyDisplayMode keeps this
        // container visible in both modes because it owns the time/items rows.
        DirectUI::Element *details = FindSkinElement(
            operationTileRoot, state.tileHeaderRoot, L"eltDetails", false);
        if (details)
        {
            HRESULT paddingResult =
                Element_SetPadding_Original(details, 0, 0, 0, 0);
            HRESULT marginResult =
                Element_SetMargin_Original(details, 0, 0, 0, 0);
            if (FAILED(paddingResult))
            {
                LogSetterFailure(eventId, L"eltDetails",
                                 L"padding=0", paddingResult);
            }
            if (FAILED(marginResult))
            {
                LogSetterFailure(eventId, L"eltDetails",
                                 L"margin=0", marginResult);
            }
        }

        // Keep Explorer's descriptive labels intact. The target uses plain
        // language (for example "Time remaining") rather than terse "ETA"
        // labels, and retaining native wording also behaves better across states.

        struct DetailTarget
        {
            PCWSTR name;
            COLORREF color;
            int size;
            int weight;
        };
        constexpr DetailTarget detailTargets[] = {
            {L"eltItemNameLabel", kSecondaryTextColor, 14, 400},
            {L"eltItemName", kPrimaryTextColor, 14, 500},
            {L"eltTimeRemainingLabel", kSecondaryTextColor, 14, 400},
            {L"eltTimeRemaining", kPrimaryTextColor, 14, 500},
            {L"eltItemsRemainingLabel", kSecondaryTextColor, 14, 400},
            {L"eltItemsRemaining", kPrimaryTextColor, 14, 500},
        };

        for (auto const &target : detailTargets)
        {
            DirectUI::Element *element = FindSkinElement(
                operationTileRoot, state.tileHeaderRoot, target.name, false);
            if (!element)
            {
                continue;
            }

            HRESULT foregroundResult =
                Element_SetForegroundColor_Original(element, target.color);
            HRESULT faceResult = Element_SetFontFace_Original(
                element, L"Segoe UI Variable");
            HRESULT sizeResult =
                Element_SetFontSize_Original(element, target.size);
            HRESULT weightResult =
                Element_SetFontWeight_Original(element, target.weight);
            bool isLabel =
                lstrcmpW(target.name, L"eltItemNameLabel") == 0 ||
                lstrcmpW(target.name, L"eltTimeRemainingLabel") == 0 ||
                lstrcmpW(target.name, L"eltItemsRemainingLabel") == 0;
            HRESULT marginResult = Element_SetMargin_Original(
                element, 0, 0, isLabel ? 12 : 0, 3);

            if (FAILED(foregroundResult))
                LogSetterFailure(eventId, target.name,
                                 L"foreground-detail", foregroundResult);
            if (FAILED(faceResult))
                LogSetterFailure(eventId, target.name,
                                 L"font-face-detail", faceResult);
            if (FAILED(sizeResult))
                LogSetterFailure(eventId, target.name,
                                 L"font-size-detail", sizeResult);
            if (FAILED(weightResult))
                LogSetterFailure(eventId, target.name,
                                 L"font-weight-detail", weightResult);
            if (FAILED(marginResult))
                LogSetterFailure(eventId, target.name, L"margin-detail",
                                 marginResult);
        }

        // Style the existing native action controls as part of the same
        // visual pass. Their parentage/actions remain untouched.
        struct ActionControlTarget
        {
            PCWSTR name;
            COLORREF color;
            int size;
            int leftMargin;
        };
        constexpr ActionControlTarget actionControlTargets[] = {
            {L"eltPauseButton", kAccentRingColor, 17, 6},
            {L"eltCancelButton", kSecondaryTextColor, 17, 8},
        };

        for (auto const &target : actionControlTargets)
        {
            DirectUI::Element *element = FindSkinElement(
                operationTileRoot, state.tileHeaderRoot, target.name, false);
            if (!element)
            {
                continue;
            }

            HRESULT foregroundResult =
                Element_SetForegroundColor_Original(element, target.color);
            HRESULT backgroundResult =
                Element_SetBackgroundColor_Original(element, kActionSurfaceColor);
            HRESULT faceResult = Element_SetFontFace_Original(
                element, L"Segoe UI Variable");
            HRESULT sizeResult =
                Element_SetFontSize_Original(element, target.size);
            HRESULT weightResult =
                Element_SetFontWeight_Original(element, 500);
            HRESULT paddingResult = Element_SetPadding_Original(
                element, 8, 4, 8, 4);
            HRESULT marginResult = Element_SetMargin_Original(
                element, target.leftMargin, 0, 3, 0);

            if (FAILED(foregroundResult))
                LogSetterFailure(eventId, target.name,
                                 L"foreground-action", foregroundResult);
            if (FAILED(backgroundResult))
                LogSetterFailure(eventId, target.name,
                                 L"background-action", backgroundResult);
            if (FAILED(faceResult))
                LogSetterFailure(eventId, target.name,
                                 L"font-face-action", faceResult);
            if (FAILED(sizeResult))
                LogSetterFailure(eventId, target.name,
                                 L"font-size-action", sizeResult);
            if (FAILED(weightResult))
                LogSetterFailure(eventId, target.name,
                                 L"font-weight-action", weightResult);
            if (FAILED(paddingResult))
                LogSetterFailure(eventId, target.name,
                                 L"padding-action", paddingResult);
            if (FAILED(marginResult))
                LogSetterFailure(eventId, target.name,
                                 L"margin-action", marginResult);
        }

        Wh_Log(L"eventId=%llu skin background=%s summary=%s primary=%u/%u "
               L"secondary=%u/%u font=%s",
               eventId, SUCCEEDED(backgroundResult) ? L"yes" : L"no",
               summaryApplied ? L"yes" : L"no", primaryApplied,
               static_cast<unsigned int>(ARRAYSIZE(primaryTargets)),
               secondaryApplied,
               static_cast<unsigned int>(ARRAYSIZE(secondaryTargets)),
               allFontsApplied ? L"yes" : L"no");

        return result;
    }

    struct SkinTargets
    {
        DUIXmlParser_CreateElement_t parserCreate;
        StrToID_t strToID;
        Element_FindDescendent_t findDescendent;
        Element_GetParent_t getParent;
        Element_SetBackgroundColor_t setBackgroundColor;
        Element_SetForegroundColor_t setForegroundColor;
        Element_SetFontFace_t setFontFace;
        Element_SetFontSize_t setFontSize;
        Element_SetFontWeight_t setFontWeight;
        Element_SetRelPixWidth_t setRelPixWidth;
        Element_SetMargin_t setMargin;
        Element_SetPadding_t setPadding;
        Element_SetBorderColor_t setBorderColor;
        Element_SetBorderThickness_t setBorderThickness;
        Element_SetVisible_t setVisible;
        Element_GetVisible_t getVisible;
        Element_SetRelPixHeight_t setRelPixHeight;
        Element_SetContentString_t setContentString;
        Element_GetRootRelativeBounds_t getRootRelativeBounds;
        COperationStatusTile_CreateTileElement_t createTileElement;
        OperationTileElement_ProgressPositionProp_t progressPositionProp;
        OperationTileElement_GetProgressHWND_t getProgressHWND;
        OperationTileElement_OnPropertyChanged_t onPropertyChanged;
        OperationTileElement_Destructor_t operationTileDestructor;
        COperationStatusTile_UpdateRemainingItemsAndSize_t
            updateRemainingItemsAndSize;
        COperationStatusTile_UpdateSummary_t updateSummary;
        COperationStatusTile_SetTileDisplayMode_t setTileDisplayMode;
        COperationStatusTileRateCalculator_CalculateRate_t calculateRate;
    };

    bool ResolveSkinTargets(SkinTargets *targets)
    {
        HMODULE dui70 =
            LoadLibraryExW(L"dui70.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!dui70)
        {
            Wh_Log(L"Skin setup failed: unable to load dui70.dll "
                   L"error=%lu",
                   GetLastError());
            return false;
        }

        constexpr PCSTR parserCreateSymbol =
            "?CreateElement@DUIXmlParser@DirectUI@@QEAAJPEBGPEAVElement@2@1PEAKPEAPEAV32@@Z";
        constexpr PCSTR findDescendentSymbol =
            "?FindDescendent@Element@DirectUI@@QEAAPEAV12@G@Z";
        constexpr PCSTR getParentSymbol =
            "?GetParent@Element@DirectUI@@QEAAPEAV12@XZ";
        constexpr PCSTR setBackgroundColorSymbol =
            "?SetBackgroundColor@Element@DirectUI@@QEAAJK@Z";
        constexpr PCSTR setForegroundColorSymbol =
            "?SetForegroundColor@Element@DirectUI@@QEAAJK@Z";
        constexpr PCSTR setFontFaceSymbol =
            "?SetFontFace@Element@DirectUI@@QEAAJPEBG@Z";
        constexpr PCSTR setFontSizeSymbol =
            "?SetFontSize@Element@DirectUI@@QEAAJH@Z";
        constexpr PCSTR setFontWeightSymbol =
            "?SetFontWeight@Element@DirectUI@@QEAAJH@Z";
        constexpr PCSTR setRelPixWidthSymbol =
            "?SetRelPixWidth@Element@DirectUI@@QEAAJH@Z";
        constexpr PCSTR setMarginSymbol =
            "?SetMargin@Element@DirectUI@@QEAAJHHHH@Z";
        constexpr PCSTR setPaddingSymbol =
            "?SetPadding@Element@DirectUI@@QEAAJHHHH@Z";
        constexpr PCSTR setBorderColorSymbol =
            "?SetBorderColor@Element@DirectUI@@QEAAJK@Z";
        constexpr PCSTR setBorderThicknessSymbol =
            "?SetBorderThickness@Element@DirectUI@@QEAAJHHHH@Z";
        constexpr PCSTR setVisibleSymbol =
            "?SetVisible@Element@DirectUI@@QEAAJ_N@Z";
        constexpr PCSTR getVisibleSymbol =
            "?GetVisible@Element@DirectUI@@QEAA_NXZ";
        constexpr PCSTR setRelPixHeightSymbol =
            "?SetRelPixHeight@Element@DirectUI@@QEAAJH@Z";
        constexpr PCSTR setContentStringSymbol =
            "?SetContentString@Element@DirectUI@@QEAAJPEBG@Z";
        constexpr PCSTR getRootRelativeBoundsSymbol =
            "?GetRootRelativeBounds@Element@DirectUI@@QEAAJPEAUtagRECT@@@Z";

        targets->parserCreate =
            reinterpret_cast<DUIXmlParser_CreateElement_t>(
                GetProcAddress(dui70, parserCreateSymbol));
        targets->strToID = reinterpret_cast<StrToID_t>(
            GetProcAddress(dui70, "StrToID"));
        targets->findDescendent =
            reinterpret_cast<Element_FindDescendent_t>(
                GetProcAddress(dui70, findDescendentSymbol));
        targets->getParent = reinterpret_cast<Element_GetParent_t>(
            GetProcAddress(dui70, getParentSymbol));
        targets->setBackgroundColor =
            reinterpret_cast<Element_SetBackgroundColor_t>(
                GetProcAddress(dui70, setBackgroundColorSymbol));
        targets->setForegroundColor =
            reinterpret_cast<Element_SetForegroundColor_t>(
                GetProcAddress(dui70, setForegroundColorSymbol));
        targets->setFontFace = reinterpret_cast<Element_SetFontFace_t>(
            GetProcAddress(dui70, setFontFaceSymbol));
        targets->setFontSize = reinterpret_cast<Element_SetFontSize_t>(
            GetProcAddress(dui70, setFontSizeSymbol));
        targets->setFontWeight = reinterpret_cast<Element_SetFontWeight_t>(
            GetProcAddress(dui70, setFontWeightSymbol));
        targets->setRelPixWidth = reinterpret_cast<Element_SetRelPixWidth_t>(
            GetProcAddress(dui70, setRelPixWidthSymbol));
        targets->setMargin = reinterpret_cast<Element_SetMargin_t>(
            GetProcAddress(dui70, setMarginSymbol));
        targets->setPadding = reinterpret_cast<Element_SetPadding_t>(
            GetProcAddress(dui70, setPaddingSymbol));
        targets->setBorderColor = reinterpret_cast<Element_SetBorderColor_t>(
            GetProcAddress(dui70, setBorderColorSymbol));
        targets->setBorderThickness = reinterpret_cast<Element_SetBorderThickness_t>(
            GetProcAddress(dui70, setBorderThicknessSymbol));
        targets->setVisible = reinterpret_cast<Element_SetVisible_t>(
            GetProcAddress(dui70, setVisibleSymbol));
        targets->getVisible = reinterpret_cast<Element_GetVisible_t>(
            GetProcAddress(dui70, getVisibleSymbol));
        targets->setRelPixHeight =
            reinterpret_cast<Element_SetRelPixHeight_t>(
                GetProcAddress(dui70, setRelPixHeightSymbol));
        targets->setContentString =
            reinterpret_cast<Element_SetContentString_t>(
                GetProcAddress(dui70, setContentStringSymbol));
        targets->getRootRelativeBounds =
            reinterpret_cast<Element_GetRootRelativeBounds_t>(
                GetProcAddress(dui70, getRootRelativeBoundsSymbol));

        if (!targets->parserCreate || !targets->strToID ||
            !targets->findDescendent || !targets->getParent ||
            !targets->setBackgroundColor ||
            !targets->setForegroundColor || !targets->setFontFace ||
            !targets->setFontSize || !targets->setFontWeight ||
            !targets->setRelPixWidth || !targets->setMargin ||
            !targets->setPadding || !targets->setBorderColor ||
            !targets->setBorderThickness || !targets->setVisible ||
            !targets->getVisible || !targets->setRelPixHeight ||
            !targets->setContentString ||
            !targets->getRootRelativeBounds)
        {
            Wh_Log(L"Skin setup failed: required dui70 exports missing "
                   L"ParserCreate=%p StrToID=%p FindDescendent=%p "
                   L"GetParent=%p "
                   L"SetBackgroundColor=%p SetForegroundColor=%p "
                   L"SetFontFace=%p SetFontSize=%p SetFontWeight=%p "
                   L"SetRelPixWidth=%p SetMargin=%p SetPadding=%p "
                   L"SetBorderColor=%p SetBorderThickness=%p "
                   L"SetVisible=%p GetVisible=%p SetRelPixHeight=%p "
                   L"GetRootRelativeBounds=%p",
                   reinterpret_cast<void *>(targets->parserCreate),
                   reinterpret_cast<void *>(targets->strToID),
                   reinterpret_cast<void *>(targets->findDescendent),
                   reinterpret_cast<void *>(targets->getParent),
                   reinterpret_cast<void *>(targets->setBackgroundColor),
                   reinterpret_cast<void *>(targets->setForegroundColor),
                   reinterpret_cast<void *>(targets->setFontFace),
                   reinterpret_cast<void *>(targets->setFontSize),
                   reinterpret_cast<void *>(targets->setFontWeight),
                   reinterpret_cast<void *>(targets->setRelPixWidth),
                   reinterpret_cast<void *>(targets->setMargin),
                   reinterpret_cast<void *>(targets->setPadding),
                   reinterpret_cast<void *>(targets->setBorderColor),
                   reinterpret_cast<void *>(targets->setBorderThickness),
                   reinterpret_cast<void *>(targets->setVisible),
                   reinterpret_cast<void *>(targets->getVisible),
                   reinterpret_cast<void *>(targets->setRelPixHeight),
                   reinterpret_cast<void *>(targets->getRootRelativeBounds));
            return false;
        }

        HMODULE shell32 =
            LoadLibraryExW(L"shell32.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!shell32)
        {
            Wh_Log(L"Skin setup failed: unable to load shell32.dll "
                   L"error=%lu",
                   GetLastError());
            return false;
        }

        WindhawkUtils::SYMBOL_HOOK shell32Symbols[] = {
            {
                {LR"(?_CreateTileElement@COperationStatusTile@@AEAAJKKPEAVElement@DirectUI@@@Z)"},
                &targets->createTileElement,
                nullptr,
                false,
            },
            {
                {LR"(?ProgressPositionProp@OperationTileElement@@SAPEBUPropertyInfo@DirectUI@@XZ)"},
                &targets->progressPositionProp,
                nullptr,
                false,
            },
            {
                {LR"(?_GetProgressHWND@OperationTileElement@@AEAAPEAUHWND__@@XZ)"},
                &targets->getProgressHWND,
                nullptr,
                false,
            },
            {
                {LR"(?OnPropertyChanged@OperationTileElement@@UEAAXPEBUPropertyInfo@DirectUI@@HPEAVValue@3@1@Z)"},
                &targets->onPropertyChanged,
                nullptr,
                false,
            },
            {
                {LR"(??1OperationTileElement@@UEAA@XZ)"},
                &targets->operationTileDestructor,
                nullptr,
                false,
            },
            {
                {LR"(?_UpdateRemainingItemsAndSize@COperationStatusTile@@AEAAJ_K000@Z)"},
                &targets->updateRemainingItemsAndSize,
                nullptr,
                false,
            },
            {
                {LR"(?_UpdateSummary@COperationStatusTile@@AEAAJPEBG@Z)"},
                &targets->updateSummary,
                nullptr,
                false,
            },
            {
                {LR"(?SetTileDisplayMode@COperationStatusTile@@UEAAJ_N@Z)"},
                &targets->setTileDisplayMode,
                nullptr,
                false,
            },
            {
                {LR"(?_CalculateRate@COperationStatusTileRateCalculator@@AEAAN_K000000PEAN@Z)"},
                &targets->calculateRate,
                nullptr,
                false,
            },
        };

        WH_HOOK_SYMBOLS_OPTIONS options{};
        options.optionsSize = sizeof(options);
        options.noUndecoratedSymbols = TRUE;

        if (!WindhawkUtils::HookSymbols(shell32, shell32Symbols,
                                        ARRAYSIZE(shell32Symbols), &options) ||
            !targets->createTileElement || !targets->progressPositionProp ||
            !targets->getProgressHWND || !targets->onPropertyChanged ||
            !targets->operationTileDestructor ||
            !targets->updateRemainingItemsAndSize || !targets->updateSummary ||
            !targets->setTileDisplayMode || !targets->calculateRate)
        {
            Wh_Log(L"Skin setup failed: unable to resolve exact "
                   L"file-operation symbols CreateTile=%p ProgressProp=%p "
                   L"GetProgressHWND=%p OnPropertyChanged=%p Destructor=%p",
                   reinterpret_cast<void *>(targets->createTileElement),
                   reinterpret_cast<void *>(targets->progressPositionProp),
                   reinterpret_cast<void *>(targets->getProgressHWND),
                   reinterpret_cast<void *>(targets->onPropertyChanged),
                   reinterpret_cast<void *>(targets->operationTileDestructor));
            return false;
        }

        return true;
    }

    bool InstallSkinHooks(SkinTargets const &targets)
    {
        StrToID_Original = targets.strToID;
        Element_FindDescendent_Original = targets.findDescendent;
        Element_GetParent_Original = targets.getParent;
        Element_SetBackgroundColor_Original = targets.setBackgroundColor;
        Element_SetForegroundColor_Original = targets.setForegroundColor;
        Element_SetFontFace_Original = targets.setFontFace;
        Element_SetFontSize_Original = targets.setFontSize;
        Element_SetFontWeight_Original = targets.setFontWeight;
        Element_SetRelPixWidth_Original = targets.setRelPixWidth;
        Element_SetMargin_Original = targets.setMargin;
        Element_SetPadding_Original = targets.setPadding;
        Element_SetBorderColor_Original = targets.setBorderColor;
        Element_SetBorderThickness_Original = targets.setBorderThickness;
        Element_SetVisible_Original = targets.setVisible;
        Element_GetVisible_Original = targets.getVisible;
        Element_SetRelPixHeight_Original = targets.setRelPixHeight;
        Element_SetContentString_Original = targets.setContentString;
        Element_GetRootRelativeBounds_Original =
            targets.getRootRelativeBounds;
        OperationTileElement_ProgressPositionProp_Original =
            targets.progressPositionProp;
        OperationTileElement_GetProgressHWND_Original =
            targets.getProgressHWND;

        if (!WindhawkUtils::SetFunctionHook(
                targets.parserCreate, DUIXmlParser_CreateElement_Hook,
                &DUIXmlParser_CreateElement_Original))
        {
            Wh_Log(L"Skin setup failed: unable to hook "
                   L"dui70!DirectUI::DUIXmlParser::CreateElement");
            return false;
        }

        if (!WindhawkUtils::SetFunctionHook(
                targets.createTileElement,
                COperationStatusTile_CreateTileElement_Hook,
                &COperationStatusTile_CreateTileElement_Original))
        {
            Wh_Log(L"Skin setup failed: unable to hook "
                   L"shell32!COperationStatusTile::_CreateTileElement");
            return false;
        }

        if (!WindhawkUtils::SetFunctionHook(
                targets.onPropertyChanged,
                OperationTileElement_OnPropertyChanged_Hook,
                &OperationTileElement_OnPropertyChanged_Original))
        {
            Wh_Log(L"Skin setup failed: unable to hook "
                   L"shell32!OperationTileElement::OnPropertyChanged");
            return false;
        }

        if (!WindhawkUtils::SetFunctionHook(
                targets.operationTileDestructor,
                OperationTileElement_Destructor_Hook,
                &OperationTileElement_Destructor_Original))
        {
            Wh_Log(L"Skin setup failed: unable to hook "
                   L"shell32!OperationTileElement destructor");
            return false;
        }

        if (!WindhawkUtils::SetFunctionHook(
                targets.updateRemainingItemsAndSize,
                COperationStatusTile_UpdateRemainingItemsAndSize_Hook,
                &COperationStatusTile_UpdateRemainingItemsAndSize_Original))
        {
            Wh_Log(L"Skin setup failed: unable to hook native byte update");
            return false;
        }

        if (!WindhawkUtils::SetFunctionHook(
                targets.updateSummary,
                COperationStatusTile_UpdateSummary_Hook,
                &COperationStatusTile_UpdateSummary_Original))
        {
            Wh_Log(L"Skin setup failed: unable to hook native summary update");
            return false;
        }

        if (!WindhawkUtils::SetFunctionHook(
                targets.setTileDisplayMode,
                COperationStatusTile_SetTileDisplayMode_Hook,
                &COperationStatusTile_SetTileDisplayMode_Original))
        {
            Wh_Log(L"Skin setup failed: unable to hook native display mode");
            return false;
        }

        if (!WindhawkUtils::SetFunctionHook(
                targets.calculateRate,
                COperationStatusTileRateCalculator_CalculateRate_Hook,
                &COperationStatusTileRateCalculator_CalculateRate_Original))
        {
            Wh_Log(L"Skin setup failed: unable to hook native rate update");
            return false;
        }

        return true;
    }

} // namespace

BOOL Wh_ModInit()
{
    Wh_Log(L"File Operation Styler 0.10.41 initialization started");

    if (!InitializeProgressCircleUi())
    {
        return FALSE;
    }

    SkinTargets targets{};
    if (!ResolveSkinTargets(&targets) || !InstallSkinHooks(targets))
    {
        ShutdownProgressCircleUi();
        return FALSE;
    }

    Wh_Log(L"File Operation Styler 0.10.41 ready");
    return TRUE;
}

void Wh_ModUninit()
{
    ShutdownProgressCircleUi();
    {
        std::lock_guard<std::mutex> lock(g_transferSummaryMutex);
        g_transferSummaries.clear();
    }
    ClearSkinState();
    Wh_Log(L"File Operation Styler 0.10.41 uninitialization complete");
}
