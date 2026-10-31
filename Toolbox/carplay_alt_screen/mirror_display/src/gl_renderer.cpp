#include "gl_renderer.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *kVertexShader =
    "attribute vec2 aPosition;\n"
    "attribute vec2 aTexCoord;\n"
    "varying mediump vec2 vTexCoord;\n"
    "void main() {\n"
    "  gl_Position = vec4(aPosition, 0.0, 1.0);\n"
    "  vTexCoord = aTexCoord;\n"
    "}\n";

static const char *kFragmentShader =
    "#ifdef GL_FRAGMENT_PRECISION_HIGH\n"
    "precision highp float;\n"
    "#else\n"
    "precision mediump float;\n"
    "#endif\n"
    "varying mediump vec2 vTexCoord;\n"
    "uniform sampler2D uTexture;\n"
    "uniform float uSwapRB;\n"
    "uniform float uOpacity;\n"
    "uniform sampler2D uChroma;\n"
    "uniform float uNV12;\n"
    "void main() {\n"
    "  vec4 c = texture2D(uTexture, vTexCoord);\n"
    "  if (uNV12 > 0.5) {\n"
    "    vec4 uv = texture2D(uChroma, vTexCoord);\n"
    "    float y = max(c.r - 16.0/255.0, 0.0);\n"
    "    float u = uv.r - 128.0/255.0;\n"
    "    float v = uv.a - 128.0/255.0;\n"
    "    c = vec4(clamp(vec3((298.0*y + 409.0*v)/256.0,\n"
    "                       (298.0*y - 100.0*u - 208.0*v)/256.0,\n"
    "                       (298.0*y + 516.0*u)/256.0), 0.0, 1.0), 1.0);\n"
    "  } else if (uSwapRB > 0.5) c = vec4(c.b, c.g, c.r, c.a);\n"
    "  gl_FragColor = vec4(c.rgb, uOpacity);\n"
    "}\n";

GlRenderer::GlRenderer()
    : program_(0), vertex_shader_(0), fragment_shader_(0), texture_(0), chroma_texture_(0), logo_texture_(0),
      attr_position_(-1), attr_texcoord_(-1), uniform_texture_(-1),
      uniform_swap_rb_(-1), uniform_opacity_(-1), uniform_chroma_(-1), uniform_nv12_(-1),
      texture_format_(0), texture_width_(0), texture_height_(0),
      output_width_(0), output_height_(0), swap_rb_(false), nv12_(false), ready_(false),
      upload_buffer_(0), upload_buffer_bytes_(0) {
    memset(vertices_, 0, sizeof(vertices_));
}

GlRenderer::~GlRenderer() {
    shutdown();
}

bool GlRenderer::compile_shader(GLuint shader, const char *source, const char *name) {
    glShaderSource(shader, 1, &source, 0);
    glCompileShader(shader);
    GLint ok = GL_FALSE;
    glGetShaderiv(shader, GL_COMPILE_STATUS, &ok);
    if (ok == GL_TRUE) return true;

    char log[1024];
    GLsizei n = 0;
    memset(log, 0, sizeof(log));
    glGetShaderInfoLog(shader, sizeof(log) - 1, &n, log);
    fprintf(stderr, "renderer: %s compile failed: %s\n", name, log);
    return false;
}

bool GlRenderer::init(int output_width, int output_height) {
    if (output_width <= 0 || output_height <= 0) return false;

    output_width_ = output_width;
    output_height_ = output_height;

    vertex_shader_ = glCreateShader(GL_VERTEX_SHADER);
    fragment_shader_ = glCreateShader(GL_FRAGMENT_SHADER);
    if (!vertex_shader_ || !fragment_shader_)
        return false;

    if (!compile_shader(vertex_shader_, kVertexShader, "vertex shader") ||
        !compile_shader(fragment_shader_, kFragmentShader, "fragment shader"))
        return false;

    program_ = glCreateProgram();
    glAttachShader(program_, vertex_shader_);
    glAttachShader(program_, fragment_shader_);
    glLinkProgram(program_);

    GLint linked = GL_FALSE;
    glGetProgramiv(program_, GL_LINK_STATUS, &linked);
    if (linked != GL_TRUE) {
        char log[1024];
        GLsizei n = 0;
        memset(log, 0, sizeof(log));
        glGetProgramInfoLog(program_, sizeof(log) - 1, &n, log);
        fprintf(stderr, "renderer: program link failed: %s\n", log);
        return false;
    }

    attr_position_ = glGetAttribLocation(program_, "aPosition");
    attr_texcoord_ = glGetAttribLocation(program_, "aTexCoord");
    uniform_texture_ = glGetUniformLocation(program_, "uTexture");
    uniform_swap_rb_ = glGetUniformLocation(program_, "uSwapRB");
    uniform_opacity_ = glGetUniformLocation(program_, "uOpacity");
    uniform_chroma_ = glGetUniformLocation(program_, "uChroma");
    uniform_nv12_ = glGetUniformLocation(program_, "uNV12");
    if (attr_position_ < 0 || attr_texcoord_ < 0 ||
        uniform_texture_ < 0 || uniform_swap_rb_ < 0 || uniform_opacity_ < 0 ||
        uniform_chroma_ < 0 || uniform_nv12_ < 0) {
        fprintf(stderr, "renderer: shader attribute/uniform lookup failed\n");
        return false;
    }

    glGenTextures(1, &texture_);
    glGenTextures(1, &chroma_texture_);
    const GLuint textures[] = {texture_, chroma_texture_};
    glActiveTexture(GL_TEXTURE0);
    for (unsigned i = 0; i < 2; ++i) {
        glBindTexture(GL_TEXTURE_2D, textures[i]);
        /* Reference renderer: preserve the CPU path's x/2,y/2 UV sampling. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER,
                         i == 0 ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER,
                         i == 0 ? GL_LINEAR : GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
    }
    /* Both samplers must be complete when the RGBA logo is drawn first. */
    const unsigned char neutral_uv[] = {128, 128};
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glTexImage2D(GL_TEXTURE_2D, 0, GL_LUMINANCE_ALPHA, 1, 1, 0,
                 GL_LUMINANCE_ALPHA, GL_UNSIGNED_BYTE, neutral_uv);
    const GLenum texture_error = glGetError();
    if (!texture_ || !chroma_texture_ || texture_error != GL_NO_ERROR) {
        fprintf(stderr, "renderer: texture init GL error=0x%x\n", (unsigned)texture_error);
        return false;
    }

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_BLEND);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glViewport(0, 0, output_width_, output_height_);

    ready_ = true;
    set_fullscreen_destination();
    fprintf(stderr, "renderer: GLES2 renderer initialized output=%dx%d\n",
            output_width_, output_height_);
    const GLubyte *vendor = glGetString(GL_VENDOR);
    const GLubyte *renderer = glGetString(GL_RENDERER);
    const GLubyte *version = glGetString(GL_VERSION);
    fprintf(stderr, "renderer: PHASE=GLES_DRIVER vendor='%s' renderer='%s' version='%s'\n",
            vendor ? (const char *)vendor : "unavailable",
            renderer ? (const char *)renderer : "unavailable",
            version ? (const char *)version : "unavailable");
    return true;
}

bool GlRenderer::ensure_upload_buffer(size_t bytes) {
    if (upload_buffer_ && upload_buffer_bytes_ >= bytes) return true;
    unsigned char *next = (unsigned char *)realloc(upload_buffer_, bytes);
    if (!next) return false;
    upload_buffer_ = next;
    upload_buffer_bytes_ = bytes;
    return true;
}

bool GlRenderer::upload_packed_rgba_bytes(const unsigned char *pixels,
                                          int width, int height,
                                          bool swap_rb) {
    if (!ready_ || !pixels || width <= 0 || height <= 0) return false;

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture_);
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);

    if (width != texture_width_ || height != texture_height_ ||
        texture_format_ != GL_RGBA) {
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                     width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, pixels);
        fprintf(stderr, "renderer: texture allocated %dx%d\n", width, height);
    } else {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0,
                        width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE, pixels);
    }

    GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        texture_format_ = 0;
        fprintf(stderr, "renderer: texture upload GL error=0x%x\n", (unsigned)err);
        return false;
    }

    swap_rb_ = swap_rb;
    nv12_ = false;
    texture_width_ = width;
    texture_height_ = height;
    texture_format_ = GL_RGBA;
    return true;
}

/* ES2 has no core unpack-row-length; padded callers upload rows directly. */
static void upload_plane(GLuint texture, GLenum format, int width, int height,
                         int stride, int row_bytes, const unsigned char *pixels,
                         bool allocate) {
    glBindTexture(GL_TEXTURE_2D, texture);
    if (allocate)
        glTexImage2D(GL_TEXTURE_2D, 0, format, width, height, 0,
                     format, GL_UNSIGNED_BYTE,
                     stride == row_bytes ? pixels : 0);
    if (stride == row_bytes) {
        if (!allocate)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                            format, GL_UNSIGNED_BYTE, pixels);
    } else {
        for (int y = 0; y < height; ++y)
            glTexSubImage2D(GL_TEXTURE_2D, 0, 0, y, width, 1,
                            format, GL_UNSIGNED_BYTE,
                            pixels + (size_t)y * stride);
    }
}

bool GlRenderer::upload_nv12(const VideoFrame &frame) {
    if (!ready_ || !frame.data || frame.width <= 0 || frame.height <= 0 ||
        frame.stride < frame.width || (frame.width & 1) || (frame.height & 1)) {
        return false;
    }

    const bool allocate = texture_format_ != GL_LUMINANCE ||
        texture_width_ != frame.width || texture_height_ != frame.height;
    const unsigned char *uv_plane =
        frame.data + (size_t)frame.stride * (size_t)frame.height;
    glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
    glActiveTexture(GL_TEXTURE0);
    upload_plane(texture_, GL_LUMINANCE, frame.width, frame.height,
                 frame.stride, frame.width, frame.data, allocate);
    glActiveTexture(GL_TEXTURE1);
    /* Interleaved U,V map to LUMINANCE_ALPHA .r,.a without repacking. */
    upload_plane(chroma_texture_, GL_LUMINANCE_ALPHA,
                 frame.width / 2, frame.height / 2,
                 frame.stride, frame.width, uv_plane, allocate);
    glActiveTexture(GL_TEXTURE0);
    const GLenum err = glGetError();
    if (err != GL_NO_ERROR) {
        texture_format_ = 0;
        fprintf(stderr, "renderer: NV12 plane upload GL error=0x%x\n", (unsigned)err);
        return false;
    }
    texture_width_ = frame.width;
    texture_height_ = frame.height;
    texture_format_ = GL_LUMINANCE;
    nv12_ = true;
    swap_rb_ = false;

    static bool reported = false;
    if (!reported) {
        reported = true;
        fprintf(stderr,
                "renderer: PHASE=NV12_CSC_READY backend=gles2-bt601 "
                "input=%dx%d stride=%d output=RGBA8888 cpu_csc=0 planes=Y_UV\n",
                frame.width, frame.height, frame.stride);
    }

    return true;
}

bool GlRenderer::upload_frame(const VideoFrame &frame) {
    if (!ready_ || !frame.data || frame.width <= 0 || frame.height <= 0)
        return false;

    if (frame.format == PIXEL_FORMAT_NV12)
        return upload_nv12(frame);

    if (frame.stride < frame.width * 4)
        return false;

    const bool swap_rb =
        frame.format == PIXEL_FORMAT_BGRA8888 ||
        frame.format == PIXEL_FORMAT_BGRX8888;

    const unsigned char *pixels = frame.data;
    const int packed_stride = frame.width * 4;

    if (frame.stride != packed_stride) {
        const size_t bytes = (size_t)packed_stride * (size_t)frame.height;
        if (!ensure_upload_buffer(bytes)) {
            fprintf(stderr, "renderer: cannot allocate stride-pack buffer (%lu bytes)\n",
                    (unsigned long)bytes);
            return false;
        }
        for (int y = 0; y < frame.height; ++y) {
            memcpy(upload_buffer_ + (size_t)y * packed_stride,
                   frame.data + (size_t)y * frame.stride,
                   (size_t)packed_stride);
        }
        pixels = upload_buffer_;
    }

    return upload_packed_rgba_bytes(pixels, frame.width, frame.height, swap_rb);
}

bool GlRenderer::upload_rgba(const unsigned char *rgba, int width, int height) {
    VideoFrame frame;
    frame.data = rgba;
    frame.width = width;
    frame.height = height;
    frame.stride = width * 4;
    frame.format = PIXEL_FORMAT_RGBA8888;
    return upload_frame(frame);
}

bool GlRenderer::upload_logo_rgba(const unsigned char *rgba,
                                  int width, int height) {
    if (!ready_ || !rgba || width != output_width_ ||
        height != output_height_) return false;
    glActiveTexture(GL_TEXTURE0);
    if (!logo_texture_) {
        glGenTextures(1, &logo_texture_);
        if (!logo_texture_) return false;
        glBindTexture(GL_TEXTURE_2D, logo_texture_);
        /* Texel-for-pixel; nearest keeps every source pixel exact. */
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
        glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, width, height, 0,
                     GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    } else {
        glBindTexture(GL_TEXTURE_2D, logo_texture_);
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, width, height,
                        GL_RGBA, GL_UNSIGNED_BYTE, rgba);
    }
    const GLenum error = glGetError();
    if (error != GL_NO_ERROR) {
        fprintf(stderr, "renderer: startup logo upload failed GL=0x%x\n",
                (unsigned)error);
        return false;
    }
    return true;
}

bool GlRenderer::upload_test_grid(int width, int height) {
    if (width <= 0 || height <= 0) return false;

    const size_t bytes = (size_t)width * (size_t)height * 4u;
    unsigned char *pixels = (unsigned char *)malloc(bytes);
    if (!pixels) return false;

    for (int y = 0; y < height; ++y) {
        for (int x = 0; x < width; ++x) {
            unsigned char r = 0, g = 0, b = 0;
            const int band = (x * 6) / width;
            switch (band) {
                case 0: r = 255; g = 0;   b = 0;   break;
                case 1: r = 255; g = 180; b = 0;   break;
                case 2: r = 0;   g = 220; b = 0;   break;
                case 3: r = 0;   g = 180; b = 255; break;
                case 4: r = 40;  g = 40;  b = 255; break;
                default:r = 200; g = 0;   b = 255; break;
            }

            if ((x % 60) < 2 || (y % 60) < 2) {
                r = g = b = 255;
            }
            if ((x > width / 2 - 5 && x < width / 2 + 5) ||
                (y > height / 2 - 5 && y < height / 2 + 5)) {
                r = g = b = 0;
            }

            const size_t off = ((size_t)y * width + x) * 4u;
            pixels[off + 0] = r;
            pixels[off + 1] = g;
            pixels[off + 2] = b;
            pixels[off + 3] = 255;
        }
    }

    const bool ok = upload_rgba(pixels, width, height);
    free(pixels);
    return ok;
}

bool GlRenderer::set_destination_rect(int x, int y, int width, int height) {
    if (output_width_ <= 0 || output_height_ <= 0 ||
        width <= 0 || height <= 0) {
        return false;
    }

    /*
     * OEM map stages may translate the full-size map canvas partially outside
     * the 1440x455 viewport (Sport + SMALL is -476 px on X).  V3.1 may also
     * pass a 1440x542 destination so the decoded canvas remains 1:1 vertically;
     * GLES clip space naturally discards the 87 rows outside the sink plane.
     * Do not clamp the translation or rescale the destination back to 455.
     */
    const long long right_px = (long long)x + (long long)width;
    const long long bottom_px = (long long)y + (long long)height;
    if (right_px <= 0 || bottom_px <= 0 ||
        x >= output_width_ || y >= output_height_) {
        return false;
    }

    const GLfloat left = -1.0f + 2.0f * (GLfloat)x / (GLfloat)output_width_;
    const GLfloat right = -1.0f + 2.0f * (GLfloat)right_px / (GLfloat)output_width_;
    const GLfloat top = 1.0f - 2.0f * (GLfloat)y / (GLfloat)output_height_;
    const GLfloat bottom = 1.0f - 2.0f * (GLfloat)bottom_px / (GLfloat)output_height_;

    vertices_[0] = left;  vertices_[1] = top;
    vertices_[2] = left;  vertices_[3] = bottom;
    vertices_[4] = right; vertices_[5] = top;
    vertices_[6] = right; vertices_[7] = bottom;

    fprintf(stderr,
            "renderer: destination x=%d y=%d size=%dx%d natural_clip=%d\n",
            x, y, width, height,
            (x < 0 || y < 0 || right_px > output_width_ ||
             bottom_px > output_height_) ? 1 : 0);
    return true;
}

void GlRenderer::set_fullscreen_destination() {
    if (output_width_ > 0 && output_height_ > 0)
        set_destination_rect(0, 0, output_width_, output_height_);
}

void GlRenderer::draw() {
    if (!ready_ || !texture_) return;
    glViewport(0, 0, output_width_, output_height_);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    draw_texture(texture_, vertices_, swap_rb_, 1.0f);
}

void GlRenderer::draw_faded(float opacity) {
    if (!ready_) return;
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    glViewport(0, 0, output_width_, output_height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    if (!texture_ || opacity <= 0.0f) return;
    /* Fade toward opaque black, never toward framebuffer alpha: a transparent
     * displayable3 would expose the OEM gray layers underneath. */
    glEnable(GL_BLEND);
    glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
    draw_texture(texture_, vertices_, swap_rb_, opacity);
    glDisable(GL_BLEND);
}

void GlRenderer::draw_texture(GLuint texture, const GLfloat *vertices,
                              bool swap_rb, float opacity) {

    static const GLfloat texcoords[] = {
        0.0f, 0.0f,
        0.0f, 1.0f,
        1.0f, 0.0f,
        1.0f, 1.0f
    };

    glUseProgram(program_);

    glActiveTexture(GL_TEXTURE0);
    glBindTexture(GL_TEXTURE_2D, texture);
    glUniform1i(uniform_texture_, 0);
    glUniform1f(uniform_swap_rb_, swap_rb ? 1.0f : 0.0f);
    glUniform1f(uniform_opacity_, opacity);
    glActiveTexture(GL_TEXTURE1);
    glBindTexture(GL_TEXTURE_2D, chroma_texture_);
    glUniform1i(uniform_chroma_, 1);
    /* Logo is RGBA even while the underlying map uses NV12 textures. */
    glUniform1f(uniform_nv12_, texture == texture_ && nv12_ ? 1.0f : 0.0f);
    glActiveTexture(GL_TEXTURE0);

    glEnableVertexAttribArray((GLuint)attr_position_);
    glEnableVertexAttribArray((GLuint)attr_texcoord_);
    glVertexAttribPointer((GLuint)attr_position_, 2, GL_FLOAT, GL_FALSE, 0, vertices);
    glVertexAttribPointer((GLuint)attr_texcoord_, 2, GL_FLOAT, GL_FALSE, 0, texcoords);

    glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);

    glDisableVertexAttribArray((GLuint)attr_position_);
    glDisableVertexAttribArray((GLuint)attr_texcoord_);
}

void GlRenderer::draw_logo(float opacity, bool over_carplay) {
    if (!ready_ || !logo_texture_) return;
    /* The 1440x455 logo frame fills the whole 1440x455 plane 1:1, unscaled. */
    static const GLfloat logo_vertices[] = {
        -1.0f,  1.0f, -1.0f, -1.0f,
         1.0f,  1.0f,  1.0f, -1.0f
    };
    if (opacity < 0.0f) opacity = 0.0f;
    if (opacity > 1.0f) opacity = 1.0f;
    glViewport(0, 0, output_width_, output_height_);
    glClearColor(0.0f, 0.0f, 0.0f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT);
    glDisable(GL_BLEND);
    if (over_carplay && texture_width_ && texture_height_) {
        /* Cross-fade the whole plane from the intro to CarPlay. */
        glEnable(GL_BLEND);
        /* Keep the entire displayable opaque, including black intro pixels.
         * Fading framebuffer alpha would expose OEM gray layers underneath. */
        glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA, GL_ZERO, GL_ONE);
        draw_texture(texture_, vertices_, swap_rb_, 1.0f - opacity);
        if (opacity > 0.0f) {
            glBlendFuncSeparate(GL_SRC_ALPHA, GL_ONE, GL_ZERO, GL_ONE);
            draw_texture(logo_texture_, logo_vertices, false, opacity);
        }
        glDisable(GL_BLEND);
    } else if (opacity > 0.0f) {
        draw_texture(logo_texture_, logo_vertices, false, 1.0f);
    }
}

void GlRenderer::release_logo() {
    if (logo_texture_) glDeleteTextures(1, &logo_texture_);
    logo_texture_ = 0;
}

void GlRenderer::shutdown() {
    release_logo();
    if (texture_) {
        glDeleteTextures(1, &texture_);
        texture_ = 0;
    }
    if (chroma_texture_) {
        glDeleteTextures(1, &chroma_texture_);
        chroma_texture_ = 0;
    }
    if (program_) {
        glDeleteProgram(program_);
        program_ = 0;
    }
    if (vertex_shader_) {
        glDeleteShader(vertex_shader_);
        vertex_shader_ = 0;
    }
    if (fragment_shader_) {
        glDeleteShader(fragment_shader_);
        fragment_shader_ = 0;
    }
    if (upload_buffer_) {
        free(upload_buffer_);
        upload_buffer_ = 0;
    }
    upload_buffer_bytes_ = 0;
    texture_width_ = 0;
    texture_height_ = 0;
    texture_format_ = 0;
    output_width_ = 0;
    output_height_ = 0;
    swap_rb_ = false;
    nv12_ = false;
    ready_ = false;
}
