/*
Copyright (c) 2012, The Linux Foundation. All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions are
met:
    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above
      copyright notice, this list of conditions and the following
      disclaimer in the documentation and/or other materials provided
      with the distribution.
    * Neither the name of Code Aurora Forum, Inc. nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NON-INFRINGEMENT
ARE DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS
BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE
OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN
IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <pthread.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <poll.h>
#include <time.h>
#include <semaphore.h>
#include <media/msm_media_info.h>

#include "mm_camera_dbg.h"
#include "mm_camera_interface.h"
#include "mm_camera.h"

/* internal function decalre */
int32_t mm_stream_qbuf(mm_stream_t *my_obj,
                       mm_camera_buf_def_t *buf);
int32_t mm_stream_set_ext_mode(mm_stream_t * my_obj);
int32_t mm_stream_set_fmt(mm_stream_t * my_obj);
int32_t mm_stream_sync_info(mm_stream_t *my_obj);
int32_t mm_stream_init_bufs(mm_stream_t * my_obj);
int32_t mm_stream_deinit_bufs(mm_stream_t * my_obj);
int32_t mm_stream_request_buf(mm_stream_t * my_obj);
int32_t mm_stream_unreg_buf(mm_stream_t * my_obj);
int32_t mm_stream_release(mm_stream_t *my_obj);
int32_t mm_stream_set_parm(mm_stream_t *my_obj,
                           cam_stream_parm_buffer_t *value);
int32_t mm_stream_get_parm(mm_stream_t *my_obj,
                           cam_stream_parm_buffer_t *value);
int32_t mm_stream_do_action(mm_stream_t *my_obj,
                            void *in_value);
int32_t mm_stream_streamon(mm_stream_t *my_obj);
int32_t mm_stream_streamoff(mm_stream_t *my_obj);
int32_t mm_stream_read_msm_frame(mm_stream_t * my_obj,
                                 mm_camera_buf_info_t* buf_info,
                                 uint8_t num_planes);
int32_t mm_stream_config(mm_stream_t *my_obj,
                         mm_camera_stream_config_t *config);
int32_t mm_stream_reg_buf(mm_stream_t * my_obj);
int32_t mm_stream_buf_done(mm_stream_t * my_obj,
                           mm_camera_buf_def_t *frame);
int32_t mm_stream_calc_offset(mm_stream_t *my_obj);
int32_t mm_stream_calc_offset_preview(cam_format_t fmt,
                                      cam_dimension_t *dim,
                                      cam_stream_buf_plane_info_t *buf_planes);
int32_t mm_stream_calc_offset_snapshot(cam_format_t fmt,
                                       cam_dimension_t *dim,
                                       cam_padding_info_t *padding,
                                       cam_stream_buf_plane_info_t *buf_planes);
int32_t mm_stream_calc_offset_raw(cam_format_t fmt,
                                  cam_dimension_t *dim,
                                  cam_padding_info_t *padding,
                                  cam_stream_buf_plane_info_t *buf_planes);
int32_t mm_stream_calc_offset_video(cam_dimension_t *dim,
                                    cam_stream_buf_plane_info_t *buf_planes);
int32_t mm_stream_calc_offset_metadata(cam_dimension_t *dim,
                                       cam_padding_info_t *padding,
                                       cam_stream_buf_plane_info_t *buf_planes);


/* state machine function declare */
int32_t mm_stream_fsm_inited(mm_stream_t * my_obj,
                             mm_stream_evt_type_t evt,
                             void * in_val,
                             void * out_val);
int32_t mm_stream_fsm_acquired(mm_stream_t * my_obj,
                               mm_stream_evt_type_t evt,
                               void * in_val,
                               void * out_val);
int32_t mm_stream_fsm_cfg(mm_stream_t * my_obj,
                          mm_stream_evt_type_t evt,
                          void * in_val,
                          void * out_val);
int32_t mm_stream_fsm_buffed(mm_stream_t * my_obj,
                             mm_stream_evt_type_t evt,
                             void * in_val,
                             void * out_val);
int32_t mm_stream_fsm_reg(mm_stream_t * my_obj,
                          mm_stream_evt_type_t evt,
                          void * in_val,
                          void * out_val);
int32_t mm_stream_fsm_active(mm_stream_t * my_obj,
                             mm_stream_evt_type_t evt,
                             void * in_val,
                             void * out_val);
uint32_t mm_stream_get_v4l2_fmt(cam_format_t fmt);


/*===========================================================================
 * FUNCTION   : mm_stream_handle_rcvd_buf
 *
 * DESCRIPTION: function to handle newly received stream buffer
 *
 * PARAMETERS :
 *   @cam_obj : stream object
 *   @buf_info: ptr to struct storing buffer information
 *
 * RETURN     : none
 *==========================================================================*/
void mm_stream_handle_rcvd_buf(mm_stream_t *my_obj,
                               mm_camera_buf_info_t *buf_info)
{
    int32_t i;
    uint8_t has_cb = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    /* enqueue to super buf thread */
    if (my_obj->is_bundled) {
        mm_camera_cmdcb_t* node = NULL;

        /* send sem_post to wake up channel cmd thread to enqueue to super buffer */
        node = (mm_camera_cmdcb_t *)malloc(sizeof(mm_camera_cmdcb_t));
        if (NULL != node) {
            memset(node, 0, sizeof(mm_camera_cmdcb_t));
            node->cmd_type = MM_CAMERA_CMD_TYPE_DATA_CB;
            node->u.buf = *buf_info;

            /* enqueue to cmd thread */
            cam_queue_enq(&(my_obj->ch_obj->cmd_thread.cmd_queue), node);

            /* wake up cmd thread */
            sem_post(&(my_obj->ch_obj->cmd_thread.cmd_sem));
        } else {
            CDBG_ERROR("%s: No memory for mm_camera_node_t", __func__);
        }
    }

    /* check if has CB */
    for (i=0 ; i< MM_CAMERA_STREAM_BUF_CB_MAX; i++) {
        if(NULL != my_obj->buf_cb[i].cb) {
            has_cb = 1;
        }
    }
    if(has_cb) {
        mm_camera_cmdcb_t* node = NULL;

        /* send sem_post to wake up cmd thread to dispatch dataCB */
        node = (mm_camera_cmdcb_t *)malloc(sizeof(mm_camera_cmdcb_t));
        if (NULL != node) {
            memset(node, 0, sizeof(mm_camera_cmdcb_t));
            node->cmd_type = MM_CAMERA_CMD_TYPE_DATA_CB;
            node->u.buf = *buf_info;

            /* enqueue to cmd thread */
            cam_queue_enq(&(my_obj->cmd_thread.cmd_queue), node);

            /* wake up cmd thread */
            sem_post(&(my_obj->cmd_thread.cmd_sem));
        } else {
            CDBG_ERROR("%s: No memory for mm_camera_node_t", __func__);
        }
    }
}

/*===========================================================================
 * FUNCTION   : mm_stream_data_notify
 *
 * DESCRIPTION: callback to handle data notify from kernel
 *
 * PARAMETERS :
 *   @user_data : user data ptr (stream object)
 *
 * RETURN     : none
 *==========================================================================*/
static void mm_stream_data_notify(void* user_data)
{
    mm_stream_t *my_obj = (mm_stream_t*)user_data;
    int32_t idx = -1, i, rc;
    uint8_t has_cb = 0;
    mm_camera_buf_info_t buf_info;

    if (NULL == my_obj) {
        return;
    }

    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    if (MM_STREAM_STATE_ACTIVE != my_obj->state) {
        /* this Cb will only received in active_stream_on state
         * if not so, return here */
        CDBG_ERROR("%s: ERROR!! Wrong state (%d) to receive data notify!",
                   __func__, my_obj->state);
        return;
    }

    memset(&buf_info, 0, sizeof(mm_camera_buf_info_t));

    pthread_mutex_lock(&my_obj->buf_lock);
    rc = mm_stream_read_msm_frame(my_obj, &buf_info, my_obj->frame_offset.num_planes);
    if (rc != 0) {
        pthread_mutex_unlock(&my_obj->buf_lock);
        return;
    }
    idx = buf_info.buf->buf_idx;

    /* update buffer location */
    my_obj->buf_status[idx].in_kernel = 0;

    /* update buf ref count */
    if (my_obj->is_bundled) {
        /* need to add into super buf since bundled, add ref count */
        my_obj->buf_status[idx].buf_refcnt++;
    }

    for (i=0; i < MM_CAMERA_STREAM_BUF_CB_MAX; i++) {
        if(NULL != my_obj->buf_cb[i].cb) {
            /* for every CB, add ref count */
            my_obj->buf_status[idx].buf_refcnt++;
            has_cb = 1;
        }
    }
    pthread_mutex_unlock(&my_obj->buf_lock);

    mm_stream_handle_rcvd_buf(my_obj, &buf_info);
}

/*===========================================================================
 * FUNCTION   : mm_stream_dispatch_app_data
 *
 * DESCRIPTION: dispatch stream buffer to registered users
 *
 * PARAMETERS :
 *   @cmd_cb  : ptr storing stream buffer information
 *   @userdata: user data ptr (stream object)
 *
 * RETURN     : none
 *==========================================================================*/
static void mm_stream_dispatch_app_data(mm_camera_cmdcb_t *cmd_cb,
                                        void* user_data)
{
    int i;
    mm_stream_t * my_obj = (mm_stream_t *)user_data;
    mm_camera_buf_info_t* buf_info = NULL;
    mm_camera_super_buf_t super_buf;

    if (NULL == my_obj) {
        return;
    }
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    if (MM_CAMERA_CMD_TYPE_DATA_CB != cmd_cb->cmd_type) {
        CDBG_ERROR("%s: Wrong cmd_type (%d) for dataCB",
                   __func__, cmd_cb->cmd_type);
        return;
    }

    buf_info = &cmd_cb->u.buf;
    memset(&super_buf, 0, sizeof(mm_camera_super_buf_t));
    super_buf.num_bufs = 1;
    super_buf.bufs[0] = buf_info->buf;
    super_buf.camera_handle = my_obj->ch_obj->cam_obj->my_hdl;
    super_buf.ch_id = my_obj->ch_obj->my_hdl;


    pthread_mutex_lock(&my_obj->cb_lock);
    for(i = 0; i < MM_CAMERA_STREAM_BUF_CB_MAX; i++) {
        if(NULL != my_obj->buf_cb[i].cb) {
            if (my_obj->buf_cb[i].cb_count != 0) {
                /* if <0, means infinite CB
                 * if >0, means CB for certain times
                 * both case we need to call CB */
                my_obj->buf_cb[i].cb(&super_buf,
                                     my_obj->buf_cb[i].user_data);
            }

            /* if >0, reduce count by 1 every time we called CB until reaches 0
             * when count reach 0, reset the buf_cb to have no CB */
            if (my_obj->buf_cb[i].cb_count > 0) {
                my_obj->buf_cb[i].cb_count--;
                if (0 == my_obj->buf_cb[i].cb_count) {
                    my_obj->buf_cb[i].cb = NULL;
                    my_obj->buf_cb[i].user_data = NULL;
                }
            }
        }
    }
    pthread_mutex_unlock(&my_obj->cb_lock);
}

/*===========================================================================
 * FUNCTION   : mm_stream_fsm_fn
 *
 * DESCRIPTION: stream finite state machine entry function. Depends on stream
 *              state, incoming event will be handled differently.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a stream object
 *   @evt      : stream event to be processed
 *   @in_val   : input event payload. Can be NULL if not needed.
 *   @out_val  : output payload, Can be NULL if not needed.
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_fsm_fn(mm_stream_t *my_obj,
                         mm_stream_evt_type_t evt,
                         void * in_val,
                         void * out_val)
{
    int32_t rc = -1;

    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    switch (my_obj->state) {
    case MM_STREAM_STATE_NOTUSED:
        CDBG("%s: Not handling evt in unused state", __func__);
        break;
    case MM_STREAM_STATE_INITED:
        rc = mm_stream_fsm_inited(my_obj, evt, in_val, out_val);
        break;
    case MM_STREAM_STATE_ACQUIRED:
        rc = mm_stream_fsm_acquired(my_obj, evt, in_val, out_val);
        break;
    case MM_STREAM_STATE_CFG:
        rc = mm_stream_fsm_cfg(my_obj, evt, in_val, out_val);
        break;
    case MM_STREAM_STATE_BUFFED:
        rc = mm_stream_fsm_buffed(my_obj, evt, in_val, out_val);
        break;
    case MM_STREAM_STATE_REG:
        rc = mm_stream_fsm_reg(my_obj, evt, in_val, out_val);
        break;
    case MM_STREAM_STATE_ACTIVE:
        rc = mm_stream_fsm_active(my_obj, evt, in_val, out_val);
        break;
    default:
        CDBG("%s: Not a valid state (%d)", __func__, my_obj->state);
        break;
    }
    CDBG("%s : X rc =%d",__func__,rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_fsm_inited
 *
 * DESCRIPTION: stream finite state machine function to handle event in INITED
 *              state.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a stream object
 *   @evt      : stream event to be processed
 *   @in_val   : input event payload. Can be NULL if not needed.
 *   @out_val  : output payload, Can be NULL if not needed.
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_fsm_inited(mm_stream_t *my_obj,
                             mm_stream_evt_type_t evt,
                             void * in_val,
                             void * out_val)
{
    int32_t rc = 0;
    char dev_name[MM_CAMERA_DEV_NAME_LEN];

    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    switch(evt) {
    case MM_STREAM_EVT_ACQUIRE:
        if ((NULL == my_obj->ch_obj) || (NULL == my_obj->ch_obj->cam_obj)) {
            CDBG_ERROR("%s: NULL channel or camera obj\n", __func__);
            rc = -1;
            break;
        }

        snprintf(dev_name, sizeof(dev_name), "/dev/%s",
                 mm_camera_util_get_dev_name(my_obj->ch_obj->cam_obj->my_hdl));

        my_obj->fd = open(dev_name, O_RDWR | O_NONBLOCK);
        if (my_obj->fd <= 0) {
            CDBG_ERROR("%s: open dev returned %d\n", __func__, my_obj->fd);
            rc = -1;
            break;
        }
        CDBG("%s: open dev fd = %d\n", __func__, my_obj->fd);
        rc = mm_stream_set_ext_mode(my_obj);
        if (0 == rc) {
            my_obj->state = MM_STREAM_STATE_ACQUIRED;
        } else {
            /* failed setting ext_mode
             * close fd */
            if(my_obj->fd > 0) {
                close(my_obj->fd);
                my_obj->fd = 0;
            }
            break;
        }
        break;
    default:
        CDBG_ERROR("%s: invalid state (%d) for evt (%d), in(%p), out(%p)",
                   __func__, my_obj->state, evt, in_val, out_val);
        break;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_fsm_acquired
 *
 * DESCRIPTION: stream finite state machine function to handle event in AQUIRED
 *              state.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a stream object
 *   @evt      : stream event to be processed
 *   @in_val   : input event payload. Can be NULL if not needed.
 *   @out_val  : output payload, Can be NULL if not needed.
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_fsm_acquired(mm_stream_t *my_obj,
                               mm_stream_evt_type_t evt,
                               void * in_val,
                               void * out_val)
{
    int32_t rc = 0;

    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    switch(evt) {
    case MM_STREAM_EVT_SET_FMT:
        {
            mm_camera_stream_config_t *config =
                (mm_camera_stream_config_t *)in_val;

            rc = mm_stream_config(my_obj, config);

            /* change state to configed */
            my_obj->state = MM_STREAM_STATE_CFG;

            break;
        }
    case MM_STREAM_EVT_RELEASE:
        rc = mm_stream_release(my_obj);
        /* change state to not used */
         my_obj->state = MM_STREAM_STATE_NOTUSED;
        break;
    case MM_STREAM_EVT_SET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_set_parm(my_obj, payload->parms);
        }
        break;
    case MM_STREAM_EVT_GET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_get_parm(my_obj, payload->parms);
        }
        break;
    default:
        CDBG_ERROR("%s: invalid state (%d) for evt (%d), in(%p), out(%p)",
                   __func__, my_obj->state, evt, in_val, out_val);
    }
    CDBG("%s :X rc = %d", __func__, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_fsm_cfg
 *
 * DESCRIPTION: stream finite state machine function to handle event in CONFIGURED
 *              state.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a stream object
 *   @evt      : stream event to be processed
 *   @in_val   : input event payload. Can be NULL if not needed.
 *   @out_val  : output payload, Can be NULL if not needed.
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_fsm_cfg(mm_stream_t * my_obj,
                          mm_stream_evt_type_t evt,
                          void * in_val,
                          void * out_val)
{
    int32_t rc = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    switch(evt) {
    case MM_STREAM_EVT_SET_FMT:
        {
            mm_camera_stream_config_t *config =
                (mm_camera_stream_config_t *)in_val;

            rc = mm_stream_config(my_obj, config);

            /* change state to configed */
            my_obj->state = MM_STREAM_STATE_CFG;

            break;
        }
    case MM_STREAM_EVT_RELEASE:
        rc = mm_stream_release(my_obj);
        my_obj->state = MM_STREAM_STATE_NOTUSED;
        break;
    case MM_STREAM_EVT_SET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_set_parm(my_obj, payload->parms);
        }
        break;
    case MM_STREAM_EVT_GET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_get_parm(my_obj, payload->parms);
        }
        break;
    case MM_STREAM_EVT_GET_BUF:
        rc = mm_stream_init_bufs(my_obj);
        /* change state to buff allocated */
        if(0 == rc) {
            my_obj->state = MM_STREAM_STATE_BUFFED;
        }
        break;
    default:
        CDBG_ERROR("%s: invalid state (%d) for evt (%d), in(%p), out(%p)",
                   __func__, my_obj->state, evt, in_val, out_val);
    }
    CDBG("%s :X rc = %d", __func__, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_fsm_buffed
 *
 * DESCRIPTION: stream finite state machine function to handle event in BUFFED
 *              state.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a stream object
 *   @evt      : stream event to be processed
 *   @in_val   : input event payload. Can be NULL if not needed.
 *   @out_val  : output payload, Can be NULL if not needed.
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_fsm_buffed(mm_stream_t * my_obj,
                             mm_stream_evt_type_t evt,
                             void * in_val,
                             void * out_val)
{
    int32_t rc = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    switch(evt) {
    case MM_STREAM_EVT_PUT_BUF:
        rc = mm_stream_deinit_bufs(my_obj);
        /* change state to configed */
        if(0 == rc) {
            my_obj->state = MM_STREAM_STATE_CFG;
        }
        break;
    case MM_STREAM_EVT_REG_BUF:
        rc = mm_stream_reg_buf(my_obj);
        /* change state to regged */
        if(0 == rc) {
            my_obj->state = MM_STREAM_STATE_REG;
        }
        break;
    case MM_STREAM_EVT_SET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_set_parm(my_obj, payload->parms);
        }
        break;
    case MM_STREAM_EVT_GET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_get_parm(my_obj, payload->parms);
        }
        break;
    default:
        CDBG_ERROR("%s: invalid state (%d) for evt (%d), in(%p), out(%p)",
                   __func__, my_obj->state, evt, in_val, out_val);
    }
    CDBG("%s :X rc = %d", __func__, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_fsm_reg
 *
 * DESCRIPTION: stream finite state machine function to handle event in REGGED
 *              state.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a stream object
 *   @evt      : stream event to be processed
 *   @in_val   : input event payload. Can be NULL if not needed.
 *   @out_val  : output payload, Can be NULL if not needed.
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_fsm_reg(mm_stream_t * my_obj,
                          mm_stream_evt_type_t evt,
                          void * in_val,
                          void * out_val)
{
    int32_t rc = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    switch(evt) {
    case MM_STREAM_EVT_UNREG_BUF:
        rc = mm_stream_unreg_buf(my_obj);

        /* change state to buffed */
        my_obj->state = MM_STREAM_STATE_BUFFED;
        break;
    case MM_STREAM_EVT_START:
        {
            uint8_t has_cb = 0;
            uint8_t i;
            /* launch cmd thread if CB is not null */
            pthread_mutex_lock(&my_obj->cb_lock);
            for (i = 0; i < MM_CAMERA_STREAM_BUF_CB_MAX; i++) {
                if(NULL != my_obj->buf_cb[i].cb) {
                    has_cb = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&my_obj->cb_lock);

            if (has_cb) {
                mm_camera_cmd_thread_launch(&my_obj->cmd_thread,
                                            mm_stream_dispatch_app_data,
                                            (void *)my_obj);
            }

            rc = mm_stream_streamon(my_obj);
            if (0 != rc) {
                /* failed stream on, need to release cmd thread if it's launched */
                if (has_cb) {
                    mm_camera_cmd_thread_release(&my_obj->cmd_thread);

                }
                break;
            }
            my_obj->state = MM_STREAM_STATE_ACTIVE;
        }
        break;
    case MM_STREAM_EVT_SET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_set_parm(my_obj, payload->parms);
        }
        break;
    case MM_STREAM_EVT_GET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_get_parm(my_obj, payload->parms);
        }
        break;
    default:
        CDBG_ERROR("%s: invalid state (%d) for evt (%d), in(%p), out(%p)",
                   __func__, my_obj->state, evt, in_val, out_val);
    }
    CDBG("%s :X rc = %d", __func__, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_fsm_active
 *
 * DESCRIPTION: stream finite state machine function to handle event in ACTIVE
 *              state.
 *
 * PARAMETERS :
 *   @my_obj   : ptr to a stream object
 *   @evt      : stream event to be processed
 *   @in_val   : input event payload. Can be NULL if not needed.
 *   @out_val  : output payload, Can be NULL if not needed.
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_fsm_active(mm_stream_t * my_obj,
                             mm_stream_evt_type_t evt,
                             void * in_val,
                             void * out_val)
{
    int32_t rc = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    switch(evt) {
    case MM_STREAM_EVT_QBUF:
        rc = mm_stream_buf_done(my_obj, (mm_camera_buf_def_t *)in_val);
        break;
    case MM_STREAM_EVT_STOP:
        {
            uint8_t has_cb = 0;
            uint8_t i;
            rc = mm_stream_streamoff(my_obj);

            pthread_mutex_lock(&my_obj->cb_lock);
            for (i = 0; i < MM_CAMERA_STREAM_BUF_CB_MAX; i++) {
                if(NULL != my_obj->buf_cb[i].cb) {
                    has_cb = 1;
                    break;
                }
            }
            pthread_mutex_unlock(&my_obj->cb_lock);

            if (has_cb) {
                mm_camera_cmd_thread_release(&my_obj->cmd_thread);
            }
            my_obj->state = MM_STREAM_STATE_REG;
        }
        break;
    case MM_STREAM_EVT_SET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_set_parm(my_obj, payload->parms);
        }
        break;
    case MM_STREAM_EVT_GET_PARM:
        {
            mm_evt_paylod_set_get_stream_parms_t *payload =
                (mm_evt_paylod_set_get_stream_parms_t *)in_val;
            rc = mm_stream_get_parm(my_obj, payload->parms);
        }
        break;
    case MM_STREAM_EVT_DO_ACTION:
        rc = mm_stream_do_action(my_obj, in_val);
        break;
    default:
        CDBG_ERROR("%s: invalid state (%d) for evt (%d), in(%p), out(%p)",
                   __func__, my_obj->state, evt, in_val, out_val);
    }
    CDBG("%s :X rc = %d", __func__, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_config
 *
 * DESCRIPTION: configure a stream
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @config       : stream configuration
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_config(mm_stream_t *my_obj,
                         mm_camera_stream_config_t *config)
{
    int32_t rc = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    my_obj->stream_info = config->stream_info;
    my_obj->buf_num = 0;
    my_obj->mem_vtbl = config->mem_vtbl;
    my_obj->padding_info = config->padding_info;
    /* cd through intf always palced at idx 0 of buf_cb */
    my_obj->buf_cb[0].cb = config->stream_cb;
    my_obj->buf_cb[0].user_data = config->userdata;
    my_obj->buf_cb[0].cb_count = -1; /* infinite by default */

    rc = mm_stream_sync_info(my_obj);
    if (rc == 0) {
        rc = mm_stream_set_fmt(my_obj);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_release
 *
 * DESCRIPTION: release a stream resource
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_release(mm_stream_t *my_obj)
{
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    /* close fd */
    if(my_obj->fd > 0)
    {
        close(my_obj->fd);
    }

    /* destroy mutex */
    pthread_mutex_destroy(&my_obj->buf_lock);
    pthread_mutex_destroy(&my_obj->cb_lock);

    /* reset stream obj */
    memset(my_obj, 0, sizeof(mm_stream_t));

    return 0;
}

/*===========================================================================
 * FUNCTION   : mm_stream_streamon
 *
 * DESCRIPTION: stream on a stream. sending v4l2 request to kernel
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_streamon(mm_stream_t *my_obj)
{
    int32_t rc;
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;

    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);
    /* Add fd to data poll thread */
    rc = mm_camera_poll_thread_add_poll_fd(&my_obj->ch_obj->poll_thread[0],
                                           my_obj->my_hdl,
                                           my_obj->fd,
                                           mm_stream_data_notify,
                                           (void*)my_obj);
    if (rc < 0) {
        return rc;
    }
    rc = ioctl(my_obj->fd, VIDIOC_STREAMON, &buf_type);
    if (rc < 0) {
        CDBG_ERROR("%s: ioctl VIDIOC_STREAMON failed: rc=%d\n",
                   __func__, rc);
        /* remove fd from data poll thread in case of failure */
        mm_camera_poll_thread_del_poll_fd(&my_obj->ch_obj->poll_thread[0], my_obj->my_hdl);
    }
    CDBG("%s :X rc = %d",__func__,rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_streamoff
 *
 * DESCRIPTION: stream off a stream. sending v4l2 request to kernel
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_streamoff(mm_stream_t *my_obj)
{
    int32_t rc;
    enum v4l2_buf_type buf_type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    /* step1: remove fd from data poll thread */
    mm_camera_poll_thread_del_poll_fd(&my_obj->ch_obj->poll_thread[0], my_obj->my_hdl);

    /* step2: stream off */
    rc = ioctl(my_obj->fd, VIDIOC_STREAMOFF, &buf_type);
    if (rc < 0) {
        CDBG_ERROR("%s: STREAMOFF failed: %s\n",
                __func__, strerror(errno));
    }
    CDBG("%s :X rc = %d",__func__,rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_read_msm_frame
 *
 * DESCRIPTION: dequeue a stream buffer from kernel queue
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @buf_info     : ptr to a struct storing buffer information
 *   @num_planes   : number of planes in the buffer
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_read_msm_frame(mm_stream_t * my_obj,
                                 mm_camera_buf_info_t* buf_info,
                                 uint8_t num_planes)
{
    int32_t rc = 0;
    struct v4l2_buffer vb;
    struct v4l2_plane planes[VIDEO_MAX_PLANES];
    uint32_t i = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    memset(&vb,  0,  sizeof(vb));
    vb.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    vb.memory = V4L2_MEMORY_USERPTR;
    vb.m.planes = &planes[0];
    vb.length = num_planes;

    rc = ioctl(my_obj->fd, VIDIOC_DQBUF, &vb);
    if (rc < 0) {
        CDBG_ERROR("%s: VIDIOC_DQBUF ioctl call failed (rc=%d)\n",
                   __func__, rc);
    } else {
        int8_t idx = vb.index;
        buf_info->buf = &my_obj->buf[idx];
        buf_info->frame_idx = vb.sequence;
        buf_info->stream_id = my_obj->my_hdl;

        buf_info->buf->stream_id = my_obj->my_hdl;
        buf_info->buf->buf_idx = idx;
        buf_info->buf->frame_idx = vb.sequence;
        buf_info->buf->ts.tv_sec  = vb.timestamp.tv_sec;
        buf_info->buf->ts.tv_nsec = vb.timestamp.tv_usec * 1000;

        for(i = 0; i < vb.length; i++) {
            CDBG("%s plane %d addr offset: %d data offset:%d\n",
                 __func__, i, vb.m.planes[i].reserved[0],
                 vb.m.planes[i].data_offset);
            buf_info->buf->planes[i].reserved[0] =
                vb.m.planes[i].reserved[0];
            buf_info->buf->planes[i].data_offset =
                vb.m.planes[i].data_offset;
        }
    }
    CDBG("%s :X rc = %d",__func__,rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_set_parms
 *
 * DESCRIPTION: set parameters per stream
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @in_value     : ptr to a param struct to be set to server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the parms struct buf is already mapped to server via
 *              domain socket. Corresponding fields of parameters to be set
 *              are already filled in by upper layer caller.
 *==========================================================================*/
int32_t mm_stream_set_parm(mm_stream_t *my_obj,
                           cam_stream_parm_buffer_t *in_value)
{
    int32_t rc = -1;
    if (in_value != NULL) {
        rc = mm_camera_util_s_ctrl(my_obj->fd, CAM_PRIV_STREAM_PARM, my_obj->server_stream_id);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_get_parms
 *
 * DESCRIPTION: get parameters per stream
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @in_value     : ptr to a param struct to be get from server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the parms struct buf is already mapped to server via
 *              domain socket. Corresponding fields of parameters to be get
 *              are already filled in by upper layer caller.
 *==========================================================================*/
int32_t mm_stream_get_parm(mm_stream_t *my_obj,
                           cam_stream_parm_buffer_t *in_value)
{
    int32_t rc = -1;
    if (in_value != NULL) {
        rc = mm_camera_util_g_ctrl(my_obj->fd, CAM_PRIV_STREAM_PARM, my_obj->server_stream_id);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_do_actions
 *
 * DESCRIPTION: request server to perform stream based actions
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @in_value     : ptr to a struct of actions to be performed by the server
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Assume the action struct buf is already mapped to server via
 *              domain socket. Corresponding fields of actions to be performed
 *              are already filled in by upper layer caller.
 *==========================================================================*/
int32_t mm_stream_do_action(mm_stream_t *my_obj,
                            void *in_value)
{
    int32_t rc = -1;
    if (in_value != NULL) {
        rc = mm_camera_util_s_ctrl(my_obj->fd, CAM_PRIV_STREAM_PARM, my_obj->server_stream_id);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_set_ext_mode
 *
 * DESCRIPTION: set stream extended mode to server via v4l2 ioctl
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : Server will return a server stream id that uniquely identify
 *              this stream on server side. Later on communication to server
 *              per stream should use this server stream id.
 *==========================================================================*/
int32_t mm_stream_set_ext_mode(mm_stream_t * my_obj)
{
    int32_t rc = 0;
    struct v4l2_streamparm s_parm;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    memset(&s_parm, 0, sizeof(s_parm));
    s_parm.type =  V4L2_BUF_TYPE_PRIVATE;

    rc = ioctl(my_obj->fd, VIDIOC_S_PARM, &s_parm);
    CDBG("%s:stream fd=%d, rc=%d, extended_mode=%d\n",
         __func__, my_obj->fd, rc, s_parm.parm.capture.extendedmode);
    if (rc == 0) {
        /* get server stream id */
        my_obj->server_stream_id = s_parm.parm.capture.extendedmode;
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_qbuf
 *
 * DESCRIPTION: enqueue buffer back to kernel queue for furture use
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @buf          : ptr to a struct storing buffer information
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_qbuf(mm_stream_t *my_obj, mm_camera_buf_def_t *buf)
{
    int32_t rc = 0;
    struct v4l2_buffer buffer;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    memset(&buffer, 0, sizeof(buffer));
    buffer.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    buffer.memory = V4L2_MEMORY_USERPTR;
    buffer.index = buf->buf_idx;
    buffer.m.planes = &buf->planes[0];
    buffer.length = buf->num_planes;

    CDBG("%s:stream_hdl=%d,fd=%d,frame idx=%d,num_planes = %d\n", __func__,
         buf->stream_id, buf->fd, buffer.index, buffer.length);

    rc = ioctl(my_obj->fd, VIDIOC_QBUF, &buffer);
    CDBG("%s: qbuf idx:%d, rc:%d", __func__, buffer.index, rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_request_buf
 *
 * DESCRIPTION: This function let kernel know the amount of buffers need to
 *              be registered via v4l2 ioctl.
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_request_buf(mm_stream_t * my_obj)
{
    int32_t rc = 0;
    uint8_t i,reg = 0;
    struct v4l2_requestbuffers bufreq;
    uint8_t buf_num = my_obj->buf_num;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    if(buf_num > MM_CAMERA_MAX_NUM_FRAMES) {
        CDBG_ERROR("%s: buf num %d > max limit %d\n",
                   __func__, buf_num, MM_CAMERA_MAX_NUM_FRAMES);
        return -1;
    }

    pthread_mutex_lock(&my_obj->buf_lock);
    for(i = 0; i < buf_num; i++){
        if (my_obj->buf_status[i].initial_reg_flag){
            reg = 1;
            break;
        }
    }
    pthread_mutex_unlock(&my_obj->buf_lock);

    if(!reg) {
        /* No need to register a buffer */
        CDBG_ERROR("No Need to register this buffer");
        return rc;
    }
    memset(&bufreq, 0, sizeof(bufreq));
    bufreq.count = buf_num;
    bufreq.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufreq.memory = V4L2_MEMORY_USERPTR;
    rc = ioctl(my_obj->fd, VIDIOC_REQBUFS, &bufreq);
    if (rc < 0) {
      CDBG_ERROR("%s: fd=%d, ioctl VIDIOC_REQBUFS failed: rc=%d\n",
           __func__, my_obj->fd, rc);
    }
    CDBG("%s :X rc = %d",__func__,rc);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_map_buf
 *
 * DESCRIPTION: mapping stream buffer via domain socket to server
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @buf_type     : type of buffer to be mapped. could be following values:
 *                   CAM_MAPPING_BUF_TYPE_STREAM_BUF
 *                   CAM_MAPPING_BUF_TYPE_STREAM_INFO
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @frame_idx    : index of buffer within the stream buffers, only valid if
 *                   buf_type is CAM_MAPPING_BUF_TYPE_STREAM_BUF or
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @plane_idx    : plane index. If all planes share the same fd,
 *                   plane_idx = -1; otherwise, plean_idx is the
 *                   index to plane (0..num_of_planes)
 *   @fd           : file descriptor of the buffer
 *   @size         : size of the buffer
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_map_buf(mm_stream_t * my_obj,
                          uint8_t buf_type,
                          uint32_t frame_idx,
                          int32_t plane_idx,
                          int fd,
                          uint32_t size)
{
    if (NULL == my_obj || NULL == my_obj->ch_obj || NULL == my_obj->ch_obj->cam_obj) {
        CDBG_ERROR("%s: NULL obj of stream/channel/camera", __func__);
        return -1;
    }

    cam_sock_packet_t packet;
    memset(&packet, 0, sizeof(cam_sock_packet_t));
    packet.msg_type = CAM_MAPPING_TYPE_FD_MAPPING;
    packet.payload.buf_map.type = buf_type;
    packet.payload.buf_map.fd = fd;
    packet.payload.buf_map.size = size;
    packet.payload.buf_map.stream_id = my_obj->server_stream_id;
    packet.payload.buf_map.frame_idx = frame_idx;
    packet.payload.buf_map.plane_idx = plane_idx;
    return mm_camera_util_sendmsg(my_obj->ch_obj->cam_obj,
                                  &packet,
                                  sizeof(cam_sock_packet_t),
                                  fd);
}

/*===========================================================================
 * FUNCTION   : mm_stream_unmap_buf
 *
 * DESCRIPTION: unmapping stream buffer via domain socket to server
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @buf_type     : type of buffer to be unmapped. could be following values:
 *                   CAM_MAPPING_BUF_TYPE_STREAM_BUF
 *                   CAM_MAPPING_BUF_TYPE_STREAM_INFO
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @frame_idx    : index of buffer within the stream buffers, only valid if
 *                   buf_type is CAM_MAPPING_BUF_TYPE_STREAM_BUF or
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @plane_idx    : plane index. If all planes share the same fd,
 *                   plane_idx = -1; otherwise, plean_idx is the
 *                   index to plane (0..num_of_planes)
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_unmap_buf(mm_stream_t * my_obj,
                            uint8_t buf_type,
                            uint32_t frame_idx,
                            int32_t plane_idx)
{
    if (NULL == my_obj || NULL == my_obj->ch_obj || NULL == my_obj->ch_obj->cam_obj) {
        CDBG_ERROR("%s: NULL obj of stream/channel/camera", __func__);
        return -1;
    }

    cam_sock_packet_t packet;
    memset(&packet, 0, sizeof(cam_sock_packet_t));
    packet.msg_type = CAM_MAPPING_TYPE_FD_UNMAPPING;
    packet.payload.buf_unmap.type = buf_type;
    packet.payload.buf_unmap.stream_id = my_obj->server_stream_id;
    packet.payload.buf_unmap.frame_idx = frame_idx;
    packet.payload.buf_unmap.plane_idx = plane_idx;
    return mm_camera_util_sendmsg(my_obj->ch_obj->cam_obj,
                                  &packet,
                                  sizeof(cam_sock_packet_t),
                                  0);
}

/*===========================================================================
 * FUNCTION   : mm_stream_map_buf_ops
 *
 * DESCRIPTION: ops for mapping stream buffer via domain socket to server.
 *              This function will be passed to upper layer as part of ops table
 *              to be used by upper layer when allocating stream buffers and mapping
 *              buffers to server via domain socket.
 *
 * PARAMETERS :
 *   @frame_idx    : index of buffer within the stream buffers, only valid if
 *                   buf_type is CAM_MAPPING_BUF_TYPE_STREAM_BUF or
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @plane_idx    : plane index. If all planes share the same fd,
 *                   plane_idx = -1; otherwise, plean_idx is the
 *                   index to plane (0..num_of_planes)
 *   @fd           : file descriptor of the buffer
 *   @size         : size of the buffer
 *   @userdata     : user data ptr (stream object)
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
static int32_t mm_stream_map_buf_ops(uint32_t frame_idx,
                                     int32_t plane_idx,
                                     int fd,
                                     uint32_t size,
                                     void *userdata)
{
    mm_stream_t *my_obj = (mm_stream_t *)userdata;
    return mm_stream_map_buf(my_obj,
                             CAM_MAPPING_BUF_TYPE_STREAM_BUF,
                             frame_idx, plane_idx, fd, size);
}

/*===========================================================================
 * FUNCTION   : mm_stream_unmap_buf_ops
 *
 * DESCRIPTION: ops for unmapping stream buffer via domain socket to server.
 *              This function will be passed to upper layer as part of ops table
 *              to be used by upper layer when allocating stream buffers and unmapping
 *              buffers to server via domain socket.
 *
 * PARAMETERS :
 *   @frame_idx    : index of buffer within the stream buffers, only valid if
 *                   buf_type is CAM_MAPPING_BUF_TYPE_STREAM_BUF or
 *                   CAM_MAPPING_BUF_TYPE_OFFLINE_INPUT_BUF
 *   @plane_idx    : plane index. If all planes share the same fd,
 *                   plane_idx = -1; otherwise, plean_idx is the
 *                   index to plane (0..num_of_planes)
 *   @userdata     : user data ptr (stream object)
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
static int32_t mm_stream_unmap_buf_ops(uint32_t frame_idx,
                                       int32_t plane_idx,
                                       void *userdata)
{
    mm_stream_t *my_obj = (mm_stream_t *)userdata;
    return mm_stream_unmap_buf(my_obj,
                               CAM_MAPPING_BUF_TYPE_STREAM_BUF,
                               frame_idx,
                               plane_idx);
}

/*===========================================================================
 * FUNCTION   : mm_stream_init_bufs
 *
 * DESCRIPTION: initialize stream buffers needed. This function will request
 *              buffers needed from upper layer through the mem ops table passed
 *              during configuration stage.
 *
 * PARAMETERS :
 *   @my_obj  : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_init_bufs(mm_stream_t * my_obj)
{
    int32_t i, rc = 0;
    uint8_t *reg_flags = NULL;
    mm_camera_map_unmap_ops_tbl_t ops_tbl;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    /* deinit buf if it's not NULL*/
    if (NULL != my_obj->buf) {
        mm_stream_deinit_bufs(my_obj);
    }

    ops_tbl.map_ops = mm_stream_map_buf_ops;
    ops_tbl.unmap_ops = mm_stream_unmap_buf_ops;
    ops_tbl.userdata = my_obj;

    rc = my_obj->mem_vtbl.get_bufs(&my_obj->frame_offset,
                                   &my_obj->buf_num,
                                   &reg_flags,
                                   &my_obj->buf,
                                   &ops_tbl,
                                   my_obj->mem_vtbl.user_data);

    if (0 != rc) {
        CDBG_ERROR("%s: Error get buf, rc = %d\n", __func__, rc);
        return rc;
    }

    my_obj->buf_status =
        (mm_stream_buf_status_t *)malloc(sizeof(mm_stream_buf_status_t) * my_obj->buf_num);

    if (NULL == my_obj->buf_status) {
        CDBG_ERROR("%s: No memory for buf_status", __func__);
        mm_stream_deinit_bufs(my_obj);
        free(reg_flags);
        return -1;
    }

    memset(my_obj->buf_status, 0, sizeof(mm_stream_buf_status_t) * my_obj->buf_num);
    for (i = 0; i < my_obj->buf_num; i++) {
        my_obj->buf_status[i].initial_reg_flag = reg_flags[i];
        my_obj->buf[i].stream_id = my_obj->my_hdl;
    }

    free(reg_flags);
    reg_flags = NULL;

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_deinit_bufs
 *
 * DESCRIPTION: return stream buffers to upper layer through the mem ops table
 *              passed during configuration stage.
 *
 * PARAMETERS :
 *   @my_obj  : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_deinit_bufs(mm_stream_t * my_obj)
{
    int32_t rc = 0;
    mm_camera_map_unmap_ops_tbl_t ops_tbl;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    if (NULL == my_obj->buf) {
        CDBG("%s: Buf is NULL, no need to deinit", __func__);
        return rc;
    }

    /* release bufs */
    ops_tbl.map_ops = mm_stream_map_buf_ops;
    ops_tbl.unmap_ops = mm_stream_unmap_buf_ops;
    ops_tbl.userdata = my_obj;

    rc = my_obj->mem_vtbl.put_bufs(&ops_tbl,
                                   my_obj->mem_vtbl.user_data);

    free(my_obj->buf);
    my_obj->buf = NULL;
    free(my_obj->buf_status);
    my_obj->buf_status = NULL;

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_reg_buf
 *
 * DESCRIPTION: register buffers with kernel by calling v4l2 ioctl QBUF for
 *              each buffer in the stream
 *
 * PARAMETERS :
 *   @my_obj  : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_reg_buf(mm_stream_t * my_obj)
{
    int32_t rc = 0;
    uint8_t i;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    rc = mm_stream_request_buf(my_obj);
    if (rc != 0) {
        return rc;
    }

    pthread_mutex_lock(&my_obj->buf_lock);
    for(i = 0; i < my_obj->buf_num; i++){
        my_obj->buf[i].buf_idx = i;

        /* check if need to qbuf initially */
        if (my_obj->buf_status[i].initial_reg_flag) {
            rc = mm_stream_qbuf(my_obj, &my_obj->buf[i]);
            if (rc != 0) {
                CDBG_ERROR("%s: VIDIOC_QBUF rc = %d\n", __func__, rc);
                break;
            }
            my_obj->buf_status[i].buf_refcnt = 0;
            my_obj->buf_status[i].in_kernel = 1;
        } else {
            /* the buf is held by upper layer, will not queue into kernel.
             * add buf reference count */
            my_obj->buf_status[i].buf_refcnt = 1;
            my_obj->buf_status[i].in_kernel = 0;
        }
    }
    pthread_mutex_unlock(&my_obj->buf_lock);

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_unreg buf
 *
 * DESCRIPTION: unregister all stream buffers from kernel
 *
 * PARAMETERS :
 *   @my_obj  : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_unreg_buf(mm_stream_t * my_obj)
{
    struct v4l2_requestbuffers bufreq;
    int32_t i, rc = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    /* unreg buf to kernel */
    bufreq.count = 0;
    bufreq.type  = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    bufreq.memory = V4L2_MEMORY_USERPTR;
    rc = ioctl(my_obj->fd, VIDIOC_REQBUFS, &bufreq);
    if (rc < 0) {
        CDBG_ERROR("%s: fd=%d, VIDIOC_REQBUFS failed, rc=%d\n",
              __func__, my_obj->fd, rc);
    }

    /* reset buf reference count */
    pthread_mutex_lock(&my_obj->buf_lock);
    if (NULL != my_obj->buf_status) {
        for(i = 0; i < my_obj->buf_num; i++){
            my_obj->buf_status[i].buf_refcnt = 0;
            my_obj->buf_status[i].in_kernel = 0;
        }
    }
    pthread_mutex_unlock(&my_obj->buf_lock);

    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_get_v4l2_fmt
 *
 * DESCRIPTION: translate camera image format into FOURCC code
 *
 * PARAMETERS :
 *   @fmt     : camera image format
 *
 * RETURN     : FOURCC code for image format
 *==========================================================================*/
uint32_t mm_stream_get_v4l2_fmt(cam_format_t fmt)
{
    uint32_t val;
    switch(fmt) {
    case CAM_FORMAT_YUV_420_NV12:
        val = V4L2_PIX_FMT_NV12;
        break;
    case CAM_FORMAT_YUV_420_NV21:
        val = V4L2_PIX_FMT_NV21;
        break;
    case CAM_FORMAT_BAYER_QCOM_RAW_10BPP_GBRG:
    case CAM_FORMAT_BAYER_QCOM_RAW_10BPP_GRBG:
    case CAM_FORMAT_BAYER_QCOM_RAW_10BPP_RGGB:
    case CAM_FORMAT_BAYER_QCOM_RAW_10BPP_BGGR:
        val= V4L2_PIX_FMT_SBGGR10;
        break;
    case CAM_FORMAT_YUV_422_NV61:
        val= V4L2_PIX_FMT_NV61;
        break;
    case CAM_FORMAT_YUV_RAW_8BIT:
        val= V4L2_PIX_FMT_YUYV;
        break;
    case CAM_FORMAT_YUV_420_YV12:
        val= V4L2_PIX_FMT_NV12;
        break;
    default:
        val = 0;
        CDBG_ERROR("%s: Unknown fmt=%d", __func__, fmt);
        break;
    }
    CDBG("%s: fmt=%d, val =%d", __func__, fmt, val);
    return val;
}

/*===========================================================================
 * FUNCTION   : mm_stream_calc_offset_preview
 *
 * DESCRIPTION: calculate preview/postview frame offset based on format and
 *              padding information
 *
 * PARAMETERS :
 *   @fmt     : image format
 *   @dim     : image dimension
 *   @buf_planes : [out] buffer plane information
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_calc_offset_preview(cam_format_t fmt,
                                      cam_dimension_t *dim,
                                      cam_stream_buf_plane_info_t *buf_planes)
{
    int32_t rc = 0;
    int stride = 0, scanline = 0;

    switch (fmt) {
    case CAM_FORMAT_YUV_420_NV12:
    case CAM_FORMAT_YUV_420_NV21:
        /* 2 planes: Y + CbCr */
        stride = PAD_TO_SIZE(dim->width, CAM_PAD_TO_16);
        scanline = PAD_TO_SIZE(dim->height, CAM_PAD_TO_2);
        buf_planes->plane_info.num_planes = 2;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len = stride * scanline;
        buf_planes->plane_info.mp[1].offset = 0;
        buf_planes->plane_info.mp[1].len =
            PAD_TO_SIZE(stride/2, CAM_PAD_TO_16) * scanline;
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len +
                        buf_planes->plane_info.mp[1].len,
                        CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_YUV_420_NV21_ADRENO:
        /* 2 planes: Y + CbCr */
        stride = PAD_TO_SIZE(dim->width, CAM_PAD_TO_32);
        scanline = PAD_TO_SIZE(dim->height, CAM_PAD_TO_32);
        buf_planes->plane_info.num_planes = 2;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline,
                        CAM_PAD_TO_4K);
        buf_planes->plane_info.mp[1].offset = 0;
        buf_planes->plane_info.mp[1].len =
            PAD_TO_SIZE(2 *
                        PAD_TO_SIZE(stride / 2, CAM_PAD_TO_32) *
                        PAD_TO_SIZE(scanline / 2, CAM_PAD_TO_32),
                        CAM_PAD_TO_4K);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len +
                        buf_planes->plane_info.mp[1].len,
                        CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_YUV_420_YV12:
        /* 3 planes: Y + Cr + Cb */
        stride = PAD_TO_SIZE(dim->width, CAM_PAD_TO_16);
        scanline = PAD_TO_SIZE(dim->height, CAM_PAD_TO_2);
        buf_planes->plane_info.num_planes = 3;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len = stride * scanline;
        buf_planes->plane_info.mp[1].offset = 0;
        buf_planes->plane_info.mp[1].len =
            PAD_TO_SIZE(stride / 2, CAM_PAD_TO_16) * scanline / 2;
        buf_planes->plane_info.mp[2].offset = 0;
        buf_planes->plane_info.mp[2].len =
            PAD_TO_SIZE(stride / 2, CAM_PAD_TO_16) * scanline / 2;
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len +
                        buf_planes->plane_info.mp[1].len +
                        buf_planes->plane_info.mp[2].len,
                        CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_YUV_422_NV16:
    case CAM_FORMAT_YUV_422_NV61:
        /* 2 planes: Y + CbCr */
        stride = PAD_TO_SIZE(dim->width, CAM_PAD_TO_16);
        scanline = dim->height;
        buf_planes->plane_info.num_planes = 2;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len = stride * scanline;
        buf_planes->plane_info.mp[1].offset = 0;
        buf_planes->plane_info.mp[1].len = stride * scanline;
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len +
                        buf_planes->plane_info.mp[1].len,
                        CAM_PAD_TO_4K);
        break;
    default:
        CDBG_ERROR("%s: Invalid cam_format for preview %d",
                   __func__, fmt);
        rc = -1;
        break;
    }

    buf_planes->offset_x =0;
    buf_planes->offset_y = 0;
    buf_planes->stride = stride;
    buf_planes->scanline = scanline;
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_calc_offset_snapshot
 *
 * DESCRIPTION: calculate snapshot/postproc frame offset based on format and
 *              padding information
 *
 * PARAMETERS :
 *   @fmt     : image format
 *   @dim     : image dimension
 *   @padding : padding information
 *   @buf_planes : [out] buffer plane information
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_calc_offset_snapshot(cam_format_t fmt,
                                       cam_dimension_t *dim,
                                       cam_padding_info_t *padding,
                                       cam_stream_buf_plane_info_t *buf_planes)
{
    int32_t rc = 0;
    uint8_t isAFamily = mm_camera_util_chip_is_a_family();
    int offset_x = 0, offset_y = 0;
    int stride = 0, scanline = 0;

    if (isAFamily) {
        stride = dim->width;
        scanline = PAD_TO_SIZE(dim->height, CAM_PAD_TO_16);
        offset_x = 0;
        offset_y = scanline - dim->height;
        scanline += offset_y; /* double padding */
    } else {
        stride = PAD_TO_SIZE(dim->width,
                             padding->width_padding);
        scanline = PAD_TO_SIZE(dim->height,
                               padding->height_padding);
        offset_x = 0;
        offset_y = 0;
    }

    switch (fmt) {
    case CAM_FORMAT_YUV_420_NV12:
    case CAM_FORMAT_YUV_420_NV21:
        /* 2 planes: Y + CbCr */
        buf_planes->plane_info.num_planes = 2;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline,
                        padding->plane_padding);
        buf_planes->plane_info.mp[0].offset = offset_x +
            PAD_TO_SIZE(stride * offset_y, padding->plane_padding);
        buf_planes->plane_info.mp[1].len =
            PAD_TO_SIZE(stride * scanline / 2,
                        padding->plane_padding);
        buf_planes->plane_info.mp[1].offset = offset_x +
            PAD_TO_SIZE(stride * offset_y / 2,
                        padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len +
                        buf_planes->plane_info.mp[1].len,
                        CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_YUV_420_YV12:
        /* 3 planes: Y + Cr + Cb */
        buf_planes->plane_info.num_planes = 3;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline, padding->plane_padding);
        buf_planes->plane_info.mp[1].offset = 0;
        buf_planes->plane_info.mp[1].len =
            PAD_TO_SIZE(stride * scanline / 4, padding->plane_padding);
        buf_planes->plane_info.mp[2].offset = 0;
        buf_planes->plane_info.mp[2].len =
            PAD_TO_SIZE(stride * scanline / 4, padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len +
                        buf_planes->plane_info.mp[1].len +
                        buf_planes->plane_info.mp[2].len,
                        CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_YUV_422_NV16:
    case CAM_FORMAT_YUV_422_NV61:
        /* 2 planes: Y + CbCr */
        buf_planes->plane_info.num_planes = 2;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline, padding->plane_padding);
        buf_planes->plane_info.mp[0].offset = offset_x +
            PAD_TO_SIZE(stride * offset_y, padding->plane_padding);
        buf_planes->plane_info.mp[1].len =
            PAD_TO_SIZE(stride * scanline, padding->plane_padding);
        buf_planes->plane_info.mp[1].offset = offset_x +
            PAD_TO_SIZE(stride * offset_y, padding->plane_padding);
        buf_planes->plane_info.frame_len = PAD_TO_SIZE(
            buf_planes->plane_info.mp[0].len + buf_planes->plane_info.mp[1].len,
            CAM_PAD_TO_4K);
        break;
    default:
        CDBG_ERROR("%s: Invalid cam_format for snapshot %d",
                   __func__, fmt);
        rc = -1;
        break;
    }

    buf_planes->offset_x = offset_x;
    buf_planes->offset_y = offset_y;
    buf_planes->stride = stride;
    buf_planes->scanline = scanline;
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_calc_offset_raw
 *
 * DESCRIPTION: calculate raw frame offset based on format and padding information
 *
 * PARAMETERS :
 *   @fmt     : image format
 *   @dim     : image dimension
 *   @padding : padding information
 *   @buf_planes : [out] buffer plane information
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_calc_offset_raw(cam_format_t fmt,
                                  cam_dimension_t *dim,
                                  cam_padding_info_t *padding,
                                  cam_stream_buf_plane_info_t *buf_planes)
{
    int32_t rc = 0;
    int stride = dim->width;
    int scanline = dim->height;

    switch (fmt) {
    case CAM_FORMAT_YUV_RAW_8BIT:
        /* 1 plane */
        buf_planes->plane_info.num_planes = 1;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline * 2, padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len, CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_BAYER_QCOM_RAW_8BPP_GBRG:
    case CAM_FORMAT_BAYER_QCOM_RAW_8BPP_GRBG:
    case CAM_FORMAT_BAYER_QCOM_RAW_8BPP_RGGB:
    case CAM_FORMAT_BAYER_QCOM_RAW_8BPP_BGGR:
    case CAM_FORMAT_BAYER_MIPI_RAW_8BPP_GBRG:
    case CAM_FORMAT_BAYER_MIPI_RAW_8BPP_GRBG:
    case CAM_FORMAT_BAYER_MIPI_RAW_8BPP_RGGB:
    case CAM_FORMAT_BAYER_MIPI_RAW_8BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_8BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_8BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_8BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_8BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_8BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_8BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_8BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_8BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN8_8BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN8_8BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN8_8BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN8_8BPP_BGGR:
        /* 1 plane */
        buf_planes->plane_info.num_planes = 1;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline, padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len, CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_BAYER_QCOM_RAW_10BPP_GBRG:
    case CAM_FORMAT_BAYER_QCOM_RAW_10BPP_GRBG:
    case CAM_FORMAT_BAYER_QCOM_RAW_10BPP_RGGB:
    case CAM_FORMAT_BAYER_QCOM_RAW_10BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_10BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_10BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_10BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_10BPP_BGGR:
        buf_planes->plane_info.num_planes = 1;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline * 8 / 6, padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len, CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_BAYER_QCOM_RAW_12BPP_GBRG:
    case CAM_FORMAT_BAYER_QCOM_RAW_12BPP_GRBG:
    case CAM_FORMAT_BAYER_QCOM_RAW_12BPP_RGGB:
    case CAM_FORMAT_BAYER_QCOM_RAW_12BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_12BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_12BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_12BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_QCOM_12BPP_BGGR:
        buf_planes->plane_info.num_planes = 1;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline * 8 / 5, padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len, CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_BAYER_MIPI_RAW_10BPP_GBRG:
    case CAM_FORMAT_BAYER_MIPI_RAW_10BPP_GRBG:
    case CAM_FORMAT_BAYER_MIPI_RAW_10BPP_RGGB:
    case CAM_FORMAT_BAYER_MIPI_RAW_10BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_10BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_10BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_10BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_10BPP_BGGR:
        buf_planes->plane_info.num_planes = 1;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline * 5 / 4, padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len, CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_BAYER_MIPI_RAW_12BPP_GBRG:
    case CAM_FORMAT_BAYER_MIPI_RAW_12BPP_GRBG:
    case CAM_FORMAT_BAYER_MIPI_RAW_12BPP_RGGB:
    case CAM_FORMAT_BAYER_MIPI_RAW_12BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_12BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_12BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_12BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_MIPI_12BPP_BGGR:
        buf_planes->plane_info.num_planes = 1;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline * 3 / 2, padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len, CAM_PAD_TO_4K);
        break;
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_8BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_8BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_8BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_8BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_10BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_10BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_10BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_10BPP_BGGR:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_12BPP_GBRG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_12BPP_GRBG:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_12BPP_RGGB:
    case CAM_FORMAT_BAYER_IDEAL_RAW_PLAIN16_12BPP_BGGR:
        buf_planes->plane_info.num_planes = 1;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline * 2, padding->plane_padding);
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len, CAM_PAD_TO_4K);
        break;
    default:
        CDBG_ERROR("%s: Invalid cam_format %d for raw stream",
                   __func__, fmt);
        rc = -1;
        break;
    }

    buf_planes->offset_x =0;
    buf_planes->offset_y = 0;
    buf_planes->stride = stride;
    buf_planes->scanline = scanline;
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_calc_offset_video
 *
 * DESCRIPTION: calculate video frame offset based on format and
 *              padding information
 *
 * PARAMETERS :
 *   @dim     : image dimension
 *   @buf_planes : [out] buffer plane information
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_calc_offset_video(cam_dimension_t *dim,
                                    cam_stream_buf_plane_info_t *buf_planes)
{
    int32_t rc = 0;
    uint8_t isVenus = (mm_camera_util_chip_is_a_family() ? FALSE : TRUE);
    int stride = 0, scanline = 0;

    if (isVenus) {
        // using Venus
        stride = VENUS_Y_STRIDE(COLOR_FMT_NV12, dim->width);
        scanline = VENUS_Y_SCANLINES(COLOR_FMT_NV12, dim->height);

        buf_planes->plane_info.frame_len =
            VENUS_BUFFER_SIZE(COLOR_FMT_NV12, dim->width, dim->height);
        buf_planes->plane_info.num_planes = 2;
        buf_planes->plane_info.mp[0].len = stride * scanline;
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[1].len =
            buf_planes->plane_info.frame_len - buf_planes->plane_info.mp[0].len;
        buf_planes->plane_info.mp[1].offset = 0;
    } else {
        stride = dim->width;
        scanline = dim->height;

        buf_planes->plane_info.num_planes = 2;
        buf_planes->plane_info.mp[0].len =
            PAD_TO_SIZE(stride * scanline, CAM_PAD_TO_2K);
        buf_planes->plane_info.mp[0].offset = 0;
        buf_planes->plane_info.mp[1].len =
            PAD_TO_SIZE(stride * scanline / 2, CAM_PAD_TO_2K);
        buf_planes->plane_info.mp[1].offset = 0;
        buf_planes->plane_info.frame_len =
            PAD_TO_SIZE(buf_planes->plane_info.mp[0].len +
                        buf_planes->plane_info.mp[1].len,
                        CAM_PAD_TO_4K);
    }

    buf_planes->offset_x =0;
    buf_planes->offset_y = 0;
    buf_planes->stride = stride;
    buf_planes->scanline = scanline;
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_calc_offset_metadata
 *
 * DESCRIPTION: calculate metadata frame offset based on format and
 *              padding information
 *
 * PARAMETERS :
 *   @dim     : image dimension
 *   @padding : padding information
 *   @buf_planes : [out] buffer plane information
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_calc_offset_metadata(cam_dimension_t *dim,
                                       cam_padding_info_t *padding,
                                       cam_stream_buf_plane_info_t *buf_planes)
{
    int32_t rc = 0;
    buf_planes->plane_info.num_planes = 1;
    buf_planes->plane_info.mp[0].offset = 0;
    buf_planes->plane_info.mp[0].len =
        PAD_TO_SIZE(dim->width * dim->height, padding->plane_padding);
    buf_planes->plane_info.frame_len =
        PAD_TO_SIZE(buf_planes->plane_info.mp[0].len, CAM_PAD_TO_4K);

    buf_planes->offset_x =0;
    buf_planes->offset_y = 0;
    buf_planes->stride = dim->width;
    buf_planes->scanline = dim->height;
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_calc_offset
 *
 * DESCRIPTION: calculate frame offset based on format and padding information
 *
 * PARAMETERS :
 *   @my_obj  : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_calc_offset(mm_stream_t *my_obj)
{
    int32_t rc = 0;

    switch (my_obj->stream_info->stream_type) {
    case CAM_STREAM_TYPE_PREVIEW:
    case CAM_STREAM_TYPE_POSTVIEW:
        rc = mm_stream_calc_offset_preview(my_obj->stream_info->fmt,
                                           &my_obj->stream_info->dim,
                                           &my_obj->stream_info->buf_planes);
        break;
    case CAM_STREAM_TYPE_SNAPSHOT:
        rc = mm_stream_calc_offset_snapshot(my_obj->stream_info->fmt,
                                            &my_obj->stream_info->dim,
                                            &my_obj->padding_info,
                                            &my_obj->stream_info->buf_planes);
        break;
    case CAM_STREAM_TYPE_OFFLINE_PROC:
        rc = mm_stream_calc_offset_snapshot(my_obj->stream_info->fmt,
                                            &my_obj->stream_info->dim,
                                            &my_obj->padding_info,
                                            &my_obj->stream_info->buf_planes);
        rc = mm_stream_calc_offset_snapshot(my_obj->stream_info->offline_proc_buf_fmt,
                                            &my_obj->stream_info->offline_proc_buf_dim,
                                            &my_obj->padding_info,
                                            &my_obj->stream_info->offline_buf_planes);
        break;
    case CAM_STREAM_TYPE_VIDEO:
        rc = mm_stream_calc_offset_video(&my_obj->stream_info->dim,
                                         &my_obj->stream_info->buf_planes);
        break;
    case CAM_STREAM_TYPE_RAW:
        rc = mm_stream_calc_offset_raw(my_obj->stream_info->fmt,
                                       &my_obj->stream_info->dim,
                                       &my_obj->padding_info,
                                       &my_obj->stream_info->buf_planes);
        break;
    case CAM_STREAM_TYPE_METADATA:
        rc = mm_stream_calc_offset_metadata(&my_obj->stream_info->dim,
                                            &my_obj->padding_info,
                                            &my_obj->stream_info->buf_planes);
        break;
    default:
        CDBG_ERROR("%s: not supported for stream type %d",
                   __func__, my_obj->stream_info->stream_type);
        rc = -1;
        break;
    }

    my_obj->frame_offset = my_obj->stream_info->buf_planes.plane_info;
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_sync_info
 *
 * DESCRIPTION: synchronize stream information with server
 *
 * PARAMETERS :
 *   @my_obj  : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 * NOTE       : assume stream info buffer is mapped to server and filled in with
 *              stream information by upper layer. This call will let server to
 *              synchornize the stream information with HAL. If server find any
 *              fields that need to be changed accroding to hardware configuration,
 *              server will modify corresponding fields so that HAL could know
 *              about it.
 *==========================================================================*/
int32_t mm_stream_sync_info(mm_stream_t *my_obj)
{
    int32_t rc = 0;
    rc = mm_stream_calc_offset(my_obj);

    if (rc == 0) {
        rc = mm_camera_util_s_ctrl(my_obj->fd,
                                   CAM_PRIV_STREAM_INFO_SYNC,
                                   my_obj->server_stream_id);
    }
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_set_fmt
 *
 * DESCRIPTION: set stream format to kernel via v4l2 ioctl
 *
 * PARAMETERS :
 *   @my_obj  : stream object
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_set_fmt(mm_stream_t *my_obj)
{
    int32_t rc = 0;
    struct v4l2_format fmt;
    struct msm_v4l2_format_data msm_fmt;
    int i;

    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    if (my_obj->stream_info->dim.width == 0 ||
        my_obj->stream_info->dim.height == 0) {
        CDBG_ERROR("%s:invalid input[w=%d,h=%d,fmt=%d]\n",
                   __func__,
                   my_obj->stream_info->dim.width,
                   my_obj->stream_info->dim.height,
                   my_obj->stream_info->fmt);
        return -1;
    }

    memset(&fmt, 0, sizeof(fmt));
    memset(&msm_fmt, 0, sizeof(msm_fmt));
    fmt.type = V4L2_BUF_TYPE_PRIVATE;
    msm_fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE_MPLANE;
    msm_fmt.width = my_obj->stream_info->dim.width;
    msm_fmt.height = my_obj->stream_info->dim.height;
    msm_fmt.pixelformat = mm_stream_get_v4l2_fmt(my_obj->stream_info->fmt);
    msm_fmt.num_planes = my_obj->frame_offset.num_planes;
    for (i = 0; i < msm_fmt.num_planes; i++) {
        msm_fmt.plane_sizes[i] = my_obj->frame_offset.mp[i].len;
    }

    memcpy(fmt.fmt.raw_data, &msm_fmt, sizeof(msm_fmt));
    rc = ioctl(my_obj->fd, VIDIOC_S_FMT, &fmt);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_buf_done
 *
 * DESCRIPTION: enqueue buffer back to kernel
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @frame        : frame to be enqueued back to kernel
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_buf_done(mm_stream_t * my_obj,
                           mm_camera_buf_def_t *frame)
{
    int32_t rc = 0;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    pthread_mutex_lock(&my_obj->buf_lock);
    if(my_obj->buf_status[frame->buf_idx].buf_refcnt == 0) {
        CDBG("%s: Error Trying to free second time?(idx=%d) count=%d\n",
                   __func__, frame->buf_idx,
                   my_obj->buf_status[frame->buf_idx].buf_refcnt);
        rc = -1;
    }else{
        my_obj->buf_status[frame->buf_idx].buf_refcnt--;
        if (0 == my_obj->buf_status[frame->buf_idx].buf_refcnt) {
            CDBG("<DEBUG> : Buf done for buffer:%d", frame->buf_idx);
            rc = mm_stream_qbuf(my_obj, frame);
            if(rc < 0) {
                CDBG_ERROR("%s: mm_camera_stream_qbuf(idx=%d) err=%d\n",
                           __func__, frame->buf_idx, rc);
            } else {
                my_obj->buf_status[frame->buf_idx].in_kernel = 1;
            }
        }else{
            CDBG("<DEBUG> : Still ref count pending count :%d",
                 my_obj->buf_status[frame->buf_idx].buf_refcnt);
            CDBG("<DEBUG> : for buffer:%p:%d",
                 my_obj, frame->buf_idx);
        }
    }
    pthread_mutex_unlock(&my_obj->buf_lock);
    return rc;
}

/*===========================================================================
 * FUNCTION   : mm_stream_reg_buf_cb
 *
 * DESCRIPTION: Allow other stream to register dataCB at this stream.
 *
 * PARAMETERS :
 *   @my_obj       : stream object
 *   @val          : ptr to info about the callback to be registered
 *
 * RETURN     : int32_t type of status
 *              0  -- success
 *              -1 -- failure
 *==========================================================================*/
int32_t mm_stream_reg_buf_cb(mm_stream_t *my_obj,
                             mm_stream_data_cb_t *val)
{
    int32_t rc = -1;
    uint8_t i;
    CDBG("%s: E, my_handle = 0x%x, fd = %d, state = %d",
         __func__, my_obj->my_hdl, my_obj->fd, my_obj->state);

    pthread_mutex_lock(&my_obj->cb_lock);
    for (i=0 ;i < MM_CAMERA_STREAM_BUF_CB_MAX; i++) {
        if(NULL == my_obj->buf_cb[i].cb) {
            my_obj->buf_cb[i] = *val;
            rc = 0;
            break;
        }
    }
    pthread_mutex_unlock(&my_obj->cb_lock);

    return rc;
}
