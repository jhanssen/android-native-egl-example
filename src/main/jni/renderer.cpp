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
#include <cstdio>
#include <cstring>
#include <string>
#include <android/native_window.h> // requires ndk r5 or newer
#include <EGL/egl.h> // requires ndk r5 or newer
#include <GLES3/gl32.h>
#include <glm/mat4x4.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include "logger.h"
#include "renderer.h"

#define LOG_TAG "EglSample"
#define CHECKERROR                                      \
    err = glGetError();                                 \
    if (err) {                                          \
        LOG_INFO("glError %d @%d", err, __LINE__);      \
    }

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

void glDebugCallback(GLenum source, GLenum type, GLuint, GLenum severity,
                     GLsizei debugMessageLength, const GLchar *debugMessage, const void *)
{
    if(source == GL_DEBUG_SOURCE_APPLICATION)
        return;

    bool error = false;
    std::string severity_str;
    switch(severity) {
    case GL_DEBUG_SEVERITY_HIGH:
        severity_str = "GL_DEBUG_SEVERITY_HIGH";
        break;
    case GL_DEBUG_SEVERITY_MEDIUM:
        severity_str = "GL_DEBUG_SEVERITY_MEDIUM";
        break;
    case GL_DEBUG_SEVERITY_LOW:
        severity_str = "GL_DEBUG_SEVERITY_LOW";
        break;
    case GL_DEBUG_SEVERITY_NOTIFICATION:
        severity_str = "GL_DEBUG_SEVERITY_NOTIFICATION";
        break;
    }

    std::string type_str;
    switch(type) {
    case GL_DEBUG_TYPE_ERROR:
        type_str = "GL_DEBUG_TYPE_ERROR";
        error = true;
        break;
    case GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR:
        type_str = "GL_DEBUG_TYPE_DEPRECATED_BEHAVIOR";
        error = true;
        break;
    case GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR:
        type_str = "GL_DEBUG_TYPE_UNDEFINED_BEHAVIOR";
        error = true;
        break;
    case GL_DEBUG_TYPE_PORTABILITY:
        type_str = "GL_DEBUG_TYPE_PORTABILITY";
        break;
    case GL_DEBUG_TYPE_PERFORMANCE:
        type_str = "GL_DEBUG_TYPE_PERFORMANCE";
        break;
    case GL_DEBUG_TYPE_MARKER:
        type_str = "GL_DEBUG_TYPE_MARKER";
        break;
    case GL_DEBUG_TYPE_PUSH_GROUP:
        type_str = "GL_DEBUG_TYPE_PUSH_GROUP";
        break;
    case GL_DEBUG_TYPE_POP_GROUP:
        type_str = "GL_DEBUG_TYPE_POP_GROUP";
        break;
    case GL_DEBUG_TYPE_OTHER:
        type_str = "GL_DEBUG_TYPE_OTHER";
        break;
    }

    std::string messageSuffix;
    if(!severity_str.empty())
        messageSuffix += " " + severity_str;
    if(!type_str.empty())
        messageSuffix += " (" + type_str + ")";

    const std::string msg = std::string(debugMessage, debugMessageLength) + messageSuffix;
    LOG_INFO("GL MESSAGE %s\n", msg.c_str());
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

Renderer::Renderer()
    : _msg(MSG_NONE), _display(0), _surface(0), _context(0), _angle(0), _ratio(0)
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
    _ratio = (GLfloat) width / height;

    glEnable(GL_DEBUG_OUTPUT);
    glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
    glDebugMessageCallback(glDebugCallback, nullptr);
    GLuint unusedIds = 0;
    glDebugMessageControl(GL_DONT_CARE, GL_DONT_CARE, GL_DONT_CARE, 0, &unusedIds, GL_TRUE);

    glDisable(GL_DITHER);
    glClearColor(0, 0, 0, 0);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);
    
    glViewport(0, 0, width, height);

    GLuint tex;
    glGenTextures(1, &tex);
    glBindTexture(GL_TEXTURE_2D, tex);
    glTexStorage2D(GL_TEXTURE_2D, 4, GL_R8, 32, 32);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
    glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);

    GLuint fbo;
    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glColorMaski(0, GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    const GLfloat clearColor[] = { 0.5f, 0.0f, 0.0f, 0.0f };
    glClearBufferfv(GL_COLOR, 0, clearColor);
    glBindFramebuffer(GL_FRAMEBUFFER, 0);
    glDeleteFramebuffers(1, &fbo);

    GLuint buf;
    glGenBuffers(1, &buf);
    glBindBuffer(GL_ARRAY_BUFFER, buf);
    glBufferData(GL_ARRAY_BUFFER, 7968, nullptr, GL_DYNAMIC_COPY);

    glGenFramebuffers(1, &fbo);
    glBindFramebuffer(GL_FRAMEBUFFER, fbo);
    glPixelStorei(GL_PACK_ROW_LENGTH, 256);
    glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, tex, 0);
    glReadBuffer(GL_COLOR_ATTACHMENT0);
    glReadPixels(0, 0, 32, 32, GL_RED, GL_UNSIGNED_BYTE, nullptr);
    glPixelStorei(GL_PACK_ROW_LENGTH, 0);

    GLint err;
    CHECKERROR;

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

void Renderer::drawFrame()
{
}

void* Renderer::threadStartCallback(void *myself)
{
    Renderer *renderer = (Renderer*)myself;

    renderer->renderLoop();
    pthread_exit(0);
    
    return 0;
}

