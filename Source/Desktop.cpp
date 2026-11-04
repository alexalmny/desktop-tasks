#include "Desktop.h"
#include <QGuiApplication>
#include <QWidget>
#include <QWindow>
#ifdef Q_OS_WIN
#include <windows.h>
#else
#include <QtGui/qguiapplication_platform.h>
#include <xcb/xcb.h>
#endif
#ifdef HAVE_LAYER_SHELL
#include <LayerShellQt/Shell>
#include <LayerShellQt/Window>
#endif

void initializeDesktopShell(bool desktop) {
#ifdef HAVE_LAYER_SHELL
    if (desktop && qEnvironmentVariable("XDG_SESSION_TYPE") == "wayland") LayerShellQt::Shell::useLayerShell();
#else
    Q_UNUSED(desktop)
#endif
}
void positionDesktop(QWidget *widget, int x, int y) {
#ifdef HAVE_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith("wayland")) {
        auto *layer = LayerShellQt::Window::get(widget->windowHandle());
        layer->setMargins(QMargins(x, y, 0, 0)); return;
    }
#endif
    widget->move(x, y);
}
#ifdef Q_OS_WIN
static BOOL CALLBACK findWorker(HWND window, LPARAM parameter) {
    if (FindWindowExW(window, nullptr, L"SHELLDLL_DefView", nullptr)) {
        auto *target = reinterpret_cast<HWND *>(parameter);
        *target = FindWindowExW(nullptr, window, L"WorkerW", nullptr);
        return FALSE;
    }
    return TRUE;
}
#endif
bool attachToDesktop(QWidget *widget, bool desktop, int x, int y) {
    if (!desktop) { widget->move(x, y); return true; }
    widget->winId();
#ifdef Q_OS_WIN
    HWND progman = FindWindowW(L"Progman", nullptr);
    if (!progman) return false;
    DWORD_PTR result = 0;
    // shortcut: Explorer's WorkerW is undocumented; use normal-window mode if Windows changes it.
    SendMessageTimeoutW(progman, 0x052C, 0, 0, SMTO_NORMAL, 1000, &result);
    HWND worker = nullptr; EnumWindows(findWorker, reinterpret_cast<LPARAM>(&worker));
    if (!worker) return false;
    HWND handle = reinterpret_cast<HWND>(widget->winId());
    SetLastError(0);
    if (!SetParent(handle, worker) && GetLastError() != 0) return false;
    const auto style = GetWindowLongPtrW(handle, GWL_STYLE);
    SetWindowLongPtrW(handle, GWL_STYLE, (style & ~WS_POPUP) | WS_CHILD);
    SetWindowPos(handle, HWND_BOTTOM, 0, 0, 0, 0, SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_FRAMECHANGED);
    widget->move(x, y); return true;
#else
#ifdef HAVE_LAYER_SHELL
    if (QGuiApplication::platformName().startsWith("wayland")) {
        auto *layer = LayerShellQt::Window::get(widget->windowHandle());
        layer->setLayer(LayerShellQt::Window::LayerBottom);
        layer->setAnchors(LayerShellQt::Window::AnchorTop | LayerShellQt::Window::AnchorLeft);
        layer->setExclusiveZone(-1);
        layer->setKeyboardInteractivity(LayerShellQt::Window::KeyboardInteractivityOnDemand);
        layer->setScope("desktop-tasks");
        positionDesktop(widget, x, y); return true;
    }
#endif
    if (auto *native = qGuiApp->nativeInterface<QNativeInterface::QX11Application>()) {
        auto *connection = native->connection();
        const auto atom = [connection](const char *name) {
            const auto cookie = xcb_intern_atom(connection, 0, uint16_t(strlen(name)), name);
            auto *reply = xcb_intern_atom_reply(connection, cookie, nullptr);
            const xcb_atom_t value = reply ? reply->atom : xcb_atom_t(XCB_ATOM_NONE); free(reply); return value;
        };
        const xcb_atom_t type = atom("_NET_WM_WINDOW_TYPE_DESKTOP");
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, xcb_window_t(widget->winId()), atom("_NET_WM_WINDOW_TYPE"), XCB_ATOM_ATOM, 32, 1, &type);
        const uint32_t all = 0xffffffff;
        xcb_change_property(connection, XCB_PROP_MODE_REPLACE, xcb_window_t(widget->winId()), atom("_NET_WM_DESKTOP"), XCB_ATOM_CARDINAL, 32, 1, &all);
        xcb_flush(connection); widget->move(x, y); return true;
    }
    return false;
#endif
}
