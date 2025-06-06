//
// Copyright (C) 2004 Andras Varga
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public License
// along with this program; if not, see <http://www.gnu.org/licenses/>.
//

#ifndef __INET_MULTISOCKETBASICCLIENT_H
#define __INET_MULTISOCKETBASICCLIENT_H

#include "inet/common/INETDefs.h"
#include "unordered_map"
#include <map>
#include <vector>
#include <queue>
#include <string>

#include "inet/common/lifecycle/ILifecycle.h"
#include "inet/common/lifecycle/NodeStatus.h"
#include "base/MultiSocketTcpAppBase.h"
#include <workload-distr.h>

using namespace inet;

/**
 * An example request-reply based client application.
 */
class INET_API MultiSocketBasicClient : public MultiSocketTcpAppBase
{
protected:
  //    cMessage *timeoutMsg = nullptr;
  bool earlySend = false; // if true, don't wait with sendRequest() until established()
                          //    int numRequestsToSend;    // requests to send in this session
  simtime_t startTime;
  simtime_t stopTime;
  simtime_t sendTime;

  // ------------ Additions --------
  simtime_t request_interval_sec;
  long request_size_B;
  long response_size_B;
  int num_client_apps;
  int num_server_apps;
  int tcp_connections_per_thread_pair;
  int incast_size;
  int incast_request_size_bytes;
  simtime_t incast_interval_sec;
  int enable_incast;
  std::string request_target_distr;
  /**
   * to avoid having to adapt the topology (which seems complex), I will be using the exising
   * 8
   */
  bool activeClient;
  int logicalIdx; // used to be compatible with manual workload files (.csv) and for seed parity
  // std::map<int, int> *dst_ids; // mapping from logical to physical indexes of possible request destinations
  std::vector<int32_t> *dst_ids; // physical indexes of possible request destinations

  RndDistr *send_interval;
  RndDistr *dst_thread_gen;
  RndDistr *req_size;
  RndDistr *resp_size;

  struct RequestIdTuple
  {
    RequestIdTuple(int target, long req_len, long rep_len, bool incast) : target(target),
                                                                          req_len(req_len),
                                                                          rep_len(rep_len),
                                                                          incast(incast) {}
    int target;
    long req_len;
    long rep_len;
    bool incast;
  };
  typedef std::queue<RequestIdTuple> queued_requests_t;
  std::map<std::string, queued_requests_t *> dst_to_queued_requests;

  // -------------------------------

  int num_requests_per_burst;
  bool is_mice_background;
  double background_inter_arrival_time_multiplier, background_flow_size_multiplier;
  double bursty_inter_arrival_time_multiplier, bursty_flow_size_multiplier;
  int repetition_num, app_index, parent_index;
  long replyLength, requestLength;

  // info for goodput
  std::unordered_map<long, b> chunk_length_keeper;
  std::unordered_map<long, b> total_length_keeper;

  virtual void sendRequest(RequestIdTuple *rit = nullptr, bool incast = false);
  virtual void sendIncast();
  virtual void rescheduleOrDeleteTimer(simtime_t d, short int msgKind, long socket_id = -1);

  virtual int numInitStages() const override { return NUM_INIT_STAGES; }
  virtual void initialize(int stage) override;
  virtual void handleTimer(cMessage *msg) override;

  virtual void socketEstablished(TcpSocket *socket) override;
  virtual void socketDataArrived(TcpSocket *socket, Packet *msg, bool urgent) override;
  virtual void socketClosed(TcpSocket *socket) override;
  virtual void socketFailure(TcpSocket *socket, int code) override;

  virtual void handleStartOperation(LifecycleOperation *operation) override;
  virtual void handleStopOperation(LifecycleOperation *operation) override;
  virtual void handleCrashOperation(LifecycleOperation *operation) override;

  virtual void close(int socket_id) override;
  virtual void finish() override;

  virtual void checkParams();
  /*
   * initiating connection
   */
  // virtual void connect_for_bursty_request();
  virtual void connect_for_background_request();

  /*
   * getting a new port for new connections
   */
  virtual int get_local_port();

public:
  MultiSocketBasicClient() {}
  virtual ~MultiSocketBasicClient();
  static simsignal_t flowEndedSignal;
  static simsignal_t flowEndedQueryIDSignal;
  static simsignal_t flowStartedSignal;
  static simsignal_t actualFlowStartedTimeSignal;
  static simsignal_t requestSentSignal;
  static simsignal_t requestSizeSignal;
  static simsignal_t notJitteredRequestSentSignal;
  static simsignal_t replyLengthsSignal;
  static simsignal_t chunksReceivedLengthSignal;
  static simsignal_t chunksReceivedTotalLengthSignal;

  /*
   * lists of different information about the flows
   */
  std::list<int> flow_sizes, background_server_idx, bursty_server_idx;
  std::list<double> inter_arrival_times;
  std::list<unsigned long> background_flow_ids, bursty_flow_ids;
  std::list<unsigned long> bursty_query_ids;
  bool is_bursty;
};

#endif
