#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <unistd.h>

#include "utils.h"

struct ub_pack share_conn_details(const char *server_ip,
                                  struct ibv_qp *local_qp,
                                  struct ibv_mr **local_mrs, uint32_t num_mrs) {
  int32_t conn_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  inet_pton(AF_INET, server_ip, &(server_addr.sin_addr));

  int32_t err =
      connect(conn_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

  printf("Connecting to server %s...\n", server_ip);

  // Unpack QP and MR from peer
  struct ub_qp peer_data;
  struct ub_mr *peer_mr = malloc(num_mrs * sizeof(struct ub_mr));
  read(conn_fd, &peer_data, sizeof(peer_data));
  read(conn_fd, peer_mr, num_mrs * sizeof(struct ub_mr));

  // Pack my QP and MR data to send
  struct ibv_port_attr port_attr;
  ibv_query_port(local_qp->context, 1, &port_attr);

  // My QP data
  struct ub_qp my_data;
  my_data.local_id = port_attr.lid;
  my_data.qp_number = local_qp->qp_num;
  my_data.num_mrs = num_mrs;

  // My MR data
  struct ub_mr *my_mrs = malloc(num_mrs * sizeof(struct ub_mr));
  for (size_t i = 0; i < num_mrs; i++) {
    struct ibv_mr *current = local_mrs[i];

    my_mrs[i].addr = (uintptr_t)current->addr;
    my_mrs[i].len = current->length;
    my_mrs[i].lkey = current->lkey;
    my_mrs[i].rkey = current->rkey;
  }

  write(conn_fd, &my_data, sizeof(my_data));
  write(conn_fd, my_mrs, num_mrs * sizeof(struct ub_mr));

  printf("IB Info:\n");
  printf("\tLocal IB. LID = %d. QP NUMBER = %d\n", my_data.local_id,
         my_data.qp_number);
  printf("\tPeer  IB. LID = %d. QP NUMBER = %d\n", peer_data.local_id,
         peer_data.qp_number);
  printf("\tPeer MR. NUMBER = %d\n", peer_data.num_mrs);

  close(conn_fd);

  struct ub_pack pack;
  pack.qp = peer_data;
  pack.mrs = peer_mr;

  return pack;
}

int main(int argc, char const *argv[]) {
  if (argc != 4) {
    printf("Run <server IP> <number messages> <message bytes size>\n");
    exit(-1);
  }

  uint32_t num_msgs = atoi(argv[2]);
  uint32_t msg_size = atoi(argv[3]); // Number of bytes each message has

  struct ibv_device **devices = ibv_get_device_list(NULL);
  struct ibv_context *ctx = ibv_open_device(devices[0]);

  printf("Using %s %s\n", ibv_node_type_str(devices[0]->node_type),
         ibv_get_device_name(devices[0]));

  ibv_free_device_list(devices);

  // Associates QPs with RDMA resources
  struct ibv_pd *domain = ibv_alloc_pd(ctx);

  // Register memory of RDMA
  uint32_t msg_bytes = num_msgs * msg_size * sizeof(uint8_t);
  uint8_t *msgs = malloc(msg_bytes);
  memset(msgs, 0xAF, msg_bytes);

  struct ibv_mr **mem_regs = malloc(num_msgs * sizeof(struct ibv_mr *));
  for (size_t i = 0; i < num_msgs; i++) {
    size_t offset = i * msg_bytes;
    mem_regs[i] =
        ibv_reg_mr(domain, msgs + offset, msg_bytes, IBV_ACCESS_LOCAL_WRITE);
  }

  printf("%d memory regions registered!\n", num_msgs);

  // Create CQ
  struct ibv_cq *cq = ibv_create_cq(ctx, 1, NULL, NULL, 0);
  // Create QP
  struct ibv_qp_init_attr qp_desc = {.send_cq = cq,
                                     .recv_cq = cq,
                                     .sq_sig_all = 0,
                                     .cap = {.max_send_wr = 1,
                                             .max_recv_wr = num_msgs,
                                             .max_send_sge = 1,
                                             .max_recv_sge = num_msgs},
                                     .qp_type = IBV_QPT_RC};
  struct ibv_qp *qp = ibv_create_qp(domain, &qp_desc);

  struct ub_pack peer_data =
      share_conn_details(argv[1], qp, mem_regs, num_msgs);

  init_qp(qp);
  recv_qp(qp, peer_data.qp);

  poll(cq);

  //   send_qp(qp);

  //   struct ibv_sge sg;
  //   struct ibv_send_wr wr;

  //   memset(&sg, 0, sizeof(sg));
  //   sg.addr = (uintptr_t)memory->addr;
  //   sg.length = memory->length;
  //   sg.lkey = memory->lkey;

  //   memset(&wr, 0, sizeof(wr));
  //   wr.wr_id = 0;
  //   wr.sg_list = &sg;
  //   wr.num_sge = 1;
  //   wr.opcode = IBV_WR_RDMA_WRITE;
  //   wr.send_flags = IBV_SEND_SIGNALED;
  //   wr.wr.rdma.remote_addr = (uintptr_t)(peer_data.peer_mr.peer_addr);
  //   wr.wr.rdma.rkey = peer_data.peer_mr.peer_rkey;

  printf("Clean-up remaining\n");

  return 0;
}
