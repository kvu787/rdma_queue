#include "verbs_wrap.hpp"

#include <iostream>
#include <string>

#include <cinttypes>
#include <cstdlib>
#include <cstring>

// constants to choose ports, tune performance parameters, etc.
// basically, we have this instead of messy command line parameters

// CreateContext
static const int DEVICE_NUM = 0; // The index of the physical InfiniBand device
                                 // in the list of IB devices. We'll just use
                                 // the first one.
static const uint8_t PORT_NUM = 1; // Sampa IB devices only have 1 port, so
                                   // we'll use that port. (Valid port numbers
                                   // start at 1.)

// CreateCompletionQueue
static const int COMPLETION_QUEUE_ENTRIES = 256;

// CreateQueuePair
static const uint32_t MAX_SEND_WR = 16; // how many operations per queue should we be able to enqueue at a time?
static const uint32_t MAX_RECV_WR = 1; // only need 1 if we're just using RDMA ops
static const uint32_t MAX_SEND_SGE = 1; // how many SGE's do we allow per send?
static const uint32_t MAX_RECV_SGE = 1; // how many SGE's do we allow per receive?
static const uint32_t MAX_INLINE_DATA = 16; // message rate drops from 6M/s to 4M/s at 29 bytes

// RegisterMemory and ConnectQueuePair
static const enum ibv_access_flags ACCESS_FLAGS =
  (enum ibv_access_flags) // cast so compiler doesn't complain
  (IBV_ACCESS_LOCAL_WRITE  | // we allow all operations except memory windows
   IBV_ACCESS_REMOTE_READ  |
   IBV_ACCESS_REMOTE_WRITE |
   IBV_ACCESS_REMOTE_ATOMIC);

// ConnectQueuePair
// reset -> init
static const uint8_t PHYS_PORT_NUM = 1;
// init -> rtr
static const enum ibv_mtu PATH_MTU = IBV_MTU_512; // use lowest to be safe
static const uint32_t RQ_PSN = 0;
static const uint8_t MAX_DEST_RD_ATOMIC = 16; // how many outstanding reads/atomic ops are allowed? (remote end of qp, limited by card)
static const uint8_t MIN_RNR_TIMER = 12;
// rtr -> rts
static const uint8_t TIMEOUT = 0x12;  // Mellanox recommendation
static const uint8_t RETRY_CNT = 6; // Mellanox recommendation
static const uint8_t RNR_RETRY = 0; // Mellanox recommendation
static const uint32_t SQ_PSN = RQ_PSN; // needs to match rq_psn
static const uint16_t MAX_RD_ATOMIC = 16;

// die, TEST_NZ, TEST_Z adapted from ...
static void die(const std::string reason) {
  std::cerr << reason << std::endl;
  exit(EXIT_FAILURE);
}

static const std::string DEVICE_NAME = "mlx4_0";

// TEST_NZ and TEST_Z are macros to automatically check returned errors on
// function calls and exit with an informative message.
#define TEST_NULL(x) do { if ((x) == nullptr) die(__FILE__ ":" + std::to_string(__LINE__) + " " "error: " #x " failed (returned NULL)." ); } while (0)
#define TEST_Z(x)    do { if ((x) == 0)       die(__FILE__ ":" + std::to_string(__LINE__) + " " "error: " #x " failed (returned zero)."); } while (0)
#define TEST_NZ(x)   do { if ((x) != 0)       die(__FILE__ ":" + std::to_string(__LINE__) + " " "error: " #x " failed (returned non-zero)." ); } while (0)

// CreateContext tries to create an ibv_context from InfiniBand device 0.
// For this program, each process only needs 1 context to setup the necessary
// RDMA structures, so this should be called exactly once in the program.
//
struct ibv_context *CreateContext() {
  static bool ran_once = false;
  if (ran_once)
    die("CreateContext already ran once");
  else
    ran_once = true;

  // Get a list of InfiniBand devices, and the number of devices.
  // These devices are physical cards on the machine.
  // Each Sampa node has a single IB device.
  int num_devices;
  struct ibv_device **device_list;
  TEST_NULL(device_list = ibv_get_device_list(&num_devices));
  if (num_devices < DEVICE_NUM) {
    die("CreateContext: not enouch infiniband devices");
  }
  // std::cout << "CreateContext: found " << num_devices << " device(s)" << std::endl;

  // Choose a device.
  struct ibv_device *device = nullptr;
  for( int i = 0; i < num_devices; ++i ) {
    const char *device_name;
    TEST_NULL(device_name = ibv_get_device_name(device_list[i]));
    if (DEVICE_NAME == ibv_get_device_name(device_list[i])) {
      device = device_list[i];
    }
  }
  if (device == nullptr) {
    die("CreateContext: desired device not found");
  }

  // Create a context from the device. (Sort of like opening a file.)
  struct ibv_context *context;
  TEST_NULL(context = ibv_open_device(device));
  // std::cout << "CreateContext: created context from device" << std::endl;

  return context;
}

// We'll use 1 completion queue to check that our RDMA ops have completed.
struct ibv_cq *CreateCompletionQueue(ibv_context *context) {
  struct ibv_cq *cq;
  TEST_NULL(cq = ibv_create_cq(context, COMPLETION_QUEUE_ENTRIES, NULL, NULL, 0));
  // std::cout << "CreateCompletionQueue: success" << std::endl;

  return cq;
}

// If we're the consumer, this protection domain should hold the RDMA memory
// and queue pairs for each producer.
// If we're the producer, this PD should hold just 1 queue pair that's
// connected to a consumer queue pair.
struct ibv_pd *CreateProtectionDomain(ibv_context *context) {
  ibv_pd *pd;
  TEST_NULL(pd = ibv_alloc_pd(context));
  // std::cout << "CreateProtectionDomain: success" << std::endl;

  return pd;
}

uint16_t GetLid(ibv_context *context) {
  struct ibv_port_attr port_attr;
  TEST_NZ(ibv_query_port(context, 1, &port_attr));
  return port_attr.lid;
}

struct ibv_qp *CreateQueuePair(ibv_pd *pd, ibv_cq *cq) {
  struct ibv_qp_init_attr qp_init_attr = {
    NULL, // void *qp_context: not used
    cq,   // struct ibv_cq *send_cq
    cq,   // struct ibv_cq *recv_cq
    NULL, // struct ibv_srq *srq
    {     // struct ibv_qp_cap	cap
      MAX_RECV_WR,      // uint32_t max_send_wr
      MAX_SEND_WR,      // uint32_t max_recv_wr
      MAX_SEND_SGE,     // uint32_t max_send_sge
      MAX_RECV_SGE,     // uint32_t max_recv_sge
      MAX_INLINE_DATA,  // uint32_t max_inline_data
    },
    IBV_QPT_RC, // enum ibv_qp_type qp_type: need IBV_QPT_RC for atomic ops
    1,          // int sq_sig_all: yes, we want to all ops to generate the completion queue events
    NULL,       // struct ibv_xrc_domain *xrc_domain: not using XRC
  };

  struct ibv_qp *qp;
  TEST_NULL(qp = ibv_create_qp(pd, &qp_init_attr));
  // std::cout << "CreateQueuePair: success" << std::endl;

  return qp;
}

struct ibv_mr *RegisterMemory(ibv_pd *pd, void *addr, size_t length) {
  struct ibv_mr *mr = NULL;
  TEST_Z(ibv_reg_mr(pd, addr, length, ACCESS_FLAGS));
  std::cout << "RegisterMemory: success" << std::endl;

  return mr;
}

// We want to get 2 processes (which may/may not be on differet machines) to
// talk with each other over RDMA. We do this by connecting their respective
// queue pairs, which involves some bizarre incantations. (See 3.5)
//
// This function needs to be run on both sides of the connection.
//
// Each RDMA device has a local identifier (LID) that's unique across the
// network of machines. Each queue pair created on an LID has a unique qp_num.
// (See Glossary for LID definition)
// So the combination of remote_lid and remote_qp_num identifies an RDMA
// process.
//
// remote_lid and remote_qp_num should be learned through some other
// communication mechanism, such as TCP sockets, message passing interface (MPI)
// or RDMA communication manager (RDMA CM).
//
void ConnectQueuePair(ibv_qp *local_qp, int remote_lid, int remote_qp_num) {
  // qp: reset -> init
  struct ibv_qp_attr init_qp_attr;
  std::memset(&init_qp_attr, 0, sizeof(init_qp_attr));
  init_qp_attr.qp_state = IBV_QPS_INIT;
  init_qp_attr.pkey_index = 0; // not quite sure what this partition key means, 0 seems to be the right choice.
  init_qp_attr.port_num = PHYS_PORT_NUM; // IB device port num (sampa has 1 physical port)
  init_qp_attr.qp_access_flags = ACCESS_FLAGS;

  TEST_NZ(ibv_modify_qp(local_qp, &init_qp_attr,
    IBV_QP_STATE |       // we need to pass a flag in for each ibv_qp_attr field we set (See 3.5)
    IBV_QP_PKEY_INDEX |
    IBV_QP_PORT |
    IBV_QP_ACCESS_FLAGS));

  // qp: init -> rtr
  struct ibv_qp_attr rtr_qp_attr;
  std::memset(&rtr_qp_attr, 0, sizeof(rtr_qp_attr));
  rtr_qp_attr.qp_state = IBV_QPS_RTR;
  rtr_qp_attr.path_mtu = PATH_MTU; // lowest mtu to be safe
  rtr_qp_attr.ah_attr.dlid = remote_lid; // destination LID
  rtr_qp_attr.ah_attr.port_num = PHYS_PORT_NUM; // destination port num
  rtr_qp_attr.ah_attr.is_global = 0; // destination port num
  rtr_qp_attr.ah_attr.sl = 0; // destination port num
  rtr_qp_attr.ah_attr.src_path_bits = 0; // destination port num
  rtr_qp_attr.dest_qp_num = remote_qp_num;
  rtr_qp_attr.rq_psn = RQ_PSN; // starting recv packet sequence number
  rtr_qp_attr.max_dest_rd_atomic = MAX_DEST_RD_ATOMIC; // resources for incoming RDMA requests
  rtr_qp_attr.min_rnr_timer = MIN_RNR_TIMER; // Mellanox recommendation

  TEST_NZ(ibv_modify_qp(local_qp, &rtr_qp_attr,
                IBV_QP_STATE |
                IBV_QP_AV |
                IBV_QP_PATH_MTU |
                IBV_QP_DEST_QPN |
                IBV_QP_RQ_PSN |
                IBV_QP_MAX_DEST_RD_ATOMIC |
                IBV_QP_MIN_RNR_TIMER));

  // qp: rtr -> rts
  struct ibv_qp_attr rts_qp_attr;
  memset(&rts_qp_attr, 0, sizeof(rts_qp_attr));
  rts_qp_attr.qp_state = IBV_QPS_RTS;
  rts_qp_attr.timeout = TIMEOUT;             // Mellanox recommendation
  rts_qp_attr.retry_cnt = RETRY_CNT;         // Mellanox recommendation
  rts_qp_attr.rnr_retry = RNR_RETRY;         // Mellanox recommendation
  rts_qp_attr.sq_psn = SQ_PSN;               // send packet sequence number, should match rq_psn
  rts_qp_attr.max_rd_atomic = MAX_RD_ATOMIC; // # of outstanding RDMA reads and atomic ops allowed

  TEST_NZ(ibv_modify_qp(local_qp, &rts_qp_attr,
    IBV_QP_STATE |
    IBV_QP_TIMEOUT |
    IBV_QP_RETRY_CNT |
    IBV_QP_RNR_RETRY |
    IBV_QP_SQ_PSN |
    IBV_QP_MAX_QP_RD_ATOMIC));
  std::cout << "SUCCESS" << std::endl;
}
