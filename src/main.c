#include <windows.h>
#include "reactui_minimal.h"

// ============================================================================
// MAIN APPLICATION
// ============================================================================

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    
    // Create UI instance
    ReactUI* ui = ReactUI_Create(hInstance);
    if (!ui) {
        MessageBox(NULL, "Failed to create UI", "Error", MB_ICONERROR);
        return 1;
    }
    
    // Define simple HTML
    const char* html = HTML(
        <div>
            <h1>Hello ReactUI!</h1>
            <h2>Minimal Version</h2>
            <p>This is a simple paragraph.</p>
            <p>Another paragraph here.</p>
            <button>Click Me</button>
            <button>Another Button</button>
        </div>
    );
    
    // Render the HTML
    ReactUI_Render(ui, html);
    
    // Create window
    HWND hwnd = ReactUI_CreateWindow(ui, "ReactUI - Minimal Parser & Renderer", 600, 500);
    if (!hwnd) {
        MessageBox(NULL, "Failed to create window", "Error", MB_ICONERROR);
        ReactUI_Destroy(ui);
        return 1;
    }
    
    // Run message loop
    int result = ReactUI_Run();
    
    // Cleanup
    ReactUI_Destroy(ui);
    
    return result;
}