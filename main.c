#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <infiniband/verbs.h>

static void die(const char *reason);
static void die(const char *reason) {
  fprintf(stderr, "%s\n", reason);
  exit(EXIT_FAILURE);
}

#define TEST_NZ(x) do { if ( (x)) die("error: " #x " failed (returned non-zero)." ); } while (0)
#define TEST_Z(x)  do { if (!(x)) die("error: " #x " failed (returned zero/null)."); } while (0)

void printgid(union ibv_gid *gid) {
  uint8_t *p = (uint8_t *) gid;
  fprintf(stdout, "GID = %02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x:%02x\n",
    p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7], p[8], p[9], p[10], p[11], p[12], p[13], p[14], p[15]);
}

int main() {
  // get devices
  int num_devices;
  struct ibv_device **device_list = ibv_get_device_list(&num_devices);
  if (num_devices == 0) {
    exit(EXIT_FAILURE);
  }
  printf("num_devices: %d\n", num_devices);

  // print device names
  for (int i = 0; i < num_devices; i ++) {
    printf("device num: %d, name: %s\n", i, ibv_get_device_name(device_list[i]));
  }
  if (num_devices == 0) {
    printf("error: no devices found\n");
    exit(EXIT_FAILURE);
  }

  // open device
  puts("choosing device 0");
  struct ibv_device *device = device_list[0];
  struct ibv_context *context = ibv_open_device(device);
  puts("chose device");

  // print num ports
  struct ibv_device_attr device_attr;
  TEST_NZ(ibv_query_device(context, &device_attr));
  printf("# of physical ports: %" PRIu8 "\n", device_attr.phys_port_cnt);

  // print LID
  struct ibv_port_attr port_attr;
  TEST_NZ(ibv_query_port(context, 1, &port_attr));
  printf("lid: %" PRIu16 "\n", port_attr.lid);

  // print GID
  union ibv_gid remote_gid;
  union ibv_gid gid;
  TEST_NZ(ibv_query_gid(context, 1, 0, &gid));
  printgid(&gid);

  /* allocate Protection Domain */
  struct ibv_pd *pd;
  TEST_Z(pd = ibv_alloc_pd(context));
  puts("allocated protection domain");

  // create completion queue
  /* each side will send only one WR, so Completion Queue with 1 entry is enough */
  struct ibv_cq *cq;
  TEST_Z(cq = ibv_create_cq(context, 1, NULL, NULL, 0));
  puts("created completion queue");

  /* allocate the memory buffer that will hold the data */
  const uint64_t bufsize = 100;
  uint8_t *buf;
  TEST_Z(buf = (uint8_t *) malloc(bufsize));
  puts("malloc'd buffer");

  /* register the memory buffer */
  struct ibv_mr *mr;
  const enum ibv_access_flags access_flags = IBV_ACCESS_LOCAL_WRITE |
    IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE | IBV_ACCESS_REMOTE_ATOMIC;
  TEST_Z(ibv_reg_mr(pd, buf, bufsize, access_flags));
  puts("registered memory");

  // create queue pair
  struct ibv_qp_init_attr qp_init_attr = {
    NULL, // void *qp_context
    cq,   // struct ibv_cq *send_cq
    cq,   // struct ibv_cq *recv_cq
    NULL, // struct ibv_srq *srq
    {     // struct ibv_qp_cap	cap
      0,     // uint32_t max_send_wr
      0,     // uint32_t max_recv_wr
      1,     // uint32_t max_send_sge
      1,     // uint32_t max_recv_sge
      1,     // uint32_t max_inline_data
    },
    IBV_QPT_RC, // enum ibv_qp_type qp_type
    1,          // int sq_sig_all
    NULL,       // struct ibv_xrc_domain *xrc_domain;
  };
  struct ibv_qp *qp;
  TEST_Z(qp = ibv_create_qp(pd, &qp_init_attr));
  printf("created queue pair with num %" PRIu32 "\n", qp->qp_num);
  puts("created queue pair");

  return 0;
}
