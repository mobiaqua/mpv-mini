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

#include <string.h>
#include <math.h>

#include "mpv_talloc.h"

#include "config.h"

#include "stream/stream.h"
#include "common/common.h"
#include "misc/bstr.h"
#include "common/msg.h"
#include "options/m_option.h"
#include "options/path.h"
#include "video/csputils.h"
#include "lcms.h"

#include "osdep/io.h"

struct gl_lcms *gl_lcms_init(void *talloc_ctx, struct mp_log *log,
                             struct mpv_global *global,
                             struct mp_icc_opts *opts)
{
    return (struct gl_lcms *) talloc_new(talloc_ctx);
}

void gl_lcms_update_options(struct gl_lcms *p) { }
bool gl_lcms_set_memory_profile(struct gl_lcms *p, bstr profile) {return false;}

bool gl_lcms_has_changed(struct gl_lcms *p, enum mp_csp_prim prim,
                         enum mp_csp_trc trc, struct AVBufferRef *vid_profile)
{
    return false;
}

bool gl_lcms_has_profile(struct gl_lcms *p)
{
    return false;
}

bool gl_lcms_get_lut3d(struct gl_lcms *p, struct lut3d **result_lut3d,
                       enum mp_csp_prim prim, enum mp_csp_trc trc,
                       struct AVBufferRef *vid_profile)
{
    return false;
}

static int validate_3dlut_size_opt(struct mp_log *log, const m_option_t *opt,
                                   struct bstr name, const char **value)
{
    int p1, p2, p3;
    return gl_parse_3dlut_size(*value, &p1, &p2, &p3) ? 0 : M_OPT_INVALID;
}

#define OPT_BASE_STRUCT struct mp_icc_opts
const struct m_sub_options mp_icc_conf = {
    .opts = (const m_option_t[]) {
        {"use-embedded-icc-profile", OPT_BOOL(use_embedded)},
        {"icc-profile", OPT_STRING(profile), .flags = M_OPT_FILE},
        {"icc-profile-auto", OPT_BOOL(profile_auto)},
        {"icc-cache", OPT_BOOL(cache)},
        {"icc-cache-dir", OPT_STRING(cache_dir), .flags = M_OPT_FILE},
        {"icc-intent", OPT_INT(intent)},
        {"icc-force-contrast", OPT_CHOICE(contrast, {"no", 0}, {"inf", -1}),
            M_RANGE(0, 1000000)},
        {"icc-3dlut-size", OPT_STRING_VALIDATE(size_str, validate_3dlut_size_opt)},
        {"icc-use-luma", OPT_BOOL(icc_use_luma)},
        {"3dlut-size", OPT_REPLACED("icc-3dlut-size")},
        {"icc-contrast", OPT_REMOVED("see icc-force-contrast")},
        {0}
    },
    .size = sizeof(struct mp_icc_opts),
    .defaults = &(const struct mp_icc_opts) {
        .size_str = "64x64x64",
        .intent = MP_INTENT_RELATIVE_COLORIMETRIC,
        .use_embedded = true,
        .cache = true,
    },
};
