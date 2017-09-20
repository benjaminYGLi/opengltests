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

using namespace android;

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

bool setupGraphicsRGBA(int w, int h, char *fileName) {

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

void renderFrameRGBA() {

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
