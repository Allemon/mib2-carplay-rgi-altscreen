#ifndef GL_RENDERER_H
#define GL_RENDERER_H

#include <stdlib.h>
#include "video_frame.h"
#include <GLES2/gl2.h>

class GlRenderer {
public:
    GlRenderer();
    ~GlRenderer();

    bool init(int output_width, int output_height);
    bool upload_frame(const VideoFrame &frame);
    bool upload_rgba(const unsigned char *rgba, int width, int height);
    bool upload_test_grid(int width, int height);

    /* Destination rectangle uses output-pixel coordinates with top-left origin. */
    bool set_destination_rect(int x, int y, int width, int height);
    void set_fullscreen_destination();

    bool upload_logo_rgba(const unsigned char *rgba, int width, int height);
    void draw_logo(float opacity, bool over_carplay);
    void release_logo();
    void draw();
    /* The current frame over opaque black: 0 = black, 1 = same as draw(). */
    void draw_faded(float opacity);
    void shutdown();

private:
    GlRenderer(const GlRenderer &);
    GlRenderer &operator=(const GlRenderer &);

    bool compile_shader(GLuint shader, const char *source, const char *name);
    bool ensure_upload_buffer(size_t bytes);
    bool upload_packed_rgba_bytes(const unsigned char *pixels,
                                  int width, int height,
                                  bool swap_rb);
    bool upload_nv12(const VideoFrame &frame);

    GLuint program_;
    GLuint vertex_shader_;
    GLuint fragment_shader_;
    void draw_texture(GLuint texture, const GLfloat *vertices,
                      bool swap_rb, float opacity);

    GLuint texture_;
    GLuint chroma_texture_;
    GLuint logo_texture_;
    GLint attr_position_;
    GLint attr_texcoord_;
    GLint uniform_texture_;
    GLint uniform_swap_rb_;
    GLint uniform_opacity_;
    GLint uniform_chroma_;
    GLint uniform_nv12_;
    GLenum texture_format_;
    int texture_width_;
    int texture_height_;
    int output_width_;
    int output_height_;
    bool swap_rb_;
    bool nv12_;
    bool ready_;

    GLfloat vertices_[8];
    unsigned char *upload_buffer_;
    size_t upload_buffer_bytes_;
};

#endif
