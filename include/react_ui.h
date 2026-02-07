#ifndef REACT_UI_OPTIMIZED_H
#define REACT_UI_OPTIMIZED_H

#include <windows.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <ctype.h>
#include <stdbool.h>

// ============================================================================
// HTML MACRO FOR CLEANER SYNTAX
// ============================================================================

#define HTML(...) #__VA_ARGS__

#define MAX_CHILDREN 32
#define MAX_STYLES 16
#define MAX_STATES 32
#define MAX_UI_INSTANCES 8
#define MAX_DEPENDENCIES 16
#define MAX_DEPENDENT_NODES 64

// ============================================================================
// CORE DATA STRUCTURES
// ============================================================================

typedef struct CSSProperty {
    char name[32];
    char value[64];
} CSSProperty;

// State with type support
typedef enum {
    STATE_INT,
    STATE_FLOAT,
    STATE_STRING,
    STATE_BOOL
} StateType;

typedef union {
    int intVal;
    float floatVal;
    char strVal[256];
    bool boolVal;
} StateValue;

typedef struct State {
    char name[32];
    StateType type;
    StateValue value;
    StateValue prevValue;
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
    char class[256];
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
    
    // NEW: Selective rendering
    bool isDirty;
    bool needsLayout;
    RECT boundingBox;
    
    // NEW: Dependency tracking
    char dependencies[MAX_DEPENDENCIES][32];
    int depCount;
    
} Node;

// Dependency map: tracks which nodes depend on which state variables
typedef struct {
    char varName[32];
    Node* dependentNodes[MAX_DEPENDENT_NODES];
    int nodeCount;
} VariableDependency;

typedef struct ReactUI {
    Node* root;
    HWND hwnd;
    StateManager stateManager;
    HINSTANCE hInstance;
    int instanceId;
    void (*onError)(const char* message);
    
    // NEW: Dependency tracking
    VariableDependency dependencies[MAX_STATES];
    int depCount;
    
} ReactUI;

// ============================================================================
// GLOBAL STATE
// ============================================================================

static ReactUI* g_reactUIInstances[MAX_UI_INSTANCES] = {0};
static int g_instanceCount = 0;
static ReactUI* g_currentUI = NULL;

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
    if (strcmp(color, "white") == 0) return RGB(255, 255, 255);
    if (strcmp(color, "black") == 0) return RGB(0, 0, 0);
    if (strcmp(color, "gray") == 0 || strcmp(color, "grey") == 0) return RGB(128, 128, 128);
    
    return RGB(255, 255, 255);
}

static void reportError(ReactUI* ui, const char* message) {
    if (ui && ui->onError) {
        ui->onError(message);
    }
}

// ============================================================================
// STATE MANAGEMENT API (WITH TYPES)
// ============================================================================

static void setState_internal(ReactUI* ui, const char* name, StateType type, StateValue value);
static StateValue getState_internal(ReactUI* ui, const char* name);

static void setState_internal(ReactUI* ui, const char* name, StateType type, StateValue value) {
    if (!ui || !name) return;
    
    // Find existing state
    for (int i = 0; i < ui->stateManager.stateCount; i++) {
        if (strcmp(ui->stateManager.states[i].name, name) == 0) {
            ui->stateManager.states[i].prevValue = ui->stateManager.states[i].value;
            ui->stateManager.states[i].value = value;
            return;
        }
    }
    
    // Create new state
    if (ui->stateManager.stateCount < MAX_STATES) {
        safe_strcpy(ui->stateManager.states[ui->stateManager.stateCount].name, 
                    name, sizeof(ui->stateManager.states[0].name));
        ui->stateManager.states[ui->stateManager.stateCount].type = type;
        ui->stateManager.states[ui->stateManager.stateCount].value = value;
        ui->stateManager.states[ui->stateManager.stateCount].prevValue = value;
        ui->stateManager.stateCount++;
    } else {
        reportError(ui, "Maximum state count exceeded");
    }
}

static StateValue getState_internal(ReactUI* ui, const char* name) {
    StateValue empty = {0};
    if (!ui || !name) return empty;
    
    for (int i = 0; i < ui->stateManager.stateCount; i++) {
        if (strcmp(ui->stateManager.states[i].name, name) == 0) {
            return ui->stateManager.states[i].value;
        }
    }
    return empty;
}

// Convenience functions
static void setStateInt(ReactUI* ui, const char* name, int value) {
    StateValue v = {0};
    v.intVal = value;
    setState_internal(ui, name, STATE_INT, v);
}

static int getStateInt(ReactUI* ui, const char* name) {
    return getState_internal(ui, name).intVal;
}

static void setStateString(ReactUI* ui, const char* name, const char* value) {
    StateValue v = {0};
    safe_strcpy(v.strVal, value, sizeof(v.strVal));
    setState_internal(ui, name, STATE_STRING, v);
}

static const char* getStateString(ReactUI* ui, const char* name) {
    return getState_internal(ui, name).strVal;
}

static void setStateBool(ReactUI* ui, const char* name, bool value) {
    StateValue v = {0};
    v.boolVal = value;
    setState_internal(ui, name, STATE_BOOL, v);
}

static bool getStateBool(ReactUI* ui, const char* name) {
    return getState_internal(ui, name).boolVal;
}

// ============================================================================
// DEPENDENCY TRACKING
// ============================================================================

static void registerDependency(ReactUI* ui, const char* varName, Node* node) {
    if (!ui || !varName || !node) return;
    
    // Find existing dependency entry
    for (int i = 0; i < ui->depCount; i++) {
        if (strcmp(ui->dependencies[i].varName, varName) == 0) {
            // Add node to this dependency
            if (ui->dependencies[i].nodeCount < MAX_DEPENDENT_NODES) {
                ui->dependencies[i].dependentNodes[ui->dependencies[i].nodeCount++] = node;
            }
            return;
        }
    }
    
    // Create new dependency entry
    if (ui->depCount < MAX_STATES) {
        safe_strcpy(ui->dependencies[ui->depCount].varName, varName, 
                    sizeof(ui->dependencies[0].varName));
        ui->dependencies[ui->depCount].dependentNodes[0] = node;
        ui->dependencies[ui->depCount].nodeCount = 1;
        ui->depCount++;
    }
}

static void invalidateNode(ReactUI* ui, Node* node) {
    if (!ui || !node) return;
    
    node->isDirty = true;
    
    // Invalidate this node's rectangle
    if (ui->hwnd) {
        InvalidateRect(ui->hwnd, &node->boundingBox, FALSE);
    }
}

static void markDependentNodesDirty(ReactUI* ui, const char* varName) {
    if (!ui || !varName) return;
    
    // Find all nodes that depend on this variable
    for (int i = 0; i < ui->depCount; i++) {
        if (strcmp(ui->dependencies[i].varName, varName) == 0) {
            // Mark all dependent nodes as dirty
            for (int j = 0; j < ui->dependencies[i].nodeCount; j++) {
                invalidateNode(ui, ui->dependencies[i].dependentNodes[j]);
            }
            return;
        }
    }
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
    
    // Initialize selective rendering
    node->isDirty = true;  // New nodes start dirty
    node->needsLayout = true;
    
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
// VARIABLE SUBSTITUTION WITH DEPENDENCY TRACKING
// ============================================================================

static void extractAndRegisterDependencies(ReactUI* ui, Node* node, const char* text) {
    if (!ui || !node || !text) return;
    
    const char* start = strstr(text, "{{");
    while (start) {
        start += 2;
        const char* end = strstr(start, "}}");
        if (!end) break;
        
        int len = (int)(end - start);
        if (len > 0 && len < 32 && node->depCount < MAX_DEPENDENCIES) {
            // Extract variable name
            char varName[32];
            strncpy(varName, start, len);
            varName[len] = '\0';
            
            // Trim whitespace
            char* trimmed = trim(varName);
            
            // Store in node's dependencies
            safe_strcpy(node->dependencies[node->depCount], trimmed, 
                       sizeof(node->dependencies[0]));
            node->depCount++;
            
            // Register in global dependency map
            registerDependency(ui, trimmed, node);
        }
        
        start = strstr(end + 2, "{{");
    }
}

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
            
            char* trimmedVar = trim(varName);
            
            // Get state value (as int for now - can be extended)
            int value = getStateInt(ui, trimmedVar);
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
    
    const char* flexGrow = getStyle(node, "flex-grow");
    if (flexGrow) {
        node->flex.flexGrow = atoi(flexGrow);
    }
    
    const char* gap = getStyle(node, "gap");
    if (gap) {
        node->flex.gap = atoi(gap);
    }
}

// ============================================================================
// CSS PARSER (SIMPLIFIED)
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
    
    parseFlexProperties(node);
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

static Node* parseElement(ReactUI* ui, const char** htmlPtr);

static Node* parseElement(ReactUI* ui, const char** htmlPtr) {
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
                        
                        // Extract dependencies from text
                        if (ui) {
                            extractAndRegisterDependencies(ui, node, node->text);
                        }
                    }
                    
                    while (*p && *p != '>') p++;
                    if (*p == '>') p++;
                    tagDepth--;
                    break;
                }
            } else if (*(p+1) != '!') {
                Node* child = parseElement(ui, &p);
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

static Node* parseHTML(ReactUI* ui, const char** htmlPtr) {
    if (!htmlPtr || !*htmlPtr) return NULL;
    
    Node* root = createNode("root");
    if (!root) return NULL;
    
    const char* p = *htmlPtr;
    
    while (*p) {
        Node* element = parseElement(ui, &p);
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
// FLEXBOX LAYOUT ENGINE (SIMPLIFIED)
// ============================================================================

static void computeFlexLayout(Node* node, int availableWidth, int availableHeight, 
                              int offsetX, int offsetY) {
    if (!node || !node->flex.isFlexContainer) return;
    
    int isRow = (node->flex.direction == FLEX_DIRECTION_ROW || 
                 node->flex.direction == FLEX_DIRECTION_ROW_REVERSE);
    
    int totalFlexGrow = 0;
    int fixedSize = 0;
    int flexItemCount = 0;
    
    for (int i = 0; i < node->childCount; i++) {
        Node* child = node->children[i];
        totalFlexGrow += child->flex.flexGrow;
        
        if (isRow) {
            const char* widthStr = getStyle(child, "width");
            if (widthStr) {
                fixedSize += atoi(widthStr);
            }
        } else {
            const char* heightStr = getStyle(child, "height");
            if (heightStr) {
                fixedSize += atoi(heightStr);
            }
        }
        flexItemCount++;
    }
    
    int totalGap = node->flex.gap * (flexItemCount > 0 ? flexItemCount - 1 : 0);
    fixedSize += totalGap;
    
    int availableSpace = isRow ? availableWidth : availableHeight;
    int remainingSpace = availableSpace - fixedSize;
    
    int flexGrowUnit = (totalFlexGrow > 0 && remainingSpace > 0) ? 
                       remainingSpace / totalFlexGrow : 0;
    
    int currentPos = 0;
    switch (node->flex.justify) {
        case JUSTIFY_FLEX_START:
            currentPos = 0;
            break;
        case JUSTIFY_CENTER:
            currentPos = remainingSpace > 0 ? remainingSpace / 2 : 0;
            break;
        case JUSTIFY_FLEX_END:
            currentPos = remainingSpace > 0 ? remainingSpace : 0;
            break;
        default:
            currentPos = 0;
            break;
    }
    
    for (int i = 0; i < node->childCount; i++) {
        Node* child = node->children[i];
        
        int childMainSize = 0;
        int childCrossSize = 0;
        
        if (isRow) {
            const char* widthStr = getStyle(child, "width");
            childMainSize = widthStr ? atoi(widthStr) : 100;
            
            if (child->flex.flexGrow > 0 && remainingSpace > 0) {
                childMainSize += flexGrowUnit * child->flex.flexGrow;
            }
            
            const char* heightStr = getStyle(child, "height");
            childCrossSize = heightStr ? atoi(heightStr) : 30;
            
            child->computedWidth = childMainSize;
            child->computedHeight = childCrossSize;
            child->x = offsetX + currentPos;
            child->y = offsetY;
            
            currentPos += childMainSize + node->flex.gap;
            
        } else {
            const char* heightStr = getStyle(child, "height");
            childMainSize = heightStr ? atoi(heightStr) : 30;
            
            if (child->flex.flexGrow > 0 && remainingSpace > 0) {
                childMainSize += flexGrowUnit * child->flex.flexGrow;
            }
            
            const char* widthStr = getStyle(child, "width");
            childCrossSize = widthStr ? atoi(widthStr) : 100;
            
            child->computedWidth = childCrossSize;
            child->computedHeight = childMainSize;
            child->x = offsetX;
            child->y = offsetY + currentPos;
            
            currentPos += childMainSize + node->flex.gap;
        }
        
        // Update bounding box
        child->boundingBox.left = child->x;
        child->boundingBox.top = child->y;
        child->boundingBox.right = child->x + child->computedWidth;
        child->boundingBox.bottom = child->y + child->computedHeight;
        
        if (child->flex.isFlexContainer) {
            computeFlexLayout(child, child->computedWidth - 20, 
                            child->computedHeight - 20, 
                            child->x + 10, child->y + 10);
        }
    }
}

// ============================================================================
// LAYOUT ENGINE
// ============================================================================

static void computeLayout(Node* node, int parentWidth, int parentHeight, 
                         int offsetX, int offsetY) {
    if (!node) return;
    
    if (node->flex.isFlexContainer && node->childCount > 0) {
        const char* widthStr = getStyle(node, "width");
        const char* heightStr = getStyle(node, "height");
        
        node->computedWidth = widthStr ? atoi(widthStr) : 
                             (parentWidth > 0 ? parentWidth - 20 : 600);
        node->computedHeight = heightStr ? atoi(heightStr) : 
                              (parentHeight > 0 ? parentHeight - 20 : 400);
        
        node->x = offsetX;
        node->y = offsetY;
        
        node->boundingBox.left = node->x;
        node->boundingBox.top = node->y;
        node->boundingBox.right = node->x + node->computedWidth;
        node->boundingBox.bottom = node->y + node->computedHeight;
        
        computeFlexLayout(node, node->computedWidth - 20, node->computedHeight - 20,
                         offsetX + 10, offsetY + 10);
        return;
    }
    
    const char* widthStr = getStyle(node, "width");
    const char* heightStr = getStyle(node, "height");
    
    if (widthStr) {
        node->computedWidth = atoi(widthStr);
    } else {
        node->computedWidth = node->isButton ? 120 : (parentWidth > 0 ? parentWidth - 20 : 200);
    }
    
    if (heightStr) {
        node->computedHeight = atoi(heightStr);
    } else {
        // Set default heights based on element type
        if (node->isButton) {
            node->computedHeight = 35;
        } else if (strcmp(node->tag, "h1") == 0) {
            node->computedHeight = 50;
        } else if (strcmp(node->tag, "h2") == 0) {
            node->computedHeight = 40;
        } else if (strcmp(node->tag, "h3") == 0) {
            node->computedHeight = 35;
        } else if (strcmp(node->tag, "p") == 0) {
            node->computedHeight = 30;
        } else {
            node->computedHeight = 30;
        }
    }
    
    node->x = offsetX;
    node->y = offsetY;
    
    node->boundingBox.left = node->x;
    node->boundingBox.top = node->y;
    node->boundingBox.right = node->x + node->computedWidth;
    node->boundingBox.bottom = node->y + node->computedHeight;
    
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
            int currentValue = getStateInt(ui, trimmed);
            setStateInt(ui, trimmed, currentValue + 1);
            markDependentNodesDirty(ui, trimmed);
        }
    } 
    else if (strstr(command, "--")) {
        char varName[32] = {0};
        if (sscanf(command, "%31[^-]", varName) == 1) {
            char* trimmed = trim(varName);
            int currentValue = getStateInt(ui, trimmed);
            setStateInt(ui, trimmed, currentValue - 1);
            markDependentNodesDirty(ui, trimmed);
        }
    }
    else if (strchr(command, '=')) {
        char varName[32] = {0};
        int newValue = 0;
        if (sscanf(command, "%31[^=]=%d", varName, &newValue) == 2) {
            char* trimmed = trim(varName);
            setStateInt(ui, trimmed, newValue);
            markDependentNodesDirty(ui, trimmed);
        }
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
// RENDERER WITH SELECTIVE UPDATE
// ============================================================================

static bool hasAnyDirtyChildren(Node* node) {
    if (!node) return false;
    
    for (int i = 0; i < node->childCount; i++) {
        if (node->children[i]->isDirty) return true;
        if (hasAnyDirtyChildren(node->children[i])) return true;
    }
    return false;
}

static void renderNode(ReactUI* ui, HDC hdc, Node* node) {
    if (!node || !ui) return;
    
    // OPTIMIZATION: Skip clean subtrees
    if (!node->isDirty && !hasAnyDirtyChildren(node)) {
        return;
    }
    
    // Render this node if dirty
    if (node->isDirty) {
        const char* bgColor = getStyle(node, "background");
        const char* color = getStyle(node, "color");
        const char* fontSize = getStyle(node, "font-size");
        const char* border = getStyle(node, "border");
        const char* padding = getStyle(node, "padding");
        
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
            FillRect(hdc, &rect, brush);
            DeleteObject(brush);
        }
        
        // Render border
        if (border || node->isButton) {
            COLORREF borderColor = RGB(0, 0, 0);
            HPEN pen = CreatePen(PS_SOLID, 1, borderColor);
            HPEN oldPen = (HPEN)SelectObject(hdc, pen);
            HBRUSH oldBrush = (HBRUSH)SelectObject(hdc, GetStockObject(NULL_BRUSH));
            
            Rectangle(hdc, node->x, node->y, 
                     node->x + node->computedWidth, 
                     node->y + node->computedHeight);
            
            SelectObject(hdc, oldBrush);
            SelectObject(hdc, oldPen);
            DeleteObject(pen);
        }
        
        // Render text with variable substitution
        if (strlen(node->text) > 0) {
            char processedText[512];
            substituteVariables(ui, node->text, processedText, sizeof(processedText));
            
            SetTextColor(hdc, color ? parseColor(color) : RGB(0, 0, 0));
            SetBkMode(hdc, TRANSPARENT);
            
            HFONT hFont = NULL;
            HFONT hOldFont = NULL;
            
            // Determine font size and weight
            int fontHeight = 16;  // Default size
            int fontWeight = FW_NORMAL;
            
            if (fontSize) {
                fontHeight = atoi(fontSize);
            } else if (strcmp(node->tag, "h1") == 0) {
                fontHeight = 32;
                fontWeight = FW_BOLD;
            } else if (strcmp(node->tag, "h2") == 0) {
                fontHeight = 24;
                fontWeight = FW_BOLD;
            } else if (strcmp(node->tag, "h3") == 0) {
                fontHeight = 20;
                fontWeight = FW_BOLD;
            } else if (strcmp(node->tag, "p") == 0) {
                fontHeight = 16;
            } else if (node->isButton) {
                fontHeight = 16;
                fontWeight = FW_BOLD;
            }
            
            hFont = CreateFont(fontHeight, 0, 0, 0, fontWeight, FALSE, FALSE, FALSE,
                              DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                              DEFAULT_QUALITY, DEFAULT_PITCH | FF_DONTCARE, "Arial");
            hOldFont = (HFONT)SelectObject(hdc, hFont);
            
            int paddingValue = padding ? atoi(padding) : 5;
            RECT textRect = {node->x + paddingValue, node->y + paddingValue, 
                            node->x + node->computedWidth - paddingValue, 
                            node->y + node->computedHeight - paddingValue};
            
            // Different text alignment for different elements
            UINT textFormat = DT_VCENTER | DT_WORDBREAK;
            if (node->isButton) {
                textFormat |= DT_CENTER | DT_SINGLELINE;
            } else if (strcmp(node->tag, "h1") == 0 || strcmp(node->tag, "h2") == 0) {
                textFormat |= DT_LEFT | DT_SINGLELINE;
            } else {
                textFormat |= DT_LEFT;
            }
            
            DrawText(hdc, processedText, -1, &textRect, textFormat);
            
            if (hFont) {
                SelectObject(hdc, hOldFont);
                DeleteObject(hFont);
            }
        }
        
        node->isDirty = false;  // Clear dirty flag
    }
    
    // Recursively render children
    for (int i = 0; i < node->childCount; i++) {
        renderNode(ui, hdc, node->children[i]);
    }
}

// ============================================================================
// WINDOW PROCEDURE
// ============================================================================

static LRESULT CALLBACK ReactUIWindowProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam) {
    ReactUI* ui = NULL;
    
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
    g_currentUI = ui;
    
    return ui;
}

static void ReactUI_SetErrorCallback(ReactUI* ui, void (*callback)(const char*)) {
    if (ui) {
        ui->onError = callback;
    }
}

static void ReactUI_Render(ReactUI* ui, const char* html) {
    if (!ui || !html) return;
    
    if (ui->root) {
        freeNode(ui->root);
        ui->root = NULL;
    }
    
    // Clear dependency map
    ui->depCount = 0;
    memset(ui->dependencies, 0, sizeof(ui->dependencies));
    
    const char* p = html;
    ui->root = parseHTML(ui, &p);
}

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

static int ReactUI_Run() {
    MSG msg;
    while(GetMessage(&msg, NULL, 0, 0)) {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return (int)msg.wParam;
}

static void ReactUI_Destroy(ReactUI* ui) {
    if (!ui) return;
    
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

// ============================================================================
// STATE MACRO API (React-like)
// ============================================================================

#define STATE(type, varName, setterName, init) \
    static type varName = init; \
    static void setterName(type value) { \
        if (varName != value) { \
            varName = value; \
            if (g_currentUI) { \
                setStateInt(g_currentUI, #varName, (int)value); \
                markDependentNodesDirty(g_currentUI, #varName); \
            } \
        } \
    }

#define STATE_STR(varName, setterName, init) \
    static char varName[256] = init; \
    static void setterName(const char* value) { \
        if (strcmp(varName, value) != 0) { \
            strncpy(varName, value, 255); \
            varName[255] = '\0'; \
            if (g_currentUI) { \
                setStateString(g_currentUI, #varName, value); \
                markDependentNodesDirty(g_currentUI, #varName); \
            } \
        } \
    }

#endif // REACT_UI_OPTIMIZED_H