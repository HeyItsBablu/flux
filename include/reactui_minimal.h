#ifndef REACTUI_MINIMAL_H
#define REACTUI_MINIMAL_H

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

// ============================================================================
// SIMPLE HTML MACRO
// ============================================================================

#define HTML(...) #__VA_ARGS__

// ============================================================================
// LIMITS
// ============================================================================

#define MAX_CHILDREN 32
#define MAX_TEXT_LENGTH 256
#define MAX_TAG_LENGTH 16
#define MAX_ATTRIBUTE_LENGTH 64
#define MAX_STATES 64
#define MAX_EVENT_HANDLERS 32

// ============================================================================
// EVENT HANDLER TYPES
// ============================================================================

typedef void (*ClickHandler)(void);

typedef struct EventHandler {
    char elementId[MAX_ATTRIBUTE_LENGTH];
    ClickHandler onClick;
} EventHandler;

typedef struct EventManager {
    EventHandler handlers[MAX_EVENT_HANDLERS];
    int handlerCount;
} EventManager;

static EventManager g_eventManager = {0};

// ============================================================================
// CORE NODE STRUCTURE (Minimal)
// ============================================================================

typedef struct Node {
    char tag[MAX_TAG_LENGTH];           // "div", "p", "button", etc.
    char text[MAX_TEXT_LENGTH];         // Text content
    char id[MAX_ATTRIBUTE_LENGTH];      // Element ID for event binding
    
    // Basic properties
    int x, y;                           // Position
    int width, height;                  // Size
    
    // Event handler
    ClickHandler onClick;
    
    // Tree structure
    struct Node* children[MAX_CHILDREN];
    int childCount;
    struct Node* parent;
    
} Node;

// ============================================================================
// UI CONTEXT
// ============================================================================

typedef struct ReactUI {
    Node* root;
    HWND hwnd;
    HINSTANCE hInstance;
} ReactUI;

// ============================================================================
// GLOBAL UI INSTANCE
// ============================================================================

static ReactUI* g_ui = NULL;

// ============================================================================
// STATE MANAGEMENT (Primitive Types Only)
// ============================================================================

typedef struct StateEntry {
    void* value;
    size_t size;
    void (*onChange)(void);
    bool active;
} StateEntry;

typedef struct StateManager {
    StateEntry states[MAX_STATES];
    int stateCount;
} StateManager;

static StateManager g_stateManager = {0};

// STATE macro for primitive types (int, float, double, char, etc.)
#define STATE(type, var, setter, initial)                       \
    static type var = initial;                                  \
    static int var##_state_id = -1;                            \
    void setter(type value) {                                   \
        if (memcmp(&var, &value, sizeof(type)) != 0) {         \
            var = value;                                        \
            if (var##_state_id >= 0 &&                         \
                g_stateManager.states[var##_state_id].onChange) { \
                g_stateManager.states[var##_state_id].onChange(); \
            }                                                   \
        }                                                       \
    }                                                           \
    static void var##_register_state() {                       \
        if (var##_state_id < 0 &&                              \
            g_stateManager.stateCount < MAX_STATES) {          \
            var##_state_id = g_stateManager.stateCount++;      \
            g_stateManager.states[var##_state_id].value = &var; \
            g_stateManager.states[var##_state_id].size = sizeof(type); \
            g_stateManager.states[var##_state_id].active = true; \
        }                                                       \
    }

// Register state with onChange callback
#define USE_STATE(var, callback)                                \
    do {                                                        \
        var##_register_state();                                \
        if (var##_state_id >= 0) {                             \
            g_stateManager.states[var##_state_id].onChange = callback; \
        }                                                       \
    } while(0)

// Trigger re-render when state changes
static void triggerRerender() {
    if (g_ui && g_ui->hwnd) {
        InvalidateRect(g_ui->hwnd, NULL, TRUE);
    }
}

// ============================================================================
// EVENT HANDLER REGISTRATION
// ============================================================================

static void registerEventHandler(const char* elementId, ClickHandler handler) {
    if (g_eventManager.handlerCount >= MAX_EVENT_HANDLERS) return;
    
    EventHandler* eh = &g_eventManager.handlers[g_eventManager.handlerCount++];
    strncpy(eh->elementId, elementId, MAX_ATTRIBUTE_LENGTH - 1);
    eh->elementId[MAX_ATTRIBUTE_LENGTH - 1] = '\0';
    eh->onClick = handler;
}

static ClickHandler getEventHandler(const char* elementId) {
    for (int i = 0; i < g_eventManager.handlerCount; i++) {
        if (strcmp(g_eventManager.handlers[i].elementId, elementId) == 0) {
            return g_eventManager.handlers[i].onClick;
        }
    }
    return NULL;
}

static void clearEventHandlers() {
    g_eventManager.handlerCount = 0;
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static char* trim(char* str) {
    if (!str) return NULL;
    
    // Trim leading whitespace
    while (isspace((unsigned char)*str)) str++;
    
    if (*str == 0) return str;
    
    // Trim trailing whitespace
    char* end = str + strlen(str) - 1;
    while (end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    
    return str;
}

// ============================================================================
// NODE MANAGEMENT
// ============================================================================

static Node* createNode(const char* tag) {
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(Node));
    
    strncpy(node->tag, tag, MAX_TAG_LENGTH - 1);
    node->tag[MAX_TAG_LENGTH - 1] = '\0';
    
    // Default size
    node->width = 200;
    node->height = 30;
    
    return node;
}

static void addChild(Node* parent, Node* child) {
    if (!parent || !child) return;
    if (parent->childCount >= MAX_CHILDREN) return;
    
    parent->children[parent->childCount++] = child;
    child->parent = parent;
}

static void freeNode(Node* node) {
    if (!node) return;
    
    for (int i = 0; i < node->childCount; i++) {
        freeNode(node->children[i]);
    }
    
    free(node);
}

// ============================================================================
// SIMPLE HTML PARSER WITH ATTRIBUTES
// ============================================================================

static Node* parseElement(const char** htmlPtr);

static void parseAttributes(const char** p, Node* node) {
    while (**p && **p != '>' && **p != '/') {
        // Skip whitespace
        while (**p && isspace(**p)) (*p)++;
        
        if (**p == '>' || **p == '/') break;
        
        // Extract attribute name
        char attrName[MAX_ATTRIBUTE_LENGTH];
        int i = 0;
        while (**p && !isspace(**p) && **p != '=' && **p != '>' && **p != '/' && i < MAX_ATTRIBUTE_LENGTH - 1) {
            attrName[i++] = tolower(**p);
            (*p)++;
        }
        attrName[i] = '\0';
        
        // Skip whitespace and '='
        while (**p && (isspace(**p) || **p == '=')) (*p)++;
        
        // Extract attribute value (handle quotes or braces)
        char attrValue[MAX_ATTRIBUTE_LENGTH];
        i = 0;
        
        if (**p == '"' || **p == '\'') {
            char quote = **p;
            (*p)++; // Skip opening quote
            while (**p && **p != quote && i < MAX_ATTRIBUTE_LENGTH - 1) {
                attrValue[i++] = **p;
                (*p)++;
            }
            if (**p == quote) (*p)++; // Skip closing quote
        } else if (**p == '{') {
            (*p)++; // Skip opening brace
            while (**p && **p != '}' && i < MAX_ATTRIBUTE_LENGTH - 1) {
                attrValue[i++] = **p;
                (*p)++;
            }
            if (**p == '}') (*p)++; // Skip closing brace
        } else {
            // No quotes
            while (**p && !isspace(**p) && **p != '>' && **p != '/' && i < MAX_ATTRIBUTE_LENGTH - 1) {
                attrValue[i++] = **p;
                (*p)++;
            }
        }
        attrValue[i] = '\0';
        
        // Process attributes
        if (strcmp(attrName, "id") == 0) {
            strncpy(node->id, attrValue, MAX_ATTRIBUTE_LENGTH - 1);
            node->id[MAX_ATTRIBUTE_LENGTH - 1] = '\0';
        } else if (strcmp(attrName, "onclick") == 0) {
            // Get handler from registry
            node->onClick = getEventHandler(attrValue);
        }
    }
}

static Node* parseElement(const char** htmlPtr) {
    if (!htmlPtr || !*htmlPtr) return NULL;
    
    const char* p = *htmlPtr;
    
    // Skip to opening tag
    while (*p && *p != '<') p++;
    if (!*p) return NULL;
    
    p++; // Skip '<'
    
    // Check for closing tag
    if (*p == '/') {
        *htmlPtr = p;
        return NULL;
    }
    
    // Extract tag name
    char tag[MAX_TAG_LENGTH];
    int i = 0;
    while (*p && !isspace(*p) && *p != '>' && *p != '/' && i < MAX_TAG_LENGTH - 1) {
        tag[i++] = tolower(*p++);
    }
    tag[i] = '\0';
    
    if (strlen(tag) == 0) return NULL;
    
    // Create node
    Node* node = createNode(tag);
    if (!node) return NULL;
    
    // Parse attributes
    parseAttributes(&p, node);
    
    // Self-closing tag?
    if (*p == '/') {
        p++;
        if (*p == '>') p++;
        *htmlPtr = p;
        return node;
    }
    
    if (*p == '>') p++;
    
    // Extract content
    const char* contentStart = p;
    
    while (*p) {
        if (*p == '<') {
            // Check if it's a closing tag
            if (*(p + 1) == '/') {
                // Extract text content before closing tag
                if (p > contentStart) {
                    int len = (int)(p - contentStart);
                    if (len > MAX_TEXT_LENGTH - 1) len = MAX_TEXT_LENGTH - 1;
                    
                    strncpy(node->text, contentStart, len);
                    node->text[len] = '\0';
                    
                    // Trim the text
                    char* trimmed = trim(node->text);
                    if (trimmed != node->text) {
                        memmove(node->text, trimmed, strlen(trimmed) + 1);
                    }
                }
                
                // Skip closing tag
                while (*p && *p != '>') p++;
                if (*p == '>') p++;
                break;
            } else {
                // It's a child element
                Node* child = parseElement(&p);
                if (child) {
                    addChild(node, child);
                    contentStart = p;
                }
            }
        } else {
            p++;
        }
    }
    
    *htmlPtr = p;
    return node;
}

static Node* parseHTML(const char* html) {
    if (!html) return NULL;
    
    // Create root container
    Node* root = createNode("root");
    if (!root) return NULL;
    
    const char* p = html;
    
    while (*p) {
        // Skip whitespace
        while (*p && isspace(*p)) p++;
        if (!*p) break;
        
        Node* element = parseElement(&p);
        if (element) {
            addChild(root, element);
        } else {
            break;
        }
    }
    
    return root;
}

// ============================================================================
// SIMPLE LAYOUT ENGINE (Stack Vertically)
// ============================================================================

static void computeLayout(Node* node, int x, int y) {
    if (!node) return;
    
    node->x = x;
    node->y = y;
    
    // Set default sizes based on tag
    if (strcmp(node->tag, "h1") == 0) {
        node->height = 40;
        node->width = 300;
    } else if (strcmp(node->tag, "h2") == 0) {
        node->height = 35;
        node->width = 300;
    } else if (strcmp(node->tag, "button") == 0) {
        node->height = 30;
        node->width = 120;
    } else if (strcmp(node->tag, "p") == 0) {
        node->height = 25;
        node->width = 300;
    } else {
        // div or other
        node->height = 30;
        node->width = 300;
    }
    
    // Layout children vertically below this node
    int childY = y + 10;
    for (int i = 0; i < node->childCount; i++) {
        computeLayout(node->children[i], x + 10, childY);
        childY += node->children[i]->height + 5;
    }
}

// ============================================================================
// SIMPLE RENDERER
// ============================================================================

static void renderNode(HDC hdc, Node* node) {
    if (!node) return;
    
    // Determine colors based on tag
    COLORREF bgColor = RGB(240, 240, 240);
    COLORREF textColor = RGB(0, 0, 0);
    bool drawBorder = false;
    int fontSize = 16;
    int fontWeight = FW_NORMAL;
    
    if (strcmp(node->tag, "button") == 0) {
        bgColor = RGB(76, 175, 80);
        textColor = RGB(255, 255, 255);
        drawBorder = true;
        fontWeight = FW_BOLD;
    } else if (strcmp(node->tag, "h1") == 0) {
        fontSize = 28;
        fontWeight = FW_BOLD;
        textColor = RGB(33, 33, 33);
    } else if (strcmp(node->tag, "h2") == 0) {
        fontSize = 22;
        fontWeight = FW_BOLD;
        textColor = RGB(66, 66, 66);
    } else if (strcmp(node->tag, "p") == 0) {
        fontSize = 14;
        textColor = RGB(33, 33, 33);
    } else if (strcmp(node->tag, "div") == 0) {
        bgColor = RGB(255, 255, 255);
        drawBorder = true;
    }
    
    // Draw background
    HBRUSH brush = CreateSolidBrush(bgColor);
    RECT rect = {node->x, node->y, node->x + node->width, node->y + node->height};
    FillRect(hdc, &rect, brush);
    DeleteObject(brush);
    
    // Draw border if needed
    if (drawBorder) {
        HPEN pen = CreatePen(PS_SOLID, 1, RGB(0, 0, 0));
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        
        Rectangle(hdc, node->x, node->y, node->x + node->width, node->y + node->height);
        
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
    
    // Draw text
    if (strlen(node->text) > 0) {
        SetTextColor(hdc, textColor);
        SetBkMode(hdc, TRANSPARENT);
        
        // Create font
        HFONT hFont = CreateFont(
            fontSize, 0, 0, 0, fontWeight,
            FALSE, FALSE, FALSE,
            DEFAULT_CHARSET, OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS, DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE, "Arial"
        );
        HFONT hOldFont = (HFONT)SelectObject(hdc, hFont);
        
        // Draw text
        RECT textRect = {
            node->x + 5,
            node->y + 5,
            node->x + node->width - 5,
            node->y + node->height - 5
        };
        
        UINT format = DT_LEFT | DT_VCENTER | DT_SINGLELINE;
        if (strcmp(node->tag, "button") == 0) {
            format = DT_CENTER | DT_VCENTER | DT_SINGLELINE;
        }
        
        DrawText(hdc, node->text, -1, &textRect, format);
        
        SelectObject(hdc, hOldFont);
        DeleteObject(hFont);
    }
    
    // Render children
    for (int i = 0; i < node->childCount; i++) {
        renderNode(hdc, node->children[i]);
    }
}

// ============================================================================
// HIT TESTING (Check if point is inside node)
// ============================================================================

static Node* findNodeAtPoint(Node* node, int x, int y) {
    if (!node) return NULL;
    
    // Check children first (they're on top)
    for (int i = 0; i < node->childCount; i++) {
        Node* found = findNodeAtPoint(node->children[i], x, y);
        if (found) return found;
    }
    
    // Check this node
    if (x >= node->x && x < node->x + node->width &&
        y >= node->y && y < node->y + node->height) {
        return node;
    }
    
    return NULL;
}

// ============================================================================
// WINDOW PROCEDURE
// ============================================================================

static LRESULT CALLBACK WindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    switch (uMsg) {
        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Clear background
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            FillRect(hdc, &clientRect, (HBRUSH)(COLOR_WINDOW + 1));
            
            // Render UI
            if (g_ui && g_ui->root) {
                renderNode(hdc, g_ui->root);
            }
            
            EndPaint(hwnd, &ps);
            return 0;
        }
        
        case WM_LBUTTONDOWN: {
            int mouseX = LOWORD(lParam);
            int mouseY = HIWORD(lParam);
            
            // Find node at click position
            if (g_ui && g_ui->root) {
                Node* clickedNode = findNodeAtPoint(g_ui->root, mouseX, mouseY);
                
                if (clickedNode && clickedNode->onClick) {
                    // Call the onClick handler
                    clickedNode->onClick();
                }
            }
            return 0;
        }
        
        case WM_DESTROY:
            PostQuitMessage(0);
            return 0;
    }
    
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ============================================================================
// PUBLIC API
// ============================================================================

static ReactUI* ReactUI_Create(HINSTANCE hInstance) {
    ReactUI* ui = (ReactUI*)malloc(sizeof(ReactUI));
    if (!ui) return NULL;
    
    memset(ui, 0, sizeof(ReactUI));
    ui->hInstance = hInstance;
    
    g_ui = ui;
    return ui;
}

static void ReactUI_Render(ReactUI* ui, const char* html) {
    if (!ui || !html) return;
    
    // Free old tree
    if (ui->root) {
        freeNode(ui->root);
        ui->root = NULL;
    }
    
    // Parse HTML
    ui->root = parseHTML(html);
    
    // Compute layout
    if (ui->root) {
        computeLayout(ui->root, 10, 10);
    }
}

static HWND ReactUI_CreateWindow(ReactUI* ui, const char* title, int width, int height) {
    if (!ui) return NULL;
    
    // Register window class
    WNDCLASS wc = {0};
    wc.lpfnWndProc = WindowProc;
    wc.hInstance = ui->hInstance;
    wc.lpszClassName = "ReactUIMinimal";
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW + 1);
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    
    RegisterClass(&wc);
    
    // Create window
    ui->hwnd = CreateWindowEx(
        0,
        "ReactUIMinimal",
        title ? title : "ReactUI",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT,
        width, height,
        NULL, NULL,
        ui->hInstance,
        NULL
    );
    
    return ui->hwnd;
}

static int ReactUI_Run() {
    MSG msg;
    while (GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

static void ReactUI_Destroy(ReactUI* ui) {
    if (!ui) return;
    
    if (ui->root) {
        freeNode(ui->root);
    }
    
    free(ui);
    g_ui = NULL;
}

// ============================================================================
// EVENT BINDING HELPER
// ============================================================================

#define BIND_EVENT(id, handler) registerEventHandler(id, handler)

#endif // REACTUI_MINIMAL_H