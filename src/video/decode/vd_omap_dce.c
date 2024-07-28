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

#include <float.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>
#include <assert.h>
#include <stdbool.h>

#include <drm/drm.h>
#include <xf86drm.h>
#include <sys/mman.h>

#define xdc_target_types__ gnu/targets/std.h
#define ti_sdo_fc_ires_NOPROTOCOLREV
#include <xdc/std.h>
#include <ti/sdo/ce/Engine.h>
#include <ti/xdais/dm/xdm.h>
#include <ti/sdo/ce/video3/viddec3.h>
#include <ti/sdo/codecs/h264vdec/ih264vdec.h>
#include <ti/sdo/codecs/mpeg4vdec/impeg4vdec.h>
#include <ti/sdo/codecs/mpeg2vdec/impeg2vdec.h>
#include <ti/sdo/codecs/vc1vdec/ivc1vdec.h>
#include <libdce.h>

#include "mpv_talloc.h"
#include "common/global.h"
#include "common/msg.h"
#include "options/m_config.h"
#include "options/options.h"
#include "misc/bstr.h"
#include "common/av_common.h"
#include "common/codecs.h"

#include "video/fmt-conversion.h"

#include "filters/f_decoder_wrapper.h"
#include "filters/filter_internal.h"
#include "video/img_format.h"
#include "video/mp_image.h"
#include "demux/demux.h"
#include "demux/stheader.h"
#include "demux/packet.h"
#include "video/csputils.h"
#include "video/out/vo.h"
#include "video/out/drm_common.h"

#define ALIGN2(value, align) (((value) + ((1 << (align)) - 1)) & ~((1 << (align)) - 1))

#ifndef O_CLOEXEC
#define O_CLOEXEC 02000000
#endif

#define OPT_BASE_STRUCT struct vd_omap_dce_params

struct vd_omap_dce_params {
};

struct vd_buffered_pts {
    double pts;
    double dts;
    double duration;
};

const struct m_sub_options vd_omap_dce_conf = {
    .opts = (const m_option_t[]){
        {0}
    },
    .size = sizeof(struct vd_omap_dce_params),
    .defaults = &(const struct vd_omap_dce_params){
    },
};

typedef struct omap_dce_ctx {
    struct mp_log *log;
    struct m_config_cache *opts_cache;
    struct vd_omap_dce_params *opts;
    struct mp_codec_params *codec;
    const char *decoder;
    struct vo *vo;
    int framedrop_flags;
    bool force_eof;
    bool flushed;

    Engine_Handle              codecEngine;
    VIDDEC3_Handle             codecHandle;
    VIDDEC3_Params             *codecParams;
    VIDDEC3_DynamicParams      *codecDynParams;
    VIDDEC3_Status             *codecStatus;
    XDM2_BufDesc               *codecInputBufs;
    XDM2_BufDesc               *codecOutputBufs;
    VIDDEC3_InArgs             *codecInputArgs;
    VIDDEC3_OutArgs            *codecOutputArgs;
    int                        frameWidth;
    int                        frameHeight;
    void                       *inputBufPtr;
    int                        inputBufSize;
    uint32_t                   inputBufHandle;
    int                        dpbSizeInFrames;
    int                        numFrameBuffers;
    struct framebuffer         **frameBuffers;
    struct vd_buffered_pts     *bufferedPts;
    unsigned int               codecId;

    struct mp_decoder public;
} vd_omap_dce_ctx;

static const char *const h264_codec_errors_str[32][2] = {
 { "ERR_NOSLICE",                           "No error-free slice header detected in the frame" },
 { "ERR_SPS",                               "Error in SPS parsing" },
 { "ERR_PPS",                               "Error during PPS parsing" },
 { "ERR_SLICEHDR",                          "Error in slice header parsing" },
 { "ERR_MBDATA",                            "Error in MB data parsing" },
 { "ERR_UNAVAILABLESPS",                    "SPS referred in the header is not available" },
 { "ERR_UNAVAILABLEPPS",                    "PPS referred in the header is not available" },
 { "ERR_INVALIDPARAM_IGNORE",               "Invalid Parameter" },

 { "XDM_PARAMSCHANGE",                      "Sequence Parameters Change" },
 { "XDM_APPLIEDCONCEALMENT",                "Applied concealment" },
 { "XDM_INSUFFICIENTDATA",                  "Insufficient input data" },
 { "XDM_CORRUPTEDDATA",                     "Data problem/corruption" },
 { "XDM_CORRUPTEDHEADER",                   "Header problem/corruption" },
 { "XDM_UNSUPPORTEDINPUT",                  "Unsupported feature/parameter in input" },
 { "XDM_UNSUPPORTEDPARAM",                  "Unsupported input parameter or configuration" },
 { "XDM_FATALERROR",                        "Fatal error" },

 { "ERR_UNSUPPFEATURE",                     "Unsupported feature" },
 { "ERR_METADATA_BUFOVERFLOW",              "SEI Buffer overflow detected" },
 { "ERR_STREAM_END",                        "End of stream reached" },
 { "ERR_NO_FREEBUF",                        "No free buffers available for reference storing reference frame" },
 { "ERR_PICSIZECHANGE",                     "Change in resolution detected" },
 { "ERR_UNSUPPRESOLUTION",                  "Unsupported resolution by the decoder" },
 { "ERR_NUMREF_FRAMES",                     "maxNumRefFrames parameter is not compliant to stream properties" },
 { "ERR_INVALID_MBOX_MESSAGE",              "Invalid (unexpected) mail box message received by M3/M4 or IVAHD" },
 { "ERR_DATA_SYNC",                         "In data sync enable mode, the input supplied is wrong" },
 { "ERR_MISSINGSLICE",                      "Missing slice in a frame" },
 { "ERR_INPUT_DATASYNC_PARAMS",             "Input data sync enable mode, the input parameter is wrong" },
 { "ERR_HDVICP2_IMPROPER_STATE",            "IVAHD standby failed or couldn't turn-on/off the IP's clock or HDVICP reset failed" },
 { "ERR_TEMPORAL_DIRECT_MODE",              "Temporal direct mode is present in the bits stream when disableTemporalDirect parameter (create time) is set" },
 { "ERR_DISPLAYWIDTH",                      "DisplayWidth is less than the Image width + Padded width" },
 { "ERR_NOHEADER",                          "Indicates that no SPS/PPS header is decoded in the current process call" },
 { "ERR_GAPSINFRAMENUM",                    "Indicates that a gap is detected in frame_num for a stream with gaps_in_frame_num_value_allowed_flag 1 in SPS" }
};

static const char *const mpeg4_codec_errors_str[32][2] = {
 { "ERR_VOS",                               "No Video Object Sequence detected in the frame" },
 { "ERR_VO",                                "Incorrect Video Object type" },
 { "ERR_VOL",                               "Error in Video Object Layer detected" },
 { "ERR_GOV",                               "Error in Group of Video parsing" },
 { "ERR_VOP",                               "Error in Video Object Plane parsing" },
 { "ERR_SHORTHEADER",                       "Error in short header parsing" },
 { "ERR_GOB",                               "Error in GOB parsing" },
 { "ERR_VIDEOPACKET",                       "Error in Video Packet parsing" },

 { "XDM_PARAMSCHANGE",                      "Sequence Parameters Change" },
 { "XDM_APPLIEDCONCEALMENT",                "Applied concealment" },
 { "XDM_INSUFFICIENTDATA",                  "Insufficient input data" },
 { "XDM_CORRUPTEDDATA",                     "Data problem/corruption" },
 { "XDM_CORRUPTEDHEADER",                   "Header problem/corruption" },
 { "XDM_UNSUPPORTEDINPUT",                  "Unsupported feature/parameter in input" },
 { "XDM_UNSUPPORTEDPARAM",                  "Unsupported input parameter or configuration" },
 { "XDM_FATALERROR",                        "Fatal error" },

 { "ERR_MBDATA",                            "Error in MB data parsing" },
 { "ERR_INVALIDPARAM_IGNORE",               "Invalid Parameter" },
 { "ERR_UNSUPPFEATURE",                     "Unsupported feature" },
 { "ERR_STREAM_END",                        "End of stream reached" },
 { "ERR_VALID_HEADER_NOT_FOUND",            "Valid header not found.i.e (VOL/VOP not found)" },
 { "ERR_UNSUPPRESOLUTION",                  "Unsupported resolution by the decoder" },
 { "ERR_BITSBUF_UNDERFLOW",                 "The stream buffer has underflowed" },
 { "ERR_INVALID_MBOX_MESSAGE",              "Invalid (unexpected) mail box message received by IVAHD" },
 { "ERR_NO_FRAME_FOR_FLUSH",                "Codec does not have any frame for flushing out to application" },
 { "ERR_VOP_NOT_CODED",                     "Given VOP is not codec" },
 { "ERR_START_CODE_NOT_PRESENT",            "Start code for given stream is not present in case of Parse Header" },
 { "ERR_VOP_TIME_INCREMENT_RES_ZERO",       "Unsupported time increment resolution by the decoder" },
 { "ERR_PICSIZECHANGE",                     "Resolution gets change in between process call" },
 { "ERR_UNSUPPORTED_H263_ANNEXS",           "Unsupported Annex S of the H263" },
 { "ERR_HDVICP2_IMPROPER_STATE",            "HDVCIP is not in correct state" },
 { "ERR_IFRAME_DROPPED",                    "Current frame is lost, no frame is present for decode" },
};

static const char *const mpeg2_codec_errors_str[32][2] = {
 { "ERR_UNSUPPORTED_VIDDEC3PARAMS",         "Unsupported VIDDEC3PARAMS" },
 { "ERR_UNSUPPORTED_VIDDEC3DYNAMICPARAMS",  "Unsupported VIDDEC3 Dynamic PARAMS" },
 { "ERR_UNSUPPORTED_MPEG2DECDYNAMICPARAMS", "Unsupported MPEG1/2 VIDDEC3 Dynamic PARAMS" },
 { "ERR_IMPROPER_DATASYNC_SETTING",         "Improper data sync setting" },
 { "ERR_NOSLICE",                           "No slice" },
 { "ERR_SLICEHDR",                          "Slice header corruption" },
 { "ERR_MBDATA",                            "MB data corruption" },
 { "ERR_UNSUPPFEATURE",                     "Unsupported MPEG1/2 feature" },

 { "XDM_PARAMSCHANGE",                      "Sequence Parameters Change" },
 { "XDM_APPLIEDCONCEALMENT",                "Applied concealment" },
 { "XDM_INSUFFICIENTDATA",                  "Insufficient input data" },
 { "XDM_CORRUPTEDDATA",                     "Data problem/corruption" },
 { "XDM_CORRUPTEDHEADER",                   "Header problem/corruption" },
 { "XDM_UNSUPPORTEDINPUT",                  "Unsupported feature/parameter in input" },
 { "XDM_UNSUPPORTEDPARAM",                  "Unsupported input parameter or configuration" },
 { "XDM_FATALERROR",                        "Fatal error" },

 { "ERR_STREAM_END",                        "End of stream" },
 { "ERR_UNSUPPRESOLUTION",                  "Unsupported resolution" },
 { "ERR_STANDBY",                           "IVAHD standby" },
 { "ERR_INVALID_MBOX_MESSAGE",              "Invalid mailbox message" },
 { "ERR_HDVICP_RESET",                      "" },
 { "ERR_HDVICP_WAIT_NOT_CLEAN_EXIT",        "" },
 { "ERR_SEQHDR",                            "Sequence header corruption" },
 { "ERR_GOP_PICHDR",                        "" },
 { "ERR_SEQLVL_EXTN",                       "" },
 { "ERR_PICLVL_EXTN",                       "" },
 { "ERR_TRICK_MODE",                        "" },
 { "ERR_PICSIZECHANGE",                     "Picture size change. It will be set for a multi resolution" },
 { "ERR_SEMANTIC",                          "" },
 { "ERR_DECODE_EXIT",                       "" },
 { "ERR_IRES_RESHANDLE",                    "" },
 { "ERR_IRES_RESDESC",                      "" },
};

static const char *const vc1_codec_errors_str[32][2] = {
 { "ERR_UNSUPPORTED_VIDDEC3PARAMS",         "Unsupported VIDDEC3PARAMS" },
 { "ERR_UNSUPPORTED_VIDDEC3DYNAMICPARAMS",  "Unsupported VIDDEC3 Dynamic PARAMS" },
 { "ERR_UNSUPPORTED_VC1DECDYNAMICPARAMS",   "Unsupported VC1 VIDDEC3 Dynamic PARAMS" },
 { "ERR_IMPROPER_DATASYNC_SETTING",         "Improper data sync setting" },
 { "ERR_NOSLICE",                           "No slice" },
 { "ERR_SLICEHDR",                          "Slice header corruption" },
 { "ERR_MBDATA",                            "MB data corruption" },
 { "ERR_UNSUPPFEATURE",                     "Unsupported VC1 feature" },

 { "XDM_PARAMSCHANGE",                      "Sequence Parameters Change" },
 { "XDM_APPLIEDCONCEALMENT",                "Applied concealment" },
 { "XDM_INSUFFICIENTDATA",                  "Insufficient input data" },
 { "XDM_CORRUPTEDDATA",                     "Data problem/corruption" },
 { "XDM_CORRUPTEDHEADER",                   "Header problem/corruption" },
 { "XDM_UNSUPPORTEDINPUT",                  "Unsupported feature/parameter in input" },
 { "XDM_UNSUPPORTEDPARAM",                  "Unsupported input parameter or configuration" },
 { "XDM_FATALERROR",                        "Fatal error" },

 { "ERR_STREAM_END",                        "End of stream" },
 { "ERR_UNSUPPRESOLUTION",                  "Unsupported resolution" },
 { "ERR_STANDBY",                           "IVAHD standby" },
 { "ERR_INVALID_MBOX_MESSAGE",              "Invalid mailbox message" },
 { "ERR_SEQHDR",                            "Sequence header corruption" },
 { "ERR_ENTRYHDR",                          "Entry point header corruption" },
 { "ERR_PICHDR",                            "Picture header corruption" },
 { "ERR_REF_PICTURE_BUFFER",                "Reference picture Buffer" },
 { "ERR_NOSEQUENCEHEADER",                  "There is no sequence header start code" },
 { "",                                      "" },
 { "",                                      "" },
 { "",                                      "" },
 { "",                                      "" },
 { "",                                      "" },
 { "ERR_BUFDESC",                           "Invalid values of input/output buffer descriptors" },
 { "ERR_PICSIZECHANGE",                     "Picture size change. It will be set for a multi resolution" },
};

static void decode_codec_error(int codecId, uint32_t error, char *buf_error, int max_buf)
{
    strncpy(buf_error, " ", max_buf);
    for (int i = 0; i <= 31; i++) {
        if (!(error & (1 << i)))
            continue;
        switch (codecId) {
        case AV_CODEC_ID_H264: {
            strncat(buf_error, h264_codec_errors_str[i][0], max_buf);
            break;
        }
        case AV_CODEC_ID_MPEG4: {
            strncat(buf_error, mpeg4_codec_errors_str[i][0], max_buf);
            break;
        }
        case AV_CODEC_ID_MPEG2VIDEO:
        case AV_CODEC_ID_MPEG1VIDEO: {
            strncat(buf_error, mpeg2_codec_errors_str[i][0], max_buf);
            break;
        }
        case AV_CODEC_ID_VC1:
        case AV_CODEC_ID_WMV3: {
            strncat(buf_error, vc1_codec_errors_str[i][0], max_buf);
            break;
        }
        }
        strncat(buf_error, " ", max_buf);
    }
}

static void uninit(struct mp_filter *vd);

static bool init(struct mp_filter *vd)
{
    vd_omap_dce_ctx *ctx = vd->priv;

    m_config_cache_update(ctx->opts_cache);

    Engine_Error engineError;
    Int32 codecError;
    struct drm_mode_create_dumb creq;
    struct drm_mode_map_dumb mreq;
    struct drm_prime_handle dreq;
    int i, dpbSizeInFrames = 0;

    switch (ctx->codec->codec_tag) {
    case 0x10000005:
    case 0x00000005:
    case MKTAG('H','2','6','4'):
    case MKTAG('h','2','6','4'):
    case MKTAG('X','2','6','4'):
    case MKTAG('x','2','6','4'):
    case MKTAG('A','V','C','1'):
    case MKTAG('a','v','c','1'):
        ctx->codecId = AV_CODEC_ID_H264;
        break;
    case 0x10000004:
    case 0x00000004:
    case MKTAG('F','M','P','4'):
    case MKTAG('f','m','p','4'):
    case MKTAG('M','P','4','V'):
    case MKTAG('m','p','4','v'):
    case MKTAG('X','V','I','D'):
    case MKTAG('x','v','i','d'):
    case MKTAG('X','v','i','D'):
    case MKTAG('X','V','I','X'):
    case MKTAG('D','X','5','0'):
    case MKTAG('D','X','G','M'):
    case MKTAG('D','I','V','X'):
        ctx->codecId = AV_CODEC_ID_MPEG4;
        break;
    case 0x10000002:
    case 0x00000002:
    case MKTAG('m','p','g','2'):
    case MKTAG('M','P','G','2'):
    case MKTAG('M','7','0','1'):
    case MKTAG('m','2','v','1'):
    case MKTAG('m','2','2','v'):
    case MKTAG('m','p','g','v'):
        ctx->codecId = AV_CODEC_ID_MPEG2VIDEO;
        break;
    case 0x10000001:
    case 0x00000001:
    case MKTAG('m','p','g','1'):
    case MKTAG('M','P','G','1'):
    case MKTAG('m','1','v','1'):
        ctx->codecId = AV_CODEC_ID_MPEG1VIDEO;
        break;
    case MKTAG('W','V','C','1'):
    case MKTAG('w','v','c','1'):
    case MKTAG('V','C','-','1'):
    case MKTAG('v','c','-','1'):
        ctx->codecId = AV_CODEC_ID_VC1;
        break;
    case MKTAG('W','M','V','3'):
        ctx->codecId = AV_CODEC_ID_WMV3;
        break;
    case 0:
        switch (ctx->codec->codec_id) {
        case AV_CODEC_ID_H264:
        case AV_CODEC_ID_MPEG4:
        case AV_CODEC_ID_MPEG2VIDEO:
        case AV_CODEC_ID_MPEG1VIDEO:
        case AV_CODEC_ID_VC1:
        case AV_CODEC_ID_WMV3:
            ctx->codecId = ctx->codec->codec_id;
            break;
        default:
            MP_ERR(vd, "Unsupported codec id: %d ------\n",
                   ctx->codec->codec_id);
            return false;
        }
        break;
    default:
        MP_ERR(vd, "Unsupported codec id: %08x, tag: '%4s' ------\n",
               ctx->codec->codec_tag, (char *)&ctx->codec->codec_tag);
        return false;
    }

    ctx->frameWidth  = ALIGN2(ctx->codec->disp_w, 4);
    ctx->frameHeight = ALIGN2(ctx->codec->disp_h, 4);

    dce_init(ctx->vo->drm->fd);

    ctx->codecEngine = Engine_open((String)"ivahd_vidsvr", NULL, &engineError);
    if (!ctx->codecEngine) {
        MP_ERR(vd, "Failed open codec engine!\n");
        goto fail;
    }

    switch (ctx->codecId) {
    case AV_CODEC_ID_H264:
        ctx->codecParams = (VIDDEC3_Params *)dce_alloc(sizeof(IH264VDEC_Params));
        ctx->numFrameBuffers = IVIDEO2_MAX_IO_BUFFERS;
        break;
    case AV_CODEC_ID_MPEG4:
        ctx->codecParams = (VIDDEC3_Params *)dce_alloc(sizeof(IMPEG4VDEC_Params));
        ctx->numFrameBuffers = 4;
        break;
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        ctx->codecParams = (VIDDEC3_Params *)dce_alloc(sizeof(IMPEG2VDEC_Params));
        ctx->numFrameBuffers = 3;
        break;
    case AV_CODEC_ID_WMV3:
    case AV_CODEC_ID_VC1:
        ctx->codecParams = (VIDDEC3_Params *)dce_alloc(sizeof(IVC1VDEC_Params));
        ctx->numFrameBuffers = 4;
        break;
    default:
        MP_ERR(vd, "Unsupported codec %d\n", ctx->codecId);
        goto fail;
    }

    if (!ctx->codecParams) {
        MP_ERR(vd, "Error allocation with dce_alloc()\n");
        goto fail;
    }

    ctx->numFrameBuffers += ctx->vo->opts->swapchain_depth + 25;

    ctx->codecParams->maxWidth = ctx->frameWidth;
    ctx->codecParams->maxHeight = ctx->frameHeight;
    ctx->codecParams->maxFrameRate = 30000;
    ctx->codecParams->maxBitRate = 10000000;
    ctx->codecParams->dataEndianness = XDM_BYTE;
    ctx->codecParams->forceChromaFormat = XDM_YUV_420SP;
    ctx->codecParams->operatingMode = IVIDEO_DECODE_ONLY;
    ctx->codecParams->displayDelay = IVIDDEC3_DISPLAY_DELAY_AUTO;
    ctx->codecParams->displayBufsMode = IVIDDEC3_DISPLAYBUFS_EMBEDDED;
    ctx->codecParams->inputDataMode = IVIDEO_ENTIREFRAME;
    ctx->codecParams->outputDataMode = IVIDEO_ENTIREFRAME;
    ctx->codecParams->numInputDataUnits = 0;
    ctx->codecParams->numOutputDataUnits = 0;
    ctx->codecParams->errorInfoMode = IVIDEO_ERRORINFO_OFF;
    ctx->codecParams->metadataType[0] = IVIDEO_METADATAPLANE_NONE;
    ctx->codecParams->metadataType[1] = IVIDEO_METADATAPLANE_NONE;
    ctx->codecParams->metadataType[2] = IVIDEO_METADATAPLANE_NONE;

    switch (ctx->codecId) {
    case AV_CODEC_ID_H264:
        ctx->frameWidth = ALIGN2(ctx->frameWidth + (32 * 2), 7);
        ctx->frameHeight = ctx->frameHeight + 4 * 24;
        ctx->codecParams->size = sizeof(IH264VDEC_Params);
        dpbSizeInFrames = FFMIN(16, 184320 / ((ctx->codec->disp_w / 16) * (ctx->codec->disp_h / 16)));
        ((IH264VDEC_Params *)ctx->codecParams)->dpbSizeInFrames = dpbSizeInFrames;//IH264VDEC_DPB_NUMFRAMES_AUTO;
        ((IH264VDEC_Params *)ctx->codecParams)->pConstantMemory = 0;
        ((IH264VDEC_Params *)ctx->codecParams)->bitStreamFormat = IH264VDEC_BYTE_STREAM_FORMAT;
        ((IH264VDEC_Params *)ctx->codecParams)->errConcealmentMode = IH264VDEC_APPLY_CONCEALMENT;
        ((IH264VDEC_Params *)ctx->codecParams)->temporalDirModePred = IH264VDEC_ENABLE_TEMPORALDIRECT;
        ((IH264VDEC_Params *)ctx->codecParams)->svcExtensionFlag = IH264VDEC_DISABLE_SVCEXTENSION;
        ((IH264VDEC_Params *)ctx->codecParams)->svcTargetLayerDID = IH264VDEC_TARGET_DID_DEFAULT;
        ((IH264VDEC_Params *)ctx->codecParams)->svcTargetLayerTID = IH264VDEC_TARGET_TID_DEFAULT;
        ((IH264VDEC_Params *)ctx->codecParams)->svcTargetLayerQID = IH264VDEC_TARGET_QID_DEFAULT;
        ((IH264VDEC_Params *)ctx->codecParams)->presetLevelIdc = IH264VDEC_MAXLEVELID;
        ((IH264VDEC_Params *)ctx->codecParams)->presetProfileIdc = IH264VDEC_PROFILE_ANY;
        ((IH264VDEC_Params *)ctx->codecParams)->detectCabacAlignErr = IH264VDEC_DISABLE_CABACALIGNERR_DETECTION;
        ((IH264VDEC_Params *)ctx->codecParams)->detectIPCMAlignErr = IH264VDEC_DISABLE_IPCMALIGNERR_DETECTION;
        ((IH264VDEC_Params *)ctx->codecParams)->debugTraceLevel = IH264VDEC_DEBUGTRACE_LEVEL0; // 0 - 3
        ((IH264VDEC_Params *)ctx->codecParams)->lastNFramesToLog = 0;
        ((IH264VDEC_Params *)ctx->codecParams)->enableDualOutput = IH264VDEC_DUALOUTPUT_DISABLE;
        ((IH264VDEC_Params *)ctx->codecParams)->processCallLevel = FALSE; // TRUE - for interlace
        ((IH264VDEC_Params *)ctx->codecParams)->enableWatermark = IH264VDEC_WATERMARK_DISABLE;
        ((IH264VDEC_Params *)ctx->codecParams)->decodeFrameType = IH264VDEC_DECODE_ALL;
        ctx->codecHandle = VIDDEC3_create(ctx->codecEngine, (String)"ivahd_h264dec", ctx->codecParams);
        MP_VERBOSE(vd, "Using ivahd_h264dec\n");
        break;
    case AV_CODEC_ID_MPEG4:
        ctx->frameWidth = ALIGN2(ctx->frameWidth + 32, 7);
        ctx->frameHeight = ctx->frameHeight + 32;
        ctx->codecParams->size = sizeof(IMPEG4VDEC_Params);
        ((IMPEG4VDEC_Params *)ctx->codecParams)->outloopDeBlocking = IMPEG4VDEC_ENHANCED_DEBLOCK_ENABLE;
        ((IMPEG4VDEC_Params *)ctx->codecParams)->errorConcealmentEnable = IMPEG4VDEC_EC_ENABLE;
        ((IMPEG4VDEC_Params *)ctx->codecParams)->sorensonSparkStream = FALSE;
        ((IMPEG4VDEC_Params *)ctx->codecParams)->debugTraceLevel = IMPEG4VDEC_DEBUGTRACE_LEVEL0; // 0 - 2
        ((IMPEG4VDEC_Params *)ctx->codecParams)->lastNFramesToLog = IMPEG4VDEC_MINNUM_OF_FRAME_LOGS;
        ((IMPEG4VDEC_Params *)ctx->codecParams)->paddingMode = IMPEG4VDEC_MPEG4_MODE_PADDING;//IMPEG4VDEC_DIVX_MODE_PADDING;
        ((IMPEG4VDEC_Params *)ctx->codecParams)->enhancedDeBlockingQp = 15; // 1 - 31
        ((IMPEG4VDEC_Params *)ctx->codecParams)->decodeOnlyIntraFrames = IMPEG4VDEC_DECODE_ONLY_I_FRAMES_DISABLE;
        ctx->codecHandle = VIDDEC3_create(ctx->codecEngine, (String)"ivahd_mpeg4dec", ctx->codecParams);
        MP_VERBOSE(vd, "Using ivahd_mpeg4dec\n");
        break;
    case AV_CODEC_ID_MPEG1VIDEO:
    case AV_CODEC_ID_MPEG2VIDEO:
        ctx->codecParams->size = sizeof(IMPEG2VDEC_Params);
        ((IMPEG2VDEC_Params *)ctx->codecParams)->ErrorConcealmentON = IMPEG2VDEC_EC_DISABLE; // IMPEG2VDEC_EC_ENABLE
        ((IMPEG2VDEC_Params *)ctx->codecParams)->outloopDeBlocking = IMPEG2VDEC_DEBLOCK_ENABLE;
        ((IMPEG2VDEC_Params *)ctx->codecParams)->debugTraceLevel = 0; // 0 - 4
        ((IMPEG2VDEC_Params *)ctx->codecParams)->lastNFramesToLog = 0;
        ctx->codecHandle = VIDDEC3_create(ctx->codecEngine, (String)"ivahd_mpeg2vdec", ctx->codecParams);
        MP_VERBOSE(vd, "Using ivahd_mpeg2vdec\n");
        break;
    case AV_CODEC_ID_WMV3:
    case AV_CODEC_ID_VC1:
        ctx->frameWidth = ALIGN2(ctx->frameWidth + (32 * 2), 7);
        ctx->frameHeight = (ALIGN2(ctx->frameHeight / 2, 4) * 2) + 2 * 40;
        ctx->codecParams->size = sizeof(IVC1VDEC_Params);
        ((IVC1VDEC_Params *)ctx->codecParams)->errorConcealmentON = TRUE;
        ((IVC1VDEC_Params *)ctx->codecParams)->frameLayerDataPresentFlag = FALSE;
        ((IVC1VDEC_Params *)ctx->codecParams)->debugTraceLevel = 0; // 0 - 4
        ((IVC1VDEC_Params *)ctx->codecParams)->lastNFramesToLog = 0;
        ctx->codecHandle = VIDDEC3_create(ctx->codecEngine, (String)"ivahd_vc1vdec", ctx->codecParams);
        MP_VERBOSE(vd, "Using ivahd_vc1dec\n");
        break;
    default:
        MP_ERR(vd, "Unsupported codec %d\n", ctx->codecId);
        goto fail;
    }

    if (!ctx->codecHandle) {
        MP_ERR(vd, "Error: VIDDEC3_create() failed\n");
        goto fail;
    }

    ctx->codecStatus = (VIDDEC3_Status *)dce_alloc(sizeof(VIDDEC3_Status));
    ctx->codecDynParams = (VIDDEC3_DynamicParams *)dce_alloc(sizeof(VIDDEC3_DynamicParams));
    ctx->codecInputBufs = (XDM2_BufDesc *)dce_alloc(sizeof(XDM2_BufDesc));
    ctx->codecOutputBufs = (XDM2_BufDesc *)dce_alloc(sizeof(XDM2_BufDesc));
    ctx->codecInputArgs = (VIDDEC3_InArgs *)dce_alloc(sizeof(VIDDEC3_InArgs));
    ctx->codecOutputArgs = (VIDDEC3_OutArgs *)dce_alloc(sizeof(VIDDEC3_OutArgs));
    if (!ctx->codecDynParams || !ctx->codecStatus || !ctx->codecInputBufs || !ctx->codecOutputBufs || !ctx->codecInputArgs || !ctx->codecOutputArgs) {
        MP_ERR(vd, "Failed allocation with dce_alloc()\n");
        goto fail;
    }

    ctx->codecDynParams->size = sizeof(VIDDEC3_DynamicParams);
    ctx->codecDynParams->decodeHeader = XDM_DECODE_AU;
    ctx->codecDynParams->displayWidth = 0;
    ctx->codecDynParams->frameSkipMode = IVIDEO_NO_SKIP;
    ctx->codecDynParams->newFrameFlag = XDAS_TRUE;
    ctx->codecDynParams->lateAcquireArg = 0;
    if (ctx->codecId == AV_CODEC_ID_MPEG4 || ctx->codecId == AV_CODEC_ID_VC1 || ctx->codecId == AV_CODEC_ID_WMV3) {
        ctx->codecDynParams->lateAcquireArg = -1;
    }
    if (ctx->codecId == AV_CODEC_ID_H264) {
        ((IH264VDEC_DynamicParams *)ctx->codecDynParams)->deblockFilterMode = IH264VDEC_DEBLOCK_DEFAULT;
    }

    ctx->codecStatus->size = sizeof(VIDDEC3_Status);
    ctx->codecInputArgs->size = sizeof(VIDDEC3_InArgs);
    ctx->codecOutputArgs->size = sizeof(VIDDEC3_OutArgs);

    codecError = VIDDEC3_control(ctx->codecHandle, XDM_SETPARAMS, ctx->codecDynParams, ctx->codecStatus);
    if (codecError != VIDDEC3_EOK) {
        MP_ERR(vd, "VIDDEC3_control(XDM_SETPARAMS) failed: %d\n", codecError);
        goto fail;
    }
    codecError = VIDDEC3_control(ctx->codecHandle, XDM_GETBUFINFO, ctx->codecDynParams, ctx->codecStatus);
    if (codecError != VIDDEC3_EOK) {
        MP_ERR(vd, "VIDDEC3_control(XDM_GETBUFINFO) failed %d\n", codecError);
        goto fail;
    }

    creq.height = (uint32_t)ctx->frameHeight;
    creq.width = (uint32_t)ctx->frameWidth;
    creq.bpp = 8;
    if (drmIoctl(ctx->vo->drm->fd, DRM_IOCTL_MODE_CREATE_DUMB, &creq) < 0) {
        MP_ERR(vd, "Failed create input buffer: %s\n", strerror(errno));
        goto fail;
    }

    ctx->inputBufHandle = creq.handle;

    mreq.handle = creq.handle;
    if (drmIoctl(ctx->vo->drm->fd, DRM_IOCTL_MODE_MAP_DUMB, &mreq)) {
        MP_ERR(vd, "Cannot map dumb buffer: %s\n", strerror(errno));
        goto fail;
    }

    ctx->inputBufPtr = mmap(0, creq.size, PROT_READ | PROT_WRITE, MAP_SHARED, ctx->vo->drm->fd, mreq.offset);
    if (ctx->inputBufPtr == MAP_FAILED) {
        MP_ERR(vd, "Cannot map dumb buffer: %s\n", strerror(errno));
        goto fail;
    }

    ctx->inputBufSize = creq.size;

    dreq.handle = ctx->inputBufHandle;
    dreq.flags = DRM_CLOEXEC;
    if (drmIoctl(ctx->vo->drm->fd, DRM_IOCTL_PRIME_HANDLE_TO_FD, &dreq) < 0) {
        MP_ERR(vd, "Cannot DMA buffer: %s\n", strerror(errno));
        goto fail;
    }

    ctx->codecInputBufs->numBufs = 1;
    ctx->codecInputBufs->descs[0].memType = XDM_MEMTYPE_RAW;
    ctx->codecInputBufs->descs[0].buf = (XDAS_Int8 *)dreq.fd;
    ctx->codecInputBufs->descs[0].bufSize.bytes = creq.size;
    dce_buf_lock(1, (size_t *)&(ctx->codecInputBufs->descs[0].buf));

    ctx->codecOutputBufs->numBufs = 2;
    ctx->codecOutputBufs->descs[0].memType = XDM_MEMTYPE_RAW;
    ctx->codecOutputBufs->descs[0].bufSize.bytes = ctx->frameWidth * ctx->frameHeight;
    ctx->codecOutputBufs->descs[1].memType = XDM_MEMTYPE_RAW;
    ctx->codecOutputBufs->descs[1].bufSize.bytes = ctx->frameWidth * (ctx->frameHeight / 2);

    ctx->frameBuffers = (struct framebuffer **)calloc(ctx->numFrameBuffers, sizeof(struct framebuffer *));
    ctx->bufferedPts = (struct vd_buffered_pts *)calloc(ctx->numFrameBuffers, sizeof(struct vd_buffered_pts));
    for (i = 0; i < ctx->numFrameBuffers; i++) {
        ctx->frameBuffers[i] = ctx->vo->driver->alloc_buffer(ctx->vo, IMGFMT_NV12, ctx->frameWidth, ctx->frameHeight);
        if (!ctx->frameBuffers[i]) {
            MP_ERR(vd, "Failed create output buffer\n");
            goto fail;
        }
        ctx->frameBuffers[i]->index = i;
        ctx->frameBuffers[i]->locked = false;
        ctx->frameBuffers[i]->free = true;

        ctx->bufferedPts[i].pts = MP_NOPTS_VALUE;
    }

    ctx->flushed = true;

    return true;

fail:

    uninit(vd);

    return false;
}

static void uninit(struct mp_filter *vd)
{
    vd_omap_dce_ctx *ctx = vd->priv;
    int i;

    if (ctx->frameBuffers) {
        for (i = 0; i < ctx->numFrameBuffers; i++) {
            if (ctx->frameBuffers[i]) {
                ctx->vo->driver->release_buffer(ctx->vo, ctx->frameBuffers[i]);
            }
        }
        free(ctx->frameBuffers);
        ctx->frameBuffers = NULL;
    }

    if (ctx->codecHandle && ctx->codecDynParams && ctx->codecParams) {
        VIDDEC3_control(ctx->codecHandle, XDM_FLUSH, ctx->codecDynParams, ctx->codecStatus);
    }

    if (ctx->codecHandle) {
        VIDDEC3_delete(ctx->codecHandle);
        ctx->codecHandle = NULL;
    }
    if (ctx->codecParams) {
        dce_free(ctx->codecParams);
        ctx->codecParams = NULL;
    }
    if (ctx->codecStatus) {
        dce_free(ctx->codecStatus);
        ctx->codecStatus = NULL;
    }
    if (ctx->codecDynParams) {
        dce_free(ctx->codecDynParams);
        ctx->codecDynParams = NULL;
    }
    if (ctx->codecInputBufs) {
        dce_buf_unlock(1, (size_t *)&(ctx->codecInputBufs->descs[0].buf));
        munmap(ctx->inputBufPtr, ctx->inputBufSize);
        close((int)ctx->codecInputBufs->descs[0].buf);
        dce_free(ctx->codecInputBufs);
        ctx->codecInputBufs = NULL;
    }
    if (ctx->inputBufHandle > 0) {
        struct drm_gem_close req = {
            .handle = ctx->inputBufHandle,
        };
        drmIoctl(ctx->vo->drm->fd, DRM_IOCTL_MODE_DESTROY_DUMB, &req);
        ctx->inputBufHandle = 0;
    }
    if (ctx->codecOutputBufs) {
        dce_free(ctx->codecOutputBufs);
        ctx->codecOutputBufs = NULL;
    }
    if (ctx->codecInputArgs) {
        dce_free(ctx->codecInputArgs);
        ctx->codecInputArgs = NULL;
    }
    if (ctx->codecOutputArgs) {
        dce_free(ctx->codecOutputArgs);
        ctx->codecOutputArgs = NULL;
    }

    if (ctx->codecEngine) {
        Engine_close(ctx->codecEngine);
        ctx->codecEngine = NULL;
    }

    dce_deinit();
}

static void unlockBuffer(struct mp_filter *vd, struct framebuffer *fb) {
    vd_omap_dce_ctx *ctx = vd->priv;

    if (!fb) {
        return;
    }

    if (!ctx->frameBuffers[fb->index]->free) {
        MP_ERR(vd, "Already unlocked frame buffer at index: %d\n", fb->index);
        return;
    }

    dce_buf_unlock(1, (size_t *)&(ctx->frameBuffers[fb->index]->dma_buf));

    ctx->frameBuffers[fb->index]->free = true;
}

static void flush_all(struct mp_filter *vd)
{
    vd_omap_dce_ctx *ctx = vd->priv;
    Int32 codecError;
    int i;

    // flush codec engine
    codecError = VIDDEC3_control(ctx->codecHandle, XDM_FLUSH, ctx->codecDynParams, ctx->codecStatus);
    if (codecError != VIDDEC3_EOK) {
        MP_ERR(vd, "VIDDEC3_control(XDM_FLUSH) failed %d\n", codecError);
        return;
    }

    ctx->codecInputArgs->inputID = 0;
    ctx->codecInputArgs->numBytes = 0;
    ctx->codecInputBufs->numBufs = 0;
    ctx->codecInputBufs->descs[0].bufSize.bytes = 0;

    ctx->codecOutputBufs->numBufs = 0;

    memset(ctx->codecOutputArgs->outputID, 0, sizeof(ctx->codecOutputArgs->outputID));
    memset(ctx->codecOutputArgs->freeBufID, 0, sizeof(ctx->codecOutputArgs->freeBufID));

    do {
        codecError = VIDDEC3_process(ctx->codecHandle, ctx->codecInputBufs, ctx->codecOutputBufs,
                                     ctx->codecInputArgs, ctx->codecOutputArgs);
        if (codecError == DCE_EXDM_FAIL) {
            if (XDM_ISFATALERROR(ctx->codecOutputArgs->extendedError)) {
                if (ctx->codecId == AV_CODEC_ID_MPEG1VIDEO || ctx->codecId == AV_CODEC_ID_MPEG2VIDEO) {
                } else {
                    MP_ERR(vd, "FLUSH: VIDDEC3_process() FATAL Error, extendedError: %08x\n",
                           ctx->codecOutputArgs->extendedError);
                }
                break;
            }
            if (((ctx->codecOutputArgs->extendedError >> IH264VDEC_ERR_STREAM_END) & 0x1) &&
                (ctx->codecId == AV_CODEC_ID_H264)) {
                    break;
            }
            if (((ctx->codecOutputArgs->extendedError >> IMPEG4D_ERR_STREAM_END) & 0x1) &&
                (ctx->codecId == AV_CODEC_ID_MPEG4)) {
                    break;
            }
        }
        for (i = 0; ctx->codecOutputArgs->freeBufID[i]; i++) {
            struct framebuffer *f = (struct framebuffer *)ctx->codecOutputArgs->freeBufID[i];
            dce_buf_unlock(1, (size_t *)&(f->dma_buf));
            f->free = true;
            f->locked = false;
        }
    } while (codecError != XDM_EFAIL);

    ctx->vo->driver->control(ctx->vo, VOCTRL_RESET, NULL);

    for (int j = 0; j < ctx->numFrameBuffers; j++) {
       if (ctx->frameBuffers[j]->locked)
           ctx->frameBuffers[j]->locked = false;
       ctx->frameBuffers[j]->ref_count = 0;
    }

    memset(ctx->codecOutputArgs->outputID, 0, sizeof(ctx->codecOutputArgs->outputID));
    memset(ctx->codecOutputArgs->freeBufID, 0, sizeof(ctx->codecOutputArgs->freeBufID));

    for (int j = 0; j < ctx->numFrameBuffers; j++) {
        ctx->bufferedPts[j].pts = MP_NOPTS_VALUE;
        ctx->bufferedPts[j].dts = MP_NOPTS_VALUE;
        ctx->bufferedPts[j].duration = 0;
    }
}

static int control(struct mp_filter *vd, enum dec_ctrl cmd, void *arg)
{
    vd_omap_dce_ctx *ctx = vd->priv;

    switch (cmd) {
    case VDCTRL_GET_HWDEC:
        *(char **)arg = NULL;
        return CONTROL_TRUE;
    case VDCTRL_SET_FRAMEDROP:
        ctx->framedrop_flags = *(int *)arg;
        return CONTROL_TRUE;
    case VDCTRL_CHECK_FORCED_EOF:
        *(bool *)arg = ctx->force_eof;
        return CONTROL_TRUE;
    case VDCTRL_GET_BFRAMES:
        return CONTROL_UNKNOWN;
    case VDCTRL_FORCE_HWDEC_FALLBACK:
        return CONTROL_UNKNOWN;
    case VDCTRL_REINIT:
        return CONTROL_UNKNOWN;
    }

    return CONTROL_UNKNOWN;
}

static int decode_packet(struct mp_filter *vd, struct demux_packet *mpkt, struct mp_frame *out_frame)
{
    vd_omap_dce_ctx *ctx = vd->priv;
    struct framebuffer *fb = NULL;
    Int32 codecError;
    XDM_Rect *r;
    char error_str[1024];
    int i;

    if (mpkt->len == 0)
        return -1;

    for (i = 0; i < ctx->numFrameBuffers; i++) {
        if (ctx->frameBuffers[i]->free && !ctx->frameBuffers[i]->locked) {
            dce_buf_lock(1, (size_t *)&(ctx->frameBuffers[i]->dma_buf));
            ctx->frameBuffers[i]->free = false;
            ctx->frameBuffers[i]->ref_count = 0;
            fb = ctx->frameBuffers[i];
            break;
        }
    }
    if (!fb) {
        MP_ERR(vd, "Failed get video buffer\n");
        return AVERROR(EAGAIN);
    }

    bool pts_pushed = false;
    for (int j = 0; j < ctx->numFrameBuffers; j++) {
        if (ctx->bufferedPts[j].pts != MP_NOPTS_VALUE)
            continue;
        if (mpkt->pts == MP_NOPTS_VALUE)
            ctx->bufferedPts[j].pts = mpkt->dts;
        else
            ctx->bufferedPts[j].pts = mpkt->pts;
        ctx->bufferedPts[j].dts = mpkt->dts;
        ctx->bufferedPts[j].duration = mpkt->duration;
        pts_pushed = true;
        break;
    }
    if (!pts_pushed) {
        MP_ERR(vd, "Failed push pts to array\n");
        ctx->force_eof = true;
        return -1;
    }

    XDAS_Int32 frameSkipMode;
    int drop = ctx->framedrop_flags;
    if (drop == 1) {
        frameSkipMode = IVIDEO_SKIP_NONREFERENCE;   // normal framedrop
    } else if (drop == 2) {
        frameSkipMode = IVIDEO_SKIP_I;     // hr-seek framedrop
    } else {
        frameSkipMode = IVIDEO_NO_SKIP;    // normal playback
    }

    ctx->codecDynParams->frameSkipMode = frameSkipMode;
    codecError = VIDDEC3_control(ctx->codecHandle, XDM_SETPARAMS, ctx->codecDynParams, ctx->codecStatus);
    if (codecError != VIDDEC3_EOK) {
        MP_ERR(vd, "VIDDEC3_control(XDM_SETPARAMS) failed: %d\n", codecError);
        ctx->force_eof = true;
        unlockBuffer(vd, fb);
        return -1;
    }
    codecError = VIDDEC3_control(ctx->codecHandle, XDM_GETBUFINFO, ctx->codecDynParams, ctx->codecStatus);
    if (codecError != VIDDEC3_EOK) {
        MP_ERR(vd, "VIDDEC3_control(XDM_GETBUFINFO) failed %d\n", codecError);
        ctx->force_eof = true;
        unlockBuffer(vd, fb);
        return -1;
    }

    memcpy(ctx->inputBufPtr, mpkt->buffer, mpkt->len);

    ctx->codecInputArgs->inputID = (XDAS_Int32)fb;
    ctx->codecInputArgs->numBytes = mpkt->len;

    ctx->codecInputBufs->numBufs = 1;
    ctx->codecInputBufs->descs[0].bufSize.bytes = mpkt->len;

    ctx->codecOutputBufs->numBufs = 2;
    ctx->codecOutputBufs->descs[0].buf = (XDAS_Int8 *)fb->dma_buf;
    ctx->codecOutputBufs->descs[1].buf = (XDAS_Int8 *)fb->dma_buf;

    memset(ctx->codecOutputArgs->outputID, 0, sizeof(ctx->codecOutputArgs->outputID));
    memset(ctx->codecOutputArgs->freeBufID, 0, sizeof(ctx->codecOutputArgs->freeBufID));

    codecError = VIDDEC3_process(ctx->codecHandle, ctx->codecInputBufs, ctx->codecOutputBufs, ctx->codecInputArgs, ctx->codecOutputArgs);
    if (codecError == DCE_EXDM_FAIL) {
        if (XDM_ISFATALERROR(ctx->codecOutputArgs->extendedError)) {
            decode_codec_error(ctx->codecId, ctx->codecOutputArgs->extendedError, error_str, sizeof(error_str));
            MP_ERR(vd, "VIDDEC3_process() FATAL Error, extendedError: %08x, %s\n",
                   ctx->codecOutputArgs->extendedError, error_str);
            unlockBuffer(vd, fb);
            ctx->force_eof = true;
            return -1;
        } else {
            if ((ctx->codecId == AV_CODEC_ID_H264) &&
                ((ctx->codecOutputArgs->extendedError >> IH264VDEC_ERR_UNAVAILABLESPS) & 0x1) &&
                ((ctx->codecOutputArgs->extendedError >> XDM_CORRUPTEDHEADER) & 0x1)) {
            } else if ((ctx->codecId == AV_CODEC_ID_MPEG1VIDEO || ctx->codecId == AV_CODEC_ID_MPEG2VIDEO) &&
                ((ctx->codecOutputArgs->extendedError >> IMPEG2VDEC_ERR_TRICK_MODE) & 0x1)) {
            } else if ((ctx->codecId == AV_CODEC_ID_MPEG4) &&
                ((ctx->codecOutputArgs->extendedError >> XDM_CORRUPTEDHEADER) & 0x1)) {
            } else {
                decode_codec_error(ctx->codecId, ctx->codecOutputArgs->extendedError, error_str, sizeof(error_str));
                MP_WARN(vd, "VIDDEC3_process() decode extendedError: %08x, %s\n",
                        ctx->codecOutputArgs->extendedError, error_str);
            }
        }
    } else if ((codecError == DCE_EXDM_UNSUPPORTED) ||
               (codecError == DCE_EIPC_CALL_FAIL) ||
               (codecError == DCE_EINVALID_INPUT)) {
        MP_ERR(vd, "VIDDEC3_process() Fatal Error\n");
        unlockBuffer(vd, fb);
        ctx->force_eof = true;
        return -1;
    }

    ctx->flushed = false;

    if (ctx->codecOutputArgs->outBufsInUseFlag) {
        MP_WARN(vd, "VIDDEC3_process() status: outBufsInUseFlag\n");
    }

    for (i = 0; ctx->codecOutputArgs->freeBufID[i]; i++) {
        struct framebuffer *f = (struct framebuffer *)ctx->codecOutputArgs->freeBufID[i];
        dce_buf_unlock(1, (size_t *)&(f->dma_buf));
        f->free = true;
    }

    int outIndex = -1;
    for (i = 0; ctx->codecOutputArgs->outputID[i]; i++) {
        struct framebuffer *f = (struct framebuffer *)ctx->codecOutputArgs->outputID[i];
        f->locked = true;
        outIndex = i;
        break;
    }
    if (outIndex == -1) {
        *out_frame = MP_NO_FRAME;
        return 0;
    }

    r = &ctx->codecOutputArgs->displayBufs.bufDesc[0].activeFrameRegion;

    fb = (struct framebuffer *)ctx->codecOutputArgs->outputID[outIndex];

    struct mp_image *mpi = talloc_zero(NULL, struct mp_image);
    mp_image_setfmt(mpi, IMGFMT_NV12);
    mp_image_set_size(mpi, r->bottomRight.x - r->topLeft.x, r->bottomRight.y - r->topLeft.y);
    mpi->x0 = r->topLeft.x;
    mpi->y0 = r->topLeft.y;
    mpi->x1 = r->bottomRight.x;
    mpi->y1 = r->bottomRight.y;
    mpi->priv = (void *)fb;
    mpi->hw_surf = true;
    mpi->planes[0] = fb->map;
    mpi->stride[0] = fb->stride;
    mpi->planes[1] = fb->map + fb->width * fb->height;
    mpi->stride[1] = fb->stride;

    int found_pts_index = -1;
    double min_pts = -MP_NOPTS_VALUE;
    for (int j = 0; j < ctx->numFrameBuffers; j++) {
        if (ctx->bufferedPts[j].pts == MP_NOPTS_VALUE)
            continue;
        if (ctx->bufferedPts[j].pts < min_pts) {
            min_pts = ctx->bufferedPts[j].pts;
            found_pts_index = j;
        }
    }
    if (found_pts_index == -1) {
        mpi->pts = MP_NOPTS_VALUE;
        mpi->dts = MP_NOPTS_VALUE;
        mpi->pkt_duration = 0.0;
    } else {
        mpi->pts = ctx->bufferedPts[found_pts_index].pts;
        mpi->dts = ctx->bufferedPts[found_pts_index].dts;
        mpi->pkt_duration = ctx->bufferedPts[found_pts_index].duration;
        ctx->bufferedPts[found_pts_index].pts = MP_NOPTS_VALUE;
    }

    if (ctx->codecOutputArgs->displayBufs.bufDesc[0].contentType == IVIDEO_INTERLACED_TOPFIELD) {
        MP_WARN(vd, "IVIDEO_INTERLACED_TOPFIELD\n");
    }
    if (ctx->codecOutputArgs->displayBufs.bufDesc[0].contentType == IVIDEO_INTERLACED_BOTTOMFIELD) {
        MP_WARN(vd, "IVIDEO_INTERLACED_BOTTOMFIELD\n");
    }
    if (ctx->codecOutputArgs->displayBufs.bufDesc[0].topFieldFirstFlag) {
    }
    if (ctx->codecOutputArgs->displayBufs.bufDesc[0].repeatFirstFieldFlag) {
        MP_WARN(vd, "repeatFirstFieldFlag\n");
    }

    *out_frame = MAKE_FRAME(MP_FRAME_VIDEO, mpi);

    return 0;
}

static void process(struct mp_filter *vd)
{
    if (!mp_pin_in_needs_data(vd->ppins[1]))
        return;

    struct mp_frame frame = {0};
    struct demux_packet *pkt = NULL;

    frame = mp_pin_out_read(vd->ppins[0]);
    if (frame.type == MP_FRAME_PACKET) {
        pkt = frame.data;
    } else if (frame.type == MP_FRAME_EOF) {
        talloc_free(pkt);
        mp_pin_in_write(vd->ppins[1], MP_EOF_FRAME);
        return;
    } else {
        if (frame.type) {
            MP_ERR(vd, "unexpected frame type\n");
            mp_frame_unref(&frame);
            mp_filter_internal_mark_failed(vd);
        }
        return;
    }

    int ret = decode_packet(vd, pkt, &frame);
    if (ret == AVERROR(EAGAIN)) {
        mp_frame_unref(&frame);
        mp_filter_internal_mark_failed(vd);
        return;
    } else if (ret < 0) {
        MP_ERR(vd, "decoding frame error\n");
        mp_frame_unref(&frame);
        mp_filter_internal_mark_failed(vd);
        return;
    } else {
        if (frame.type != MP_FRAME_NONE)
            mp_pin_in_write(vd->ppins[1], frame);
    }

    talloc_free(pkt);
    mp_filter_internal_mark_progress(vd);
}

static void reset(struct mp_filter *vd)
{
    vd_omap_dce_ctx *ctx = vd->priv;

    if (!ctx->flushed) {
        flush_all(vd);
        ctx->flushed = true;
    }

    ctx->framedrop_flags = 0;
}

static void destroy(struct mp_filter *vd)
{
    vd_omap_dce_ctx *ctx = vd->priv;

    ctx->vo->driver->control(ctx->vo, VOCTRL_RESET, NULL);

    uninit(vd);
}

static const struct mp_filter_info vd_omap_dce_filter = {
    .name = "vd_omap_dce",
    .priv_size = sizeof(vd_omap_dce_ctx),
    .process = process,
    .reset = reset,
    .destroy = destroy,
};

static const int codecs[] = {
    AV_CODEC_ID_H264,
    AV_CODEC_ID_MPEG4,
    AV_CODEC_ID_MPEG2VIDEO,
    AV_CODEC_ID_MPEG1VIDEO,
    AV_CODEC_ID_VC1,
    AV_CODEC_ID_WMV3,
    AV_CODEC_ID_NONE
};

static bool find_codec(const char *name)
{
    for (int n = 0; codecs[n] != AV_CODEC_ID_NONE; n++) {
        const char *format = mp_codec_from_av_codec_id(codecs[n]);
        if (format && name && strcmp(format, name) == 0)
            return true;
    }
    return false;
}

struct mp_decoder_list *select_omap_dce_codec(const char *codec, const char *pref)
{
    struct mp_decoder_list *list = talloc_zero(NULL, struct mp_decoder_list);

    if (!find_codec(codec))
        return list;

    char name[80];
    snprintf(name, sizeof(name), "omap_dce_%s", codec);
    mp_add_decoder(list, codec, name,
                   "omap_dce video decoder");
    return list;
}

static struct mp_decoder *create(struct mp_filter *parent,
                                 struct mp_codec_params *codec,
                                 const char *decoder)
{
    struct mp_filter *vd = mp_filter_create(parent, &vd_omap_dce_filter);
    if (!vd)
        return NULL;

    mp_filter_add_pin(vd, MP_PIN_IN, "in");
    mp_filter_add_pin(vd, MP_PIN_OUT, "out");

    vd->log = mp_log_new(vd, parent->log, NULL);

    vd_omap_dce_ctx *ctx = vd->priv;
    struct mp_stream_info *info = mp_filter_find_stream_info(vd);
    if (!info)
        return NULL;
    ctx->vo = info->dr_vo;
    ctx->log = vd->log;
    ctx->opts_cache = m_config_cache_alloc(ctx, vd->global, &vd_omap_dce_conf);
    ctx->opts = ctx->opts_cache->opts;
    ctx->codec = codec;
    ctx->decoder = talloc_strdup(ctx, decoder);

    ctx->public.f = vd;
    ctx->public.control = control;

    if (!init(vd)) {
        return NULL;
    }

    ctx->vo->hwdec = true;

    return &ctx->public;
}

static void add_decoders(struct mp_decoder_list *list)
{
    for (int n = 0; codecs[n] != AV_CODEC_ID_NONE; n++) {
        const AVCodec *codec = avcodec_find_decoder(codecs[n]);
        const char *format = mp_codec_from_av_codec_id(codecs[n]);
        if (format && codec)
            mp_add_decoder(list, format,
                           codec->name, codec->long_name);
    }
}

const struct mp_decoder_fns vd_omap_dce = {
    .create = create,
    .add_decoders = add_decoders,
};
