/*
 *
 * This code is (c) by Andrei Krotkov.
 * The base fot this code was made through
 * the tutorials at nehe.gamedev.net.
 *
 */

#pragma comment(lib, "winmm.lib")

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <mmsystem.h>
#include <time.h>

#include <gl\gl.h>
#include <gl\glu.h>
// glaux.h / glaux.lib were removed from the Windows SDK and Visual Studio
// long ago. The only thing this project used from it was the BMP loader
// (AUX_RGBImageRec / auxDIBImageLoad), which is now provided below by
// LoadDIBBitmap()/LoadDIBBitmapFromMemory(), so glaux is no longer needed
// at all.

// The digit atlas as a raw BMP byte array (NUMBERS_BMP_DATA), so the .exe
// no longer needs Graphics\numbers.bmp shipped alongside it at runtime.
#include "numbers_bmp_data.h"

HDC			hDC=NULL;		
HGLRC		hRC=NULL;	
HWND		hWnd=NULL;		
HINSTANCE	hInstance;		

bool	active=true;
bool	fullscreen=false;

const float piover180 = 0.0174532925f;
const float onesix = .0625f;

int screennum=0;

// Current client size (kept in sync by ReSizeGLScene) and the black
// letterbox strip reserved at the top and bottom of the window.
int windowWidth=1000;
int windowHeight=140;
const int CLOCK_BORDER_PX = 24;

// Current orthographic projection half-extents (world units), recomputed by
// ReSizeGLScene() on every resize. DrawEdgeShading() reads orthoHalfHeight
// to find exactly where the top/bottom of the current viewport sits, so its
// shading tracks the real margin between the digit faces and the viewport
// edge instead of a size baked in for one particular window shape.
float orthoHalfWidth = 5.6f;
float orthoHalfHeight = 0.515f;

DWORD ticks=0;
long int frames=0;
int fps=0;

GLuint texture[15];
GLfloat color[]={0,0,0,0};

int rot[9]={-18,-18,-18,-18,-18,-18,-18,-18,-18};
int dat[9]={0,0,0,0,0,0,0,0,0};

int timev=0;

LRESULT	CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);	

void drawwheel(int num)
{

	int x=0;
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture[0]);
	glBegin(GL_QUADS);
	for (int dir=0;dir<360;dir+=36)
	{
		if (x==num)
		{
			glColor3f(1,0,0);
		}
		else
		{
			glColor3f(1,1,1);
		}
		glTexCoord2f(.0625*x,0);
		glVertex3f(cos(dir*piover180),sin(dir*piover180),.5);
		glTexCoord2f(.0625*(x+1),0);
		glVertex3f(cos(dir*piover180),sin(dir*piover180),-.5);
		glTexCoord2f(.0625*(x+1),1);
		glVertex3f(cos((dir+36)*piover180),sin((dir+36)*piover180),-.5);
		glTexCoord2f(.0625*x,1);
		glVertex3f(cos((dir+36)*piover180),sin((dir+36)*piover180),.5);
		x++;
	}
	glEnd();
}

void drawcolon()
{
	int x=0;
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture[0]);
	glBegin(GL_QUADS);
	for (int dir=0;dir<360;dir+=36)
	{
		if (x==0)
		{
			glColor3f(1,0,0);
		}
		else
		{
			glColor3f(1,1,1);
		}		
		glTexCoord2f(.0625*11,0);
		glVertex3f(cos(dir*piover180),sin(dir*piover180),.5);
		glTexCoord2f(.0625*(11+1),0);
		glVertex3f(cos(dir*piover180),sin(dir*piover180),-.5);
		glTexCoord2f(.0625*(11+1),1);
		glVertex3f(cos((dir+36)*piover180),sin((dir+36)*piover180),-.5);
		glTexCoord2f(.0625*11,1);
		glVertex3f(cos((dir+36)*piover180),sin((dir+36)*piover180),.5);
		x++;
	}
	glEnd();
}

void drawpmam(bool pm)
{
	int x=0;
	glEnable(GL_TEXTURE_2D);
	glBindTexture(GL_TEXTURE_2D, texture[0]);
	glBegin(GL_QUADS);
	for (int dir=0;dir<360;dir+=36)
	{
		if (x==0)
		{
			glColor3f(1,0,0);
		}
		else
		{
			glColor3f(1,1,1);
		}		
		if (pm)
		{
			glTexCoord2f(.0625*12,0);
			glVertex3f(cos(dir*piover180),sin(dir*piover180),.5);
			glTexCoord2f(.0625*(12+1),0);
			glVertex3f(cos(dir*piover180),sin(dir*piover180),-.5);
			glTexCoord2f(.0625*(12+1),1);
			glVertex3f(cos((dir+36)*piover180),sin((dir+36)*piover180),-.5);
			glTexCoord2f(.0625*12,1);
			glVertex3f(cos((dir+36)*piover180),sin((dir+36)*piover180),.5);
		}
		else
		{
			glTexCoord2f(.0625*13,0);
			glVertex3f(cos(dir*piover180),sin(dir*piover180),.5);
			glTexCoord2f(.0625*(13+1),0);
			glVertex3f(cos(dir*piover180),sin(dir*piover180),-.5);
			glTexCoord2f(.0625*(13+1),1);
			glVertex3f(cos((dir+36)*piover180),sin((dir+36)*piover180),-.5);
			glTexCoord2f(.0625*13,1);
			glVertex3f(cos((dir+36)*piover180),sin((dir+36)*piover180),.5);
		}
		x++;
	}
	glEnd();
}

// Darkens the margin area above and below the digit faces -- between the
// numbers themselves and the top/bottom of the visible clock band -- with a
// soft gradient, fading to fully transparent right at the digits. Purely a
// flat, textureless strip otherwise reads as flat -- this gives the band a
// subtle rounded/embossed look without ever touching the digits' own
// texturing.
//
// Drawn directly in the clock's own world-space coordinates -- the same
// modelview/projection the wheels themselves use -- rather than swapping
// in a temporary screen-space orthographic overlay. Each wheel's readable
// front facet spans a fixed Y range of +/-sin(18 deg) regardless of window
// size (see drawwheel()/drawcolon()/drawpmam()), so the inner edge of the
// gradient is anchored to that constant. The outer edge is anchored to
// orthoHalfHeight, which ReSizeGLScene() keeps in sync with wherever the
// current viewport's top/bottom edge actually sits -- so the shaded margin
// always exactly fills the gap between the numbers and the edge of the
// clock band no matter the window's size or aspect ratio, and never
// overlaps the digits. Must be called with the same modelview transform
// active as when the wheels were drawn (i.e. right after gluLookAt(), with
// no leftover translation/rotation from the wheel-drawing loop -- see the
// glPushMatrix/glPopMatrix wrapped around that loop in DrawGLScene()).
void DrawEdgeShading(void)
{
	glDisable(GL_DEPTH_TEST);
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

	const float faceEdgeY  = 0.309f;		// sin(18 Degrees) -- The Wheel Faces' Fixed Y Extent
	const float shadeAlpha = 0.8f;			// Darkest Point's Opacity, Right At The Viewport Edge
	const float x          = 1.0f;			// Same Depth As The Wheels' Front-Facing Surface
	const float zNear      = -2.0f;		// Comfortably Past The First Wheel
	const float zFar       = 12.0f;		// Comfortably Past The Last Wheel

	const float outerY = orthoHalfHeight;	// Wherever The Current Viewport's Top/Bottom Edge Sits

	glBegin(GL_QUADS);
		// Top Margin -- Dark At The Viewport Edge, Fading To Nothing Right At The Digit Face
		glColor4f(0,0,0,shadeAlpha);
		glVertex3f(x, outerY, zNear);
		glVertex3f(x, outerY, zFar);
		glColor4f(0,0,0,0);
		glVertex3f(x, faceEdgeY, zFar);
		glVertex3f(x, faceEdgeY, zNear);

		// Bottom Margin -- Mirror Image Of The Above
		glColor4f(0,0,0,0);
		glVertex3f(x, -faceEdgeY, zNear);
		glVertex3f(x, -faceEdgeY, zFar);
		glColor4f(0,0,0,shadeAlpha);
		glVertex3f(x, -outerY, zFar);
		glVertex3f(x, -outerY, zNear);
	glEnd();

	glDisable(GL_BLEND);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
}

int DrawGLScene(void)
{
	// int hour10=0,hour1=0,min10=0,min1=0,sec10=0,sec1=0;
	int hours=0, minutes=0, seconds=0;
	bool pm=true;

	// Ask the OS for local time directly (time zone + DST already applied)
	// instead of the old hard-coded "-4" offset, which assumed US Eastern
	// Daylight Time and would be wrong for any other zone (and for Eastern
	// itself once DST ends).
	time_t rawtime = time(NULL);
	struct tm *local = localtime(&rawtime);

	pm = local->tm_hour >= 12;
	hours = local->tm_hour % 12;
	if (hours == 0) hours = 12;      // 12-hour display: both 12am and 12pm show as 12, never 00
	minutes = local->tm_min;
	seconds = local->tm_sec;

	rot[1]=360-((seconds%10)*36+18);
	dat[1]=seconds%10;
	rot[2]=360-((seconds/10)*36+18+((seconds%10)/10.0)*36.0f);
	dat[2]=seconds/10;
	// rot[3] is a colon
	rot[4]=360-((minutes%10)*36+18+((seconds/10)/6.0)*36.0f);
	dat[4]=minutes%10;
	rot[5]=360-((minutes/10)*36+18+((minutes%10)/10.0)*36.0f);
	dat[5]=minutes/10;
	// rot[6] is a colon
	rot[7]=360-((hours%10)*36+18+((minutes/10)/6.0)*36.0f);
	dat[7]=hours%10;
	rot[8]=360-((hours/10)*36+18+((hours%10)/10.0)*36.0f);
	dat[8]=hours/10;

	if (screennum==0)
	{
		// Clear the whole window first (full viewport), then restrict the
		// actual viewport to a band inset by CLOCK_BORDER_PX top and
		// bottom before drawing the scene. Nothing draws into that inset
		// margin, so it's left showing the black clear color -- a simple
		// letterboxed border without needing any extra geometry.
		glViewport(0,0,windowWidth,windowHeight);
		glClear(GL_COLOR_BUFFER_BIT|GL_DEPTH_BUFFER_BIT);
		glViewport(0,CLOCK_BORDER_PX,windowWidth,windowHeight-2*CLOCK_BORDER_PX);

		glLoadIdentity();
		// Camera position/distance no longer controls framing now that
		// ReSizeGLScene uses an orthographic projection (see there) --
		// this just needs to sit somewhere that keeps the clock between
		// the near/far clip planes while looking straight down the
		// wheels' shared axis.
		gluLookAt(10,0,5,
				  0,0,5,
				  0,1,0);
		glPushMatrix();
		for (int x=0;x<9;x++)
		{
			glTranslatef(0,0,1);
			glRotatef(rot[x],0,0,1);
			if ((x!=3)&(x!=6)&&(x!=0))
			{
				drawwheel(dat[x]);
			}
			else
			{
				if (x!=0)
					drawcolon();
				else
					drawpmam(pm);
			}
			glRotatef(rot[x],0,0,-1);
		}
		glPopMatrix();		// Undo The Loop's Cumulative Z-Translation Before Drawing The Shading Below

		DrawEdgeShading();
	}
	return true;
}

void ReSizeGLScene(GLsizei width, GLsizei height)
{
	windowWidth = width;
	windowHeight = height;

	glViewport(0,0,width,height);

	glMatrixMode(GL_PROJECTION);
	glLoadIdentity();

	// The clock face is really a flat, wide strip of digits (roughly
	// world Z 0..10 by world Y +/-0.31), so an orthographic projection
	// framed tightly around that box fills the window far better than a
	// perspective camera can -- with gluPerspective, closing the gap on
	// one axis (by moving the camera closer) only widens the field of
	// view on the other, and pushing that to the extreme this content's
	// shape would need causes heavy fisheye stretching toward the edges.
	// Orthographic has no such tradeoff: no matter how the window is
	// shaped or resized, this always frames the content edge-to-edge
	// (plus a small margin) on whichever axis is tighter, with zero
	// perspective distortion.
	//
	// The aspect ratio is computed from the *inset* viewport (see
	// DrawGLScene), not the full window, since that's what the clock
	// actually renders into once the top/bottom border is carved out.
	const float contentHalfWidth  = 5.0f;    // digit wheels span world Z 0..10
	const float contentHalfHeight = 0.31f;   // visible digit-face height in world Y
	const float margin = 1.12f;              // 12% breathing room so digits don't touch the edge

	int insetHeight = height - 2*CLOCK_BORDER_PX;
	if (insetHeight < 1)
		insetHeight = 1;

	float aspect = (GLfloat)width/(GLfloat)insetHeight;
	float halfHeight = contentHalfHeight;
	if (contentHalfWidth/aspect > halfHeight)
		halfHeight = contentHalfWidth/aspect;
	halfHeight *= margin;
	float halfWidth = halfHeight*aspect;

	orthoHalfWidth = halfWidth;
	orthoHalfHeight = halfHeight;

	glOrtho(-halfWidth, halfWidth, -halfHeight, halfHeight, 0.1, 100.0);

	glMatrixMode(GL_MODELVIEW);
	glLoadIdentity();
}

// Drop-in replacement for glaux's AUX_RGBImageRec. Same shape (sizeX, sizeY,
// data), so every caller below needed only a type/name swap.
struct RGBImage
{
	long sizeX;
	long sizeY;
	unsigned char *data;
};

// Drop-in replacement for glaux's auxDIBImageLoad(). Reads an uncompressed
// 24-bit BMP directly via the Windows BITMAPFILEHEADER/BITMAPINFOHEADER
// structs (already available from windows.h/wingdi.h, so no extra headers
// or libraries are required) and hands back top-to-bottom RGB data the way
// glaux used to.
RGBImage *LoadDIBBitmap(const char *Filename)
{
	if (!Filename)
		return NULL;

	FILE *File = fopen(Filename, "rb");
	if (!File)
		return NULL;

	BITMAPFILEHEADER bfh;
	BITMAPINFOHEADER bih;

	if (fread(&bfh, sizeof(bfh), 1, File) != 1 || bfh.bfType != 0x4D42 /* 'BM' */ ||
		fread(&bih, sizeof(bih), 1, File) != 1 ||
		bih.biBitCount != 24 || bih.biCompression != BI_RGB)
	{
		fclose(File);
		return NULL;
	}

	long width = bih.biWidth;
	long height = labs(bih.biHeight);
	bool topDown = bih.biHeight < 0;                    // negative height = stored top-down
	long rowSize = ((width * 3 + 3) / 4) * 4;            // BMP rows are padded to 4 bytes

	// Bottom-up BMPs (the common case, positive biHeight) store their first
	// scanline at the bottom of the image, which is exactly the row order
	// OpenGL wants (row 0 of the data = bottom of the texture) -- so those
	// need no flip. Only top-down BMPs (negative biHeight) need flipping.

	RGBImage *Image = (RGBImage *)malloc(sizeof(RGBImage));
	unsigned char *data = (unsigned char *)malloc(width * height * 3);
	unsigned char *row = (unsigned char *)malloc(rowSize);

	if (!Image || !data || !row)
	{
		free(Image); free(data); free(row);
		fclose(File);
		return NULL;
	}

	fseek(File, bfh.bfOffBits, SEEK_SET);

	for (long y = 0; y < height; y++)
	{
		if (fread(row, 1, rowSize, File) != (size_t)rowSize)
		{
			free(Image); free(data); free(row);
			fclose(File);
			return NULL;
		}

		long destY = topDown ? (height - 1 - y) : y;
		unsigned char *dest = data + destY * width * 3;

		for (long x = 0; x < width; x++)
		{
			// BMP stores pixels as BGR; OpenGL expects RGB.
			dest[x * 3 + 0] = row[x * 3 + 2];
			dest[x * 3 + 1] = row[x * 3 + 1];
			dest[x * 3 + 2] = row[x * 3 + 0];
		}
	}

	free(row);
	fclose(File);

	Image->sizeX = width;
	Image->sizeY = height;
	Image->data = data;
	return Image;
}

// Same as LoadDIBBitmap() above, but reads from a BMP already sitting in
// memory (e.g. the embedded NUMBERS_BMP_DATA array) instead of a file on
// disk. Shares all the same format assumptions (24-bit, uncompressed).
RGBImage *LoadDIBBitmapFromMemory(const unsigned char *bytes, unsigned int len)
{
	if (!bytes || len < sizeof(BITMAPFILEHEADER)+sizeof(BITMAPINFOHEADER))
		return NULL;

	BITMAPFILEHEADER bfh;
	BITMAPINFOHEADER bih;
	memcpy(&bfh, bytes, sizeof(bfh));
	memcpy(&bih, bytes+sizeof(bfh), sizeof(bih));

	if (bfh.bfType != 0x4D42 /* 'BM' */ || bih.biBitCount != 24 || bih.biCompression != BI_RGB)
		return NULL;

	long width = bih.biWidth;
	long height = labs(bih.biHeight);
	bool topDown = bih.biHeight < 0;
	long rowSize = ((width * 3 + 3) / 4) * 4;

	if (bfh.bfOffBits + (unsigned long)rowSize*height > len)
		return NULL;                                    // truncated/corrupt data

	RGBImage *Image = (RGBImage *)malloc(sizeof(RGBImage));
	unsigned char *data = (unsigned char *)malloc(width * height * 3);
	if (!Image || !data)
	{
		free(Image); free(data);
		return NULL;
	}

	const unsigned char *src = bytes + bfh.bfOffBits;

	for (long y = 0; y < height; y++)
	{
		const unsigned char *row = src + y*rowSize;
		long destY = topDown ? (height - 1 - y) : y;
		unsigned char *dest = data + destY * width * 3;

		for (long x = 0; x < width; x++)
		{
			dest[x * 3 + 0] = row[x * 3 + 2];
			dest[x * 3 + 1] = row[x * 3 + 1];
			dest[x * 3 + 2] = row[x * 3 + 0];
		}
	}

	Image->sizeX = width;
	Image->sizeY = height;
	Image->data = data;
	return Image;
}

void LoadGLTextures()                                    // Load Bitmaps And Convert To Textures
{
	// The digit atlas used to be read from Graphics\numbers.bmp (by way of
	// Graphics\textures.txt listing it) at runtime. It's now compiled
	// straight into the .exe as NUMBERS_BMP_DATA, so there's no file I/O
	// and nothing extra to ship alongside the binary.
	RGBImage *TextureImage = LoadDIBBitmapFromMemory(NUMBERS_BMP_DATA, NUMBERS_BMP_DATA_LEN);

	if (TextureImage)
	{
		glGenTextures(1, &texture[0]);

		glBindTexture(GL_TEXTURE_2D, texture[0]);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);
		gluBuild2DMipmaps(GL_TEXTURE_2D, 3, TextureImage->sizeX, TextureImage->sizeY, GL_RGB, GL_UNSIGNED_BYTE, TextureImage->data);

		free(TextureImage->data);
		free(TextureImage);
	}
}

int InitGL(void)									
{
	LoadGLTextures();
	glShadeModel(GL_SMOOTH);						
	glClearColor(0.0f, 0.0f,0.0f, 0.0f);				
	glClearDepth(1.0f);		
	glEnable(GL_DEPTH_TEST);							
	glDepthFunc(GL_LEQUAL);							
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	return true;
}


void KillGLWindow(void)							
{
	if (fullscreen)									
	{
		ChangeDisplaySettings(NULL,0);			
		ShowCursor(true);					
	}

	if (hRC)										
	{
		if (!wglMakeCurrent(NULL,NULL))				
		{
			MessageBox(NULL,"Release Of DC And RC Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}

		if (!wglDeleteContext(hRC))					
		{
			MessageBox(NULL,"Release Rendering Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		}
		hRC=NULL;									
	}

	if (hDC && !ReleaseDC(hWnd,hDC))					
	{
		MessageBox(NULL,"Release Device Context Failed.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		hDC=NULL;									
	}

	if (hWnd && !DestroyWindow(hWnd))					
	{
		MessageBox(NULL,"Could Not Release hWnd.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		hWnd=NULL;									
	}

	if (!UnregisterClass("OpenGL",hInstance))		
	{
		MessageBox(NULL,"Could Not Unregister Class.","SHUTDOWN ERROR",MB_OK | MB_ICONINFORMATION);
		hInstance=NULL;								
	}
}

BOOL CreateGLWindow(char* title, int width, int height, int bits, bool fullscreenflag)
{
	GLuint		PixelFormat;		
	WNDCLASS	wc;						
	DWORD		dwExStyle;				
	DWORD		dwStyle;			
	RECT		WindowRect;				
	WindowRect.left=(long)0;		
	WindowRect.right=(long)width;	
	WindowRect.top=(long)0;			
	WindowRect.bottom=(long)height;	

	fullscreen=fullscreenflag;			

	hInstance			= GetModuleHandle(NULL);			
	wc.style			= CS_HREDRAW | CS_VREDRAW | CS_OWNDC;
	wc.lpfnWndProc		= (WNDPROC) WndProc;				
	wc.cbClsExtra		= 0;									
	wc.cbWndExtra		= 0;								
	wc.hInstance		= hInstance;						
	wc.hIcon			= LoadIcon(NULL, IDI_WINLOGO);		
	wc.hCursor			= LoadCursor(NULL, IDC_ARROW);		
	wc.hbrBackground	= NULL;								
	wc.lpszMenuName		= NULL;								
	wc.lpszClassName	= "OpenGL";						

	if (!RegisterClass(&wc))								
	{
		MessageBox(NULL,"Failed To Register The Window Class.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;										
	}
	
	if (fullscreen)											
	{
		DEVMODE dmScreenSettings;							
		memset(&dmScreenSettings,0,sizeof(dmScreenSettings));	
		dmScreenSettings.dmSize=sizeof(dmScreenSettings);		
		dmScreenSettings.dmPelsWidth	= width;			
		dmScreenSettings.dmPelsHeight	= height;			
		dmScreenSettings.dmBitsPerPel	= bits;				
		dmScreenSettings.dmFields=DM_BITSPERPEL|DM_PELSWIDTH|DM_PELSHEIGHT;

		if (ChangeDisplaySettings(&dmScreenSettings,CDS_FULLSCREEN)!=DISP_CHANGE_SUCCESSFUL)
		{
			if (MessageBox(NULL,"The Requested Fullscreen Mode Is Not Supported By\nYour Video Card. Use Windowed Mode Instead?","NeHe GL",MB_YESNO|MB_ICONEXCLAMATION)==IDYES)
			{
				fullscreen=false;	
			}
			else
			{
				MessageBox(NULL,"Program Will Now Close.","ERROR",MB_OK|MB_ICONSTOP);
				return false;								
			}
		}
	}

	if (fullscreen)											
	{
		dwExStyle=WS_EX_APPWINDOW;								
		dwStyle=WS_POPUP;									
		ShowCursor(false);									
	}
	else
	{
		dwExStyle=WS_EX_APPWINDOW | WS_EX_WINDOWEDGE;		
		dwStyle=WS_OVERLAPPEDWINDOW;						
	}

	AdjustWindowRectEx(&WindowRect, dwStyle, false, dwExStyle);	


	if (!(hWnd=CreateWindowEx(	dwExStyle,							
								"OpenGL",						
								title,							
								dwStyle |							
								WS_CLIPSIBLINGS |				
								WS_CLIPCHILDREN,				
								0, 0,								
								WindowRect.right-WindowRect.left,	
								WindowRect.bottom-WindowRect.top,
								NULL,							
								NULL,							
								hInstance,						
								NULL)))							
	{
		KillGLWindow();							
		MessageBox(NULL,"Window Creation Error.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;							
	}

	static	PIXELFORMATDESCRIPTOR pfd=			
	{
		sizeof(PIXELFORMATDESCRIPTOR),			
		1,										
		PFD_DRAW_TO_WINDOW |					
		PFD_SUPPORT_OPENGL |					
		PFD_DOUBLEBUFFER,						
		PFD_TYPE_RGBA,								
		bits,									
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
	
	if (!(hDC=GetDC(hWnd)))						
	{
		KillGLWindow();						
		MessageBox(NULL,"Can't Create A GL Device Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;							
	}

	if (!(PixelFormat=ChoosePixelFormat(hDC,&pfd)))	
	{
		KillGLWindow();								
		MessageBox(NULL,"Can't Find A Suitable PixelFormat.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;						
	}

	if(!SetPixelFormat(hDC,PixelFormat,&pfd))
	{
		KillGLWindow();							
		MessageBox(NULL,"Can't Set The PixelFormat.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;							
	}

	if (!(hRC=wglCreateContext(hDC)))			
	{
		KillGLWindow();								
		MessageBox(NULL,"Can't Create A GL Rendering Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;							
	}

	if(!wglMakeCurrent(hDC,hRC))					
	{
		KillGLWindow();								
		MessageBox(NULL,"Can't Activate The GL Rendering Context.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;					
	}

	ShowWindow(hWnd,SW_SHOW);					
	SetForegroundWindow(hWnd);					
	SetFocus(hWnd);								
	ReSizeGLScene(width, height);				

	if (!InitGL())									
	{
		KillGLWindow();							
		MessageBox(NULL,"Initialization Failed.","ERROR",MB_OK|MB_ICONEXCLAMATION);
		return false;							
	}

	return true;								
}

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)			
{
	switch (uMsg)									
	{
		case WM_ACTIVATE:						
		{
			if (!HIWORD(wParam))				
			{
				active=true;					
			}
			else
			{
				active=false;						
			}
		}
		break;
		case WM_SYSCOMMAND:						
		{
			switch (wParam)						
			{
				case SC_SCREENSAVE:				
				case SC_MONITORPOWER:			
				return 0;						
			}
			break;								
		}

		case WM_CLOSE:								
		{
			PostQuitMessage(0);						
			return 0;					
		}

		case WM_SIZE:								
		{
			ReSizeGLScene(LOWORD(lParam),HIWORD(lParam));  
			return 0;								
		}

		case WM_KEYDOWN:
			{
				// WM_KEYDOWN is only ever delivered to whichever window
				// currently has keyboard focus, so handling Escape here
				// (instead of polling GetAsyncKeyState(VK_ESCAPE) in the
				// main loop, which reports the physical key's state
				// system-wide regardless of focus) means Escape only
				// closes the clock when its window is the one actually
				// focused.
				if (wParam==VK_ESCAPE)
				{
					PostQuitMessage(0);
				}
			}
			break;
	}

	return DefWindowProc(hWnd,uMsg,wParam,lParam);
}

int WINAPI WinMain(HINSTANCE hInstance,HINSTANCE hPrevInstance, LPSTR lpCmdLine,int nShowCmd)
{
	MSG		msg;								
	BOOL	done=false;			

	// The clock face is a short, wide strip (roughly 16:1), nothing like
	// 640x480 (4:3) -- that mismatch is what was leaving so much black
	// space above and below it. This shape gets much closer.
	if (!CreateGLWindow("3D RPG DEMO",1000,140,16,fullscreen))
	{
		return 0;								
	}

	while(!done)								
	{
		if (PeekMessage(&msg,NULL,0,0,PM_REMOVE))	
		{
			if (msg.message==WM_QUIT)				
			{
				done=true;						
			}
			else								
			{
				TranslateMessage(&msg);			
				DispatchMessage(&msg);		
			}
		}
		else									
		{
			if (active)
			{
				// Escape is handled in WndProc's WM_KEYDOWN case, which
				// only fires while this window has keyboard focus -- see
				// the comment there.
				DrawGLScene();
				SwapBuffers(hDC);
			}
		}
	}

	KillGLWindow();								
	return (msg.wParam);						
}