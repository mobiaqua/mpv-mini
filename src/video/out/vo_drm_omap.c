/*
 * This file is part of mpv.
 *
 * mpv is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * mpv is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with mpv.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <sys/mman.h>
#include <poll.h>
#include <unistd.h>

#include <drm_fourcc.h>

#include "common/msg.h"
#include "drm_atomic.h"
#include "drm_common.h"
#include "osdep/timer.h"
#include "sub/osd.h"
#include "sub/draw_bmp.h"
#include "video/fmt-conversion.h"
#include "video/mp_image.h"
#include "video/out/present_sync.h"
#include "video/sws_utils.h"
#include "vo.h"

#define BYTES_PER_PIXEL 4
#define BITS_PER_PIXEL 32

struct drm_frame {
    struct framebuffer *fb;
};

struct priv {
    struct drm_frame **fb_queue;
    unsigned int fb_queue_len;

    uint32_t drm_format;
    enum mp_imgfmt imgfmt;

    struct mp_image *last_input;
    struct mp_image *cur_frame;
    struct mp_image *cur_frame_cropped;
    struct mp_rect src;
    struct mp_rect dst;
    struct mp_osd_res osd;
    struct mp_draw_sub_cache *osd_cache;
    struct mp_sws_context *sws;

    struct framebuffer **bufs;
    struct framebuffer *primary_buf;
    struct framebuffer *osd_buf;
    int front_buf;
    int buf_count;
    bool osd_changed;
};

static void destroy_framebuffer(struct framebuffer *fb)
{
    if (!fb)
        return;

    if (fb->map) {
        munmap(fb->map, fb->size);
    }

    if (fb->id) {
        drmModeRmFB(fb->fd, fb->id);
    }

    if (fb->dma_buf) {
        close(fb->dma_buf);
    }

    if (fb->handle) {
        struct drm_mode_destroy_dumb dreq = {
            .handle = fb->handle,
        };
        drmIoctl(fb->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
    }
}

static struct framebuffer *setup_framebuffer(struct vo *vo, int width, int height, enum mp_imgfmt imgfmt)
{
    struct vo_drm_state *drm = vo->drm;
    struct drm_mode_create_dumb creq = { 0 };
    uint32_t handles[4] = { 0 }, pitches[4] = { 0 }, offsets[4] = { 0 };
    uint32_t drm_format;

    struct framebuffer *fb = talloc_zero(drm, struct framebuffer);
    fb->width = width;
    fb->height = height;
    fb->fd = drm->fd;

    // create dumb buffer
    switch (imgfmt) {
    case IMGFMT_ARGB:
        creq.width = fb->width;
        creq.height = fb->height;
        creq.bpp = 32;
        break;
    case IMGFMT_NV12:
        creq.width = 1;
        creq.height = fb->width * fb->height * 3 / 2;
        creq.bpp = 8;
        break;
    default:
        MP_ERR(vo, "Not supported pixel format to create dumb buffer\n");
        goto err;
    }
    if (drmIoctl(drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        MP_ERR(vo, "Cannot create dumb buffer: %s\n", mp_strerror(errno));
        goto err;
    }

    fb->size = creq.size;
    fb->handle = creq.handle;
    handles[0] = creq.handle;

    // select format
    switch (imgfmt) {
    case IMGFMT_ARGB:
        fb->stride = creq.pitch;
        pitches[0] = creq.pitch;
        drm_format = DRM_FORMAT_ARGB8888;
        break;
    case IMGFMT_NV12:
        fb->stride = fb->width;
        pitches[0] = fb->width;
        pitches[1] = pitches[0];
        handles[1] = handles[0];
        offsets[1] = fb->width * fb->height;
        drm_format = DRM_FORMAT_NV12;
        break;
    default:
        MP_ERR(vo, "Requested format not supported by VO\n");
        goto err;
    }

    // create framebuffer object for the dumb-buffer
    int ret = drmModeAddFB2(fb->fd,
                            fb->width,
                            fb->height,
                            drm_format,
                            handles,
                            pitches,
                            offsets,
                            &fb->id, 0);
    if (ret) {
        MP_ERR(vo, "Cannot create framebuffer: %s\n", mp_strerror(errno));
        goto err;
    }

    // prepare buffer for memory mapping
    struct drm_mode_map_dumb mreq = {
        .handle = fb->handle,
    };
    if (drmIoctl(drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
        MP_ERR(vo, "Cannot map dumb buffer: %s\n", mp_strerror(errno));
        goto err;
    }

    // perform actual memory mapping
    fb->map = mmap(0, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED,
                   drm->fd, mreq.offset);
    if (fb->map == MAP_FAILED) {
        MP_ERR(vo, "Cannot map dumb buffer: %s\n", mp_strerror(errno));
        goto err;
    }

    memset(fb->map, 0, fb->size);

    if (drmPrimeHandleToFD(fb->fd, fb->handle, DRM_CLOEXEC, &fb->dma_buf)) {
        MP_ERR(vo, "Cannot export a dma-buf: %s\n", mp_strerror(errno));
        goto err;
    }

    return fb;

err:
    destroy_framebuffer(fb);
    return NULL;
}

static int reconfig(struct vo *vo, struct mp_image_params *params)
{
    struct priv *p = vo->priv;
    struct vo_drm_state *drm = vo->drm;

    vo->dwidth = params->w;
    vo->dheight = params->h;
    vo_get_src_dst_rects(vo, &p->src, &p->dst, &p->osd);
    p->osd = (struct mp_osd_res) {
        .w = drm->fb->width / 2,
        .h = drm->fb->height / 2,
        .mt = 0,
        .mb = 0,
        .ml = 0,
        .mr = 0,
        .display_par = 1,
    };

    struct mp_imgfmt_desc fmt = mp_imgfmt_get_desc(p->imgfmt);
    p->dst.x0 = MP_ALIGN_DOWN(p->dst.x0, fmt.align_x);
    p->dst.y0 = MP_ALIGN_DOWN(p->dst.y0, fmt.align_y);

    p->sws->src = *params;
    p->sws->dst = (struct mp_image_params) {
        .imgfmt = p->imgfmt,
        .w = mp_rect_w(p->src),
        .h = mp_rect_h(p->src),
        .p_w = 1,
        .p_h = 1,
    };

    talloc_free(p->cur_frame);
    p->cur_frame = mp_image_alloc(p->imgfmt, mp_rect_w(p->src), mp_rect_h(p->src));
    mp_image_params_guess_csp(&p->sws->dst);
    mp_image_set_params(p->cur_frame, &p->sws->dst);
    mp_image_set_size(p->cur_frame, mp_rect_w(p->src), mp_rect_h(p->src));

    talloc_free(p->cur_frame_cropped);
    p->cur_frame_cropped = mp_image_new_dummy_ref(p->cur_frame);
    mp_image_crop_rc(p->cur_frame_cropped, p->src);

    talloc_free(p->last_input);
    p->last_input = NULL;

    if (!vo->hwdec) {
        if (mp_sws_reinit(p->sws) < 0)
            return -1;
        p->buf_count = vo->opts->swapchain_depth + 1;
        if (!p->bufs)
            p->bufs = talloc_zero_array(p, struct framebuffer *, p->buf_count);

        for (int i = 0; i < p->buf_count; i++) {
            if (p->bufs[i])
                destroy_framebuffer(p->bufs[i]);
            p->bufs[i] = setup_framebuffer(vo, mp_rect_w(p->src), mp_rect_h(p->src), IMGFMT_NV12);
            if (!p->bufs[i])
                return -1;
        }
    }

    vo->want_redraw = true;
    p->osd_changed = false;

    return 0;
}

static void enqueue_frame(struct vo *vo, struct framebuffer *fb)
{
    struct priv *p = vo->priv;

    struct drm_frame *new_frame = talloc(p, struct drm_frame);
    new_frame->fb = fb;
    if (vo->hwdec) {
        fb->ref_count++;
    }
    MP_TARRAY_APPEND(p, p->fb_queue, p->fb_queue_len, new_frame);
}

static void dequeue_frame(struct vo *vo)
{
    struct priv *p = vo->priv;
    struct drm_frame *frame = p->fb_queue[0];

    if (vo->hwdec) {
        if (--frame->fb->ref_count == 0) {
            frame->fb->locked = false;
        }
    }
    talloc_free(frame);
    MP_TARRAY_REMOVE_AT(p->fb_queue, p->fb_queue_len, 0);
}

static void swapchain_step(struct vo *vo)
{
    struct priv *p = vo->priv;

    if (p->fb_queue_len > 0) {
        dequeue_frame(vo);
    }
}

static void draw_frame(struct vo *vo, struct vo_frame *frame)
{
    struct vo_drm_state *drm = vo->drm;
    struct priv *p = vo->priv;
    struct framebuffer *fb;
    struct mp_rect act_rc[1], mod_rc[64];
    int num_act_rc = 0, num_mod_rc = 0;

    if (!drm->active)
        return;

    drm->still = frame->still;

    if (vo->hwdec) {
        fb = frame->current->priv;
        p->src.x0 = frame->current->x0;
        p->src.y0 = frame->current->y0;
        p->src.x1 = frame->current->x1;
        p->src.y1 = frame->current->y1;
    } else {
        const bool repeat = frame->repeat && !frame->redraw;
        // we redraw the entire image when OSD needs to be redrawn
        fb = p->bufs[p->front_buf];
        if (!repeat) {
            p->front_buf++;
            p->front_buf %= p->buf_count;
            fb = p->bufs[p->front_buf];
            mp_image_t *mpi = mp_image_new_ref(frame->current);
            if (mpi) {
                struct mp_image src = *mpi;
                struct mp_rect src_rc = p->src;
                src_rc.x0 = MP_ALIGN_DOWN(src_rc.x0, mpi->fmt.align_x);
                src_rc.y0 = MP_ALIGN_DOWN(src_rc.y0, mpi->fmt.align_y);
                mp_image_crop_rc(&src, src_rc);

                mp_sws_scale(p->sws, p->cur_frame_cropped, &src);
            } else {
                mp_image_clear(p->cur_frame, 0, 0, p->cur_frame->w, p->cur_frame->h);
            }

            memcpy_pic(fb->map, p->cur_frame->planes[0],
                       p->cur_frame->w, p->cur_frame->h,
                       fb->stride,
                       p->cur_frame->stride[0]);
            memcpy_pic(fb->map + fb->width * fb->height, p->cur_frame->planes[1],
                       p->cur_frame->w, p->cur_frame->h / 2,
                       fb->stride,
                       p->cur_frame->stride[1]);

            if (mpi != p->last_input) {
                talloc_free(p->last_input);
                p->last_input = mpi;
            }
        }
    }

    if (!p->osd_cache)
        p->osd_cache = mp_draw_sub_alloc(p, vo->global);

    struct sub_bitmap_list *sbs = osd_render(vo->osd, p->osd, frame->current->pts, 0, mp_draw_sub_formats);
    if (!sbs) {
        MP_ERR(vo, "Cannot get sub bitmap list!\n");
    } else {
        struct mp_image *osd_sub = mp_draw_sub_overlay(p->osd_cache, sbs,
                                                       act_rc, MP_ARRAY_SIZE(act_rc), &num_act_rc,
                                                       mod_rc, MP_ARRAY_SIZE(mod_rc), &num_mod_rc);
        talloc_free(sbs);
        if (!osd_sub) {
            MP_ERR(vo, "Cannot get OSD image!\n");
        } else {
            for (int n = 0; n < num_mod_rc; n++) {
                 p->osd_changed = true;
                 struct mp_rect rc = mod_rc[n];

                 int rw = mp_rect_w(rc);
                 int rh = mp_rect_h(rc);

                 void *src = mp_image_pixel_ptr(osd_sub, 0, rc.x0, rc.y0);
                 void *dst = p->osd_buf->map + rc.x0 * 4 + rc.y0 * p->osd_buf->stride;

                 // Avoid overflowing if we have this special case.
                 if (n == num_mod_rc - 1)
                    --rh;

                 memcpy_pic(dst, src, rw * 4, rh, p->osd_buf->stride, osd_sub->stride[0]);
            }
        }
    }

    enqueue_frame(vo, fb);
}

static void queue_flip(struct vo *vo, struct drm_frame *frame)
{
    struct vo_drm_state *drm = vo->drm;
    struct priv *p = vo->priv;
    struct drm_atomic_context *atomic_ctx = drm->atomic_context;

    int src_w = p->src.x1 - p->src.x0;
    int src_h = p->src.y1 - p->src.y0;
    int dst_x = p->dst.x0;
    int dst_y = p->dst.y0;
    int dst_w = p->dst.x1 - p->dst.x0;
    int dst_h = p->dst.y1 - p->dst.y0;

    // Anisotropic video hack
    if (src_w == 720 && (src_h == 288 || src_h == 240)) {
        dst_x = 0;
        dst_y = 0;
        dst_w = drm->fb->width;
        dst_h = drm->fb->height;
    } else if (src_w == 1920 && src_h == 540) {
        // specific video hack
        dst_y = 0;
        dst_h *= 2;
    } else {
        float rw = (float)(src_w) / drm->fb->width;
        float rh = (float)(src_h) / drm->fb->height;
        if (rw >= rh) {
            dst_w = drm->fb->width;
            dst_h = drm->fb->height * (rh / rw);
            dst_x = 0;
            dst_y = (drm->fb->height - dst_h) / 2;
        } else {
            dst_w = drm->fb->width * (rw / rh);
            dst_h = drm->fb->height;
            dst_x = (drm->fb->width - dst_w) / 2;
            dst_y = 0;
        }
    }

    drmModeSetPlane(drm->fd, atomic_ctx->drmprime_video_plane->id, drm->crtc_id, frame->fb->id, 0,
                    MP_ALIGN_DOWN(dst_x, 2),
                    MP_ALIGN_DOWN(dst_y, 2),
                    MP_ALIGN_DOWN(dst_w ,2),
                    MP_ALIGN_DOWN(dst_h, 2),
                    p->src.x0 << 16,
                    p->src.y0 << 16,
                    src_w << 16,
                    src_h << 16);

    if (p->osd_changed) {
        drmModeSetPlane(drm->fd, atomic_ctx->osd_plane->id, drm->crtc_id, p->osd_buf->id, 0,
                        0,
                        0,
                        drm->fb->width,
                        drm->fb->height,
                        0,
                        0,
                        p->osd.w << 16,
                        p->osd.h << 16);
        p->osd_changed = false;
    }

    int ret = drmModePageFlip(drm->fd, drm->crtc_id,
                              drm->fb->id, DRM_MODE_PAGE_FLIP_EVENT, drm);
    if (ret)
        MP_WARN(vo, "Failed to queue page flip: %s\n", mp_strerror(errno));
    drm->waiting_for_flip = !ret;
}

static void flip_page(struct vo *vo)
{
    struct priv *p = vo->priv;
    struct vo_drm_state *drm = vo->drm;
    const bool drain = drm->paused || drm->still;

    if (!drm->active)
        return;

    while (drain || p->fb_queue_len > vo->opts->swapchain_depth) {
        if (drm->waiting_for_flip) {
            vo_drm_wait_on_flip(vo->drm);
            swapchain_step(vo);
        }
        if (p->fb_queue_len <= 1)
            break;
        if (!p->fb_queue[1] || !p->fb_queue[1]->fb) {
            MP_ERR(vo, "Hole in swapchain?\n");
            swapchain_step(vo);
            continue;
        }
        queue_flip(vo, p->fb_queue[1]);
    }
}

static void get_vsync(struct vo *vo, struct vo_vsync_info *info)
{
    struct vo_drm_state *drm = vo->drm;
    present_sync_get_info(drm->present, info);
}

static struct framebuffer *alloc_buffer(struct vo *vo, int imgfmt, int w, int h)
{
    return setup_framebuffer(vo, w, h, imgfmt);
}

static void release_buffer(struct vo *vo, struct framebuffer *buffer)
{
    if (buffer) {
        destroy_framebuffer(buffer);
    }
}

static void uninit(struct vo *vo)
{
    struct priv *p = vo->priv;

    if (p->osd_buf) {
        destroy_framebuffer(p->osd_buf);
    }

    if (!vo->hwdec && p->bufs) {
        for (int i = 0; i < p->buf_count; i++) {
            if (p->bufs[i]) {
                destroy_framebuffer(p->bufs[i]);
            }
        }
        talloc_free(p->bufs);
    }
    if (p->primary_buf) {
        destroy_framebuffer(p->primary_buf);
        talloc_free(p->primary_buf);
    }

    vo_drm_uninit(vo);

    while (p->fb_queue_len > 0) {
        swapchain_step(vo);
    }

    talloc_free(p->last_input);
    talloc_free(p->cur_frame);
    talloc_free(p->cur_frame_cropped);
}

static int preinit(struct vo *vo)
{
    struct priv *p = vo->priv;
    int ret;

    if (!vo_drm_init(vo))
        goto err;

    struct vo_drm_state *drm = vo->drm;

    p->primary_buf = setup_framebuffer(vo, drm->mode.mode.hdisplay, drm->mode.mode.vdisplay, IMGFMT_ARGB);
    if (!p->primary_buf)
        goto err;

    p->drm_format = DRM_FORMAT_NV12;
    p->imgfmt = IMGFMT_NV12;

    p->front_buf = 0;
    drm->fb = p->primary_buf;

    vo->drm->width = vo->drm->fb->width;
    vo->drm->height = vo->drm->fb->height;

    if (!vo_drm_acquire_crtc(vo->drm)) {
        MP_ERR(vo, "Failed to set CRTC for connector %u: %s\n",
               vo->drm->connector->connector_id, mp_strerror(errno));
        goto err;
    }

    if (!p->osd_buf) {
        p->osd_buf = setup_framebuffer(vo, drm->mode.mode.hdisplay / 2, drm->mode.mode.vdisplay / 2, IMGFMT_ARGB);
        if (!p->osd_buf) {
            MP_ERR(vo, "Failed to allocate OSD buffer\n");
            goto err;
        }
        p->osd_buf->locked = false;
    }

    struct drm_atomic_context *atomic_ctx = drm->atomic_context;

    drmModeAtomicReqPtr request = drmModeAtomicAlloc();
    if (!request) {
        MP_ERR(drm, "Failed to allocate DRM atomic request\n");
        goto err;
    }

    ret = drm_object_set_property(request, atomic_ctx->drmprime_video_plane, "zorder", 0);
    if (ret < 0) {
        MP_ERR(drm, "Could not set ZPOS on video plane: %s\n", mp_strerror(ret));
        drmModeAtomicFree(request);
        goto err;
    }
    ret = drm_object_set_property(request, atomic_ctx->draw_plane, "zorder", 2);
    if (ret < 0) {
        MP_ERR(drm, "Could not set ZPOS on primary plane: %s\n", mp_strerror(ret));
        drmModeAtomicFree(request);
        goto err;
    }
    ret = drm_object_set_property(request, atomic_ctx->osd_plane, "zorder", 1);
    if (ret < 0) {
        MP_ERR(drm, "Could not set ZPOS on OSD plane: %s\n", mp_strerror(ret));
        drmModeAtomicFree(request);
        goto err;
    }
    ret = drmModeAtomicCommit(drm->fd, request, DRM_MODE_ATOMIC_NONBLOCK, drm);
    if (ret)
        MP_WARN(vo, "Failed to commit DRM atomic request: %s\n", mp_strerror(ret));
    drmModeAtomicFree(request);

    vo_drm_set_monitor_par(vo);
    if (!vo->hwdec) {
        p->sws = mp_sws_alloc(vo);
        p->sws->log = vo->log;
        mp_sws_enable_cmdline_opts(p->sws, vo->global);
    }
    return 0;

err:
    uninit(vo);
    return -1;
}

static int query_format(struct vo *vo, int format)
{
    struct priv *p = vo->priv;
    return mp_sws_supports_formats(p->sws, p->imgfmt, format) ? 1 : 0;
}

static int control(struct vo *vo, uint32_t request, void *arg)
{
    struct priv *p = vo->priv;

    switch (request) {
    case VOCTRL_SET_PANSCAN:
        if (vo->config_ok)
            reconfig(vo, vo->params);
        return VO_TRUE;
    case VOCTRL_RESET:
        while (p->fb_queue_len > 0) {
            swapchain_step(vo);
        }
        return VO_TRUE;
    case VOCTRL_CHECK_EVENTS:
        break;
    default:
        break;
    }

    int events = 0;
    int ret = vo_drm_control(vo, &events, request, arg);
    vo_event(vo, events);
    return ret;
}

const struct vo_driver video_out_drm_omap = {
    .name = "drm_omap",
    .description = "Direct Rendering Manager (OMAP)",
    .preinit = preinit,
    .query_format = query_format,
    .reconfig = reconfig,
    .control = control,
    .alloc_buffer = alloc_buffer,
    .release_buffer = release_buffer,
    .draw_frame = draw_frame,
    .flip_page = flip_page,
    .get_vsync = get_vsync,
    .uninit = uninit,
    .wait_events = vo_drm_wait_events,
    .wakeup = vo_drm_wakeup,
    .priv_size = sizeof(struct priv),
};
