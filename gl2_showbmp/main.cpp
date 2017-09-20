/*
 * Copyright (C) 2007 The Android Open Source Project
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
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <utils/Timers.h>

#include <WindowSurface.h>
#include <EGLUtils.h>

#include "common.h"
#include "TimeUtils.h"

typedef struct tagTextureDrawer {
    bool (*setupGraphics)(int, int, char*);
    void (*renderFrame)();
}TextureDrawer;

using namespace android;

int main(int argc, char** argv) {
    //parse the input parameters
    if (argc <= 1) {
        fprintf(stderr, "glDrawBMP bmpFile {RGB/RGBA}, please assign a bmp file!\n");
        return -1;
    }

    // select the program mode
    int mode = RGBA_MODE;
    if (argc == 3) {
        if (strcmp(argv[2], "RGBA") == 0)
            mode = RGBA_MODE;
        else if (strcmp(argv[2], "RGB") == 0)
            mode = RGB_MODE;
    }

    int m_statsNumFrames = 0;
    long long m_statsStartTime = 0.0f;

    EGLBoolean returnValue;
    EGLConfig myConfig = {0};

    EGLint context_attribs[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint s_configAttribs[] = {
            EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
            EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
            EGL_RED_SIZE, 8,
            EGL_GREEN_SIZE, 8,
            EGL_BLUE_SIZE, 8,
            EGL_ALPHA_SIZE, 8,
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

    if (!printEGLConfigurations(dpy)) {
        printf("printEGLConfigurations failed\n");
        return 0;
    }

    checkEglError("printEGLConfigurations");

    WindowSurface windowSurface;
    EGLNativeWindowType window = windowSurface.getSurface();
    EGLint numConfigs = -1, n = 0;
    eglChooseConfig(dpy, s_configAttribs, 0, 0, &numConfigs);
    if (numConfigs) {
        EGLConfig* const configs = new EGLConfig[numConfigs];
        eglChooseConfig(dpy, s_configAttribs, configs, numConfigs, &n);
        myConfig = configs[0];
        delete[] configs;
    }

    checkEglError("EGLUtils::selectConfigForNativeWindow");

    printf("Chose this configuration:\n");
    printEGLConfiguration(dpy, myConfig);

    surface = eglCreateWindowSurface(dpy, myConfig, window, NULL);
    checkEglError("eglCreateWindowSurface");
    if (surface == EGL_NO_SURFACE) {
        printf("gelCreateWindowSurface failed.\n");
        return 0;
    }

    context = eglCreateContext(dpy, myConfig, EGL_NO_CONTEXT, context_attribs);
    checkEglError("eglCreateContext");
    if (context == EGL_NO_CONTEXT) {
        printf("eglCreateContext failed\n");
        return 0;
    }
    returnValue = eglMakeCurrent(dpy, surface, surface, context);
    checkEglError("eglMakeCurrent", returnValue);
    if (returnValue != EGL_TRUE) {
        return 0;
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

    // file texture drawer
    TextureDrawer texDrawer[2];
    texDrawer[RGB_MODE].setupGraphics = setupGraphicsRGB;
    texDrawer[RGB_MODE].renderFrame = renderFrameRGB;

    texDrawer[RGBA_MODE].setupGraphics = setupGraphicsRGBA;
    texDrawer[RGBA_MODE].renderFrame = renderFrameRGBA;

    // start to execute the texture drawer
    {
        if (!(*texDrawer[mode].setupGraphics)(w, h, argv[1])) {
            fprintf(stderr, "Could not set up graphics.\n");
            return 1;
        }

        for (;;) {
            (*texDrawer[mode].renderFrame)();
            eglSwapBuffers(dpy, surface);
            checkEglError("eglSwapBuffers");
	    long long currTime = GetCurrentTimeMS();
            m_statsNumFrames++;
            if (currTime - m_statsStartTime >= 1000) {
		float dt = (float)(currTime - m_statsStartTime) / 1000.0f;
		printf("FPS: %5.3f\n", (float)m_statsNumFrames / dt);
		m_statsStartTime = currTime;
		m_statsNumFrames = 0;
	    }
        }
    }

    return 0;
}
