#include "reactui_minimal.h"

// Define states
STATE(int, count, setCount, 0)
STATE(float, temperature, setTemperature, 20.5f)

// Event handlers
void handleIncrement() {
    setCount(count + 1);
}

void handleReset() {
    setCount(0);
    setTemperature(20.5f);
}

void handleTempUp() {
    setTemperature(temperature + 0.5f);
}

// Component-like render function
const char* App() {
    static char html[2048];
    snprintf(html, sizeof(html),
        HTML(
            <div>
                <h1>React-Style UI with Events</h1>
                <p>Count: %d</p>
                <p>Temperature: %.1f C</p>
                <button onclick={handleIncrement}>Increment</button>
                <button onclick={handleTempUp}>Temp Up</button>
                <button onclick={handleReset}>Reset</button>
            </div>
        ),
        count,
        temperature
    );
    return html;
}

// Re-render when state changes
void onStateChange() {
    ReactUI_Render(g_ui, App());
    triggerRerender();
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    
    // Create UI
    ReactUI* ui = ReactUI_Create(hInstance);
    
    // Register event handlers BEFORE rendering
    BIND_EVENT("handleIncrement", handleIncrement);
    BIND_EVENT("handleReset", handleReset);
    BIND_EVENT("handleTempUp", handleTempUp);
    
    // Register states with auto-rerender
    USE_STATE(count, onStateChange);
    USE_STATE(temperature, onStateChange);
    
    // Initial render
    ReactUI_Render(ui, App());
    ReactUI_CreateWindow(ui, "React-Style UI", 450, 350);
    
    // Message loop
    int result = ReactUI_Run();
    
    // Cleanup
    ReactUI_Destroy(ui);
    
    return result;
}