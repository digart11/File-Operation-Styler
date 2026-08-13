// ==WindhawkMod==
// @id              file-operation-styler
// @name            File Operation Styler
// @description     Experimental dark skin for native file-operation tiles.
// @version         0.10.5
// @author          digART
// @license         GPL-3.0
// @include         explorer.exe
// @architecture    x86-64
// @compilerOptions -lcomctl32 -lgdi32 -lgdiplus
// ==/WindhawkMod==

// Experimental first-pass skin for the native DirectUI file-operation tile.
// File-operation behavior and the native progress, chart, and button controls
// remain unchanged.

#include <windhawk_utils.h>

#include <commctrl.h>
#include <gdiplus.h>
#include <windows.h>

#include <algorithm>
#include <atomic>
#include <mutex>
#include <vector>

struct COperationStatusTile;
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

    constexpr COLORREF kBackgroundColor = RGB(24, 24, 24);
    constexpr COLORREF kPrimaryTextColor = RGB(245, 245, 245);
    constexpr COLORREF kSecondaryTextColor = RGB(165, 165, 165);
    constexpr int kRequestedTileWidth = 612;
    constexpr int kReservedLeftWidth = 160;
    constexpr int kContentTopPadding = 8;
    constexpr int kContentRightPadding = 16;
    constexpr int kContentBottomPadding = 8;
    constexpr int kTileVerticalMargin = 4;
    constexpr int kCircleColumnWidth = 160;
    constexpr int kCircleWindowHeight = 144;
    constexpr int kCircleDiameter = 120;
    constexpr int kCircleTop = 8;
    constexpr int kCircleStrokeWidth = 6;
    constexpr COLORREF kInactiveRingColor = RGB(54, 59, 66);
    constexpr COLORREF kAccentRingColor = RGB(16, 144, 226);
    constexpr wchar_t kCircleWindowClass[] =
        L"Windhawk.FileOperationStyler.ProgressCircle.0.10.3";
    constexpr UINT_PTR kHostWindowSubclassId = 0xF0510010;
    constexpr UINT_PTR kProgressWindowSubclassId = 0xF0510011;

    struct CircleState
    {
        OperationTileElement *tile;
        HWND circleWindow;
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

    std::mutex g_circleMutex;
    std::vector<CircleState> g_circles;
    std::vector<HWND> g_subclassedHosts;
    std::vector<HostPositionRequest> g_hostPositionRequests;
    HINSTANCE g_circleClassInstance;
    ATOM g_circleClassAtom;
    ULONG_PTR g_gdiplusToken;
    UINT g_removeHostSubclassMessage;
    UINT g_positionCirclesMessage;

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
    void ScheduleProgressCirclePosition(HWND hostWindow, PCWSTR reason);
    LRESULT CALLBACK NativeProgressWindowSubclassProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR referenceData);

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
            static_cast<Gdiplus::REAL>(ScaleForDpi(28, dpi)),
            Gdiplus::FontStyleRegular, Gdiplus::UnitPixel);
        Gdiplus::Font percentageFallback(
            L"Segoe UI",
            static_cast<Gdiplus::REAL>(ScaleForDpi(28, dpi)),
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

    void ForgetCircleWindow(HWND circleWindow)
    {
        HWND progressWindow = nullptr;
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
            g_circles.erase(it);
        }

        if (progressWindow && IsWindow(progressWindow))
        {
            RemoveWindowSubclass(progressWindow,
                                 NativeProgressWindowSubclassProc,
                                 kProgressWindowSubclassId);
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
            if (state.circleWindow && IsWindow(state.circleWindow))
            {
                DestroyWindow(state.circleWindow);
            }
        }
    }

    LRESULT CALLBACK OperationStatusWindowSubclassProc(
        HWND window,
        UINT message,
        WPARAM wParam,
        LPARAM lParam,
        UINT_PTR subclassId,
        DWORD_PTR)
    {
        if (message == g_removeHostSubclassMessage)
        {
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
            }
            return 0;
        }

        if (message == WM_NCDESTROY)
        {
            DestroyProgressCirclesForHost(window);
            RemoveHostSubclassRecord(window);
            RemoveWindowSubclass(window, OperationStatusWindowSubclassProc,
                                 subclassId);
        }

        LRESULT result = DefSubclassProc(window, message, wParam, lParam);
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

    void DestroyCircleForProgressWindow(HWND progressWindow)
    {
        CircleState removed{};
        bool found = false;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            auto it = std::find_if(
                g_circles.begin(), g_circles.end(),
                [progressWindow](CircleState const &state)
                {
                    return state.progressWindow == progressWindow;
                });
            if (it != g_circles.end())
            {
                removed = *it;
                g_circles.erase(it);
                found = true;
            }
        }

        if (found && removed.circleWindow &&
            IsWindow(removed.circleWindow))
        {
            DestroyWindow(removed.circleWindow);
        }
        if (found && removed.hostWindow && IsWindow(removed.hostWindow))
        {
            ScheduleProgressCirclePosition(removed.hostWindow,
                                           L"tile-removed");
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
        LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (message == WM_NCDESTROY)
        {
            RemoveWindowSubclass(window, NativeProgressWindowSubclassProc,
                                 subclassId);
            DestroyCircleForProgressWindow(window);
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
        RECT tileBounds{};
        HRESULT boundsResult = Element_GetRootRelativeBounds_Original(
            reinterpret_cast<DirectUI::Element *>(state.tile), &tileBounds);
        if (SUCCEEDED(boundsResult) && tileBounds.bottom > tileBounds.top)
        {
            *x = std::max(static_cast<int>(tileBounds.left), 0);
            *y = std::max(static_cast<int>(tileBounds.top) +
                              ScaleForDpi(8, dpi),
                          ScaleForDpi(40, dpi));
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

        if (!placementAvailable || !positionChanged || !circleWindow ||
            !IsWindow(circleWindow))
        {
            return;
        }

        if (!SetWindowPos(circleWindow, HWND_TOP, x, y, width, height,
                          SWP_NOACTIVATE | SWP_NOOWNERZORDER |
                              SWP_SHOWWINDOW))
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
               L"reason=%s",
               reinterpret_cast<void *>(tile), x, y, width, height,
               initialPlacement ? L"initial" : reason);
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
        if (!tile || !g_circleClassAtom)
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
                InvalidateRect(existingCircleWindow, nullptr, FALSE);
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

        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            g_circles.push_back(
                {tile, circleWindow, progressWindow, hostWindow,
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
        Wh_Log(L"eventId=%llu circle created tile=%p host=%p progressHwnd=%p "
               L"hostSubclass=%s progressSubclass=%s result=%s",
               eventId, reinterpret_cast<void *>(tile),
               reinterpret_cast<void *>(hostWindow),
               reinterpret_cast<void *>(progressWindow),
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
        std::vector<HWND> hosts;
        {
            std::lock_guard<std::mutex> lock(g_circleMutex);
            for (auto const &state : g_circles)
            {
                circleWindows.push_back(state.circleWindow);
            }
            hosts = g_subclassedHosts;
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
            return false;
        }
        Wh_Log(L"circle renderer=memory-dc");
        return true;
    }

    void ShutdownProgressCircleUi()
    {
        DestroyAllProgressCircles();
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

    void __cdecl OperationTileElement_Destructor_Hook(
        OperationTileElement *thisPtr)
    {
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

        ApplyBaseLayout(eventId, parentElement, operationTileRoot,
                        state.tileHeaderRoot);

        auto *operationTile =
            reinterpret_cast<OperationTileElement *>(operationTileRoot);
        HWND nativeProgressWindow =
            OperationTileElement_GetProgressHWND_Original(operationTile);
        NativeProgressSnapshot initialProgress =
            ReadNativeProgress(nativeProgressWindow, 0);
        EnsureProgressCircle(operationTile, eventId, initialProgress);

        DirectUI::Element *summary =
            FindSkinElement(operationTileRoot, state.tileHeaderRoot,
                            L"eltSummary", false);
        bool summaryApplied = false;
        bool allFontsApplied = summary != nullptr;
        if (summary)
        {
            HRESULT foregroundResult = Element_SetForegroundColor_Original(
                summary, kAccentRingColor);
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
                Element_SetFontSize_Original(summary, 24);
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
                Element_SetMargin_Original(summary, 0, 6, 0, 6);
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
        Element_SetBackgroundColor_t setBackgroundColor;
        Element_SetForegroundColor_t setForegroundColor;
        Element_SetFontFace_t setFontFace;
        Element_SetFontSize_t setFontSize;
        Element_SetFontWeight_t setFontWeight;
        Element_SetRelPixWidth_t setRelPixWidth;
        Element_SetMargin_t setMargin;
        Element_SetPadding_t setPadding;
        Element_GetRootRelativeBounds_t getRootRelativeBounds;
        COperationStatusTile_CreateTileElement_t createTileElement;
        OperationTileElement_ProgressPositionProp_t progressPositionProp;
        OperationTileElement_GetProgressHWND_t getProgressHWND;
        OperationTileElement_OnPropertyChanged_t onPropertyChanged;
        OperationTileElement_Destructor_t operationTileDestructor;
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
        targets->getRootRelativeBounds =
            reinterpret_cast<Element_GetRootRelativeBounds_t>(
                GetProcAddress(dui70, getRootRelativeBoundsSymbol));

        if (!targets->parserCreate || !targets->strToID ||
            !targets->findDescendent || !targets->setBackgroundColor ||
            !targets->setForegroundColor || !targets->setFontFace ||
            !targets->setFontSize || !targets->setFontWeight ||
            !targets->setRelPixWidth || !targets->setMargin ||
            !targets->setPadding || !targets->getRootRelativeBounds)
        {
            Wh_Log(L"Skin setup failed: required dui70 exports missing "
                   L"ParserCreate=%p StrToID=%p FindDescendent=%p "
                   L"SetBackgroundColor=%p SetForegroundColor=%p "
                   L"SetFontFace=%p SetFontSize=%p SetFontWeight=%p "
                   L"SetRelPixWidth=%p SetMargin=%p SetPadding=%p "
                   L"GetRootRelativeBounds=%p",
                   reinterpret_cast<void *>(targets->parserCreate),
                   reinterpret_cast<void *>(targets->strToID),
                   reinterpret_cast<void *>(targets->findDescendent),
                   reinterpret_cast<void *>(targets->setBackgroundColor),
                   reinterpret_cast<void *>(targets->setForegroundColor),
                   reinterpret_cast<void *>(targets->setFontFace),
                   reinterpret_cast<void *>(targets->setFontSize),
                   reinterpret_cast<void *>(targets->setFontWeight),
                   reinterpret_cast<void *>(targets->setRelPixWidth),
                   reinterpret_cast<void *>(targets->setMargin),
                   reinterpret_cast<void *>(targets->setPadding),
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
        };

        WH_HOOK_SYMBOLS_OPTIONS options{};
        options.optionsSize = sizeof(options);
        options.noUndecoratedSymbols = TRUE;

        if (!WindhawkUtils::HookSymbols(shell32, shell32Symbols,
                                        ARRAYSIZE(shell32Symbols), &options) ||
            !targets->createTileElement || !targets->progressPositionProp ||
            !targets->getProgressHWND || !targets->onPropertyChanged ||
            !targets->operationTileDestructor)
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
        Element_SetBackgroundColor_Original = targets.setBackgroundColor;
        Element_SetForegroundColor_Original = targets.setForegroundColor;
        Element_SetFontFace_Original = targets.setFontFace;
        Element_SetFontSize_Original = targets.setFontSize;
        Element_SetFontWeight_Original = targets.setFontWeight;
        Element_SetRelPixWidth_Original = targets.setRelPixWidth;
        Element_SetMargin_Original = targets.setMargin;
        Element_SetPadding_Original = targets.setPadding;
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

        return true;
    }

} // namespace

BOOL Wh_ModInit()
{
    Wh_Log(L"File Operation Styler 0.10.4 initialization started");

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

    Wh_Log(L"File Operation Styler 0.10.4 ready");
    return TRUE;
}

void Wh_ModUninit()
{
    ShutdownProgressCircleUi();
    ClearSkinState();
    Wh_Log(L"File Operation Styler 0.10.4 uninitialization complete");
}
