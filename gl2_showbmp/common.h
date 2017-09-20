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

#define RGB_MODE  0
#define RGBA_MODE 1

#define BMP_HEADER_SIZE 54
#define BMP_HEADER_TYPE 0x4d42
#define BMP_HEADER_MAGIC_0 0x42
#define BMP_HEADER_MAGIC_1 0x4d

// common functions for EGL and GLES ops
void printGLString(const char *name, GLenum s);
void checkEglError(const char* op, EGLBoolean returnVal = EGL_TRUE);
void checkGlError(const char* op);
GLuint loadShader(GLenum shaderType, const char* pSource);
GLuint createProgram(const char* pVertexSource, const char* pFragmentSource);
void printEGLConfiguration(EGLDisplay dpy, EGLConfig config);
int printEGLConfigurations(EGLDisplay dpy);

// special function for RGB
bool setupYuvTexSurface(char *fileName);

// following is the generic interface for RGB and RGBA
bool setupGraphicsRGBA(int w, int h, char *fileName);
bool setupGraphicsRGB(int w, int h, char *fileName);

void renderFrameRGBA();
void renderFrameRGB();
