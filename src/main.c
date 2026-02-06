#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

#define MAX_CHILDREN 32
#define MAX_STYLES 16

typedef struct CSSProperty {
    char name[32];
    char value[64];
} CSSProperty;

typedef struct Node {
    char tag[16];
    char text[256];
    char id[32];
    char class[32];
    
    CSSProperty styles[MAX_STYLES];
    int styleCount;
    
    int x, y;
    int width, height;
    int computedWidth, computedHeight;
    
    struct Node* children[MAX_CHILDREN];
    int childCount;
    struct Node* parent;
} Node;

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
    
    return RGB(255, 255, 255);
}

// --- Node creation ---
Node* createNode(const char* tag) {
    Node* node = (Node*)malloc(sizeof(Node));
    memset(node, 0, sizeof(Node));
    strcpy(node->tag, tag);
    node->width = -1;  // auto
    node->height = -1; // auto
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

// --- HTML Parser ---
const char* findTagEnd(const char* start) {
    int depth = 1;
    const char* p = start;
    while (*p && depth > 0) {
        if (*p == '<') {
            if (*(p+1) == '/') depth--;
            else if (*(p+1) != '!') depth++;
        }
        p++;
    }
    return p;
}

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
            attrName[i++] = *p++;
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
    const char* displayStr = getStyle(node, "display");
    
    // Compute dimensions
    if (widthStr) {
        node->computedWidth = atoi(widthStr);
    } else {
        node->computedWidth = parentWidth > 0 ? parentWidth - 20 : 200;
    }
    
    if (heightStr) {
        node->computedHeight = atoi(heightStr);
    } else {
        // Auto height based on content
        node->computedHeight = 30 + (node->childCount * 40);
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

// --- Renderer ---
void renderNode(HDC hdc, Node* node) {
    if (!node) return;
    
    const char* bgColor = getStyle(node, "background");
    const char* color = getStyle(node, "color");
    const char* fontSize = getStyle(node, "font-size");
    const char* border = getStyle(node, "border");
    
    // Draw background
    if (bgColor) {
        HBRUSH brush = CreateSolidBrush(parseColor(bgColor));
        RECT rect = {node->x, node->y, 
                     node->x + node->computedWidth, 
                     node->y + node->computedHeight};
        FillRect(hdc, &rect, brush);
        DeleteObject(brush);
    }
    
    // Draw border
    if (border) {
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
    
    // Draw text
    if (strlen(node->text) > 0) {
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
        }
        
        RECT textRect = {node->x + 5, node->y + 5, 
                        node->x + node->computedWidth - 5, 
                        node->y + node->computedHeight - 5};
        DrawText(hdc, node->text, -1, &textRect, DT_LEFT | DT_TOP | DT_WORDBREAK);
        
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
    static Node* root = NULL;

    switch (uMsg) {
        case WM_CREATE: {
            const char* html = 
                "<div style=\"background:#ff6b6b;width:350;height:200;border:1px;\">"
                    "<h1 style=\"color:white;font-size:24;\">Welcome to HTML Renderer</h1>"
                    "<p style=\"color:#fff;font-size:14;\">This is a proper HTML parser that supports:</p>"
                    "<div style=\"background:#4ecdc4;width:300;height:80;\">"
                        "<p style=\"color:white;font-size:12;\">• Multiple nested elements</p>"
                        "<p style=\"color:white;font-size:12;\">• CSS styling with colors and sizes</p>"
                    "</div>"
                "</div>"
                "<div style=\"background:#95e1d3;width:350;height:100;border:1px;\">"
                    "<p style=\"color:#2c3e50;font-size:16;\">Multiple root elements work too!</p>"
                "</div>";
            
            const char* p = html;
            root = parseHTML(&p);
            computeLayout(root, 400, 600, 10, 10);
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            // Fill background
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            FillRect(hdc, &clientRect, (HBRUSH)(COLOR_WINDOW+1));
            
            renderNode(hdc, root);
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY:
            freeNode(root);
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

    RegisterClass(&wc);

    HWND hwnd = CreateWindowEx(
        0, "HTMLRendererWindow", "HTML Renderer with Proper Parser",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, 450, 550,
        NULL, NULL, hInstance, NULL
    );

    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}