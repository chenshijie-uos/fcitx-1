/***************************************************************************
 *   Copyright (C) 2002~2005 by Yuking                                     *
 *   yuking_net@sohu.com                                                   *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA.              *
 ***************************************************************************/

#include <X11/Xutil.h>
#include <X11/extensions/Xrender.h>
#include <X11/extensions/shape.h>
#include <X11/Xatom.h>
#include <unistd.h>
#include <cairo.h>
#include <limits.h>
#include <libintl.h>
#include <errno.h>


#include "fcitx/fcitx.h"
#include "fcitx/ui.h"
#include "fcitx/module.h"

#include "classicui.h"
#include "classicuiinterface.h"
#include "fcitx-config/xdg.h"
#include "fcitx-utils/log.h"
#include "fcitx/instance.h"
#include "fcitx/frontend.h"
#include "InputWindow.h"
#include "MainWindow.h"
#include "TrayWindow.h"
#include "MenuWindow.h"
#include "fcitx/hook.h"
#include "fcitx-utils/utils.h"
#include "module/notificationitem/fcitx-notificationitem.h"

DBusHandlerResult ClassicuiDBusFilter(DBusConnection* connection, DBusMessage* msg, void* user_data);

struct _FcitxSkin;
static boolean MainMenuAction(FcitxUIMenu* menu, int index);
static void UpdateMainMenu(FcitxUIMenu* menu);

static void* ClassicUICreate(FcitxInstance* instance);
static void ClassicUICloseInputWindow(void* arg);
static void ClassicUIShowInputWindow(void* arg);
static void ClassicUIMoveInputWindow(void* arg);
static void ClassicUIRegisterMenu(void *arg, FcitxUIMenu* menu);
static void ClassicUIUnRegisterMenu(void *arg, FcitxUIMenu* menu);
static void ClassicUIUpdateStatus(void *arg, FcitxUIStatus* status);
static void ClassicUIRegisterStatus(void *arg, FcitxUIStatus* status);
static void ClassicUIUpdateComplexStatus(void *arg, FcitxUIComplexStatus* status);
static void ClassicUIRegisterComplexStatus(void *arg, FcitxUIComplexStatus* status);
static void ClassicUIOnInputFocus(void *arg);
static void ClassicUIOnInputUnFocus(void *arg);
static void ClassicUIOnTriggerOn(void *arg);
static void ClassicUIOnTriggerOff(void *arg);
static void ClassicUIInputReset(void *arg);
static void ReloadConfigClassicUI(void *arg);
static void ClassicUISuspend(void *arg);
static void ClassicUIResume(void *arg);
static void ClassicUIDelayedInitTray(void* arg);
static void ClassicUIDelayedShowTray(void* arg);
static void ClassicUINotificationItemAvailable(void* arg, boolean avaiable);

static FcitxConfigFileDesc* GetClassicUIDesc();
static void ClassicUIMainWindowSizeHint(void *arg, int* x, int* y,
                                        int* w, int* h);

DECLARE_ADDFUNCTIONS(ClassicUI)

FCITX_DEFINE_PLUGIN(fcitx_classic_ui, ui, FcitxUI) = {
    ClassicUICreate,
    ClassicUICloseInputWindow,
    ClassicUIShowInputWindow,
    ClassicUIMoveInputWindow,
    ClassicUIUpdateStatus,
    ClassicUIRegisterStatus,
    ClassicUIRegisterMenu,
    ClassicUIOnInputFocus,
    ClassicUIOnInputUnFocus,
    ClassicUIOnTriggerOn,
    ClassicUIOnTriggerOff,
    NULL,
    ClassicUIMainWindowSizeHint,
    ReloadConfigClassicUI,
    ClassicUISuspend,
    ClassicUIResume,
    NULL,
    ClassicUIRegisterComplexStatus,
    ClassicUIUpdateComplexStatus,
    ClassicUIUnRegisterMenu,
};

void* ClassicUICreate(FcitxInstance* instance)
{
    FcitxAddon *classicuiaddon = Fcitx_ClassicUI_GetAddon(instance);
    FcitxClassicUI *classicui = fcitx_utils_new(FcitxClassicUI);
    classicui->owner = instance;
    if (!LoadClassicUIConfig(classicui)) {
        free(classicui);
        return NULL;
    }
    if (GetSkinDesc() == NULL) {
        free(classicui);
        return NULL;
    }
    classicui->dpy = FcitxX11GetDisplay(instance);
    if (classicui->dpy == NULL) {
        free(classicui);
        return NULL;
    }

    FcitxX11GetDPI(instance, &classicui->dpi, NULL);
    if (classicui->dpi <= 0)
        classicui->dpi = 96;

    int dummy1 = 0, dummy2 = 0, major, minor;
    if (XShapeQueryExtension(classicui->dpy, &dummy1, &dummy2) == True &&
        XShapeQueryVersion(classicui->dpy, &major, &minor)) {
        if (major > 1 || (major == 1 && minor >= 1)) {
            classicui->hasXShape = true;
        }
    }

    if (LoadSkinConfig(&classicui->skin, &classicui->skinType, /*fallback=*/true)) {
        free(classicui);
        return NULL;
    }

    classicui->isfallback = FcitxUIIsFallback(instance, classicuiaddon);

    classicui->iScreen = DefaultScreen(classicui->dpy);

    classicui->protocolAtom = XInternAtom(classicui->dpy, "WM_PROTOCOLS", False);
    classicui->killAtom = XInternAtom(classicui->dpy, "WM_DELETE_WINDOW", False);


    InitSkinMenu(classicui);
    FcitxUIRegisterMenu(instance, &classicui->skinMenu);
    /* Main Menu Initial */
    FcitxMenuInit(&classicui->mainMenu);
    classicui->mainMenu.UpdateMenu = UpdateMainMenu;
    classicui->mainMenu.MenuAction = MainMenuAction;
    classicui->mainMenu.priv = classicui;
    classicui->mainMenu.mark = -1;

    classicui->inputWindow = InputWindowCreate(classicui);
    classicui->mainWindow = MainWindowCreate(classicui);
    classicui->trayWindow = TrayWindowCreate(classicui);
    classicui->mainMenuWindow = MainMenuWindowCreate(classicui);

    FcitxIMEventHook resethk;
    resethk.arg = classicui;
    resethk.func = ClassicUIInputReset;
    FcitxInstanceRegisterResetInputHook(instance, resethk);

    DisplaySkin(classicui, classicui->skinType);

    FcitxClassicUIAddFunctions(instance);

    classicui->waitDelayed = FcitxInstanceAddTimeout(instance, 0, ClassicUIDelayedInitTray, classicui);

    /* 锁屏状态下不显示状态栏 by UT000591 for TaskID 30163 */
    classicui->conn = FcitxDBusGetConnection(instance);
    do {
        if (NULL != classicui->conn){
            DBusError err;
            dbus_error_init(&err);

            dbus_bus_add_match(classicui->conn, "type='signal',sender='com.deepin.dde.lockFront',interface='com.deepin.dde.lockFront'", &err);
            dbus_connection_flush(classicui->conn);
            if (dbus_error_is_set(&err)) {
                FcitxLog(ERROR, "Match Error (%s)", err.message);
                break;
            }
            
            if (!dbus_connection_add_filter(classicui->conn, ClassicuiDBusFilter, classicui, NULL)) {
                FcitxLog(ERROR, "No memory");
                break;
            }
            dbus_error_free(&err);
        }
    }while(FALSE);

    return classicui;
}

void ClassicUIDelayedInitTray(void* arg) {
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    // FcitxLog(INFO, "yeah we delayed!");
    if (!classicui->bUseTrayIcon || classicui->isSuspend)
        return;
    /*
     * if this return false, something wrong happened and callback
     * will never be called, show tray directly
     */
    if (FcitxNotificationItemEnable(classicui->owner, ClassicUINotificationItemAvailable, classicui)) {
        if (!classicui->trayTimeout)
            classicui->trayTimeout = FcitxInstanceAddTimeout(classicui->owner, 100, ClassicUIDelayedShowTray, classicui);
    } else {
        TrayWindowRelease(classicui->trayWindow);
        TrayWindowInit(classicui->trayWindow);
    }
}

void ClassicUIDelayedShowTray(void* arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    classicui->trayTimeout = 0;
    if (!classicui->bUseTrayIcon || classicui->isSuspend)
        return;

    if (!classicui->trayWindow->bTrayMapped) {
        TrayWindowRelease(classicui->trayWindow);
        TrayWindowInit(classicui->trayWindow);
    }
}

void ClassicUISetWindowProperty(FcitxClassicUI* classicui, Window window, FcitxXWindowType type, char *windowTitle)
{
    FcitxX11SetWindowProp(classicui->owner, &window, &type, windowTitle);
}

static void ClassicUIInputReset(void *arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    if (classicui->isSuspend)
        return;
    MainWindowShow(classicui->mainWindow);
    TrayWindowDraw(classicui->trayWindow);
    classicui->inputWindow->highlight = 0;
}

static void ClassicUICloseInputWindow(void *arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    InputWindowClose(classicui->inputWindow);
}

static void ClassicUIShowInputWindow(void *arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    InputWindowShow(classicui->inputWindow);
}

static void ClassicUIMoveInputWindow(void *arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    classicui->inputWindow->parent.MoveWindow(&classicui->inputWindow->parent);
}

static void ClassicUIUpdateStatus(void *arg, FcitxUIStatus* status)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    MainWindowShow(classicui->mainWindow);
}

void ClassicUIUpdateComplexStatus(void* arg, FcitxUIComplexStatus* status)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    MainWindowShow(classicui->mainWindow);
}

void ClassicUIRegisterComplexStatus(void* arg, FcitxUIComplexStatus* status)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    status->uipriv[classicui->isfallback] = fcitx_utils_malloc0(sizeof(FcitxClassicUIStatus));
}


static void ClassicUIRegisterMenu(void *arg, FcitxUIMenu* menu)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    XlibMenu* xlibMenu = XlibMenuCreate(classicui);
    menu->uipriv[classicui->isfallback] = xlibMenu;
    xlibMenu->menushell = menu;
}

static void ClassicUIUnRegisterMenu(void *arg, FcitxUIMenu* menu)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    XlibMenuDestroy((XlibMenu*) menu->uipriv[classicui->isfallback]);
}

static void ClassicUIRegisterStatus(void *arg, FcitxUIStatus* status)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    FcitxSkin* sc = &classicui->skin;
    status->uipriv[classicui->isfallback] = fcitx_utils_new(FcitxClassicUIStatus);
    char *name;

    fcitx_utils_alloc_cat_str(name, status->name, "_active.png");
    LoadImage(sc, name, false);
    free(name);

    fcitx_utils_alloc_cat_str(name, status->name, "_inactive.png");
    LoadImage(sc, name, false);
    free(name);
}

static void ClassicUIOnInputFocus(void *arg)
{
    printf("ClassicUIOnInputFocus\n");
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    if (classicui->isSuspend||classicui->mainMenuWindow->visible)
        return;
    MainWindowShow(classicui->mainWindow);
    TrayWindowDraw(classicui->trayWindow);
}

static void ClassicUIOnInputUnFocus(void *arg)
{
    printf("ClassicUIOnInputUnFocus\n");
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    if (classicui->isSuspend||classicui->mainMenuWindow->visible)
        return;
    MainWindowShow(classicui->mainWindow);
    TrayWindowDraw(classicui->trayWindow);
}

void ClassicUISuspend(void* arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    classicui->isSuspend = true;
    classicui->notificationItemAvailable = false;
    InputWindowClose(classicui->inputWindow);
    MainWindowClose(classicui->mainWindow);
    TrayWindowRelease(classicui->trayWindow);
    /* always call this function will not do anything harm */
    FcitxNotificationItemDisable(classicui->owner);
}

void ClassicUIResume(void* arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    classicui->isSuspend = false;
    ClassicUIDelayedInitTray(classicui);
}

void ClassicUINotificationItemAvailable(void* arg, boolean avaiable) {
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    /* ClassicUISuspend has already done all clean up */
    if (classicui->isSuspend)
        return;
    classicui->notificationItemAvailable = avaiable;
    if (!avaiable) {
        TrayWindowRelease(classicui->trayWindow);
        TrayWindowInit(classicui->trayWindow);
    } else {
        if (classicui->trayTimeout) {
            FcitxInstanceRemoveTimeoutById(classicui->owner, classicui->trayTimeout);
            classicui->trayTimeout = 0;
        }
        TrayWindowRelease(classicui->trayWindow);
    }
}

void ActivateWindow(Display *dpy, int iScreen, Window window)
{
    XEvent ev;

    memset(&ev, 0, sizeof(ev));

    static Atom _NET_ACTIVE_WINDOW;
    if (_NET_ACTIVE_WINDOW == None)
        _NET_ACTIVE_WINDOW = XInternAtom(dpy, "_NET_ACTIVE_WINDOW", False);

    ev.xclient.type = ClientMessage;
    ev.xclient.window = window;
    ev.xclient.message_type = _NET_ACTIVE_WINDOW;
    ev.xclient.format = 32;
    ev.xclient.data.l[0] = 1;
    ev.xclient.data.l[1] = CurrentTime;
    ev.xclient.data.l[2] = 0;

    XSendEvent(dpy, RootWindow(dpy, iScreen), False, SubstructureNotifyMask, &ev);
    XSync(dpy, False);
}

FcitxRect GetScreenGeometry(FcitxClassicUI* classicui, int x, int y)
{
    FcitxRect result = { 0, 0 , 0 , 0 };
    FcitxX11GetScreenGeometry(classicui->owner, &x, &y, &result);
    return result;
}

CONFIG_DESC_DEFINE(GetClassicUIDesc, "fcitx-classic-ui.desc")

boolean LoadClassicUIConfig(FcitxClassicUI* classicui)
{
    FcitxConfigFileDesc* configDesc = GetClassicUIDesc();
    if (configDesc == NULL)
        return false;
    FILE *fp;
    fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-classic-ui.config", "r", NULL);
    if (!fp) {
        if (errno == ENOENT)
            SaveClassicUIConfig(classicui);
    }

    FcitxConfigFile *cfile = FcitxConfigParseConfigFileFp(fp, configDesc);

    FcitxClassicUIConfigBind(classicui, cfile, configDesc);
    FcitxConfigBindSync(&classicui->gconfig);

    if (fp)
        fclose(fp);
    return true;
}

void SaveClassicUIConfig(FcitxClassicUI *classicui)
{
    FcitxConfigFileDesc* configDesc = GetClassicUIDesc();
    FILE *fp = FcitxXDGGetFileUserWithPrefix("conf", "fcitx-classic-ui.config", "w", NULL);
    FcitxConfigSaveConfigFileFp(fp, &classicui->gconfig, configDesc);
    if (fp)
        fclose(fp);
}

boolean IsInRspArea(int x0, int y0, FcitxClassicUIStatus* status)
{
    return FcitxUIIsInBox(x0, y0, status->x, status->y, status->w, status->h);
}

boolean
ClassicUIMouseClick(FcitxClassicUI* classicui, Window window, int *x, int *y)
{
    boolean bMoved = false;
    FcitxX11MouseClick(classicui->owner, &window, x, y, &bMoved);
    return bMoved;
}

void ClassicUIOnTriggerOn(void* arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    MainWindowShow(classicui->mainWindow);
    TrayWindowDraw(classicui->trayWindow);
}

void ClassicUIOnTriggerOff(void* arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    MainWindowShow(classicui->mainWindow);
    TrayWindowDraw(classicui->trayWindow);
}

static void UpdateMainMenu(FcitxUIMenu* menu)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) menu->priv;
    FcitxInstance* instance = classicui->owner;
    FcitxMenuClear(menu);

    FcitxMenuAddMenuItem(menu, _("Online Help"), MENUTYPE_SIMPLE, NULL);
    FcitxMenuAddMenuItem(menu, NULL, MENUTYPE_DIVLINE, NULL);
    boolean flag = false;

    FcitxUIStatus* status;
    UT_array* uistats = FcitxInstanceGetUIStats(instance);
    for (status = (FcitxUIStatus*) utarray_front(uistats);
            status != NULL;
            status = (FcitxUIStatus*) utarray_next(uistats, status)
        ) {
        FcitxClassicUIStatus* privstat =  GetPrivateStatus(status);
        if (privstat == NULL || !status->visible)
            continue;

        flag = true;
        FcitxMenuAddMenuItemWithData(menu, status->shortDescription, MENUTYPE_SIMPLE, NULL, strdup(status->name));
    }

    FcitxUIComplexStatus* compstatus;
    UT_array* uicompstats = FcitxInstanceGetUIComplexStats(instance);
    for (compstatus = (FcitxUIComplexStatus*) utarray_front(uicompstats);
            compstatus != NULL;
            compstatus = (FcitxUIComplexStatus*) utarray_next(uicompstats, compstatus)
        ) {
        FcitxClassicUIStatus* privstat =  GetPrivateStatus(compstatus);
        if (privstat == NULL || !compstatus->visible)
            continue;
        if (FcitxUIGetMenuByStatusName(instance, compstatus->name))
            continue;

        flag = true;
        FcitxMenuAddMenuItemWithData(menu, compstatus->shortDescription, MENUTYPE_SIMPLE, NULL, strdup(compstatus->name));
    }

    if (flag)
        FcitxMenuAddMenuItem(menu, NULL, MENUTYPE_DIVLINE, NULL);

    FcitxUIMenu **menupp;
    UT_array* uimenus = FcitxInstanceGetUIMenus(instance);
    for (menupp = (FcitxUIMenu **) utarray_front(uimenus);
            menupp != NULL;
            menupp = (FcitxUIMenu **) utarray_next(uimenus, menupp)
        ) {
        FcitxUIMenu * menup = *menupp;
        if (menup->isSubMenu)
            continue;

        if (!menup->visible)
            continue;

        if (menup->candStatusBind) {
            FcitxUIComplexStatus* compStatus = FcitxUIGetComplexStatusByName(instance, menup->candStatusBind);
            if (compStatus) {
                if (!compStatus->visible)
                    continue;
            }
        }

        FcitxMenuAddMenuItem(menu, menup->name, MENUTYPE_SUBMENU, menup);
    }
    FcitxMenuAddMenuItem(menu, NULL, MENUTYPE_DIVLINE, NULL);
    FcitxMenuAddMenuItem(menu, _("Configure"), MENUTYPE_SIMPLE, NULL);
    FcitxMenuAddMenuItem(menu, _("Restart"), MENUTYPE_SIMPLE, NULL);
    FcitxMenuAddMenuItem(menu, _("Exit"), MENUTYPE_SIMPLE, NULL);
}

boolean MainMenuAction(FcitxUIMenu* menu, int index)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) menu->priv;
    FcitxInstance* instance = classicui->owner;
    int length = utarray_len(&menu->shell);
    if (index == 0) {
        char* args[] = {
            "xdg-open",
            "http://fcitx-im.org/",
            0
        };
        fcitx_utils_start_process(args);
    } else if (index == length - 1) { /* Exit */
        FcitxInstanceEnd(classicui->owner);
    } else if (index == length - 2) { /* Restart */
        FcitxInstanceRestart(instance);
    } else if (index == length - 3) { /* Configuration */
        fcitx_utils_launch_configure_tool();
    } else {
        FcitxMenuItem* item = (FcitxMenuItem*) utarray_eltptr(&menu->shell, index);
        if (item && item->type == MENUTYPE_SIMPLE && item->data) {
            const char* name = item->data;
            FcitxUIUpdateStatus(instance, name);
        }
    }
    return true;
}

Visual * ClassicUIFindARGBVisual(FcitxClassicUI* classicui)
{
    return FcitxX11FindARGBVisual(classicui->owner);
}

void ClassicUIMainWindowSizeHint(void* arg, int* x, int* y, int* w, int* h)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    if (x) {
        *x = classicui->iMainWindowOffsetX;
    }
    if (y) {
        *y = classicui->iMainWindowOffsetY;
    }

    XWindowAttributes attr;
    XGetWindowAttributes(classicui->dpy, classicui->mainWindow->parent.wId, &attr);
    if (w) {
        *w = attr.width;
    }
    if (h) {
        *h = attr.height;
    }

}

void ReloadConfigClassicUI(void* arg)
{
    FcitxClassicUI* classicui = (FcitxClassicUI*) arg;
    LoadClassicUIConfig(classicui);
    DisplaySkin(classicui, classicui->skinType);
}

boolean WindowIsVisable(Display* dpy, Window window)
{
    XWindowAttributes attr;
    XGetWindowAttributes(dpy, window, &attr);
    return attr.map_state == IsViewable;
}

boolean EnlargeCairoSurface(cairo_surface_t** sur, int w, int h)
{
    int ow = cairo_image_surface_get_width(*sur);
    int oh = cairo_image_surface_get_height(*sur);

    if (ow >= w && oh >= h)
        return false;

    while (ow < w) {
        ow *= 2;
    }

    while (oh < h) {
        oh *= 2;
    }

    cairo_surface_destroy(*sur);
    *sur = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, ow, oh);
    return true;
}

void ResizeSurface(cairo_surface_t** surface, int w, int h)
{
    int ow = cairo_image_surface_get_width(*surface);
    int oh = cairo_image_surface_get_height(*surface);

    if ((ow == w && oh == h) || w == 0 || h == 0 || ow == 0 || oh == 0)
        return;

    double scalex = (double)w / ow;
    double scaley = (double)h / oh;
    double scale = (scalex > scaley) ? scaley : scalex;

    int nw = ow * scale;
    int nh = oh * scale;

    cairo_surface_t* newsurface = cairo_image_surface_create(CAIRO_FORMAT_ARGB32, w, h);
    cairo_t* c = cairo_create(newsurface);
    cairo_set_operator(c, CAIRO_OPERATOR_SOURCE);
    cairo_set_source_rgba(c ,1, 1, 1, 0.0);
    cairo_paint(c);
    cairo_translate(c, (w - nw) / 2.0 , (h - nh) / 2.0);
    cairo_scale(c, scale, scale);
    cairo_set_source_surface(c, *surface, 0, 0);
    cairo_rectangle(c, 0, 0, ow, oh);
    cairo_clip(c);
    cairo_paint(c);
    cairo_destroy(c);

    cairo_surface_destroy(*surface);

    *surface = newsurface;
}

/* 锁屏状态下不显示状态栏 by UT000591 for TaskID 30163 */
DBusHandlerResult ClassicuiDBusFilter(DBusConnection* connection, DBusMessage* msg, void* user_data)
{
    FCITX_UNUSED(connection);
    FcitxClassicUI* classicui = (FcitxClassicUI*) user_data;
    boolean locked = false;
    if (dbus_message_is_signal(msg, "com.deepin.dde.lockFront", "Visible")) {
        DBusError error;
        dbus_error_init(&error);
        dbus_message_get_args(msg, &error, DBUS_TYPE_BOOLEAN, &locked , DBUS_TYPE_INVALID);
        dbus_error_free(&error);

        classicui->mainWindow->isScreenLocked = locked;
        return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
    }
    return DBUS_HANDLER_RESULT_NOT_YET_HANDLED;
}

#include "fcitx-classic-ui-addfunctions.h"
