
ENABLE_DEBUG = 1
ENABLE_DRM = 1
ENABLE_DRM_OMAP = 0
ENABLE_DRM_INTEL = 0
ENABLE_OMAP_DCE = 0
ENABLE_VAAPI = 1
ENABLE_GL = 1

AR      = $(CROSS_COMPILE)ar
AS      = $(CROSS_COMPILE)gcc $(CPU_FLAGS) --sysroot=$(SYSROOT)
CC      = $(CROSS_COMPILE)gcc $(CPU_FLAGS) --sysroot=$(SYSROOT)

DCE_INCLUDES = $(SYSROOT)/usr/include/dce
DRM_INCLUDES = $(SYSROOT)/usr/include/libdrm
KMS_INCLUDES = $(SYSROOT)/usr/include/libkms
GBM_INCLUDES = $(SYSROOT)/usr/include/gbm
OMAP_INCLUDES = $(SYSROOT)/usr/include/omap
FREETYPE_INCLUDES = $(SYSROOT)/usr/include/freetype2
FRIBIDI_INCLUDES = $(SYSROOT)/usr/include/fribidi
HARFBUZZ_INCLUDES = $(SYSROOT)/usr/include/harfbuzz

CFLAGS = -D_ISOC99_SOURCE -D_GNU_SOURCE -D_FILE_OFFSET_BITS=64 \
-Wall -std=c11 -Werror=implicit-function-declaration -Wno-error=deprecated-declarations -Wno-error=unused-function \
-Wstrict-prototypes -Wno-format-zero-length -Werror=format-security -Wno-redundant-decls -Wvla -Wempty-body \
-Wno-format-truncation -Wimplicit-fallthrough -fno-math-errno -Wall -Wundef -Wmissing-prototypes -Wshadow -Wno-switch \
-Wparentheses -Wpointer-arith -Wno-pointer-sign -Wno-unused-result \
-pthread -I. -I$(FREETYPE_INCLUDES) -I$(FRIBIDI_INCLUDES) -I$(HARFBUZZ_INCLUDES)

ifeq ($(ENABLE_DRM),1)
CFLAGS += -I$(DRM_INCLUDES) -I$(KMS_INCLUDES)
endif

ifeq ($(ENABLE_DRM_OMAP),1)
CFLAGS += -I$(OMAP_INCLUDES)
endif

ifeq ($(ENABLE_DRM_DCE),1)
CFLAGS += -I$(DCE_INCLUDES)
endif

ifeq ($(ENABLE_GL),1)
CFLAGS += -I$(GBM_INCLUDES)
endif

ifeq ($(ENABLE_DEBUG),1)
CFLAGS += -O0 -g3
else
CFLAGS += -O2 -g
endif


EXTRALIBS = -Wl,-z,noexecstack -Wl,-O1 -Wl,--hash-style=gnu -Wl,--as-needed -Wl,-version-script -Wl,mpv.def \
-lavformat -lavcodec -lavfilter -lavdevice -lswscale -lswresample -lavutil \
-lasound -lass -llua -lz -lm

ifeq ($(ENABLE_DRM),1)
EXTRALIBS += -ldrm
endif

ifeq ($(ENABLE_DRM_OMAP),1)
EXTRALIBS += -ldrm_omap
endif

ifeq ($(ENABLE_DRM_INTEL),1)
EXTRALIBS += -ldrm_intel
endif

ifeq ($(ENABLE_OMAP_DCE),1)
EXTRALIBS += -ldce
endif

ifeq ($(ENABLE_GL),1)
EXTRALIBS += -lEGL -lgbm
endif

ifeq ($(ENABLE_VAAPI),1)
EXTRALIBS += -lva-drm -lva
endif


ASFLAGS     = $(CFLAGS)
AS_DEPFLAGS = -MMD -MP
CC_DEPFLAGS = -MMD -MP
HOSTCC      = cc
HOSTCFLAGS  = -I. -O3
HOSTLIBS    = -lm
AS_O        = -o $@
CC_O        = -o $@
CXX_O       = -o $@
AS_C        = -c
CC_C        = -c
CXX_C       = -c
LD          = gcc
RANLIB      = true
STRIP       = strip
