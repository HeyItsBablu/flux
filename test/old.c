#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_CHILDREN 32
#define MAX_STYLES 16
#define MAX_STATES 16

typedef struct CSSProperty {
    char name[32];
    char value[64];
} CSSProperty;

// --- State Management ---
typedef struct State {
    char name[32];
    int value;
} State;

typedef struct StateManager {
    State states[MAX_STATES];
    int stateCount;
} StateManager;

StateManager globalStateManager = {0};

void setState(const char* name, int value) {
    // Update existing or add new
    for (int i = 0; i < globalStateManager.stateCount; i++) {
        if (strcmp(globalStateManager.states[i].name, name) == 0) {
            globalStateManager.states[i].value = value;
            return;
        }
    }
    
    // Add new state
    if (globalStateManager.stateCount < MAX_STATES) {
        strcpy(globalStateManager.states[globalStateManager.stateCount].name, name);
        globalStateManager.states[globalStateManager.stateCount].value = value;
        globalStateManager.stateCount++;
    }
}

int getState(const char* name) {
    for (int i = 0; i < globalStateManager.stateCount; i++) {
        if (strcmp(globalStateManager.states[i].name, name) == 0) {
            return globalStateManager.states[i].value;
        }
    }
    return 0;
}

typedef struct Node {
    char tag[16];
    char text[256];
    char id[32];
    char class[32];
    char onClick[128];  // NEW: onClick handler
    
    CSSProperty styles[MAX_STYLES];
    int styleCount;
    
    int x, y;
    int width, height;
    int computedWidth, computedHeight;
    
    struct Node* children[MAX_CHILDREN];
    int childCount;
    struct Node* parent;
    
    int isButton;  // NEW: Flag to identify buttons
} Node;

// Forward declarations
void computeLayout(Node* node, int parentWidth, int parentHeight, int offsetX, int offsetY);
void renderNode(HDC hdc, Node* node);
Node* globalRoot = NULL;
HWND globalHwnd = NULL;

// --- Helper functions ---
char* trim(char* str) {
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

// --- Convert color name to RGB ---
COLORREF parseColor(const char* color) {
    if (!color) return RGB(255, 255, 255);
    
    // Hex colors
    if (color[0] == '#') {
        unsigned int r, g, b;
        if (strlen(color) == 7) {
            sscanf(color + 1, "%02x%02x%02x", &r, &g, &b);
            return RGB(r, g, b);
        }
    }
    
    // Named colors
    if (strcmp(color, "red") == 0) return RGB(255, 0, 0);
    if (strcmp(color, "blue") == 0) return RGB(0, 0, 255);
    if (strcmp(color, "green") == 0) return RGB(0, 255, 0);
    if (strcmp(color, "yellow") == 0) return RGB(255, 255, 0);
    if (strcmp(color, "cyan") == 0) return RGB(0, 255, 255);
    if (strcmp(color, "magenta") == 0) return RGB(255, 0, 255);
    if (strcmp(color, "white") == 0) return RGB(255, 255, 255);
    if (strcmp(color, "black") == 0) return RGB(0, 0, 0);
    if (strcmp(color, "gray") == 0 || strcmp(color, "grey") == 0) return RGB(128, 128, 128);
    if (strcmp(color, "lightgray") == 0) return RGB(211, 211, 211);
    if (strcmp(color, "darkgray") == 0) return RGB(169, 169, 169);
    if (strcmp(color, "orange") == 0) return RGB(255, 165, 0);
    if (strcmp(color, "purple") == 0) return RGB(128, 0, 128);
    
    return RGB(255, 255, 255);
}

// --- Node creation ---
Node* createNode(const char* tag) {
    Node* node = (Node*)malloc(sizeof(Node));
    memset(node, 0, sizeof(Node));
    strcpy(node->tag, tag);
    node->width = -1;  // auto
    node->height = -1; // auto
    
    // Check if it's a button
    if (strcmp(tag, "button") == 0) {
        node->isButton = 1;
    }
    
    return node;
}

void addChild(Node* parent, Node* child) {
    if (parent->childCount < MAX_CHILDREN) {
        parent->children[parent->childCount++] = child;
        child->parent = parent;
    }
}

void addStyle(Node* node, const char* name, const char* value) {
    if (node->styleCount < MAX_STYLES) {
        strcpy(node->styles[node->styleCount].name, name);
        strcpy(node->styles[node->styleCount].value, value);
        node->styleCount++;
    }
}

const char* getStyle(Node* node, const char* name) {
    for (int i = 0; i < node->styleCount; i++) {
        if (strcmp(node->styles[i].name, name) == 0) {
            return node->styles[i].value;
        }
    }
    return NULL;
}

// --- CSS Parser ---
void parseInlineStyle(Node* node, const char* styleStr) {
    char buf[512];
    strncpy(buf, styleStr, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    
    char* token = strtok(buf, ";");
    while (token) {
        char* colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            char* name = trim(token);
            char* value = trim(colon + 1);
            addStyle(node, name, value);
        }
        token = strtok(NULL, ";");
    }
}

// --- Variable substitution in text ---
void substituteVariables(char* text, char* output, int maxLen) {
    char* src = text;
    char* dst = output;
    int remaining = maxLen - 1;
    
    while (*src && remaining > 0) {
        if (*src == '{' && *(src + 1) == '{') {
            // Found variable start
            src += 2;
            char varName[32] = {0};
            int i = 0;
            
            // Extract variable name
            while (*src && *src != '}' && i < 31) {
                varName[i++] = *src++;
            }
            varName[i] = '\0';
            
            // Skip closing braces
            if (*src == '}' && *(src + 1) == '}') {
                src += 2;
            }
            
            // Get state value and convert to string
            int value = getState(trim(varName));
            char valueStr[32];
            sprintf(valueStr, "%d", value);
            
            // Copy value to output
            char* v = valueStr;
            while (*v && remaining > 0) {
                *dst++ = *v++;
                remaining--;
            }
        } else {
            *dst++ = *src++;
            remaining--;
        }
    }
    *dst = '\0';
}

// --- HTML Parser ---
const char* parseAttributes(const char* str, Node* node) {
    const char* p = str;
    char attrName[32], attrValue[512];
    
    while (*p && *p != '>' && *p != '/') {
        // Skip whitespace
        while (*p && isspace(*p)) p++;
        if (*p == '>' || *p == '/') break;
        
        // Read attribute name
        int i = 0;
        while (*p && *p != '=' && *p != '>' && *p != '/' && !isspace(*p) && i < 31) {
            attrName[i++] = tolower(*p++);
        }
        attrName[i] = '\0';
        
        if (*p == '=') {
            p++; // skip '='
            
            // Skip whitespace
            while (*p && isspace(*p)) p++;
            
            // Read attribute value
            char quote = 0;
            if (*p == '"' || *p == '\'') {
                quote = *p++;
            }
            
            i = 0;
            if (quote) {
                while (*p && *p != quote && i < 511) {
                    attrValue[i++] = *p++;
                }
                if (*p == quote) p++;
            } else {
                while (*p && !isspace(*p) && *p != '>' && i < 511) {
                    attrValue[i++] = *p++;
                }
            }
            attrValue[i] = '\0';
            
            // Handle specific attributes
            if (strcmp(attrName, "style") == 0) {
                parseInlineStyle(node, attrValue);
            } else if (strcmp(attrName, "id") == 0) {
                strcpy(node->id, attrValue);
            } else if (strcmp(attrName, "class") == 0) {
                strcpy(node->class, attrValue);
            } else if (strcmp(attrName, "onclick") == 0) {
                strcpy(node->onClick, attrValue);
            }
        }
    }
    
    return p;
}

Node* parseHTML(const char** htmlPtr);

Node* parseElement(const char** htmlPtr) {
    const char* p = *htmlPtr;
    
    // Skip to '<'
    while (*p && *p != '<') p++;
    if (!*p) return NULL;
    
    p++; // skip '<'
    
    // Check for closing tag
    if (*p == '/') {
        *htmlPtr = p;
        return NULL;
    }
    
    // Read tag name
    char tag[16];
    int i = 0;
    while (*p && !isspace(*p) && *p != '>' && *p != '/' && i < 15) {
        tag[i++] = tolower(*p++);
    }
    tag[i] = '\0';
    
    if (strlen(tag) == 0) return NULL;
    
    Node* node = createNode(tag);
    
    // Parse attributes
    p = parseAttributes(p, node);
    
    // Check for self-closing tag
    if (*p == '/') {
        p++; // skip '/'
        if (*p == '>') p++;
        *htmlPtr = p;
        return node;
    }
    
    if (*p == '>') p++;
    
    // Parse content and children
    const char* contentStart = p;
    int tagDepth = 1;
    
    while (*p && tagDepth > 0) {
        if (*p == '<') {
            if (*(p+1) == '/') {
                // Closing tag
                const char* closeStart = p + 2;
                char closeTag[16];
                i = 0;
                while (*closeStart && *closeStart != '>' && i < 15) {
                    closeTag[i++] = tolower(*closeStart++);
                }
                closeTag[i] = '\0';
                
                if (strcmp(closeTag, tag) == 0) {
                    // Extract text content before this closing tag
                    if (p > contentStart) {
                        int len = p - contentStart;
                        if (len > 255) len = 255;
                        strncpy(node->text, contentStart, len);
                        node->text[len] = '\0';
                        
                        // Trim text
                        char* trimmed = trim(node->text);
                        if (trimmed != node->text) {
                            memmove(node->text, trimmed, strlen(trimmed) + 1);
                        }
                    }
                    
                    // Skip to end of closing tag
                    while (*p && *p != '>') p++;
                    if (*p == '>') p++;
                    tagDepth--;
                    break;
                }
            } else if (*(p+1) != '!') {
                // Opening tag - parse child
                Node* child = parseElement(&p);
                if (child) {
                    addChild(node, child);
                    contentStart = p; // Reset content start after child
                }
                continue;
            }
        }
        p++;
    }
    
    *htmlPtr = p;
    return node;
}

Node* parseHTML(const char** htmlPtr) {
    Node* root = createNode("root");
    const char* p = *htmlPtr;
    
    while (*p) {
        Node* element = parseElement(&p);
        if (element) {
            addChild(root, element);
        } else {
            break;
        }
        
        // Skip whitespace between elements
        while (*p && isspace(*p)) p++;
    }
    
    *htmlPtr = p;
    return root;
}

// --- Layout Engine ---
void computeLayout(Node* node, int parentWidth, int parentHeight, int offsetX, int offsetY) {
    if (!node) return;
    
    // Get styles
    const char* widthStr = getStyle(node, "width");
    const char* heightStr = getStyle(node, "height");
    
    // Compute dimensions
    if (widthStr) {
        node->computedWidth = atoi(widthStr);
    } else {
        if (node->isButton) {
            node->computedWidth = 120;  // Default button width
        } else {
            node->computedWidth = parentWidth > 0 ? parentWidth - 20 : 200;
        }
    }
    
    if (heightStr) {
        node->computedHeight = atoi(heightStr);
    } else {
        if (node->isButton) {
            node->computedHeight = 35;  // Default button height
        } else {
            // Auto height based on content
            node->computedHeight = 30 + (node->childCount * 40);
        }
    }
    
    // Position
    node->x = offsetX;
    node->y = offsetY;
    
    // Layout children
    int childY = offsetY + 10;
    for (int i = 0; i < node->childCount; i++) {
        computeLayout(node->children[i], node->computedWidth - 20, -1, 
                     offsetX + 10, childY);
        childY += node->children[i]->computedHeight + 5;
    }
}

// --- Event Handler ---
void executeOnClick(const char* onClick) {
    char command[128];
    strncpy(command, onClick, sizeof(command) - 1);
    command[sizeof(command) - 1] = '\0';
    
    // Parse simple commands like: setState('count', getState('count') + 1)
    // Or: count++, count--, count+=5, etc.
    
    if (strstr(command, "++")) {
        // Simple increment: count++
        char varName[32] = {0};
        sscanf(command, "%[^+]", varName);
        char* trimmed = trim(varName);
        int currentValue = getState(trimmed);
        setState(trimmed, currentValue + 1);
    } 
    else if (strstr(command, "--")) {
        // Simple decrement: count--
        char varName[32] = {0};
        sscanf(command, "%[^-]", varName);
        char* trimmed = trim(varName);
        int currentValue = getState(trimmed);
        setState(trimmed, currentValue - 1);
    }
    else if (strstr(command, "+=")) {
        // Add value: count+=5
        char varName[32] = {0};
        int addValue = 0;
        sscanf(command, "%[^+]+=%d", varName, &addValue);
        char* trimmed = trim(varName);
        int currentValue = getState(trimmed);
        setState(trimmed, currentValue + addValue);
    }
    else if (strstr(command, "-=")) {
        // Subtract value: count-=5
        char varName[32] = {0};
        int subValue = 0;
        sscanf(command, "%[^-]-=%d", varName, &subValue);
        char* trimmed = trim(varName);
        int currentValue = getState(trimmed);
        setState(trimmed, currentValue - subValue);
    }
    else if (strstr(command, "=")) {
        // Direct assignment: count=10
        char varName[32] = {0};
        int newValue = 0;
        sscanf(command, "%[^=]=%d", varName, &newValue);
        char* trimmed = trim(varName);
        setState(trimmed, newValue);
    }
    
    // Trigger re-render
    if (globalHwnd) {
        InvalidateRect(globalHwnd, NULL, TRUE);
    }
}

// --- Check if point is inside node ---
Node* findNodeAtPoint(Node* node, int x, int y) {
    if (!node) return NULL;
    
    // Check children first (front to back)
    for (int i = node->childCount - 1; i >= 0; i--) {
        Node* found = findNodeAtPoint(node->children[i], x, y);
        if (found) return found;
    }
    
    // Check this node
    if (node->isButton && 
        x >= node->x && x <= node->x + node->computedWidth &&
        y >= node->y && y <= node->y + node->computedHeight) {
        return node;
    }
    
    return NULL;
}

// --- Renderer ---
void renderNode(HDC hdc, Node* node) {
    if (!node) return;
    
    const char* bgColor = getStyle(node, "background");
    const char* color = getStyle(node, "color");
    const char* fontSize = getStyle(node, "font-size");
    const char* border = getStyle(node, "border");
    
    // Default button styling
    if (node->isButton && !bgColor) {
        bgColor = "#4CAF50";
    }
    if (node->isButton && !color) {
        color = "white";
    }
    
    // Draw background
    if (bgColor) {
        HBRUSH brush = CreateSolidBrush(parseColor(bgColor));
        RECT rect = {node->x, node->y, 
                     node->x + node->computedWidth, 
                     node->y + node->computedHeight};
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
    }
    
    // Draw border (default for buttons)
    if (border || node->isButton) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN oldPen = SelectObject(hdc, pen);
        HBRUSH oldBrush = SelectObject(hdc, GetStockObject(NULL_BRUSH));
        Rectangle(hdc, node->x, node->y, 
                 node->x + node->computedWidth, 
                 node->y + node->computedHeight);
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
    
    // Draw text (with variable substitution)
    if (strlen(node->text) > 0) {
        char processedText[512];
        substituteVariables(node->text, processedText, sizeof(processedText));
        
        SetTextColor(hdc, color ? parseColor(color) : RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        
        // Create font if specified
        HFONT hFont = NULL;
        HFONT hOldFont = NULL;
        if (fontSize) {
            int size = atoi(fontSize);
            hFont = CreateFont(size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
            hOldFont = SelectObject(hdc, hFont);
        } else if (node->isButton) {
            hFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
            hOldFont = SelectObject(hdc, hFont);
        }
        
        RECT textRect = {node->x + 5, node->y + 5, 
                        node->x + node->computedWidth - 5, 
                        node->y + node->computedHeight - 5};
        DrawText(hdc, processedText, -1, &textRect, 
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_WORDBREAK);
        
        if (hFont) {
            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
        }
    }
    
    // Render children
    for (int i = 0; i < node->childCount; i++) {
        renderNode(hdc, node->children[i]);
    }
}

// --- Free memory ---
void freeNode(Node* node) {
    if (!node) return;
    for (int i = 0; i < node->childCount; i++) {
        freeNode(node->children[i]);
    }
    free(node);
}

// --- Window procedure ---
LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_CREATE: {
            globalHwnd = hwnd;
            
            // Initialize states
            setState("count", 0);
            setState("score", 100);
            
            const char* html = 
                "<div style=\"background:#f0f0f0;width:400;height:550;border:1px;\">"
                    "<h1 style=\"color:#333;font-size:28;\">State Management Demo</h1>"
                    
                    "<div style=\"background:#fff;width:360;height:100;border:1px;\">"
                        "<p style=\"color:#666;font-size:18;\">Counter: {{count}}</p>"
                        "<button onclick=\"count++\">Increment</button>"
                        "<button onclick=\"count--\" style=\"background:#f44336;\">Decrement</button>"
                        "<button onclick=\"count=0\" style=\"background:#ff9800;\">Reset</button>"
                    "</div>"
                    
                    "<div style=\"background:#e3f2fd;width:360;height:120;border:1px;\">"
                        "<p style=\"color:#1976d2;font-size:16;\">Score System: {{score}}</p>"
                        "<button onclick=\"score+=10\" style=\"background:#2196f3;\">+10 Points</button>"
                        "<button onclick=\"score-=5\" style=\"background:#ff5722;\">-5 Points</button>"
                        "<button onclick=\"score=100\" style=\"background:#9c27b0;\">Reset Score</button>"
                    "</div>"
                    
                    "<div style=\"background:#c8e6c9;width:360;height:80;border:1px;\">"
                        "<p style=\"color:#2e7d32;font-size:14;\">Total: {{count}} + {{score}} = {{count}}</p>"
                        "<p style=\"color:#555;font-size:12;\">Click buttons to update state!</p>"
                    "</div>"
                "</div>"
                
                "<div style=\"background:#ffe0b2;width:400;height:100;border:1px;\">"
                    "<p style=\"color:#e65100;font-size:14;\">Previous features still work:</p>"
                    "<p style=\"color:#555;font-size:12;\">✓ Nested elements ✓ CSS styling ✓ Colors</p>"
                "</div>";
            
            const char* p = html;
            globalRoot = parseHTML(&p);
            computeLayout(globalRoot, 450, 700, 10, 10);
            return 0;
        }

        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            
            Node* clickedNode = findNodeAtPoint(globalRoot, x, y);
            if (clickedNode && clickedNode->isButton && strlen(clickedNode->onClick) > 0) {
                executeOnClick(clickedNode->onClick);
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Fill background
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            FillRect(hdc, &clientRect, (HBRUSH)(COLOR_WINDOW+1));
            
            renderNode(hdc, globalRoot);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            freeNode(globalRoot);
            PostQuitMessage(0);
            return 0;
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// --- Main function ---
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow) {
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = hInstance;
    wc.lpszClassName = "HTMLRendererWindow";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor = LoadCursor(NULL, IDC_HAND);  // Hand cursor for better UX

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, "HTMLRendererWindow", "HTML Renderer with State Management",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 500, 750,
        NULL, NULL, hInstance, NULL
    );

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}