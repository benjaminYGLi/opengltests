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
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include <utils/Timers.h>

#include <WindowSurface.h>
#include <EGLUtils.h>

#include "common.h"

using namespace android;

//--------------------------------------------------------------------------------------
// Name: g_strVertexShader / g_strFragmentShader
// Desc: The vertex and fragment shader programs
//--------------------------------------------------------------------------------------
static const char* g_strVertexShader =
    "attribute vec4 g_vPosition;\n"
    "attribute vec3 g_vColor;\n"
    "attribute vec2 g_vTexCoord;\n"
    "\n"
    "varying   vec3 g_vVSColor;\n"
    "varying   vec2 g_vVSTexCoord;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    gl_Position  = g_vPosition;\n"
    "    g_vVSColor = g_vColor;\n"
    "    g_vVSTexCoord = g_vTexCoord;\n"
    "}\n";


static const char* g_strFragmentShader =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "   precision highp float;\n"
    "#else\n"
    "   precision mediump float;\n"
    "#endif\n"
    "\n"
    "uniform   sampler2D s_texture;\n"
    "varying   vec3      g_vVSColor;\n"
    "varying   vec2      g_vVSTexCoord;\n"
    "\n"
    "void main()\n"
    "{\n"
    "    gl_FragColor = texture2D(s_texture,g_vVSTexCoord);\n"
    "}\n";

bool setupGraphicsRGB(int w, int h, char *fileName) {
    if (setupYuvTexSurface(fileName) == false)
       return false;

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
GLuint gProgram;

bool setupYuvTexSurface(char *fileName) {
    // parse the BMP header
    unsigned char header[BMP_HEADER_SIZE];
    FILE* fp = fopen(fileName, "rb");
    if (fp) {
        fread (header, 1, BMP_HEADER_SIZE, fp);
    } else {
        fprintf(stderr, "unable to open BMP file:%s, err:%s\n", fileName, strerror(errno));
        return false;
    }

    // check the BMP magic words
    if ((BMP_HEADER_MAGIC_0 != header[0]) || (BMP_HEADER_MAGIC_1 != header[1])) {
        fprintf(stderr, "%s is not valid BMP file!\n", fileName);
        fclose(fp);
        return false;
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
    if ((3 != mF) && (4 != mF)) {
        fprintf(stderr, "%s is wrong with bits per pixel: %d!\n", fileName, mF);
        fclose(fp);
        return false;
    }

    // check the picture size
    int imageSize = mW * mH * mF;
    if (mS != (imageSize + BMP_HEADER_SIZE)) {
        fprintf(stderr, "%s is wrong with file size!\n", fileName);
        fclose(fp);
        return false;
    }

    yuvTexWidth = mW;
    yuvTexHeight = mH;

    // allocate memory space for storing pixels
    uint8_t *t8my = (uint8_t *) malloc(imageSize);
    fseek(fp, BMP_HEADER_SIZE, SEEK_SET);
    fread(t8my, 1, imageSize, fp);
    fclose(fp);

    //loop image data,swaps R B.
    uint8_t temp;
    for (int i = 0 ; i < imageSize; i += 3){
       temp      = t8my[i];
       t8my[i]   = t8my[i+2];
       t8my[i+2] = temp;
    }

    // loader the program
    gProgram = createProgram(g_strVertexShader, g_strFragmentShader);
    if (!gProgram) {
        return false;
    }

    // Finally, use the program
    glUseProgram(gProgram);
    checkGlError("glUseProgram");
    // Switch back to no shader
    glUseProgram(0);
    checkGlError("glUseProgram");

    // create GL texture object
    glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
    checkGlError("glPixelStorei");
    glEnable(GL_TEXTURE_2D);
    checkGlError("glEnable");

    // generate texture
    glGenTextures(1, &yuvTex);
    checkGlError("glGenTextures");
    glBindTexture(GL_TEXTURE_2D, yuvTex);
    checkGlError("glBindTexture");
    glTexImage2D(GL_TEXTURE_2D,
                 0,
                 GL_RGB,
                 mW,
                 mH,
                 0,
                 GL_RGB,
                 GL_UNSIGNED_BYTE,
                 t8my);
    free(t8my);

    // set texture parameters
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    checkGlError("glTexParameterf");
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    checkGlError("glTexParameterf");
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
    checkGlError("glTexParameterf");
    glTexParameterf(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
    checkGlError("glTexParameterf");

    return true;
}

GLuint       g_hVertexLoc_RGB          = 0;
GLuint       g_hVertexTexLoc_RGB       = 2;
GLuint       g_hColorLoc_RGB           = 1;

void renderFrameRGB() {
#if 0
    GLfloat vtx[] = {
          -1.0f, 1.0f, 0.0f,
           1.0f, 1.0f, 0.0f,
          -1.0f,-1.0f, 0.0f,
           1.0f,-1.0f, 0.0f
    };
#endif
    GLfloat vtx[] = {
    // X, Y, Z,
          -1.0f, -1.0f, 0.0f,
           1.0f, -1.0f, 0.0f,
          -1.0f,  1.0f, 0.0f,
           1.0f,  1.0f, 0.0f,
    };

    GLfloat tex[] = {
          0.0f, 0.0f,
          1.0f, 0.0f,
          0.0f, 1.0f,
          1.0f, 1.0f
    };
    GLfloat color[] = {
         1.0f, 0.0f, 0.0f,
         0.0f, 1.0f, 0.0f,
         0.0f, 0.0f, 1.0f,
         1.0f, 1.0f, 0.0f
    };

    glUseProgram(gProgram);
    checkGlError("glUseProgram");

#if 0
    glEnable(GL_TEXTURE_2D);
    checkGlError("glEnable");
#endif

    // Set the shader program
    glBindAttribLocation(gProgram, g_hVertexLoc_RGB, "g_vPosition");
    checkGlError("glBindAttribLocation");
    glBindAttribLocation(gProgram, g_hColorLoc_RGB, "g_vColor");
    checkGlError("glBindAttribLocation");
    glBindAttribLocation(gProgram, g_hVertexTexLoc_RGB, "g_vTexCoord");
    checkGlError("glBindAttribLocation");

    glVertexAttribPointer( g_hVertexLoc_RGB, 3, GL_FLOAT, 0, 0, vtx );
    checkGlError("glVertexAttribPointer");
    glEnableVertexAttribArray( g_hVertexLoc_RGB );
    checkGlError("glEnableVertexAttribArray");

    glVertexAttribPointer( g_hVertexTexLoc_RGB, 2, GL_FLOAT, 0, 0, tex );
    checkGlError("glVertexAttribPointer");
    glEnableVertexAttribArray( g_hVertexTexLoc_RGB );
    checkGlError("glEnableVertexAttribArray");

    glVertexAttribPointer( g_hColorLoc_RGB, 3, GL_FLOAT, 0, 0, color );
    checkGlError("glVertexAttribPointer");
    glEnableVertexAttribArray( g_hColorLoc_RGB );
    checkGlError("glEnableVertexAttribArray");

    glActiveTexture(GL_TEXTURE0);
    checkGlError("glActiveTexture");
    glBindTexture(GL_TEXTURE_2D, yuvTex);
    checkGlError("glBindTexture");
    int texBind = glGetUniformLocation(gProgram, "s_texture");
    checkGlError("glGetUniformLocation");
    glUniform1i(texBind, 0);  /* Bind yuvtex to texture unit 0 */
    checkGlError("glUniform1i");
    /* Drawing Using Triangle strips, draw triangle strips using 4 vertices */
    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
    checkGlError("glDrawArrays");
    glDisableVertexAttribArray(g_hVertexLoc_RGB);
    checkGlError("glDisableVertexAttribArray");
    glDisableVertexAttribArray(g_hColorLoc_RGB);
    checkGlError("glDisableVertexAttribArray");
    glDisableVertexAttribArray(g_hVertexTexLoc_RGB);
    checkGlError("glDisableVertexAttribArray");
    glUseProgram(0);
    checkGlError("glUseProgram");

}
