// Standalone Win32 checks. WH_EDITING supplies inert Windhawk APIs; no hooks
// are installed and no Explorer process or file operation is touched.
#include "../file-operation-styler.wh.cpp"
#include <cstdio>
#include <cstdlib>

namespace
{
    int checks = 0;
    void Check(bool value, const char *message)
    {
        ++checks;
        if (!value)
        {
            std::fprintf(stderr, "FAIL: %s\n", message);
            std::exit(1);
        }
    }

    HWND __cdecl NoProgressWindow(OperationTileElement *) { return nullptr; }
    ATOM WINAPI NoElement(PCWSTR) { return 0; }
    bool exerciseResizeReentry = false;
    int nestedResizeCalls = 0;
    LRESULT CALLBACK TestHostSubclass(HWND window, UINT message, WPARAM wParam,
                                      LPARAM lParam, UINT_PTR id, DWORD_PTR)
    {
        if (RouteTransferScrollMessage(window, message, wParam, lParam))
            return 0;
        if (exerciseResizeReentry && message == WM_SIZE &&
            GetRegisteredTileCountForHost(window))
        {
            // Simulate Explorer requesting layout synchronously during the
            // SetWindowPos issued by our outer resize.
            ++nestedResizeCalls;
            Check(!ResizeOperationStatusWindowForMode(window, false, 0),
                  "own resize cannot recursively enter custom host sizing");
        }
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(window, TestHostSubclass, id);
        return DefSubclassProc(window, message, wParam, lParam);
    }

    bool failDuringScrollUpdate = false;
    bool retireDuringScrollUpdate = false;
    LRESULT CALLBACK TestScrollBarSubclass(HWND window, UINT message, WPARAM wParam,
                                           LPARAM lParam, UINT_PTR id, DWORD_PTR)
    {
        if (failDuringScrollUpdate && message == WM_WINDOWPOSCHANGING)
        {
            failDuringScrollUpdate = false;
            FailHostTransferViewport(GetParent(window), L"test-reentrant-failure");
        }
        if (retireDuringScrollUpdate && message == WM_WINDOWPOSCHANGING)
        {
            retireDuringScrollUpdate = false;
            ForgetHostTransferViewport(GetParent(window));
        }
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(window, TestScrollBarSubclass, id);
        return DefSubclassProc(window, message, wParam, lParam);
    }

    void CheckGeometry()
    {
        for (UINT dpi : {96U, 144U})
        {
            int barWidth = GetSystemMetricsForDpi(SM_CXVSCROLL, dpi);
            Check(barWidth > 0, "DPI-specific scrollbar width");
            std::printf("DPI %u: scrollbar width %d px\n", dpi, barWidth);
            for (int logicalRow : {148, 226})
            {
                int row = MulDiv(logicalRow, dpi, 96);
                int footer = MulDiv(52, dpi, 96);
                int nonClient = MulDiv(31, dpi, 96);
                for (int count : {1, 2, 4, 6, 10})
                {
                    int desired = count * row + footer;
                    int height = std::min(desired, 1040 - 20 - nonClient);
                    TransferViewport viewport{};
                    Check(CalculateTransferViewport(count, row, MulDiv(570, dpi, 96),
                        height, footer, barWidth, INT_MAX, &viewport), "valid geometry");
                    Check(viewport.rowHeight == row, "row heights never compress");
                    Check(viewport.scrollBarWidth == (height < desired ? barWidth : 0),
                          "scrollbar exists exactly when content overflows");
                    Check(viewport.offset == std::max(count * row + footer - height, 0),
                          "bottom offset reaches final row");
                    Check(viewport.bounds.bottom == height - footer - 1,
                          "footer seam remains outside viewport");
                    Check(count * row - viewport.offset == viewport.bounds.bottom + 1,
                          "last row reaches the existing footer seam");
                    Check(CalculateTransferViewport(count, row, 570, height, footer,
                        barWidth, -10, &viewport) && viewport.offset == 0,
                        "negative offsets clamp to top");
                }
            }
        }
        TransferViewport viewport{};
        Check(!CalculateTransferViewport(0, 148, 570, 400, 52, 17, 0, &viewport),
              "empty hosts have no custom viewport");
        Check(!CalculateTransferViewport(10, 226, 570, 53, 52, 17, 0, &viewport),
              "unusable viewport rejected");
        Check(!CalculateTransferViewport(10, 226, 570, 900, 52, 0, 0, &viewport),
              "missing scrollbar metrics rejected");
        Check(!CalculateTransferViewport(INT_MAX, 226, 570, 900, 52, 17, 0, &viewport),
              "content extent overflow rejected");
    }

    void AddRow(HWND host, unsigned identity)
    {
        auto *tile = reinterpret_cast<OperationTileElement *>(
            static_cast<ULONG_PTR>(identity));
        CircleState circle{};
        circle.tile = tile;
        circle.hostWindow = host;
        circle.circleWindow = CreateWindowExW(0, kCircleWindowClass, nullptr,
            WS_CHILD, 0, 0, 1, 1, host, nullptr, g_circleClassInstance, nullptr);
        circle.infoWindow = CreateWindowExW(0, kInfoPanelWindowClass, nullptr,
            WS_CHILD | WS_CLIPSIBLINGS, 0, 0, 1, 1, host, nullptr,
            g_circleClassInstance, nullptr);
        Check(circle.circleWindow && circle.infoWindow, "create mod-owned row windows");
        g_circles.push_back(circle);
        TransferSummaryState summary{};
        summary.tile = tile;
        summary.displayModeKnown = true;
        summary.expanded = true;
        g_transferSummaries.push_back(summary);
    }

    void CheckRows(HWND host, bool glass)
    {
        TransferViewport viewport{};
        Check(GetTransferViewport(host, &viewport), "host viewport available");
        for (size_t index = 0; index < viewport.tiles.size(); ++index)
        {
            int top = 0, bottom = 0;
            Check(GetMultiTileSlotBounds(viewport.tiles[index], host, &top, &bottom),
                  "every transfer has a mapping");
            Check(top == static_cast<int>(index) * viewport.rowHeight - viewport.offset &&
                  bottom - top == viewport.rowHeight, "shared row mapping");
            auto it = std::find_if(g_circles.begin(), g_circles.end(),
                [&](CircleState const &row) { return row.tile == viewport.tiles[index]; });
            Check(it != g_circles.end() && it->positionY == top +
                  ScaleForDpi(kCircleHostY, viewport.dpi), "circle uses shared row mapping");
            if (glass)
            {
                Check(!(GetWindowLongPtrW(it->infoWindow, GWL_STYLE) & WS_VISIBLE),
                      "Glass keeps information child windows hidden");
            }
            else if (bottom <= 0 || top >= viewport.bounds.bottom)
            {
                Check(!(GetWindowLongPtrW(it->infoWindow, GWL_STYLE) & WS_VISIBLE),
                      "non-Glass offscreen rows hidden");
            }
            else
            {
                RECT screen{}, clip{};
                Check(GetWindowRect(it->infoWindow, &screen), "panel rectangle");
                POINT origin{screen.left, screen.top};
                ScreenToClient(host, &origin);
                Check(origin.y == top, "non-Glass panel uses shared row mapping");
                HRGN region = CreateRectRgn(0, 0, 0, 0);
                Check(GetWindowRgn(it->infoWindow, region) != ERROR, "panel clipping region");
                GetRgnBox(region, &clip);
                DeleteObject(region);
                Check(origin.y + clip.top >= 0 &&
                      origin.y + clip.bottom <= viewport.bounds.bottom,
                      "panel cannot cover footer or escape viewport");
            }
        }
        for (int y = 0; y < viewport.bounds.bottom; ++y)
        {
            int expected = (y + viewport.offset) / viewport.rowHeight;
            int top = 0, bottom = 0;
            Check(expected < static_cast<int>(viewport.tiles.size()) &&
                GetMultiTileSlotBounds(viewport.tiles[expected], host, &top, &bottom) &&
                y >= top && y < bottom, "pointer inverse mapping matches presentation");
        }
    }

    void CheckPaintClipping(HWND host)
    {
        TransferViewport viewport{};
        Check(GetTransferViewport(host, &viewport), "paint viewport");
        RECT client{};
        GetClientRect(host, &client);
        HDC dc = CreateCompatibleDC(nullptr);
        BITMAPINFO info{};
        info.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
        info.bmiHeader.biWidth = client.right;
        info.bmiHeader.biHeight = -client.bottom;
        info.bmiHeader.biPlanes = 1;
        info.bmiHeader.biBitCount = 32;
        void *pixels = nullptr;
        HBITMAP bitmap = CreateDIBSection(dc, &info, DIB_RGB_COLORS, &pixels, nullptr, 0);
        Check(dc && bitmap && pixels, "paint clipping test bitmap");
        HGDIOBJ previous = SelectObject(dc, bitmap);
        auto *bits = static_cast<DWORD *>(pixels);
        constexpr DWORD untouched = 0xFF113355;
        for (int top : {-viewport.rowHeight / 2,
                        static_cast<int>(viewport.bounds.bottom) - viewport.rowHeight / 2})
        {
            std::fill_n(bits, client.right * client.bottom, untouched);
            RECT row{0, top, viewport.bounds.right, top + viewport.rowHeight};
            g_glassTransparentInfoPanelPaint = true;
            DrawInfoPanelFrame(g_circles.front().infoWindow, dc, row, &viewport.bounds);
            g_glassTransparentInfoPanelPaint = false;
            GdiFlush();
            int ink = 0, escaped = 0;
            for (int y = 0; y < client.bottom; ++y)
                for (int x = 0; x < client.right; ++x)
                    if (bits[y * client.right + x] != untouched)
                    {
                        ++ink;
                        if (x >= viewport.bounds.right || y >= viewport.bounds.bottom)
                            ++escaped;
                    }
            Check(ink > 0 && escaped == 0,
                  "Glass row rasterization cannot escape viewport or cover footer/scrollbar");
        }
        SelectObject(dc, previous);
        DeleteObject(bitmap);
        DeleteDC(dc);
    }

    void CheckHost(bool glass, unsigned identityBase)
    {
        // A tool window beyond the virtual desktop exercises real USER32
        // controls without displaying a window or taking user focus.
        HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, L"STATIC", L"",
            WS_OVERLAPPEDWINDOW, GetSystemMetrics(SM_XVIRTUALSCREEN) +
                GetSystemMetrics(SM_CXVIRTUALSCREEN) + 2000, 20, 570, 700,
            nullptr, nullptr, g_circleClassInstance, nullptr);
        Check(host != nullptr, "create isolated test host");
        Check(SetWindowSubclass(host, TestHostSubclass, 1, 0), "test-only scroll routing");
        ShowWindow(host, SW_SHOWNOACTIVATE);
        g_settings.customizationEnabled = true;
        g_settings.preset = glass ? L"glass" : L"blueDark";
        for (unsigned i = 0; i < 10; ++i)
            AddRow(host, identityBase + i);
        RECT current{}, client{};
        GetWindowRect(host, &current);
        GetClientRect(host, &client);
        WINDOWPOS nativeSize{};
        nativeSize.cx = current.right - current.left;
        nativeSize.cy = current.bottom - current.top;
        CaptureHostNativeGeometry(host, nativeSize);
        MONITORINFO monitor{sizeof(monitor)};
        Check(GetMonitorInfoW(MonitorFromWindow(host, MONITOR_DEFAULTTONEAREST),
                              &monitor), "monitor work area");
        for (LONG proposedTop : {monitor.rcWork.top + 20,
                                  monitor.rcWork.bottom - 5, monitor.rcWork.top - 200})
        {
            RECT placement = current;
            OffsetRect(&placement, 0, proposedTop - placement.top);
            int height = 0, clientHeight = 0, top = 0;
            Check(CalculateCustomHostWindowHeight(host, true, &height,
                &clientHeight, &placement, &top), "calculate capped host height");
            Check(top >= monitor.rcWork.top && top + height <= monitor.rcWork.bottom,
                  "host fits monitor work area at all tested positions");
            Check(height - clientHeight == nativeSize.cy - client.bottom,
                  "non-client dimensions included in height cap");
        }
        PositionProgressCirclesForHost(host, L"test-initial");
        Check(!ReadHostScrollState(host).failed, "viewport initialized");
        auto state = ReadHostScrollState(host);
        Check(state.scrollBar && IsWindowVisible(state.scrollBar), "overflow scrollbar visible");
        SendMessageW(state.scrollBar, WM_KEYDOWN, VK_DOWN, 0);
        Check(ReadHostScrollState(host).offset > 0, "standard scrollbar keyboard navigation");
        RouteTransferScrollMessage(host, WM_VSCROLL, SB_TOP,
            reinterpret_cast<LPARAM>(state.scrollBar));
        CheckRows(host, glass);

        // A wheel/scrollbar event can precede the posted DPI layout refresh.
        {
            std::lock_guard<std::mutex> lock(g_hostScrollMutex);
            for (auto &entry : g_hostScrollStates)
                if (entry.hostWindow == host)
                {
                    entry.dpi = GetDpiForWindow(host) * 3 / 2;
                    entry.offset = 300;
                }
        }
        TransferViewport beforeDpiInput{};
        Check(GetTransferViewport(host, &beforeDpiInput), "DPI input viewport");
        int expectedOffset = std::min<int>(
            beforeDpiInput.offset + ScaleForDpi(24, beforeDpiInput.dpi),
            beforeDpiInput.contentHeight - beforeDpiInput.bounds.bottom);
        Check(RouteTransferScrollMessage(host, WM_VSCROLL, SB_LINEDOWN,
            reinterpret_cast<LPARAM>(state.scrollBar)), "input before DPI refresh");
        TransferViewport afterDpiInput{};
        Check(GetTransferViewport(host, &afterDpiInput) &&
              afterDpiInput.offset == expectedOffset &&
              ReadHostScrollState(host).dpi == afterDpiInput.dpi,
              "input offset is converted exactly once across DPI changes");
        RouteTransferScrollMessage(host, WM_VSCROLL, SB_TOP,
            reinterpret_cast<LPARAM>(state.scrollBar));
        if (glass)
            CheckPaintClipping(host);

        // Synthetic wheel input exercises accumulation and modifier handling.
        POINT wheelPoint{10, 10};
        ClientToScreen(host, &wheelPoint);
        LPARAM wheelPosition = MAKELPARAM(wheelPoint.x, wheelPoint.y);
        Check(RouteTransferScrollMessage(host, WM_MOUSEWHEEL,
            MAKEWPARAM(0, -60), wheelPosition), "high-resolution wheel input handled");
        Check(ReadHostScrollState(host).wheelRemainder == -60,
              "partial wheel delta retained per host");
        Check(RouteTransferScrollMessage(host, WM_MOUSEWHEEL,
            MAKEWPARAM(0, -60), wheelPosition), "second partial wheel delta handled");
        UINT wheelLines = 3;
        SystemParametersInfoW(SPI_GETWHEELSCROLLLINES, 0, &wheelLines, 0);
        Check(ReadHostScrollState(host).wheelRemainder == 0 &&
              (wheelLines == 0 || ReadHostScrollState(host).offset > 0),
              "wheel detent completed");
        Check(!RouteTransferScrollMessage(host, WM_MOUSEWHEEL,
            MAKEWPARAM(MK_CONTROL, 120), wheelPosition), "modified wheel input left to Explorer");
        Check(!RouteTransferScrollMessage(host, WM_VSCROLL, SB_BOTTOM, 0),
              "native Explorer scrollbar messages not consumed");
        RECT footerBefore{}, footerAfter{};
        GetWindowRect(GetFooterOverlayWindow(host), &footerBefore);
        Check(RouteTransferScrollMessage(host, WM_VSCROLL, SB_BOTTOM,
            reinterpret_cast<LPARAM>(state.scrollBar)), "own scrollbar input handled");
        Check(ReadHostScrollState(host).offset > 0, "scroll reaches later transfers");
        GetWindowRect(GetFooterOverlayWindow(host), &footerAfter);
        Check(EqualRect(&footerBefore, &footerAfter), "footer stays fixed while scrolling");
        CheckRows(host, glass);

        // Addition and removal both happen while the host is scrolled.
        TransferViewport orderedRows{};
        Check(GetTransferViewport(host, &orderedRows), "capture original tile order");
        auto originalOrder = orderedRows.tiles;
        AddRow(host, identityBase + 10);
        PositionProgressCirclesForHost(host, L"test-add");
        Check(GetTransferViewport(host, &orderedRows) &&
              orderedRows.tiles.size() == originalOrder.size() + 1 &&
              std::equal(originalOrder.begin(), originalOrder.end(), orderedRows.tiles.begin()),
              "adding a transfer preserves every existing row's order");
        CheckRows(host, glass);
        for (auto &summary : g_transferSummaries)
            summary.expanded = false;
        exerciseResizeReentry = true;
        nestedResizeCalls = 0;
        Check(ResizeOperationStatusWindowForMode(host, false, 0), "compact mode resize");
        exerciseResizeReentry = false;
        Check(nestedResizeCalls > 0, "synchronous resize callbacks exercised");
        PositionProgressCirclesForHost(host, L"test-compact");
        CheckRows(host, glass);
        for (auto &summary : g_transferSummaries)
            summary.expanded = true;
        Check(ResizeOperationStatusWindowForMode(host, true, 0), "expanded mode resize");
        PositionProgressCirclesForHost(host, L"test-expand");
        CheckRows(host, glass);
        RemoveTransferSummary(reinterpret_cast<OperationTileElement *>(
            static_cast<ULONG_PTR>(identityBase)));
        DestroyProgressCircle(reinterpret_cast<OperationTileElement *>(
            static_cast<ULONG_PTR>(identityBase)));
        PositionProgressCirclesForHost(host, L"test-remove-leading");
        Check(GetTransferViewport(host, &orderedRows) &&
              orderedRows.tiles.size() == originalOrder.size() &&
              std::equal(originalOrder.begin() + 1, originalOrder.end(), orderedRows.tiles.begin()),
              "removing a leading transfer preserves surviving row order");
        CheckRows(host, glass);

        while (GetRegisteredTileCountForHost(host) > 2)
        {
            auto *tile = g_circles.front().tile;
            RemoveTransferSummary(tile);
            DestroyProgressCircle(tile);
        }
        PositionProgressCirclesForHost(host, L"test-shrink");
        Check(ReadHostScrollState(host).offset == 0, "shrinking content resets offset");
        Check(!IsWindowVisible(ReadHostScrollState(host).scrollBar), "fitting content hides scrollbar");

        // Simulate loss of the required control: custom content must give way
        // to the native UI rather than remain clipped and inaccessible.
        for (unsigned i = 11; i < 20; ++i)
            AddRow(host, identityBase + i);
        PositionProgressCirclesForHost(host, L"test-grow");
        state = ReadHostScrollState(host);
        Check(IsWindow(state.scrollBar), "scrollbar retained for renewed overflow");
        if (glass)
        {
            Check(SetWindowSubclass(state.scrollBar, TestScrollBarSubclass, 1, 0),
                  "test-only reentrant failure callback");
            failDuringScrollUpdate = true;
        }
        else
        {
            DestroyWindow(state.scrollBar);
        }
        Check(!UpdateHostTransferViewport(host) && ReadHostScrollState(host).failed,
              "lost scrollbar or reentrant failure cannot publish a ready viewport");
        Check(ReadHostScrollState(host).offset == 0 &&
              !IsWindowVisible(ReadHostScrollState(host).scrollBar),
              "native fallback retains reset offset and hidden scrollbar");
        for (auto const &row : g_circles)
            Check(!(GetWindowLongPtrW(row.infoWindow, GWL_STYLE) & WS_VISIBLE),
                  "fallback hides every custom row");
        while (GetRegisteredTileCountForHost(host))
        {
            auto *tile = g_circles.front().tile;
            RemoveTransferSummary(tile);
            DestroyProgressCircle(tile);
        }
        Check(!ReadHostScrollState(host).scrollBar, "last transfer releases scrollbar");
        Check(!GetFooterOverlayWindow(host), "last transfer releases footer");
        Check(DestroyProgressCirclesForHost(host), "host cleanup succeeds");
        ForgetHostTransferViewport(host);
        ForgetHostNativeGeometry(host);
        DestroyWindow(host);
    }

    void CheckIndependentHostsAndCompletion()
    {
        HWND hosts[2]{};
        g_settings.preset = L"blueDark";
        for (int h = 0; h < 2; ++h)
        {
            hosts[h] = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
                L"STATIC", L"", WS_OVERLAPPEDWINDOW,
                GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 2000,
                20, 570, 700, nullptr, nullptr, g_circleClassInstance, nullptr);
            Check(hosts[h] != nullptr, "create second independent host");
            ShowWindow(hosts[h], SW_SHOWNOACTIVATE);
            for (unsigned i = 0; i < 10; ++i)
                AddRow(hosts[h], 300 + h * 100 + i);
            PositionProgressCirclesForHost(hosts[h], L"test-independent");
        }
        auto state = ReadHostScrollState(hosts[0]);
        Check(RouteTransferScrollMessage(hosts[0], WM_VSCROLL, SB_BOTTOM,
            reinterpret_cast<LPARAM>(state.scrollBar)), "scroll first independent host");
        Check(ReadHostScrollState(hosts[0]).offset > 0 &&
              ReadHostScrollState(hosts[1]).offset == 0,
              "two active hosts never share offsets");
        for (int h = 0; h < 2; ++h)
        {
            if (h == 1)
            {
                auto retiringState = ReadHostScrollState(hosts[h]);
                Check(SetWindowSubclass(retiringState.scrollBar, TestScrollBarSubclass, 1, 0),
                      "test-only registry retirement callback");
                retireDuringScrollUpdate = true;
                Check(!UpdateHostTransferViewport(hosts[h]) &&
                      !ReadHostScrollState(hosts[h]).scrollBar &&
                      !IsWindow(retiringState.scrollBar),
                      "retired host update cannot leave an orphan scrollbar or recreate state");
            }
            while (GetRegisteredTileCountForHost(hosts[h]))
            {
                auto it = std::find_if(g_circles.begin(), g_circles.end(),
                    [&](CircleState const &row) { return row.hostWindow == hosts[h]; });
                auto *tile = it->tile;
                RemoveTransferSummary(tile);
                DestroyProgressCircle(tile);
            }
            Check(!ReadHostScrollState(hosts[h]).scrollBar &&
                  !GetFooterOverlayWindow(hosts[h]), "normal completion releases all viewport HWNDs");
            Check(DestroyProgressCirclesForHost(hosts[h]), "independent host cleanup");
            ForgetHostTransferViewport(hosts[h]);
            ForgetHostNativeGeometry(hosts[h]);
            DestroyWindow(hosts[h]);
        }
        Check(g_hostScrollStates.empty() && g_circles.empty(), "no stale host or row state");
    }

    bool clampNativeGeometry = false;
    int nativeGeometryClamps = 0;
    constexpr UINT kTestSettleGeometry = WM_APP + 0x400;
    bool lateGeometryClamp = false;
    bool settleGeometryAsynchronously = false;
    bool geometrySettlePosted = false;
    LRESULT CALLBACK GeometryHostProc(HWND window, UINT message, WPARAM wParam, LPARAM lParam)
    {
        if (clampNativeGeometry && message == WM_WINDOWPOSCHANGING &&
            g_suppressNativeGeometryCaptureHost == window &&
            g_restoringNativeGeometryHost != window)
        {
            // Reproduce the native 26H2 dimensions from the runtime log.
            auto *position = reinterpret_cast<WINDOWPOS *>(lParam);
            position->cx = 463;
            position->cy = 172;
            ++nativeGeometryClamps;
        }
        return DefWindowProcW(window, message, wParam, lParam);
    }
    ATOM WINAPI AnyElementId(PCWSTR) { return 1; }
    DirectUI::Element *__cdecl SameElement(DirectUI::Element *element, unsigned short)
    { return element; }
    DirectUI::Element *__cdecl NoParentElement(DirectUI::Element *) { return nullptr; }
    bool __cdecl VisibleElement(DirectUI::Element *) { return true; }
    HRESULT __cdecl SetVisibleElement(DirectUI::Element *, bool) { return S_OK; }

    LRESULT CALLBACK LateGeometrySubclass(HWND window, UINT message, WPARAM wParam,
                                          LPARAM lParam, UINT_PTR id, DWORD_PTR)
    {
        if (message == kTestSettleGeometry)
        {
            lateGeometryClamp = false;
            RECT current{}, client{};
            GetWindowRect(window, &current);
            GetClientRect(window, &client);
            int height = 0, top = 0;
            Check(CalculateCustomHostWindowHeight(window, false, &height, nullptr,
                                                  nullptr, &top), "asynchronous size target");
            Check(SetWindowPos(window, nullptr, current.left, top,
                ScaleForDpi(570, GetDpiForWindow(window)) +
                    current.right - current.left - client.right,
                height, SWP_NOACTIVATE | SWP_NOZORDER), "queued native geometry completion");
            return 0;
        }
        LRESULT result = DefSubclassProc(window, message, wParam, lParam);
        if (lateGeometryClamp && message == WM_WINDOWPOSCHANGING &&
            g_suppressNativeGeometryCaptureHost == window &&
            g_restoringNativeGeometryHost != window)
        {
            // A later native/subclass stage can still defeat our correction.
            // Exercise bounded deferred verification rather than assuming
            // every successful SetWindowPos has already settled.
            auto *position = reinterpret_cast<WINDOWPOS *>(lParam);
            position->cx = 463;
            position->cy = 172;
            if (settleGeometryAsynchronously && !geometrySettlePosted)
            {
                geometrySettlePosted = true;
                Check(PostMessageW(window, kTestSettleGeometry, 0, 0),
                      "queue asynchronous native resize");
            }
        }
        if (message == WM_NCDESTROY)
            RemoveWindowSubclass(window, LateGeometrySubclass, id);
        return result;
    }

    void CheckNativeGeometryRewrite()
    {
        WNDCLASSW wc{};
        wc.hInstance = g_circleClassInstance;
        wc.lpfnWndProc = GeometryHostProc;
        wc.lpszClassName = L"FileOperationStyler.GeometryRegressionTest";
        Check(RegisterClassW(&wc) != 0, "register geometry regression host");
        for (int scenario : {0, 1, 2})
        {
            HWND host = CreateWindowExW(WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE, wc.lpszClassName,
                L"57% complete", WS_OVERLAPPEDWINDOW,
                GetSystemMetrics(SM_XVIRTUALSCREEN) + GetSystemMetrics(SM_CXVIRTUALSCREEN) + 2000,
                20, 463, 172, nullptr, nullptr, g_circleClassInstance, nullptr);
            Check(host != nullptr, "create logged native-size host");
            RECT nativeClient{};
            Check(GetClientRect(host, &nativeClient), "capture initial native client size");
            if (GetDpiForWindow(host) == 96)
                Check(nativeClient.right == 447 && nativeClient.bottom == 133,
                      "96-DPI regression uses the exact logged 447x133 native client");
            AddRow(host, 1000);
            auto &summary = g_transferSummaries.back();
            summary.expanded = false;
            summary.deleteLikeKnown = true;
            summary.operationTileRoot = reinterpret_cast<DirectUI::Element *>(summary.tile);
            summary.tileHeaderRoot = summary.operationTileRoot;
            StrToID_Original = AnyElementId;
            Element_FindDescendent_Original = SameElement;
            Element_GetParent_Original = NoParentElement;
            Element_GetVisible_Original = VisibleElement;
            Element_SetVisible_Original = SetVisibleElement;
            Check(SetWindowSubclass(host, OperationStatusWindowSubclassProc,
                kHostWindowSubclassId, 0), "exercise production host subclass");
            WINDOWPOS nativeSize{};
            nativeSize.cx = 463;
            nativeSize.cy = 172;
            CaptureHostNativeGeometry(host, nativeSize);
            lateGeometryClamp = scenario != 0;
            settleGeometryAsynchronously = scenario == 1;
            geometrySettlePosted = false;
            if (scenario != 0)
                Check(SetWindowSubclass(host, LateGeometrySubclass, 42, 0),
                      "exercise late native geometry stage");
            clampNativeGeometry = true;
            nativeGeometryClamps = 0;
            if (scenario != 0)
                Check(PostMessageW(host, g_positionCirclesMessage, 0, 0),
                      "older layout message cannot prematurely verify a newer geometry request");
            bool resized = ResizeOperationStatusWindowForMode(host, false, 0);
            RECT actual{};
            GetClientRect(host, &actual);
            Check(nativeGeometryClamps > 0, "native WINDOWPOS rewrite was exercised");
            if (scenario == 0)
            {
                Check(resized && !ReadHostScrollState(host).failed &&
                      actual.right == ScaleForDpi(570, GetDpiForWindow(host)) &&
                      actual.bottom == ScaleForDpi(200, GetDpiForWindow(host)),
                      "single transfer survives native geometry rewrite and retains styled size");
                Check(std::none_of(g_hostScrollStates.begin(), g_hostScrollStates.end(),
                    [host](HostScrollState const &state) { return state.hostWindow == host; }),
                      "fitting single transfer needs no scroll state initialization");
                PositionProgressCirclesForHost(host, L"test-single-fit");
                Check(GetWindowLongPtrW(g_circles.front().infoWindow, GWL_STYLE) & WS_VISIBLE,
                      "single transfer styled panel is shown");
            }
            else
            {
                Check(!resized && !ReadHostScrollState(host).failed &&
                      ReadHostScrollState(host).geometryPending,
                      "immediate native dimensions defer verification without permanent fallback");
                int nativeAttempts = nativeGeometryClamps;
                MSG message{};
                int dispatched = 0;
                while (PeekMessageW(&message, host, 0, 0, PM_REMOVE))
                {
                    Check(++dispatched < 32, "deferred geometry verification is bounded");
                    DispatchMessageW(&message);
                }
                Check(nativeGeometryClamps == nativeAttempts,
                      "verification never starts a recursive or repeated resize");
                auto completed = ReadHostScrollState(host);
                Check(!completed.geometryPending, "deferred geometry request completed");
                Check(!completed.scrollBar, "fitting single transfer never initializes a scrollbar");
                if (scenario == 1)
                {
                    GetClientRect(host, &actual);
                    Check(!completed.failed && actual.right == ScaleForDpi(570, GetDpiForWindow(host)) &&
                        actual.bottom == ScaleForDpi(200, GetDpiForWindow(host)),
                        "queued native size completion restores the styled single-transfer size");
                    Check(GetWindowLongPtrW(g_circles.front().infoWindow, GWL_STYLE) & WS_VISIBLE,
                          "asynchronous completion shows the styled panel");
                }
                else
                {
                    Check(completed.failed &&
                        !(GetWindowLongPtrW(g_circles.front().infoWindow, GWL_STYLE) & WS_VISIBLE),
                        "persistent mismatch restores native presentation and hides custom panels");
                    GetClientRect(host, &actual);
                    Check(actual.right == nativeClient.right && actual.bottom == nativeClient.bottom,
                          "persistent failure restores the captured logged native dimensions");
                }
            }
            clampNativeGeometry = false;
            RemoveTransferSummary(g_circles.front().tile);
            DestroyWindow(host);
            Check(g_circles.empty() && g_transferSummaries.empty(),
                  "geometry regression host releases all tile registrations");
            StrToID_Original = NoElement;
            Element_FindDescendent_Original = nullptr;
            Element_GetParent_Original = nullptr;
            Element_GetVisible_Original = nullptr;
            Element_SetVisible_Original = nullptr;
        }
        Check(UnregisterClassW(wc.lpszClassName, g_circleClassInstance),
              "release geometry regression host class");
    }
}

int main()
{
    SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);
    Check(InitializeProgressCircleUi(), "initialize custom window classes");
    OperationTileElement_GetProgressHWND_Original = NoProgressWindow;
    StrToID_Original = NoElement;
    CheckGeometry();
    CheckHost(false, 100);
    CheckHost(true, 200);
    CheckIndependentHostsAndCompletion();
    CheckNativeGeometryRewrite();
    ShutdownProgressCircleUi();
    ShutdownDwmApi();
    std::printf("PASS: %d viewport checks\n", checks);
}
