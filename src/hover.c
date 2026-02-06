#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#define MAX_CHILDREN 32
#define MAX_STYLES 16

/* ===================== NODE ===================== */

typedef enum {
    STATE_NORMAL,
    STATE_HOVER,
    STATE_ACTIVE,
    STATE_FOCUSED
} UIState;

struct Node;
typedef void (*EventFn)(struct Node*);

typedef struct {
    char name[32];
    char value[64];
} CSSProperty;

typedef struct Node {
    char tag[16];
    char text[256];
    char id[32];

    CSSProperty styles[MAX_STYLES];
    int styleCount;

    int x,y,w,h;
    int computedW, computedH;

    UIState state;
    int focusable;

    char onclick[32];
    EventFn onClickFn;

    struct Node* parent;
    struct Node* children[MAX_CHILDREN];
    int childCount;
} Node;

/* ===================== GLOBAL ===================== */

Node* root = NULL;
Node* hovered = NULL;
Node* active = NULL;
Node* focused = NULL;

/* ===================== UTIL ===================== */

char* trim(char* s){
    while(isspace(*s)) s++;
    char* e=s+strlen(s)-1;
    while(e>s && isspace(*e)) *e--=0;
    return s;
}

COLORREF color(const char* c){
    if(!c) return RGB(240,240,240);
    if(!strcmp(c,"red")) return RGB(255,80,80);
    if(!strcmp(c,"blue")) return RGB(80,80,255);
    if(!strcmp(c,"green")) return RGB(80,200,80);
    if(!strcmp(c,"gray")) return RGB(180,180,180);
    if(!strcmp(c,"dark")) return RGB(80,80,80);
    return RGB(200,200,200);
}

/* ===================== DOM ===================== */

Node* node(const char* tag){
    Node* n=calloc(1,sizeof(Node));
    strcpy(n->tag,tag);
    n->state=STATE_NORMAL;
    return n;
}

void add(Node* p, Node* c){
    p->children[p->childCount++]=c;
    c->parent=p;
}

void style(Node* n,const char* k,const char* v){
    for(int i=0;i<n->styleCount;i++)
        if(!strcmp(n->styles[i].name,k)){
            strcpy(n->styles[i].value,v);
            return;
        }
    strcpy(n->styles[n->styleCount].name,k);
    strcpy(n->styles[n->styleCount++].value,v);
}

const char* css(Node* n,const char* k){
    for(int i=0;i<n->styleCount;i++)
        if(!strcmp(n->styles[i].name,k))
            return n->styles[i].value;
    return NULL;
}

/* ===================== EVENTS ===================== */

void toggleColor(Node* n){
    const char* bg=css(n,"background");
    style(n,"background", bg && !strcmp(bg,"red") ? "blue":"red");
}

EventFn resolveFn(const char* name){
    if(!strcmp(name,"toggleColor")) return toggleColor;
    return NULL;
}

/* ===================== HTML PARSER (compact) ===================== */

Node* parse(const char** h){
    const char* p=*h;
    while(*p && *p!='<') p++;
    if(!*p) return NULL;
    p++;

    char tag[16]={0}; int i=0;
    while(*p && *p!='>' && !isspace(*p)) tag[i++]=*p++;
    Node* n=node(tag);

    while(*p && *p!='>'){
        while(isspace(*p)) p++;
        char k[32]={0},v[64]={0};
        i=0;
        while(*p && *p!='=' && *p!='>') k[i++]=*p++;
        if(*p=='='){
            p++; char q=*p++;
            i=0;
            while(*p && *p!=q) v[i++]=*p++;
            p++;
        }
        if(!strcmp(k,"style")){
            char* s=strtok(v,";");
            while(s){
                char* c=strchr(s,':');
                if(c){*c=0;style(n,trim(s),trim(c+1));}
                s=strtok(NULL,";");
            }
        }
        if(!strcmp(k,"onclick")) strcpy(n->onclick,v);
        if(!strcmp(k,"tabindex")) n->focusable=1;
    }
    p++;

    const char* t=p;
    while(*p){
        if(*p=='<' && *(p+1)=='/'){
            strncpy(n->text,t,p-t);
            while(*p && *p!='>') p++;
            p++;
            break;
        }
        if(*p=='<' && *(p+1)!='/'){
            Node* c=parse(&p);
            if(c) add(n,c);
            t=p;
            continue;
        }
        p++;
    }
    *h=p;
    return n;
}

/* ===================== LAYOUT (FLEX) ===================== */

void layout(Node* n,int x,int y,int w){
    n->x=x; n->y=y;
    n->computedW=css(n,"width")?atoi(css(n,"width")):w;
    n->computedH=css(n,"height")?atoi(css(n,"height")):40;

    int cx=x+5, cy=y+5;
    int row=!strcmp(css(n,"flex-direction")?: "column","row");

    for(int i=0;i<n->childCount;i++){
        Node* c=n->children[i];
        layout(c,cx,cy,n->computedW-10);
        if(row) cx+=c->computedW+5;
        else cy+=c->computedH+5;
    }
}

/* ===================== HIT TEST ===================== */

Node* hit(Node* n,int mx,int my){
    if(mx>=n->x && mx<=n->x+n->computedW &&
       my>=n->y && my<=n->y+n->computedH){
        for(int i=0;i<n->childCount;i++){
            Node* h=hit(n->children[i],mx,my);
            if(h) return h;
        }
        return n;
    }
    return NULL;
}

/* ===================== RENDER ===================== */

void draw(HDC h,Node* n){
    COLORREF bg=color(css(n,"background"));
    if(n->state==STATE_HOVER) bg=RGB(220,220,220);
    if(n->state==STATE_ACTIVE) bg=RGB(150,150,150);
    if(n->state==STATE_FOCUSED) bg=RGB(180,200,255);

    RECT r={n->x,n->y,n->x+n->computedW,n->y+n->computedH};
    HBRUSH b=CreateSolidBrush(bg);
    FillRect(h,&r,b);
    DeleteObject(b);

    DrawText(h,n->text,-1,&r,DT_CENTER|DT_VCENTER|DT_SINGLELINE);

    for(int i=0;i<n->childCount;i++) draw(h,n->children[i]);
}

/* ===================== WINDOW ===================== */

LRESULT CALLBACK WndProc(HWND w,UINT m,WPARAM wp,LPARAM lp){
    switch(m){
    case WM_CREATE:{
        const char* html=
            "<div style='display:flex;flex-direction:row;'>"
              "<div tabindex='0' onclick='toggleColor' "
              "style='background:red;width:120;height:40;'>Button</div>"
            "</div>";
        root=parse(&html);
        root->onClickFn=NULL;
        layout(root,20,20,360);
        return 0;
    }
    case WM_MOUSEMOVE:{
        Node* h=hit(root,LOWORD(lp),HIWORD(lp));
        if(h!=hovered){
            if(hovered) hovered->state=STATE_NORMAL;
            hovered=h;
            if(hovered) hovered->state=STATE_HOVER;
            InvalidateRect(w,NULL,1);
        }
        return 0;
    }
    case WM_LBUTTONDOWN:{
        active=hit(root,LOWORD(lp),HIWORD(lp));
        if(active){ active->state=STATE_ACTIVE; SetCapture(w); }
        return 0;
    }
    case WM_LBUTTONUP:{
        ReleaseCapture();
        if(active){
            active->state=STATE_HOVER;
            if(active->onclick[0]){
                active->onClickFn=resolveFn(active->onclick);
                if(active->onClickFn) active->onClickFn(active);
            }
            InvalidateRect(w,NULL,1);
            active=NULL;
        }
        return 0;
    }
    case WM_KEYDOWN:{
        if(wp==VK_TAB){
            focused=root->children[0];
            focused->state=STATE_FOCUSED;
            InvalidateRect(w,NULL,1);
        }
        if((wp==VK_RETURN||wp==VK_SPACE) && focused){
            if(focused->onclick[0]){
                focused->onClickFn=resolveFn(focused->onclick);
                focused->onClickFn(focused);
                InvalidateRect(w,NULL,1);
            }
        }
        return 0;
    }
    case WM_PAINT:{
        PAINTSTRUCT ps;
        HDC h=BeginPaint(w,&ps);
        draw(h,root);
        EndPaint(w,&ps);
        return 0;
    }
    case WM_DESTROY:
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProc(w,m,wp,lp);
}

int WINAPI WinMain(HINSTANCE h,HINSTANCE p,LPSTR c,int n){
    WNDCLASS wc={0};
    wc.lpfnWndProc=WndProc;
    wc.hInstance=h;
    wc.lpszClassName="MiniBrowser";
    RegisterClass(&wc);

    CreateWindow("MiniBrowser","Mini HTML UI Engine",
        WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        200,200,420,240,
        0,0,h,0);

    MSG msg;
    while(GetMessage(&msg,0,0,0)){
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }
    return 0;
}
