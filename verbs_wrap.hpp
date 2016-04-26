// verbs_wrap wraps several IB verbs functions into more convenient, higher
// level functions that use config options defined in verbs_wrap.cpp.
#pragma once

#include <infiniband/verbs.h>
#include <cstddef>

ibv_context *CreateContext();
ibv_cq      *CreateCompletionQueue(ibv_context *context);
ibv_pd      *CreateProtectionDomain(ibv_context *context);
uint16_t     GetLid(ibv_context *context);
ibv_qp      *CreateQueuePair(ibv_pd *pd, ibv_cq *cq);
ibv_mr      *RegisterMemory(ibv_pd *pd, void *addr, size_t length);
void         ConnectQueuePair(ibv_qp *local_qp, int remote_lid, int remote_qp_num);
