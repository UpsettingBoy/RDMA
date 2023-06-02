#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <arpa/inet.h>
#include <infiniband/verbs.h>
#include <net/if.h>
#include <sys/ioctl.h>

#include "utils.h"

void get_ipv4(char *ip_str) {
  int fd;
  struct ifreq ifr;

  fd = socket(AF_INET, SOCK_DGRAM, 0);
  ifr.ifr_addr.sa_family = AF_INET;
  strncpy(ifr.ifr_name, "eth0", IFNAMSIZ - 1);

  ioctl(fd, SIOCGIFADDR, &ifr);
  close(fd);

  strcpy(ip_str, inet_ntoa(((struct sockaddr_in *)&ifr.ifr_addr)->sin_addr));
}

struct ib_qp_data share_conn_details(struct ibv_qp *local_qp,
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

  int32_t listen_fd = socket(AF_INET, SOCK_STREAM, 0);

  char ip_str[INET_ADDRSTRLEN];
  get_ipv4(ip_str);

  struct sockaddr_in servaddr = {};
  servaddr.sin_family = AF_INET;
  servaddr.sin_addr.s_addr = htonl(INADDR_ANY);
  servaddr.sin_port = htons(PORT);

  bind(listen_fd, (struct sockaddr *)&servaddr, sizeof(servaddr));
  listen(listen_fd, 1);

  printf("Server listening at %s...\n", ip_str);

  struct sockaddr client_addr;
  uint32_t client_size = sizeof(client_addr);
  int32_t conn_fd = accept(listen_fd, &client_addr, &client_size);

  write(conn_fd, &my_data, sizeof(my_data));
  struct ib_qp_data peer_data;
  read(conn_fd, &peer_data, sizeof(peer_data));

  printf("IB Info:\n");
  printf("\tLocal IB. LID = %d. QP NUMBER = %d\n", my_data.peer_local_id,
         my_data.peer_qp_number);
  printf("\tPeer IB.  LID = %d. QP NUMBER = %d\n", peer_data.peer_local_id,
         peer_data.peer_qp_number);
  printf("\tPeer MR. LEN = %lld. LKEY = %d. RKEY = %d\n",
         peer_data.peer_mr.peer_len, peer_data.peer_mr.peer_lkey,
         peer_data.peer_mr.peer_rkey);

  close(conn_fd);
  close(listen_fd);

  return peer_data;
}

int main(int argc, char const *argv[]) {
  if (argc != 3) {
    printf("Run <number messages> <message bytes size>\n");
    exit(-1);
  }

  uint32_t num_msgs = atoi(argv[1]);
  uint32_t msg_size = atoi(argv[2]); // Number of bytes each message has2

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
                                     .cap = {.max_send_wr = num_msgs,
                                             .max_recv_wr = 1,
                                             .max_send_sge = num_msgs,
                                             .max_recv_sge = 1},
                                     .qp_type = IBV_QPT_RC};
  struct ibv_qp *qp = ibv_create_qp(domain, &qp_desc);

  struct ib_qp_data peer_data = share_conn_details(qp, memory);

  init_qp(qp);
  recv_qp(qp, peer_data);
  send_qp(qp);

  struct ibv_sge *sg_sends = calloc(num_msgs, sizeof(struct ibv_send_wr));
  struct ibv_send_wr *wr_sends = calloc(num_msgs, sizeof(struct ibv_send_wr));

  for (size_t i = 0; i < num_msgs; i++) {
    sg_sends[i].addr = (uintptr_t)(memory->addr + (i * msg_bytes));
    sg_sends[i].length = msg_bytes;
    sg_sends[i].lkey = memory->lkey;

    // RDMA peer memory
    wr_sends[i].wr_id = i;
    wr_sends[i].next = (i == num_msgs - 1) ? NULL : wr_sends + i + 1;
    wr_sends[i].sg_list = sg_sends + i;
    wr_sends[i].num_sge = 1;
    wr_sends[i].opcode = IBV_WR_RDMA_WRITE;
    wr_sends[i].send_flags = IBV_SEND_SIGNALED;
    wr_sends[i].wr.rdma.remote_addr =
        (uintptr_t)(peer_data.peer_mr.peer_addr + (i * msg_bytes));
    wr_sends[i].wr.rdma.rkey = peer_data.peer_mr.peer_rkey;
  }

  clock_t start = clock();
  ibv_post_send(qp, wr_sends, NULL);
  poll(cq);
  clock_t end = clock();
  float seconds = (float)(end - start) / CLOCKS_PER_SEC;

  printf("RDMA write of %ds took %0.5lf s\n", *msgs, seconds);

  printf("Clean-up remaining\n");

  return 0;
}
