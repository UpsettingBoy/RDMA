#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <unistd.h>

#include "utils.h"

struct ib_qp_data share_conn_details(const char *server_ip,
                                     struct ibv_qp *local_qp,
                                     struct ibv_mr *local_mr) {
  struct ib_mr my_memory;
  my_memory.peer_addr = local_mr->addr;
  my_memory.peer_len = local_mr->length;
  my_memory.peer_lkey = local_mr->lkey;
  my_memory.peer_rkey = local_mr->rkey;

  struct ibv_port_attr port_attr;
  ibv_query_port(local_qp->context, 1, &port_attr);

  struct ib_qp_data my_data;
  my_data.peer_local_id = port_attr.lid;
  my_data.peer_qp_number = local_qp->qp_num;
  my_data.peer_mr = my_memory;

  int32_t conn_fd = socket(AF_INET, SOCK_STREAM, 0);

  struct sockaddr_in server_addr;
  server_addr.sin_family = AF_INET;
  server_addr.sin_port = htons(PORT);
  inet_pton(AF_INET, server_ip, &(server_addr.sin_addr));

  int32_t err =
      connect(conn_fd, (struct sockaddr *)&server_addr, sizeof(server_addr));

  printf("Connecting to server %s...\n", server_ip);

  struct ib_qp_data peer_data;
  read(conn_fd, &peer_data, sizeof(peer_data));
  write(conn_fd, &my_data, sizeof(my_data));

  printf("IB Info:\n");
  printf("\tLocal IB. LID = %d. QP NUMBER = %d\n", my_data.peer_local_id,
         my_data.peer_qp_number);
  printf("\tPeer  IB. LID = %d. QP NUMBER = %d\n", peer_data.peer_local_id,
         peer_data.peer_qp_number);
  printf("\tPeer MR. LEN = %lld. LKEY = %d. RKEY = %d\n",
         peer_data.peer_mr.peer_len, peer_data.peer_mr.peer_lkey,
         peer_data.peer_mr.peer_rkey);

  close(conn_fd);

  return peer_data;
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

  struct ibv_mr *memory =
      ibv_reg_mr(domain, msgs, msg_bytes,
                 IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE);

  // Create CQ
  struct ibv_cq *cq = ibv_create_cq(ctx, 1, NULL, NULL, 0);
  // Create QP
  struct ibv_qp_init_attr qp_desc = {.send_cq = cq,
                                     .recv_cq = cq,
                                     .sq_sig_all = 0,
                                     .cap = {.max_send_wr = 1,
                                             .max_recv_wr = num_msgs,
                                             .max_send_sge = 1,
                                             .max_recv_sge = 1},
                                     .qp_type = IBV_QPT_RC};
  struct ibv_qp *qp = ibv_create_qp(domain, &qp_desc);

  struct ib_qp_data peer_data = share_conn_details(argv[1], qp, memory);

  init_qp(qp);
  recv_qp(qp, peer_data);

  struct ibv_wc wc;
  int num_comp;

  poll(cq);

  //   for (size_t i = 0; i < num_msgs; i++) {
  //     printf("RDMA memory[%d] read is: %d\n", i, msgs[i]);
  //   }

  printf("Clean-up remaining\n");

  return 0;
}
