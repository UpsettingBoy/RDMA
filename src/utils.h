#pragma once

#include <inttypes.h>
#include <stdbool.h>

#include <infiniband/verbs.h>

const uint16_t PORT = 1710;
const int32_t HCA_PORT = 1;

struct ib_mr {
  void *peer_addr;
  size_t peer_len;
  uint32_t peer_lkey;
  uint32_t peer_rkey;
};

struct ib_qp_data {
  uint16_t peer_local_id;
  uint32_t peer_qp_number;
  struct ib_mr peer_mr;
};

bool init_qp(struct ibv_qp *qp) {
  struct ibv_qp_attr init_attr;
  memset(&init_attr, 0, sizeof(init_attr));
  init_attr.qp_state = IBV_QPS_INIT;
  init_attr.port_num = HCA_PORT;
  init_attr.pkey_index = 0;
  init_attr.qp_access_flags =
      IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;

  return ibv_modify_qp(qp, &init_attr,
                       IBV_QP_STATE | IBV_QP_PKEY_INDEX | IBV_QP_PORT |
                           IBV_QP_ACCESS_FLAGS) == 0
             ? true
             : false;
}

bool recv_qp(struct ibv_qp *qp, struct ib_qp_data peer_data) {
  struct ibv_qp_attr rtr_attr;
  memset(&rtr_attr, 0, sizeof(rtr_attr));
  rtr_attr.qp_state = IBV_QPS_RTR;
  rtr_attr.path_mtu = IBV_MTU_1024;
  rtr_attr.rq_psn = 0;
  rtr_attr.max_dest_rd_atomic = 1;
  rtr_attr.min_rnr_timer = 0x12;
  rtr_attr.ah_attr.is_global = 0;
  rtr_attr.ah_attr.sl = 0;
  rtr_attr.ah_attr.src_path_bits = 0;
  rtr_attr.ah_attr.port_num = HCA_PORT;

  rtr_attr.dest_qp_num = peer_data.peer_qp_number;
  rtr_attr.ah_attr.dlid = peer_data.peer_local_id;

  return ibv_modify_qp(qp, &rtr_attr,
                       IBV_QP_STATE | IBV_QP_AV | IBV_QP_PATH_MTU |
                           IBV_QP_DEST_QPN | IBV_QP_RQ_PSN |
                           IBV_QP_MAX_DEST_RD_ATOMIC | IBV_QP_MIN_RNR_TIMER) ==
                 0
             ? true
             : false;
}

bool send_qp(struct ibv_qp *queue_pair) {
  struct ibv_qp_attr rts_attr;
  memset(&rts_attr, 0, sizeof(rts_attr));
  rts_attr.qp_state = IBV_QPS_RTS;
  rts_attr.timeout = 0x12;
  rts_attr.retry_cnt = 7;
  rts_attr.rnr_retry = 7;
  rts_attr.sq_psn = 0;
  rts_attr.max_rd_atomic = 1;

  return ibv_modify_qp(queue_pair, &rts_attr,
                       IBV_QP_STATE | IBV_QP_TIMEOUT | IBV_QP_RETRY_CNT |
                           IBV_QP_RNR_RETRY | IBV_QP_SQ_PSN |
                           IBV_QP_MAX_QP_RD_ATOMIC) == 0
             ? true
             : false;
}
