/* -*- Mode: C; c-basic-offset:4 ; indent-tabs-mode:nil -*- */
/*
 * Copyright (c) 2011-2022 Sandia National Laboratories.  All rights reserved.
 * Copyright (c) 2015-2018 Los Alamos National Security, LLC.  All rights
 *                         reserved.
 * Copyright (c) 2015-2017 Research Organization for Information Science
 *                         and Technology (RIST). All rights reserved.
 * Copyright (c) 2017      The University of Tennessee and The University
 *                         of Tennessee Research Foundation.  All rights
 *                         reserved.
 * Copyright (c) 2016-2017 IBM Corporation. All rights reserved.
 * Copyright (c) 2018-2022 Amazon.com, Inc. or its affiliates.  All Rights reserved.
 * Copyright (c) 2020      High Performance Computing Center Stuttgart,
 *                         University of Stuttgart.  All rights reserved.
 * $COPYRIGHT$
 *
 * Additional copyrights may follow
 *
 * $HEADER$
 */

#include "ompi_config.h"

#include "opal/util/printf.h"
#include "opal/include/opal/align.h"
#include "opal/mca/mpool/base/base.h"
#include "opal/opal_portable_platform.h"

#include "ompi/mca/osc/base/base.h"
#include "ompi/mca/osc/base/osc_base_obj_convert.h"
#include "ompi/mca/osc/osc.h"
#include "ompi/request/request.h"

#include "osc_portals4.h"
#include "osc_portals4_request.h"

static int component_open(void);
static int component_register(void);
static int component_init(bool enable_progress_threads, bool enable_mpi_threads);
static int component_finalize(void);
static int component_query(struct ompi_win_t *win, void **base, size_t size, ptrdiff_t disp_unit,
                           struct ompi_communicator_t *comm, struct opal_info_t *info,
                           int flavor);
static int component_select(struct ompi_win_t *win, void **base, size_t size, ptrdiff_t disp_unit,
                            struct ompi_communicator_t *comm, struct opal_info_t *info,
                            int flavor, int *model);


ompi_osc_portals4_component_t mca_osc_portals4_component = {
    { /* ompi_osc_base_component_t */
        .osc_version = {
            OMPI_OSC_BASE_VERSION_4_0_0,
            .mca_component_name = "portals4",
            MCA_BASE_MAKE_VERSION(component, OMPI_MAJOR_VERSION, OMPI_MINOR_VERSION,
                                  OMPI_RELEASE_VERSION),
            .mca_open_component = component_open,
            .mca_register_component_params = component_register,
        },
        .osc_data = {
            /* The component is not checkpoint ready */
            MCA_BASE_METADATA_PARAM_NONE
        },
        .osc_init = component_init,
        .osc_query = component_query,
        .osc_select = component_select,
        .osc_finalize = component_finalize,
    }
};
MCA_BASE_COMPONENT_INIT(ompi, osc, portals4)


ompi_osc_portals4_module_t ompi_osc_portals4_module_template = {
    {
        .osc_win_attach = ompi_osc_portals4_attach,
        .osc_win_detach = ompi_osc_portals4_detach,
        .osc_free = ompi_osc_portals4_free,

        .osc_put = ompi_osc_portals4_put,
        .osc_get = ompi_osc_portals4_get,
        .osc_accumulate = ompi_osc_portals4_accumulate,
        .osc_compare_and_swap = ompi_osc_portals4_compare_and_swap,
        .osc_fetch_and_op = ompi_osc_portals4_fetch_and_op,
        .osc_get_accumulate = ompi_osc_portals4_get_accumulate,

        .osc_rput = ompi_osc_portals4_rput,
        .osc_rget = ompi_osc_portals4_rget,
        .osc_raccumulate = ompi_osc_portals4_raccumulate,
        .osc_rget_accumulate = ompi_osc_portals4_rget_accumulate,

        .osc_fence = ompi_osc_portals4_fence,

        .osc_start = ompi_osc_portals4_start,
        .osc_complete = ompi_osc_portals4_complete,
        .osc_post = ompi_osc_portals4_post,
        .osc_wait = ompi_osc_portals4_wait,
        .osc_test = ompi_osc_portals4_test,

        .osc_lock = ompi_osc_portals4_lock,
        .osc_unlock = ompi_osc_portals4_unlock,
        .osc_lock_all = ompi_osc_portals4_lock_all,
        .osc_unlock_all = ompi_osc_portals4_unlock_all,

        .osc_sync = ompi_osc_portals4_sync,
        .osc_flush = ompi_osc_portals4_flush,
        .osc_flush_all = ompi_osc_portals4_flush_all,
        .osc_flush_local = ompi_osc_portals4_flush_local,
        .osc_flush_local_all = ompi_osc_portals4_flush_local_all,
    }
};


/* look up parameters for configuring this window.  The code first
   looks in the info structure passed by the user, then through mca
   parameters. */
static bool
check_config_value_bool(char *key, opal_info_t *info)
{
    int ret, flag, param;
    bool result = false;
    const bool *flag_value = &result;

    ret = opal_info_get_bool(info, key, &result, &flag);
    if (OMPI_SUCCESS == ret && flag) {
        return result;
    }

    param = mca_base_var_find("ompi", "osc", "portals4", key);
    if (0 <= param) {
        (void) mca_base_var_get_value(param, &flag_value, NULL, NULL);
    }

    return flag_value[0];
}


static bool
check_config_value_equal(char *key, opal_info_t *info, char *value)
{
    int ret, flag, param;
    const char *mca_value;
    bool result = false;
    opal_cstring_t *value_string;

    ret = opal_info_get(info, key, &value_string, &flag);
    if (OMPI_SUCCESS != ret || !flag) {
        goto info_not_found;
    }
    if (0 == strcmp(value_string->string, value)) {
        result = true;
    }
    OBJ_RELEASE(value_string);
    return result;

 info_not_found:
    param = mca_base_var_find("ompi", "osc", "portals4", key);
    if (0 > param) return false;

    ret = mca_base_var_get_value(param, &mca_value, NULL, NULL);
    if (OMPI_SUCCESS != ret) return false;

    if (0 == strcmp(mca_value, value)) {
        result = true;
    }

    return result;
}


static int
progress_callback(void)
{
    int ret, count = 0;
    ptl_event_t ev;
    ompi_osc_portals4_request_t *req;
    int32_t ops;

    while (true) {
        ret = PtlEQGet(mca_osc_portals4_component.matching_eq_h, &ev);
        if (PTL_OK == ret) {
            goto process;
        } else if (PTL_EQ_DROPPED == ret) {
            opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                                "%s:%d: PtlEQGet reported dropped event",
                                __FILE__, __LINE__);
            goto process;
        } else if (PTL_EQ_EMPTY == ret) {
            return 0;
        } else {
            opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                                "%s:%d: PtlEQGet failed: %d\n",
                                __FILE__, __LINE__, ret);
            return 0;
        }

process:
        if (ev.ni_fail_type != PTL_OK) {
            opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                                "%s:%d: event failure: %d %d",
                                __FILE__, __LINE__, ev.type, ev.ni_fail_type);
            return 0;
        }

        count++;

        if (NULL != ev.user_ptr) {
            /* be sure that we receive the PTL_EVENT_LINK */
            if (ev.type == PTL_EVENT_LINK) {
              *(int *)ev.user_ptr = *(int *)ev.user_ptr + 1;
              opal_condition_broadcast(&mca_osc_portals4_component.cond);
              continue;
            }

            req = (ompi_osc_portals4_request_t*) ev.user_ptr;
            req->super.req_status._ucount = opal_atomic_add_fetch_32(&req->bytes_committed, ev.mlength);
            ops = opal_atomic_add_fetch_32(&req->ops_committed, 1);
            if (ops == req->ops_expected) {
                ompi_request_complete(&req->super, true);
            }
        }
    }

    return count;
}


static int
component_open(void)
{
    return OMPI_SUCCESS;
}


static int
component_register(void)
{
    mca_osc_portals4_component.no_locks = false;
    (void) mca_base_component_var_register(&mca_osc_portals4_component.super.osc_version,
                                           "no_locks",
                                           "Enable optimizations available only if MPI_LOCK is "
                                           "not used.  "
                                           "Info key of same name overrides this value.",
                                           MCA_BASE_VAR_TYPE_BOOL, NULL, 0, 0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_osc_portals4_component.no_locks);

    mca_osc_portals4_component.ptl_max_msg_size = PTL_SIZE_MAX;
    (void) mca_base_component_var_register(&mca_osc_portals4_component.super.osc_version,
                                           "max_msg_size",
                                           "Max size supported by portals4 (above that, a message is cut into messages less than that size)",
                                           MCA_BASE_VAR_TYPE_UNSIGNED_LONG,
                                           NULL,
                                           0,
                                           0,
                                           OPAL_INFO_LVL_9,
                                           MCA_BASE_VAR_SCOPE_READONLY,
                                           &mca_osc_portals4_component.ptl_max_msg_size);

    return OMPI_SUCCESS;
}


static int
component_init(bool enable_progress_threads, bool enable_mpi_threads)
{
    int ret;
    ptl_ni_limits_t actual;

    ret = PtlInit();
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlInit failed: %d\n",
                            __FILE__, __LINE__, ret);
        return OMPI_ERROR;
    }

    ret = PtlNIInit(PTL_IFACE_DEFAULT,
                    PTL_NI_PHYSICAL | PTL_NI_MATCHING,
                    PTL_PID_ANY,
                    NULL,
                    &actual,
                    &mca_osc_portals4_component.matching_ni_h);
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlNIInit failed: %d\n",
                            __FILE__, __LINE__, ret);
        return ret;
    }

    /* BWB: FIX ME: Need to make sure our ID matches with the MTL... */

    if (mca_osc_portals4_component.ptl_max_msg_size > actual.max_msg_size)
        mca_osc_portals4_component.ptl_max_msg_size = actual.max_msg_size;
    OPAL_OUTPUT_VERBOSE((10, ompi_osc_base_framework.framework_output,
                         "max_size = %lu", mca_osc_portals4_component.ptl_max_msg_size));

    mca_osc_portals4_component.matching_atomic_max = actual.max_atomic_size;
    mca_osc_portals4_component.matching_fetch_atomic_max = actual.max_fetch_atomic_size;
    mca_osc_portals4_component.matching_atomic_ordered_size =
        MAX(actual.max_waw_ordered_size, actual.max_war_ordered_size);

    ret = PtlEQAlloc(mca_osc_portals4_component.matching_ni_h,
                     4096,
                     &mca_osc_portals4_component.matching_eq_h);
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlEQAlloc failed: %d\n",
                            __FILE__, __LINE__, ret);
        return ret;
    }

    ret = PtlPTAlloc(mca_osc_portals4_component.matching_ni_h,
                     0,
                     mca_osc_portals4_component.matching_eq_h,
                     REQ_OSC_TABLE_ID,
                     &mca_osc_portals4_component.matching_pt_idx);
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlPTAlloc failed: %d\n",
                            __FILE__, __LINE__, ret);
        return ret;
    }

    if (mca_osc_portals4_component.matching_pt_idx != REQ_OSC_TABLE_ID) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlPTAlloc did not allocate the requested PT: %d\n",
                            __FILE__, __LINE__, mca_osc_portals4_component.matching_pt_idx);
        return ret;
    }

    OBJ_CONSTRUCT(&mca_osc_portals4_component.requests, opal_free_list_t);
    ret = opal_free_list_init (&mca_osc_portals4_component.requests,
                               sizeof(ompi_osc_portals4_request_t),
                               opal_cache_line_size,
                               OBJ_CLASS(ompi_osc_portals4_request_t),
                               0, 0, 8, 0, 8, NULL, 0, NULL, NULL, NULL);
    if (OMPI_SUCCESS != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: opal_free_list_init failed: %d\n",
                            __FILE__, __LINE__, ret);
        return ret;
    }

    ret = opal_progress_register(progress_callback);
    if (OMPI_SUCCESS != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: opal_progress_register failed: %d\n",
                            __FILE__, __LINE__, ret);
        return ret;
    }

    ompi_osc_base_requires_world = true;

    return OMPI_SUCCESS;
}


static int
component_finalize(void)
{
    PtlNIFini(mca_osc_portals4_component.matching_ni_h);

    return OMPI_SUCCESS;
}


static int
component_query(struct ompi_win_t *win, void **base, size_t size, ptrdiff_t disp_unit,
                struct ompi_communicator_t *comm, struct opal_info_t *info,
                int flavor)
{
    int ret;

    if (MPI_WIN_FLAVOR_SHARED == flavor) return -1;

    ret = PtlGetUid(mca_osc_portals4_component.matching_ni_h, &mca_osc_portals4_component.uid);
    if (PTL_OK != ret) {
      opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                          "%s:%d: PtlGetUid failed: %d\n",
                          __FILE__, __LINE__, ret);
      return OMPI_ERROR;
    }

    return 20;
}


static int
component_select(struct ompi_win_t *win, void **base, size_t size, ptrdiff_t disp_unit,
                 struct ompi_communicator_t *comm, struct opal_info_t *info,
                 int flavor, int *model)
{
    ompi_osc_portals4_module_t *module = NULL;
    int ret = OMPI_ERROR;
    int tmp, flag;
    ptl_md_t md;
    ptl_me_t me;
    char *name;
    size_t memory_alignment = OPAL_ALIGN_MIN;

    if (MPI_WIN_FLAVOR_SHARED == flavor) return OMPI_ERR_NOT_SUPPORTED;

    if (NULL != info) {
        opal_cstring_t *align_info_str;
        opal_info_get(info, "mpi_minimum_memory_alignment",
                      &align_info_str, &flag);
        if (flag) {
            size_t tmp_align = atoll(align_info_str->string);
            OBJ_RELEASE(align_info_str);
            if (OPAL_ALIGN_MIN < tmp_align) {
                memory_alignment = tmp_align;
            }
        }
    }

    /* create module structure */
    module = (ompi_osc_portals4_module_t*)
        calloc(1, sizeof(ompi_osc_portals4_module_t));
    if (NULL == module) return OMPI_ERR_TEMP_OUT_OF_RESOURCE;

    /* fill in the function pointer part */
    memcpy(module, &ompi_osc_portals4_module_template,
           sizeof(ompi_osc_base_module_t));

    /* fill in our part */
    if (MPI_WIN_FLAVOR_ALLOCATE == flavor) {
        *base = mca_mpool_base_default_module->mpool_alloc(mca_mpool_base_default_module, size,
                                                           memory_alignment, 0);
        if (NULL == *base) goto error;
        module->free_after = *base;
    } else {
        module->free_after = NULL;
    }

    ret = ompi_comm_dup(comm, &module->comm);
    if (OMPI_SUCCESS != ret) goto error;

    opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                        "portals4 component creating window with id %d",
                        ompi_comm_get_local_cid(module->comm));

    opal_asprintf(&name, "portals4 window %d", ompi_comm_get_local_cid(module->comm));
    ompi_win_set_name(win, name);
    free(name);

    /* share everyone's displacement units. Only do an allgather if
       strictly necessary, since it requires O(p) state. */
    tmp = disp_unit;
    ret = module->comm->c_coll->coll_bcast(&tmp, 1, MPI_INT, 0,
                                          module->comm,
                                          module->comm->c_coll->coll_bcast_module);
    if (OMPI_SUCCESS != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: MPI_Bcast failed: %d\n",
                            __FILE__, __LINE__, ret);
        goto error;
    }
    tmp = (tmp == disp_unit) ? 1 : 0;
    ret = module->comm->c_coll->coll_allreduce(MPI_IN_PLACE, &tmp, 1, MPI_INT, MPI_LAND,
                                              module->comm, module->comm->c_coll->coll_allreduce_module);
    if (OMPI_SUCCESS != ret) goto error;
    if (tmp == 1) {
        module->disp_unit = disp_unit;
        module->disp_units = NULL;
    } else {
        module->disp_unit = -1;
        module->disp_units = malloc(sizeof(ptrdiff_t) * ompi_comm_size(module->comm));
        ret = module->comm->c_coll->coll_allgather(&disp_unit, sizeof(ptrdiff_t), MPI_BYTE,
                                                  module->disp_units, sizeof(ptrdiff_t), MPI_BYTE,
                                                  module->comm,
                                                  module->comm->c_coll->coll_allgather_module);
        if (OMPI_SUCCESS != ret) goto error;
    }

    module->ni_h = mca_osc_portals4_component.matching_ni_h;
    module->pt_idx = mca_osc_portals4_component.matching_pt_idx;

    ret = PtlCTAlloc(module->ni_h, &(module->ct_h));
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlCTAlloc failed: %d\n",
                            __FILE__, __LINE__, ret);
        goto error;
    }

    md.start = 0;
    md.length = PTL_SIZE_MAX;
    md.options = PTL_MD_EVENT_SUCCESS_DISABLE | PTL_MD_EVENT_CT_REPLY | PTL_MD_EVENT_CT_ACK;
    md.eq_handle = mca_osc_portals4_component.matching_eq_h;
    md.ct_handle = module->ct_h;
    ret = PtlMDBind(module->ni_h, &md, &module->md_h);
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlMDBind failed: %d\n",
                            __FILE__, __LINE__, ret);
        goto error;
    }

    md.start = 0;
    md.length = PTL_SIZE_MAX;
    md.options = PTL_MD_EVENT_SEND_DISABLE | PTL_MD_EVENT_CT_REPLY | PTL_MD_EVENT_CT_ACK;
    md.eq_handle = mca_osc_portals4_component.matching_eq_h;
    md.ct_handle = module->ct_h;
    ret = PtlMDBind(module->ni_h, &md, &module->req_md_h);
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlMDBind failed: %d\n",
                            __FILE__, __LINE__, ret);
        goto error;
    }

    module->origin_iovec_list = NULL;
    module->origin_iovec_md_h = PTL_INVALID_HANDLE;
    module->result_iovec_list = NULL;
    module->result_iovec_md_h = PTL_INVALID_HANDLE;

    if (MPI_WIN_FLAVOR_DYNAMIC == flavor) {
        me.start = 0;
        me.length = PTL_SIZE_MAX;
    } else {
        me.start = *base;
        me.length = size;
    }
    me.ct_handle = PTL_CT_NONE;
    me.uid = mca_osc_portals4_component.uid;
    me.options = PTL_ME_OP_PUT | PTL_ME_OP_GET | PTL_ME_NO_TRUNCATE | PTL_ME_EVENT_SUCCESS_DISABLE;
    me.match_id.phys.nid = PTL_NID_ANY;
    me.match_id.phys.pid = PTL_PID_ANY;
    me.match_bits = ompi_comm_get_local_cid(module->comm);
    me.ignore_bits = 0;

    ret = PtlMEAppend(module->ni_h,
                      module->pt_idx,
                      &me,
                      PTL_PRIORITY_LIST,
                      &module->ct_link,
                      &module->data_me_h);
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlMEAppend failed: %d\n",
                            __FILE__, __LINE__, ret);
        goto error;
    }

    me.start = &module->state;
    me.length = sizeof(module->state);
    me.ct_handle = PTL_CT_NONE;
    me.uid = mca_osc_portals4_component.uid;
    me.options = PTL_ME_OP_PUT | PTL_ME_OP_GET | PTL_ME_NO_TRUNCATE | PTL_ME_EVENT_SUCCESS_DISABLE;
    me.match_id.phys.nid = PTL_NID_ANY;
    me.match_id.phys.pid = PTL_PID_ANY;
    me.match_bits = ompi_comm_get_local_cid(module->comm) | OSC_PORTALS4_MB_CONTROL;
    me.ignore_bits = 0;

    ret = PtlMEAppend(module->ni_h,
                      module->pt_idx,
                      &me,
                      PTL_PRIORITY_LIST,
                      &module->ct_link,
                      &module->control_me_h);
    if (PTL_OK != ret) {
        opal_output_verbose(1, ompi_osc_base_framework.framework_output,
                            "%s:%d: PtlMEAppend failed: %d\n",
                            __FILE__, __LINE__, ret);
        goto error;
    }

    module->opcount = 0;
    module->match_bits = ompi_comm_get_local_cid(module->comm);
    module->atomic_max = (check_config_value_equal("accumulate_ordering", info, "none")) ?
        mca_osc_portals4_component.matching_atomic_max :
        MIN(mca_osc_portals4_component.matching_atomic_max,
            mca_osc_portals4_component.matching_atomic_ordered_size);
    module->fetch_atomic_max = (check_config_value_equal("accumulate_ordering", info, "none")) ?
        mca_osc_portals4_component.matching_fetch_atomic_max :
        MIN(mca_osc_portals4_component.matching_fetch_atomic_max,
            mca_osc_portals4_component.matching_atomic_ordered_size);

    module->zero = 0;
    module->one = 1;
    module->start_group = NULL;
    module->post_group = NULL;

    module->state.post_count = 0;
    module->state.complete_count = 0;
    if (check_config_value_bool("no_locks", info)) {
        module->state.lock = LOCK_ILLEGAL;
    } else {
        module->state.lock = LOCK_UNLOCKED;
    }

    OBJ_CONSTRUCT(&module->outstanding_locks, opal_list_t);

    module->passive_target_access_epoch = false;

#if defined(PLATFORM_ARCH_X86) || defined(PLATFORM_ARCH_X86_64)
    *model = MPI_WIN_UNIFIED;
#else
    *model = MPI_WIN_SEPARATE;
#endif

    win->w_osc_module = &module->super;

    PtlAtomicSync();

    /* Make sure that everyone's ready to receive. */
    OPAL_THREAD_LOCK(&mca_osc_portals4_component.lock);
    while (module->ct_link != 2) {
        opal_condition_wait(&mca_osc_portals4_component.cond,
                            &mca_osc_portals4_component.lock);
    }
    OPAL_THREAD_UNLOCK(&mca_osc_portals4_component.lock);

    module->comm->c_coll->coll_barrier(module->comm,
                                      module->comm->c_coll->coll_barrier_module);

    return OMPI_SUCCESS;

 error:
    /* BWB: FIX ME: This is all wrong... */
    if (!PtlHandleIsEqual(module->ct_h, PTL_INVALID_HANDLE)) PtlCTFree(module->ct_h);
    if (!PtlHandleIsEqual(module->data_me_h, PTL_INVALID_HANDLE)) PtlMEUnlink(module->data_me_h);
    if (!PtlHandleIsEqual(module->req_md_h, PTL_INVALID_HANDLE)) PtlMDRelease(module->req_md_h);
    if (!PtlHandleIsEqual(module->md_h, PTL_INVALID_HANDLE)) PtlMDRelease(module->md_h);
    if (NULL != module->comm) ompi_comm_free(&module->comm);
    if (NULL != module) free(module);

    return ret;
}


int
ompi_osc_portals4_attach(struct ompi_win_t *win, void *base, size_t len)
{
    return OMPI_SUCCESS;
}


int
ompi_osc_portals4_detach(struct ompi_win_t *win, const void *base)
{
    return OMPI_SUCCESS;
}


int
ompi_osc_portals4_free(struct ompi_win_t *win)
{
    ompi_osc_portals4_module_t *module =
        (ompi_osc_portals4_module_t*) win->w_osc_module;
    int ret = OMPI_SUCCESS;

    /* synchronize */
    module->comm->c_coll->coll_barrier(module->comm,
                                      module->comm->c_coll->coll_barrier_module);

    /* cleanup */
    PtlMEUnlink(module->control_me_h);
    PtlMEUnlink(module->data_me_h);
    PtlMDRelease(module->md_h);
    if (!PtlHandleIsEqual(module->origin_iovec_md_h,PTL_INVALID_HANDLE)) {
        PtlMDRelease(module->origin_iovec_md_h);
        free(module->origin_iovec_list);
    }
    if (!PtlHandleIsEqual(module->result_iovec_md_h,PTL_INVALID_HANDLE)) {
        PtlMDRelease(module->result_iovec_md_h);
        free(module->result_iovec_list);
    }
    PtlMDRelease(module->req_md_h);
    PtlCTFree(module->ct_h);
    if (NULL != module->disp_units) free(module->disp_units);
    ompi_comm_free(&module->comm);
    mca_mpool_base_default_module->mpool_free(mca_mpool_base_default_module,
                                              module->free_after);

    if (!opal_list_is_empty(&module->outstanding_locks)) {
        ret = OMPI_ERR_RMA_SYNC;
    }
    OBJ_DESTRUCT(&module->outstanding_locks);

    free(module);

    return ret;
}
