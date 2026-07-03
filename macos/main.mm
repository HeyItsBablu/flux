//macos/main.mm
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#include <cstdio>

#include "flux/flux.hpp"
#include "AppConfig.generated.h"



// Forward declaration — defined in lib/main.cpp
WidgetPtr createApp(FluxUI* app);


// ============================================================================
// FluxAppDelegate — sets up the app menu and launches the FluxUI window
// ============================================================================

@interface FluxAppDelegate : NSObject <NSApplicationDelegate> {
    FluxUI* _app;
}
@end
 
@implementation FluxAppDelegate

- (void)applicationDidFinishLaunching:(NSNotification*)notification {

    // ── Basic app menu (Quit item so Cmd+Q works) ─────────────────────────────
    NSMenu*     menuBar  = [[NSMenu alloc] init];
    NSMenuItem* appItem  = [[NSMenuItem alloc] init];
    [menuBar addItem:appItem];
    [NSApp setMainMenu:menuBar];

    NSMenu*     appMenu  = [[NSMenu alloc] init];
    NSMenuItem* quitItem = [[NSMenuItem alloc]
        initWithTitle:@"Quit"
               action:@selector(terminate:)
        keyEquivalent:@"q"];
    [appMenu addItem:quitItem];
    [appItem setSubmenu:appMenu];

    // ── Build FluxUI ──────────────────────────────────────────────────────────
    _app = new FluxUI(nullptr);
    _app->build([&]() { return createApp(_app); });

    // Window geometry comes straight from AppConfig.json — FluxAppWidget
    // no longer carries window state.
    int w = FLUX_APP_WINDOW_WIDTH;
    int h = FLUX_APP_WINDOW_HEIGHT;
    bool fullscreen = static_cast<bool>(FLUX_APP_FULLSCREEN);
    bool maximize   = static_cast<bool>(FLUX_APP_MAXIMIZE);

    if (maximize && !fullscreen) {
        NSScreen* screen = [NSScreen mainScreen];
        NSRect    frame  = screen.visibleFrame;
        w = (int)frame.size.width;
        h = (int)frame.size.height;
    }

    _app->createWindow(FLUX_APP_NAME, w, h);

    if (fullscreen) {
        NSWindow* win = (__bridge NSWindow*)_app->getWindow();
        if (win) [win toggleFullScreen:nil];
    } else if (maximize) {
        NSWindow* win = (__bridge NSWindow*)_app->getWindow();
        if (win) [win zoom:nil];
    }
}

- (void)applicationWillTerminate:(NSNotification*)notification {
    delete _app;
    _app = nullptr;
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:(NSApplication*)sender {
    return YES;
}

@end

// ============================================================================
// main
// ============================================================================

int main(int argc, const char* argv[]) {

#ifdef FLUX_DEBUG
    setvbuf(stdout, nullptr, _IONBF, 0);
    setvbuf(stderr, nullptr, _IONBF, 0);
#endif

    @autoreleasepool {
        [NSApplication sharedApplication];
        [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];

        FluxAppDelegate* delegate = [[FluxAppDelegate alloc] init];
        [NSApp setDelegate:delegate];

        [NSApp run];
    }

    return 0;
}

#endif // TARGET_OS_OSX
#endif // __APPLE__