LIST=CPU
NAME=stream-player
SRCS=opengl.cc
LIBS+=EGL
LIBS+=GLESv2
LIBS+=gf
LIBS+=m
LIBS+=socket

# Set FFMPEG_ROOT to the per-arch FFmpeg build directory before invoking make.
FFMPEG_ROOT_ARM ?= /root/ffmpeg-6.1.5
FFMPEG_ROOT_X86 ?= /root/ffmpeg-4.4.4

ifneq ($(FFMPEG_ROOT),)
CCFLAGS += -I$(FFMPEG_ROOT)
LDFLAGS += -L$(FFMPEG_ROOT)/libavformat
LDFLAGS += -L$(FFMPEG_ROOT)/libavcodec
LDFLAGS += -L$(FFMPEG_ROOT)/libswscale
LDFLAGS += -L$(FFMPEG_ROOT)/libavutil
LDFLAGS += -L$(FFMPEG_ROOT)/libswresample
endif

# Static FFmpeg .a files have circular dependencies; wrap in --start/end-group
LDFLAGS += -Wl,--start-group
LIBS+=avformat
LIBS+=avcodec
LIBS+=swscale
LIBS+=swresample
LIBS+=avutil
LIBS+=bz2
LIBS+=z
LDFLAGS += -Wl,--end-group

ifndef QRECURSE
QRECURSE=recurse.mk
ifdef QCONFIG
QRDIR=$(dir $(QCONFIG))
endif
endif
include $(QRDIR)$(QRECURSE)
