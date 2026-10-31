#ifndef CLUSTER_VIDEO_DISPLAY_H
#define CLUSTER_VIDEO_DISPLAY_H

#include "gl_renderer.h"
#include "mhi2q_backend.h"
#include "video_frame.h"

/* Generic displayable-3 presenter. Java owns terminal1/ctx80; this class owns pixels only. */
class ClusterVideoDisplay {
public:
    ClusterVideoDisplay();
    ~ClusterVideoDisplay();

    bool init(const Mhi2qBackendConfig &cfg);
    bool present_frame(const VideoFrame &frame);
    /* Over opaque black at 0..1; present_faded() reuses the uploaded frame. */
    bool present_frame_faded(const VideoFrame &frame, float opacity);
    bool present_faded(float opacity);
    bool present_logo_frame(const unsigned char *rgba, int width, int height);
    bool present_logo_fade_frame(const VideoFrame &frame, float logo_opacity);
    void release_logo();
    bool present_test_grid();
    bool set_destination_rect(int x, int y, int width, int height);
    void set_fullscreen_destination();
    void refresh();
    void shutdown();

    bool is_ready() const { return ready_; }
    bool first_frame_presented() const { return first_frame_presented_; }
    bool sample_window_state(Mhi2qWindowState *state) const {
        return backend_.sample_window_state(state);
    }
    unsigned long frame_count() const { return frame_count_; }

private:
    ClusterVideoDisplay(const ClusterVideoDisplay &);
    ClusterVideoDisplay &operator=(const ClusterVideoDisplay &);

    bool present_uploaded_frame();

    Mhi2qBackend backend_;
    GlRenderer renderer_;
    bool ready_;
    bool first_frame_presented_;
    unsigned long frame_count_;
};

#endif
