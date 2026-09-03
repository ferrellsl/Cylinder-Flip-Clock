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
// long ago, but nothing here needs it any more anyway -- the digits used
// to be drawn as flat textured quads (loaded from glaux, then later from
// embedded BMP byte arrays); they're now real 3D meshes, so there's no
// texture loading of any kind left in this file.

// The digits themselves, as real 3D meshes extruded from a TrueType font
// (DejaVu Sans Bold) and triangulated offline -- see the comment atop
// glyph_meshes.h for the coordinate convention each mesh is stored in.
// Each digit-wheel facet is drawn as a flat colored "field" quad (white
// for whichever facet is currently displaying the live time, black for
// every other facet -- that's what marks the current digit, instead of a
// red tint or a second texture atlas) with the glyph mesh placed on top
// of it, raised slightly off the surface. See DrawFacetField() and
// DrawGlyphOnFacet() below.
#include "glyph_meshes.h"
#include "resource.h"		// IDI_ICON1 -- the clock icon baked in via CylinderClock.rc

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

GLfloat color[]={0,0,0,0};

// The two textures DrawFacetField()/DrawGlyphOnFacet() swap between --
// created by CreateFacetTexture()/CreateEnvMapTexture() (defined further
// down, near InitGL, which is where they're actually built), but declared
// up here since both draw functions run long before InitGL does in the
// file.
GLuint facetTexture = 0;
GLuint envMapTexture = 0;

// Facet coloring: only the field switches between the white of the facet
// currently displaying the live digit and the dark gray of every other
// facet -- the ink itself stays the same light, raised-metal gray either
// way, so the digit is always read the same way, by its own shading and
// specular highlight rather than by a flat color swap. (It used to go
// dark on the white field for old-fashioned "black numeral" contrast, but
// that just crushed the raised glyph's own shading down to invisible --
// see the comment on DrawGlyphOnFacet's specular material for why a flat
// color can't carry that job the way the lit geometry can.)
//
// The inactive field used to be pure (0,0,0) -- under GL_COLOR_MATERIAL
// (see InitGL) that always renders fully black no matter what the light
// is doing, which read as flat and, worse, blended straight into the
// window's own black background, losing the drum's curved shape entirely.
// A dark gray field still reads as "recessed" next to the white active
// field, but is bright enough for the light to actually roll across it --
// the lit facet visibly brighter than the background, the shadowed ones
// visibly darker, which is what makes it read as a curved cylinder rather
// than a flat printed strip.
// The active field and the ink are a warm ivory and a cool steel blue,
// rather than plain white and gray. A tinted overall light wouldn't have
// helped here -- both materials are lit by the very same GL_LIGHT0/1, so
// recoloring the *light* just recolors both of them together, in lockstep,
// leaving the same relative contrast (this is why the digits were still
// hard to read after the previous fix: the field can't go past white,
// and 0.68-gray-on-white is a small gap to begin with, and a small gap
// reads as even smaller once both sides are that bright). Giving the two
// *materials* distinct hues instead means they stay visually separated by
// color even where their brightness levels land close together -- and
// unlike a brightness gap, a warm-vs-cool split survives a bright specular
// hot spot landing on the ink, since that only pulls its color toward
// neutral white locally, not toward the field's own warm cast.
//
// The active field started out fully white with just a light warm cast
// (1.0, 0.95, 0.85) -- turned out both too saturated a yellow and, next
// to the inactive field's 0.34 gray, too big a brightness jump between
// the two. Toned down both at once: a much subtler warm-gray rather than
// cream, and pulled down closer to (while the inactive field is nudged
// up closer to) a middle ground, so "this facet is active" still reads
// clearly without such a stark jump.
const float ACTIVE_FIELD_R=0.80f,   ACTIVE_FIELD_G=0.79f,   ACTIVE_FIELD_B=0.75f;
const float INACTIVE_FIELD_R=0.38f, INACTIVE_FIELD_G=0.38f, INACTIVE_FIELD_B=0.38f;
const float INK_R=0.50f,            INK_G=0.58f,            INK_B=0.72f;

// Which GlyphMesh (see glyph_meshes.h) goes with each digit 0-9.
static const GlyphMesh* const DIGIT_GLYPHS[10] =
{
	&GLYPH_0,&GLYPH_1,&GLYPH_2,&GLYPH_3,&GLYPH_4,
	&GLYPH_5,&GLYPH_6,&GLYPH_7,&GLYPH_8,&GLYPH_9
};

// Largest vertCount among all the glyph meshes (GLYPH_8, at 3084) sets how
// big the reusable per-vertex scratch buffers below need to be, with some
// headroom.
const unsigned int MAX_GLYPH_VERTS = 4096;
static float scratchVerts[MAX_GLYPH_VERTS*3];
static float scratchNormals[MAX_GLYPH_VERTS*3];

// Shared per-facet placement geometry, computed once and used by both
// DrawFacetField() and DrawGlyphOnFacet() for the same facet -- the flat
// parallelogram running from angle "dir" to angle "dir+36" degrees around
// the unit-radius drum, matching exactly where the old texture-mapped
// facet quad used to sit (see the vertex/texcoord layout drawwheel() used
// to have): corner B is the facet's (u=0,v=0) corner, chordX/chordY is
// the (non-unit) vector to its (u=0,v=1) corner, and Nx/Ny is the unit
// outward normal (away from the drum's axis).
struct FacetGeometry
{
	float Bx, By, Bz;
	float chordX, chordY, chordLen;
	float chordDirX, chordDirY;
	float Nx, Ny;
};

FacetGeometry ComputeFacetGeometry(float dir)
{
	FacetGeometry g;
	float a0 = dir*piover180;
	float a1 = (dir+36)*piover180;

	g.Bx = cos(a0); g.By = sin(a0); g.Bz = 0.5f;
	g.chordX = cos(a1)-cos(a0);
	g.chordY = sin(a1)-sin(a0);
	g.chordLen = sqrt(g.chordX*g.chordX + g.chordY*g.chordY);
	g.chordDirX = g.chordX/g.chordLen;
	g.chordDirY = g.chordY/g.chordLen;

	// The outward normal is the chord rotated -90 degrees -- verified
	// against the facet's own expected outward direction (the angle
	// midway between dir and dir+36): at dir=0 this comes out to roughly
	// (0.95,0.31,0), matching cos(18 deg),sin(18 deg).
	g.Nx = g.chordY/g.chordLen;
	g.Ny = -g.chordX/g.chordLen;

	return g;
}

// Draws the flat colored "field" quad a facet sits on -- what used to be
// the whole textured quad in the old scheme, now just a plain colored
// backdrop behind the raised 3D glyph. Nudged very slightly inward (along
// -N) from the glyph's own base plane so the two don't sit exactly
// coplanar, which would z-fight/flicker where the glyph's mesh touches
// the field.
//
// Used to be left out of the specular highlight and chrome env-map
// reflection entirely -- those were the *raised metal glyph's* shine
// only, not the flat backdrop's, because letting either hit the field
// was what made a white active-facet field wash the ink digit sitting on
// it out to solid white (white is already at the top of the range, so
// extra light added on top of it can't go anywhere, while the same
// addition on the usually-darker ink pushed it up to meet it, erasing
// the contrast between the two). That's still true of the *ink's*
// reflection strength (inkSpec/envMapTexture over in DrawGlyphOnFacet),
// left untouched.
//
// The field itself now gets its own much weaker version of the same two
// tricks -- a modest specular material (fieldSpec below) on the base
// pass, plus a second, additive glaze pass afterward reusing the chrome
// env-map -- for a "wet gloss coat" look requested on top of the plain
// metal finish. Two passes rather than one because the base pass already
// spends the single texture unit on the grain texture (GL_MODULATE); the
// reflection needs the env-map bound instead, so it's drawn as a second
// full pass over the same quad, blended additively (GL_ONE,GL_ONE) on
// top of the framebuffer rather than replacing it. glDepthFunc(GL_LEQUAL)
// is already the global depth test (see InitGL), so the second pass's
// identical vertices pass depth against the first pass without z-fighting;
// glDepthMask(GL_FALSE) keeps it from writing the depth buffer a second
// time regardless. Kept deliberately weak (see reflectTint below) to
// avoid re-triggering the same washout that hitting the field used to
// cause -- this is meant to read as a thin coat sweeping across the
// surface as the drum turns, not as another light source.
//
// Also gets its own subtle grain noise (CreateFacetTexture()), on the
// base pass only -- explicit texture coordinates (not the glyph's
// sphere-map texgen), tiled a handful of times across the facet for a
// speckle rather than one giant smear, and GL_MODULATE (not GL_ADD), so
// it can only darken the field a little, never brighten it past the
// carefully-tuned base color.
void DrawFacetField(float dir, float r, float g, float b)
{
	FacetGeometry fg = ComputeFacetGeometry(dir);
	const float recess = 0.004f;
	float ox = fg.Nx*recess, oy = fg.Ny*recess;
	const float rep = 5.0f;	// Texture Repeat Count Across Each Axis Of The Facet

	// Pass 1: base field color, grain-modulated, with a modest specular
	// material so the flat facet picks up a sliding highlight as it
	// turns through the light (the same GL_LIGHT_MODEL_LOCAL_VIEWER
	// mechanism that gave the glyphs a moving highlight instead of a
	// uniform flat glow -- see InitGL). Weaker and less tightly focused
	// than the glyph ink's own specular (inkSpec in DrawGlyphOnFacet) so
	// the numbers still read as the shinier, raised element.
	static const GLfloat fieldSpec[] = {0.30f,0.30f,0.32f,1.0f};
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, fieldSpec);
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
	glBindTexture(GL_TEXTURE_2D, facetTexture);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);

	glColor3f(r,g,b);
	glNormal3f(fg.Nx,fg.Ny,0);
	glBegin(GL_QUADS);
		glTexCoord2f(0,0);     glVertex3f(fg.Bx-ox,               fg.By-oy,               fg.Bz);
		glTexCoord2f(rep,0);   glVertex3f(fg.Bx-ox,               fg.By-oy,               fg.Bz-1.0f);
		glTexCoord2f(rep,rep); glVertex3f(fg.Bx+fg.chordX-ox,     fg.By+fg.chordY-oy,     fg.Bz-1.0f);
		glTexCoord2f(0,rep);   glVertex3f(fg.Bx+fg.chordX-ox,     fg.By+fg.chordY-oy,     fg.Bz);
	glEnd();

	// Pass 2: thin additive reflection glaze, same quad, chrome env-map
	// in place of the grain texture. Deliberately dim (reflectTint) --
	// this is a coat of gloss over the tuned field color, not a second
	// light source.
	static const GLfloat noSpec[] = {0.0f,0.0f,0.0f,1.0f};
	static const float reflectTint[3] = {0.20f,0.20f,0.22f};
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, noSpec);
	glBindTexture(GL_TEXTURE_2D, envMapTexture);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_MODULATE);
	glEnable(GL_TEXTURE_GEN_S);
	glEnable(GL_TEXTURE_GEN_T);
	glDepthMask(GL_FALSE);
	glEnable(GL_BLEND);
	glBlendFunc(GL_ONE, GL_ONE);

	glColor3f(reflectTint[0],reflectTint[1],reflectTint[2]);
	glNormal3f(fg.Nx,fg.Ny,0);
	glBegin(GL_QUADS);
		glVertex3f(fg.Bx-ox,               fg.By-oy,               fg.Bz);
		glVertex3f(fg.Bx-ox,               fg.By-oy,               fg.Bz-1.0f);
		glVertex3f(fg.Bx+fg.chordX-ox,     fg.By+fg.chordY-oy,     fg.Bz-1.0f);
		glVertex3f(fg.Bx+fg.chordX-ox,     fg.By+fg.chordY-oy,     fg.Bz);
	glEnd();

	glDisable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);	// Restore The Global Default (see InitGL)
	glDepthMask(GL_TRUE);
	glDisable(GL_TEXTURE_GEN_S);
	glDisable(GL_TEXTURE_GEN_T);
}

// Transforms a glyph mesh, given in facet-local (u,v,w) coordinates (see
// the comment atop glyph_meshes.h), onto the facet running from angle
// "dir" to angle "dir+36", and draws it with the given ink color.
//
// u spans the facet's Z extent (what the old texture atlas mapped
// horizontally, +0.5..-0.5), so its basis vector is the fixed unit vector
// (0,0,-1). v spans the angular sweep from dir to dir+36 (what the atlas
// mapped vertically, 0..1), so its basis is the actual (non-unit) chord
// between the two angle points. w is how far the glyph is extruded
// outward from the drum's surface, so its basis is the unit outward
// normal N.
//
// Normals need the *unit* version of the v-axis chord (chordDir) instead
// of the chord itself -- transforming a normal through a non-uniform
// scale (v's chord length isn't 1, while u and N are) the same way as a
// position would tilt it. The mesh's own stored w-normal component also
// needs negating here: the mesh generation step flips the extrusion's Z
// axis when normalizing each glyph into u/v/w space, but doesn't flip the
// normals' Z component to match, so the raised (outward) cap comes out of
// glyph_meshes.h with an inward-pointing normal unless corrected here.
void DrawGlyphOnFacet(const GlyphMesh &mesh, float dir, float r, float g, float b)
{
	FacetGeometry fg = ComputeFacetGeometry(dir);

	const float *src = mesh.verts;
	unsigned int n = mesh.vertCount;
	if (n > MAX_GLYPH_VERTS) n = MAX_GLYPH_VERTS;	// Should never happen -- safety clamp only

	for (unsigned int i=0;i<n;i++)
	{
		float u  = src[i*6+0];
		float v  = src[i*6+1];
		float w  = src[i*6+2];
		float nu = src[i*6+3];
		float nv = src[i*6+4];
		float nw = -src[i*6+5];	// Corrects The Generation-Time Sign Flip -- See Comment Above

		scratchVerts[i*3+0] = fg.Bx + v*fg.chordX + w*fg.Nx;
		scratchVerts[i*3+1] = fg.By + v*fg.chordY + w*fg.Ny;
		scratchVerts[i*3+2] = fg.Bz - u;

		float nx = nv*fg.chordDirX + nw*fg.Nx;
		float ny = nv*fg.chordDirY + nw*fg.Ny;
		float nz = -nu;
		float nlen = sqrt(nx*nx+ny*ny+nz*nz);
		if (nlen > 0.00001f)
		{
			nx/=nlen; ny/=nlen; nz/=nlen;
		}
		scratchNormals[i*3+0] = nx;
		scratchNormals[i*3+1] = ny;
		scratchNormals[i*3+2] = nz;
	}

	// The specular highlight and chrome env-map reflection (see InitGL)
	// are restored here -- they're switched off in DrawFacetField() so
	// only the raised metal glyph itself catches the shine, not the flat
	// backdrop it's sitting on (which gets its own, plainer grain texture
	// instead -- see the comment there). GL_TEXTURE_GEN_S/T drive the
	// sphere-map's automatic reflection coordinates, so those need
	// switching back on too, since DrawFacetField() turns them off to use
	// its own explicit texture coordinates instead.
	static const GLfloat inkSpec[] = {0.55f,0.55f,0.55f,1.0f};
	glMaterialfv(GL_FRONT_AND_BACK, GL_SPECULAR, inkSpec);
	glBindTexture(GL_TEXTURE_2D, envMapTexture);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_ADD);
	glEnable(GL_TEXTURE_GEN_S);
	glEnable(GL_TEXTURE_GEN_T);

	glColor3f(r,g,b);
	glVertexPointer(3, GL_FLOAT, 0, scratchVerts);
	glNormalPointer(GL_FLOAT, 0, scratchNormals);
	glDrawElements(GL_TRIANGLES, mesh.indexCount, GL_UNSIGNED_SHORT, mesh.indices);
}

int rot[9]={-18,-18,-18,-18,-18,-18,-18,-18,-18};
int dat[9]={0,0,0,0,0,0,0,0,0};

int timev=0;

LRESULT	CALLBACK WndProc(HWND, UINT, WPARAM, LPARAM);	

void drawwheel(int num)
{
	int x=0;
	for (int dir=0;dir<360;dir+=36)
	{
		// Which digit is "current" used to be shown by tinting a shared
		// texture red, then by which of two texture atlases the facet was
		// bound to; now it's the field/ink coloring below -- white field +
		// dark ink for the one facet actually displaying the live time,
		// dark gray field + light ink for every other facet.
		if (x==num)
		{
			DrawFacetField(dir, ACTIVE_FIELD_R,ACTIVE_FIELD_G,ACTIVE_FIELD_B);
			DrawGlyphOnFacet(*DIGIT_GLYPHS[x], dir, INK_R,INK_G,INK_B);
		}
		else
		{
			DrawFacetField(dir, INACTIVE_FIELD_R,INACTIVE_FIELD_G,INACTIVE_FIELD_B);
			DrawGlyphOnFacet(*DIGIT_GLYPHS[x], dir, INK_R,INK_G,INK_B);
		}
		x++;
	}
}

void drawcolon()
{
	// The colon never rotates (rot[3]/rot[6] stay at a constant -18), so
	// facet x==0 is always the one actually shown up front -- same active/
	// inactive coloring split as drawwheel(), just fixed to that one facet
	// (every facet shows the same colon glyph either way).
	int x=0;
	for (int dir=0;dir<360;dir+=36)
	{
		if (x==0)
		{
			DrawFacetField(dir, ACTIVE_FIELD_R,ACTIVE_FIELD_G,ACTIVE_FIELD_B);
			DrawGlyphOnFacet(GLYPH_COLON, dir, INK_R,INK_G,INK_B);
		}
		else
		{
			DrawFacetField(dir, INACTIVE_FIELD_R,INACTIVE_FIELD_G,INACTIVE_FIELD_B);
			DrawGlyphOnFacet(GLYPH_COLON, dir, INK_R,INK_G,INK_B);
		}
		x++;
	}
}

void drawpmam(bool pm)
{
	// Same fixed, non-rotating facet as drawcolon() -- x==0 is always the
	// one shown up front.
	int x=0;
	const GlyphMesh &glyph = pm ? GLYPH_PM : GLYPH_AM;
	for (int dir=0;dir<360;dir+=36)
	{
		if (x==0)
		{
			DrawFacetField(dir, ACTIVE_FIELD_R,ACTIVE_FIELD_G,ACTIVE_FIELD_B);
			DrawGlyphOnFacet(glyph, dir, INK_R,INK_G,INK_B);
		}
		else
		{
			DrawFacetField(dir, INACTIVE_FIELD_R,INACTIVE_FIELD_G,INACTIVE_FIELD_B);
			DrawGlyphOnFacet(glyph, dir, INK_R,INK_G,INK_B);
		}
		x++;
	}
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
	glDisable(GL_LIGHTING);	// Flat Vertex-Alpha Overlay -- Not A Lit Surface, And Draws No Normals
	// Also Turn Off The Chrome Env-Map Texture (see InitGL) -- These Quads
	// Don't Set A Normal, So The Sphere-Map Texgen Would Otherwise Reuse
	// Whatever Normal The Last-Drawn Glyph Happened To Leave Behind.
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
	glEnable(GL_LIGHTING);
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
		// the near/far clip planes while looking down the wheels' shared
		// axis.
		//
		// Not quite straight down it, though: dead-on, each glyph's raised
		// cap faces the camera exactly, which puts its side "bevel" walls
		// perfectly edge-on -- zero screen area, so no amount of lighting
		// can make the embossing read. A few degrees of azimuthal tilt
		// (orbiting the eye slightly around the wheels' shared Y axis)
		// opens those bevels up into visible shaded slivers, which is what
		// actually sells the raised-3D look.
		const float cameraTiltDeg = 10.0f;
		float tiltRad = cameraTiltDeg*piover180;
		float eyeX = 10.0f*cos(tiltRad);
		float eyeZ = 5.0f - 10.0f*sin(tiltRad);
		gluLookAt(eyeX,0,eyeZ,
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

// Builds a small synthetic "studio chrome ball" gradient, purely in code
// (no embedded image data needed) -- a soft highlight standing in for an
// overhead light, offset a bit up and to the left of center, fading out
// to a darker room tone toward the rim. Used as a GL_SPHERE_MAP
// environment texture in InitGL, which is what actually gives the raised
// digits (and the curved drum facets under them) their rolling reflection
// -- see the comment there. Kept dim throughout (peak well under full
// white) since it's *added* onto the existing lit color rather than
// replacing it, so this should only ever contribute a subtle sheen, never
// wash anything out.
void CreateEnvMapTexture()
{
	const int size = 64;
	unsigned char *data = (unsigned char*)malloc(size*size*3);

	const float hlU = -0.2f, hlV = 0.35f;	// Highlight Position, Upper-Left Of Center
	const float sigma = 0.55f;				// Highlight Falloff Width

	for (int j=0;j<size;j++)
	{
		for (int i=0;i<size;i++)
		{
			float u = (i+0.5f)/size*2.0f-1.0f;
			float v = (j+0.5f)/size*2.0f-1.0f;
			float r = sqrt(u*u+v*v);

			float base = 0.02f + 0.04f*(1.0f-(r<1.0f?r:1.0f));	// Faint Center-Weighted Glow
			float dx = u-hlU, dy = v-hlV;
			float highlight = 0.35f*exp(-(dx*dx+dy*dy)/(2.0f*sigma*sigma));

			float c = base+highlight;
			if (c>0.42f) c=0.42f;	// Kept Modest -- See The Comment On DrawFacetField() For Why
			if (r>1.0f) c*=0.4f;	// Darken Past The Visible Disc, Same As A Real Sphere Map's Unused Corners

			unsigned char v8 = (unsigned char)(c*255.0f);
			unsigned char *px = data + (j*size+i)*3;
			px[0]=v8; px[1]=v8; px[2]=v8;
		}
	}

	glGenTextures(1, &envMapTexture);
	glBindTexture(GL_TEXTURE_2D, envMapTexture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);
	gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, size, size, GL_RGB, GL_UNSIGNED_BYTE, data);

	free(data);
}

// Builds a small grain-noise texture, purely in code, for the flat field
// quads (see DrawFacetField()) -- random brightness per noise cell, tiled
// a few times across each facet (see the repeat count in DrawFacetField())
// for a speckled/anodized look rather than directional brushed-metal
// streaks. Used with GL_MODULATE rather than GL_ADD, so texel values only
// ever darken the field's own color, never brighten past it -- the low
// end of the range is what makes the grain visible at all, since
// GL_MODULATE has no way to lighten a texel above the base color.
//
// The noise is generated in BLOCK x BLOCK texel cells, not one random
// value per individual texel: a true 1-texel checkerboard of random noise
// is exactly the kind of high spatial frequency that GL_LINEAR_MIPMAP_NEAREST
// and bilinear filtering are built to smooth away, so at typical facet
// size on screen it was mipmapping straight down to a flat average and
// reading as "basically no texture" -- which is what prompted this
// pass (the first cut used 1-texel noise across a narrow 0.90..1.00
// range, both of which fought against it staying visible). Coarser
// cells and a wider brightness swing survive minification and actually
// read as grain.
//
// Wrapped with GL_REPEAT on both axes rather than GL_CLAMP: since this
// texture is genuinely tiled (repeated several times across a facet, not
// stretched once edge-to-edge), GL_REPEAT is the correct mode anyway, and
// it has the added benefit of never sampling the texture's border color --
// GL_CLAMP does, and since nothing here ever explicitly set that border
// color, filtering right at the 0/1 texture-coordinate edge was quietly
// blending in the default black, which is what made the previous
// (GL_CLAMP-on-one-axis) brushed-metal version show black bars right at
// the top/bottom edge of every facet. Uncorrelated noise like this also
// has no visible seam when it wraps, since there's no directional
// continuity for a seam to interrupt.
void CreateFacetTexture()
{
	const int size = 64;
	const int block = 2;	// Texels Per Noise Cell -- Coarser Than 1:1 So
				// The Grain Survives Mipmap/Bilinear Filtering
	unsigned char *data = (unsigned char*)malloc(size*size*3);

	for (int jb=0;jb<size;jb+=block)
	{
		for (int ib=0;ib<size;ib+=block)
		{
			float v = 0.55f + (rand()%1000)/1000.0f*0.45f;	// 0.55..1.00, Wide Swing
			unsigned char v8 = (unsigned char)(v*255.0f);

			for (int j=jb; j<jb+block && j<size; j++)
			{
				for (int i=ib; i<ib+block && i<size; i++)
				{
					unsigned char *px = data + (j*size+i)*3;
					px[0]=v8; px[1]=v8; px[2]=v8;
				}
			}
		}
	}

	glGenTextures(1, &facetTexture);
	glBindTexture(GL_TEXTURE_2D, facetTexture);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR_MIPMAP_NEAREST);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_REPEAT);
	gluBuild2DMipmaps(GL_TEXTURE_2D, GL_RGB, size, size, GL_RGB, GL_UNSIGNED_BYTE, data);

	free(data);
}

int InitGL(void)
{
	// The glyph meshes are drawn via glDrawElements out of the scratch
	// buffers DrawGlyphOnFacet() fills each call, rather than immediate
	// mode -- enabled once, permanently, since it has no effect on the
	// glBegin/glVertex3f calls DrawFacetField()/DrawEdgeShading() still
	// use for their own flat quads.
	glEnableClientState(GL_VERTEX_ARRAY);
	glEnableClientState(GL_NORMAL_ARRAY);

	glShadeModel(GL_SMOOTH);
	// Medium gray instead of flat black -- see INACTIVE_FIELD_* above,
	// which is deliberately lighter still (and moves further above/below
	// this depending on how the light is hitting each facet) so the
	// drum's lit facets stay visibly brighter than the background and its
	// shadowed ones visibly darker, instead of the facets and the backdrop
	// reading as one continuous field. Keep the two in step if either
	// changes -- go too close together and the inactive facets start
	// blending back into the window the way they did against plain black.
	glClearColor(0.24f, 0.24f, 0.24f, 1.0f);
	glClearDepth(1.0f);
	glEnable(GL_DEPTH_TEST);							
	glDepthFunc(GL_LEQUAL);							
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glHint(GL_PERSPECTIVE_CORRECTION_HINT, GL_NICEST);

	// A single directional light shining roughly from the camera's side
	// (see drawwheel()'s per-vertex radial normals) gives the digit drum a
	// sense of actually curving away toward its edges -- the front-facing
	// facet catches full light while the partial facets peeking above and
	// below it dim slightly, reinforcing the cylinder shape. Ambient is
	// kept fairly high so nothing goes dark enough to hurt readability.
	// GL_COLOR_MATERIAL ties the existing glColor3f() calls (the red/white
	// active-digit highlighting) into the lit material color, so they keep
	// working exactly as before, just now modulated by the light too.
	GLfloat lightDir[]  = {1.0f, 0.0f, 0.0f, 0.0f};
	GLfloat lightAmb[]  = {0.55f, 0.55f, 0.55f, 1.0f};
	GLfloat lightDiff[] = {0.6f, 0.6f, 0.6f, 1.0f};
	glLightfv(GL_LIGHT0, GL_POSITION, lightDir);
	glLightfv(GL_LIGHT0, GL_AMBIENT, lightAmb);
	glLightfv(GL_LIGHT0, GL_DIFFUSE, lightDiff);

	// A tight specular highlight on top of that is what actually sells the
	// raised glyphs as metal -- ambient/diffuse alone just shade the
	// mesh's flat facets a bit darker or lighter, which is subtle to the
	// point of invisible on the ink. A specular highlight is *added* on
	// top of the ink color rather than multiplying it, so it stays
	// visible sweeping across the bevels no matter how dark the ink is.
	// Only actually applied to the glyph mesh (DrawGlyphOnFacet sets the
	// material specular color back to this; DrawFacetField zeroes it out
	// first) -- a strong highlight plus the env-map reflection below both
	// landing on an already-white active facet is what was originally
	// pushing its ink to solid white and erasing it. Nudged back up a bit
	// from that fix's most conservative setting now that the active
	// field and the ink are separated by hue as well as brightness (see
	// INK_* above), which gives both more room before a bright spot on
	// the ink reads as "merged with the field" rather than just "a shiny
	// highlight."
	GLfloat lightSpec[] = {0.65f, 0.65f, 0.65f, 1.0f};
	glLightfv(GL_LIGHT0, GL_SPECULAR, lightSpec);
	glMaterialf(GL_FRONT_AND_BACK, GL_SHININESS, 48.0f);

	// A second, much dimmer light from a different angle -- above, and a
	// bit toward the viewer -- fills in what would otherwise be a hard
	// black shadow on the side of the drum facing away from GL_LIGHT0, so
	// the curvature keeps reading all the way round the facets that peek
	// above and below the front row instead of some of them just going
	// flat black. No specular of its own -- one bright moving highlight
	// per surface is already plenty; a second would just compete with it.
	GLfloat light1Dir[]  = {0.0f, 1.0f, 0.4f, 0.0f};
	GLfloat light1Amb[]  = {0.0f, 0.0f, 0.0f, 1.0f};
	GLfloat light1Diff[] = {0.18f, 0.18f, 0.18f, 1.0f};
	glLightfv(GL_LIGHT1, GL_POSITION, light1Dir);
	glLightfv(GL_LIGHT1, GL_AMBIENT, light1Amb);
	glLightfv(GL_LIGHT1, GL_DIFFUSE, light1Diff);
	glEnable(GL_LIGHT1);

	// The camera sits only 10 world units away, not at infinity, so the
	// default specular math (which just assumes the eye is always looking
	// straight down -Z) is noticeably wrong at this range -- concretely,
	// it makes the specular highlight on every flat surface (each facet's
	// field quad, each glyph's flat raised cap) perfectly uniform across
	// the whole thing, since one constant assumed eye direction plus one
	// constant normal can only ever produce one intensity. The *local*
	// viewer model computes each vertex's real direction to the eye
	// instead, so those same flat surfaces get a highlight that actually
	// moves across them as the drum turns, rather than lighting up (or
	// not) all at once.
	glLightModeli(GL_LIGHT_MODEL_LOCAL_VIEWER, GL_TRUE);

	glEnable(GL_LIGHT0);
	glEnable(GL_LIGHTING);
	glColorMaterial(GL_FRONT_AND_BACK, GL_AMBIENT_AND_DIFFUSE);
	glEnable(GL_COLOR_MATERIAL);

	// A small synthetic "chrome ball" gradient (see CreateEnvMapTexture())
	// auto-mapped onto every lit surface via GL_SPHERE_MAP texture
	// coordinate generation -- classic environment-mapped reflection,
	// standard since OpenGL 1.1, no shaders or extension loading needed.
	// The generated coordinates come from each vertex's own normal, so
	// different facets around the drum -- and different points along each
	// glyph's curved bevel -- sample different parts of the gradient,
	// giving the metal a rolling sheen on top of the direct lighting
	// above. It's *added* (GL_TEXTURE_ENV_MODE, GL_ADD) rather than
	// multiplied in, same reasoning as the specular highlight: it only
	// adds shine on top of the ink/field color, never darkens it.
	CreateEnvMapTexture();
	glTexGeni(GL_S, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
	glTexGeni(GL_T, GL_TEXTURE_GEN_MODE, GL_SPHERE_MAP);
	glEnable(GL_TEXTURE_2D);

	// The field quads get their own, much plainer brushed-metal grain
	// texture instead (see CreateFacetTexture()) -- explicit texture
	// coordinates rather than sphere-map generation, and GL_MODULATE
	// rather than GL_ADD so it can only darken the field slightly, not
	// brighten it. Since both the env-map and the grain texture share the
	// single texture unit here, DrawFacetField() and DrawGlyphOnFacet()
	// each swap in their own texture/texgen/blend-mode setup right before
	// drawing rather than InitGL picking one default -- see those
	// functions.
	CreateFacetTexture();

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
	// The custom clock icon (CylinderClock.rc) -- used for the window's
	// title-bar icon here, and separately as the .exe/taskbar icon just by
	// virtue of being the resource script's first/only icon. RegisterClass
	// (as opposed to RegisterClassEx) has no separate "small icon" slot, so
	// Windows automatically scales this one down for the title bar.
	wc.hIcon			= LoadIcon(hInstance, MAKEINTRESOURCE(IDI_ICON1));
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

	// wc.hIcon (set above) reliably drives the title-bar and Alt-Tab icon,
	// but the taskbar button doesn't always pick it up just from the
	// window class -- setting it explicitly via WM_SETICON, for both the
	// big (taskbar/Alt-Tab) and small (title-bar) sizes, is the standard
	// fix. Done here, right before the window -- and its taskbar button --
	// actually appears.
	HICON hIconBig   = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 32, 32, LR_DEFAULTCOLOR);
	HICON hIconSmall = (HICON)LoadImage(hInstance, MAKEINTRESOURCE(IDI_ICON1), IMAGE_ICON, 16, 16, LR_DEFAULTCOLOR);
	SendMessage(hWnd, WM_SETICON, ICON_BIG, (LPARAM)hIconBig);
	SendMessage(hWnd, WM_SETICON, ICON_SMALL, (LPARAM)hIconSmall);

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
	if (!CreateGLWindow("Cylinder Clock",1000,340,16,fullscreen))
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