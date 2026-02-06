#include <windows.h>
#include "react_ui.h"

// Example 1: Simple Counter
void example_counter(HINSTANCE hInstance) {
    ReactUI* ui = ReactUI_Create(hInstance);
    
    ReactUI_SetState(ui, "counter", 0);
    
    const char* html = HTML(
        <div class='flex-col-center card' style='width: 400; height: 300;'>
            <div class='text-3xl text-blue-700'>Counter Application</div>
            <div class='text-2xl text-gray-700'>Count: {{counter}}</div>
            <div class='flex-row gap-10'>
                <button class='btn-success' onclick='counter++'>Increment</button>
                <button class='btn-danger' onclick='counter--'>Decrement</button>
                <button class='btn-warning' onclick='counter=0'>Reset</button>
            </div>
        </div>
    );
    
    ReactUI_Render(ui, html);
    ReactUI_CreateWindow(ui, "Counter App", 450, 350);
}

// Example 2: Dashboard
void example_dashboard(HINSTANCE hInstance) {
    ReactUI* ui = ReactUI_Create(hInstance);
    
    ReactUI_SetState(ui, "users", 150);
    ReactUI_SetState(ui, "sessions", 42);
    ReactUI_SetState(ui, "errors", 0);
    ReactUI_SetState(ui, "uptime", 99);
    
    const char* html = HTML(
        <div class='flex-col gap-20 p-20 bg-gray-50' style='width: 800; height: 500;'>
            <div class='text-4xl text-center text-blue-700'>System Dashboard</div>
            
            <div class='flex-row gap-15'>
                <div class='card flex-1 flex-col-center'>
                    <div class='text-sm text-gray-600'>Total Users</div>
                    <div class='text-3xl text-green-600'>{{users}}</div>
                </div>
                
                <div class='card flex-1 flex-col-center'>
                    <div class='text-sm text-gray-600'>Active Sessions</div>
                    <div class='text-3xl text-blue-600'>{{sessions}}</div>
                </div>
                
                <div class='card flex-1 flex-col-center'>
                    <div class='text-sm text-gray-600'>Errors</div>
                    <div class='text-3xl text-red-600'>{{errors}}</div>
                </div>
                
                <div class='card flex-1 flex-col-center'>
                    <div class='text-sm text-gray-600'>Uptime %</div>
                    <div class='text-3xl text-purple-500'>{{uptime}}</div>
                </div>
            </div>
            
            <div class='flex-row-center gap-10'>
                <button class='btn-success btn-lg' onclick='users++'>New User</button>
                <button class='btn-warning btn-lg' onclick='errors++'>Log Error</button>
                <button class='btn-danger btn-lg' onclick='sessions=0'>Clear Sessions</button>
            </div>
        </div>
    );
    
    ReactUI_Render(ui, html);
    ReactUI_CreateWindow(ui, "Dashboard", 850, 550);
}

// Example 3: Calculator
void example_calculator(HINSTANCE hInstance) {
    ReactUI* ui = ReactUI_Create(hInstance);
    
    ReactUI_SetState(ui, "result", 0);
    
    const char* html = HTML(
        <div class='flex-col gap-15 p-20 card' style='width: 350; height: 450;'>
            <div class='text-3xl text-center text-blue-700'>Calculator</div>
            
            <div class='card-header text-4xl text-center text-gray-700'>
                {{result}}
            </div>
            
            <div class='flex-col gap-10'>
                <div class='flex-row gap-10'>
                    <button class='btn-primary flex-1' onclick='result+=1'>1</button>
                    <button class='btn-primary flex-1' onclick='result+=2'>2</button>
                    <button class='btn-primary flex-1' onclick='result+=3'>3</button>
                </div>
                
                <div class='flex-row gap-10'>
                    <button class='btn-primary flex-1' onclick='result+=4'>4</button>
                    <button class='btn-primary flex-1' onclick='result+=5'>5</button>
                    <button class='btn-primary flex-1' onclick='result+=6'>6</button>
                </div>
                
                <div class='flex-row gap-10'>
                    <button class='btn-primary flex-1' onclick='result+=7'>7</button>
                    <button class='btn-primary flex-1' onclick='result+=8'>8</button>
                    <button class='btn-primary flex-1' onclick='result+=9'>9</button>
                </div>
                
                <div class='flex-row gap-10'>
                    <button class='btn-danger flex-1' onclick='result=0'>C</button>
                    <button class='btn-primary flex-1' onclick='result+=0'>0</button>
                    <button class='btn-success flex-1' onclick='result+=10'>+10</button>
                </div>
            </div>
        </div>
    );
    
    ReactUI_Render(ui, html);
    ReactUI_CreateWindow(ui, "Calculator", 400, 500);
}

// Example 4: Todo Counter
void example_todo(HINSTANCE hInstance) {
    ReactUI* ui = ReactUI_Create(hInstance);
    
    ReactUI_SetState(ui, "total", 0);
    ReactUI_SetState(ui, "completed", 0);
    ReactUI_SetState(ui, "pending", 0);
    
    const char* html = HTML(
        <div class='flex-col gap-20 p-20 bg-blue-50' style='width: 500; height: 400;'>
            <div class='text-4xl text-center text-blue-700'>Todo Tracker</div>
            
            <div class='flex-row gap-15'>
                <div class='card flex-1 flex-col-center gap-10'>
                    <div class='text-sm text-gray-600'>Total Tasks</div>
                    <div class='text-3xl text-blue-600'>{{total}}</div>
                </div>
                
                <div class='card flex-1 flex-col-center gap-10'>
                    <div class='text-sm text-gray-600'>Completed</div>
                    <div class='text-3xl text-green-600'>{{completed}}</div>
                </div>
                
                <div class='card flex-1 flex-col-center gap-10'>
                    <div class='text-sm text-gray-600'>Pending</div>
                    <div class='text-3xl text-orange-500'>{{pending}}</div>
                </div>
            </div>
            
            <div class='flex-col gap-10'>
                <button class='btn-success btn-lg' onclick='total++'>Add Task</button>
                <div class='flex-row gap-10'>
                    <button class='btn-primary flex-1' onclick='completed++'>Complete Task</button>
                    <button class='btn-warning flex-1' onclick='pending++'>Add Pending</button>
                </div>
                <button class='btn-danger' onclick='total=0'>Clear All</button>
            </div>
        </div>
    );
    
    ReactUI_Render(ui, html);
    ReactUI_CreateWindow(ui, "Todo Tracker", 550, 450);
}

// Example 5: Settings Panel
void example_settings(HINSTANCE hInstance) {
    ReactUI* ui = ReactUI_Create(hInstance);
    
    ReactUI_SetState(ui, "volume", 50);
    ReactUI_SetState(ui, "brightness", 75);
    ReactUI_SetState(ui, "notifications", 1);
    
    const char* html = HTML(
        <div class='flex-col gap-20 p-20' style='width: 450; height: 450;'>
            <div class='card-header text-3xl text-center'>Settings</div>
            
            <div class='card flex-col gap-15'>
                <div class='flex-row-between'>
                    <div class='text-lg'>Volume:</div>
                    <div class='text-xl text-blue-600'>{{volume}}%</div>
                </div>
                <div class='flex-row gap-10'>
                    <button class='btn-secondary flex-1' onclick='volume-=10'>-10</button>
                    <button class='btn-secondary flex-1' onclick='volume+=10'>+10</button>
                </div>
            </div>
            
            <div class='card flex-col gap-15'>
                <div class='flex-row-between'>
                    <div class='text-lg'>Brightness:</div>
                    <div class='text-xl text-blue-600'>{{brightness}}%</div>
                </div>
                <div class='flex-row gap-10'>
                    <button class='btn-secondary flex-1' onclick='brightness-=10'>-10</button>
                    <button class='btn-secondary flex-1' onclick='brightness+=10'>+10</button>
                </div>
            </div>
            
            <div class='card flex-col gap-15'>
                <div class='flex-row-between'>
                    <div class='text-lg'>Notifications:</div>
                    <div class='text-xl text-blue-600'>{{notifications}}</div>
                </div>
                <div class='flex-row gap-10'>
                    <button class='btn-success flex-1' onclick='notifications=1'>On</button>
                    <button class='btn-danger flex-1' onclick='notifications=0'>Off</button>
                </div>
            </div>
        </div>
    );
    
    ReactUI_Render(ui, html);
    ReactUI_CreateWindow(ui, "Settings", 500, 500);
}

// Main entry point
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    
    // Choose which example to run:
    // Uncomment one of the following lines:
    
    example_counter(hInstance);
    // example_dashboard(hInstance);
    // example_calculator(hInstance);
    // example_todo(hInstance);
    // example_settings(hInstance);
    
    return ReactUI_Run();
}