#ifndef STARTUP_LOGO_H
#define STARTUP_LOGO_H

/* Host-decoded Logo.mp4. One zlib frame is inflated at a time; no video
 * decoder or full-clip buffer is needed on the unit. */
class StartupLogo {
public:
    StartupLogo();
    ~StartupLogo();

    bool open();
    bool next_frame();
    void close();
    const unsigned char *pixels() const { return pixels_; }
    unsigned frame_count() const { return frame_count_; }
    unsigned fps() const { return fps_; }

private:
    StartupLogo(const StartupLogo &);
    StartupLogo &operator=(const StartupLogo &);

    const unsigned char *cursor_;
    const unsigned char *end_;
    unsigned char *pixels_;
    unsigned frame_count_;
    unsigned frame_index_;
    unsigned fps_;
};

#endif
