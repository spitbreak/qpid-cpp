/*
 *
 * Licensed to the Apache Software Foundation (ASF) under one
 * or more contributor license agreements.  See the NOTICE file
 * distributed with this work for additional information
 * regarding copyright ownership.  The ASF licenses this file
 * to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance
 * with the License.  You may obtain a copy of the License at
 * 
 *   http://www.apache.org/licenses/LICENSE-2.0
 * 
 * Unless required by applicable law or agreed to in writing,
 * software distributed under the License is distributed on an
 * "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY
 * KIND, either express or implied.  See the License for the
 * specific language governing permissions and limitations
 * under the License.
 *
 */

#include "Multicaster.h"
#include "Cpg.h"
#include "ClusterLeaveException.h"
#include "qpid/log/Statement.h"


namespace qpid {
namespace cluster {

Multicaster::Multicaster(Cpg& cpg_, const boost::shared_ptr<sys::Poller>& poller) :
    cpg(cpg_), queue(boost::bind(&Multicaster::sendMcast, this, _1), poller),
    holding(true)
{
    queue.start();
}

void Multicaster::mcastControl(const framing::AMQBody& body, const ConnectionId& id) {
    mcast(Event::control(body, id));
}

void Multicaster::mcastBuffer(const char* data, size_t size, const ConnectionId& id) {
    Event e(DATA, id, size);
    memcpy(e.getData(), data, size);
    mcast(e);
}

void Multicaster::mcast(const Event& e) {
    {
        sys::Mutex::ScopedLock l(lock);
        if (e.getType() == DATA && e.isConnection() && holding) {
            holdingQueue.push_back(e); 
            QPID_LOG(trace, " MCAST held: " << e );
            return;
        }
    }
    queue.push(e);
}

void Multicaster::sendMcast(PollableEventQueue::Queue& values) {
    try {
        PollableEventQueue::Queue::iterator i = values.begin();
        while (i != values.end() && i->mcast(cpg)) {
            QPID_LOG(trace, " MCAST " << *i);
            ++i;
        }
        values.erase(values.begin(), i);
    }
    catch (const std::exception& e) {
        throw ClusterLeaveException(e.what());
    }
}

void Multicaster::release() {
    sys::Mutex::ScopedLock l(lock);
    holding = false;
    std::for_each(holdingQueue.begin(), holdingQueue.end(), boost::bind(&Multicaster::mcast, this, _1));
    holdingQueue.clear();
}

}} // namespace qpid::cluster
