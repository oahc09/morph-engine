﻿#include "NetMessage.hpp"

NetMessage::NetMessage(const NetMessage& msg)
  : NetMessage() {
  mNextWrite = msg.mNextWrite;
  mNextRead = msg.mNextRead;
  mDefinition = msg.mDefinition;
  mIndex = msg.mIndex;
  mName = msg.mName;
  memcpy(mLocalBuffer, msg.mLocalBuffer, NET_MESSAGE_MTU);
}

NetMessage& NetMessage::operator=(const NetMessage& rhs) {
  new(this)NetMessage(rhs);
  return *this;
}

void NetMessage::setDefinition(const Def& def) {
  mDefinition = &def;
  mName = mDefinition->name;
  mIndex = mDefinition->index;
}
