/*
 * Copyright (C) 2010 The Android Open Source Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <stdlib.h>
#include <stdio.h>
#include <time.h>
#include <sched.h>
#include <sys/resource.h>

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES/gl.h>
#include <GLES/glext.h>

#include <utils/Timers.h>

#include <WindowSurface.h>
#include <ui/GraphicBuffer.h>
#include <EGLUtils.h>

#define BMP_HEADER_SIZE 54
#define BMP_HEADER_MAGIC_0 0x42
#define BMP_HEADER_MAGIC_1 0x4d

using namespace android;

inline void* SafePointerFromUInt(unsigned int handle) {
    return (void*)(uintptr_t)(handle);
}

static void printGLString(const char *name, GLenum s) {
    // fprintf(stderr, "printGLString %s, %d\n", name, s);
    const char *v = (const char *) glGetString(s);
    // int error = glGetError();
    // fprintf(stderr, "glGetError() = %d, result of glGetString = %x\n", error,
    //        (unsigned int) v);
    // if ((v < (const char*) 0) || (v > (const char*) 0x10000))
    //    fprintf(stderr, "GL %s = %s\n", name, v);
    // else
    //    fprintf(stderr, "GL %s = (null) 0x%08x\n", name, (unsigned int) v);
    fprintf(stderr, "GL %s = %s\n", name, v);
}

static void checkEglError(const char* op, EGLBoolean returnVal = EGL_TRUE) {
    if (returnVal != EGL_TRUE) {
        fprintf(stderr, "%s() returned %d\n", op, returnVal);
    }

    for (EGLint error = eglGetError(); error != EGL_SUCCESS; error
            = eglGetError()) {
        fprintf(stderr, "after %s() eglError %s (0x%x)\n", op, EGLUtils::strerror(error),
                error);
    }
}

static void checkGlError(const char* op) {
    for (GLint error = glGetError(); error; error
            = glGetError()) {
        fprintf(stderr, "after %s() glError (0x%x)\n", op, error);
    }
}

bool setupGraphics(int w, int h) {
    glViewport(0, 0, w, h);
    checkGlError("glViewport");
    return true;
}

int align(int x, int a) {
    return (x + (a-1)) & (~(a-1));
}

int yuvTexWidth;
int yuvTexHeight;
GLuint yuvTex;

bool setupYuvTexSurface(EGLDisplay dpy, EGLContext context, char *fileName) {
    // check fp status
    // parse the BMP header
    unsigned char header[BMP_HEADER_SIZE];
    FILE* fp = fopen(fileName, "rb");
    if (fp) {
        fread (header, 1, BMP_HEADER_SIZE, fp);
    } else {
        fprintf(stderr, "unable to open BMP file:%s, err:%s\n", fileName, strerror(errno));
        return -errno;
    }

    // check the BMP magic words
    if ((BMP_HEADER_MAGIC_0 != header[0]) || (BMP_HEADER_MAGIC_1 != header[1])) {
        fprintf(stderr, "%s is not valid BMP file!\n", fileName);
        fclose(fp);
        return -1;
    }

    // get the file size, picture width, height and per-pixel size info
    int mS = header[02] + (header[03] << 8) + (header[04] << 16) + (header[05] << 24);
    int mW = header[18] + (header[19] << 8) + (header[20] << 16) + (header[21] << 24);
    int mH = header[22] + (header[23] << 8) + (header[24] << 16) + (header[25] << 24);
    // per-pixel size
    int mP = header[28];
    printf("BMP width:%d, height:%d, file size:%d, per-pixel size:%d\n", mW, mH, mS, mP);
    // check the pixel format is RGBA or RGB
    int mF = mP / 8;

    yuvTexWidth = mW;
    yuvTexHeight = mH;

    // allocate memory space for storing pixels
    uint32_t *t32my = (uint32_t *) malloc(sizeof(uint32_t) * mW * mH);
    unsigned char tmpb[mF*mW];
    for(int ii = 0 ; ii < mH ; ii++) {
        fseek(fp, BMP_HEADER_SIZE + (mH-ii-1)*mF*mW , SEEK_SET);
        fread(tmpb, 1, mF*mW, fp);
        for(int jj = 0; jj < mW; jj++) {
            t32my[ii*mW+jj] = (0xff000000|(tmpb[jj*mF+0]<<16)|(tmpb[jj*mF+1]<<8)|tmpb[jj*mF+2]);
        }
    }
    fclose(fp);

    glGenTextures(1, &yuvTex);
    checkGlError("glGenTextures");
    glBindTexture(GL_TEXTURE_2D, yuvTex);
    checkGlError("glBindTexture");
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGBA,
                 mW,
                 mH,
                 0,
                 GL_RGBA,
                 GL_UNSIGNED_BYTE,
                 t32my);
    free(t32my);

    // the image is used for copySubTexImage
    EGLImageKHR img = eglCreateImageKHR(dpy,
                                        eglGetCurrentContext(),
                                        EGL_GL_TEXTURE_2D_KHR,
                                        (EGLClientBuffer)SafePointerFromUInt(yuvTex),
                                        NULL);
    checkEglError("eglCreateImageKHR");
    if (img == EGL_NO_IMAGE_KHR) {
        return false;
    }

    glEGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES)img);
    checkGlError("glEGLImageTargetTexture2DOES");

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    checkGlError("glTexParameteri");
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    checkGlError("glTexParameteri");
    glTexEnvx(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
    checkGlError("glTexEnvx");

    GLint crop[4] = { 0, yuvTexHeight, yuvTexWidth, -yuvTexHeight };
    glTexParameteriv(GL_TEXTURE_2D, GL_TEXTURE_CROP_RECT_OES, crop);
    checkGlError("glTexParameteriv");

    return true;
}

void renderFrame(int w, int h) {
    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    checkGlError("glClearColor");
    glClear( GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    checkGlError("glClear");
    
    glBindTexture(GL_TEXTURE_2D, yuvTex);
    checkGlError("glBindTexture");
    glEnable(GL_TEXTURE_2D);
    checkGlError("glEnable");

    glDrawTexiOES(0, 0, 0, w, h);
    checkGlError("glDrawTexiOES");
}

void printEGLConfiguration(EGLDisplay dpy, EGLConfig config) {

#define X(VAL) {VAL, #VAL}
    struct {EGLint attribute; const char* name;} names[] = {
    X(EGL_BUFFER_SIZE),
    X(EGL_ALPHA_SIZE),
    X(EGL_BLUE_SIZE),
    X(EGL_GREEN_SIZE),
    X(EGL_RED_SIZE),
    X(EGL_DEPTH_SIZE),
    X(EGL_STENCIL_SIZE),
    X(EGL_CONFIG_CAVEAT),
    X(EGL_CONFIG_ID),
    X(EGL_LEVEL),
    X(EGL_MAX_PBUFFER_HEIGHT),
    X(EGL_MAX_PBUFFER_PIXELS),
    X(EGL_MAX_PBUFFER_WIDTH),
    X(EGL_NATIVE_RENDERABLE),
    X(EGL_NATIVE_VISUAL_ID),
    X(EGL_NATIVE_VISUAL_TYPE),
    X(EGL_SAMPLES),
    X(EGL_SAMPLE_BUFFERS),
    X(EGL_SURFACE_TYPE),
    X(EGL_TRANSPARENT_TYPE),
    X(EGL_TRANSPARENT_RED_VALUE),
    X(EGL_TRANSPARENT_GREEN_VALUE),
    X(EGL_TRANSPARENT_BLUE_VALUE),
    X(EGL_BIND_TO_TEXTURE_RGB),
    X(EGL_BIND_TO_TEXTURE_RGBA),
    X(EGL_MIN_SWAP_INTERVAL),
    X(EGL_MAX_SWAP_INTERVAL),
    X(EGL_LUMINANCE_SIZE),
    X(EGL_ALPHA_MASK_SIZE),
    X(EGL_COLOR_BUFFER_TYPE),
    X(EGL_RENDERABLE_TYPE),
    X(EGL_CONFORMANT),
   };
#undef X

    for (size_t j = 0; j < sizeof(names) / sizeof(names[0]); j++) {
        EGLint value = -1;
        EGLint returnVal = eglGetConfigAttrib(dpy, config, names[j].attribute, &value);
        EGLint error = eglGetError();
        if (returnVal && error == EGL_SUCCESS) {
            printf(" %s: ", names[j].name);
            printf("%d (0x%x)", value, value);
        }
    }
    printf("\n");
}

int main(int argc, char** argv) {
    //parse the input parameters
    if (argc <= 1) {
        fprintf(stderr, "glDrawBMP bmpFile need bmp file as parameter!\n");
        return -1;
    }

    EGLBoolean returnValue;
    EGLConfig myConfig = {0};

    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 1, EGL_NONE };
    EGLint s_configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES_BIT,
            EGL_NONE };
    EGLint majorVersion;
    EGLint minorVersion;
    EGLContext context;
    EGLSurface surface;
    EGLint w, h;

    EGLDisplay dpy;

    checkEglError("<init>");
    dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    checkEglError("eglGetDisplay");
    if (dpy == EGL_NO_DISPLAY) {
        printf("eglGetDisplay returned EGL_NO_DISPLAY.\n");
        return 0;
    }

    returnValue = eglInitialize(dpy, &majorVersion, &minorVersion);
    checkEglError("eglInitialize", returnValue);
    fprintf(stderr, "EGL version %d.%d\n", majorVersion, minorVersion);
    if (returnValue != EGL_TRUE) {
        printf("eglInitialize failed\n");
        return 0;
    }

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();
    returnValue = EGLUtils::selectConfigForNativeWindow(dpy, s_configAttribs, window, &myConfig);
    if (returnValue) {
        printf("EGLUtils::selectConfigForNativeWindow() returned %d", returnValue);
        return 1;
    }

    checkEglError("EGLUtils::selectConfigForNativeWindow");

    printf("Chose this configuration:\n");
    printEGLConfiguration(dpy, myConfig);

    surface = eglCreateWindowSurface(dpy, myConfig, window, NULL);
    checkEglError("eglCreateWindowSurface");
    if (surface == EGL_NO_SURFACE) {
        printf("gelCreateWindowSurface failed.\n");
        return 1;
    }

    context = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, context_attribs);
    checkEglError("eglCreateContext");
    if (context == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        return 1;
    }
    returnValue = eglMakeCurrent(dpy, surface, surface, context);
    checkEglError("eglMakeCurrent", returnValue);
    if (returnValue != EGL_TRUE) {
        return 1;
    }
    eglQuerySurface(dpy, surface, EGL_WIDTH, &w);
    checkEglError("eglQuerySurface");
    eglQuerySurface(dpy, surface, EGL_HEIGHT, &h);
    checkEglError("eglQuerySurface");
    GLint dim = w < h ? w : h;

    fprintf(stderr, "Window dimensions: %d x %d\n", w, h);

    printGLString("Version", GL_VERSION);
    printGLString("Vendor", GL_VENDOR);
    printGLString("Renderer", GL_RENDERER);
    printGLString("Extensions", GL_EXTENSIONS);

    if(!setupYuvTexSurface(dpy, context, argv[1])) {
        fprintf(stderr, "Could not set up texture surface.\n");
        return 1;
    }

    if(!setupGraphics(w, h)) {
        fprintf(stderr, "Could not set up graphics.\n");
        return 1;
    }

    for (;;) {
        static int dir = -1;

        renderFrame(yuvTexWidth, yuvTexHeight);
        eglSwapBuffers(dpy, surface);
        checkEglError("eglSwapBuffers");
    }

    return 0;
}
