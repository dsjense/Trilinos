// @HEADER
//
// ***********************************************************************
//
//        MueLu: A package for multigrid based preconditioning
//                  Copyright 2012 Sandia Corporation
//
// Under the terms of Contract DE-AC04-94AL85000 with Sandia Corporation,
// the U.S. Government retains certain rights in this software.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are
// met:
//
// 1. Redistributions of source code must retain the above copyright
// notice, this list of conditions and the following disclaimer.
//
// 2. Redistributions in binary form must reproduce the above copyright
// notice, this list of conditions and the following disclaimer in the
// documentation and/or other materials provided with the distribution.
//
// 3. Neither the name of the Corporation nor the names of the
// contributors may be used to endorse or promote products derived from
// this software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY SANDIA CORPORATION "AS IS" AND ANY
// EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
// PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL SANDIA CORPORATION OR THE
// CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
// EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
// PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
// PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
// LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
// NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
// SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
//
// Questions? Contact
//                    Jonathan Hu       (jhu@sandia.gov)
//                    Andrey Prokopenko (aprokop@sandia.gov)
//                    Ray Tuminaro      (rstumin@sandia.gov)
//
// ***********************************************************************
//
// @HEADER
#include <cstdio>
#include <cmath>
#include <numeric>
#include <utility>
#include <chrono>
#include <Teuchos_StackedTimer.hpp>
#include <Teuchos_ScalarTraits.hpp>
#include "MueLu_PerfModels_decl.hpp"

#ifdef HAVE_MPI
#include <mpi.h>
#endif

namespace MueLu {

  namespace PerfDetails {
    template<class Scalar,class Node>
    double stream_vector_add(int KERNEL_REPEATS, int VECTOR_SIZE) {      
      using exec_space   = typename Node::execution_space;
      using memory_space = typename Node::memory_space;
      using range_policy = Kokkos::RangePolicy<exec_space>;

      Kokkos::View<Scalar*,memory_space> a("a", VECTOR_SIZE);
      Kokkos::View<Scalar*,memory_space> b("b", VECTOR_SIZE);
      Kokkos::View<Scalar*,memory_space> c("c", VECTOR_SIZE);
      double total_test_time = 0.0;

      Scalar ONE = Teuchos::ScalarTraits<Scalar>::one();

      Kokkos::parallel_for("stream/fill",range_policy(0,VECTOR_SIZE), KOKKOS_LAMBDA (const size_t i) {
          a(i) = ONE * i;
          b(i) = a(i);
        });
      exec_space().fence();

      using clock = std::chrono::high_resolution_clock;

      clock::time_point start, stop;

      for(int i = 0; i < KERNEL_REPEATS; i++) {       
        start = clock::now();
        Kokkos::parallel_for("stream/add",range_policy(0,VECTOR_SIZE), KOKKOS_LAMBDA (const size_t j) { //Vector Addition
            c(j) = a(j) + b(j);
        });

        exec_space().fence();
        stop = clock::now();
        double my_test_time = std::chrono::duration<double>(stop - start).count();
        total_test_time += my_test_time;
      }

      return total_test_time / KERNEL_REPEATS;
    }

    template<class Scalar,class Node>
    double stream_vector_copy(int KERNEL_REPEATS, int VECTOR_SIZE) {      
      using exec_space   = typename Node::execution_space;
      using memory_space = typename Node::memory_space;
      using range_policy = Kokkos::RangePolicy<exec_space>;

      Kokkos::View<Scalar*,memory_space> a("a", VECTOR_SIZE);
      Kokkos::View<Scalar*,memory_space> b("b", VECTOR_SIZE);
      double total_test_time = 0.0;

      Scalar ONE = Teuchos::ScalarTraits<Scalar>::one();

      Kokkos::parallel_for("stream/fill",range_policy(0,VECTOR_SIZE), KOKKOS_LAMBDA (const size_t i) {
          a(i) = ONE;
        });
      exec_space().fence();

      using clock = std::chrono::high_resolution_clock;
      clock::time_point start, stop;

      for(int i = 0; i < KERNEL_REPEATS; i++) {       
        start = clock::now();
        Kokkos::parallel_for("stream/copy",range_policy(0,VECTOR_SIZE), KOKKOS_LAMBDA (const size_t j) { //Vector Addition
            b(j) = a(j);
        });

        exec_space().fence();
        stop = clock::now();
        double my_test_time = std::chrono::duration<double>(stop - start).count();
        total_test_time += my_test_time;
      }

      return total_test_time / KERNEL_REPEATS;
    }


 
    double table_lookup(const std::vector<int> & x, const std::vector<double> & y, int value) {
      // If there's no table, nan
      if(x.size() == 0) return Teuchos::ScalarTraits<double>::nan();
      
      // NOTE:  This should probably be a binary search, but this isn't performance sensitive, so we'll go simple
      int N = (int) x.size();
      int hi = 0;
      for(  ; hi < N; hi++) {
        if (x[hi] > value) 
          break;
      }
      
      if(hi == 0) {
        // Lower end (return the min time)
        //printf("Lower end: %d < %d\n",value,x[0]);
        return y[0];
      }
      else if (hi == N) {
        // Higher end (extrapolate from the last two points)
        //printf("Upper end: %d > %d\n",value,x[N-1]);
        hi = N-1;
        int run     = x[hi] - x[hi-1];
        double rise = y[hi] - y[hi-1];
        double slope = rise / run;     
        int diff = value - x[hi-1];
        
        return y[hi-1] + slope * diff;
      }
      else {
        // Interpolate
        //printf("Middle: %d < %d < %d\n",x[hi-1],value,x[hi]);
        int run     = x[hi] - x[hi-1];
        double rise = y[hi] - y[hi-1];
        double slope = rise / run;     
        int diff = value - x[hi-1];
        
        return y[hi-1] + slope * diff;
      }
    }

    // Report bandwidth in GB / sec
    const double GB = 1024.0 * 1024.0 * 1024.0;
    double convert_time_to_bandwidth_gbs(double time, int num_calls, double memory_per_call_bytes) {
      double time_per_call = time / num_calls;      
      return memory_per_call_bytes / GB / time_per_call;
    }



  }// end namespace PerfDetails

 
  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  double
  PerfModels<Scalar, LocalOrdinal, GlobalOrdinal, Node>::stream_vector_add_SC(int KERNEL_REPEATS, int VECTOR_SIZE) {      
    return PerfDetails::stream_vector_add<Scalar,Node>(KERNEL_REPEATS,VECTOR_SIZE);
  }


  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  double
  PerfModels<Scalar, LocalOrdinal, GlobalOrdinal, Node>::stream_vector_add_LO(int KERNEL_REPEATS, int VECTOR_SIZE) {      
    return PerfDetails::stream_vector_add<LocalOrdinal,Node>(KERNEL_REPEATS,VECTOR_SIZE);
  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  double
  PerfModels<Scalar, LocalOrdinal, GlobalOrdinal, Node>::stream_vector_add_size_t(int KERNEL_REPEATS, int VECTOR_SIZE) {      
    return PerfDetails::stream_vector_add<size_t,Node>(KERNEL_REPEATS,VECTOR_SIZE);
  }

  
  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  double
  PerfModels<Scalar, LocalOrdinal, GlobalOrdinal, Node>::stream_vector_copy_SC(int KERNEL_REPEATS, int VECTOR_SIZE) {      
    return PerfDetails::stream_vector_copy<Scalar,Node>(KERNEL_REPEATS,VECTOR_SIZE);
  }
 
  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  void 
  PerfModels<Scalar, LocalOrdinal, GlobalOrdinal, Node>::stream_vector_copy_make_table(int KERNEL_REPEATS,  int LOG_MAX_SIZE) {
    if(LOG_MAX_SIZE < 2)
      LOG_MAX_SIZE=20;

    stream_copy_sizes_.resize(LOG_MAX_SIZE+1);    
    stream_copy_times_.resize(LOG_MAX_SIZE+1);    
        
    for(int i=0; i<LOG_MAX_SIZE+1; i++) {
      int size = (int) pow(2,i);
      double time = PerfDetails::stream_vector_copy<Scalar,Node>(KERNEL_REPEATS,size);

      double time2 = PerfDetails::stream_vector_add<Scalar,Node>(KERNEL_REPEATS,size);

      printf("Timer per call: %8d %10.4f %10.4f us    %10.4f %10.4f GB/sec\n",size,time*1e6,time2*1e6,
             PerfDetails::convert_time_to_bandwidth_gbs(time,1,2*size*sizeof(Scalar)),
             PerfDetails::convert_time_to_bandwidth_gbs(time2,1,3*size*sizeof(Scalar)));

      stream_copy_sizes_[i] = size;

      stream_copy_times_[i] = time;
    }

  }

  template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  double
  PerfModels<Scalar, LocalOrdinal, GlobalOrdinal, Node>::stream_vector_copy_lookup(int SIZE_IN_BYTES) {
    return PerfDetails::table_lookup(stream_copy_sizes_,stream_copy_times_,SIZE_IN_BYTES/sizeof(Scalar));
  }


 template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  std::map<int,double> 
  PerfModels<Scalar, LocalOrdinal, GlobalOrdinal, Node>::pingpong_test_host(int KERNEL_REPEATS, int MAX_SIZE, const RCP<const Teuchos::Comm<int> > &comm) {
    std::map<int, double> time_map;

#ifdef HAVE_MPI
    using Teuchos::BaseTimer;
    int msg_length, i, j;
    std::vector<int> msg_arr(MAX_SIZE+1); // values from 0,1,2... to 2^15. Sizes of each buffer send
    char  *s_buf, *r_buf;  // Send & recieve buffers
    double t_avg;
    std::vector<double> time_array(KERNEL_REPEATS); //Stores the times for every single kernel repetition. Reset with each repeat.
    
    BaseTimer timer;   
    RCP<Teuchos::CommRequest<int> > request;
    RCP<Teuchos::CommStatus<int> > status;    
    int rank = comm->getRank();
    int nproc = comm->getSize();

    if(nproc < 2)
      return time_map;

    msg_arr[0] = 0;
    for(i = 0; i < MAX_SIZE; i++) {
      msg_arr[i+1] = (int) pow(2,i);
    }
    
    //Allocating memory for the buffers.
    MPI_Alloc_mem( (int) pow(2,MAX_SIZE), MPI_INFO_NULL, &r_buf);
    MPI_Alloc_mem( (int) pow(2,MAX_SIZE), MPI_INFO_NULL, &s_buf);
    
    // Populate send buffer
    for(i = 0; i < (int) pow(2,MAX_SIZE); i++)
      s_buf[i] = 1;
    
    //Send and recieve.
    for(msg_length = 0; msg_length < MAX_SIZE + 1 ; msg_length++) {
      comm->barrier();
      
      for(j = 0; j < KERNEL_REPEATS; j++) {
        timer.start();
        
        if(rank == 1) {
          comm->send(msg_arr[msg_length], s_buf, 0);
        }
        
        else if(rank == 0){
          comm->receive(1, msg_arr[msg_length],r_buf);
        }
        
        timer.stop();
        time_array[j] = timer.accumulatedTime() * 1.0e6; // Formmated in microseconds (us)
        timer.reset();
      }
      
      t_avg = std::accumulate(time_array.begin(), time_array.end(), 0.0) / KERNEL_REPEATS;
      
      time_map.insert(std::pair<int, double>(msg_arr[msg_length],t_avg));
      
    }
#endif
    return time_map;
  }


template <class Scalar, class LocalOrdinal, class GlobalOrdinal, class Node>
  std::map<int,double> 
PerfModels<Scalar, LocalOrdinal, GlobalOrdinal, Node>::pingpong_test_device(int KERNEL_REPEATS, int MAX_SIZE,const RCP<const Teuchos::Comm<int> > &comm) {
    std::map<int, double> time_map;
#ifdef HAVE_MPI
    using exec_space   = typename Node::execution_space;
    using memory_space = typename Node::memory_space;
    using range_policy = Kokkos::RangePolicy<exec_space>;
    using Teuchos::BaseTimer;
    int msg_length, i, j;
    const int buff_size = (int) pow(2,MAX_SIZE);

    double t_avg;
    std::vector<double> time_array(KERNEL_REPEATS); //Stores the times for every single kernel repetition. Reset with each repeat.
    
    BaseTimer timer;   
    RCP<Teuchos::CommRequest<int> > request;
    RCP<Teuchos::CommStatus<int> > status;    
    int rank  = comm->getRank();
    int nproc = comm->getSize();

    if(nproc < 2)
      return time_map;
    
    // Precompute message sizes
    std::vector<int> msg_arr(MAX_SIZE+1);
    msg_arr[0]=0;
    for(i = 0; i < MAX_SIZE; i++) {
      msg_arr[i+1] = (int) pow(2,i);
    }
        
    // Allocate memory for the buffers (and fill send)
    Kokkos::View<char*,memory_space> r_buf("recv",buff_size), s_buf("send",buff_size);
    Kokkos::deep_copy(s_buf,1);


    //Send and recieve.   

    // NOTE:  Do consectutive pair buddies here for simplicity.  We should be smart later
    int odd = rank % 2;
    int buddy = odd ? rank - 1 : rank + 1;
   
    for(msg_length = 0; msg_length < MAX_SIZE + 1 ; msg_length++) {
      comm->barrier();
      
      for(j = 0; j < KERNEL_REPEATS; j++) {
        timer.start();

        if (buddy < nproc) {
          if (odd)
            comm->send(msg_arr[msg_length], (char*)s_buf.data(), buddy);
          else
            comm->receive(buddy, msg_arr[msg_length],(char*)r_buf.data());
        }

        timer.stop();
        time_array[j] = timer.accumulatedTime() * 1.0e6; // Formmated in microseconds (us)
        timer.reset();
      }
      
      t_avg = std::accumulate(time_array.begin(), time_array.end(), 0.0) / KERNEL_REPEATS;
      
      time_map.insert(std::pair<int, double>(msg_arr[msg_length],t_avg));
      
    }
#endif
    return time_map;
  }





} //namespace MueLu
