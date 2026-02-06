#ifndef REACT_UI_HYBRID_H
#define REACT_UI_HYBRID_H

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>

// ============================================================================
// HTML MACRO FOR CLEANER SYNTAX
// ============================================================================

// Allow multi-line HTML strings without quotes
#define HTML(...) #__VA_ARGS__

#define MAX_CHILDREN 32
#define MAX_STYLES 16
#define MAX_STATES 16
#define MAX_EFFECTS 16
#define MAX_UI_INSTANCES 8
#define MAX_CLASS_NAMES 8

// ============================================================================
// UTILITY CLASS DEFINITIONS
// ============================================================================

typedef struct UtilityClass {
    const char* className;
    const char* inlineStyle;
} UtilityClass;

// Pre-defined utility classes (Tailwind-inspired)
static const UtilityClass utilityClasses[] = {
    // Layout Classes
    {"flex-row",           "display: flex; flex-direction: row;"},
    {"flex-col",           "display: flex; flex-direction: column;"},
    {"flex-row-center",    "display: flex; flex-direction: row; justify-content: center; align-items: center;"},
    {"flex-col-center",    "display: flex; flex-direction: column; justify-content: center; align-items: center;"},
    {"flex-row-between",   "display: flex; flex-direction: row; justify-content: space-between;"},
    {"flex-row-around",    "display: flex; flex-direction: row; justify-content: space-around;"},
    {"flex-row-evenly",    "display: flex; flex-direction: row; justify-content: space-evenly;"},
    {"flex-row-start",     "display: flex; flex-direction: row; align-items: flex-start;"},
    {"flex-row-end",       "display: flex; flex-direction: row; align-items: flex-end;"},
    {"flex-col-start",     "display: flex; flex-direction: column; align-items: flex-start;"},
    {"flex-col-end",       "display: flex; flex-direction: column; align-items: flex-end;"},
    {"flex-wrap",          "flex-wrap: wrap;"},
    {"flex-1",             "flex-grow: 1;"},
    {"flex-2",             "flex-grow: 2;"},
    {"flex-3",             "flex-grow: 3;"},
    
    // Spacing Classes
    {"p-0",    "padding: 0;"},
    {"p-5",    "padding: 5;"},
    {"p-10",   "padding: 10;"},
    {"p-15",   "padding: 15;"},
    {"p-20",   "padding: 20;"},
    {"gap-5",  "gap: 5;"},
    {"gap-10", "gap: 10;"},
    {"gap-15", "gap: 15;"},
    {"gap-20", "gap: 20;"},
    {"m-0",    "margin: 0;"},
    {"m-5",    "margin: 5;"},
    {"m-10",   "margin: 10;"},
    
    // Background Color Classes
    {"bg-primary",     "background: #4CAF50;"},
    {"bg-secondary",   "background: #2196F3;"},
    {"bg-danger",      "background: #f44336;"},
    {"bg-warning",     "background: #FF9800;"},
    {"bg-success",     "background: #4CAF50;"},
    {"bg-info",        "background: #00BCD4;"},
    {"bg-white",       "background: #ffffff;"},
    {"bg-black",       "background: #000000;"},
    {"bg-gray-50",     "background: #fafafa;"},
    {"bg-gray-100",    "background: #f5f5f5;"},
    {"bg-gray-200",    "background: #eeeeee;"},
    {"bg-gray-300",    "background: #e0e0e0;"},
    {"bg-blue-50",     "background: #E3F2FD;"},
    {"bg-blue-500",    "background: #2196F3;"},
    {"bg-blue-600",    "background: #1976D2;"},
    {"bg-blue-700",    "background: #1565C0;"},
    {"bg-green-50",    "background: #E8F5E9;"},
    {"bg-green-500",   "background: #4CAF50;"},
    {"bg-green-600",   "background: #43A047;"},
    {"bg-red-50",      "background: #FFEBEE;"},
    {"bg-red-500",     "background: #f44336;"},
    {"bg-red-600",     "background: #E53935;"},
    {"bg-orange-500",  "background: #FF9800;"},
    {"bg-purple-500",  "background: #9C27B0;"},
    
    // Text Color Classes
    {"text-white",     "color: white;"},
    {"text-black",     "color: black;"},
    {"text-gray-600",  "color: #666666;"},
    {"text-gray-700",  "color: #555555;"},
    {"text-blue-600",  "color: #1976D2;"},
    {"text-blue-700",  "color: #1565C0;"},
    {"text-green-600", "color: #43A047;"},
    {"text-red-600",   "color: #E53935;"},
    
    // Typography Classes
    {"text-xs",   "font-size: 12;"},
    {"text-sm",   "font-size: 14;"},
    {"text-base", "font-size: 16;"},
    {"text-lg",   "font-size: 18;"},
    {"text-xl",   "font-size: 20;"},
    {"text-2xl",  "font-size: 24;"},
    {"text-3xl",  "font-size: 30;"},
    {"text-4xl",  "font-size: 36;"},
    {"text-5xl",  "font-size: 48;"},
    
    // Border Classes
    {"border",         "border: 1px solid #e0e0e0;"},
    {"border-2",       "border: 2px solid #e0e0e0;"},
    {"border-gray",    "border: 1px solid #e0e0e0;"},
    {"border-blue",    "border: 1px solid #2196F3;"},
    {"rounded",        "border-radius: 5;"},
    {"rounded-lg",     "border-radius: 8;"},
    {"rounded-xl",     "border-radius: 12;"},
    {"rounded-full",   "border-radius: 999;"},
    
    // Component Presets
    {"btn",            "padding: 10; border-radius: 5; font-size: 16;"},
    {"btn-primary",    "background: #4CAF50; color: white; padding: 10; border-radius: 5; font-size: 16;"},
    {"btn-secondary",  "background: #2196F3; color: white; padding: 10; border-radius: 5; font-size: 16;"},
    {"btn-danger",     "background: #f44336; color: white; padding: 10; border-radius: 5; font-size: 16;"},
    {"btn-warning",    "background: #FF9800; color: white; padding: 10; border-radius: 5; font-size: 16;"},
    {"btn-success",    "background: #4CAF50; color: white; padding: 10; border-radius: 5; font-size: 16;"},
    {"btn-lg",         "padding: 15; border-radius: 5; font-size: 18;"},
    {"btn-sm",         "padding: 8; border-radius: 5; font-size: 14;"},
    {"card",           "background: white; border: 1px solid #e0e0e0; border-radius: 8; padding: 20;"},
    {"card-header",    "background: #f5f5f5; padding: 15; border-radius: 8;"},
    {"shadow",         "border: 1px solid #e0e0e0;"},
    {"shadow-lg",      "border: 2px solid #d0d0d0;"},
    
    // Sizing Classes
    {"w-full",   "width: 100%;"},
    {"h-full",   "height: 100%;"},
    {"w-50",     "width: 50;"},
    {"w-100",    "width: 100;"},
    {"w-200",    "width: 200;"},
    {"w-300",    "width: 300;"},
    {"h-50",     "height: 50;"},
    {"h-100",    "height: 100;"},
    {"h-200",    "height: 200;"},
};

static const int utilityClassCount = sizeof(utilityClasses) / sizeof(UtilityClass);

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================

typedef struct CSSProperty {
    char name[32];
    char value[64];
} CSSProperty;

typedef struct State {
    char name[32];
    int value;
} State;

typedef struct StateManager {
    State states[MAX_STATES];
    int stateCount;
} StateManager;

// Flexbox layout modes
typedef enum {
    FLEX_DIRECTION_ROW,
    FLEX_DIRECTION_COLUMN,
    FLEX_DIRECTION_ROW_REVERSE,
    FLEX_DIRECTION_COLUMN_REVERSE
} FlexDirection;

typedef enum {
    JUSTIFY_FLEX_START,
    JUSTIFY_FLEX_END,
    JUSTIFY_CENTER,
    JUSTIFY_SPACE_BETWEEN,
    JUSTIFY_SPACE_AROUND,
    JUSTIFY_SPACE_EVENLY
} JustifyContent;

typedef enum {
    ALIGN_FLEX_START,
    ALIGN_FLEX_END,
    ALIGN_CENTER,
    ALIGN_STRETCH,
    ALIGN_BASELINE
} AlignItems;

typedef enum {
    FLEX_WRAP_NOWRAP,
    FLEX_WRAP_WRAP,
    FLEX_WRAP_WRAP_REVERSE
} FlexWrap;

typedef struct FlexProperties {
    int isFlexContainer;
    FlexDirection direction;
    JustifyContent justify;
    AlignItems align;
    FlexWrap wrap;
    int flexGrow;
    int flexShrink;
    int flexBasis;
    int gap;
} FlexProperties;

typedef struct Node {
    char tag[16];
    char text[256];
    char id[32];
    char class[256];  // Increased to hold multiple class names
    char onClick[128];
    
    CSSProperty styles[MAX_STYLES];
    int styleCount;
    
    int x, y;
    int width, height;
    int computedWidth, computedHeight;
    
    // Flexbox properties
    FlexProperties flex;
    
    struct Node* children[MAX_CHILDREN];
    int childCount;
    struct Node* parent;
    
    int isButton;
} Node;

typedef struct ReactUI {
    Node* root;
    HWND hwnd;
    StateManager stateManager;
    HINSTANCE hInstance;
    int instanceId;
    void (*onError)(const char* message);
} ReactUI;

// ============================================================================
// GLOBAL STATE
// ============================================================================

static ReactUI* g_reactUIInstances[MAX_UI_INSTANCES] = {0};
static int g_instanceCount = 0;

// ============================================================================
// SAFE STRING OPERATIONS
// ============================================================================

static void safe_strcpy(char* dst, const char* src, size_t dst_size) {
    if (!dst || !src || dst_size == 0) return;
    strncpy(dst, src, dst_size - 1);
    dst[dst_size - 1] = '\0';
}

static void safe_strcat(char* dst, const char* src, size_t dst_size) {
    if (!dst || !src || dst_size == 0) return;
    size_t current_len = strnlen(dst, dst_size);
    if (current_len >= dst_size - 1) return;
    strncat(dst, src, dst_size - current_len - 1);
    dst[dst_size - 1] = '\0';
}

static int safe_sprintf(char* dst, size_t dst_size, const char* format, ...) {
    if (!dst || dst_size == 0) return -1;
    va_list args;
    va_start(args, format);
    int result = vsnprintf(dst, dst_size, format, args);
    va_end(args);
    dst[dst_size - 1] = '\0';
    return result;
}

// ============================================================================
// UTILITY CLASS EXPANSION
// ============================================================================

static void expandUtilityClasses(const char* classNames, char* output, size_t maxLen) {
    if (!classNames || !output || maxLen == 0) return;
    
    output[0] = '\0';
    char classCopy[256];
    safe_strcpy(classCopy, classNames, sizeof(classCopy));
    
    // Split class names by space
    char* token = strtok(classCopy, " ");
    while (token) {
        // Trim whitespace
        while (*token && isspace(*token)) token++;
        if (*token == '\0') {
            token = strtok(NULL, " ");
            continue;
        }
        
        // Look up utility class
        int found = 0;
        for (int i = 0; i < utilityClassCount; i++) {
            if (strcmp(token, utilityClasses[i].className) == 0) {
                // Append the style
                if (output[0] != '\0') {
                    safe_strcat(output, " ", maxLen);
                }
                safe_strcat(output, utilityClasses[i].inlineStyle, maxLen);
                found = 1;
                break;
            }
        }
        
        token = strtok(NULL, " ");
    }
}

// ============================================================================
// HELPER FUNCTIONS
// ============================================================================

static char* trim(char* str) {
    if (!str) return NULL;
    while(isspace((unsigned char)*str)) str++;
    if(*str == 0) return str;
    char* end = str + strlen(str) - 1;
    while(end > str && isspace((unsigned char)*end)) end--;
    end[1] = '\0';
    return str;
}

static COLORREF parseColor(const char* color) {
    if (!color) return RGB(255, 255, 255);
    
    if (color[0] == '#') {
        unsigned int r, g, b;
        if (strlen(color) == 7) {
            if (sscanf(color + 1, "%02x%02x%02x", &r, &g, &b) == 3) {
                return RGB(r & 0xFF, g & 0xFF, b & 0xFF);
            }
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
    if (strcmp(color, "pink") == 0) return RGB(255, 192, 203);
    if (strcmp(color, "brown") == 0) return RGB(165, 42, 42);
    
    return RGB(255, 255, 255);
}

static void reportError(ReactUI* ui, const char* message) {
    if (ui && ui->onError) {
        ui->onError(message);
    }
}

// ============================================================================
// STATE MANAGEMENT API
// ============================================================================

static void setState(ReactUI* ui, const char* name, int value) {
    if (!ui || !name) return;
    
    for (int i = 0; i < ui->stateManager.stateCount; i++) {
        if (strcmp(ui->stateManager.states[i].name, name) == 0) {
            ui->stateManager.states[i].value = value;
            return;
        }
    }
    
    if (ui->stateManager.stateCount < MAX_STATES) {
        safe_strcpy(ui->stateManager.states[ui->stateManager.stateCount].name, 
                    name, sizeof(ui->stateManager.states[0].name));
        ui->stateManager.states[ui->stateManager.stateCount].value = value;
        ui->stateManager.stateCount++;
    } else {
        reportError(ui, "Maximum state count exceeded");
    }
}

static int getState(ReactUI* ui, const char* name) {
    if (!ui || !name) return 0;
    
    for (int i = 0; i < ui->stateManager.stateCount; i++) {
        if (strcmp(ui->stateManager.states[i].name, name) == 0) {
            return ui->stateManager.states[i].value;
        }
    }
    return 0;
}

// ============================================================================
// NODE MANAGEMENT
// ============================================================================

static Node* createNode(const char* tag) {
    if (!tag) return NULL;
    
    Node* node = (Node*)malloc(sizeof(Node));
    if (!node) return NULL;
    
    memset(node, 0, sizeof(Node));
    safe_strcpy(node->tag, tag, sizeof(node->tag));
    node->width = -1;
    node->height = -1;
    
    // Initialize flexbox defaults
    node->flex.isFlexContainer = 0;
    node->flex.direction = FLEX_DIRECTION_ROW;
    node->flex.justify = JUSTIFY_FLEX_START;
    node->flex.align = ALIGN_FLEX_START;
    node->flex.wrap = FLEX_WRAP_NOWRAP;
    node->flex.flexGrow = 0;
    node->flex.flexShrink = 1;
    node->flex.flexBasis = -1;
    node->flex.gap = 0;
    
    if (strcmp(tag, "button") == 0) {
        node->isButton = 1;
    }
    
    return node;
}

static void addChild(Node* parent, Node* child) {
    if (!parent || !child) return;
    
    if (parent->childCount < MAX_CHILDREN) {
        parent->children[parent->childCount++] = child;
        child->parent = parent;
    }
}

static void addStyle(Node* node, const char* name, const char* value) {
    if (!node || !name || !value) return;
    
    if (node->styleCount < MAX_STYLES) {
        safe_strcpy(node->styles[node->styleCount].name, name, 
                    sizeof(node->styles[0].name));
        safe_strcpy(node->styles[node->styleCount].value, value, 
                    sizeof(node->styles[0].value));
        node->styleCount++;
    }
}

static const char* getStyle(Node* node, const char* name) {
    if (!node || !name) return NULL;
    
    for (int i = 0; i < node->styleCount; i++) {
        if (strcmp(node->styles[i].name, name) == 0) {
            return node->styles[i].value;
        }
    }
    return NULL;
}

static void freeNode(Node* node) {
    if (!node) return;
    for (int i = 0; i < node->childCount; i++) {
        freeNode(node->children[i]);
    }
    free(node);
}

// ============================================================================
// VARIABLE SUBSTITUTION
// ============================================================================

static void substituteVariables(ReactUI* ui, char* text, char* output, int maxLen) {
    if (!ui || !text || !output || maxLen <= 0) return;
    
    char* src = text;
    char* dst = output;
    int remaining = maxLen - 1;
    
    while (*src && remaining > 0) {
        if (*src == '{' && *(src + 1) == '{') {
            src += 2;
            char varName[32] = {0};
            int i = 0;
            
            while (*src && *src != '}' && i < 31) {
                varName[i++] = *src++;
            }
            varName[i] = '\0';
            
            if (*src == '}' && *(src + 1) == '}') {
                src += 2;
            }
            
            int value = getState(ui, trim(varName));
            char valueStr[32];
            safe_sprintf(valueStr, sizeof(valueStr), "%d", value);
            
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

// ============================================================================
// FLEXBOX PARSER
// ============================================================================

static void parseFlexProperties(Node* node) {
    if (!node) return;
    
    const char* display = getStyle(node, "display");
    if (display && strcmp(display, "flex") == 0) {
        node->flex.isFlexContainer = 1;
    }
    
    const char* flexDirection = getStyle(node, "flex-direction");
    if (flexDirection) {
        if (strcmp(flexDirection, "row") == 0) {
            node->flex.direction = FLEX_DIRECTION_ROW;
        } else if (strcmp(flexDirection, "column") == 0) {
            node->flex.direction = FLEX_DIRECTION_COLUMN;
        } else if (strcmp(flexDirection, "row-reverse") == 0) {
            node->flex.direction = FLEX_DIRECTION_ROW_REVERSE;
        } else if (strcmp(flexDirection, "column-reverse") == 0) {
            node->flex.direction = FLEX_DIRECTION_COLUMN_REVERSE;
        }
    }
    
    const char* justifyContent = getStyle(node, "justify-content");
    if (justifyContent) {
        if (strcmp(justifyContent, "flex-start") == 0) {
            node->flex.justify = JUSTIFY_FLEX_START;
        } else if (strcmp(justifyContent, "flex-end") == 0) {
            node->flex.justify = JUSTIFY_FLEX_END;
        } else if (strcmp(justifyContent, "center") == 0) {
            node->flex.justify = JUSTIFY_CENTER;
        } else if (strcmp(justifyContent, "space-between") == 0) {
            node->flex.justify = JUSTIFY_SPACE_BETWEEN;
        } else if (strcmp(justifyContent, "space-around") == 0) {
            node->flex.justify = JUSTIFY_SPACE_AROUND;
        } else if (strcmp(justifyContent, "space-evenly") == 0) {
            node->flex.justify = JUSTIFY_SPACE_EVENLY;
        }
    }
    
    const char* alignItems = getStyle(node, "align-items");
    if (alignItems) {
        if (strcmp(alignItems, "flex-start") == 0) {
            node->flex.align = ALIGN_FLEX_START;
        } else if (strcmp(alignItems, "flex-end") == 0) {
            node->flex.align = ALIGN_FLEX_END;
        } else if (strcmp(alignItems, "center") == 0) {
            node->flex.align = ALIGN_CENTER;
        } else if (strcmp(alignItems, "stretch") == 0) {
            node->flex.align = ALIGN_STRETCH;
        } else if (strcmp(alignItems, "baseline") == 0) {
            node->flex.align = ALIGN_BASELINE;
        }
    }
    
    const char* flexWrap = getStyle(node, "flex-wrap");
    if (flexWrap) {
        if (strcmp(flexWrap, "nowrap") == 0) {
            node->flex.wrap = FLEX_WRAP_NOWRAP;
        } else if (strcmp(flexWrap, "wrap") == 0) {
            node->flex.wrap = FLEX_WRAP_WRAP;
        } else if (strcmp(flexWrap, "wrap-reverse") == 0) {
            node->flex.wrap = FLEX_WRAP_WRAP_REVERSE;
        }
    }
    
    const char* flexGrow = getStyle(node, "flex-grow");
    if (flexGrow) {
        node->flex.flexGrow = atoi(flexGrow);
    }
    
    const char* flexShrink = getStyle(node, "flex-shrink");
    if (flexShrink) {
        node->flex.flexShrink = atoi(flexShrink);
    }
    
    const char* flexBasis = getStyle(node, "flex-basis");
    if (flexBasis) {
        if (strcmp(flexBasis, "auto") != 0) {
            node->flex.flexBasis = atoi(flexBasis);
        }
    }
    
    const char* gap = getStyle(node, "gap");
    if (gap) {
        node->flex.gap = atoi(gap);
    }
}

// ============================================================================
// CSS PARSER WITH UTILITY CLASS SUPPORT
// ============================================================================

static void parseInlineStyle(Node* node, const char* styleStr) {
    if (!node || !styleStr) return;
    
    char buf[512];
    safe_strcpy(buf, styleStr, sizeof(buf));
    
    char* token = strtok(buf, ";");
    while (token) {
        char* colon = strchr(token, ':');
        if (colon) {
            *colon = '\0';
            char* name = trim(token);
            char* value = trim(colon + 1);
            if (name && value) {
                addStyle(node, name, value);
            }
        }
        token = strtok(NULL, ";");
    }
    
    // Parse flexbox properties after all styles are added
    parseFlexProperties(node);
}

static void applyUtilityClasses(Node* node) {
    if (!node || strlen(node->class) == 0) return;
    
    // Expand utility classes to inline styles
    char expandedStyles[1024];
    expandUtilityClasses(node->class, expandedStyles, sizeof(expandedStyles));
    
    // Parse the expanded styles
    if (strlen(expandedStyles) > 0) {
        parseInlineStyle(node, expandedStyles);
    }
}

// ============================================================================
// HTML PARSER
// ============================================================================

static const char* parseAttributes(const char* str, Node* node) {
    if (!str || !node) return str;
    
    const char* p = str;
    char attrName[32], attrValue[512];
    
    while (*p && *p != '>' && *p != '/') {
        while (*p && isspace(*p)) p++;
        if (*p == '>' || *p == '/') break;
        
        int i = 0;
        while (*p && *p != '=' && *p != '>' && *p != '/' && !isspace(*p) && i < 31) {
            attrName[i++] = tolower(*p++);
        }
        attrName[i] = '\0';
        
        if (*p == '=') {
            p++;
            while (*p && isspace(*p)) p++;
            
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
            
            if (strcmp(attrName, "style") == 0) {
                parseInlineStyle(node, attrValue);
            } else if (strcmp(attrName, "id") == 0) {
                safe_strcpy(node->id, attrValue, sizeof(node->id));
            } else if (strcmp(attrName, "class") == 0) {
                safe_strcpy(node->class, attrValue, sizeof(node->class));
            } else if (strcmp(attrName, "onclick") == 0) {
                safe_strcpy(node->onClick, attrValue, sizeof(node->onClick));
            }
        }
    }
    
    return p;
}

static Node* parseElement(const char** htmlPtr);

static Node* parseElement(const char** htmlPtr) {
    if (!htmlPtr || !*htmlPtr) return NULL;
    
    const char* p = *htmlPtr;
    
    while (*p && *p != '<') p++;
    if (!*p) return NULL;
    
    p++;
    
    if (*p == '/') {
        *htmlPtr = p;
        return NULL;
    }
    
    char tag[16];
    int i = 0;
    while (*p && !isspace(*p) && *p != '>' && *p != '/' && i < 15) {
        tag[i++] = tolower(*p++);
    }
    tag[i] = '\0';
    
    if (strlen(tag) == 0) return NULL;
    
    Node* node = createNode(tag);
    if (!node) return NULL;
    
    p = parseAttributes(p, node);
    
    // Apply utility classes AFTER parsing attributes
    applyUtilityClasses(node);
    
    if (*p == '/') {
        p++;
        if (*p == '>') p++;
        *htmlPtr = p;
        return node;
    }
    
    if (*p == '>') p++;
    
    const char* contentStart = p;
    int tagDepth = 1;
    
    while (*p && tagDepth > 0) {
        if (*p == '<') {
            if (*(p+1) == '/') {
                const char* closeStart = p + 2;
                char closeTag[16];
                i = 0;
                while (*closeStart && *closeStart != '>' && i < 15) {
                    closeTag[i++] = tolower(*closeStart++);
                }
                closeTag[i] = '\0';
                
                if (strcmp(closeTag, tag) == 0) {
                    if (p > contentStart) {
                        int len = (int)(p - contentStart);
                        if (len > 255) len = 255;
                        strncpy(node->text, contentStart, len);
                        node->text[len] = '\0';
                        
                        char* trimmed = trim(node->text);
                        if (trimmed != node->text) {
                            memmove(node->text, trimmed, strlen(trimmed) + 1);
                        }
                    }
                    
                    while (*p && *p != '>') p++;
                    if (*p == '>') p++;
                    tagDepth--;
                    break;
                }
            } else if (*(p+1) != '!') {
                Node* child = parseElement(&p);
                if (child) {
                    addChild(node, child);
                    contentStart = p;
                }
                continue;
            }
        }
        p++;
    }
    
    *htmlPtr = p;
    return node;
}

static Node* parseHTML(const char** htmlPtr) {
    if (!htmlPtr || !*htmlPtr) return NULL;
    
    Node* root = createNode("root");
    if (!root) return NULL;
    
    const char* p = *htmlPtr;
    
    while (*p) {
        Node* element = parseElement(&p);
        if (element) {
            addChild(root, element);
        } else {
            break;
        }
        
        while (*p && isspace(*p)) p++;
    }
    
    *htmlPtr = p;
    return root;
}

// ============================================================================
// FLEXBOX LAYOUT ENGINE
// ============================================================================

static void computeFlexLayout(Node* node, int availableWidth, int availableHeight, 
                              int offsetX, int offsetY) {
    if (!node || !node->flex.isFlexContainer) return;
    
    int isRow = (node->flex.direction == FLEX_DIRECTION_ROW || 
                 node->flex.direction == FLEX_DIRECTION_ROW_REVERSE);
    int isReverse = (node->flex.direction == FLEX_DIRECTION_ROW_REVERSE || 
                     node->flex.direction == FLEX_DIRECTION_COLUMN_REVERSE);
    
    // Calculate total flex grow and shrink
    int totalFlexGrow = 0;
    int totalFlexShrink = 0;
    int fixedSize = 0;
    int flexItemCount = 0;
    
    for (int i = 0; i < node->childCount; i++) {
        Node* child = node->children[i];
        totalFlexGrow += child->flex.flexGrow;
        totalFlexShrink += child->flex.flexShrink;
        
        if (isRow) {
            if (child->flex.flexBasis >= 0) {
                fixedSize += child->flex.flexBasis;
            } else {
                const char* widthStr = getStyle(child, "width");
                if (widthStr) {
                    fixedSize += atoi(widthStr);
                }
            }
        } else {
            if (child->flex.flexBasis >= 0) {
                fixedSize += child->flex.flexBasis;
            } else {
                const char* heightStr = getStyle(child, "height");
                if (heightStr) {
                    fixedSize += atoi(heightStr);
                }
            }
        }
        flexItemCount++;
    }
    
    // Add gaps
    int totalGap = node->flex.gap * (flexItemCount > 0 ? flexItemCount - 1 : 0);
    fixedSize += totalGap;
    
    int availableSpace = isRow ? availableWidth : availableHeight;
    int remainingSpace = availableSpace - fixedSize;
    
    // Distribute remaining space based on flex-grow
    int flexGrowUnit = (totalFlexGrow > 0 && remainingSpace > 0) ? 
                       remainingSpace / totalFlexGrow : 0;
    
    // Calculate positions based on justify-content
    int currentPos = 0;
    int spacing = 0;
    
    switch (node->flex.justify) {
        case JUSTIFY_FLEX_START:
            currentPos = 0;
            break;
        case JUSTIFY_FLEX_END:
            currentPos = remainingSpace > 0 ? remainingSpace : 0;
            break;
        case JUSTIFY_CENTER:
            currentPos = remainingSpace > 0 ? remainingSpace / 2 : 0;
            break;
        case JUSTIFY_SPACE_BETWEEN:
            currentPos = 0;
            spacing = (flexItemCount > 1 && remainingSpace > 0) ? 
                     remainingSpace / (flexItemCount - 1) : 0;
            break;
        case JUSTIFY_SPACE_AROUND:
            spacing = (flexItemCount > 0 && remainingSpace > 0) ? 
                     remainingSpace / flexItemCount : 0;
            currentPos = spacing / 2;
            break;
        case JUSTIFY_SPACE_EVENLY:
            spacing = (flexItemCount > 0 && remainingSpace > 0) ? 
                     remainingSpace / (flexItemCount + 1) : 0;
            currentPos = spacing;
            break;
    }
    
    // Layout children
    int startIndex = isReverse ? node->childCount - 1 : 0;
    int endIndex = isReverse ? -1 : node->childCount;
    int step = isReverse ? -1 : 1;
    
    for (int i = startIndex; i != endIndex; i += step) {
        Node* child = node->children[i];
        
        // Calculate child size
        int childMainSize = 0;
        int childCrossSize = 0;
        
        if (isRow) {
            // Main axis (width)
            if (child->flex.flexBasis >= 0) {
                childMainSize = child->flex.flexBasis;
            } else {
                const char* widthStr = getStyle(child, "width");
                childMainSize = widthStr ? atoi(widthStr) : 100;
            }
            
            if (child->flex.flexGrow > 0 && remainingSpace > 0) {
                childMainSize += flexGrowUnit * child->flex.flexGrow;
            }
            
            // Cross axis (height)
            const char* heightStr = getStyle(child, "height");
            if (heightStr) {
                childCrossSize = atoi(heightStr);
            } else if (node->flex.align == ALIGN_STRETCH) {
                childCrossSize = availableHeight;
            } else {
                childCrossSize = child->isButton ? 35 : 30;
            }
            
            child->computedWidth = childMainSize;
            child->computedHeight = childCrossSize;
            
        } else {
            // Main axis (height)
            if (child->flex.flexBasis >= 0) {
                childMainSize = child->flex.flexBasis;
            } else {
                const char* heightStr = getStyle(child, "height");
                childMainSize = heightStr ? atoi(heightStr) : (child->isButton ? 35 : 30);
            }
            
            if (child->flex.flexGrow > 0 && remainingSpace > 0) {
                childMainSize += flexGrowUnit * child->flex.flexGrow;
            }
            
            // Cross axis (width)
            const char* widthStr = getStyle(child, "width");
            if (widthStr) {
                childCrossSize = atoi(widthStr);
            } else if (node->flex.align == ALIGN_STRETCH) {
                childCrossSize = availableWidth;
            } else {
                childCrossSize = child->isButton ? 120 : 100;
            }
            
            child->computedWidth = childCrossSize;
            child->computedHeight = childMainSize;
        }
        
        // Position child based on align-items
        int crossOffset = 0;
        switch (node->flex.align) {
            case ALIGN_FLEX_START:
                crossOffset = 0;
                break;
            case ALIGN_FLEX_END:
                crossOffset = (isRow ? availableHeight : availableWidth) - 
                             (isRow ? childCrossSize : childCrossSize);
                break;
            case ALIGN_CENTER:
                crossOffset = ((isRow ? availableHeight : availableWidth) - 
                              (isRow ? childCrossSize : childCrossSize)) / 2;
                break;
            case ALIGN_STRETCH:
            case ALIGN_BASELINE:
                crossOffset = 0;
                break;
        }
        
        if (isRow) {
            child->x = offsetX + currentPos;
            child->y = offsetY + crossOffset;
            currentPos += childMainSize + node->flex.gap + spacing;
        } else {
            child->x = offsetX + crossOffset;
            child->y = offsetY + currentPos;
            currentPos += childMainSize + node->flex.gap + spacing;
        }
        
        // Recursively layout child's children
        if (child->flex.isFlexContainer) {
            computeFlexLayout(child, child->computedWidth - 20, 
                            child->computedHeight - 20, 
                            child->x + 10, child->y + 10);
        } else if (child->childCount > 0) {
            // Regular layout for non-flex containers
            int childY = child->y + 10;
            for (int j = 0; j < child->childCount; j++) {
                Node* grandChild = child->children[j];
                const char* widthStr = getStyle(grandChild, "width");
                const char* heightStr = getStyle(grandChild, "height");
                
                grandChild->computedWidth = widthStr ? atoi(widthStr) : 
                                           (grandChild->isButton ? 120 : child->computedWidth - 20);
                grandChild->computedHeight = heightStr ? atoi(heightStr) : 
                                            (grandChild->isButton ? 35 : 30);
                grandChild->x = child->x + 10;
                grandChild->y = childY;
                childY += grandChild->computedHeight + 5;
            }
        }
    }
}

// ============================================================================
// LAYOUT ENGINE
// ============================================================================

static void computeLayout(Node* node, int parentWidth, int parentHeight, 
                         int offsetX, int offsetY) {
    if (!node) return;
    
    // Check if this is a flex container
    if (node->flex.isFlexContainer && node->childCount > 0) {
        const char* widthStr = getStyle(node, "width");
        const char* heightStr = getStyle(node, "height");
        
        node->computedWidth = widthStr ? atoi(widthStr) : 
                             (parentWidth > 0 ? parentWidth - 20 : 600);
        node->computedHeight = heightStr ? atoi(heightStr) : 
                              (parentHeight > 0 ? parentHeight - 20 : 400);
        
        node->x = offsetX;
        node->y = offsetY;
        
        computeFlexLayout(node, node->computedWidth - 20, node->computedHeight - 20,
                         offsetX + 10, offsetY + 10);
        return;
    }
    
    // Regular layout (non-flex)
    const char* widthStr = getStyle(node, "width");
    const char* heightStr = getStyle(node, "height");
    
    if (widthStr) {
        node->computedWidth = atoi(widthStr);
    } else {
        if (node->isButton) {
            node->computedWidth = 120;
        } else {
            node->computedWidth = parentWidth > 0 ? parentWidth - 20 : 200;
        }
    }
    
    if (heightStr) {
        node->computedHeight = atoi(heightStr);
    } else {
        if (node->isButton) {
            node->computedHeight = 35;
        } else {
            node->computedHeight = 30 + (node->childCount * 40);
        }
    }
    
    node->x = offsetX;
    node->y = offsetY;
    
    int childY = offsetY + 10;
    for (int i = 0; i < node->childCount; i++) {
        computeLayout(node->children[i], node->computedWidth - 20, -1, 
                     offsetX + 10, childY);
        childY += node->children[i]->computedHeight + 5;
    }
}

// ============================================================================
// EVENT HANDLING
// ============================================================================

static void executeOnClick(ReactUI* ui, const char* onClick) {
    if (!ui || !onClick) return;
    
    char command[128];
    safe_strcpy(command, onClick, sizeof(command));
    
    // Sanitize command - only allow alphanumeric, operators, and spaces
    for (char* p = command; *p; p++) {
        if (!isalnum(*p) && !strchr("+-=_ ", *p)) {
            reportError(ui, "Invalid character in onClick handler");
            return;
        }
    }
    
    if (strstr(command, "++")) {
        char varName[32] = {0};
        if (sscanf(command, "%31[^+]", varName) == 1) {
            char* trimmed = trim(varName);
            int currentValue = getState(ui, trimmed);
            setState(ui, trimmed, currentValue + 1);
        }
    } 
    else if (strstr(command, "--")) {
        char varName[32] = {0};
        if (sscanf(command, "%31[^-]", varName) == 1) {
            char* trimmed = trim(varName);
            int currentValue = getState(ui, trimmed);
            setState(ui, trimmed, currentValue - 1);
        }
    }
    else if (strstr(command, "+=")) {
        char varName[32] = {0};
        int addValue = 0;
        if (sscanf(command, "%31[^+]+=%d", varName, &addValue) == 2) {
            char* trimmed = trim(varName);
            int currentValue = getState(ui, trimmed);
            setState(ui, trimmed, currentValue + addValue);
        }
    }
    else if (strstr(command, "-=")) {
        char varName[32] = {0};
        int subValue = 0;
        if (sscanf(command, "%31[^-]-=%d", varName, &subValue) == 2) {
            char* trimmed = trim(varName);
            int currentValue = getState(ui, trimmed);
            setState(ui, trimmed, currentValue - subValue);
        }
    }
    else if (strchr(command, '=')) {
        char varName[32] = {0};
        int newValue = 0;
        if (sscanf(command, "%31[^=]=%d", varName, &newValue) == 2) {
            char* trimmed = trim(varName);
            setState(ui, trimmed, newValue);
        }
    }
    
    if (ui->hwnd) {
        InvalidateRect(ui->hwnd, NULL, TRUE);
    }
}

static Node* findNodeAtPoint(Node* node, int x, int y) {
    if (!node) return NULL;
    
    for (int i = node->childCount - 1; i >= 0; i--) {
        Node* found = findNodeAtPoint(node->children[i], x, y);
        if (found) return found;
    }
    
    if (node->isButton && 
        x >= node->x && x <= node->x + node->computedWidth &&
        y >= node->y && y <= node->y + node->computedHeight) {
        return node;
    }
    
    return NULL;
}

// ============================================================================
// RENDERER
// ============================================================================

static void renderNode(ReactUI* ui, HDC hdc, Node* node) {
    if (!node || !ui) return;
    
    const char* bgColor = getStyle(node, "background");
    const char* color = getStyle(node, "color");
    const char* fontSize = getStyle(node, "font-size");
    const char* border = getStyle(node, "border");
    const char* borderRadius = getStyle(node, "border-radius");
    const char* padding = getStyle(node, "padding");
    
    // Default button styles
    if (node->isButton && !bgColor) {
        bgColor = "#4CAF50";
    }
    if (node->isButton && !color) {
        color = "white";
    }
    
    // Render background
    if (bgColor) {
        HBRUSH brush = CreateSolidBrush(parseColor(bgColor));
        RECT rect = {node->x, node->y, 
                     node->x + node->computedWidth, 
                     node->y + node->computedHeight};
        
        if (borderRadius) {
            int radius = atoi(borderRadius);
            RoundRect(hdc, rect.left, rect.top, rect.right, rect.bottom, 
                     radius * 2, radius * 2);
        } else {
            FillRect(hdc, &rect, brush);
        }
        DeleteObject(brush);
    }
    
    // Render border
    if (border || node->isButton) {
        COLORREF borderColor = RGB(0, 0, 0);
        int borderWidth = 1;
        
        if (border) {
            // Simple border parsing (e.g., "2px solid #333")
            char borderStr[64];
            safe_strcpy(borderStr, border, sizeof(borderStr));
            char* token = strtok(borderStr, " ");
            if (token) borderWidth = atoi(token);
            token = strtok(NULL, " "); // skip style
            token = strtok(NULL, " ");
            if (token) borderColor = parseColor(token);
        }
        
        HPEN pen = CreatePen(PS_SOLID, borderWidth, borderColor);
        HPEN oldPen = (HPEN)SelectObject(hdc, pen);
        HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
        
        if (borderRadius) {
            int radius = atoi(borderRadius);
            RoundRect(hdc, node->x, node->y, 
                     node->x + node->computedWidth, 
                     node->y + node->computedHeight,
                     radius * 2, radius * 2);
        } else {
            Rectangle(hdc, node->x, node->y, 
                     node->x + node->computedWidth, 
                     node->y + node->computedHeight);
        }
        
        SelectObject(hdc, oldBrush);
        SelectObject(hdc, oldPen);
        DeleteObject(pen);
    }
    
    // Render text
    if (strlen(node->text) > 0) {
        char processedText[512];
        substituteVariables(ui, node->text, processedText, sizeof(processedText));
        
        SetTextColor(hdc, color ? parseColor(color) : RGB(0, 0, 0));
        SetBkMode(hdc, TRANSPARENT);
        
        HFONT hFont = NULL;
        HFONT hOldFont = NULL;
        if (fontSize) {
            int size = atoi(fontSize);
            hFont = CreateFont(size, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
            hOldFont = (HFONT)SelectObject(hdc, hFont);
        } else if (node->isButton) {
            hFont = CreateFont(16, 0, 0, 0, FW_BOLD, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
            hOldFont = (HFONT)SelectObject(hdc, hFont);
        }
        
        int paddingValue = padding ? atoi(padding) : 5;
        RECT textRect = {node->x + paddingValue, node->y + paddingValue, 
                        node->x + node->computedWidth - paddingValue, 
                        node->y + node->computedHeight - paddingValue};
        DrawText(hdc, processedText, -1, &textRect, 
                DT_CENTER | DT_VCENTER | DT_SINGLELINE | DT_WORDBREAK);
        
        if (hFont) {
            SelectObject(hdc, hOldFont);
            DeleteObject(hFont);
        }
    }
    
    // Render children
    for (int i = 0; i < node->childCount; i++) {
        renderNode(ui, hdc, node->children[i]);
    }
}

// ============================================================================
// WINDOW PROCEDURE
// ============================================================================

static LRESULT CALLBACK ReactUIWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ReactUI* ui = NULL;
    
    // Find the ReactUI instance for this window
    for (int i = 0; i < g_instanceCount; i++) {
        if (g_reactUIInstances[i] && g_reactUIInstances[i]->hwnd == hwnd) {
            ui = g_reactUIInstances[i];
            break;
        }
    }
    
    if (!ui) return DefWindowProc(hwnd, uMsg, wParam, lParam);
    
    switch (uMsg) {
        case WM_LBUTTONDOWN: {
            int x = LOWORD(lParam);
            int y = HIWORD(lParam);
            
            Node* clickedNode = findNodeAtPoint(ui->root, x, y);
            if (clickedNode && clickedNode->isButton && strlen(clickedNode->onClick) > 0) {
                executeOnClick(ui, clickedNode->onClick);
            }
            return 0;
        }

        case WM_PAINT: {
            PAINTSTRUCT ps;
            HDC hdc = BeginPaint(hwnd, &ps);
            
            RECT clientRect;
            GetClientRect(hwnd, &clientRect);
            FillRect(hdc, &clientRect, (HBRUSH)(COLOR_WINDOW+1));
            
            if (ui->root) {
                renderNode(ui, hdc, ui->root);
            }
            EndPaint(hwnd, &ps);
            return 0;
        }

        case WM_DESTROY: {
            // Remove from instances array
            for (int i = 0; i < g_instanceCount; i++) {
                if (g_reactUIInstances[i] == ui) {
                    g_reactUIInstances[i] = NULL;
                    break;
                }
            }
            
            if (ui->root) {
                freeNode(ui->root);
                ui->root = NULL;
            }
            PostQuitMessage(0);
            return 0;
        }
    }
    return DefWindowProc(hwnd, uMsg, wParam, lParam);
}

// ============================================================================
// PUBLIC API
// ============================================================================

// Initialize ReactUI
static ReactUI* ReactUI_Create(HINSTANCE hInstance) {
    if (g_instanceCount >= MAX_UI_INSTANCES) {
        return NULL;
    }
    
    ReactUI* ui = (ReactUI*)malloc(sizeof(ReactUI));
    if (!ui) return NULL;
    
    memset(ui, 0, sizeof(ReactUI));
    ui->hInstance = hInstance;
    ui->instanceId = g_instanceCount;
    ui->onError = NULL;
    
    g_reactUIInstances[g_instanceCount++] = ui;
    
    return ui;
}

// Set error callback
static void ReactUI_SetErrorCallback(ReactUI* ui, void (*callback)(const char*)) {
    if (ui) {
        ui->onError = callback;
    }
}

// Set initial state
static void ReactUI_SetState(ReactUI* ui, const char* name, int value) {
    setState(ui, name, value);
}

// Get current state
static int ReactUI_GetState(ReactUI* ui, const char* name) {
    return getState(ui, name);
}

// Render HTML
static void ReactUI_Render(ReactUI* ui, const char* html) {
    if (!ui || !html) return;
    
    if (ui->root) {
        freeNode(ui->root);
        ui->root = NULL;
    }
    
    const char* p = html;
    ui->root = parseHTML(&p);
}

// Create window and show UI
static HWND ReactUI_CreateWindow(ReactUI* ui, const char* title, int width, int height) {
    if (!ui) return NULL;
    
    char className[64];
    safe_sprintf(className, sizeof(className), "ReactUIWindow_%d", ui->instanceId);
    
    WNDCLASS wc = {0};
    wc.lpfnWndProc = ReactUIWindowProc;
    wc.hInstance = ui->hInstance;
    wc.lpszClassName = className;
    wc.hbrBackground = (HBRUSH)(COLOR_WINDOW+1);
    wc.hCursor = LoadCursor(NULL, IDC_HAND);

    if (!RegisterClass(&wc)) {
        return NULL;
    }

    ui->hwnd = CreateWindowEx(
        0, className, title ? title : "ReactUI Window",
        WS_OVERLAPPEDWINDOW | WS_VISIBLE,
        CW_USEDEFAULT, CW_USEDEFAULT, width, height,
        NULL, NULL, ui->hInstance, NULL
    );
    
    if (ui->root) {
        computeLayout(ui->root, width - 40, height - 60, 10, 10);
    }
    
    return ui->hwnd;
}

// Run message loop
static int ReactUI_Run() {
    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

// Force re-render
static void ReactUI_ForceUpdate(ReactUI* ui) {
    if (ui && ui->hwnd) {
        InvalidateRect(ui->hwnd, NULL, TRUE);
    }
}

// Cleanup
static void ReactUI_Destroy(ReactUI* ui) {
    if (!ui) return;
    
    // Remove from instances
    for (int i = 0; i < g_instanceCount; i++) {
        if (g_reactUIInstances[i] == ui) {
            g_reactUIInstances[i] = NULL;
            break;
        }
    }
    
    if (ui->root) {
        freeNode(ui->root);
    }
    free(ui);
}

#endif // REACT_UI_HYBRID_H