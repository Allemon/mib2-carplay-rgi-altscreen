#include "startup_logo.h"

#include <stdlib.h>
#include <stdint.h>
#include <string.h>
#include <zlib.h>

extern "C" const unsigned char altscreen_logo_data[];
extern "C" const unsigned char altscreen_logo_end[];

static const unsigned kLogoWidth = 1440u;
static const unsigned kLogoHeight = 455u;
static const unsigned kFrameBytes = kLogoWidth * kLogoHeight * 4u;

static bool read_u32(const unsigned char **cursor,
                     const unsigned char *end, unsigned *value) {
    if ((uintptr_t)*cursor > (uintptr_t)end ||
        (uintptr_t)end - (uintptr_t)*cursor < 4u) return false;
    const unsigned char *bytes = *cursor;
    *value = (unsigned)bytes[0] | ((unsigned)bytes[1] << 8) |
             ((unsigned)bytes[2] << 16) | ((unsigned)bytes[3] << 24);
    *cursor += 4u;
    return true;
}

StartupLogo::StartupLogo()
    : cursor_(0), end_(0), pixels_(0),
      frame_count_(0), frame_index_(0), fps_(0) {
}

StartupLogo::~StartupLogo() {
    close();
}

bool StartupLogo::open() {
    close();
    cursor_ = altscreen_logo_data;
    end_ = altscreen_logo_end;
    unsigned width = 0, height = 0;
    if ((uintptr_t)end_ <= (uintptr_t)cursor_ ||
        (uintptr_t)end_ - (uintptr_t)cursor_ < 24u ||
        memcmp(cursor_, "ALTLOGO1", 8u) != 0) {
        close();
        return false;
    }
    cursor_ += 8u;
    if (!read_u32(&cursor_, end_, &width) ||
        !read_u32(&cursor_, end_, &height) ||
        !read_u32(&cursor_, end_, &fps_) ||
        !read_u32(&cursor_, end_, &frame_count_) ||
        width != kLogoWidth || height != kLogoHeight || fps_ != 30u ||
        frame_count_ == 0u || frame_count_ > 300u) {
        close();
        return false;
    }
    pixels_ = (unsigned char *)malloc(kFrameBytes);
    if (!pixels_) {
        close();
        return false;
    }
    return true;
}

bool StartupLogo::next_frame() {
    if (!cursor_ || !pixels_ || frame_index_ >= frame_count_) return false;
    unsigned compressed_bytes = 0;
    if (!read_u32(&cursor_, end_, &compressed_bytes) ||
        compressed_bytes == 0u ||
        compressed_bytes > kFrameBytes) return false;
    if ((uintptr_t)cursor_ > (uintptr_t)end_ ||
        (uintptr_t)end_ - (uintptr_t)cursor_ < compressed_bytes) return false;
    uLongf decoded_bytes = (uLongf)kFrameBytes;
    if (uncompress(pixels_, &decoded_bytes, cursor_,
                   (uLong)compressed_bytes) != Z_OK ||
        decoded_bytes != (uLongf)kFrameBytes) return false;
    cursor_ += compressed_bytes;
    ++frame_index_;
    return true;
}

void StartupLogo::close() {
    free(pixels_);
    cursor_ = 0;
    end_ = 0;
    pixels_ = 0;
    frame_count_ = 0;
    frame_index_ = 0;
    fps_ = 0;
}
