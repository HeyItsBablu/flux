#include "react_ui.h"

// ============================================================================
// TEXT RENDERING TEST
// ============================================================================

STATE(int, count, setCount, 0);

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    
    ReactUI* ui = ReactUI_Create(hInstance);
    if (!ui) return -1;
    
    setCount(42);
    
    // Test all text elements
    ReactUI_Render(ui, HTML(
        <div style="display: flex; flex-direction: column; gap: 15; padding: 20;">
            
            <h1 style="color: #1976D2;">{{count}}</h1>
            
            <h2 style="color: #388E3C;">This is H2 Header</h2>
            
            <h3 style="color: #F57C00;">This is H3 Header</h3>
            
            <p style="color: #333;">This is a paragraph with regular text.</p>
            
            <p style="color: #666; font-size: 14;">
                This is smaller paragraph text (14px).
            </p>
            
            <p style="color: #E53935; font-size: 20;">
                This is larger paragraph (20px) with state: Count = {{count}}
            </p>
            
            <div style="background: #E3F2FD; padding: 10;">
                <p style="color: #1976D2;">Text inside a div with background</p>
            </div>
            
            <button onclick="count++" style="background: #4CAF50; color: white; padding: 10;">
                Increment (Count: {{count}})
            </button>
            
            <button onclick="count--" style="background: #f44336; color: white; padding: 10;">
                Decrement
            </button>
            
            <div style="display: flex; flex-direction: row; gap: 10;">
                <p style="flex-grow: 1; background: #FFF3E0; padding: 10; color: #E65100;">
                    Left column text
                </p>
                <p style="flex-grow: 1; background: #E8F5E9; padding: 10; color: #2E7D32;">
                    Right column text with count: {{count}}
                </p>
            </div>
            
        </div>
    ));
    
    HWND hwnd = ReactUI_CreateWindow(ui, "Text Rendering Test", 800, 700);
    if (!hwnd) {
        ReactUI_Destroy(ui);
        return -1;
    }
    
    int result = ReactUI_Run();
    ReactUI_Destroy(ui);
    
    return result;
}