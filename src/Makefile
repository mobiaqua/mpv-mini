
include config.mak

SRCS_COMMON = \
    audio/aframe.c \
    audio/chmap.c \
    audio/chmap_sel.c \
    audio/decode/ad_lavc.c \
    audio/decode/ad_spdif.c \
    audio/filter/af_drop.c \
    audio/filter/af_format.c \
    audio/filter/af_lavcac3enc.c \
    audio/filter/af_scaletempo.c \
    audio/filter/af_scaletempo2.c \
    audio/filter/af_scaletempo2_internals.c \
    audio/fmt-conversion.c \
    audio/chmap_avchannel.c \
    audio/format.c \
    audio/out/ao.c \
    audio/out/ao_null.c \
    audio/out/buffer.c \
    audio/out/ao_alsa.c \
    common/av_common.c \
    common/av_log.c \
    common/codecs.c \
    common/common.c \
    common/encode_lavc.c \
    common/msg.c \
    common/playlist.c \
    common/recorder.c \
    common/stats.c \
    common/tags.c \
    common/version.c \
    demux/codec_tags.c \
    demux/cue.c \
    demux/cache.c \
    demux/demux.c \
    demux/demux_cue.c \
    demux/demux_lavf.c \
    demux/demux_mkv.c \
    demux/demux_mkv_timeline.c \
    demux/demux_null.c \
    demux/demux_playlist.c \
    demux/demux_raw.c \
    demux/demux_timeline.c \
    demux/ebml.c \
    demux/packet.c \
    demux/timeline.c \
    filters/f_async_queue.c \
    filters/f_autoconvert.c \
    filters/f_auto_filters.c \
    filters/f_decoder_wrapper.c \
    filters/f_demux_in.c \
    filters/f_hwtransfer.c \
    filters/f_lavfi.c \
    filters/f_output_chain.c \
    filters/f_swresample.c \
    filters/f_swscale.c \
    filters/f_utils.c \
    filters/filter.c \
    filters/frame.c \
    filters/user_filters.c \
    input/cmd.c \
    input/event.c \
    input/input.c \
    input/ipc.c \
    input/keycodes.c \
    input/ipc-unix.c \
    misc/bstr.c \
    misc/charset_conv.c \
    misc/dispatch.c \
    misc/json.c \
    misc/language.c \
    misc/natural_sort.c \
    misc/node.c \
    misc/random.c \
    misc/rendezvous.c \
    misc/thread_pool.c \
    misc/thread_tools.c \
    options/m_config_core.c \
    options/m_config_frontend.c \
    options/m_option.c \
    options/m_property.c \
    options/options.c \
    options/parse_commandline.c \
    options/parse_configfile.c \
    options/path.c \
    player/audio.c \
    player/client.c \
    player/command.c \
    player/configfiles.c \
    player/external_files.c \
    player/loadfile.c \
    player/lua.c \
    player/main.c \
    player/misc.c \
    player/osd.c \
    player/playloop.c \
    player/screenshot.c \
    player/scripting.c \
    player/sub.c \
    player/video.c \
    stream/cookies.c \
    stream/stream.c \
    stream/stream_avdevice.c \
    stream/stream_concat.c \
    stream/stream_file.c \
    stream/stream_lavf.c \
    stream/stream_memory.c \
    sub/ass_mp.c \
    sub/dec_sub.c \
    sub/draw_bmp.c \
    sub/filter_sdh.c \
    sub/img_convert.c \
    sub/lavc_conv.c \
    sub/osd.c \
    sub/osd_libass.c \
    sub/sd_ass.c \
    sub/sd_lavc.c \
    sub/filter_regex.c \
    video/csputils.c \
    video/decode/vd_lavc.c \
    video/filter/refqueue.c \
    video/filter/vf_format.c \
    video/filter/vf_sub.c \
    video/fmt-conversion.c \
    video/hwdec.c \
    video/image_loader.c \
    video/image_writer.c \
    video/img_format.c \
    video/mp_image.c \
    video/mp_image_pool.c \
    video/out/aspect.c \
    video/out/bitmap_packer.c \
    video/out/dither.c \
    video/out/dr_helper.c \
    video/out/filter_kernels.c \
    video/out/gpu/context.c \
    video/out/gpu/error_diffusion.c \
    video/out/gpu/hwdec.c \
    video/out/gpu/lcms.c \
    video/out/gpu/libmpv_gpu.c \
    video/out/gpu/osd.c \
    video/out/gpu/ra.c \
    video/out/gpu/spirv.c \
    video/out/gpu/shader_cache.c \
    video/out/gpu/user_shaders.c \
    video/out/gpu/utils.c \
    video/out/gpu/video.c \
    video/out/gpu/video_shaders.c \
    video/out/libmpv_sw.c \
    video/out/vo.c \
    video/out/vo_gpu.c \
    video/out/vo_libmpv.c \
    video/out/vo_null.c \
    video/out/win_state.c \
    video/repack.c \
    video/sws_utils.c \
    osdep/io.c \
    osdep/subprocess.c \
    osdep/threads.c \
    osdep/timer.c \
    osdep/path-unix.c \
    osdep/subprocess-posix.c \
    osdep/polldev.c \
    osdep/terminal-unix.c \
    osdep/path-unix.c \
    osdep/timer-linux.c \
    osdep/main-fn-unix.c \
    osdep/language-posix.c \
    ta/ta.c \
    ta/ta_talloc.c \
    ta/ta_utils.c

ifeq ($(ENABLE_DRM),1)
    SRCS_COMMON += \
    video/drmprime.c \
    video/out/drm_atomic.c \
    video/out/drm_common.c \
    video/out/drm_prime.c \
    video/out/hwdec/hwdec_drmprime.c \
    video/out/hwdec/hwdec_drmprime_overlay.c \
    video/out/vo_drm.c
endif

ifeq ($(ENABLE_GL),1)
    SRCS_COMMON += \
    video/out/opengl/context_drm_egl.c \
    video/out/hwdec/dmabuf_interop_gl.c
endif

ifeq ($(ENABLE_GL),1)
    SRCS_COMMON += \
    video/out/opengl/egl_helpers.c \
    video/filter/vf_gpu.c \
    video/out/opengl/common.c \
    video/out/opengl/context.c \
    video/out/opengl/formats.c \
    video/out/opengl/libmpv_gl.c \
    video/out/opengl/ra_gl.c \
    video/out/opengl/utils.c
endif

ifeq ($(ENABLE_VAAPI),1)
    SRCS_COMMON += \
    video/filter/vf_vavpp.c \
    video/vaapi.c \
    video/out/hwdec/hwdec_vaapi.c
endif

ifeq ($(ENABLE_OMAP_DRM),1)
    SRCS_COMMON +=
endif

ifeq ($(ENABLE_OMAP_DCE),1)
    SRCS_COMMON +=
endif

COMMON_LIBS                   += $(COMMON_LIBS-yes)

OBJS_COMMON                   += $(addsuffix .o, $(basename $(SRCS_COMMON)))

MPV_DEPS      = $(OBJS_COMMON)
DEP_FILES     = $(SRCS_COMMON)
$(foreach suffix,.c .cpp .m .S,$(eval DEP_FILES := $(DEP_FILES:$(suffix)=.d)))

ALL_PRG-yes  += mpv

INSTALL_TARGETS-yes  += install-mpv

DIRS =  .                       \
        audio                   \
        audio/decode            \
        audio/filter            \
        audio/out               \
        common                  \
        demux                   \
        filters                 \
        input                   \
        misc                    \
        options                 \
        player                  \
        stream                  \
        sub                     \
        video                   \
        video/decode            \
        video/filter            \
        video/out               \
        video/out/gpu           \
        video/out/hwdec         \
        osdep                   \
        ta                      \

ALL_DIRS = $(DIRS)

ALLHEADERS = $(foreach dir,$(DIRS),$(wildcard $(dir)/*.h))

ADDSUFFIXES     = $(foreach suf,$(1),$(addsuffix $(suf),$(2)))
ADD_ALL_DIRS    = $(call ADDSUFFIXES,$(1),$(ALL_DIRS))
ADD_ALL_EXESUFS = $(1) $(call ADDSUFFIXES,$(EXESUFS_ALL),$(1))


###### generic rules #######

all: $(ALL_PRG-yes)

%.o: %.S
	$(CC) $(CC_DEPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.c
	$(CC) $(CC_DEPFLAGS) $(CFLAGS) -c -o $@ $<

%.o: %.cpp
	$(CC) $(CC_DEPFLAGS) $(CXXFLAGS) -c -o $@ $<

%.o: %.m
	$(CC) $(CC_DEPFLAGS) $(CFLAGS) -c -o $@ $<

mpv: $(MPV_DEPS)
mpv: EXTRALIBS += $(EXTRALIBS_MPV)
mpv:
	$(CC) -o $@ $^ $(EXTRALIBS)

%: %.c
	$(CC) $(CC_DEPFLAGS) $(CFLAGS) -o $@ $^ $(LIBS)

%.ho: %.h
	$(CC) $(CFLAGS) -Wno-unused -c -o $@ -x c $<

checkheaders: $(ALLHEADERS:.h=.ho)


###### installation / clean / generic rules #######

install: $(INSTALL_TARGETS-yes)

install-dirs:
	$(INSTALL) -d $(BINDIR)

install-%: % install-dirs
	$(INSTALL) -m 755 $(INSTALLSTRIP) $< $(BINDIR)

clean:
	-rm -f $(call ADD_ALL_DIRS,/*.o /*.d /*.a /*.ho /*~)
	-rm -f $(call ADD_ALL_EXESUFS,mpv)

distclean: clean
	-rm -f config.*


-include $(DEP_FILES)

.PHONY: all checkheaders *install* *clean

# Disable suffix rules.  Most of the builtin rules are suffix rules,
# so this saves some time on slow systems.
.SUFFIXES:
