// This code uses MPI (Message Passing Interface) to setup RDMA connections
// between 1 consumer node and many producer nodes. It is intended as
// scaffolding for a single-consumer, multi-producer queue over RDMA.
//
// make && srun --label --nodes=5 --ntasks-per-node=1 ./main

#include "verbs_wrap.hpp"

#include <mpi.h>
#include <iostream>
#include <cstdlib>
#include <cstdio>

// MPI_Check should wrap every MPI call to check for an error, print it
// if there is one, and exit.
#define MPI_Check(mpi_call)                              \
  do {                                                   \
    int retval = (mpi_call);                             \
    if (retval != MPI_SUCCESS ) {                        \
      char error_string[MPI_MAX_ERROR_STRING];           \
      int length;                                        \
      MPI_Error_string(retval, error_string, &length);   \
      std::cerr << "MPI call failed: " #mpi_call ": "    \
                << error_string << "\n";                 \
      exit(EXIT_FAILURE);                                \
    }                                                    \
  } while(0)

// This is used as the tag in each MPI_Send since we don't need tag numbers
// to keep track of anything.
static const int IGNORE_SEND_TAG = 0;

int main(int argc, char **argv) {
  // initialize MPI
  MPI_Check(MPI_Init(&argc, &argv));

  // get total # of nodes in this MPI task
  int rank;
  MPI_Check(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
  int size;
  MPI_Check(MPI_Comm_size(MPI_COMM_WORLD, &size));

  // setup objects needed by both consumer and producer nodes.
  ibv_context *context = CreateContext();
  ibv_cq *cq = CreateCompletionQueue(context);
  ibv_pd *pd = CreateProtectionDomain(context);
  uint16_t lid = GetLid(context);

  if (rank == 0) {
    // This is code for the consumer.

    // create and connect a queue pair for each producer
    for (int i = 1; i < size; ++i) {

      // create queue pair
      ibv_qp *qp = CreateQueuePair(pd, cq);
      uint32_t qp_num = qp->qp_num;

      // send lid and qp_num
      MPI_Check(MPI_Send(&lid, sizeof(uint16_t), MPI_UINT16_T, i, IGNORE_SEND_TAG, MPI_COMM_WORLD));
      MPI_Check(MPI_Send(&qp_num, sizeof(uint32_t), MPI_UINT32_T, i, IGNORE_SEND_TAG, MPI_COMM_WORLD));
      std::cout << "consumer: sent lid and qp_num " << lid << " " << qp_num << std::endl;

      // receive remote lid and qp_num
      uint16_t remote_lid;
      uint32_t remote_qp_num;
      MPI_Check(MPI_Recv(&remote_lid, sizeof(uint16_t), MPI_UINT16_T, i, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
      std::cout << "consumer: received lid " << remote_lid << std::endl;
      MPI_Check(MPI_Recv(&remote_qp_num, sizeof(uint32_t), MPI_UINT32_T, i, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
      std::cout << "consumer: received qp_num " << remote_qp_num << std::endl;

      // connect to producer queue pair
      ConnectQueuePair(qp, remote_lid, remote_qp_num);
    }
    std::cout << "consumer: all producer connections succeeded" << std::endl;

  } else {

    // This is code for the producer

    // create queue pair
    ibv_qp *qp = CreateQueuePair(pd, cq);
    uint32_t qp_num = qp->qp_num;

    // send lid and qp_num to consumer
    MPI_Check(MPI_Send(&lid, sizeof(uint16_t), MPI_UINT16_T, 0, IGNORE_SEND_TAG, MPI_COMM_WORLD));
    MPI_Check(MPI_Send(&qp_num, sizeof(uint32_t), MPI_UINT32_T, 0, IGNORE_SEND_TAG, MPI_COMM_WORLD));
    std::cout << "producer: sent lid and qp_num " << lid << " " << qp_num << std::endl;

    // receive remote lid and qp_num
    uint16_t remote_lid;
    uint32_t remote_qp_num;
    MPI_Check(MPI_Recv(&remote_lid, sizeof(uint16_t), MPI_UINT16_T, 0, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    std::cout << "producer: received lid " << remote_lid << std::endl;
    MPI_Check(MPI_Recv(&remote_qp_num, sizeof(uint32_t), MPI_UINT32_T, 0, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    std::cout << "producer: received qp_num " << remote_qp_num << std::endl;

    // connect to consumer queue pair
    ConnectQueuePair(qp, remote_lid, remote_qp_num);

    std::cout << "producer: connection to consumer succeeded" << std::endl;
  }
}
