// Copyright (c) 2014-2016, The Regents of the University of California.
// Copyright (c) 2016-2017, Nefeli Networks, Inc.
// All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// * Redistributions of source code must retain the above copyright notice, this
// list of conditions and the following disclaimer.
//
// * Redistributions in binary form must reproduce the above copyright notice,
// this list of conditions and the following disclaimer in the documentation
// and/or other materials provided with the distribution.
//
// * Neither the names of the copyright holders nor the names of their
// contributors may be used to endorse or promote products derived from this
// software without specific prior written permission.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.

#ifndef BESS_MODULES_BUFFER_H_
#define BESS_MODULES_BUFFER_H_
#include "../module.h"
#include <wait.h>
#include "../pb/module_msg.pb.h"
#include "../kmod/llring.h"


#include <stdio.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <iostream>

using namespace std;

/* TODO: timer-triggered flush */
class Buffer final : public Module {
 public:
  
  Buffer() : Module(), buf_(), queue_(), burst_(), size_(), high_water_(),
        low_water_(), prefetch_() {
    is_task_ = true;
    propagate_workers_ = false;
    max_allowed_workers_ = Worker::kMaxWorkers;
  }
  void DeInit() override;

  void ProcessBatch(Context *ctx, bess::PacketBatch *batch) override;
  struct task_result RunTask(Context *ctx, bess::PacketBatch *batch,
                             void *arg) override;

  static const Commands cmds;
  CommandResponse CommandAddPDUSession(const bess::pb::BufferCommandAddPDUSessionArg &arg);
  CommandResponse CommandRelease(const bess::pb::BufferCommandReleaseArg &arg);
  CommandResponse CommandAddUDPSocket(const bess::pb::BufferCommandAddUdpSocketArg &arg);
  CommandResponse Init(const bess::pb::EmptyArg &arg);

  int SendPfcpReport();
  

 private:

  // Packets Buffer Vars 
  bess::PacketBatch buf_; //private buffer batch
  int release_flag = 0; //0: buffer incoming packet 1: release buffered packet
  struct list{
    bess::PacketBatch buf_;
    uint farID;
    struct list *next;
    int releaseFlag; // 0: buffer, 1: Release
    int notifyCpFlag; // 0: Don't notify, 1: notify
    //list of UE
  };
  struct list *head = NULL;

  //Session Report Vars
  sockaddr_in serAdd;
  char *pfcpAgentAddr;
  int client = socket (AF_INET, SOCK_DGRAM, IPPROTO_UDP);
  int portNum = 8805;
  //lient = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

  //Packet Queue Related Variables
  const double kHighWaterRatio = 0.90;
  const double kLowWaterRatio = 0.15;
  int Resize(int slots);
  // Readjusts the water level according to `size_`.
  void AdjustWaterLevels();
  struct llring *queue_; //ring buffer
  // Whether backpressure should be applied or not
  bool backpressure_;
  int burst_;
  // Queue capacity
  uint64_t size_;
  // High water occupancy
  uint64_t high_water_;
  // Low water occupancy
  uint64_t low_water_;
  bool prefetch_;
};
#endif  // BESS_MODULES_BUFFER_H_
