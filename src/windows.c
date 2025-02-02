/* -copyright-
#-# 
#-# plasmasnow: Let it snow on your desktop
#-# Copyright (C) 1984,1988,1990,1993-1995,2000-2001 Rick Jansen
#-# 	      2019,2020,2021,2022,2023 Willem Vermin
#-#          2024 Mark Capella
#-# 
#-# This program is free software: you can redistribute it and/or modify
#-# it under the terms of the GNU General Public License as published by
#-# the Free Software Foundation, either version 3 of the License, or
#-# (at your option) any later version.
#-# 
#-# This program is distributed in the hope that it will be useful,
#-# but WITHOUT ANY WARRANTY; without even the implied warranty of
#-# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#-# GNU General Public License for more details.
#-# 
#-# You should have received a copy of the GNU General Public License
#-# along with this program.  If not, see <http://www.gnu.org/licenses/>.
#-# 
*/
#include <ctype.h>
#include <pthread.h>
#include <stdbool.h>
#include <stdio.h>

#include <X11/Intrinsic.h>
#include <X11/extensions/Xinerama.h>

#include <gdk/gdkx.h>
#include <gtk/gtk.h>

#include "ColorCodes.h"
#include "dsimple.h"
#include "FallenSnow.h"
#include "Flags.h"
#include "MsgBox.h"
#include "mygettext.h"
#include "plasmasnow.h"
#include "safe_malloc.h"
#include "scenery.h"
#include "StormWindow.h"
#include "Utils.h"
#include "windows.h"
#include "WinInfo.h"
#include "xdo.h"


/***********************************************************
 * Externally provided to this Module.
 */
void uninitQPickerDialog();


/** *********************************************************************
 ** Module globals and consts.
 **/
bool mIsWindowBeingDragged;
Window mWindowBeingDragged = None;
Window mActiveAppDragWindowCandidate = None;

int mUpdateWindowsLockCounter = 0;

Window mActiveAppWindow = None;
const int mINVALID_POSITION = -1;
int mActiveAppXPos = mINVALID_POSITION;
int mActiveAppYPos = mINVALID_POSITION;


/** *********************************************************************
 ** This method ...
 **/
void addWindowsModuleToMainloop() {
    if (mGlobal.hasDestopWindow) {
        mGlobal.currentWorkspace = getCurrentWorkspaceNumber();
        getCurrentWorkspaceData();

        addMethodToMainloop(PRIORITY_DEFAULT, time_wupdate,
            updateWindowsList);
    }

    if (!mGlobal.isDoubleBuffered) {
        addMethodToMainloop(PRIORITY_DEFAULT, time_sendevent,
            do_sendevent);
    }
}

/** *********************************************************************
 ** This method ...
 **/
int WorkspaceActive() {
    // ah, so difficult ...
    if (Flags.AllWorkspaces) {
        return 1;
    }

    for (int i = 0; i < mGlobal.NVisWorkSpaces; i++) {
        if (mGlobal.VisWorkSpaces[i] == mGlobal.ChosenWorkSpace) {
            return 1;
        }
    }

    return 0;
}

/** *********************************************************************
 ** This method ...
 **/
int do_sendevent() {
    XExposeEvent event;
    event.type = Expose;

    event.send_event = True;
    event.display = mGlobal.display;
    event.window = mGlobal.SnowWin;

    event.x = 0;
    event.y = 0;
    event.width = mGlobal.SnowWinWidth;
    event.height = mGlobal.SnowWinHeight;

    XSendEvent(mGlobal.display, mGlobal.SnowWin, True, Expose, (XEvent *) &event);
    return TRUE;
}

/** *********************************************************************
 ** This method ...
 **/
void getCurrentWorkspaceData() {
    static XClassHint class_hints;
    static XSetWindowAttributes attr;
    static long valuemask;
    static long hints[5] = {2, 0, 0, 0, 0};
    static Atom motif_hints;
    static XSizeHints wmsize;

    if (!mGlobal.hasDestopWindow) {
        mGlobal.NVisWorkSpaces = 1;
        mGlobal.VisWorkSpaces[0] = mGlobal.currentWorkspace;
        return;
    }

    static Window probeWindow = 0;
    if (probeWindow) {
        XDestroyWindow(mGlobal.display, probeWindow);
    } else {
        // Probe window
        attr.background_pixel = WhitePixel(mGlobal.display, mGlobal.Screen);
        attr.border_pixel = WhitePixel(mGlobal.display, mGlobal.Screen);
        attr.event_mask = ButtonPressMask;
        valuemask = CWBackPixel | CWBorderPixel | CWEventMask;
        class_hints.res_name = (char*) "plasmasnow";
        class_hints.res_class = (char*) "plasmasnow";
        motif_hints = XInternAtom(mGlobal.display, "_MOTIF_WM_HINTS", False);
        wmsize.flags = USPosition | USSize;
    }

    int number;
    XineramaScreenInfo *info = XineramaQueryScreens(mGlobal.display, &number);
    if (number == 1 || info == NULL) {
        mGlobal.NVisWorkSpaces = 1;
        mGlobal.VisWorkSpaces[0] = mGlobal.currentWorkspace;
        return;
    }

    // This is for bspwm and possibly other tiling window magagers.
    // Determine which workspaces are visible: place a window (probeWindow)
    // in each xinerama screen, and ask in which workspace the window
    // is located.
    probeWindow = XCreateWindow(mGlobal.display, mGlobal.Rootwindow, 1, 1, 1, 1,
        10, DefaultDepth(mGlobal.display, mGlobal.Screen), InputOutput,
        DefaultVisual(mGlobal.display, mGlobal.Screen), valuemask, &attr);

    XSetClassHint(mGlobal.display, probeWindow, &class_hints);

    // to prevent the user to determine the intial position (in twm for example)
    XSetWMNormalHints(mGlobal.display, probeWindow, &wmsize);

    XChangeProperty(mGlobal.display, probeWindow, motif_hints, motif_hints, 32,
        PropModeReplace, (unsigned char *) &hints, 5);

    xdo_map_window(mGlobal.xdo, probeWindow);

    mGlobal.NVisWorkSpaces = number;
    int prev = -SOMENUMBER;
    for (int i = 0; i < number; i++) {
        int x = info[i].x_org;
        int y = info[i].y_org;
        int w = info[i].width;
        int h = info[i].height;

        // Place probeWindow in the center of xinerama screen[i].
        int xm = x + w / 2;
        int ym = y + h / 2;

        xdo_move_window(mGlobal.xdo, probeWindow, xm, ym);
        xdo_wait_for_window_map_state(mGlobal.xdo,
            probeWindow, IsViewable);

        long desktop;
        int rc = xdo_get_desktop_for_window(mGlobal.xdo,
            probeWindow, &desktop);
        if (rc == XDO_ERROR) {
            desktop = mGlobal.currentWorkspace;
        }
        mGlobal.VisWorkSpaces[i] = desktop;

        // This is for the case that the xinerama screens belong to
        // different workspaces, as seems to be the case in e.g. bspwm.
        if (desktop != prev) {
            if (prev >= 0) {
                mGlobal.WindowOffsetX = 0;
                mGlobal.WindowOffsetY = 0;
            }
            prev = desktop;
        }
    }

    xdo_unmap_window(mGlobal.xdo, probeWindow);
}


/** *********************************************************************
 ** This method gets the location and size of xinerama screen.
 **/
int getXineramaScreenInfo(Display *display, int requestScreen,
    int *resultScreenPosX, int *resultScreenPosY,
    int *resultScreenWidth, int *resultScreenHeigth) {

    // Get Xinerama Screen Info, if none, exit.
    int infoArrayLength;
    XineramaScreenInfo* infoArray = XineramaQueryScreens(
        display, &infoArrayLength);
    if (infoArray == NULL) {
        return FALSE;
    }

    // Bound the requested screen to the array, and return
    // it's info if found.
    const int boundedRequestScreeen =
        (requestScreen <= infoArrayLength) ?
            requestScreen : infoArrayLength;

    if (boundedRequestScreeen >= 0) {
        *resultScreenWidth = infoArray[boundedRequestScreeen].width;
        *resultScreenHeigth = infoArray[boundedRequestScreeen].height;
        *resultScreenPosX = infoArray[boundedRequestScreeen].x_org;
        *resultScreenPosY = infoArray[boundedRequestScreeen].y_org;
        XFree(infoArray);
        return infoArrayLength;
    }

    // If bounded requested screen item not found in xinerama
    // array, assume screen position = 0/0, then assume
    // largest accomodativescreen  width/heigth.
    *resultScreenPosX = 0; *resultScreenPosY = 0;

    *resultScreenWidth = 0; *resultScreenHeigth = 0;
    for (int i = 0; i < infoArrayLength; i++) {
        if (infoArray[i].width > *resultScreenWidth) {
            *resultScreenWidth = infoArray[i].width;
        }
        if (infoArray[i].height > *resultScreenHeigth) {
            *resultScreenHeigth = infoArray[i].height;
        }
    }

    XFree(infoArray);
    return infoArrayLength;
}

/** *********************************************************************
 ** This method ...
 **/
void initDisplayDimensions() {
    int x, y;
    xdo_get_window_location(mGlobal.xdo, mGlobal.Rootwindow,
        &x, &y, NULL);
    mGlobal.Xroot = x;
    mGlobal.Yroot = y;

    unsigned int w, h;
    xdo_get_window_size(mGlobal.xdo, mGlobal.Rootwindow,
        &w, &h);
    mGlobal.Wroot = w;
    mGlobal.Hroot = h;

    updateDisplayDimensions();
}

/** *********************************************************************
 ** This method ...
 **/
void updateDisplayDimensions() {
    lockFallenSnowSemaphore();

    xdo_wait_for_window_map_state(mGlobal.xdo,
        mGlobal.SnowWin, IsViewable);

    Window root;
    int x, y;
    unsigned int w, h, b, d;
    int rc = XGetGeometry(mGlobal.display, mGlobal.SnowWin,
        &root, &x, &y, &w, &h, &b, &d);
    if (rc == 0) {
        uninitQPickerDialog();
        exit(1);
        return;
    }

    mGlobal.SnowWinWidth = w;
    mGlobal.SnowWinHeight = h + Flags.OffsetS;
    mGlobal.SnowWinBorderWidth = b;
    mGlobal.SnowWinDepth = d;

    updateFallenSnowDesktopItemHeight();
    clearAndRedrawScenery();
    updateFallenSnowDesktopItemDepth();

    if (!mGlobal.isDoubleBuffered) {
        clearGlobalSnowWindow();
    }

    unlockFallenSnowSemaphore();
}

/** *********************************************************************
 ** This method sets OS desktop background
 **/
void SetBackground() {
    char *f = Flags.BackgroundFile;
    if (!IsReadableFile(f)) {
        return;
    }

    Display *display = mGlobal.display;
    Window window = mGlobal.SnowWin;
    int screen_num = DefaultScreen(display);
    int depth = DefaultDepth(display, screen_num);

    int w = mGlobal.SnowWinWidth;
    int h = mGlobal.SnowWinHeight;
    GdkPixbuf* pixbuf = gdk_pixbuf_new_from_file_at_scale(
        f, w, h, FALSE, NULL);
    if (!pixbuf) {
        return;
    }

    // pixels1 is freed by XDestroyImage.
    int n_channels = gdk_pixbuf_get_n_channels(pixbuf);
    guchar* pixels = gdk_pixbuf_get_pixels(pixbuf);
    unsigned char* pixels1 = (unsigned char *)
        malloc(w * h * 4 * sizeof(unsigned char));

    const int ROW_STRIDE = gdk_pixbuf_get_rowstride(pixbuf);
    if (is_little_endian()) {
        for (int k = 0, i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                guchar *p = &pixels[i * ROW_STRIDE + j * n_channels];
                pixels1[k++] = p[2];
                pixels1[k++] = p[1];
                pixels1[k++] = p[0];
                pixels1[k++] = 0xff;
            }
        }
    } else {
        for (int k = 0, i = 0; i < h; i++) {
            for (int j = 0; j < w; j++) {
                guchar *p = &pixels[i * ROW_STRIDE + j * n_channels];
                pixels1[k++] = 0xff;
                pixels1[k++] = p[0];
                pixels1[k++] = p[1];
                pixels1[k++] = p[2];
            }
        }
    }

    XImage* ximage = XCreateImage(display,
        DefaultVisual(display, screen_num),
        depth, ZPixmap, 0, (char *) pixels1, w, h,
        XBitmapPad(display), 0);
    XInitImage(ximage);

    Pixmap pixmap = XCreatePixmap(display, window,
        w, h, DefaultDepth(display, screen_num));
    XPutImage(display, pixmap, XCreateGC(display, pixmap, 0, 0),
        ximage, 0, 0, 0, 0, w, h);
    XSetWindowBackgroundPixmap(display, window, pixmap);

    g_object_unref(pixbuf);
    XFreePixmap(display, pixmap);
    XDestroyImage(ximage);

    return;
}

/** *********************************************************************
 ** Module MAINLOOP methods.
 **
 ** This method is called periodically from the UI mainloop to update
 ** our internal X11 Windows array. (Laggy huh).
 **/
int updateWindowsList() {
    if (Flags.shutdownRequested) {
        return false;
    }
    if (Flags.NoKeepSnowOnWindows) {
        return true;
    }

    if (softLockFallenSnowBaseSemaphore(3,
        &mUpdateWindowsLockCounter)) {
        return true;
    }

    // Once in a while, we force updating windows.
    static int wcounter = 0;
    wcounter++;
    if (wcounter > 9) {
        mGlobal.WindowsChanged = 1;
        wcounter = 0;
    }
    if (!mGlobal.WindowsChanged) {
        unlockFallenSnowSemaphore();
        return true;
    }
    mGlobal.WindowsChanged = 0;

    // Get current workspace number & sanity check.
    const long WORKSPACE = getCurrentWorkspaceNumber();
    if (WORKSPACE < 0) {
        unlockFallenSnowSemaphore();
        printf("%splasmasnow: Virtual workspace has been lost - FATAL.%s\n",
            COLOR_RED, COLOR_NORMAL);
        displayMessageBox(100, 200, 355, 66, "plasmasnow",
            "Virtual workspace has been lost - FATAL.");
        Flags.shutdownRequested = 1;
        return true;
    }

    // Get current workspace data on workspace number change.
    if (mGlobal.currentWorkspace != WORKSPACE) {
        mGlobal.currentWorkspace = WORKSPACE;
        getCurrentWorkspaceData();
    }

    // Don't update windows list until drag stops.
    if (isWindowBeingDragged()) {
        doAllFallenSnowWinInfoUpdates();
        unlockFallenSnowSemaphore();
        return true;
    }

    // Update windows list.
    getWinInfoForAllWindows();
    for (int i = 0; i < mGlobal.winInfoListLength; i++) {
        mGlobal.winInfoList[i].x += mGlobal.WindowOffsetX - mGlobal.SnowWinX;
        mGlobal.winInfoList[i].y += mGlobal.WindowOffsetY - mGlobal.SnowWinY;
    }

    // Sanity check Snow window every time.
    if (mGlobal.SnowWin != mGlobal.Rootwindow) {
        WinInfo* winInfo = getWinInfoForWindow(mGlobal.SnowWin);
        if (!winInfo && !mGlobal.hasTransparentWindow) {
            printf("%splasmasnow: SnowWindow has been lost - FATAL.%s\n",
                COLOR_RED, COLOR_NORMAL);
            displayMessageBox(100, 200, 310, 66, "plasmasnow",
                "SnowWindow has been lost - FATAL.");
            Flags.shutdownRequested = 1;
        }
    }

    // Resolve fallensnow surfaces states with
    // new windows WinInfo surfaces list.
    doAllFallenSnowWinInfoUpdates();

    unlockFallenSnowSemaphore();
    return true;
}

/** *********************************************************************
 ** This method returns the Active window.
 **/
Window getActiveX11Window() {
    Window activeWindow = None;

    getActiveWindowFromXDO(mGlobal.xdo, &activeWindow);

    return activeWindow;
}

/** *********************************************************************
 ** This method returns the Focused window.
 **/
Window getFocusedX11Window() {
    Window focusedWindow = None;
    int focusedWindowState = 0;

    XGetInputFocus(mGlobal.display,
        &focusedWindow, &focusedWindowState);

    return focusedWindow;
}

/** *********************************************************************
 ** These are helper methods for FocusedApp member values.
 **/
int getFocusedX11XPos() {
    const WinInfo* focusedWinInfo =
        getWinInfoForWindow(getFocusedX11Window());
    return focusedWinInfo ?
        focusedWinInfo->x : mINVALID_POSITION;
}

int getFocusedX11YPos() {
    const WinInfo* focusedWinInfo =
        getWinInfoForWindow(getFocusedX11Window());
    return focusedWinInfo ?
        focusedWinInfo->y : mINVALID_POSITION;
}

/** *********************************************************************
 ** These are helper methods for ActiveApp member values.
 **/
void clearAllActiveAppFields() {
    setActiveAppWindow(None);
    setActiveAppXPos(mINVALID_POSITION);
    setActiveAppYPos(mINVALID_POSITION);

    clearAllDragFields();
}

// Active App Window value.
Window getActiveAppWindow() {
    return mActiveAppWindow;
}
void setActiveAppWindow(Window window) {
    mActiveAppWindow = window;
}

// Active App Window parent.
Window getParentOfActiveAppWindow() {
    Window rootWindow = None;
    Window parentWindow = None;
    Window* childrenWindow = NULL;
    unsigned int windowChildCount = 0;

    XQueryTree(mGlobal.display, getActiveAppWindow(),
        &rootWindow, &parentWindow,
        &childrenWindow, &windowChildCount);

    if (childrenWindow) {
        XFree((char *) childrenWindow);
    }

    return parentWindow;
}

// Active App Window x/y value.
int getActiveAppXPos() {
    return mActiveAppXPos;
}
void setActiveAppXPos(int xPos) {
    mActiveAppXPos = xPos;
}

int getActiveAppYPos() {
    return mActiveAppYPos;
}
void setActiveAppYPos(int yPos) {
    mActiveAppYPos = yPos;
}


/** *********************************************************************
 ** This method handles XFixes XFixesCursorNotify Cursor change events.
 **/
void onCursorChange(__attribute__((unused)) XEvent* event) {
    // XFixesCursorNotifyEvent* cursorEvent =
    //     (XFixesCursorNotifyEvent*) event;
}

/** *********************************************************************
 ** This method handles X11 Window focus (activation status) change.
 **/
void onAppWindowChange(Window window) {
    // Set default ActivateApp window values & Drag values.
    clearAllActiveAppFields();

    // Save actual Activated window values.
    setActiveAppWindow(window);

    const WinInfo* activeAppWinInfo =
        getWinInfoForWindow(getActiveAppWindow());
    if (activeAppWinInfo) {
        setActiveAppXPos(activeAppWinInfo->x);
        setActiveAppYPos(activeAppWinInfo->y);
    }
}

/** *********************************************************************
 ** This method handles X11 Windows being created.
 **/
void onWindowCreated(XEvent* event) {
    // Update our list to include the created one.
    getWinInfoForAllWindows();

    // Is this a signature of a transient Plasma DRAG Window
    // being created? If not, early exit.
    //     Event:  se? 0  w [0x01886367]  pw [0x00000764]
    //             pos (0,0) @ (1920,1080) w(0)  r? 0.
    if (event->xcreatewindow.send_event != 0) {
        return;
    }
    if (event->xcreatewindow.parent != mGlobal.Rootwindow) {
        return;
    }
    if (event->xcreatewindow.x != 0) {
        return;
    }
    if (event->xcreatewindow.y != 0) {
        return;
    }
    if (event->xcreatewindow.width != mGlobal.SnowWinWidth) {
        return;
    }
    if (event->xcreatewindow.height != mGlobal.SnowWinHeight) {
        return;
    }
    if (event->xcreatewindow.border_width != 0) {
        return;
    }
    if (event->xcreatewindow.override_redirect != 0) {
        return;
    }

    setActiveAppDragWindowCandidate(event->xcreatewindow.window);
}

/** *********************************************************************
 ** This method handles X11 Windows being reparented.
 **/
void onWindowReparent(__attribute__((unused)) XEvent* event) {
}

/** *********************************************************************
 ** This method handles X11 Windows being moved, sized, changed.
 **/
void onWindowChanged(__attribute__((unused)) XEvent* event) {
}

/** *********************************************************************
 ** This method handles X11 Windows being made visible to view.
 **
 ** Determine if user is dragging a window, and clear it's fallensnow.
 **/
void onWindowMapped(XEvent* event) {
    // Update our list for visibility change.
    getWinInfoForAllWindows();

    // Determine window drag state.
    if (!isWindowBeingDragged()) {
        if (isMouseClickedAndHeldInWindow(
            event->xmap.window)) {
            if (event->xmap.window != None) {
                const Window focusedWindow =
                    getFocusedX11Window();
                if (focusedWindow != None) {
                    const Window dragWindow =
                        getDragWindowOf(focusedWindow);
                    if (dragWindow != None) {
                        setIsWindowBeingDragged(true);
                        setWindowBeingDragged(dragWindow);
                        removeFallenSnowFromWindow(getWindowBeingDragged());
                        return;
                    }
                }
            }
        }
    }

    // 2nd Determine window drag state, for KDE Plasma.
    // Is this a signature of a transient Plasma DRAG Window
    // being mapped? If not, early exit.
    //     Event:  se? 0  ew [0x00000764]  w [0x018a1b21]  r? 0.
    bool isActiveAppMoving = true;
    if (event->xmap.send_event != 0) {
        isActiveAppMoving = false;
    }
    if (event->xmap.window != getActiveAppDragWindowCandidate()) {
        isActiveAppMoving = false;
    }
    if (event->xmap.event != mGlobal.Rootwindow) {
        isActiveAppMoving = false;
    }
    if (event->xmap.override_redirect != 0) {
        isActiveAppMoving = false;
    }

    // Can we set drag state - New Plasma "keyboard" method?
    if (isActiveAppMoving) {
        setIsWindowBeingDragged(getActiveAppWindow() != None);
        setWindowBeingDragged(getActiveAppWindow());
        if (isWindowBeingDragged()) {
            // New Plasma "keyboard" DRAG method, we can't determine which
            // visible window (window neither focused nor active),
            // so we shake all free to avoid magically hanging snow.
            removeFallenSnowFromAllWindows();
        }
    }
}

/** *********************************************************************
 ** This method handles X11 Windows being focused In.
 **/
void onWindowFocused(__attribute__((unused)) XEvent* event) {
}

/** *********************************************************************
 ** This method handles X11 Windows being focused In.
 **/
void onWindowBlurred(__attribute__((unused)) XEvent* event) {
}

/** *********************************************************************
 ** This method handles X11 Windows being Hidden from view.
 **
 ** Our main job is to clear window drag state.
 **/
void onWindowUnmapped(__attribute__((unused)) XEvent* event) {
    // Update our list for visibility change.
    getWinInfoForAllWindows();

    // Clear window drag state.
    if (isWindowBeingDragged()) {
        clearAllDragFields();
    }
}

/** *********************************************************************
 ** This method handles X11 Windows being destroyed.
 **/
void onWindowDestroyed(__attribute__((unused)) XEvent* event) {
    // Update our list to reflect the destroyed one.
    getWinInfoForAllWindows();

    // Clear window drag state.
    if (isWindowBeingDragged()) {
        clearAllDragFields();
    }
}

/** *********************************************************************
 ** This method decides if the user ia dragging a window via a mouse
 ** click-and-hold on the titlebar.
 **/
bool isMouseClickedAndHeldInWindow(Window window) {
    //  Find the focused window pointer click state.
    Window root_return, child_return;
    int root_x_return, root_y_return;
    int win_x_return, win_y_return;
    unsigned int pointerState;

    bool foundPointerState = XQueryPointer(
        mGlobal.display, window,
        &root_return, &child_return,
        &root_x_return, &root_y_return,
        &win_x_return, &win_y_return,
        &pointerState);

    // If click-state is clicked-down, we're dragging.
    const unsigned int POINTER_CLICKDOWN = 256;
    return foundPointerState && (pointerState & POINTER_CLICKDOWN);
}

/** *********************************************************************
 ** These methods are window drag-state helpers.
 **/
void clearAllDragFields() {
    setIsWindowBeingDragged(false);
    setWindowBeingDragged(None);
    setActiveAppDragWindowCandidate(None);
}

bool isWindowBeingDragged() {
    return mIsWindowBeingDragged;
}
void setIsWindowBeingDragged(bool isWindowBeingDragged) {
    mIsWindowBeingDragged = isWindowBeingDragged;
}

Window getWindowBeingDragged() {
    return mWindowBeingDragged;
}
void setWindowBeingDragged(Window window) {
    mWindowBeingDragged = window;
}

// Active App Drag window candidate value.
Window getActiveAppDragWindowCandidate() {
    return mActiveAppDragWindowCandidate;
}
void setActiveAppDragWindowCandidate(Window candidate) {
    mActiveAppDragWindowCandidate = candidate;
}

/** *********************************************************************
 ** This method determines which window is being dragged on user
 ** click and hold window. Returns self or ancestor whose Window
 ** is in mGlobal.winInfoList (visible window on screen).
 **/
Window getDragWindowOf(Window window) {
    Window windowNode = window;

    while (true) {
        // Is current node in windows list?
        WinInfo* windowListItem = mGlobal.winInfoList;
        for (int i = 0; i < mGlobal.winInfoListLength; i++) {
            if (windowNode == windowListItem->window) {
                return windowNode;
            }
            windowListItem++;
        }

        // If not in list, move up to parent and loop.
        Window root, parent;
        Window* children = NULL;
        unsigned int windowChildCount;
        if (!(XQueryTree(mGlobal.display, windowNode,
                &root, &parent, &children, &windowChildCount))) {
            return None;
        }
        if (children) {
            XFree((char *) children);
        }

        windowNode = parent;
    }
}
/** *********************************************************************
 ** This method logs a timestamp in seconds & milliseconds.
 **/
void logCurrentTimestamp() {
    // Get long date.
    time_t dateNow = time(NULL);
    char* dateStringWithEOL = ctime(&dateNow);

    // Get Milliseconds.
    struct timespec timeNow;
    clock_gettime(CLOCK_REALTIME, &timeNow);

    // Parse date. In: |Mon Feb 19 11:59:09 2024\n|
    //            Out: |Mon Feb 19 11:59:09|
    int lenDateStringWithoutEOL =
        strlen(dateStringWithEOL) - 6;

    // Parse seconds and milliseconds.
    time_t seconds  = timeNow.tv_sec;
    long milliseconds = round(timeNow.tv_nsec / 1.0e6);
    if (milliseconds > 999) {
        milliseconds = 0;
        seconds++;
    }

    // Log parseed date. Out: |Mon Feb 19 11:59:09 2024.### : |
    printf("%.*s.", lenDateStringWithoutEOL, dateStringWithEOL);
    printf("%03ld : ", (intmax_t) milliseconds);
}

/** *********************************************************************
 ** This method returns the Active window.
 **/
void logWindowAndAllParents(Window window) {
    logCurrentTimestamp();
    fprintf(stdout, "  win: 0x%08lx  ", window);

    Window windowItem = window;
    while (windowItem != None) {
        Window rootWindow = None;
        Window parentWindow = None;
        Window* childrenWindow = NULL;
        unsigned int windowChildCount = 0;

        XQueryTree(mGlobal.display, windowItem,
            &rootWindow, &parentWindow, &childrenWindow,
            &windowChildCount);
        fprintf(stdout, "  par: 0x%08lx", parentWindow);

        if (childrenWindow) {
            XFree((char *) childrenWindow);
        }

        windowItem = parentWindow;
    }

    // Terminate the log line.
    fprintf(stdout, "\n");
}
