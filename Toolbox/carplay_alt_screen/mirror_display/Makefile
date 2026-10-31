BUILD_DIR ?= build
TARGET ?= $(BUILD_DIR)/carplay-alt111-mirror-display
QNX_HOST ?= /usr/qnx650/host/qnx6/x86
QNX_TARGET ?= /usr/qnx650/target/qnx6
CC ?= $(QNX_HOST)/usr/bin/ntoarmv7-gcc
STATIC_STDCXX ?= $(QNX_TARGET)/armle-v7/lib/gcc/4.4.2/libstdc++.a
SOURCES = src/main.cpp src/private111_direct_source.cpp src/cluster_video_display.cpp src/mhi2q_backend.cpp src/gl_renderer.cpp src/state_snapshot.cpp src/startup_logo.cpp src/startup_logo_data.S
LOGO_ASSET = assets/logo_frames.z
CXXFLAGS += -O2 -Wall -Wextra -fno-exceptions -fno-rtti
LDFLAGS += -lEGL -lGLESv2 -lm -l:libz.a -lsocket
.PHONY: all clean
all: $(TARGET)
$(BUILD_DIR):
	mkdir -p $(BUILD_DIR)
$(TARGET): $(SOURCES) $(LOGO_ASSET) | $(BUILD_DIR)
	@test -f "$(STATIC_STDCXX)" || { echo "ERROR: static QNX ARMv7 libstdc++ not found: $(STATIC_STDCXX)"; exit 1; }
	QNX_HOST="$(QNX_HOST)" QNX_TARGET="$(QNX_TARGET)" "$(CC)" $(CXXFLAGS) -Isrc -I../src $(SOURCES) -o "$@" "$(STATIC_STDCXX)" $(LDFLAGS)
	@echo "Built: $@"
clean:
	rm -rf $(BUILD_DIR)
