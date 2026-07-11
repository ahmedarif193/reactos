/* gears.c — Classic glxgears port to Win32/WGL, ARM64 */
#include <windows.h>
#include <GL/gl.h>
#include <math.h>
#include <stdio.h>

static HDC hDC;
static HGLRC hRC;
static HWND hWnd;
static GLfloat view_rotx=20,view_roty=30,view_rotz;
static GLint gear1,gear2,gear3;
static GLfloat angle;

/* mirror a line to the launching terminal (stdout) and the serial debugger */
static void log_line(const char *s){ fputs(s,stdout); fflush(stdout); OutputDebugStringA(s); }

static void gear(GLfloat ir,GLfloat or,GLfloat w,GLint t,GLfloat d){
    GLint i;GLfloat r0=ir,r1=or-d/2,r2=or+d/2,a,da,u,v,len;
    da=2.0*3.14159265/t/4.0;
    glShadeModel(GL_FLAT);glNormal3f(0,0,1);
    glBegin(GL_QUAD_STRIP);
    for(i=0;i<=t;i++){a=i*2.0*3.14159265/t;
        glVertex3f(r0*cos(a),r0*sin(a),w*.5f);
        glVertex3f(r1*cos(a),r1*sin(a),w*.5f);
        if(i<t){glVertex3f(r0*cos(a),r0*sin(a),w*.5f);
        glVertex3f(r1*cos(a+3*da),r1*sin(a+3*da),w*.5f);}}
    glEnd();da=2.0*3.14159265/t/4.0;
    glBegin(GL_QUADS);
    for(i=0;i<t;i++){a=i*2.0*3.14159265/t;
        glVertex3f(r1*cos(a),r1*sin(a),w*.5f);
        glVertex3f(r2*cos(a+da),r2*sin(a+da),w*.5f);
        glVertex3f(r2*cos(a+2*da),r2*sin(a+2*da),w*.5f);
        glVertex3f(r1*cos(a+3*da),r1*sin(a+3*da),w*.5f);}
    glEnd();glNormal3f(0,0,-1);
    glBegin(GL_QUAD_STRIP);
    for(i=0;i<=t;i++){a=i*2.0*3.14159265/t;
        glVertex3f(r1*cos(a),r1*sin(a),-w*.5f);
        glVertex3f(r0*cos(a),r0*sin(a),-w*.5f);
        if(i<t){glVertex3f(r1*cos(a+3*da),r1*sin(a+3*da),-w*.5f);
        glVertex3f(r0*cos(a),r0*sin(a),-w*.5f);}}
    glEnd();da=2.0*3.14159265/t/4.0;
    glBegin(GL_QUADS);
    for(i=0;i<t;i++){a=i*2.0*3.14159265/t;
        glVertex3f(r1*cos(a+3*da),r1*sin(a+3*da),-w*.5f);
        glVertex3f(r2*cos(a+2*da),r2*sin(a+2*da),-w*.5f);
        glVertex3f(r2*cos(a+da),r2*sin(a+da),-w*.5f);
        glVertex3f(r1*cos(a),r1*sin(a),-w*.5f);}
    glEnd();glBegin(GL_QUAD_STRIP);
    for(i=0;i<t;i++){a=i*2.0*3.14159265/t;
        glVertex3f(r1*cos(a),r1*sin(a),w*.5f);
        glVertex3f(r1*cos(a),r1*sin(a),-w*.5f);
        u=r2*cos(a+da)-r1*cos(a);v=r2*sin(a+da)-r1*sin(a);len=sqrt(u*u+v*v);
        u/=len;v/=len;glNormal3f(v,-u,0);
        glVertex3f(r2*cos(a+da),r2*sin(a+da),w*.5f);
        glVertex3f(r2*cos(a+da),r2*sin(a+da),-w*.5f);
        glNormal3f(cos(a),sin(a),0);
        glVertex3f(r2*cos(a+2*da),r2*sin(a+2*da),w*.5f);
        glVertex3f(r2*cos(a+2*da),r2*sin(a+2*da),-w*.5f);
        u=r1*cos(a+3*da)-r2*cos(a+2*da);v=r1*sin(a+3*da)-r2*sin(a+2*da);
        glNormal3f(v,-u,0);
        glVertex3f(r1*cos(a+3*da),r1*sin(a+3*da),w*.5f);
        glVertex3f(r1*cos(a+3*da),r1*sin(a+3*da),-w*.5f);
        glNormal3f(cos(a),sin(a),0);}
    glVertex3f(r1*cos(0),r1*sin(0),w*.5f);
    glVertex3f(r1*cos(0),r1*sin(0),-w*.5f);
    glEnd();glShadeModel(GL_SMOOTH);
    glBegin(GL_QUAD_STRIP);
    for(i=0;i<=t;i++){a=i*2.0*3.14159265/t;
        glNormal3f(-cos(a),-sin(a),0);
        glVertex3f(r0*cos(a),r0*sin(a),-w*.5f);
        glVertex3f(r0*cos(a),r0*sin(a),w*.5f);}
    glEnd();
}

static void draw(void){
    glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
    glPushMatrix();
    glRotatef(view_rotx,1,0,0);glRotatef(view_roty,0,1,0);glRotatef(view_rotz,0,0,1);
    glPushMatrix();
    glTranslatef(-3,-2,0);glRotatef(angle,0,0,1);glCallList(gear1);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(3.1f,-2,0);glRotatef(-2*angle-9,0,0,1);glCallList(gear2);
    glPopMatrix();
    glPushMatrix();
    glTranslatef(-3.1f,4.2f,0);glRotatef(-2*angle-25,0,0,1);glCallList(gear3);
    glPopMatrix();
    glPopMatrix();
}

static void reshape(int w,int h){
    GLfloat a=(GLfloat)h/(GLfloat)w;
    glViewport(0,0,w,h);glMatrixMode(GL_PROJECTION);glLoadIdentity();
    glFrustum(-1,1,-a,a,5,60);glMatrixMode(GL_MODELVIEW);glLoadIdentity();
    glTranslatef(0,0,-40);
}

LRESULT CALLBACK wndproc(HWND h,UINT m,WPARAM wp,LPARAM lp){
    if(m==WM_CLOSE||(m==WM_KEYDOWN&&wp==VK_ESCAPE)){PostQuitMessage(0);return 0;}
    if(m==WM_SIZE){reshape(LOWORD(lp),HIWORD(lp));return 0;}
    return DefWindowProcA(h,m,wp,lp);
}

int main(int argc,char**argv){
    HINSTANCE hi=GetModuleHandleA(NULL);
    WNDCLASSA wc={0};MSG msg;PIXELFORMATDESCRIPTOR pfd={
        sizeof(pfd),1,PFD_DRAW_TO_WINDOW|PFD_SUPPORT_OPENGL|PFD_DOUBLEBUFFER,
        PFD_TYPE_RGBA,24,0,0,0,0,0,0,0,0,0,0,0,0,0,16,0,0,PFD_MAIN_PLANE,0,0,0,0};
    GLfloat pos[4]={5,5,10,0},red[4]={0.8f,0.1f,0,1},green[4]={0,0.8f,0.2f,1},blue[4]={0.2f,0.2f,1,1};
    int pf,frames=0;DWORD t0,start,now,elapsed;char buf[160];
    LARGE_INTEGER qf,qa,qb,qc,qd;LONGLONG peek_t=0,draw_t=0,swap_t=0;
    QueryPerformanceFrequency(&qf);

    log_line("[GEARS] starting\n");
    wc.lpfnWndProc=wndproc;wc.hInstance=hi;wc.lpszClassName="Gears";
    RegisterClassA(&wc);
    hWnd=CreateWindowExA(0,"Gears","glxgears",WS_OVERLAPPEDWINDOW|WS_VISIBLE,
        100,100,300,300,NULL,NULL,hi,NULL);
    if(!hWnd){log_line("[GEARS] FAIL window\n");return 1;}
    hDC=GetDC(hWnd);pf=ChoosePixelFormat(hDC,&pfd);SetPixelFormat(hDC,pf,&pfd);
    hRC=wglCreateContext(hDC);wglMakeCurrent(hDC,hRC);
    {typedef BOOL(WINAPI*SWAPINTERVALPROC)(int);
     SWAPINTERVALPROC si=(SWAPINTERVALPROC)wglGetProcAddress("wglSwapIntervalEXT");
     if(si){si(0);log_line("[GEARS] swap interval 0 (vsync off)\n");}}
    reshape(300,300);

    {const char*s; s=(const char*)glGetString(GL_VENDOR);
    wsprintfA(buf,"[GEARS] %s: %s | %s\n",s?s:"(null)",
        (const char*)glGetString(GL_RENDERER),(const char*)glGetString(GL_VERSION));
    log_line(buf);}

    glLightfv(GL_LIGHT0,GL_POSITION,pos);
    glEnable(GL_CULL_FACE);glEnable(GL_LIGHTING);glEnable(GL_LIGHT0);
    glEnable(GL_DEPTH_TEST);glEnable(GL_NORMALIZE);
    gear1=glGenLists(1);glNewList(gear1,GL_COMPILE);
    glMaterialfv(GL_FRONT,GL_AMBIENT_AND_DIFFUSE,red);gear(1,4,1,20,0.7f);glEndList();
    gear2=glGenLists(1);glNewList(gear2,GL_COMPILE);
    glMaterialfv(GL_FRONT,GL_AMBIENT_AND_DIFFUSE,green);gear(0.5f,2,2,10,0.7f);glEndList();
    gear3=glGenLists(1);glNewList(gear3,GL_COMPILE);
    glMaterialfv(GL_FRONT,GL_AMBIENT_AND_DIFFUSE,blue);gear(1.3f,2,0.5f,10,0.7f);glEndList();

    log_line("[GEARS] running 20s, FPS per second\n");
    t0=start=GetTickCount();
    while(1){
        QueryPerformanceCounter(&qa);
        while(PeekMessageA(&msg,NULL,0,0,PM_REMOVE)){
            if(msg.message==WM_QUIT)goto done;
            TranslateMessage(&msg);DispatchMessageA(&msg);
        }
        QueryPerformanceCounter(&qb);
        angle+=2.0f;draw();
        QueryPerformanceCounter(&qc);
        SwapBuffers(hDC);
        QueryPerformanceCounter(&qd);
        peek_t+=qb.QuadPart-qa.QuadPart;draw_t+=qc.QuadPart-qb.QuadPart;swap_t+=qd.QuadPart-qc.QuadPart;
        frames++;
        now=GetTickCount();
        if((now-start)>=20000)break;
        elapsed=now-t0;
        if(elapsed>=1000){
            wsprintfA(buf,"[GEARS] %d FPS  peek=%dus draw=%dus swap=%dus\n",
                (int)(frames*1000.0f/elapsed),
                (int)(peek_t*1000000/qf.QuadPart/frames),
                (int)(draw_t*1000000/qf.QuadPart/frames),
                (int)(swap_t*1000000/qf.QuadPart/frames));
            log_line(buf);
            t0=now;frames=0;peek_t=draw_t=swap_t=0;
        }
    }

done:
    log_line("[GEARS] exiting\n");
    wglMakeCurrent(NULL,NULL);wglDeleteContext(hRC);
    ReleaseDC(hWnd,hDC);DestroyWindow(hWnd);
    return 0;
}
