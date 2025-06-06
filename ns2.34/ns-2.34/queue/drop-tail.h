/* -*-	Mode:C++; c-basic-offset:8; tab-width:8; indent-tabs-mode:t -*- */
/*
 * Copyright (c) 1994 Regents of the University of California.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by the Computer Systems
 *	Engineering Group at Lawrence Berkeley Laboratory.
 * 4. Neither the name of the University nor of the Laboratory may be used
 *    to endorse or promote products derived from this software without
 *    specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE REGENTS AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE REGENTS OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * @(#) $Header: /cvsroot/nsnam/ns-2/queue/drop-tail.h,v 1.19 2004/10/28 23:35:37 haldar Exp $ (LBL)
 */

#ifndef ns_drop_tail_h
#define ns_drop_tail_h

/*#ifdef __GNUC__
#include <ext/hash_map>
#else
#include <hash_map>
#endif


namespace std
{
 using namespace __gnu_cxx;
}
*/
#include <tr1/unordered_map>
#include <tr1/functional>
#include <queue>

using std::queue;
// using std::tr1::unordered_map;
// using std::tr1::functional;

#include <string>
#include "queue.h"
#include "config.h"

typedef struct flowkey
{
	nsaddr_t src, dst;
	int fid;
} FlowKey;

/*
 * A bounded, drop-tail queue
 */
class DropTail : public Queue
{
public:
	DropTail()
	{
		q_ = new PacketQueue;
		pq_ = q_;
		bind_bool("drop_front_", &drop_front_);
		bind_bool("drop_smart_", &drop_smart_);
		bind_bool("drop_prio_", &drop_prio_);
		bind_bool("deque_prio_", &deque_prio_);
		bind_bool("keep_order_", &keep_order_);
		bind_bool("summarystats_", &summarystats);
		bind_bool("queue_in_bytes_", &qib_); // boolean: q in bytes?
		bind("mean_pktsize_", &mean_pktsize_);
		bind("sq_limit_", &sq_limit_);
		bind("bw_gbps_", &bw_gbps_); // only true for leaves of the topology
		//		_RENAMED("drop-front_", "drop_front_");
		bind("num_prios_", &num_prios_);
		do_print_ = false;
		bytes_sent_ = 0;
		prev_deq_ = 0;
		assert(bw_gbps_ > 0.0);
	}
	~DropTail()
	{
		delete q_;
	}
	void reset();
	int command(int argc, const char *const *argv);
	void enque(Packet *);
	Packet *deque();
	virtual void attach_node(Node *node);

protected:
	void shrink_queue(); // To shrink queue and drop excessive packets.
	void do_r2p2_ops(Packet *p);
	PacketQueue *q_; /* underlying FIFO queue */
	int drop_front_; /* drop-from-front (rather than from tail) */
	int summarystats;
	void print_summarystats();
	int qib_;		   /* bool: queue measured in bytes? */
	int mean_pktsize_; /* configured mean packet size in bytes */
	// Mohammad: for smart dropping
	int drop_smart_;
	// Shuang: for priority dropping
	int drop_prio_;
	int deque_prio_;
	int keep_order_;
	bool do_print_;
	uint64_t bytes_sent_;
	double prev_deq_;

	unsigned int sq_limit_;
	double bw_gbps_;
	int num_prios_;
	std::tr1::unordered_map<size_t, int> sq_counts_;
	std::queue<size_t> sq_queue_;
};

#endif
