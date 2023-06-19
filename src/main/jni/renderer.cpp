//
// Copyright 2011 Tero Saarni
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
//

#include <stdint.h>
#include <unistd.h>
#include <pthread.h>
#include <cstring>
#include <string>
#include <android/native_window.h> // requires ndk r5 or newer
#include <EGL/egl.h> // requires ndk r5 or newer
#include <GLES3/gl32.h>

#include "logger.h"
#include "renderer.h"

#define LOG_TAG "EglSample"

static float vertices[] = {
    -1.0, -1.0, -1.0,    +0.0, +0.0, +0.0, +1.0,
    +1.0, -1.0, -1.0,    +1.0, +0.0, +0.0, +1.0,
    +1.0, +1.0, -1.0,    +1.0, +1.0, +0.0, +1.0,
    -1.0, +1.0, -1.0,    +0.0, +1.0, +0.0, +1.0,
    -1.0, -1.0, +1.0,    +0.0, +0.0, +1.0, +1.0,
    +1.0, -1.0, +1.0,    +1.0, +0.0, +1.0, +1.0,
    +1.0, +1.0, +1.0,    +1.0, +1.0, +1.0, +1.0,
    -1.0, +1.0, +1.0,    +0.0, +1.0, +1.0, +1.0
};

static GLubyte indices[] = {
    0, 4, 5,    0, 5, 1,
    1, 5, 6,    1, 6, 2,
    2, 6, 7,    2, 7, 3,
    3, 7, 4,    3, 4, 0,
    4, 7, 6,    4, 6, 5,
    3, 0, 1,    3, 1, 2
};

Renderer::Renderer()
    : _msg(MSG_NONE), _display(0), _surface(0), _context(0), _angle(0), _inited(false)
{
    LOG_INFO("Renderer instance created");
    pthread_mutex_init(&_mutex, 0);    
    return;
}

Renderer::~Renderer()
{
    LOG_INFO("Renderer instance destroyed");
    pthread_mutex_destroy(&_mutex);
    return;
}

void Renderer::start()
{
    LOG_INFO("Creating renderer thread");
    pthread_create(&_threadId, 0, threadStartCallback, this);
    return;
}

void Renderer::stop()
{
    LOG_INFO("Stopping renderer thread");

    // send message to render thread to stop rendering
    pthread_mutex_lock(&_mutex);
    _msg = MSG_RENDER_LOOP_EXIT;
    pthread_mutex_unlock(&_mutex);    

    pthread_join(_threadId, 0);
    LOG_INFO("Renderer thread stopped");

    return;
}

void Renderer::setWindow(ANativeWindow *window)
{
    // notify render thread that window has changed
    pthread_mutex_lock(&_mutex);
    _msg = MSG_WINDOW_SET;
    _window = window;
    pthread_mutex_unlock(&_mutex);

    return;
}



void Renderer::renderLoop()
{
    bool renderingEnabled = true;
    
    LOG_INFO("renderLoop()");

    while (renderingEnabled) {

        pthread_mutex_lock(&_mutex);

        // process incoming messages
        switch (_msg) {

            case MSG_WINDOW_SET:
                initialize();
                break;

            case MSG_RENDER_LOOP_EXIT:
                renderingEnabled = false;
                destroy();
                break;

            default:
                break;
        }
        _msg = MSG_NONE;
        
        if (_display) {
            drawFrame();
            if (!eglSwapBuffers(_display, _surface)) {
                LOG_ERROR("eglSwapBuffers() returned error %d", eglGetError());
            }
        }
        
        pthread_mutex_unlock(&_mutex);
    }
    
    LOG_INFO("Render loop exits");
    
    return;
}

bool Renderer::initialize()
{
    const EGLint api_version = EGL_OPENGL_ES3_BIT;
    const EGLint attribs[] = {
        EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
        EGL_BLUE_SIZE, 8,
        EGL_GREEN_SIZE, 8,
        EGL_RED_SIZE, 8,
        EGL_RENDERABLE_TYPE, api_version,
        EGL_CONFORMANT, api_version,
        EGL_NONE
    };
    EGLDisplay display;
    EGLConfig config;    
    EGLint numConfigs;
    EGLint format;
    EGLSurface surface;
    EGLContext context;
    EGLint width;
    EGLint height;
    // GLfloat ratio;

    LOG_INFO("Initializing context");
    
    if ((display = eglGetDisplay(EGL_DEFAULT_DISPLAY)) == EGL_NO_DISPLAY) {
        LOG_ERROR("eglGetDisplay() returned error %d", eglGetError());
        return false;
    }
    if (!eglInitialize(display, 0, 0)) {
        LOG_ERROR("eglInitialize() returned error %d", eglGetError());
        return false;
    }

    if (!eglChooseConfig(display, attribs, &config, 1, &numConfigs)) {
        LOG_ERROR("eglChooseConfig() returned error %d", eglGetError());
        destroy();
        return false;
    }

    if (!eglGetConfigAttrib(display, config, EGL_NATIVE_VISUAL_ID, &format)) {
        LOG_ERROR("eglGetConfigAttrib() returned error %d", eglGetError());
        destroy();
        return false;
    }

    ANativeWindow_setBuffersGeometry(_window, 0, 0, format);

    if (!(surface = eglCreateWindowSurface(display, config, _window, 0))) {
        LOG_ERROR("eglCreateWindowSurface() returned error %d", eglGetError());
        destroy();
        return false;
    }

    EGLint contextAttributes[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
    if (!(context = eglCreateContext(display, config, EGL_NO_CONTEXT, contextAttributes))) {
        LOG_ERROR("eglCreateContext() returned error %d", eglGetError());
        destroy();
        return false;
    }
    
    if (!eglMakeCurrent(display, surface, surface, context)) {
        LOG_ERROR("eglMakeCurrent() returned error %d", eglGetError());
        destroy();
        return false;
    }

    if (!eglQuerySurface(display, surface, EGL_WIDTH, &width) ||
        !eglQuerySurface(display, surface, EGL_HEIGHT, &height)) {
        LOG_ERROR("eglQuerySurface() returned error %d", eglGetError());
        destroy();
        return false;
    }

    _display = display;
    _surface = surface;
    _context = context;

    glDisable(GL_DITHER);
    glClearColor(0, 0, 0, 0);
    glDisable(GL_CULL_FACE);
    glDisable(GL_DEPTH_TEST);
    
    glViewport(0, 0, width, height);

    return true;
}

void Renderer::destroy() {
    LOG_INFO("Destroying context");

    eglMakeCurrent(_display, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
    eglDestroyContext(_display, _context);
    eglDestroySurface(_display, _surface);
    eglTerminate(_display);
    
    _display = EGL_NO_DISPLAY;
    _surface = EGL_NO_SURFACE;
    _context = EGL_NO_CONTEXT;

    return;
}

#define CHECKERROR                              \
    err = glGetError();                         \
    if (err) {                                  \
        LOG_INFO("glError %d @%d", err, __LINE__);      \
    }

static inline GLint createProgram(const char* name, const char* vshader, const char* fshader)
{
    auto prog = glCreateProgram();
    GLint cstatus, lstatus;
    GLenum err;

    auto vs = glCreateShader(GL_VERTEX_SHADER);
    GLint vshaderlen = strlen(vshader);
    glShaderSource(vs, 1, &vshader, &vshaderlen);
    glCompileShader(vs);
    glGetShaderiv(vs, GL_COMPILE_STATUS, &cstatus);
    LOG_INFO("vs compile status for %s 0x%x", name, cstatus);
    if (cstatus == 0) {
        // error
        GLint maxLength = 0;
        glGetShaderiv(vs, GL_INFO_LOG_LENGTH, &maxLength);
        std::string err;
        err.resize(maxLength - 1); // subtract '\0'
        glGetShaderInfoLog(vs, maxLength, &maxLength, err.data());
        LOG_INFO("vs error '%s'", err.c_str());
    }
    glAttachShader(prog, vs);
    glDeleteShader(vs);

    auto fs = glCreateShader(GL_FRAGMENT_SHADER);
    GLint fshaderlen = strlen(fshader);
    glShaderSource(fs, 1, &fshader, &fshaderlen);
    glCompileShader(fs);
    glGetShaderiv(fs, GL_COMPILE_STATUS, &cstatus);
    LOG_INFO("fs compile status for %s 0x%x", name, cstatus);
    if (cstatus == 0) {
        // error
        GLint maxLength = 0;
        glGetShaderiv(fs, GL_INFO_LOG_LENGTH, &maxLength);
        std::string err;
        err.resize(maxLength - 1); // subtract '\0'
        glGetShaderInfoLog(fs, maxLength, &maxLength, err.data());
        LOG_INFO("fs error '%s'", err.c_str());
    }
    glAttachShader(prog, fs);
    glDeleteShader(fs);

    glLinkProgram(prog);
    glGetProgramiv(prog, GL_LINK_STATUS, &lstatus);
    LOG_INFO("link status for %s 0x%x", name, lstatus);

    return prog;
}

void Renderer::drawFrame()
{
    GLint err;
    if (!_inited) {
        _inited = true;

        const char* vshader =
            "#version 320 es\n"
            "#pragma shader_stage(vertex)\n"
            "layout(location=0) in vec3 inPosition;\n"
            "layout(location=1) in vec4 color;\n"
            "layout(location=0) out vec4 fragColor;\n"
            "void main() {\n"
            "    gl_Position = vec4(inPosition, 1.0);\n"
            "    fragColor = color;\n"
            "}\n";

        const char* fshader =
        "#version 320 es\n"
        "#pragma shader_stage(fragment)\n"
        "precision highp float;\n"
        "precision highp int;\n"
        "layout(location=0) in vec4 fragColor;\n"
        "layout(location=0) out vec4 outColor;\n"
        "void main() {\n"
        "    outColor = fragColor;\n"
        "}\n";

        _program = createProgram("render", vshader, fshader);
        glGenBuffers(2, _buffers);
        glBindBuffer(GL_ARRAY_BUFFER, _buffers[0]);
        glBufferData(GL_ARRAY_BUFFER, sizeof(vertices), vertices, GL_STATIC_DRAW);
        glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _buffers[1]);
        glBufferData(GL_ELEMENT_ARRAY_BUFFER, sizeof(indices), indices, GL_STATIC_DRAW);
        glUseProgram(_program);
        CHECKERROR;
    }

    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glBindBuffer(GL_ARRAY_BUFFER, _buffers[0]);
    glEnableVertexAttribArray(0);
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, 28, reinterpret_cast<const void*>(0));
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, 28, reinterpret_cast<const void*>(12));
    glBindBuffer(GL_ELEMENT_ARRAY_BUFFER, _buffers[1]);

    glFrontFace(GL_CW);
    // glUniform1f(0, _angle);
    glDrawElements(GL_TRIANGLES, 36, GL_UNSIGNED_BYTE, nullptr);
    CHECKERROR;

    _angle += 1.2f;    
}

void* Renderer::threadStartCallback(void *myself)
{
    Renderer *renderer = (Renderer*)myself;

    renderer->renderLoop();
    pthread_exit(0);
    
    return 0;
}

