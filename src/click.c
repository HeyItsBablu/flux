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

struct Node;
typedef void (*OnClickHandler)(struct Node*);

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

    OnClickHandler onClick;   // 👈 EVENT HANDLER
} Node;

/* ---------------- UTIL ---------------- */

char* trim(char* str) {
    while (isspace(*str)) str++;
    if (!*str) return str;
    char* end = str + strlen(str) - 1;
    while (end > str && isspace(*end)) end--;
    end[1] = 0;
    return str;
}

COLORREF parseColor(const char* c) {
    if (!c) return RGB(255,255,255);

    if (c[0] == '#') {
        unsigned r,g,b;
        sscanf(c+1,"%02x%02x%02x",&r,&g,&b);
        return RGB(r,g,b);
    }

    if (!strcmp(c,"red")) return RGB(255,0,0);
    if (!strcmp(c,"blue")) return RGB(0,0,255);
    if (!strcmp(c,"green")) return RGB(0,255,0);
    if (!strcmp(c,"white")) return RGB(255,255,255);
    if (!strcmp(c,"black")) return RGB(0,0,0);
    if (!strcmp(c,"gray")) return RGB(128,128,128);

    return RGB(255,255,255);
}

/* ---------------- DOM ---------------- */

Node* createNode(const char* tag) {
    Node* n = calloc(1,sizeof(Node));
    strcpy(n->tag, tag);
    n->width = -1;
    n->height = -1;
    return n;
}

void addChild(Node* p, Node* c) {
    if (p->childCount < MAX_CHILDREN) {
        p->children[p->childCount++] = c;
        c->parent = p;
    }
}

void addStyle(Node* n,const char* name,const char* val) {
    for(int i=0;i<n->styleCount;i++)
        if(!strcmp(n->styles[i].name,name)){
            strcpy(n->styles[i].value,val);
            return;
        }
    if(n->styleCount<MAX_STYLES){
        strcpy(n->styles[n->styleCount].name,name);
        strcpy(n->styles[n->styleCount].value,val);
        n->styleCount++;
    }
}

const char* getStyle(Node* n,const char* name){
    for(int i=0;i<n->styleCount;i++)
        if(!strcmp(n->styles[i].name,name))
            return n->styles[i].value;
    return NULL;
}

/* ---------------- HTML PARSER ---------------- */

void parseInlineStyle(Node* n,const char* s){
    char buf[512];
    strcpy(buf,s);
    char* tok=strtok(buf,";");
    while(tok){
        char* c=strchr(tok,':');
        if(c){
            *c=0;
            addStyle(n,trim(tok),trim(c+1));
        }
        tok=strtok(NULL,";");
    }
}

const char* parseAttrs(const char* p,Node* n){
    char name[32],val[256];
    while(*p && *p!='>' && *p!='/'){
        while(isspace(*p)) p++;
        int i=0;
        while(*p && *p!='=' && !isspace(*p)) name[i++]=*p++;
        name[i]=0;
        if(*p=='='){
            p++;
            char q=*p=='"'||*p=='\''?*p++:0;
            i=0;
            while(*p && (q?*p!=q:!isspace(*p)))
                val[i++]=*p++;
            val[i]=0;
            if(q) p++;
            if(!strcmp(name,"style")) parseInlineStyle(n,val);
            if(!strcmp(name,"id")) strcpy(n->id,val);
        }
    }
    return p;
}

Node* parseElement(const char** h){
    const char* p=*h;
    while(*p && *p!='<') p++;
    if(!*p) return NULL;
    p++;

    if(*p=='/') return NULL;

    char tag[16]; int i=0;
    while(*p && !isspace(*p) && *p!='>') tag[i++]=tolower(*p++);
    tag[i]=0;

    Node* n=createNode(tag);
    p=parseAttrs(p,n);
    if(*p=='>') p++;

    const char* text=p;
    while(*p){
        if(*p=='<' && *(p+1)=='/'){
            int len=p-text;
            if(len>0){
                strncpy(n->text,text,len);
                n->text[len]=0;
                strcpy(n->text,trim(n->text));
            }
            while(*p && *p!='>') p++;
            if(*p) p++;
            break;
        }
        if(*p=='<' && *(p+1)!='/'){
            Node* c=parseElement(&p);
            if(c) addChild(n,c);
            text=p;
            continue;
        }
        p++;
    }
    *h=p;
    return n;
}

Node* parseHTML(const char** h){
    Node* root=createNode("root");
    while(*h && **h){
        Node* e=parseElement(h);
        if(e) addChild(root,e);
        else break;
    }
    return root;
}

/* ---------------- LAYOUT ---------------- */

void computeLayout(Node* n,int pw,int ox,int oy){
    if(!n) return;
    n->computedWidth = getStyle(n,"width") ? atoi(getStyle(n,"width")) : pw-20;
    n->computedHeight = getStyle(n,"height") ? atoi(getStyle(n,"height")) : 40;
    n->x=ox; n->y=oy;

    int cy=oy+10;
    for(int i=0;i<n->childCount;i++){
        computeLayout(n->children[i],n->computedWidth,ox+10,cy);
        cy+=n->children[i]->computedHeight+5;
    }
}

/* ---------------- RENDER ---------------- */

void renderNode(HDC h,Node* n){
    if(!n) return;
    const char* bg=getStyle(n,"background");
    if(bg){
        HBRUSH b=CreateSolidBrush(parseColor(bg));
        RECT r={n->x,n->y,n->x+n->computedWidth,n->y+n->computedHeight};
        FillRect(h,&r,b);
        DeleteObject(b);
    }

    if(*n->text){
        SetBkMode(h,TRANSPARENT);
        RECT r={n->x+5,n->y+5,n->x+n->computedWidth,n->y+n->computedHeight};
        DrawText(h,n->text,-1,&r,DT_LEFT);
    }

    for(int i=0;i<n->childCount;i++)
        renderNode(h,n->children[i]);
}

/* ---------------- EVENTS ---------------- */

Node* hitTest(Node* n,int mx,int my){
    if(mx>=n->x && mx<=n->x+n->computedWidth &&
       my>=n->y && my<=n->y+n->computedHeight){
        for(int i=0;i<n->childCount;i++){
            Node* h=hitTest(n->children[i],mx,my);
            if(h) return h;
        }
        return n;
    }
    return NULL;
}

void toggleColor(Node* n){
    const char* bg=getStyle(n,"background");
    addStyle(n,"background", bg && !strcmp(bg,"red") ? "blue":"red");
}

void bindHandlers(Node* n){
    if(!strcmp(n->id,"btn"))
        n->onClick=toggleColor;
    for(int i=0;i<n->childCount;i++)
        bindHandlers(n->children[i]);
}

/* ---------------- WINDOW ---------------- */

Node* root=NULL;

LRESULT CALLBACK WndProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_CREATE:{
        const char* html=
            "<div id='btn' style='background:red;width:200;height:40;'>"
            "Click Me"
            "</div>";
        root=parseHTML(&html);
        computeLayout(root,300,20,20);
        bindHandlers(root);
        return 0;
    }
    case WM_LBUTTONDOWN:{
        int x=LOWORD(lp),y=HIWORD(lp);
        Node* h=hitTest(root,x,y);
        if(h && h->onClick){
            h->onClick(h);
            InvalidateRect(w,NULL,TRUE);
        }
        return 0;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps;
        HDC h=BeginPaint(w,&ps);
        renderNode(h,root);
        EndPaint(w,&ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w,m,wp,lp);
}

int WINAPI WinMain(HINSTANCE h, HINSTANCE p, LPSTR c, int n){
    WNDCLASS wc={0};
    wc.lpfnWndProc=WndProc;
    wc.hInstance=h;
    wc.lpszClassName="MiniDOM";
    RegisterClass(&wc);

    CreateWindow("MiniDOM","HTML UI with C Events",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        200,200,400,300,
        NULL,NULL,h,NULL);

    MSG msg;
    while(GetMessage(&msg,NULL,0,0)){
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
