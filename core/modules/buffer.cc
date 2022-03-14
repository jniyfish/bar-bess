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


const Commands Buffer::cmds = {
    {"release", "EmptyArg", MODULE_CMD_FUNC(&Buffer::CommandRelease),
     Command::THREAD_UNSAFE},
    {"add", "PDUSessionArg", MODULE_CMD_FUNC(&Buffer::CommandAddPDUSession),
     Command::THREAD_SAFE}
    };

void Buffer::DeInit() {
  bess::PacketBatch *buf = &buf_;
  bess::Packet::Free(buf);
}

void Buffer::ProcessBatch(Context *ctx, bess::PacketBatch *batch) {
  bess::PacketBatch *buf = NULL;
  struct list *ptr = this->head;
  uint left = batch->cnt();
  for (uint i = 0; i < left; i++) {
    bess::Packet *pkt = batch->pkts()[i];
    uint farId = get_attr<uint32_t>(this, 0, pkt);
    while(ptr!=NULL){
      if(farId == ptr->farID){
          buf = &ptr->buf_;
          bess::Packet **p_buf = &buf->pkts()[buf->cnt()];
          bess::Packet **p_batch = &batch->pkts()[i];
          buf->incr_cnt(1);
          bess::utils::CopyInlined(p_buf, p_batch, 1 * sizeof(bess::Packet *));
      }
      ptr = ptr->next;
    }
  }
  


  if(1>2)
    RunNextModule(ctx, batch);
}
struct task_result Buffer::RunTask(Context *ctx, bess::PacketBatch *batch, void *) {
  bess::PacketBatch *buf = NULL;
  struct list *ptr = this->head;
  while(ptr!=NULL){
    if (ptr->releaseFlag == 1) {
      ptr->releaseFlag = 0;
      buf = &ptr->buf_;
      buf->set_cnt( buf->cnt() + batch->cnt());
      bess::PacketBatch *new_batch = ctx->task->AllocPacketBatch();
      new_batch->Copy(buf);
      buf->clear();
      RunNextModule(ctx, new_batch);
    }
    ptr = ptr->next;
  }
      return {
        .block = true,
        .packets = 0,
        .bits = 0,
    };
}
//buffer incoming packet batch to private buffer batch
CommandResponse Buffer::CommandRelease(const bess::pb::BufferCommandReleaseArg &arg) {
  struct list *ptr = head;
  while(ptr!=NULL){
    if(ptr->farID  == arg.farid()){
      ptr->releaseFlag = 1;
    }
    ptr=ptr->next;
  }
  return CommandSuccess();
}

CommandResponse Buffer::CommandAddPDUSession(const bess::pb::PDUSessionArg &arg) {
  if(head == NULL) {
      head = (struct list*)malloc(sizeof(struct list));
      head->buf_ = buf_;
      head->farID = arg.farid();
      head->releaseFlag = 0;
      head->next = NULL;
      return CommandSuccess();
  }
  struct list *ptr = head;
  while(ptr!=NULL){
    if (ptr->farID == arg.farid()) {
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
          ptr->next->next = NULL;
      }
    }
  }

  return CommandSuccess();
}

CommandResponse Buffer::Init(const bess::pb::EmptyArg &) {
  using AccessMode = bess::metadata::Attribute::AccessMode;

  AddMetadataAttr("farid", 4, AccessMode::kRead);
  //AddMetadataAttr("ip_dst", 4, AccessMode::kRead);

  task_id_t tid;
  CommandResponse err;

  tid = RegisterTask(nullptr);
  if (tid == INVALID_TASK_ID) {
    return CommandFailure(ENOMEM, "Task creation failed");
  }

  return CommandSuccess();
}

ADD_MODULE(Buffer, "buffer", "buffers packets into larger batches")
