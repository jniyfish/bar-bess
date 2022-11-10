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

#include "buffer.h"
#include "../utils/format.h"
#include <cstdlib>
#include "../core/packet.h"

const Commands Buffer::cmds = {
    {"release", "BufferCommandReleaseArg", MODULE_CMD_FUNC(&Buffer::CommandRelease),
     Command::THREAD_SAFE},
    {"add", "BufferCommandAddPDUSessionArg", MODULE_CMD_FUNC(&Buffer::CommandAddPDUSession),
     Command::THREAD_UNSAFE},
    {"add_socket", "BufferCommandAddUdpSocketArg", MODULE_CMD_FUNC(&Buffer::CommandAddUDPSocket),
     Command::THREAD_SAFE}
    };

void Buffer::DeInit() {
  bess::PacketBatch *buf = &buf_;
  bess::Packet::Free(buf);
}

void Buffer::ProcessBatch(Context *, bess::PacketBatch *batch) {
  bess::PacketBatch *buf = NULL;
  struct list *ptr = this->head;
  uint left = batch->cnt();
  if(ptr!=NULL) {  //check linked list if has buffer session 
    for (uint i = 0; i < left; i++) {
      bess::Packet *pkt = batch->pkts()[i];
      uint farId = get_attr<uint32_t>(this, 0, pkt);
      while(ptr!=NULL){ //link list traversal
        if(farId == ptr->farID){
          if(ptr->notifyCpFlag == 1) {
            SendPfcpReport(); //Send PFCP Report
            ptr->notifyCpFlag = 0;
          }
          buf = &ptr->buf_;
          bess::Packet **p_buf = &buf->pkts()[buf->cnt()];
          bess::Packet **p_batch = &batch->pkts()[i];
          buf->incr_cnt(1);
          bess::utils::CopyInlined(p_buf, p_batch, 1 * sizeof(bess::Packet *));
          bess::Packet::Free(pkt); //delete buffered packets from pkt batch
        }
        ptr = ptr->next;
      }
    }
  }
    //pass pkt btach from upstream
    //Enqueue forward packets (packets without buffer,nocp flag) to ring buffer
    int queued =
      llring_mp_enqueue_burst(queue_, (void **)batch->pkts(), batch->cnt());
    if (backpressure_ && llring_count(queue_) > high_water_) {
      SignalOverload();
    }

    if (queued < batch->cnt()) {
      int to_drop = batch->cnt() - queued;
      bess::Packet::Free(batch->pkts() + queued, to_drop);
    }
}
struct task_result Buffer::RunTask(Context *ctx, bess::PacketBatch *batch, void *) {
  bess::PacketBatch *buf = NULL;
  struct list *ptr = this->head;
  while(ptr!=NULL){ 
    //Fetch packets from released packets buffer
    //TODO: think a method to check current has released buffer or not
    // -> A global released flag??
    if (ptr->releaseFlag == 1) {
      buf = &ptr->buf_;
      buf->set_cnt( buf->cnt() + batch->cnt());
      bess::PacketBatch *new_batch = ctx->task->AllocPacketBatch();
      new_batch->Copy(buf);
      buf->clear();
      RunNextModule(ctx, new_batch);
      return {
        .block = false,
        .packets = 0,
        .bits = 0,
      };
    }
    ptr = ptr->next;
  }
  //To downstream
  if (children_overload_ > 0) {
    return {
        .block = true,
        .packets = 0,
        .bits = 0,
    };
  }
  const int burst = ACCESS_ONCE(burst_);
  //const int pkt_overhead = 24;

  //Fetch (Dequeue) packets from ring buffer then processing
  //Fetch priority: 1. release buffer, 2. ring buffer
  //Ring Buffer Ops may cause program crash
  //Ring Buffer too dificult QQ
  uint32_t cnt = llring_sc_dequeue_burst(queue_, (void **)batch->pkts(), burst);
  if (cnt == 0) {
    return {.block = true, .packets = 0, .bits = 0};
  }
  batch->set_cnt(cnt);
  RunNextModule(ctx, batch);
      return {
        .block = false,
        .packets = cnt * 24,
        .bits = 0,
    };
}
//buffer incoming packet batch to private buffer batch
CommandResponse Buffer::CommandRelease(const bess::pb::BufferCommandReleaseArg &arg) {
  struct list *ptr = head;
  while(ptr!=NULL){
    if(ptr->farID  == arg.farid()){
      ptr->releaseFlag = 1;
      break;
    }
    ptr=ptr->next;
  }
  return CommandSuccess();
}

CommandResponse Buffer::CommandAddPDUSession(const bess::pb::BufferCommandAddPDUSessionArg &arg) {
  if(head == NULL) {
      head = (struct list*)malloc(sizeof(struct list));
      head->buf_ = buf_;
      head->farID = arg.farid();
      head->releaseFlag = 0;
      head->notifyCpFlag = 1;
      head->next = NULL;
      return CommandSuccess();
  }
  struct list *ptr = head;
  while(ptr!=NULL){
    if (ptr->farID == arg.farid()) {
      head->releaseFlag = 0;
      head->notifyCpFlag = 1;
      return CommandSuccess();
    }
    else if (ptr->farID != arg.farid()){
      if(ptr->next !=NULL){
        ptr = ptr->next;
      }
      else if (ptr->next == NULL){
          ptr->next = (struct list*)malloc(sizeof(struct list));
          ptr->next->buf_ = buf_;
          ptr->next->farID = arg.farid();
          ptr->next->releaseFlag = 0;
          ptr->next->notifyCpFlag = 1;
          ptr->next->next = NULL;
      }
    }
  }

  return CommandSuccess();
}

CommandResponse Buffer::CommandAddUDPSocket(const bess::pb::BufferCommandAddUdpSocketArg &arg) {
  
  this->serAdd.sin_family=AF_INET;
  this->serAdd.sin_port=htons(this->portNum);
  inet_pton(AF_INET, arg.pfcpagentaddr().c_str(), &serAdd.sin_addr);
  if(connect(this->client, (const sockaddr *)&serAdd, sizeof(serAdd))!=0)
    return CommandFailure(1, "connect failed");

  //SendPfcpReport();

  return CommandSuccess();
}

int Buffer::Resize(int slots) {
  struct llring *old_queue = queue_;
  struct llring *new_queue;

  int bytes = llring_bytes_with_slots(slots);

  new_queue =
      reinterpret_cast<llring *>(std::aligned_alloc(alignof(llring), bytes));
  if (!new_queue) {
    return -ENOMEM;
  }

  int ret = llring_init(new_queue, slots, 0, 1);
  if (ret) {
    std::free(new_queue);
    return -EINVAL;
  }

  /* migrate packets from the old queue */
  if (old_queue) {
    bess::Packet *pkt;

    while (llring_sc_dequeue(old_queue, (void **)&pkt) == 0) {
      ret = llring_sp_enqueue(new_queue, pkt);
      if (ret == -LLRING_ERR_NOBUF) {
        bess::Packet::Free(pkt);
      }
    }

    std::free(old_queue);
  }

  queue_ = new_queue;
  size_ = slots;

  return 0;
}

int Buffer::SendPfcpReport() {
  int result;
  const char pfcpmsg[40]={
        0x21, 0x38, 0x00, 0x1b, 
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01, 
        0x00, 0x00, 0x01, 0x00, 0x00, 0x27, 0x00, 0x01, 0x01, 0x00,
        0x53, 0x00, 0x06, 0x00, 0x38, 
        0x00, 0x02, 0x00, 0x02,
  }; //TODO: dynaimic SEID and PDRID, current too lazy

  size_t msg_l = strlen(pfcpmsg);
	msg_l = 31;
  result = sendto(client, pfcpmsg, msg_l, 0, 
    (sockaddr*)&serAdd, sizeof(serAdd));
  if (result < 0)
    return -1;
  return 0;
}

CommandResponse Buffer::Init(const bess::pb::EmptyArg &) {
  using AccessMode = bess::metadata::Attribute::AccessMode;
  this->serAdd.sin_family=AF_INET;
  this->serAdd.sin_port=htons(this->portNum);
  inet_pton(AF_INET, "140.113.194.239", &serAdd.sin_addr);
  AddMetadataAttr("far_id", 4, AccessMode::kRead);

  task_id_t tid;
  CommandResponse err;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return CommandFailure(ENOMEM, "Task creation failed");
  }

//Queueing
  burst_ = bess::PacketBatch::kMaxBurst;

  int ret = Resize(1024); //DEFAULT_QUEUE_SIZE
    if (ret) {
      return CommandFailure(-ret);
    }
// Queueing
  return CommandSuccess();
}

ADD_MODULE(Buffer, "buffer", "buffers packets into larger batches")