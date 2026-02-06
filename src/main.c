#include "react_ui.h"
#include <windows.h>

// Simple error handler
void onError(const char* message) {
    char errorMsg[256];
    sprintf(errorMsg, "Error: %s", message);
    MessageBoxA(NULL, errorMsg, "ReactUI Error", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    
    // Simple counter with flexbox layout
    const char* html = 
        "<div style=\"display: flex; flex-direction: column; justify-content: center; "
        "align-items: center; width: 400; height: 300; background: #f5f5f5; gap: 20;\">"
            
            "<div style=\"font-size: 48; color: #1976D2; padding: 20;\">"
                "{{counter}}"
            "</div>"
            
            "<div style=\"display: flex; flex-direction: row; gap: 10;\">"
                "<button onclick=\"counter++\" style=\"width: 100; height: 50; "
                "background: #4CAF50; color: white; font-size: 18; border-radius: 5;\">"
                    "+"
                "</button>"
                
                "<button onclick=\"counter--\" style=\"width: 100; height: 50; "
                "background: #f44336; color: white; font-size: 18; border-radius: 5;\">"
                    "-"
                "</button>"
                
                "<button onclick=\"counter=0\" style=\"width: 100; height: 50; "
                "background: #FF9800; color: white; font-size: 18; border-radius: 5;\">"
                    "Reset"
                "</button>"
            "</div>"
            
            "<div style=\"font-size: 14; color: #666; padding: 10;\">"
                "Click buttons to update counter"
            "</div>"
            
        "</div>";
    
    // Create UI instance
    ReactUI* ui = ReactUI_Create(hInstance);
    if (!ui) {
        MessageBoxA(NULL, "Failed to create ReactUI instance", "Error", MB_OK);
        return 1;
    }
    
    // Set error callback
    ReactUI_SetErrorCallback(ui, onError);
    
    // Initialize state
    ReactUI_SetState(ui, "counter", 0);
    
    // Render HTML
    ReactUI_Render(ui, html);
    
    // Create window
    HWND hwnd = ReactUI_CreateWindow(ui, "Simple Counter - Flexbox Test", 450, 400);
    if (!hwnd) {
        MessageBoxA(NULL, "Failed to create window", "Error", MB_OK);
        ReactUI_Destroy(ui);
        return 1;
    }
    
    // Run message loop
    int result = ReactUI_Run();
    
    // Cleanup
    ReactUI_Destroy(ui);
    
    return result;
}