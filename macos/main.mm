//macos/main.mm
#ifdef __APPLE__
#include <TargetConditionals.h>
#if TARGET_OS_OSX

#import <Cocoa/Cocoa.h>
#include <cstdio>

#include "flux/flux.hpp"


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

    auto cfg = FluxAppWidget::getInstance();

    int w = cfg->windowWidth;
    int h = cfg->windowHeight;

    if (cfg->fullscreen || cfg->maximize) {
        NSScreen* screen = [NSScreen mainScreen];
        NSRect    frame  = screen.visibleFrame;
        w = (int)frame.size.width;
        h = (int)frame.size.height;
    }

    _app->createWindow(cfg->title, w, h);

    if (cfg->fullscreen) {
        NSWindow* win = (__bridge NSWindow*)_app->getWindow();
        if (win) [win toggleFullScreen:nil];
    } else if (cfg->maximize) {
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