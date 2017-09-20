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

#define BMP_HEADER_SIZE 54
#define BMP_HEADER_MAGIC_0 0x42
#define BMP_HEADER_MAGIC_1 0x4d

using namespace android;

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

GLuint loadShader(GLenum shaderType, const char* pSource) {
    GLuint shader = glCreateShader(shaderType);
    if (shader) {
        glShaderSource(shader, 1, &pSource, NULL);
        glCompileShader(shader);
        GLint compiled = 0;
        glGetShaderiv(shader, GL_COMPILE_STATUS, &compiled);
        if (!compiled) {
            GLint infoLen = 0;
            glGetShaderiv(shader, GL_INFO_LOG_LENGTH, &infoLen);
            if (infoLen) {
                char* buf = (char*) malloc(infoLen);
                if (buf) {
                    glGetShaderInfoLog(shader, infoLen, NULL, buf);
                    fprintf(stderr, "Could not compile shader %d:\n%s\n",
                            shaderType, buf);
                    free(buf);
                }
                glDeleteShader(shader);
                shader = 0;
            }
        }
    }
    return shader;
}

GLuint createProgram(const char* pVertexSource, const char* pFragmentSource) {
    GLuint vertexShader = loadShader(GL_VERTEX_SHADER, pVertexSource);
    if (!vertexShader) {
        return 0;
    }

    GLuint pixelShader = loadShader(GL_FRAGMENT_SHADER, pFragmentSource);
    if (!pixelShader) {
        return 0;
    }

    GLuint program = glCreateProgram();
    if (program) {
        glAttachShader(program, vertexShader);
        checkGlError("glAttachShader");
        glAttachShader(program, pixelShader);
        checkGlError("glAttachShader");
        glLinkProgram(program);
        GLint linkStatus = GL_FALSE;
        glGetProgramiv(program, GL_LINK_STATUS, &linkStatus);
        if (linkStatus != GL_TRUE) {
            GLint bufLength = 0;
            glGetProgramiv(program, GL_INFO_LOG_LENGTH, &bufLength);
            if (bufLength) {
                char* buf = (char*) malloc(bufLength);
                if (buf) {
                    glGetProgramInfoLog(program, bufLength, NULL, buf);
                    fprintf(stderr, "Could not link program:\n%s\n", buf);
                    free(buf);
                }
            }
            glDeleteProgram(program);
            program = 0;
        }
    }
    return program;
}

GLuint gTextureProgram;
GLuint gvPositionHandle;
GLuint gvTexturePositionHandle;
GLuint gvTextureTexCoordsHandle;
GLuint gvTextureSamplerHandle;
GLuint gBufferTexture;

static const char gSimpleVS[] =
    "attribute vec4 position;\n"
    "attribute vec2 texCoords;\n"
    "varying vec2 outTexCoords;\n"
    "\nvoid main(void) {\n"
    "    gl_Position = position;\n"
    "    outTexCoords = texCoords;\n"
    "}\n\n";

static const char gSimpleFS[] =
    "precision mediump float;\n\n"
    "varying vec2 outTexCoords;\n"
    "uniform sampler2D texture;\n"
    "\nvoid main(void) {\n"
    "    gl_FragColor = texture2D(texture, outTexCoords);\n"
    "}\n\n";

bool setupGraphics(int w, int h, char *fileName) {

    gTextureProgram = createProgram(gSimpleVS, gSimpleFS);
    if (!gTextureProgram) {
        return false;
    }

    gvTexturePositionHandle = glGetAttribLocation(gTextureProgram, "position");
    checkGlError("glGetAttribLocation");
    gvTextureTexCoordsHandle = glGetAttribLocation(gTextureProgram, "texCoords");
    checkGlError("glGetAttribLocation");
    gvTextureSamplerHandle = glGetUniformLocation(gTextureProgram, "texture");
    checkGlError("glGetAttribLocation");

    glActiveTexture(GL_TEXTURE0);

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

    glGenTextures(1, &gBufferTexture);
    glBindTexture(GL_TEXTURE_2D, gBufferTexture);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, mW, mH, 0, GL_RGBA, GL_UNSIGNED_BYTE, t32my);
    free(t32my);

    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    glViewport(0, 0, w, h);
    checkGlError("glViewport");
    return true;
}

const GLint FLOAT_SIZE_BYTES = 4;
const GLint TRIANGLE_VERTICES_DATA_STRIDE_BYTES = 5 * FLOAT_SIZE_BYTES;

void renderFrame() {

    // set tri-angle vertices data
    GLfloat gTriangleVerticesData[] = {
         // X, Y, Z, U, V
         -1.0f,  1.0f, 0.0f, 0.0f, 0.0f,
          1.0f,  1.0f, 0.0f, 1.0f, 0.0f,
         -1.0f, -1.0f, 0.0f, 0.0f, 1.0f,
          1.0f, -1.0f, 0.0f, 1.0f, 1.0f,
    };

    glClearColor(0.0f, 0.0f, 0.0f, 0.0f);
    checkGlError("glClearColor");
    glClear(GL_DEPTH_BUFFER_BIT | GL_COLOR_BUFFER_BIT);
    checkGlError("glClear");

    // bind texture
    glBindTexture(GL_TEXTURE_2D, gBufferTexture);
    checkGlError("glBindTexture");

    // Draw copied content on the screen
    glUseProgram(gTextureProgram);
    checkGlError("glUseProgram");

    glEnable(GL_BLEND);
    glBlendFunc(GL_ONE, GL_ONE_MINUS_SRC_ALPHA);

    // Set the shader program
    glVertexAttribPointer(gvTexturePositionHandle, 3, GL_FLOAT, GL_FALSE,
            TRIANGLE_VERTICES_DATA_STRIDE_BYTES, gTriangleVerticesData);
    checkGlError("glVertexAttribPointer");

    glVertexAttribPointer(gvTextureTexCoordsHandle, 2, GL_FLOAT, GL_FALSE,
            TRIANGLE_VERTICES_DATA_STRIDE_BYTES, &gTriangleVerticesData[3]);
    checkGlError("glVertexAttribPointer");

    glEnableVertexAttribArray(gvTexturePositionHandle);
    checkGlError("glEnableVertexAttribArray");
    glEnableVertexAttribArray(gvTextureTexCoordsHandle);
    checkGlError("glEnableVertexAttribArray");

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    checkGlError("glDrawArrays");

    glUseProgram(0);
    checkGlError("glUseProgram");
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

int printEGLConfigurations(EGLDisplay dpy) {
    EGLint numConfig = 0;
    EGLint returnVal = eglGetConfigs(dpy, NULL, 0, &numConfig);
    checkEglError("eglGetConfigs", returnVal);
    if (!returnVal) {
        return false;
    }

    printf("Number of EGL configuration: %d\n", numConfig);

    EGLConfig* configs = (EGLConfig*) malloc(sizeof(EGLConfig) * numConfig);
    if (! configs) {
        printf("Could not allocate configs.\n");
        return false;
    }

    returnVal = eglGetConfigs(dpy, configs, numConfig, &numConfig);
    checkEglError("eglGetConfigs", returnVal);
    if (!returnVal) {
        free(configs);
        return false;
    }

    for(int i = 0; i < numConfig; i++) {
        printf("Configuration %d\n", i);
        printEGLConfiguration(dpy, configs[i]);
    }

    free(configs);
    return true;
}

int main(int argc, char** argv) {
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

    if(!setupGraphics(w, h, argv[1])) {
        fprintf(stderr, "Could not set up graphics.\n");
        return 0;
    }

    for (;;) {
        renderFrame();
        eglSwapBuffers(dpy, surface);
        checkEglError("eglSwapBuffers");
    }

    return 0;
}
