#include "reactui_minimal.h"

// Define states
STATE(int, count, setCount, 0)
STATE(float, temperature, setTemperature, 20.5f)
STATE(double, price, setPrice, 99.99)
STATE(char, grade, setGrade, 'A')

// Event handlers
void handleIncrement() {
    setCount(count + 1);
}

void handleDecrement() {
    setCount(count - 1);
}

void handleReset() {
    setCount(0);
    setTemperature(20.5f);
    setPrice(99.99);
    setGrade('A');
}

void handleTempUp() {
    setTemperature(temperature + 0.5f);
}

void handleTempDown() {
    setTemperature(temperature - 0.5f);
}

void handlePriceUp() {
    setPrice(price + 10.0);
}

void handlePriceDown() {
    setPrice(price - 10.0);
}

void handleGradeChange() {
    if (grade == 'A') setGrade('B');
    else if (grade == 'B') setGrade('C');
    else if (grade == 'C') setGrade('D');
    else if (grade == 'D') setGrade('F');
    else setGrade('A');
}

// Component-like render function with React-style template
const char* App() {
    return HTML(
        <div>
            <h1>React-Style UI Framework</h1>
            <h2>Counter Demo</h2>
            <p>Count: {count}</p>
            <button onclick={handleIncrement}>Increment</button>
            <button onclick={handleDecrement}>Decrement</button>
            
            <h2>Temperature</h2>
            <p>Temperature: {temperature} C</p>
            <button onclick={handleTempUp}>Temp +</button>
            <button onclick={handleTempDown}>Temp -</button>
            
            <h2>Price Tracker</h2>
            <p>Price: ${price}</p>
            <button onclick={handlePriceUp}>Price +</button>
            <button onclick={handlePriceDown}>Price -</button>
            
            <h2>Grade System</h2>
            <p>Current Grade: {grade}</p>
            <button onclick={handleGradeChange}>Next Grade</button>
            
            <button onclick={handleReset}>Reset All</button>
        </div>
    );
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
    
    // Register variables for template rendering
    REGISTER_VAR_INT(count);
    REGISTER_VAR_FLOAT(temperature);
    REGISTER_VAR_DOUBLE(price);
    REGISTER_VAR_CHAR(grade);
    
    // Register event handlers
    BIND_EVENT("handleIncrement", handleIncrement);
    BIND_EVENT("handleDecrement", handleDecrement);
    BIND_EVENT("handleReset", handleReset);
    BIND_EVENT("handleTempUp", handleTempUp);
    BIND_EVENT("handleTempDown", handleTempDown);
    BIND_EVENT("handlePriceUp", handlePriceUp);
    BIND_EVENT("handlePriceDown", handlePriceDown);
    BIND_EVENT("handleGradeChange", handleGradeChange);
    
    // Register states with auto-rerender
    USE_STATE(count, onStateChange);
    USE_STATE(temperature, onStateChange);
    USE_STATE(price, onStateChange);
    USE_STATE(grade, onStateChange);
    
    // Initial render
    ReactUI_Render(ui, App());
    ReactUI_CreateWindow(ui, "React-Style UI", 450, 550);
    
    // Message loop
    int result = ReactUI_Run();
    
    // Cleanup
    ReactUI_Destroy(ui);
    
    return result;
}