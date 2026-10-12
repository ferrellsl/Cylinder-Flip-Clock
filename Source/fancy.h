// Per-pixel shading for the clock: brushed-metal drums, polished raised
// digits, studio reflections, soft shadows under the digits and
// anti-aliased edges.
//
// The original program draws with the fixed-function pipeline of OpenGL
// 1.1, which is all <gl\gl.h> on Windows declares.  Everything newer than
// that is still in the graphics driver, though -- it only has to be asked
// for by name (wglGetProcAddress) -- so this file fetches the handful of
// OpenGL 2.0 entry points a GLSL shader needs, and the two WGL extensions
// for multisampling and vertical sync, with no extra library.  If any of it
// is missing (a very old driver, a remote desktop session) FancyInit()
// returns false and main.cpp carries on drawing the way it did before.
//
// The shaders are GLSL 1.20, which still has the built-in gl_Vertex,
// gl_Normal, gl_Color and matrix uniforms, so the drawing code goes on using
// glBegin/glVertex, vertex arrays and the matrix stack unchanged.
#pragma once

#ifndef APIENTRY
#define APIENTRY
#endif

#define FGL_FRAGMENT_SHADER   0x8B30
#define FGL_VERTEX_SHADER     0x8B31
#define FGL_COMPILE_STATUS    0x8B81
#define FGL_LINK_STATUS       0x8B82
#define FGL_MULTISAMPLE       0x809D

typedef GLuint (APIENTRY *FglCreateShader)(GLenum);
typedef void   (APIENTRY *FglShaderSource)(GLuint, GLsizei, const char **, const GLint *);
typedef void   (APIENTRY *FglCompileShader)(GLuint);
typedef void   (APIENTRY *FglGetShaderiv)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *FglGetShaderInfoLog)(GLuint, GLsizei, GLsizei *, char *);
typedef GLuint (APIENTRY *FglCreateProgram)(void);
typedef void   (APIENTRY *FglAttachShader)(GLuint, GLuint);
typedef void   (APIENTRY *FglLinkProgram)(GLuint);
typedef void   (APIENTRY *FglGetProgramiv)(GLuint, GLenum, GLint *);
typedef void   (APIENTRY *FglUseProgram)(GLuint);
typedef GLint  (APIENTRY *FglGetUniformLocation)(GLuint, const char *);
typedef void   (APIENTRY *FglUniform1i)(GLint, GLint);
typedef void   (APIENTRY *FglUniform1f)(GLint, GLfloat);
typedef void   (APIENTRY *FglUniform3f)(GLint, GLfloat, GLfloat, GLfloat);
typedef BOOL   (APIENTRY *FwglChoosePixelFormatARB)(HDC, const int *, const FLOAT *, UINT, int *, UINT *);
typedef BOOL   (APIENTRY *FwglSwapIntervalEXT)(int);

static FglUseProgram  fglUseProgram = NULL;
static FglUniform1i   fglUniform1i = NULL;
static FglUniform1f   fglUniform1f = NULL;
static FglUniform3f   fglUniform3f = NULL;

static GLuint fancyProgram = 0;
static GLint  fancyKind = -1, fancySeed = -1, fancyFacetN = -1, fancyFacetD = -1, fancyDepth = -1;

// What is being drawn (the shader's uKind)
enum { FANCY_FIELD = 0, FANCY_GLYPH = 1, FANCY_CAP = 2, FANCY_SHADOW = 3, FANCY_BACKDROP = 4 };

static const char *fancyVertexSource =
"#version 120\n"
"uniform vec3 uFacetN;\n"         // the facet a digit stands on: its plane, in the wheel's own space
"uniform float uFacetD;\n"
"varying vec3 vN;\n"
"varying vec3 vObj;\n"
"varying vec2 vUV;\n"
"varying vec4 vCol;\n"
"varying float vH;\n"             // height above that facet
"varying vec3 vTu;\n"             // the wheel's axis, in eye space
"varying vec3 vEye;\n"            // where the point is, in eye space
"void main()\n"
"{\n"
"  gl_Position = gl_ModelViewProjectionMatrix * gl_Vertex;\n"
"  vEye = (gl_ModelViewMatrix * gl_Vertex).xyz;\n"
"  vN = gl_NormalMatrix * gl_Normal;\n"
"  vObj = gl_Vertex.xyz;\n"
"  vUV = gl_MultiTexCoord0.xy;\n"
"  vCol = gl_Color;\n"
"  vH = dot(gl_Vertex.xyz, uFacetN) - uFacetD;\n"
"  vTu = gl_NormalMatrix * vec3(0.0, 0.0, -1.0);\n"
"}\n";

static const char *fancyFragmentSource =
"#version 120\n"
"uniform int uKind;\n"
"uniform float uSeed;\n"          // differs from wheel to wheel, so their grain does too
"uniform float uDepth;\n"         // how far the digits stand out
"varying vec3 vN;\n"
"varying vec3 vObj;\n"
"varying vec2 vUV;\n"
"varying vec4 vCol;\n"
"varying float vH;\n"
"varying vec3 vTu;\n"
"varying vec3 vEye;\n"
"\n"
"float hash(vec2 p) { return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453); }\n"
"float noise(vec2 p)\n"
"{\n"
"  vec2 i = floor(p), f = fract(p);\n"
"  f = f * f * (3.0 - 2.0 * f);\n"
"  return mix(mix(hash(i), hash(i + vec2(1.0, 0.0)), f.x),\n"
"             mix(hash(i + vec2(0.0, 1.0)), hash(i + vec2(1.0, 1.0)), f.x), f.y);\n"
"}\n"
"float band(float x, float a, float c, float b)\n"
"{\n"
"  return smoothstep(a - b, a + b, x) * (1.0 - smoothstep(c - b, c + b, x));\n"
"}\n"
"\n"
// The room the metal reflects: a dim studio with a long soft light
// overhead, a thin strip light at eye level, a warm lamp off to one side
// and a dark floor.  b blurs it, for the rougher surfaces.
"vec3 room(vec3 R, float b)\n"
"{\n"
"  vec3 c = mix(vec3(0.012, 0.013, 0.016), vec3(0.11, 0.12, 0.15), smoothstep(-0.5, 0.7, R.y));\n"
"  c += vec3(1.00, 0.97, 0.92) * 1.55 * band(R.y, 0.38, 0.93, b) * band(R.x, -0.85, 0.55, b + 0.05);\n"
"  c += vec3(1.00, 0.95, 0.88) * 0.62 * band(R.y, -0.10, 0.14, b);\n"
"  c += vec3(1.00, 0.62, 0.30) * 0.50 * band(R.x, 0.55, 1.00, b + 0.08) * band(R.y, -0.45, 0.30, b + 0.08);\n"
"  return c;\n"
"}\n"
"\n"
"void main()\n"
"{\n"
"  if (uKind == 3) { gl_FragColor = vec4(0.0, 0.0, 0.0, vCol.a); return; }\n"
"  if (uKind == 4)\n"
"  {\n"
"    float r = length(vUV * vec2(0.85, 1.5));\n"
"    vec3 c = mix(vec3(0.060, 0.068, 0.085), vec3(0.004, 0.004, 0.006), smoothstep(0.05, 1.25, r));\n"
     // (the last term is a grain of one color step, against banding in the gradient)
"    gl_FragColor = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)) + (hash(gl_FragCoord.xy) - 0.5) / 255.0, 1.0);\n"
"    return;\n"
"  }\n"
"\n"
   // The projection is orthographic, but the reflections are worked out for
   // a viewer a few units away: seen from infinitely far, a flat face
   // reflects one single point of the room and comes out one flat color.
"  vec3 V = normalize(vec3(-vEye.x * 0.35, -vEye.y * 2.2, 4.0));\n"
"  vec3 N = normalize(vN);\n"
"  if (dot(N, V) < 0.0) N = -N;\n"
"  vec3 Tu = normalize(vTu);\n"
"  vec3 Tv = normalize(cross(N, Tu));\n"
"  vec3 tint = vCol.rgb;\n"
"  float blur = 0.30, shade = 1.0, body = 0.30, gloss = 40.0, shine = 0.8;\n"
"\n"
"  if (uKind == 0)\n"
"  {\n"
     // A drum's face: brushed around the drum, so the grain is fine along
     // the axis and long around it; it tips the surface a little this way
     // and that.  The face rolls over at the wheel's two rims, and a fine
     // dark line parts each facet from the next.
"    float around = atan(vObj.y, vObj.x);\n"
"    float grain = 0.55 * noise(vec2(vObj.z * 330.0 + uSeed * 17.0, around * 3.0 + uSeed))\n"
"                + 0.30 * noise(vec2(vObj.z * 900.0 + uSeed * 5.0, around * 7.0))\n"
"                + 0.15 * noise(vec2(vObj.z * 60.0, around * 40.0 + uSeed * 3.0));\n"
"    float rim = min(vUV.x, 1.0 - vUV.x);\n"
"    float roll = (1.0 - smoothstep(0.0, 0.035, rim)) * (vUV.x < 0.5 ? -1.0 : 1.0);\n"
"    N = normalize(N + Tu * ((grain - 0.5) * 0.16 + roll * 0.9));\n"
"    float seam = min(vUV.y, 1.0 - vUV.y);\n"
"    shade = mix(0.35, 1.0, smoothstep(0.0, 0.012, rim)) * mix(0.45, 1.0, smoothstep(0.004, 0.022, seam));\n"
"    tint *= 0.88 + 0.24 * grain;\n"
"    blur = 0.20;  body = 0.42;  gloss = 26.0;  shine = 0.55;\n"
"  }\n"
"  else if (uKind == 1)\n"
"  {\n"
     // A digit: polished, and darker toward its foot, where less light gets in
"    shade = mix(0.50, 1.0, smoothstep(0.0, uDepth * 0.9, vH));\n"
     // (and never quite flat: polished by hand, its face waves a very little)
"    vec2 q = vec2(vObj.z * 9.0 + uSeed * 3.0, atan(vObj.y, vObj.x) * 9.0);\n"
"    N = normalize(N + Tu * (noise(q) - 0.5) * 0.10 + Tv * (noise(q + 37.0) - 0.5) * 0.10);\n"
"    blur = 0.035;  body = 0.26;  gloss = 90.0;  shine = 1.25;\n"
"  }\n"
"  else\n"
"  {\n"
"    blur = 0.35;  body = 0.5;  gloss = 12.0;  shine = 0.25;\n"
"  }\n"
"\n"
"  vec3 L1 = normalize(vec3(-0.45, 0.62, 0.64));\n"      // key light: above, to the left
"  vec3 L2 = normalize(vec3(0.75, -0.15, 0.64));\n"      // fill: low, to the right, warm
"  float d1 = max(dot(N, L1), 0.0), d2 = max(dot(N, L2), 0.0);\n"
"  vec3 H1 = normalize(L1 + V), H2 = normalize(L2 + V);\n"
"  float s1 = pow(max(dot(N, H1), 0.0), gloss), s2 = pow(max(dot(N, H2), 0.0), gloss);\n"
"  if (uKind == 0)\n"
"  {\n"
     // (brushed metal smears a highlight out across its grain)
"    float a1 = dot(Tv, H1), a2 = dot(Tv, H2);\n"
"    s1 = 0.5 * s1 + 0.5 * pow(sqrt(max(1.0 - a1 * a1, 0.0)), 70.0) * d1;\n"
"    s2 = 0.5 * s2 + 0.5 * pow(sqrt(max(1.0 - a2 * a2, 0.0)), 70.0) * d2;\n"
"  }\n"
"  float fresnel = pow(1.0 - max(dot(N, V), 0.0), 5.0);\n"
"  vec3 mirror = mix(tint, vec3(1.0), fresnel);\n"
"  vec3 c = tint * body * (0.10 + 0.95 * d1 + 0.30 * d2 * vec3(1.0, 0.85, 0.7));\n"
"  c += mirror * room(reflect(-V, N), blur) * (1.0 - body) * 0.9;\n"
"  c += mirror * shine * (s1 * vec3(1.0, 0.98, 0.94) + 0.35 * s2 * vec3(1.0, 0.8, 0.6));\n"
"  c *= shade;\n"
   // (bright reflections roll off toward white gently, keeping their color,
   // instead of clipping to it)
"  float lum = dot(c, vec3(0.3, 0.6, 0.1));\n"
"  c *= 1.25 * (1.0 + lum / 6.0) / (1.0 + lum);\n"
"  gl_FragColor = vec4(pow(max(c, 0.0), vec3(1.0 / 2.2)), 1.0);\n"
"}\n";

static GLuint FancyCompile(GLenum type, const char *source, FglCreateShader createShader, FglShaderSource shaderSource,
                           FglCompileShader compileShader, FglGetShaderiv getShaderiv, FglGetShaderInfoLog getLog)
{
	GLuint shader = createShader(type);
	if (!shader)
		return 0;
	shaderSource(shader, 1, &source, NULL);
	compileShader(shader);
	GLint ok = 0;
	getShaderiv(shader, FGL_COMPILE_STATUS, &ok);
	if (!ok)
	{
		char log[2048] = "";
		getLog(shader, sizeof(log) - 1, NULL, log);
		OutputDebugStringA("CylinderClock shader: ");
		OutputDebugStringA(log);
		if (getenv("CYLINDERCLOCK_DEBUG"))
			MessageBoxA(NULL, log, "Shader", MB_OK);
		return 0;
	}
	return shader;
}

// Build the shader program.  Call with the OpenGL context current.  False
// if this driver can't do it: the caller then draws the old way.
static bool FancyInit(void)
{
#define FANCY_GET(type, name) type name = (type)wglGetProcAddress(#name); if (!name) return false;
	FANCY_GET(FglCreateShader, glCreateShader)
	FANCY_GET(FglShaderSource, glShaderSource)
	FANCY_GET(FglCompileShader, glCompileShader)
	FANCY_GET(FglGetShaderiv, glGetShaderiv)
	FANCY_GET(FglGetShaderInfoLog, glGetShaderInfoLog)
	FANCY_GET(FglCreateProgram, glCreateProgram)
	FANCY_GET(FglAttachShader, glAttachShader)
	FANCY_GET(FglLinkProgram, glLinkProgram)
	FANCY_GET(FglGetProgramiv, glGetProgramiv)
	FANCY_GET(FglGetUniformLocation, glGetUniformLocation)
#undef FANCY_GET
	fglUseProgram = (FglUseProgram)wglGetProcAddress("glUseProgram");
	fglUniform1i = (FglUniform1i)wglGetProcAddress("glUniform1i");
	fglUniform1f = (FglUniform1f)wglGetProcAddress("glUniform1f");
	fglUniform3f = (FglUniform3f)wglGetProcAddress("glUniform3f");
	if (!fglUseProgram || !fglUniform1i || !fglUniform1f || !fglUniform3f)
		return false;

	GLuint vs = FancyCompile(FGL_VERTEX_SHADER, fancyVertexSource, glCreateShader, glShaderSource, glCompileShader,
	                         glGetShaderiv, glGetShaderInfoLog);
	GLuint fs = FancyCompile(FGL_FRAGMENT_SHADER, fancyFragmentSource, glCreateShader, glShaderSource, glCompileShader,
	                         glGetShaderiv, glGetShaderInfoLog);
	if (!vs || !fs)
		return false;
	GLuint program = glCreateProgram();
	glAttachShader(program, vs);
	glAttachShader(program, fs);
	glLinkProgram(program);
	GLint ok = 0;
	glGetProgramiv(program, FGL_LINK_STATUS, &ok);
	if (!ok)
		return false;

	fancyProgram = program;
	fancyKind   = glGetUniformLocation(program, "uKind");
	fancySeed   = glGetUniformLocation(program, "uSeed");
	fancyFacetN = glGetUniformLocation(program, "uFacetN");
	fancyFacetD = glGetUniformLocation(program, "uFacetD");
	fancyDepth  = glGetUniformLocation(program, "uDepth");

	// Draw no faster than the screen shows (the loop otherwise runs flat out)
	FwglSwapIntervalEXT swapInterval = (FwglSwapIntervalEXT)wglGetProcAddress("wglSwapIntervalEXT");
	if (swapInterval)
		swapInterval(1);
	glEnable(FGL_MULTISAMPLE);
	return true;
}

// A pixel format with multisampling (smooth edges), or 0 if there is none.
// Asking for one takes an OpenGL context, and a window's pixel format can
// be set only once, so this makes a throwaway window to ask with.
static int FancyMultisampleFormat(HINSTANCE instance)
{
	int format = 0;
	HWND window = CreateWindowA("STATIC", "", WS_POPUP, 0, 0, 16, 16, NULL, NULL, instance, NULL);
	if (!window)
		return 0;
	HDC dc = GetDC(window);
	PIXELFORMATDESCRIPTOR pfd;
	memset(&pfd, 0, sizeof(pfd));
	pfd.nSize = sizeof(pfd);
	pfd.nVersion = 1;
	pfd.dwFlags = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER;
	pfd.iPixelType = PFD_TYPE_RGBA;
	pfd.cColorBits = 32;
	pfd.cDepthBits = 24;
	int plain = ChoosePixelFormat(dc, &pfd);
	HGLRC rc = NULL;
	if (plain && SetPixelFormat(dc, plain, &pfd) && (rc = wglCreateContext(dc)) != NULL && wglMakeCurrent(dc, rc))
	{
		FwglChoosePixelFormatARB choose = (FwglChoosePixelFormatARB)wglGetProcAddress("wglChoosePixelFormatARB");
		for (int samples = 8; choose && samples >= 2 && !format; samples /= 2)
		{
			const int wanted[] =
			{
				0x2001, 1,          // WGL_DRAW_TO_WINDOW_ARB
				0x2010, 1,          // WGL_SUPPORT_OPENGL_ARB
				0x2011, 1,          // WGL_DOUBLE_BUFFER_ARB
				0x2003, 0x2027,     // WGL_ACCELERATION_ARB: WGL_FULL_ACCELERATION_ARB
				0x2013, 0x202B,     // WGL_PIXEL_TYPE_ARB: WGL_TYPE_RGBA_ARB
				0x2014, 32,         // WGL_COLOR_BITS_ARB
				0x2022, 24,         // WGL_DEPTH_BITS_ARB
				0x2041, 1,          // WGL_SAMPLE_BUFFERS_ARB
				0x2042, samples,    // WGL_SAMPLES_ARB
				0
			};
			int found = 0;
			UINT count = 0;
			if (choose(dc, wanted, NULL, 1, &found, &count) && count > 0)
				format = found;
		}
	}
	wglMakeCurrent(NULL, NULL);
	if (rc)
		wglDeleteContext(rc);
	ReleaseDC(window, dc);
	DestroyWindow(window);
	return format;
}
