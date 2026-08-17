/*
 * Copyright (C) 1999-2001  Brian Paul   All Rights Reserved.
 *
 * SPDX-License-Identifier: MIT
 */

/*
 * This is a port of the infamous "gears" demo to straight GLX (i.e. no GLUT)
 * Port by Brian Paul  23 March 2001
 *
 * Command line options:
 *    -info      print GL implementation information
 *
 * Modified from X11/GLX to Win32/WGL by Ben Skeggs
 * 25th October 2004
 */

#include <assert.h>
#include <windows.h>
#include "glad/gl.h"
#include "glad/wgl.h"
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <math.h>


/* Global vars */
static HDC hDC;
static HGLRC hRC;
static HWND hWnd;
static HINSTANCE hInst;
static RECT winrect;

static GLfloat view_rotx = 20.0, view_roty = 30.0, view_rotz = 0.0;
static GLint gear1, gear2, gear3;
static GLfloat angle = 0.0;

static GLboolean fullscreen = GL_FALSE;
static GLint samples = 0;
static GLboolean use_srgb = GL_FALSE;
static GLboolean animate = GL_TRUE;


static void
usage(void)
{
   printf("Usage:\n");
   printf("  -srgb              run in sRGB mode\n");
   printf("  -samples N         run in multisample mode with at least N samples\n");
   printf("  -fullscreen        run in fullscreen mode\n");
   printf("  -info              display OpenGL renderer info\n");
   printf("  -geometry WxH+X+Y  window geometry\n");
}


/* return current time (in seconds) */
static double
current_time(void)
{
   return timeGetTime() / 1000.0;
}


/*
 *
 *  Draw a gear wheel.  You'll probably want to call this function when
 *  building a display list since we do a lot of trig here.
 *
 *  Input:  inner_radius - radius of hole at center
 *          outer_radius - radius at center of teeth
 *          width - width of gear
 *          teeth - number of teeth
 *          tooth_depth - depth of tooth
 */
static void
gear(GLfloat inner_radius, GLfloat outer_radius, GLfloat width,
     GLint teeth, GLfloat tooth_depth)
{
   GLint i;
   GLfloat r0, r1, r2;
   GLfloat angle, da;
   GLfloat u, v, len;

   r0 = inner_radius;
   r1 = outer_radius - tooth_depth / 2.0;
   r2 = outer_radius + tooth_depth / 2.0;

   da = 2.0 * M_PI / teeth / 4.0;

   glShadeModel(GL_FLAT);

   glNormal3f(0.0, 0.0, 1.0);

   /* draw front face */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      if (i < teeth) {
	 glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
	 glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		    width * 0.5);
      }
   }
   glEnd();

   /* draw front sides of teeth */
   glBegin(GL_QUADS);
   da = 2.0 * M_PI / teeth / 4.0;
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 width * 0.5);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 width * 0.5);
   }
   glEnd();

   glNormal3f(0.0, 0.0, -1.0);

   /* draw back face */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      if (i < teeth) {
	 glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		    -width * 0.5);
	 glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      }
   }
   glEnd();

   /* draw back sides of teeth */
   glBegin(GL_QUADS);
   da = 2.0 * M_PI / teeth / 4.0;
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 -width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 -width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
   }
   glEnd();

   /* draw outward faces of teeth */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i < teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;

      glVertex3f(r1 * cos(angle), r1 * sin(angle), width * 0.5);
      glVertex3f(r1 * cos(angle), r1 * sin(angle), -width * 0.5);
      u = r2 * cos(angle + da) - r1 * cos(angle);
      v = r2 * sin(angle + da) - r1 * sin(angle);
      len = sqrt(u * u + v * v);
      u /= len;
      v /= len;
      glNormal3f(v, -u, 0.0);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), width * 0.5);
      glVertex3f(r2 * cos(angle + da), r2 * sin(angle + da), -width * 0.5);
      glNormal3f(cos(angle), sin(angle), 0.0);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 width * 0.5);
      glVertex3f(r2 * cos(angle + 2 * da), r2 * sin(angle + 2 * da),
		 -width * 0.5);
      u = r1 * cos(angle + 3 * da) - r2 * cos(angle + 2 * da);
      v = r1 * sin(angle + 3 * da) - r2 * sin(angle + 2 * da);
      glNormal3f(v, -u, 0.0);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 width * 0.5);
      glVertex3f(r1 * cos(angle + 3 * da), r1 * sin(angle + 3 * da),
		 -width * 0.5);
      glNormal3f(cos(angle), sin(angle), 0.0);
   }

   glVertex3f(r1 * cos(0), r1 * sin(0), width * 0.5);
   glVertex3f(r1 * cos(0), r1 * sin(0), -width * 0.5);

   glEnd();

   glShadeModel(GL_SMOOTH);

   /* draw inside radius cylinder */
   glBegin(GL_QUAD_STRIP);
   for (i = 0; i <= teeth; i++) {
      angle = i * 2.0 * M_PI / teeth;
      glNormal3f(-cos(angle), -sin(angle), 0.0);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), -width * 0.5);
      glVertex3f(r0 * cos(angle), r0 * sin(angle), width * 0.5);
   }
   glEnd();
}


static void
draw(void)
{
   glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

   glPushMatrix();
   glRotatef(view_rotx, 1.0, 0.0, 0.0);
   glRotatef(view_roty, 0.0, 1.0, 0.0);
   glRotatef(view_rotz, 0.0, 0.0, 1.0);

   glPushMatrix();
   glTranslatef(-3.0, -2.0, 0.0);
   glRotatef(angle, 0.0, 0.0, 1.0);
   glCallList(gear1);
   glPopMatrix();

   glPushMatrix();
   glTranslatef(3.1, -2.0, 0.0);
   glRotatef(-2.0 * angle - 9.0, 0.0, 0.0, 1.0);
   glCallList(gear2);
   glPopMatrix();

   glPushMatrix();
   glTranslatef(-3.1, 4.2, 0.0);
   glRotatef(-2.0 * angle - 25.0, 0.0, 0.0, 1.0);
   glCallList(gear3);
   glPopMatrix();

   glPopMatrix();
}


/* new window size or exposure */
static void
reshape(int width, int height)
{
   GLfloat h = (GLfloat) height / (GLfloat) width;

   glViewport(0, 0, (GLint) width, (GLint) height);
   glMatrixMode(GL_PROJECTION);
   glLoadIdentity();
   glFrustum(-1.0, 1.0, -h, h, 5.0, 60.0);
   glMatrixMode(GL_MODELVIEW);
   glLoadIdentity();
   glTranslatef(0.0, 0.0, -40.0);
}


static GLfloat
srgb_to_linear(GLfloat c)
{
   if (c <= 0.04045f)
      return c / 12.92f;
   return powf((c + 0.055f) / 1.055f, 2.4f);
}

static void
init(void)
{
   static GLfloat pos[4] = { 5.0, 5.0, 10.0, 0.0 };
   static GLfloat red[4] = { 0.8, 0.1, 0.0, 1.0 };
   static GLfloat green[4] = { 0.0, 0.8, 0.2, 1.0 };
   static GLfloat blue[4] = { 0.2, 0.2, 1.0, 1.0 };
   int i;

   glLightfv(GL_LIGHT0, GL_POSITION, pos);
   glEnable(GL_CULL_FACE);
   glEnable(GL_LIGHTING);
   glEnable(GL_LIGHT0);
   glEnable(GL_DEPTH_TEST);
   if (use_srgb) {
      for (i = 0; i < 3; ++i) {
         red[i] = srgb_to_linear(red[i]);
         green[i] = srgb_to_linear(green[i]);
         blue[i] = srgb_to_linear(blue[i]);
      }
      glEnable(GL_FRAMEBUFFER_SRGB);
   }

   /* make the gears */
   gear1 = glGenLists(1);
   glNewList(gear1, GL_COMPILE);
   glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, red);
   gear(1.0, 4.0, 1.0, 20, 0.7);
   glEndList();

   gear2 = glGenLists(1);
   glNewList(gear2, GL_COMPILE);
   glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, green);
   gear(0.5, 2.0, 2.0, 10, 0.7);
   glEndList();

   gear3 = glGenLists(1);
   glNewList(gear3, GL_COMPILE);
   glMaterialfv(GL_FRONT, GL_AMBIENT_AND_DIFFUSE, blue);
   gear(1.3, 2.0, 0.5, 10, 0.7);
   glEndList();

   glEnable(GL_NORMALIZE);
}


static LRESULT CALLBACK
WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
   switch (uMsg) {
   case WM_CLOSE:
      PostQuitMessage(0);
      return 0;
   case WM_SIZE:
      /* This can be reached before wglMakeCurrent */
      if (wglGetCurrentContext() != NULL) {
         reshape(LOWORD(lParam), HIWORD(lParam));
      }
      return 0;
   case WM_KEYDOWN:
      if (wParam == VK_LEFT)
         view_roty += 5.0;
      else if (wParam == VK_RIGHT)
         view_roty -= 5.0;
      else if (wParam == VK_UP)
         view_rotx += 5.0;
      else if (wParam == VK_DOWN)
         view_rotx -= 5.0;
      else if (wParam == VK_ESCAPE)
         PostQuitMessage(0);
      else if (wParam == 'A')
         animate = !animate;
      return 0;
#if WINVER >= 0x0605
   case WM_NCCREATE:
       EnableNonClientDpiScaling(hWnd);
       break;
#endif
   }

   return DefWindowProc(hWnd, uMsg, wParam, lParam);
}


/*
 * Create an RGB, double-buffered window.
 * Return the window and context handles.
 */
static void
make_window(const char *name, int x, int y, int width, int height)
{
   int pixelFormat;
   WNDCLASS wc;
   DWORD dwExStyle, dwStyle;
   static const PIXELFORMATDESCRIPTOR pfd = {
      sizeof(PIXELFORMATDESCRIPTOR),
      1,
      PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
      PFD_TYPE_RGBA,
      24,
      0, 0, 0, 0, 0, 0,
      0,
      0,
      0,
      0, 0, 0, 0,
      16,
      0,
      0,
      PFD_MAIN_PLANE,
      0,
      0, 0, 0
   };

   winrect.left = (long)0;
   winrect.right = (long)width;
   winrect.top = (long) 0;
   winrect.bottom = (long)height;

   hInst = GetModuleHandle(NULL);
   wc.style = CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
   wc.lpfnWndProc = (WNDPROC)WndProc;
   wc.cbClsExtra = 0;
   wc.cbWndExtra = 0;
   wc.hInstance = hInst;
   wc.hIcon = LoadIcon(NULL, IDI_WINLOGO);
   wc.hCursor = LoadCursor(NULL, IDC_ARROW);
   wc.hbrBackground = NULL;
   wc.lpszMenuName = NULL;
   wc.lpszClassName = name;
   RegisterClass(&wc);

   dwExStyle = WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;
   if (!fullscreen) {
      dwStyle = WS_OVERLAPPEDWINDOW;
      AdjustWindowRectEx(&winrect, dwStyle, FALSE, dwExStyle);
   }
   else {
      dwStyle = WS_POPUP;
   }

   hWnd = CreateWindowEx(dwExStyle, name, name,
                         WS_CLIPSIBLINGS | WS_CLIPCHILDREN | dwStyle,
                         x, y,
                         winrect.right - winrect.left,
                         winrect.bottom - winrect.top,
                         NULL, NULL, hInst, NULL);

   hDC = GetDC(hWnd);
   pixelFormat = ChoosePixelFormat(hDC, &pfd);
   if (!pixelFormat)
      goto nopixelformat;

   SetPixelFormat(hDC, pixelFormat, &pfd);
   hRC = wglCreateContext(hDC);
   wglMakeCurrent(hDC, hRC);

   gladLoaderLoadWGL(hDC);

   if (use_srgb || samples > 0) {
      /* We can't query/use extension functions until after we've
       * created and bound a rendering context (done above).
       *
       * We can only set the pixel format of the window once, so we need to
       * create a new device context in order to use the pixel format returned
       * from wglChoosePixelFormatARB, and then create a new window.
       */
      assert(GLAD_WGL_ARB_create_context);

      int int_attribs[64] = {
         WGL_SUPPORT_OPENGL_ARB, TRUE,
         WGL_DRAW_TO_WINDOW_ARB, TRUE,
         WGL_COLOR_BITS_ARB, 24,  // at least 24-bits of RGB
         WGL_DEPTH_BITS_ARB, 24,
         WGL_DOUBLE_BUFFER_ARB, TRUE,
         WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB, TRUE,
      };
      int i = 10;

      if (use_srgb) {
         int_attribs[i++] = WGL_FRAMEBUFFER_SRGB_CAPABLE_ARB;
         int_attribs[i++] = TRUE;
      }
      if (samples > 0) {
         int_attribs[i++] = WGL_SAMPLE_BUFFERS_ARB;
         int_attribs[i++] = 1;
         int_attribs[i++] = WGL_SAMPLES_ARB;
         int_attribs[i++] = samples;
      }

      int_attribs[i++] = 0;

      static const float float_attribs[] = { 0 };
      UINT numFormats;

      pixelFormat = 0;
      if (!wglChoosePixelFormatARB(hDC, int_attribs, float_attribs, 1,
                                   &pixelFormat, &numFormats) ||
          !numFormats)
         goto nopixelformat;

      PIXELFORMATDESCRIPTOR newPfd;
      DescribePixelFormat(hDC, pixelFormat, sizeof(pfd), &newPfd);

      /* now, create new context with new pixel format */
      wglMakeCurrent(hDC, NULL);
      wglDeleteContext(hRC);
      DeleteDC(hDC);

      DestroyWindow(hWnd);
      hWnd = CreateWindowEx(dwExStyle, name, name,
                            WS_CLIPSIBLINGS | WS_CLIPCHILDREN | dwStyle,
                            x, y,
                            winrect.right - winrect.left,
                            winrect.bottom - winrect.top,
                            NULL, NULL, hInst, NULL);

      hDC = GetDC(hWnd);
      SetPixelFormat(hDC, pixelFormat, &pfd);
      hRC = wglCreateContext(hDC);
      wglMakeCurrent(hDC, hRC);
   }

   gladLoaderLoadGL();

   ShowWindow(hWnd, SW_SHOW);
   SetForegroundWindow(hWnd);
   SetFocus(hWnd);
   return;

nopixelformat:
   printf("Error: couldn't get an RGB, Double-buffered");
   if (samples > 0)
      printf(", Multisample");
   if (use_srgb)
      printf(", sRGB");
   printf(" pixelformat\n");
   exit(1);
}

static void
draw_frame()
{
   static int frames = 0;
   static double tRot0 = -1.0, tRate0 = -1.0;
   double dt, t = current_time();

   if (tRot0 < 0.0)
      tRot0 = t;
   dt = t - tRot0;
   tRot0 = t;

   if (animate) {
      /* advance rotation for next frame */
      angle += 70.0 * dt;  /* 70 degrees per second */
      angle = fmodf(angle, 360.0f); /* prevents eventual overflow */
   }

   draw();
   SwapBuffers(hDC);

   frames++;

   if (tRate0 < 0.0)
      tRate0 = t;
   if (t - tRate0 >= 5.0) {
      GLfloat seconds = t - tRate0;
      GLfloat fps = frames / seconds;
      printf("%d frames in %3.1f seconds = %6.3f FPS\n", frames, seconds,
             fps);
      fflush(stdout);
      tRate0 = t;
      frames = 0;
   }
}

/**
 * Attempt to determine whether or not the display is synched to vblank.
 */
static void
query_vsync()
{
   int interval = 0;
   if (GLAD_WGL_EXT_swap_control) {
      interval = wglGetSwapIntervalEXT();
   }

   if (interval > 0) {
      printf("Running synchronized to the vertical refresh.  The framerate should be\n");
      if (interval == 1) {
         printf("approximately the same as the monitor refresh rate.\n");
      }
      else if (interval > 1) {
         printf("approximately 1/%d the monitor refresh rate.\n",
                interval);
      }
   }
}


static void
event_loop(void)
{
   MSG msg;

   TIMECAPS tc;
   timeGetDevCaps(&tc, sizeof(tc));
   timeBeginPeriod(tc.wPeriodMin);

   while(1) {
      if (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE)) {
         if (msg.message == WM_QUIT) break;;
         TranslateMessage(&msg);
         DispatchMessage(&msg);
      }

      draw_frame();
   }

   timeEndPeriod(tc.wPeriodMin);
}


static void
parse_geometry(const char *str, int *x, int *y, unsigned int *w, unsigned int *h)
{
   char *end;
   if (*str == '=')
      str++;

   long tw = LONG_MAX;
   if (isdigit(*str)) {
      tw = strtol(str, &end, 10);
      if (str == end)
         return;
      str = end;
   }

   long th = LONG_MAX;
   if (tolower(*str) == 'x') {
      str++;
      th = strtol(str, &end, 10);
      if (str== end)
         return;
      str = end;
   }

   long tx = LONG_MAX;
   if (*str == '+' || *str == '-') {
      tx = strtol(str, &end, 10);
      if (str == end)
         return;
      str = end;
   }

   long ty = LONG_MAX;
   if (*str == '+' || *str == '-') {
      ty = strtol(str, &end, 10);
      if (str == end)
         return;
      str = end;
   }

   if (tw < LONG_MAX)
      *w = tw;
   if (th < LONG_MAX)
      *h = th;
   if (tx < LONG_MAX)
      *x = tx;
   if (ty < LONG_MAX)
      *y = ty;
}


int
main(int argc, char *argv[])
{
   unsigned int winWidth = 300, winHeight = 300;
   int x = CW_USEDEFAULT, y = 0;
   int i;
   GLboolean printInfo = GL_FALSE;

   for (i = 1; i < argc; i++) {
      if (strcmp(argv[i], "-info") == 0) {
         printInfo = GL_TRUE;
      }
      else if (strcmp(argv[i], "-srgb") == 0) {
         use_srgb = GL_TRUE;
      }
      else if (i < argc - 1 && strcmp(argv[i], "-samples") == 0) {
         samples = strtod(argv[i + 1], NULL);
         ++i;
      }
      else if (strcmp(argv[i], "-fullscreen") == 0) {
         fullscreen = GL_TRUE;
      }
      else if (strcmp(argv[i], "-geometry") == 0) {
         parse_geometry(argv[i+1], &x, &y, &winWidth, &winHeight);
         i++;
      }
      else {
         usage();
         return -1;
      }
   }

#if WINVER >= 0x0605
   SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE);
#endif

   if (fullscreen) {
       x = 0; y = 0;
       winWidth = GetSystemMetrics(SM_CXSCREEN);
       winHeight = GetSystemMetrics(SM_CYSCREEN);
   }

   make_window("wglgears", x, y, winWidth, winHeight);

   GetClientRect(hWnd, &winrect);
   reshape(winrect.right - winrect.left, winrect.bottom - winrect.top);

   query_vsync();

   if (printInfo) {
      printf("GL_RENDERER   = %s\n", (char *) glGetString(GL_RENDERER));
      printf("GL_VERSION    = %s\n", (char *) glGetString(GL_VERSION));
      printf("GL_VENDOR     = %s\n", (char *) glGetString(GL_VENDOR));
      printf("GL_EXTENSIONS = %s\n", (char *) glGetString(GL_EXTENSIONS));
   }

   init();

   event_loop();

   /* cleanup */
   gladLoaderUnloadGL();
   wglMakeCurrent (NULL, NULL);
   wglDeleteContext (hRC);
   ReleaseDC (hWnd, hDC);

   return EXIT_SUCCESS;
}
