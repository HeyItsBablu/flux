#include "react_ui_improved.h"
#include <windows.h>

// Error callback function
void onError(const char* message) {
    MessageBoxA(NULL, message, "ReactUI Error", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    
    // Example 1: Flexbox Row Layout with Space Between
    const char* flexRowHTML = 
        "<div style=\"display: flex; flex-direction: row; justify-content: space-between; "
        "align-items: center; width: 700; height: 100; background: #f0f0f0; "
        "border: 2px solid #333; border-radius: 10; gap: 10;\">"
            "<button onclick=\"count++\" style=\"flex-grow: 1; background: #2196F3; "
            "color: white; font-size: 18;\">Increment</button>"
            "<div style=\"flex-grow: 2; background: white; border: 1px solid #ccc; "
            "padding: 10; font-size: 20; color: #333;\">Count: {{count}}</div>"
            "<button onclick=\"count--\" style=\"flex-grow: 1; background: #f44336; "
            "color: white; font-size: 18;\">Decrement</button>"
        "</div>";
    
    ReactUI* ui1 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui1, onError);
    ReactUI_SetState(ui1, "count", 0);
    ReactUI_Render(ui1, flexRowHTML);
    ReactUI_CreateWindow(ui1, "Flexbox Row Layout - Space Between", 750, 180);
    
    
    // Example 2: Flexbox Column Layout with Center Alignment
    const char* flexColumnHTML = 
        "<div style=\"display: flex; flex-direction: column; justify-content: center; "
        "align-items: center; width: 400; height: 400; background: #e8f5e9; gap: 15;\">"
            "<div style=\"background: #4CAF50; color: white; padding: 20; "
            "border-radius: 8; width: 300; font-size: 24;\">Score: {{score}}</div>"
            "<button onclick=\"score+=10\" style=\"width: 300; height: 50; "
            "background: #66BB6A; color: white; font-size: 18; border-radius: 5;\">+10 Points</button>"
            "<button onclick=\"score+=5\" style=\"width: 300; height: 50; "
            "background: #81C784; color: white; font-size: 18; border-radius: 5;\">+5 Points</button>"
            "<button onclick=\"score=0\" style=\"width: 300; height: 50; "
            "background: #FF5722; color: white; font-size: 18; border-radius: 5;\">Reset</button>"
        "</div>";
    
    ReactUI* ui2 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui2, onError);
    ReactUI_SetState(ui2, "score", 0);
    ReactUI_Render(ui2, flexColumnHTML);
    ReactUI_CreateWindow(ui2, "Flexbox Column Layout - Centered", 450, 500);
    
    
    // Example 3: Nested Flexbox - Dashboard Layout
    const char* dashboardHTML = 
        "<div style=\"display: flex; flex-direction: column; width: 800; height: 500; "
        "background: #fafafa; gap: 10;\">"
            
            "<!-- Header -->"
            "<div style=\"display: flex; flex-direction: row; justify-content: space-between; "
            "background: #1976D2; color: white; padding: 10; height: 60; align-items: center;\">"
                "<div style=\"font-size: 24; padding: 10;\">Dashboard</div>"
                "<div style=\"font-size: 18; padding: 10;\">User Score: {{total}}</div>"
            "</div>"
            
            "<!-- Main Content -->"
            "<div style=\"display: flex; flex-direction: row; flex-grow: 1; gap: 10;\">"
                
                "<!-- Left Sidebar -->"
                "<div style=\"display: flex; flex-direction: column; width: 200; "
                "background: #E3F2FD; gap: 10; padding: 10;\">"
                    "<button onclick=\"total+=100\" style=\"background: #42A5F5; "
                    "color: white; height: 50; border-radius: 5;\">+100</button>"
                    "<button onclick=\"total+=50\" style=\"background: #64B5F6; "
                    "color: white; height: 50; border-radius: 5;\">+50</button>"
                    "<button onclick=\"total+=10\" style=\"background: #90CAF9; "
                    "color: white; height: 50; border-radius: 5;\">+10</button>"
                "</div>"
                
                "<!-- Center Content -->"
                "<div style=\"display: flex; flex-direction: column; flex-grow: 1; "
                "justify-content: space-around; align-items: center; background: white; "
                "border: 2px solid #E0E0E0; border-radius: 8;\">"
                    "<div style=\"font-size: 48; color: #1976D2; padding: 20;\">{{total}}</div>"
                    "<div style=\"font-size: 20; color: #666;\">Total Points</div>"
                    "<button onclick=\"total=0\" style=\"background: #F44336; color: white; "
                    "width: 200; height: 50; border-radius: 5; font-size: 18;\">Reset All</button>"
                "</div>"
                
                "<!-- Right Sidebar -->"
                "<div style=\"display: flex; flex-direction: column; width: 200; "
                "background: #FFF3E0; gap: 10; padding: 10;\">"
                    "<button onclick=\"total-=10\" style=\"background: #FF9800; "
                    "color: white; height: 50; border-radius: 5;\">-10</button>"
                    "<button onclick=\"total-=50\" style=\"background: #FB8C00; "
                    "color: white; height: 50; border-radius: 5;\">-50</button>"
                    "<button onclick=\"total-=100\" style=\"background: #F57C00; "
                    "color: white; height: 50; border-radius: 5;\">-100</button>"
                "</div>"
                
            "</div>"
            
            "<!-- Footer -->"
            "<div style=\"background: #455A64; color: white; padding: 10; height: 40; "
            "display: flex; justify-content: center; align-items: center;\">"
                "<div style=\"font-size: 14;\">ReactUI Dashboard Example - Flexbox Layout</div>"
            "</div>"
            
        "</div>";
    
    ReactUI* ui3 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui3, onError);
    ReactUI_SetState(ui3, "total", 0);
    ReactUI_Render(ui3, dashboardHTML);
    ReactUI_CreateWindow(ui3, "Flexbox Dashboard - Nested Layout", 850, 600);
    
    
    // Example 4: Flex-Grow Demonstration
    const char* flexGrowHTML = 
        "<div style=\"display: flex; flex-direction: row; width: 700; height: 100; "
        "background: #f5f5f5; gap: 10; padding: 10;\">"
            "<div style=\"flex-grow: 1; background: #FF6B6B; color: white; "
            "padding: 10; border-radius: 5; font-size: 16;\">Grow: 1</div>"
            "<div style=\"flex-grow: 2; background: #4ECDC4; color: white; "
            "padding: 10; border-radius: 5; font-size: 16;\">Grow: 2</div>"
            "<div style=\"flex-grow: 1; background: #95E1D3; color: white; "
            "padding: 10; border-radius: 5; font-size: 16;\">Grow: 1</div>"
            "<div style=\"flex-grow: 3; background: #F38181; color: white; "
            "padding: 10; border-radius: 5; font-size: 16;\">Grow: 3</div>"
        "</div>";
    
    ReactUI* ui4 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui4, onError);
    ReactUI_Render(ui4, flexGrowHTML);
    ReactUI_CreateWindow(ui4, "Flex-Grow Distribution", 750, 180);
    
    
    // Example 5: Different Justify Content Options
    const char* justifyHTML = 
        "<div style=\"display: flex; flex-direction: column; width: 600; height: 500; gap: 15;\">"
            
            "<div style=\"display: flex; flex-direction: row; justify-content: flex-start; "
            "background: #E8EAF6; height: 80; gap: 10; padding: 10;\">"
                "<button style=\"width: 100; background: #3F51B5; color: white;\">Start 1</button>"
                "<button style=\"width: 100; background: #3F51B5; color: white;\">Start 2</button>"
                "<button style=\"width: 100; background: #3F51B5; color: white;\">Start 3</button>"
            "</div>"
            
            "<div style=\"display: flex; flex-direction: row; justify-content: center; "
            "background: #F3E5F5; height: 80; gap: 10; padding: 10;\">"
                "<button style=\"width: 100; background: #9C27B0; color: white;\">Center 1</button>"
                "<button style=\"width: 100; background: #9C27B0; color: white;\">Center 2</button>"
                "<button style=\"width: 100; background: #9C27B0; color: white;\">Center 3</button>"
            "</div>"
            
            "<div style=\"display: flex; flex-direction: row; justify-content: flex-end; "
            "background: #E0F2F1; height: 80; gap: 10; padding: 10;\">"
                "<button style=\"width: 100; background: #009688; color: white;\">End 1</button>"
                "<button style=\"width: 100; background: #009688; color: white;\">End 2</button>"
                "<button style=\"width: 100; background: #009688; color: white;\">End 3</button>"
            "</div>"
            
            "<div style=\"display: flex; flex-direction: row; justify-content: space-between; "
            "background: #FFF3E0; height: 80; gap: 10; padding: 10;\">"
                "<button style=\"width: 100; background: #FF9800; color: white;\">Between 1</button>"
                "<button style=\"width: 100; background: #FF9800; color: white;\">Between 2</button>"
                "<button style=\"width: 100; background: #FF9800; color: white;\">Between 3</button>"
            "</div>"
            
            "<div style=\"display: flex; flex-direction: row; justify-content: space-around; "
            "background: #FFEBEE; height: 80; gap: 10; padding: 10;\">"
                "<button style=\"width: 100; background: #F44336; color: white;\">Around 1</button>"
                "<button style=\"width: 100; background: #F44336; color: white;\">Around 2</button>"
                "<button style=\"width: 100; background: #F44336; color: white;\">Around 3</button>"
            "</div>"
            
        "</div>";
    
    ReactUI* ui5 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui5, onError);
    ReactUI_Render(ui5, justifyHTML);
    ReactUI_CreateWindow(ui5, "Justify-Content Options", 650, 600);
    
    
    // Run message loop
    return ReactUI_Run();
}