#include "verbs_wrap.hpp"

#include <mpi.h>
#include <iostream>
#include <cstdlib>
#include <cstdio>

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

// make && srun --label --nodes=2 --ntasks-per-node=3 ./simple_atomic_increment
// make && srun --label --nodes=2 --ntasks-per-node=1 ./main
int main(int argc, char **argv) {
  // MPIConnection mpi( &argc, &argv );
  MPI_Check(MPI_Init(&argc, &argv));
  int rank;
  MPI_Check(MPI_Comm_rank(MPI_COMM_WORLD, &rank));
  std::cout << "my rank: " << rank << std::endl;
  int size;
  MPI_Check(MPI_Comm_size(MPI_COMM_WORLD, &size));
  std::cout << "size: " << size << std::endl;
  static const int IGNORE_SEND_TAG = 0;

  if (rank == 0) {
    ibv_context *context = CreateContext();
    ibv_cq *cq = CreateCompletionQueue(context);
    ibv_pd *pd = CreateProtectionDomain(context);
    uint16_t lid = GetLid(context);

    ibv_qp *qp = CreateQueuePair(pd, cq);
    uint32_t qp_num = qp->qp_num;
    ConnectQueuePair(qp, lid, qp_num);
    // for (int i = 1; i < size; ++i) {
    //
    //   // send lid and qp_num
    //   MPI_Check(MPI_Send(&lid, sizeof(uint16_t), MPI_UINT16_T, i, IGNORE_SEND_TAG, MPI_COMM_WORLD));
    //   MPI_Check(MPI_Send(&qp_num, sizeof(uint32_t), MPI_UINT32_T, i, IGNORE_SEND_TAG, MPI_COMM_WORLD));
    //   std::cout << "consumer: sent lid and qp_num " << lid << " " << qp_num << std::endl;
    //
    //   // receive remote lid and qp_num
    //   uint16_t remote_lid;
    //   uint32_t remote_qp_num;
    //   MPI_Check(MPI_Recv(&remote_lid, sizeof(uint16_t), MPI_UINT16_T, i, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    //   std::cout << "consumer: received lid " << remote_lid << std::endl;
    //   MPI_Check(MPI_Recv(&remote_qp_num, sizeof(uint32_t), MPI_UINT32_T, i, MPI_ANY_TAG, MPI_COMM_WORLD, MPI_STATUS_IGNORE));
    //   std::cout << "consumer: received qp_num " << remote_qp_num << std::endl;
    //
    // }
  } else {
    ibv_context *context = CreateContext();
    ibv_cq *cq = CreateCompletionQueue(context);
    ibv_pd *pd = CreateProtectionDomain(context);
    uint16_t lid = GetLid(context);

    ibv_qp *qp = CreateQueuePair(pd, cq);
    uint32_t qp_num = qp->qp_num;

    // send lid and qp_num
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

    ConnectQueuePair(qp, remote_lid, remote_qp_num);
  }
  MPI_Finalize();

  // if (mpi.rank == 0) { // consumer
  //   while (true) {
  //     std::cout << "receiving!" << std::endl;
  //     // receive magic numbers
  //     uint8_t magic_number;
  //     int res = MPI_Recv(
  //       &magic_number,    // buf
  //       1,                // count
  //       MPI_UINT8_T,      // datatype
  //       MPI_ANY_SOURCE,   // source
  //       MPI_ANY_TAG,      // tag
  //       MPI_COMM_WORLD,   // comm
  //       MPI_STATUS_IGNORE // status
  //     );
  //     mpi
  //     if (res != MPI_SUCCESS)
  //       std::cerr << "receiver error" << std::endl;
  //     std::cout << "magic number: " << magic_number << std::endl;
  //   }
  // } else {
  //   // ibv_context *context = CreateContext();
  //   // ibv_cq *cq = CreateCompletionQueue(context);
  //   // ibv_pd *pd = CreateProtectionDomain(context);
  //   // ibv_qp *qp = CreateQueuePair(pd, cq);
  //
  //   // send magic number
  //   uint8_t magic_number = 42;
  //   int res = MPI_Ssend(
  //     &magic_number, // buf
  //     1,             // count
  //     MPI_UINT8_T,   // datatype
  //     0,             // dest
  //     MPI_ANY_TAG,   // tag
  //     MPI_COMM_WORLD // comm
  //   );
  //   if (res != MPI_SUCCESS)
  //     std::cerr << "sender error" << std::endl;
  // }
}
