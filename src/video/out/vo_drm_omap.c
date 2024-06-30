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
    struct mp_sws_context *sws;

    struct framebuffer **bufs;
    struct framebuffer *primary_buf;
    int front_buf;
    int buf_count;
};

static void destroy_framebuffer(int fd, struct framebuffer *fb)
{
    if (!fb)
        return;

    if (fb->map) {
        munmap(fb->map, fb->size);
    }

    if (fb->id) {
        drmModeRmFB(fd, fb->id);
    }

    if (fb->dma_buf) {
        close(fb->dma_buf);
    }

    if (fb->handle) {
        struct drm_mode_destroy_dumb dreq = {
            .handle = fb->handle,
        };
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &dreq);
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
    destroy_framebuffer(drm->fd, fb);
    return NULL;
}

static int reconfig(struct vo *vo, struct mp_image_params *params)
{
    struct priv *p = vo->priv;
    struct vo_drm_state *drm = vo->drm;

    vo->dwidth = drm->fb->width;
    vo->dheight = drm->fb->height;
    vo_get_src_dst_rects(vo, &p->src, &p->dst, &p->osd);

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

    if (mp_sws_reinit(p->sws) < 0)
        return -1;

    p->buf_count = vo->opts->swapchain_depth + 1;
    p->bufs = talloc_zero_array(p, struct framebuffer *, p->buf_count);

    for (int i = 0; i < p->buf_count; i++) {
        if (p->bufs[i]) {
            destroy_framebuffer(drm->fd, p->bufs[i]);
        }
        p->bufs[i] = setup_framebuffer(vo, mp_rect_w(p->src), mp_rect_h(p->src), IMGFMT_NV12);
        if (!p->bufs[i])
            return -1;
    }

    vo->want_redraw = true;
    return 0;
}

static struct framebuffer *get_new_fb(struct vo *vo)
{
    struct priv *p = vo->priv;

    p->front_buf++;
    p->front_buf %= p->buf_count;

    return p->bufs[p->front_buf];
}

static void draw_image(struct vo *vo, mp_image_t *mpi, struct framebuffer *buf)
{
    struct priv *p = vo->priv;
    struct vo_drm_state *drm = vo->drm;

    if (drm->active && buf != NULL) {
        if (mpi) {
            struct mp_image src = *mpi;
            struct mp_rect src_rc = p->src;
            src_rc.x0 = MP_ALIGN_DOWN(src_rc.x0, mpi->fmt.align_x);
            src_rc.y0 = MP_ALIGN_DOWN(src_rc.y0, mpi->fmt.align_y);
            mp_image_crop_rc(&src, src_rc);

            mp_sws_scale(p->sws, p->cur_frame_cropped, &src);
            //osd_draw_on_image(vo->osd, p->osd, src.pts, 0, p->cur_frame);
        } else {
            mp_image_clear(p->cur_frame, 0, 0, p->cur_frame->w, p->cur_frame->h);
            //osd_draw_on_image(vo->osd, p->osd, 0, 0, p->cur_frame);
        }

        memcpy_pic(buf->map, p->cur_frame->planes[0],
                   p->cur_frame->w, p->cur_frame->h,
                   buf->stride,
                   p->cur_frame->stride[0]);
        memcpy_pic(buf->map + buf->width * buf->height, p->cur_frame->planes[1],
                   p->cur_frame->w, p->cur_frame->h / 2,
                   buf->stride,
                   p->cur_frame->stride[1]);
    }

    if (mpi != p->last_input) {
        talloc_free(p->last_input);
        p->last_input = mpi;
    }
}

static void enqueue_frame(struct vo *vo, struct framebuffer *fb)
{
    struct priv *p = vo->priv;

    struct drm_frame *new_frame = talloc(p, struct drm_frame);
    new_frame->fb = fb;
    MP_TARRAY_APPEND(p, p->fb_queue, p->fb_queue_len, new_frame);
}

static void dequeue_frame(struct vo *vo)
{
    struct priv *p = vo->priv;

    talloc_free(p->fb_queue[0]);
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

    if (!drm->active)
        return;

    drm->still = frame->still;

    // we redraw the entire image when OSD needs to be redrawn
    struct framebuffer *fb =  p->bufs[p->front_buf];
    const bool repeat = frame->repeat && !frame->redraw;
    if (!repeat) {
        fb = get_new_fb(vo);
        draw_image(vo, mp_image_new_ref(frame->current), fb);
    }

    enqueue_frame(vo, fb);
}

static void queue_flip(struct vo *vo, struct drm_frame *frame)
{
    struct vo_drm_state *drm = vo->drm;
    struct priv *p = vo->priv;
    struct drm_atomic_context *atomic_ctx = drm->atomic_context;

    drmModeSetPlane(drm->fd, atomic_ctx->drmprime_video_plane->id, drm->crtc_id, frame->fb->id, 0,
                    p->dst.x0, p->dst.y0, p->dst.x1 - p->dst.x0, p->dst.y1 - p->dst.y0,
                    p->src.x0 << 16, p->src.y0 << 16, p->src.x1 << 16, p->src.y1 << 16);

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

static void uninit(struct vo *vo)
{
    struct priv *p = vo->priv;
    struct vo_drm_state *drm = vo->drm;

    for (int i = 0; i < p->buf_count; i++) {
        if (p->bufs[i]) {
            destroy_framebuffer(drm->fd, p->bufs[i]);
        }
    }
    if (p->primary_buf) {
        destroy_framebuffer(drm->fd, p->primary_buf);
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

    struct drm_atomic_context *atomic_ctx = drm->atomic_context;

    drmModeAtomicReqPtr request = drmModeAtomicAlloc();
    if (!request) {
        MP_ERR(drm, "Failed to allocate DRM atomic request\n");
        goto err;
    }

    ret = drm_object_set_property(request, atomic_ctx->drmprime_video_plane, "zorder", 1);
    if (ret < 0) {
        MP_ERR(drm, "Could not set ZPOS on prime plane: %s\n", mp_strerror(ret));
        drmModeAtomicFree(request);
        goto err;
    }
    ret = drm_object_set_property(request, atomic_ctx->draw_plane, "zorder", 0);
    if (ret < 0) {
        MP_ERR(drm, "Could not set ZPOS on video plane: %s\n", mp_strerror(ret));
        drmModeAtomicFree(request);
        goto err;
    }
    ret = drmModeAtomicCommit(drm->fd, request, DRM_MODE_ATOMIC_NONBLOCK, drm);
    if (ret)
        MP_WARN(vo, "Failed to commit DRM atomic request: %s\n", mp_strerror(ret));
    drmModeAtomicFree(request);

    vo_drm_set_monitor_par(vo);
    p->sws = mp_sws_alloc(vo);
    p->sws->log = vo->log;
    mp_sws_enable_cmdline_opts(p->sws, vo->global);
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
    switch (request) {
    case VOCTRL_SET_PANSCAN:
        if (vo->config_ok)
            reconfig(vo, vo->params);
        return VO_TRUE;
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
    .draw_frame = draw_frame,
    .flip_page = flip_page,
    .get_vsync = get_vsync,
    .uninit = uninit,
    .wait_events = vo_drm_wait_events,
    .wakeup = vo_drm_wakeup,
    .priv_size = sizeof(struct priv),
};
