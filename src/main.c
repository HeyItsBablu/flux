#include "react_ui.h"
#include <windows.h>

void onError(const char* message) {
    MessageBoxA(NULL, message, "ReactUI Error", MB_OK | MB_ICONERROR);
}

int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, 
                   LPSTR lpCmdLine, int nCmdShow) {
    
    // ========================================================================
    // EXAMPLE 1: Simple Counter - Before vs After
    // ========================================================================
    
    // BEFORE: Pure inline styles (524 chars)
    /*
    const char* oldStyle = 
        "<div style=\"display: flex; flex-direction: column; justify-content: center; "
        "align-items: center; width: 400; height: 300; background: #f5f5f5; gap: 20;\">"
            "<div style=\"font-size: 48; color: #1976D2; padding: 20;\">{{counter}}</div>"
            "<div style=\"display: flex; flex-direction: row; gap: 10;\">"
                "<button onclick=\"counter++\" style=\"width: 100; height: 50; "
                "background: #4CAF50; color: white; font-size: 18; border-radius: 5;\">+</button>"
                "<button onclick=\"counter--\" style=\"width: 100; height: 50; "
                "background: #f44336; color: white; font-size: 18; border-radius: 5;\">-</button>"
                "<button onclick=\"counter=0\" style=\"width: 100; height: 50; "
                "background: #FF9800; color: white; font-size: 18; border-radius: 5;\">Reset</button>"
            "</div>"
        "</div>";
    */
    
    // AFTER: Utility classes (174 chars - 67% reduction!)
    const char* html1 = 
        "<div class=\"flex-col-center bg-gray-100 gap-20\" style=\"width: 400; height: 300;\">"
            "<div class=\"text-5xl text-blue-600 p-20\">{{counter}}</div>"
            "<div class=\"flex-row gap-10\">"
                "<button class=\"btn-success rounded\" onclick=\"counter++\" style=\"width: 100;\">+</button>"
                "<button class=\"btn-danger rounded\" onclick=\"counter--\" style=\"width: 100;\">-</button>"
                "<button class=\"btn-warning rounded\" onclick=\"counter=0\" style=\"width: 100;\">Reset</button>"
            "</div>"
        "</div>";
    
    ReactUI* ui1 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui1, onError);
    ReactUI_SetState(ui1, "counter", 0);
    ReactUI_Render(ui1, html1);
    ReactUI_CreateWindow(ui1, "Example 1: Simple Counter (Utility Classes)", 450, 400);
    
    
    // ========================================================================
    // EXAMPLE 2: Button Variations
    // ========================================================================
    
    const char* html2 = 
        "<div class=\"flex-col-center bg-gray-50 gap-15 p-20\" style=\"width: 600; height: 500;\">"
            "<div class=\"text-2xl text-gray-700 p-10\">Button Component Styles</div>"
            
            "<!-- Primary Buttons -->"
            "<div class=\"flex-row gap-10\">"
                "<button class=\"btn-primary\">Primary</button>"
                "<button class=\"btn-secondary\">Secondary</button>"
                "<button class=\"btn-success\">Success</button>"
            "</div>"
            
            "<!-- Danger/Warning Buttons -->"
            "<div class=\"flex-row gap-10\">"
                "<button class=\"btn-danger\">Danger</button>"
                "<button class=\"btn-warning\">Warning</button>"
                "<button class=\"btn bg-info text-white\">Info</button>"
            "</div>"
            
            "<!-- Size Variations -->"
            "<div class=\"flex-row gap-10\">"
                "<button class=\"btn-primary btn-sm\">Small</button>"
                "<button class=\"btn-primary\">Regular</button>"
                "<button class=\"btn-primary btn-lg\">Large</button>"
            "</div>"
            
            "<!-- Rounded Variations -->"
            "<div class=\"flex-row gap-10\">"
                "<button class=\"btn-success rounded\">Rounded</button>"
                "<button class=\"btn-success rounded-lg\">Rounded-LG</button>"
                "<button class=\"btn-success rounded-full\">Pill</button>"
            "</div>"
            
            "<!-- With Icons/Text -->"
            "<div class=\"flex-row gap-10\">"
                "<button class=\"btn-primary\" style=\"width: 150;\">Download</button>"
                "<button class=\"btn-secondary\" style=\"width: 150;\">Upload</button>"
            "</div>"
        "</div>";
    
    ReactUI* ui2 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui2, onError);
    ReactUI_Render(ui2, html2);
    ReactUI_CreateWindow(ui2, "Example 2: Button Variations", 650, 600);
    
    
    // ========================================================================
    // EXAMPLE 3: Dashboard Layout (Complex)
    // ========================================================================
    
    const char* html3 = 
        "<div class=\"flex-col gap-10\" style=\"width: 800; height: 600;\">"
            
            "<!-- Header -->"
            "<div class=\"flex-row-between bg-blue-700 text-white p-10\" style=\"height: 60;\">"
                "<div class=\"text-2xl p-10\">Dashboard</div>"
                "<div class=\"text-lg p-10\">Score: {{total}}</div>"
            "</div>"
            
            "<!-- Main Content Area -->"
            "<div class=\"flex-row flex-1 gap-10\">"
                
                "<!-- Left Sidebar -->"
                "<div class=\"flex-col bg-blue-50 gap-10 p-10\" style=\"width: 200;\">"
                    "<div class=\"text-base text-gray-700 p-5\">Add Points</div>"
                    "<button class=\"btn-primary\" onclick=\"total+=100\">+100</button>"
                    "<button class=\"btn-primary\" onclick=\"total+=50\">+50</button>"
                    "<button class=\"btn-primary\" onclick=\"total+=10\">+10</button>"
                "</div>"
                
                "<!-- Center Content -->"
                "<div class=\"flex-col-center flex-1 card gap-20\">"
                    "<div class=\"text-5xl text-blue-600\">{{total}}</div>"
                    "<div class=\"text-xl text-gray-600\">Total Points</div>"
                    "<div class=\"flex-row gap-10\">"
                        "<button class=\"btn-success\" onclick=\"total+=5\">+5</button>"
                        "<button class=\"btn-danger\" onclick=\"total-=5\">-5</button>"
                    "</div>"
                    "<button class=\"btn-warning\" onclick=\"total=0\" style=\"width: 200;\">Reset All</button>"
                "</div>"
                
                "<!-- Right Sidebar -->"
                "<div class=\"flex-col bg-red-50 gap-10 p-10\" style=\"width: 200;\">"
                    "<div class=\"text-base text-gray-700 p-5\">Subtract</div>"
                    "<button class=\"btn-danger\" onclick=\"total-=10\">-10</button>"
                    "<button class=\"btn-danger\" onclick=\"total-=50\">-50</button>"
                    "<button class=\"btn-danger\" onclick=\"total-=100\">-100</button>"
                "</div>"
                
            "</div>"
            
            "<!-- Footer -->"
            "<div class=\"flex-row-center bg-gray-300 text-black p-10\" style=\"height: 50;\">"
                "<div class=\"text-sm\">ReactUI Hybrid - Utility Classes Demo</div>"
            "</div>"
            
        "</div>";
    
    ReactUI* ui3 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui3, onError);
    ReactUI_SetState(ui3, "total", 0);
    ReactUI_Render(ui3, html3);
    ReactUI_CreateWindow(ui3, "Example 3: Dashboard Layout", 850, 700);
    
    
    // ========================================================================
    // EXAMPLE 4: Card Components
    // ========================================================================
    
    const char* html4 = 
        "<div class=\"flex-row-center bg-gray-100 gap-20 p-20\" style=\"width: 900; height: 400;\">"
            
            "<!-- Card 1: User Profile -->"
            "<div class=\"card\" style=\"width: 250; height: 300;\">"
                "<div class=\"flex-col-center gap-10\">"
                    "<div class=\"bg-blue-500 text-white rounded-full\" "
                         "style=\"width: 80; height: 80; padding: 25;\">User</div>"
                    "<div class=\"text-xl text-gray-700\">John Doe</div>"
                    "<div class=\"text-sm text-gray-600\">Software Developer</div>"
                    "<button class=\"btn-primary\" style=\"width: 200;\">View Profile</button>"
                "</div>"
            "</div>"
            
            "<!-- Card 2: Statistics -->"
            "<div class=\"card\" style=\"width: 250; height: 300;\">"
                "<div class=\"card-header text-lg text-gray-700\">Statistics</div>"
                "<div class=\"flex-col gap-10 p-10\">"
                    "<div class=\"flex-row-between\">"
                        "<div class=\"text-sm text-gray-600\">Views</div>"
                        "<div class=\"text-base text-blue-600\">{{views}}</div>"
                    "</div>"
                    "<div class=\"flex-row-between\">"
                        "<div class=\"text-sm text-gray-600\">Likes</div>"
                        "<div class=\"text-base text-green-600\">{{likes}}</div>"
                    "</div>"
                    "<div class=\"flex-row-between\">"
                        "<div class=\"text-sm text-gray-600\">Shares</div>"
                        "<div class=\"text-base text-orange-500\">{{shares}}</div>"
                    "</div>"
                    "<button class=\"btn-secondary btn-sm\" onclick=\"views++\">+View</button>"
                "</div>"
            "</div>"
            
            "<!-- Card 3: Actions -->"
            "<div class=\"card\" style=\"width: 250; height: 300;\">"
                "<div class=\"card-header text-lg text-gray-700\">Quick Actions</div>"
                "<div class=\"flex-col gap-10 p-10\">"
                    "<button class=\"btn-success\">Create New</button>"
                    "<button class=\"btn-secondary\">Edit</button>"
                    "<button class=\"btn-warning\">Archive</button>"
                    "<button class=\"btn-danger\">Delete</button>"
                "</div>"
            "</div>"
            
        "</div>";
    
    ReactUI* ui4 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui4, onError);
    ReactUI_SetState(ui4, "views", 1234);
    ReactUI_SetState(ui4, "likes", 567);
    ReactUI_SetState(ui4, "shares", 89);
    ReactUI_Render(ui4, html4);
    ReactUI_CreateWindow(ui4, "Example 4: Card Components", 950, 500);
    
    
    // ========================================================================
    // EXAMPLE 5: Spacing & Layout Demo
    // ========================================================================
    
    const char* html5 = 
        "<div class=\"flex-col gap-20 p-20\" style=\"width: 700; height: 600;\">"
            
            "<div class=\"text-2xl text-gray-700\">Spacing & Layout Utilities</div>"
            
            "<!-- Gap Variations -->"
            "<div class=\"card\">"
                "<div class=\"card-header text-base\">Gap Utilities</div>"
                "<div class=\"flex-col p-10\">"
                    "<div class=\"flex-row gap-5 bg-gray-100 p-5\">"
                        "<div class=\"bg-blue-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"bg-blue-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"bg-blue-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"text-sm text-gray-600 p-10\">gap-5</div>"
                    "</div>"
                    "<div class=\"flex-row gap-10 bg-gray-100 p-5\">"
                        "<div class=\"bg-green-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"bg-green-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"bg-green-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"text-sm text-gray-600 p-10\">gap-10</div>"
                    "</div>"
                    "<div class=\"flex-row gap-20 bg-gray-100 p-5\">"
                        "<div class=\"bg-red-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"bg-red-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"bg-red-500\" style=\"width: 50; height: 50;\"></div>"
                        "<div class=\"text-sm text-gray-600 p-10\">gap-20</div>"
                    "</div>"
                "</div>"
            "</div>"
            
            "<!-- Padding Variations -->"
            "<div class=\"card\">"
                "<div class=\"card-header text-base\">Padding Utilities</div>"
                "<div class=\"flex-row gap-10 p-10\">"
                    "<div class=\"bg-blue-100 border p-5\">"
                        "<div class=\"bg-blue-500 text-white text-sm\">p-5</div>"
                    "</div>"
                    "<div class=\"bg-green-100 border p-10\">"
                        "<div class=\"bg-green-500 text-white text-sm\">p-10</div>"
                    "</div>"
                    "<div class=\"bg-red-100 border p-20\">"
                        "<div class=\"bg-red-500 text-white text-sm\">p-20</div>"
                    "</div>"
                "</div>"
            "</div>"
            
            "<!-- Flex Grow Demo -->"
            "<div class=\"card\">"
                "<div class=\"card-header text-base\">Flex Grow</div>"
                "<div class=\"flex-row gap-10 p-10\">"
                    "<div class=\"flex-1 bg-blue-500 text-white p-10\">flex-1</div>"
                    "<div class=\"flex-2 bg-green-500 text-white p-10\">flex-2</div>"
                    "<div class=\"flex-1 bg-red-500 text-white p-10\">flex-1</div>"
                "</div>"
            "</div>"
            
        "</div>";
    
    ReactUI* ui5 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui5, onError);
    ReactUI_Render(ui5, html5);
    ReactUI_CreateWindow(ui5, "Example 5: Spacing & Layout", 750, 700);
    
    
    // ========================================================================
    // EXAMPLE 6: Color Palette Showcase
    // ========================================================================
    
    const char* html6 = 
        "<div class=\"flex-col gap-15 p-20\" style=\"width: 600; height: 700;\">"
            
            "<div class=\"text-2xl text-gray-700\">Color Palette</div>"
            
            "<!-- Semantic Colors -->"
            "<div class=\"card\">"
                "<div class=\"card-header text-base\">Semantic Colors</div>"
                "<div class=\"flex-row gap-10 p-10\">"
                    "<button class=\"btn-primary\">Primary</button>"
                    "<button class=\"btn-secondary\">Secondary</button>"
                    "<button class=\"btn-success\">Success</button>"
                    "<button class=\"btn-danger\">Danger</button>"
                    "<button class=\"btn-warning\">Warning</button>"
                "</div>"
            "</div>"
            
            "<!-- Blue Shades -->"
            "<div class=\"card\">"
                "<div class=\"card-header text-base\">Blue Shades</div>"
                "<div class=\"flex-row gap-10 p-10\">"
                    "<div class=\"bg-blue-50 border\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">50</div>"
                    "</div>"
                    "<div class=\"bg-blue-500 text-white\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">500</div>"
                    "</div>"
                    "<div class=\"bg-blue-600 text-white\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">600</div>"
                    "</div>"
                    "<div class=\"bg-blue-700 text-white\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">700</div>"
                    "</div>"
                "</div>"
            "</div>"
            
            "<!-- Green Shades -->"
            "<div class=\"card\">"
                "<div class=\"card-header text-base\">Green Shades</div>"
                "<div class=\"flex-row gap-10 p-10\">"
                    "<div class=\"bg-green-50 border\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">50</div>"
                    "</div>"
                    "<div class=\"bg-green-500 text-white\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">500</div>"
                    "</div>"
                    "<div class=\"bg-green-600 text-white\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">600</div>"
                    "</div>"
                "</div>"
            "</div>"
            
            "<!-- Gray Shades -->"
            "<div class=\"card\">"
                "<div class=\"card-header text-base\">Gray Shades</div>"
                "<div class=\"flex-row gap-10 p-10\">"
                    "<div class=\"bg-gray-50 border\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">50</div>"
                    "</div>"
                    "<div class=\"bg-gray-100 border\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">100</div>"
                    "</div>"
                    "<div class=\"bg-gray-200 border\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">200</div>"
                    "</div>"
                    "<div class=\"bg-gray-300 text-white\" style=\"width: 80; height: 60; padding: 5;\">"
                        "<div class=\"text-xs\">300</div>"
                    "</div>"
                "</div>"
            "</div>"
            
            "<!-- Text Colors -->"
            "<div class=\"card\">"
                "<div class=\"card-header text-base\">Text Colors</div>"
                "<div class=\"flex-col gap-5 p-10\">"
                    "<div class=\"text-blue-600\">Blue Text</div>"
                    "<div class=\"text-green-600\">Green Text</div>"
                    "<div class=\"text-red-600\">Red Text</div>"
                    "<div class=\"text-gray-600\">Gray Text</div>"
                "</div>"
            "</div>"
            
        "</div>";
    
    ReactUI* ui6 = ReactUI_Create(hInstance);
    ReactUI_SetErrorCallback(ui6, onError);
    ReactUI_Render(ui6, html6);
    ReactUI_CreateWindow(ui6, "Example 6: Color Palette", 650, 800);
    
    
    // Run message loop
    return ReactUI_Run();
}