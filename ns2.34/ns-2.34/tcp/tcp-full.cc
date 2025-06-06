/* Mortier <Richard.Mortier@cl.cam.ac.uk>
 *
 * Some warnings and comments:
 *	this version of TCP will not work correctly if the sequence number
 *	goes above 2147483648 due to sequence number wrap
 *
 *	this version of TCP by default sends data at the beginning of a
 *	connection in the "typical" way... That is,
 *		A   ------> SYN ------> B
 *		A   <----- SYN+ACK ---- B
 *		A   ------> ACK ------> B
 *		A   ------> data -----> B
 *
 *	there is no dynamic receiver's advertised window.   The advertised
 *	window is simulated by simply telling the sender a bound on the window
 *	size (wnd_).
 *
 *	in real TCP, a user process performing a read (via PRU_RCVD)
 *		calls tcp_output each time to (possibly) send a window
 *		update.  Here we don't have a user process, so we simulate
 *		a user process always ready to consume all the receive buffer
 *
 * Notes:
 *	wnd_, wnd_init_, cwnd_, ssthresh_ are in segment units
 *	sequence and ack numbers are in byte units
 *
 * Futures:
 *      there are different existing TCPs with respect to how
 *      ack's are handled on connection startup.  Some delay
 *      the ack for the first segment, which can cause connections
 *      to take longer to start up than if we be sure to ack it quickly.
 *
 *      some TCPs arrange for immediate ACK generation if the incoming segment
 *      contains the PUSH bit
 *
 *
 */

#ifndef lint
static const char rcsid[] =
	"@(#) $Header: /cvsroot/nsnam/ns-2/tcp/tcp-full.cc,v 1.128 2009/03/29 20:59:41 sallyfloyd Exp $ (LBL)";
#endif

#include "ip.h"
#include "tcp-full.h"
#include "flags.h"
#include "random.h"
#include "template.h"
#include "math.h"
#include "simple-log.h"
#include <iostream>
#include "r2p2.h"

#ifndef TRUE
#define TRUE 1
#endif

#ifndef FALSE
#define FALSE 0
#endif

/*
 * Tcl Linkage for the following:
 *	Agent/TCP/FullTcp, Agent/TCP/FullTcp/Tahoe,
 *	Agent/TCP/FullTcp/Newreno, Agent/TCP/FullTcp/Sack
 *
 * See tcl/lib/ns-default.tcl for init methods for
 *	Tahoe, Newreno, and Sack
 */

static class FullTcpClass : public TclClass
{
public:
	FullTcpClass() : TclClass("Agent/TCP/FullTcp") {}
	TclObject *create(int, const char *const *)
	{
		return (new FullTcpAgent());
	}
} class_full;

static class TahoeFullTcpClass : public TclClass
{
public:
	TahoeFullTcpClass() : TclClass("Agent/TCP/FullTcp/Tahoe") {}
	TclObject *create(int, const char *const *)
	{
		// ns-default sets reno_fastrecov_ to false
		return (new TahoeFullTcpAgent());
	}
} class_tahoe_full;

static class NewRenoFullTcpClass : public TclClass
{
public:
	NewRenoFullTcpClass() : TclClass("Agent/TCP/FullTcp/Newreno") {}
	TclObject *create(int, const char *const *)
	{
		// ns-default sets open_cwnd_on_pack_ to false
		return (new NewRenoFullTcpAgent());
	}
} class_newreno_full;

static class SackFullTcpClass : public TclClass
{
public:
	SackFullTcpClass() : TclClass("Agent/TCP/FullTcp/Sack") {}
	TclObject *create(int, const char *const *)
	{
		// ns-default sets reno_fastrecov_ to false
		// ns-default sets open_cwnd_on_pack_ to false
		return (new SackFullTcpAgent());
	}
} class_sack_full;

static class MinTcpClass : public TclClass
{
public:
	MinTcpClass() : TclClass("Agent/TCP/FullTcp/Sack/MinTCP") {}
	TclObject *create(int, const char *const *)
	{
		return (new MinTcpAgent());
	}
} class_min_full;

static class DDTcpClass : public TclClass
{
public:
	DDTcpClass() : TclClass("Agent/TCP/FullTcp/Sack/DDTCP") {}
	TclObject *create(int, const char *const *)
	{
		return (new DDTcpAgent());
	}
} class_dd_full;

/*
 * Delayed-binding variable linkage
 */

void FullTcpAgent::delay_bind_init_all()
{
	delay_bind_init_one("segsperack_");
	delay_bind_init_one("segsize_");
	delay_bind_init_one("tcprexmtthresh_");
	delay_bind_init_one("iss_");
	delay_bind_init_one("nodelay_");
	delay_bind_init_one("data_on_syn_");
	delay_bind_init_one("dupseg_fix_");
	delay_bind_init_one("dupack_reset_");
	delay_bind_init_one("close_on_empty_");
	delay_bind_init_one("signal_on_empty_");
	delay_bind_init_one("interval_");
	delay_bind_init_one("ts_option_size_");
	delay_bind_init_one("reno_fastrecov_");
	delay_bind_init_one("pipectrl_");
	delay_bind_init_one("open_cwnd_on_pack_");
	delay_bind_init_one("halfclose_");
	delay_bind_init_one("nopredict_");
	delay_bind_init_one("ecn_syn_");
	delay_bind_init_one("ecn_syn_wait_");
	delay_bind_init_one("debug_");
	delay_bind_init_one("spa_thresh_");

	delay_bind_init_one("flow_remaining_"); // Mohammad
	delay_bind_init_one("dynamic_dupack_");

	delay_bind_init_one("prio_scheme_");	  // Shuang
	delay_bind_init_one("prio_num_");		  // Shuang
	delay_bind_init_one("prio_cap0");		  // Shuang
	delay_bind_init_one("prio_cap1");		  // Shuang
	delay_bind_init_one("prio_cap2");		  // Shuang
	delay_bind_init_one("prio_cap3");		  // Shuang
	delay_bind_init_one("prio_cap4");		  // Shuang
	delay_bind_init_one("prio_cap5");		  // Shuang
	delay_bind_init_one("prio_cap6");		  // Shuang
	delay_bind_init_one("deadline");		  // Shuang
	delay_bind_init_one("early_terminated_"); // Shuang

	TcpAgent::delay_bind_init_all();

	reset();
}

int FullTcpAgent::delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer)
{
	if (delay_bind(varName, localName, "segsperack_", &segs_per_ack_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "segsize_", &maxseg_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "tcprexmtthresh_", &tcprexmtthresh_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "iss_", &iss_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "spa_thresh_", &spa_thresh_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "nodelay_", &nodelay_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "data_on_syn_", &data_on_syn_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "dupseg_fix_", &dupseg_fix_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "dupack_reset_", &dupack_reset_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "close_on_empty_", &close_on_empty_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "signal_on_empty_", &signal_on_empty_, tracer))
		return TCL_OK;
	if (delay_bind_time(varName, localName, "interval_", &delack_interval_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "ts_option_size_", &ts_option_size_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "reno_fastrecov_", &reno_fastrecov_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "pipectrl_", &pipectrl_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "open_cwnd_on_pack_", &open_cwnd_on_pack_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "halfclose_", &halfclose_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "nopredict_", &nopredict_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "ecn_syn_", &ecn_syn_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "ecn_syn_wait_", &ecn_syn_wait_, tracer))
		return TCL_OK;
	if (delay_bind_bool(varName, localName, "debug_", &debug_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "flow_remaining_", &flow_remaining_, tracer))
		return TCL_OK; // Mohammad
	if (delay_bind(varName, localName, "dynamic_dupack_", &dynamic_dupack_, tracer))
		return TCL_OK; // Mohammad
	if (delay_bind(varName, localName, "prio_scheme_", &prio_scheme_, tracer))
		return TCL_OK; // Shuang
	if (delay_bind(varName, localName, "prio_num_", &prio_num_, tracer))
		return TCL_OK; // Shuang
	if (delay_bind(varName, localName, "prio_cap0", &prio_cap_[0], tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap1", &prio_cap_[1], tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap2", &prio_cap_[2], tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap3", &prio_cap_[3], tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap4", &prio_cap_[4], tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap5", &prio_cap_[5], tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "prio_cap6", &prio_cap_[6], tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "prob_cap_", &prob_cap_, tracer))
		return TCL_OK; // Shuang
	if (delay_bind(varName, localName, "deadline", &deadline, tracer))
		return TCL_OK; // Shuang
	if (delay_bind(varName, localName, "early_terminated_", &early_terminated_, tracer))
		return TCL_OK; // Shuang
	return TcpAgent::delay_bind_dispatch(varName, localName, tracer);
}

void SackFullTcpAgent::delay_bind_init_all()
{
	delay_bind_init_one("clear_on_timeout_");
	delay_bind_init_one("sack_rtx_cthresh_");
	delay_bind_init_one("sack_rtx_bthresh_");
	delay_bind_init_one("sack_block_size_");
	delay_bind_init_one("sack_option_size_");
	delay_bind_init_one("max_sack_blocks_");
	delay_bind_init_one("sack_rtx_threshmode_");
	FullTcpAgent::delay_bind_init_all();
}

int SackFullTcpAgent::delay_bind_dispatch(const char *varName, const char *localName, TclObject *tracer)
{
	if (delay_bind_bool(varName, localName, "clear_on_timeout_", &clear_on_timeout_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "sack_rtx_cthresh_", &sack_rtx_cthresh_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "sack_rtx_bthresh_", &sack_rtx_bthresh_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "sack_rtx_threshmode_", &sack_rtx_threshmode_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "sack_block_size_", &sack_block_size_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "sack_option_size_", &sack_option_size_, tracer))
		return TCL_OK;
	if (delay_bind(varName, localName, "max_sack_blocks_", &max_sack_blocks_, tracer))
		return TCL_OK;
	return FullTcpAgent::delay_bind_dispatch(varName, localName, tracer);
}

int FullTcpAgent::command(int argc, const char *const *argv)
{
	// would like to have some "connect" primitive
	// here, but the problem is that we get called before
	// the simulation is running and we want to send a SYN.
	// Because no routing exists yet, this fails.
	// Instead, see code in advance().
	//
	// listen can happen any time because it just changes state_
	//
	// close is designed to happen at some point after the
	// simulation is running (using an ns 'at' command)

	if (argc == 2)
	{
		if (strcmp(argv[1], "listen") == 0)
		{
			// just a state transition
			listen();
			return (TCL_OK);
		}
		if (strcmp(argv[1], "close") == 0)
		{
			usrclosed();
			return (TCL_OK);
		}
	}
	if (argc == 3)
	{
		if (strcmp(argv[1], "advance") == 0)
		{
			advanceby(atoi(argv[2]));
			return (TCL_OK);
		}
		if (strcmp(argv[1], "advanceby") == 0)
		{
			advanceby(atoi(argv[2]));
			return (TCL_OK);
		}
		if (strcmp(argv[1], "advance-bytes") == 0)
		{
			advance_bytes(atoi(argv[2]));
			return (TCL_OK);
		}
		// Mohammad
		if (strcmp(argv[1], "get-flow") == 0)
		{
			flow_remaining_ = atoi(argv[2]);
			return (TCL_OK);
		}
	}
	if (argc == 4)
	{
		if (strcmp(argv[1], "sendmsg") == 0)
		{
			sendmsg(atoi(argv[2]), argv[3]);
			return (TCL_OK);
		}
	}
	return (TcpAgent::command(argc, argv));
}

/*
 * "User Interface" Functions for Full TCP
 *	advanceby(number of packets)
 *	advance_bytes(number of bytes)
 *	sendmsg(int bytes, char* buf)
 *	listen
 *	close
 */

/*
 * the 'advance' interface to the regular tcp is in packet
 * units.  Here we scale this to bytes for full tcp.
 *
 * 'advance' is normally called by an "application" (i.e. data source)
 * to signal that there is something to send
 *
 * 'curseq_' is the sequence number of the last byte provided
 * by the application.  In the case where no data has been supplied
 * by the application, curseq_ is the iss_.
 */
void FullTcpAgent::advanceby(int np)
{

	// XXX hack:
	//	because np is in packets and a data source
	//	may pass a *huge* number as a way to tell us
	//	and if it's there, pre-divide it
	if (np >= 0x10000000)
		np /= maxseg_;

	advance_bytes(np * maxseg_);
	return;
}

/*
 * the byte-oriented interface: advance_bytes(int nbytes)
 */

void FullTcpAgent::advance_bytes(int nb)
{

	////Shuang: hardcode
	cwnd_ = initial_window();
	slog::log4(debug_, addr(), "FullTcpAgent::advance_bytes(int nb). bytes=", nb, "cwnd_=", cwnd_, "addr():", addr(), "daddr():", daddr());
	//	//ssthresh_ = cwnd_;
	//
	// state-specific operations:
	//	if CLOSED or LISTEN, reset and try a new active open/connect
	//	if ESTABLISHED, queue and try to send more
	//	if SYN_SENT or SYN_RCVD, just queue
	//	if above ESTABLISHED, we are closing, so don't allow
	//
	start_time = now();
	early_terminated_ = 0;
	switch (state_)
	{

	case TCPS_CLOSED:
	case TCPS_LISTEN:

		reset();
		startseq_ = iss_;
		curseq_ = iss_ + nb;
		seq_bound_ = -1;
		connect(); // initiate new connection
		break;
	case TCPS_ESTABLISHED:
	case TCPS_SYN_SENT:
	case TCPS_SYN_RECEIVED:
		if (curseq_ < iss_)
			curseq_ = iss_;
		startseq_ = curseq_;
		seq_bound_ = -1;
		curseq_ += nb;
		break;

	default:
		if (debug_)
			fprintf(stderr, "%f: FullTcpAgent::advance(%s): cannot advance while in state %s\n",
					now(), name(), statestr(state_));
	}
	if (state_ == TCPS_ESTABLISHED)
		send_much(0, REASON_NORMAL, maxburst_);

	return;
}

/**
 * Quick and dirty way to get an r2p2-header in that carries usefull info (should use a diff hdr)
 */
void FullTcpAgent::advance_bytes(int nb, RequestIdTuple &&req_id)
{
	////Shuang: hardcode
	cwnd_ = initial_window();
	slog::log4(debug_, addr(), "FullTcpAgent::advance_bytes(int nb, RequestIdTuple&& req_id). bytes=", nb, "cwnd_=",
			   cwnd_, "addr():", addr(), "daddr():", daddr(), "req_id:", req_id.app_level_id_, "ts:", req_id.ts_);
	//	//ssthresh_ = cwnd_;
	// std::cout << "advance bytes: " << nb << " " << addr() << " " << port() << " " << daddr() << " " << dport() << std::endl;
	// std::cout << "WINODW sZ: " << cwnd_ << std::endl;
	// hack to pass the request id w/o changing functions
	// will only work properly if the tcp agent (i.e., the connection) is used for one request/response each time
	// That is, this function is not called before all the packets for the
	if (cur_req_id_tup_ == nullptr)
	{
		cur_req_id_tup_ = new RequestIdTuple();
	}
	*cur_req_id_tup_ = req_id;
	assert(cur_req_id_tup_->ts_ > 0);
	//
	// state-specific operations:
	//	if CLOSED or LISTEN, reset and try a new active open/connect
	//	if ESTABLISHED, queue and try to send more
	//	if SYN_SENT or SYN_RCVD, just queue
	//	if above ESTABLISHED, we are closing, so don't allow
	//
	start_time = now();
	early_terminated_ = 0;
	switch (state_)
	{

	case TCPS_CLOSED:
	case TCPS_LISTEN:

		reset();
		startseq_ = iss_;
		curseq_ = iss_ + nb;
		seq_bound_ = -1;
		connect(); // initiate new connection
		break;
	case TCPS_ESTABLISHED:
	case TCPS_SYN_SENT:
	case TCPS_SYN_RECEIVED:
		if (curseq_ < iss_)
			curseq_ = iss_;
		startseq_ = curseq_;
		seq_bound_ = -1;
		curseq_ += nb;
		break;

	default:
		if (debug_)
			fprintf(stderr, "%f: FullTcpAgent::advance(%s): cannot advance while in state %s\n",
					now(), name(), statestr(state_));
	}
	if (state_ == TCPS_ESTABLISHED)
		send_much(0, REASON_NORMAL, maxburst_);

	return;
}

/*
 * If MSG_EOF is set, by setting close_on_empty_ to TRUE, we ensure that
 * a FIN will be sent when the send buffer emptys.
 * If DAT_EOF is set, the callback function done_data is called
 * when the send buffer empty
 *
 * When (in the future?) FullTcpAgent implements T/TCP, avoidance of 3-way
 * handshake can be handled in this function.
 */
void FullTcpAgent::sendmsg(int nbytes, const char *flags)
{
	if (flags && strcmp(flags, "MSG_EOF") == 0)
	{
		close_on_empty_ = TRUE;
		printf("setting 2 closeonempty to true for fid= %d\n", fid_);
	}

	if (flags && strcmp(flags, "DAT_EOF") == 0)
	{
		signal_on_empty_ = TRUE;
		printf("setting signalonempty to true for fid= %d\n", fid_);
	}
	if (nbytes == -1)
	{
		infinite_send_ = TRUE;
		advance_bytes(0);
	}
	else
		advance_bytes(nbytes);
}

/*
 * do an active open
 * (in real TCP, see tcp_usrreq, case PRU_CONNECT)
 */
void FullTcpAgent::connect()
{
	newstate(TCPS_SYN_SENT); // sending a SYN now
	sent(iss_, foutput(iss_, REASON_NORMAL));
	return;
}

/*
 * be a passive opener
 * (in real TCP, see tcp_usrreq, case PRU_LISTEN)
 * (for simulation, make this peer's ptype ACKs)
 */
void FullTcpAgent::listen()
{
	newstate(TCPS_LISTEN);
	type_ = PT_ACK; // instead of PT_TCP
}

/*
 * This function is invoked when the sender buffer is empty. It in turn
 * invokes the Tcl done_data procedure that was registered with TCP.
 */

void FullTcpAgent::bufferempty()
{
	signal_on_empty_ = FALSE;
	Tcl::instance().evalf("%s done_data", this->name());
}

/*
 * called when user/application performs 'close'
 */

void FullTcpAgent::usrclosed()
{
	curseq_ = maxseq_ - 1;	// now, no more data
	infinite_send_ = FALSE; // stop infinite send
	switch (state_)
	{
	case TCPS_CLOSED:
	case TCPS_LISTEN:
		cancel_timers();
		newstate(TCPS_CLOSED);
		finish();
		break;
	case TCPS_SYN_SENT:
		newstate(TCPS_CLOSED);
		/* fall through */
	case TCPS_LAST_ACK:
		flags_ |= TF_NEEDFIN;
		send_much(1, REASON_NORMAL, maxburst_);
		break;
	case TCPS_SYN_RECEIVED:
	case TCPS_ESTABLISHED:
		newstate(TCPS_FIN_WAIT_1);
		flags_ |= TF_NEEDFIN;
		send_much(1, REASON_NORMAL, maxburst_);
		break;
	case TCPS_CLOSE_WAIT:
		newstate(TCPS_LAST_ACK);
		flags_ |= TF_NEEDFIN;
		send_much(1, REASON_NORMAL, maxburst_);
		break;
	case TCPS_FIN_WAIT_1:
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSING:
		/* usr asked for a close more than once [?] */
		if (debug_)
			fprintf(stderr,
					"%f FullTcpAgent(%s): app close in bad state %s\n",
					now(), name(), statestr(state_));
		break;
	default:
		if (debug_)
			fprintf(stderr,
					"%f FullTcpAgent(%s): app close in unknown state %s\n",
					now(), name(), statestr(state_));
	}

	return;
}

/*
 * Utility type functions
 */

void FullTcpAgent::cancel_timers()
{

	// cancel: rtx, burstsend, delsnd
	TcpAgent::cancel_timers();
	// cancel: delack
	delack_timer_.force_cancel();
}

void FullTcpAgent::newstate(int state)
{
	// printf("%f(%s): state changed from %s to %s\n",
	// now(), name(), statestr(state_), statestr(state));

	state_ = state;
}

void FullTcpAgent::prpkt(Packet *pkt)
{
	hdr_tcp *tcph = hdr_tcp::access(pkt); // TCP header
	hdr_cmn *th = hdr_cmn::access(pkt);	  // common header (size, etc)
	// hdr_flags *fh = hdr_flags::access(pkt);	// flags (CWR, CE, bits)
	hdr_ip *iph = hdr_ip::access(pkt);
	int datalen = th->size() - tcph->hlen(); // # payload bytes

	fprintf(stdout, " [%d:%d.%d>%d.%d] (hlen:%d, dlen:%d, seq:%d, ack:%d, flags:0x%x (%s), salen:%d, reason:0x%x)\n",
			th->uid(),
			iph->saddr(), iph->sport(),
			iph->daddr(), iph->dport(),
			tcph->hlen(),
			datalen,
			tcph->seqno(),
			tcph->ackno(),
			tcph->flags(), flagstr(tcph->flags()),
			tcph->sa_length(),
			tcph->reason());
}

char *
FullTcpAgent::flagstr(int hflags)
{
	// update this if tcp header flags change
	static char *flagstrs[28] = {
		"<null>", "<FIN>", "<SYN>", "<SYN,FIN>",			// 0-3
		"<?>", "<?,FIN>", "<?,SYN>", "<?,SYN,FIN>",			// 4-7
		"<PSH>", "<PSH,FIN>", "<PSH,SYN>", "<PSH,SYN,FIN>", // 0x08-0x0b
		/* do not use <??, in next line because that's an ANSI trigraph */
		"<?>", "<?,FIN>", "<?,SYN>", "<?,SYN,FIN>",							// 0x0c-0x0f
		"<ACK>", "<ACK,FIN>", "<ACK,SYN>", "<ACK,SYN,FIN>",					// 0x10-0x13
		"<ACK>", "<ACK,FIN>", "<ACK,SYN>", "<ACK,SYN,FIN>",					// 0x14-0x17
		"<PSH,ACK>", "<PSH,ACK,FIN>", "<PSH,ACK,SYN>", "<PSH,ACK,SYN,FIN>", // 0x18-0x1b
	};
	if (hflags < 0 || (hflags > 28))
	{
		/* Added strings for CWR and ECE  -M. Weigle 6/27/02 */
		if (hflags == 72)
			return ("<ECE,PSH>");
		else if (hflags == 80)
			return ("<ECE,ACK>");
		else if (hflags == 88)
			return ("<ECE,PSH,ACK>");
		else if (hflags == 152)
			return ("<CWR,PSH,ACK>");
		else if (hflags == 153)
			return ("<CWR,PSH,ACK,FIN>");
		else
			return ("<invalid>");
	}
	return (flagstrs[hflags]);
}

char *
FullTcpAgent::statestr(int state)
{
	static char *statestrs[TCP_NSTATES] = {
		"CLOSED", "LISTEN", "SYN_SENT", "SYN_RCVD",
		"ESTABLISHED", "CLOSE_WAIT", "FIN_WAIT_1", "CLOSING",
		"LAST_ACK", "FIN_WAIT_2"};
	if (state < 0 || (state >= TCP_NSTATES))
		return ("INVALID");
	return (statestrs[state]);
}

void DelAckTimer::expire(Event *)
{
	a_->timeout(TCP_TIMER_DELACK);
}

/*
 * reset to starting point, don't set state_ here,
 * because our starting point might be LISTEN rather
 * than CLOSED if we're a passive opener
 */
void FullTcpAgent::reset()
{
	cancel_timers();   // cancel timers first
	TcpAgent::reset(); // resets most variables
	rq_.clear();	   // clear reassembly queue
	rtt_init();		   // zero rtt, srtt, backoff
	last_ack_sent_ = -1;
	flow_remaining_ = -1; // Mohammad
	rcv_nxt_ = -1;
	pipe_ = 0;
	rtxbytes_ = 0;
	flags_ = 0;
	t_seqno_ = iss_;
	maxseq_ = -1;
	irs_ = -1;
	last_send_time_ = -1.0;
	if (ts_option_)
		recent_ = recent_age_ = 0.0;
	else
		recent_ = recent_age_ = -1.0;

	fastrecov_ = FALSE;

	closed_ = 0;
	close_on_empty_ = FALSE;

	if (ecn_syn_)
		ecn_syn_next_ = 1;
	else
		ecn_syn_next_ = 0;
	// Shuang
	prob_mode_ = false;
	prob_count_ = 0;
	last_sqtotal_ = 0;
	deadline = 0;
	early_terminated_ = 0;
}

/*
 * This function is invoked when the connection is done. It in turn
 * invokes the Tcl finish procedure that was registered with TCP.
 * This function mimics tcp_close()
 */

void FullTcpAgent::finish()
{
	Tcl::instance().evalf("%s done", this->name());
}
/*
 * headersize:
 *	how big is an IP+TCP header in bytes; include options such as ts
 * this function should be virtual so others (e.g. SACK) can override
 */
int FullTcpAgent::headersize()
{
	int total = tcpip_base_hdr_size_;
	if (total < 1)
	{
		fprintf(stderr,
				"%f: FullTcpAgent(%s): warning: tcpip hdr size is only %d bytes\n",
				now(), name(), tcpip_base_hdr_size_);
	}

	if (ts_option_)
		total += ts_option_size_;

	return (total);
}

/*
 * flags that are completely dependent on the tcp state
 * these are used for the next outgoing packet in foutput()
 * (in real TCP, see tcp_fsm.h, the "tcp_outflags" array)
 */
int FullTcpAgent::outflags()
{
	// in real TCP an RST is added in the CLOSED state
	static int tcp_outflags[TCP_NSTATES] = {
		TH_ACK,			 /* 0, CLOSED */
		0,				 /* 1, LISTEN */
		TH_SYN,			 /* 2, SYN_SENT */
		TH_SYN | TH_ACK, /* 3, SYN_RECEIVED */
		TH_ACK,			 /* 4, ESTABLISHED */
		TH_ACK,			 /* 5, CLOSE_WAIT */
		TH_FIN | TH_ACK, /* 6, FIN_WAIT_1 */
		TH_FIN | TH_ACK, /* 7, CLOSING */
		TH_FIN | TH_ACK, /* 8, LAST_ACK */
		TH_ACK,			 /* 9, FIN_WAIT_2 */
						 /* 10, TIME_WAIT --- not used in simulator */
	};

	if (state_ < 0 || (state_ >= TCP_NSTATES))
	{
		fprintf(stderr, "%f FullTcpAgent(%s): invalid state %d\n",
				now(), name(), state_);
		return (0x0);
	}

	return (tcp_outflags[state_]);
}

/*
 * reaass() -- extract the appropriate fields from the packet
 *	and pass this info the ReassemblyQueue add routine
 *
 * returns the TCP header flags representing the "or" of
 *	the flags contained in the adjacent sequence # blocks
 */

int FullTcpAgent::reass(Packet *pkt)
{
	hdr_tcp *tcph = hdr_tcp::access(pkt);
	hdr_cmn *th = hdr_cmn::access(pkt);

	int start = tcph->seqno();
	int end = start + th->size() - tcph->hlen();
	int tiflags = tcph->flags();
	int fillshole = (start == rcv_nxt_);
	int flags;

	// end contains the seq of the last byte of
	// in the packet plus one

	if (start == end && (tiflags & TH_FIN) == 0)
	{
		fprintf(stderr, "%f: FullTcpAgent(%s)::reass() -- bad condition - adding non-FIN zero-len seg\n",
				now(), name());
		abort();
	}

	flags = rq_.add(start, end, tiflags, 0);

	// present:
	//
	//  If we've never received a SYN (unlikely)
	//  or this is an out of order addition, no reason to coalesce
	//

	if (TCPS_HAVERCVDSYN(state_) == 0 || !fillshole)
	{
		return (0x00);
	}
	//
	// If we get some data in SYN_RECVD, no need to present to user yet
	//
	if (state_ == TCPS_SYN_RECEIVED && (end > start))
		return (0x00);

	// clear out data that has been passed, up to rcv_nxt_,
	// collects flags

	flags |= rq_.cleartonxt();

	return (flags);
}

/*
 * utility function to set rcv_next_ during inital exchange of seq #s
 */

int FullTcpAgent::rcvseqinit(int seq, int dlen)
{
	return (seq + dlen + 1);
	// printf("newww3 fid= %d, rcv_nxt_= %d diff= %d, highest_ack= %d, last_ack_sent= %d diff= %d\n",fid_,(int)rcv_nxt_,((int)rcv_nxt_)-oldrcvnxt,(int)highest_ack_,last_ack_sent_,((int)last_ack_sent_)-oldlastacksent);
}

/*
 * build a header with the timestamp option if asked
 */
int FullTcpAgent::build_options(hdr_tcp *tcph)
{
	int total = 0;
	if (ts_option_)
	{
		tcph->ts() = now();
		tcph->ts_echo() = recent_;
		total += ts_option_size_;
	}
	else
	{
		tcph->ts() = tcph->ts_echo() = -1.0;
	}
	return (total);
}

/*
 * pack() -- is the ACK a partial ACK? (not past recover_)
 */

int FullTcpAgent::pack(Packet *pkt)
{
	hdr_tcp *tcph = hdr_tcp::access(pkt);
	/* Added check for fast recovery.  -M. Weigle 5/2/02 */
	return (fastrecov_ && tcph->ackno() >= highest_ack_ &&
			tcph->ackno() < recover_);
}

/*
 * baseline reno TCP exists fast recovery on a partial ACK
 */

void FullTcpAgent::pack_action(Packet *)
{
	if (reno_fastrecov_ && fastrecov_ && cwnd_ > double(ssthresh_))
	{
		cwnd_ = double(ssthresh_); // retract window if inflated
	}
	fastrecov_ = FALSE;
	// printf("%f: EXITED FAST RECOVERY\n", now());
	dupacks_ = 0;
}

/*
 * ack_action -- same as partial ACK action for base Reno TCP
 */

void FullTcpAgent::ack_action(Packet *p)
{
	FullTcpAgent::pack_action(p);
}

int FullTcpAgent::set_prio(int seq, int maxseq)
{
	int max = 100 * 1460;
	int prio;
	if (prio_scheme_ == 0)
	{
		if (seq - startseq_ > max)
			prio = max;
		else
			prio = seq - startseq_;
	}
	if (prio_scheme_ == 1)
		prio = maxseq - startseq_;
	if (prio_scheme_ == 2)
		prio = maxseq - seq;
	if (prio_scheme_ == 3)
		prio = seq - startseq_;

	if (prio_num_ == 0)
		return prio;
	else
		return calPrio(prio);
}

int FullTcpAgent::calPrio(int prio)
{
	if (prio_num_ != 2 && prio_num_ != 4 && prio_num_ != 8)
	{
		fprintf(stderr, "wrong number or priority class %d\n", prio_num_);
		return 0;
	}
	for (int i = 1; i < prio_num_; i++)
		if (prio <= prio_cap_[i * 8 / prio_num_ - 1])
		{
			// printf("prio %d cap %d ans %d\n", prio, prio_cap_[i*8/prio_num_ - 1], i - 1);
			return i - 1;
		}

	// printf("prio %d cap %d ans %d\n", prio, prio_cap_[8/prio_num_ - 1], prio_num_ - 1);
	return prio_num_ - 1;
}

/*
 * sendpacket:
 *	allocate a packet, fill in header fields, and send
 *	also keeps stats on # of data pkts, acks, re-xmits, etc
 *
 * fill in packet fields.  Agent::allocpkt() fills
 * in most of the network layer fields for us.
 * So fill in tcp hdr and adjust the packet size.
 *
 * Also, set the size of the tcp header.
 */
void FullTcpAgent::sendpacket(int seqno, int ackno, int pflags, int datalen, int reason, Packet *p)
{
	if (!p)
		p = allocpkt();
	hdr_tcp *tcph = hdr_tcp::access(p);
	hdr_flags *fh = hdr_flags::access(p);
	hdr_ip *iph = hdr_ip::access(p);
	slog::log5(debug_, addr(), "FullTcpAgent::sendpacket(). cwnd_=", cwnd_, "datalen:", datalen,
			   "addr():", addr(), "daddr():", daddr());

	// hack for pfabric app (using the r2p2 hdr bcs it is convenient)
	if (conctd_to_pfabric_app_ && cur_req_id_tup_ != nullptr)
	{
		hdr_r2p2 *r2p2_hdr = hdr_r2p2::access(p);
		r2p2_hdr->app_level_id() = cur_req_id_tup_->app_level_id_;
		// put the total number of bytes in the packet id field so that
		// the receiver knows when it has received a full message.
		r2p2_hdr->msg_bytes() = cur_req_id_tup_->msg_bytes_;
		if (cur_req_id_tup_->is_request_)
		{
			r2p2_hdr->msg_type() = hdr_r2p2::REQUEST;
		}
		else
		{
			r2p2_hdr->msg_type() = hdr_r2p2::REPLY;
		}
		r2p2_hdr->cl_thread_id() = cur_req_id_tup_->cl_thread_id_;
		r2p2_hdr->cl_addr() = cur_req_id_tup_->cl_addr_;
		r2p2_hdr->sr_addr() = cur_req_id_tup_->sr_addr_;
		r2p2_hdr->msg_creation_time() = cur_req_id_tup_->ts_;
		r2p2_hdr->is_pfabric_app_msg() = true;
		assert(cur_req_id_tup_->ts_ > 0);
	}
	/* build basic header w/options */

	tcph->seqno() = seqno;
	tcph->ackno() = ackno;
	tcph->flags() = pflags;
	tcph->reason() |= reason; // make tcph->reason look like ns1 pkt->flags?
	tcph->sa_length() = 0;	  // may be increased by build_options()
	tcph->hlen() = tcpip_base_hdr_size_;
	tcph->hlen() += build_options(tcph);
	// Shuang: reduce header length
	// tcph->hlen() = 1;

	// iph->prio() = curseq_ - seqno + 10;
	/*
	 * Explicit Congestion Notification (ECN) related:
	 * Bits in header:
	 * 	ECT (EC Capable Transport),
	 * 	ECNECHO (ECHO of ECN Notification generated at router),
	 * 	CWR (Congestion Window Reduced from RFC 2481)
	 * States in TCP:
	 *	ecn_: I am supposed to do ECN if my peer does
	 *	ect_: I am doing ECN (ecn_ should be T and peer does ECN)
	 */

	if (datalen > 0 && ecn_)
	{
		// set ect on data packets
		fh->ect() = ect_; // on after mutual agreement on ECT
	}
	else if (ecn_ && ecn_syn_ && ecn_syn_next_ && (pflags & TH_SYN) && (pflags & TH_ACK))
	{
		// set ect on syn/ack packet, if syn packet was negotiating ECT
		fh->ect() = ect_;
	}
	else
	{
		/* Set ect() to 0.  -M. Weigle 1/19/05 */
		fh->ect() = 0;
	}

	// Mohammad: for DCTCP, ect should be set on all packets
	if (ecnhat_)
		fh->ect() = ect_;

	if (ecn_ && ect_ && recent_ce_)
	{
		// This is needed here for the ACK in a SYN, SYN/ACK, ACK
		// sequence.
		pflags |= TH_ECE;
	}
	// fill in CWR and ECE bits which don't actually sit in
	// the tcp_flags but in hdr_flags
	if (pflags & TH_ECE)
	{
		fh->ecnecho() = 1;
	}
	else
	{
		fh->ecnecho() = 0;
	}

	if (pflags & TH_CWR)
	{
		fh->cong_action() = 1;
	}
	else
	{
		/* Set cong_action() to 0  -M. Weigle 1/19/05 */
		fh->cong_action() = 0;
	}

	/* actual size is data length plus header length */

	hdr_cmn *ch = hdr_cmn::access(p);
	ch->size() = datalen + tcph->hlen(); // original

	if (datalen <= 0)
	{
		++nackpack_;
		// Shuang: artifically reduce ack size
		// ch->size() = 1;
	}
	else
	{
		++ndatapack_;
		ndatabytes_ += datalen;
		last_send_time_ = now(); // time of last data
	}
	if (reason == REASON_TIMEOUT || reason == REASON_DUPACK || reason == REASON_SACK)
	{
		++nrexmitpack_;
		nrexmitbytes_ += datalen;
	}
	last_ack_sent_ = ackno;

	// if (state_ != TCPS_ESTABLISHED) {
	// printf("%f(%s)[state:%s]: sending pkt ", now(), name(), statestr(state_));
	// prpkt(p);
	// }

	if (deadline > 0)
		iph->prio_type() = 1;
	if (datalen > 0)
	{
		// iph->prio_type() = 0;
		// iph->prio() = set_prio(seqno, curseq_);
		/* Shuang: prio dropping */
		if (deadline == 0)
		{
			iph->prio() = set_prio(seqno, curseq_);
			iph->prio_type() = 0;
		}
		else
		{
			int tleft = deadline - int((now() - start_time) * 1e6);
			iph->prio_type() = 1;
			iph->prio() = deadline + int(start_time * 1e6);
			if (tleft < 0 || byterm() * 8 / 1e4 > tleft)
			{
				iph->prio_type() = 0;
				iph->prio() = (1 << 30);
			}
			else
			{
				//				iph->prio() = iph->prio() / 40 * 1000 + set_prio(seqno, curseq_) / 1460;
			}
		}

		/* Mohammad: this is deprecated
		 * it was for path-aware multipath
		 * congestion control experiments */
		// Shuang: delete it
		// iph->prio() = fid_;

		/* Mohammad: inform pacer (TBF) that
		 * this connection received an EcnEcho.
		 * this is a bit hacky, but necessary
		 * for now since the TBF class doesn't see the
		 * ACKS. */

		if (informpacer)
			iph->gotecnecho = 1;
		else
			iph->gotecnecho = 0;

		informpacer = 0;
		// abd
	}
	hdr_r2p2::access(p)->is_pfabric_app_msg() = true;
	slog::log5(debug_, addr(), "FullTcpAgent::sending packet of size:", hdr_cmn::access(p)->size());
	// fix header sizes
	hdr_cmn::access(p)->size() += INTER_PKT_GAP_SIZE + ETHERNET_PREAMBLE_SIZE + ETHERNET_HEADER_SIZE;
	slog::log5(debug_, addr(), "FullTcpAgent::sending packet of adjusted size:", hdr_cmn::access(p)->size());
	assert(Scheduler::instance().clock() <= 10.0 || hdr_cmn::access(p)->size() <= MAX_ETHERNET_FRAME_ON_WIRE);
	send(p, 0);

	return;
}

//
// reset_rtx_timer: called during a retransmission timeout
// to perform exponential backoff.  Also, note that because
// we have performed a retransmission, our rtt timer is now
// invalidated (indicate this by setting rtt_active_ false)
//
void FullTcpAgent::reset_rtx_timer(int /* mild */)
{
	// cancel old timer, set a new one
	/* if there is no outstanding data, don't back off rtx timer *
	 * (Fix from T. Kelly.) */
	if (!(highest_ack_ == maxseq_ && restart_bugfix_))
	{
		rtt_backoff(); // double current timeout
	}
	set_rtx_timer();	 // set new timer
	rtt_active_ = FALSE; // no timing during this window
}

/*
 * see if we should send a segment, and if so, send it
 * 	(may be ACK or data)
 * return the number of data bytes sent (count a SYN or FIN as 1 each)
 *
 * simulator var, desc (name in real TCP)
 * --------------------------------------
 * maxseq_, largest seq# we've sent plus one (snd_max)
 * flags_, flags regarding our internal state (t_state)
 * pflags, a local used to build up the tcp header flags (flags)
 * curseq_, is the highest sequence number given to us by "application"
 * highest_ack_, the highest ACK we've seen for our data (snd_una-1)
 * seqno, the next seq# we're going to send (snd_nxt)
 */
int FullTcpAgent::foutput(int seqno, int reason)
{
	// if maxseg_ not set, set it appropriately
	// Q: how can this happen?

	if (maxseg_ == 0)
		maxseg_ = size_; // Mohammad: changed from size_  - headersize();
	// Mohamad: commented the else condition
	// which is unnecessary and conflates with
	// tcp.cc
	// else
	//	size_ =  maxseg_ + headersize();

	int is_retransmit = (seqno < maxseq_);
	int quiet = (highest_ack_ == maxseq_);
	int pflags = outflags();
	int syn = (seqno == iss_);
	int emptying_buffer = FALSE;
	int buffered_bytes = (infinite_send_) ? TCP_MAXSEQ : curseq_ - highest_ack_ + 1;
	// printf("buffered bytes= %d now= %lf fid= %d cwnd= %d\n", buffered_bytes,now(),fid_,(int)cwnd_);
	int win = window() * maxseg_; // window (in bytes)
	if (prob_mode_ && win > 1)
		win = 1;

	int off = seqno - highest_ack_; // offset of seg in window
	int datalen;
	// int amtsent = 0;

	// be careful if we have not received any ACK yet
	if (highest_ack_ < 0)
	{
		if (!infinite_send_)
			buffered_bytes = curseq_ - iss_;
		;
		off = seqno - iss_;
	}

	if (syn && !data_on_syn_)
		datalen = 0;
	else if (pipectrl_)
		datalen = buffered_bytes - off;
	else
		datalen = min(buffered_bytes, win) - off;

	//	if (fid_ == 13 || fid_ == 14) {
	//		int tmp = 0;
	//		if (prob_mode_)
	//			tmp = 1;
	//		int tmph = highest_ack_;
	//		printf("%.5lf: FLOW%d: win %d probe: %d buffered bytes %d off %d seqno %d, highestack %d, datalen %d\n", now(), fid_, win, tmp, buffered_bytes, off, seqno, tmph, datalen);
	//		fflush(stdout);
	//	}

	//	if (deadline != 0 && !syn) {
	//		double tleft = deadline/1e6 - (now() - start_time);
	//		if (tleft < 0) {
	//			printf("early termination now %.8lf start %.8lf deadline %d\n", now(), start_time, deadline);
	//			fflush(stdout);
	//			buffered_bytes = 0;
	//			datalen = 0;
	//		}
	//    }
	if ((signal_on_empty_) && (!buffered_bytes) && (!syn))
	{
		bufferempty();
	}
	//
	// in real TCP datalen (len) could be < 0 if there was window
	// shrinkage, or if a FIN has been sent and neither ACKd nor
	// retransmitted.  Only this 2nd case concerns us here...
	//
	if (datalen < 0)
	{
		datalen = 0;
	}
	else if (datalen > maxseg_)
	{
		datalen = maxseg_;
	}

	//
	// this is an option that causes us to slow-start if we've
	// been idle for a "long" time, where long means a rto or longer
	// the slow-start is a sort that does not set ssthresh
	//

	if (slow_start_restart_ && quiet && datalen > 0)
	{
		if (idle_restart())
		{
			slowdown(CLOSE_CWND_INIT);
		}
	}

	// printf("%f %d %d\n", Scheduler::instance().clock(), (int) highest_ack_, (int) maxseq_);

	//
	// see if sending this packet will empty the send buffer
	// a dataless SYN packet counts also
	//

	if (!infinite_send_ && ((seqno + datalen) > curseq_ ||
							(syn && datalen == 0)))
	{
		emptying_buffer = TRUE;
		//
		// if not a retransmission, notify application that
		// everything has been sent out at least once.
		//
		if (!syn)
		{
			idle();
			if (close_on_empty_ && quiet)
			{
				flags_ |= TF_NEEDCLOSE;
			}
		}
		pflags |= TH_PUSH;
		//
		// if close_on_empty set, we are finished
		// with this connection; close it
		//
	}
	else
	{
		/* not emptying buffer, so can't be FIN */
		pflags &= ~TH_FIN;
	}
	if (infinite_send_ && (syn && datalen == 0))
		pflags |= TH_PUSH; // set PUSH for dataless SYN

	/* sender SWS avoidance (Nagle) */

	if (datalen > 0)
	{
		// if full-sized segment, ok
		if (datalen == maxseg_)
			goto send;
		// if Nagle disabled and buffer clearing, ok
		if ((quiet || nodelay_) && emptying_buffer)
			goto send;
		// if a retransmission
		if (is_retransmit)
			goto send;
		// if big "enough", ok...
		//	(this is not a likely case, and would
		//	only happen for tiny windows)
		if (datalen >= ((wnd_ * maxseg_) / 2.0))
			goto send;
		// Shuang
		if (datalen == 1 && prob_mode_)
			goto send;
	}

	if (need_send())
	{
		//		if(fid_==2352) printf("before need_send fid= %d, rcv_nxt_= %d highest_ack= %d, last_ack_sent= %d\n",fid_,(int)rcv_nxt_,(int)highest_ack_,last_ack_sent_);
		goto send;
	}

	/*
	 * send now if a control packet or we owe peer an ACK
	 * TF_ACKNOW can be set during connection establishment and
	 * to generate acks for out-of-order data
	 */

	if ((flags_ & (TF_ACKNOW | TF_NEEDCLOSE)) ||
		(pflags & (TH_SYN | TH_FIN)))
	{
		goto send;
	}

	/*
	 * No reason to send a segment, just return.
	 */
	return 0;

send:

	// is a syn or fin?
	// printf("made it to send\n");
	syn = (pflags & TH_SYN) ? 1 : 0;

	int fin = (pflags & TH_FIN) ? 1 : 0;

	/* setup ECN syn and ECN SYN+ACK packet headers */
	if (ecn_ && syn && !(pflags & TH_ACK))
	{
		pflags |= TH_ECE;
		pflags |= TH_CWR;
	}
	if (ecn_ && syn && (pflags & TH_ACK))
	{
		pflags |= TH_ECE;
		pflags &= ~TH_CWR;
	}
	else if (ecn_ && ect_ && cong_action_ &&
			 (!is_retransmit || SetCWRonRetransmit_))
	{
		/*
		 * Don't set CWR for a retranmitted SYN+ACK (has ecn_
		 * and cong_action_ set).
		 * -M. Weigle 6/19/02
		 *
		 * SetCWRonRetransmit_ was changed to true,
		 * allowing CWR on retransmitted data packets.
		 * See test ecn_burstyEcn_reno_full
		 * in test-suite-ecn-full.tcl.
		 * - Sally Floyd, 6/5/08.
		 */
		/* set CWR if necessary */
		pflags |= TH_CWR;
		/* Turn cong_action_ off: Added 6/5/08, Sally Floyd. */
		cong_action_ = FALSE;
	}

	/* moved from sendpacket()  -M. Weigle 6/19/02 */
	//
	// although CWR bit is ordinarily associated with ECN,
	// it has utility within the simulator for traces. Thus, set
	// it even if we aren't doing ECN
	//
	if (datalen > 0 && cong_action_ && !is_retransmit)
	{
		pflags |= TH_CWR;
	}

	/* set ECE if necessary */
	if (ecn_ && ect_ && recent_ce_)
	{
		pflags |= TH_ECE;
	}

	/*
	 * Tack on the FIN flag to the data segment if close_on_empty_
	 * was previously set-- avoids sending a separate FIN
	 */
	if (flags_ & TF_NEEDCLOSE)
	{
		std::cout << "Tack on the FIN flag to the data segment if close_on_empty_ was previously set-- avoids sending a separate FIN" << std::endl;
		flags_ &= ~TF_NEEDCLOSE;
		if (state_ <= TCPS_ESTABLISHED && state_ != TCPS_CLOSED)
		{
			std::cout << "Tack on the FIN flag to the data segment if close_on_empty_ was previously set-- avoids sending a separate FIN" << std::endl;

			pflags |= TH_FIN;
			fin = 1; /* FIN consumes sequence number */
			newstate(TCPS_FIN_WAIT_1);
		}
	}
	sendpacket(seqno, rcv_nxt_, pflags, datalen, reason);

	/*
	 * Data sent (as far as we can tell).
	 * Any pending ACK has now been sent.
	 */
	flags_ &= ~(TF_ACKNOW | TF_DELACK);

	// Mohammad
	delack_timer_.force_cancel();
	/*
	if (datalen == 0)
			printf("%f -- %s sent ACK for %d, canceled delack\n", this->name(), Scheduler::instance().clock(), rcv_nxt_);
	*/

	/*
	 * if we have reacted to congestion recently, the
	 * slowdown() procedure will have set cong_action_ and
	 * sendpacket will have copied that to the outgoing pkt
	 * CWR field. If that packet contains data, then
	 * it will be reliably delivered, so we are free to turn off the
	 * cong_action_ state now  If only a pure ACK, we keep the state
	 * around until we actually send a segment
	 */

	int reliable = datalen + syn + fin; // seq #'s reliably sent
	/*
	 * Don't reset cong_action_ until we send new data.
	 * -M. Weigle 6/19/02
	 */
	if (cong_action_ && reliable > 0 && !is_retransmit)
		cong_action_ = FALSE;

	// highest: greatest sequence number sent + 1
	//	and adjusted for SYNs and FINs which use up one number

	int highest = seqno + reliable;
	if (highest > ecnhat_maxseq)
		ecnhat_maxseq = highest;
	if (highest > maxseq_)
	{
		maxseq_ = highest;
		//
		// if we are using conventional RTT estimation,
		// establish timing on this segment
		//
		if (!ts_option_ && rtt_active_ == FALSE)
		{
			rtt_active_ = TRUE; // set timer
			rtt_seq_ = seqno;	// timed seq #
			rtt_ts_ = now();	// when set
		}
	}

	/*
	 * Set retransmit timer if not currently set,
	 * and not doing an ack or a keep-alive probe.
	 * Initial value for retransmit timer is smoothed
	 * round-trip time + 2 * round-trip time variance.
	 * Future values are rtt + 4 * rttvar.
	 */
	if (rtx_timer_.status() != TIMER_PENDING && reliable)
	{
		set_rtx_timer(); // no timer pending, schedule one
	}

	return (reliable);
}

/*
 *
 * send_much: send as much data as we are allowed to.  This is
 * controlled by the "pipectrl_" variable.  If pipectrl_ is set
 * to FALSE, then we are working as a normal window-based TCP and
 * we are allowed to send whatever the window allows.
 * If pipectrl_ is set to TRUE, then we are allowed to send whatever
 * pipe_ allows us to send.  One tricky part is to make sure we
 * do not overshoot the receiver's advertised window if we are
 * in (pipectrl_ == TRUE) mode.
 */

void FullTcpAgent::send_much(int force, int reason, int maxburst)
{
	int npackets = 0; // sent so far

	// if ((int(t_seqno_)) > 1)
	// printf("%f: send_much(f:%d, win:%d, pipectrl:%d, pipe:%d, t_seqno:%d, topwin:%d, maxseq_:%d\n",
	// now(), force, win, pipectrl_, pipe_, int(t_seqno_), topwin, int(maxseq_));

	if (!force && (delsnd_timer_.status() == TIMER_PENDING))
		return;

	while (1)
	{

		/*
		 * note that if output decides to not actually send
		 * (e.g. because of Nagle), then if we don't break out
		 * of this loop, we can loop forever at the same
		 * simulated time instant
		 */
		int amt;
		int seq = nxt_tseq();

		if (!force && !send_allowed(seq))
			break;
		// Q: does this need to be here too?
		if (!force && overhead_ != 0 &&
			(delsnd_timer_.status() != TIMER_PENDING))
		{
			delsnd_timer_.resched(Random::uniform(overhead_));
			return;
		}
		if ((amt = foutput(seq, reason)) <= 0)
		{
			// printf("made call to foutput: returned %d\n", amt);
			break;
		}
		if ((outflags() & TH_FIN))
			--amt; // don't count FINs
		sent(seq, amt);
		force = 0;

		if ((outflags() & (TH_SYN | TH_FIN)) ||
			(maxburst && ++npackets >= maxburst))
			break;
	}
	return;
}

/*
 * base TCP: we are allowed to send a sequence number if it
 * is in the window
 */
int FullTcpAgent::send_allowed(int seq)
{
	int win = window() * maxseg_;
	// Shuang: probe_mode
	if (prob_mode_ && win > 1)
		win = 1;
	int topwin = curseq_; // 1 seq number past the last byte we can send

	if ((topwin > highest_ack_ + win) || infinite_send_)
		topwin = highest_ack_ + win;

	//	if (seq >= topwin) {
	//		printf("%.5lf: fid %d send not allowed\n", now(), fid_);
	//		fflush(stdout);
	//	}
	return (seq < topwin);
}
/*
 * Process an ACK
 *	this version of the routine doesn't necessarily
 *	require the ack to be one which advances the ack number
 *
 * if this ACKs a rtt estimate
 *	indicate we are not timing
 *	reset the exponential timer backoff (gamma)
 * update rtt estimate
 * cancel retrans timer if everything is sent and ACK'd, else set it
 * advance the ack number if appropriate
 * update segment to send next if appropriate
 */
void FullTcpAgent::newack(Packet *pkt)
{

	// Shuang: cancel prob_mode_ when receiving an ack
	prob_mode_ = false;
	prob_count_ = 0;

	hdr_tcp *tcph = hdr_tcp::access(pkt);

	register int ackno = tcph->ackno();
	int progress = (ackno > highest_ack_);

	// printf("NEWACK cur %d last %d ackno %d highest %d\n", cur_sqtotal_, last_sqtotal_,int(ackno), int(highest_ack_));
	if (ackno == maxseq_)
	{
		cancel_rtx_timer(); // all data ACKd
	}
	else if (progress)
	{
		set_rtx_timer();
	}

	// advance the ack number if this is for new data
	if (progress)
	{
		highest_ack_ = ackno;
	}

	// if we have suffered a retransmit timeout, t_seqno_
	// will have been reset to highest_ ack.  If the
	// receiver has cached some data above t_seqno_, the
	// new-ack value could (should) jump forward.  We must
	// update t_seqno_ here, otherwise we would be doing
	// go-back-n.

	if (t_seqno_ < highest_ack_)
		t_seqno_ = highest_ack_; // seq# to send next

	/*
		 * Update RTT only if it's OK to do so from info in the flags header.
		 * This is needed for protocols in which intermediate agents

		 * in the network intersperse acks (e.g., ack-reconstructors) for
		 * various reasons (without violating e2e semantics).
		 */
	hdr_flags *fh = hdr_flags::access(pkt);

	if (!fh->no_ts_)
	{
		if (ts_option_)
		{
			recent_age_ = now();
			recent_ = tcph->ts();
			rtt_update(now() - tcph->ts_echo());
			if (ts_resetRTO_ && (!ect_ || !ecn_backoff_ ||
								 !hdr_flags::access(pkt)->ecnecho()))
			{
				// From Andrei Gurtov
				//
				// Don't end backoff if still in ECN-Echo with
				// a congestion window of 1 packet.
				t_backoff_ = 1;
			}
		}
		else if (rtt_active_ && ackno > rtt_seq_)
		{
			// got an RTT sample, record it
			// "t_backoff_ = 1;" deleted by T. Kelly.
			rtt_active_ = FALSE;
			rtt_update(now() - rtt_ts_);
		}
		if (!ect_ || !ecn_backoff_ ||
			!hdr_flags::access(pkt)->ecnecho())
		{
			/*
			 * Don't end backoff if still in ECN-Echo with
			 * a congestion window of 1 packet.
			 * Fix from T. Kelly.
			 */
			t_backoff_ = 1;
			ecn_backoff_ = 0;
		}
	}
	return;
}

/*
 * this is the simulated form of the header prediction
 * predicate.  While not really necessary for a simulation, it
 * follows the code base more closely and can sometimes help to reveal
 * odd behavior caused by the implementation structure..
 *
 * Here's the comment from the real TCP:
 *
 * Header prediction: check for the two common cases
 * of a uni-directional data xfer.  If the packet has
 * no control flags, is in-sequence, the window didn't
 * change and we're not retransmitting, it's a
 * candidate.  If the length is zero and the ack moved
 * forward, we're the sender side of the xfer.  Just
 * free the data acked & wake any higher level process
 * that was blocked waiting for space.  If the length
 * is non-zero and the ack didn't move, we're the
 * receiver side.  If we're getting packets in-order
 * (the reassembly queue is empty), add the data to
 * the socket buffer and note that we need a delayed ack.
 * Make sure that the hidden state-flags are also off.
 * Since we check for TCPS_ESTABLISHED above, it can only
 * be TF_NEEDSYN.
 */

int FullTcpAgent::predict_ok(Packet *pkt)
{
	hdr_tcp *tcph = hdr_tcp::access(pkt);
	hdr_flags *fh = hdr_flags::access(pkt);

	/* not the fastest way to do this, but perhaps clearest */

	int p1 = (state_ == TCPS_ESTABLISHED);							   // ready
	int p2 = ((tcph->flags() & (TH_SYN | TH_FIN | TH_ACK)) == TH_ACK); // ACK
	int p3 = ((flags_ & TF_NEEDFIN) == 0);							   // don't need fin
	int p4 = (!ts_option_ || fh->no_ts_ || (tcph->ts() >= recent_));   // tsok
	int p5 = (tcph->seqno() == rcv_nxt_);							   // in-order data
	int p6 = (t_seqno_ == maxseq_);									   // not re-xmit
	int p7 = (!ecn_ || fh->ecnecho() == 0);							   // no ECN
	int p8 = (tcph->sa_length() == 0);								   // no SACK info

	return (p1 && p2 && p3 && p4 && p5 && p6 && p7 && p8);
}

/*
 * fast_retransmit using the given seqno
 *	perform fast RTX, set recover_, set last_cwnd_action
 */

int FullTcpAgent::fast_retransmit(int seq)
{
	// we are now going to fast-retransmit and willtrace that event
	trace_event("FAST_RETX");
	// printf("%f: fid %d did a fast retransmit - dupacks = %d\n", now(), fid_, (int)dupacks_);
	recover_ = maxseq_; // recovery target
	last_cwnd_action_ = CWND_ACTION_DUPACK;
	return (foutput(seq, REASON_DUPACK)); // send one pkt
}

/*
 * real tcp determines if the remote
 * side should receive a window update/ACK from us, and often
 * results in sending an update every 2 segments, thereby
 * giving the familiar 2-packets-per-ack behavior of TCP.
 * Here, we don't advertise any windows, so we just see if
 * there's at least 'segs_per_ack_' pkts not yet acked
 *
 * also, provide for a segs-per-ack "threshold" where
 * we generate 1-ack-per-seg until enough stuff
 * (spa_thresh_ bytes) has been received from the other side
 * This idea came from vj/kmn in BayTcp.  Added 8/21/01.
 */

int FullTcpAgent::need_send()
{
	if (flags_ & TF_ACKNOW)
		return TRUE;

	int spa = (spa_thresh_ > 0 && ((rcv_nxt_ - irs_) < spa_thresh_)) ? 1 : segs_per_ack_;
	// Shuang
	return ((rcv_nxt_ - last_ack_sent_) > 0);
	// return ((rcv_nxt_ - last_ack_sent_) >= spa * maxseg_);
}

/*
 * determine whether enough time has elapsed in order to
 * conclude a "restart" is necessary (e.g. a slow-start)
 *
 * for now, keep track of this similarly to how rtt_update() does
 */

int FullTcpAgent::idle_restart()
{
	if (last_send_time_ < 0.0)
	{
		// last_send_time_ isn't set up yet, we shouldn't
		// do the idle_restart
		return (0);
	}

	double tao = now() - last_send_time_;
	if (!ts_option_)
	{
		double tickoff = fmod(last_send_time_ + boot_time_,
							  tcp_tick_);
		tao = int((tao + tickoff) / tcp_tick_) * tcp_tick_;
	}

	return (tao > t_rtxcur_); // verify this CHECKME
							  // return (tao > (int(t_srtt_) >> T_SRTT_BITS)*tcp_tick_); //Mohammad
}

/*
 * tcp-full's version of set_initial_window()... over-rides
 * the one in tcp.cc
 */
void FullTcpAgent::set_initial_window()
{
	syn_ = TRUE; // full-tcp always models SYN exchange
	TcpAgent::set_initial_window();
}

/*
 * main reception path -
 * called from the agent that handles the data path below in its muxing mode
 * advance() is called when connection is established with size sent from
 * user/application agent
 *
 * This is a fairly complex function.  It operates generally as follows:
 *	do header prediction for simple cases (pure ACKS or data)
 *	if in LISTEN and we get a SYN, begin initializing connection
 *	if in SYN_SENT and we get an ACK, complete connection init
 *	trim any redundant data from received dataful segment
 *	deal with ACKS:
 *		if in SYN_RCVD, complete connection init then go on
 *		see if ACK is old or at the current highest_ack
 *		if at current high, is the threshold reached or not
 *		if so, maybe do fast rtx... otherwise drop or inflate win
 *	deal with incoming data
 *	deal with FIN bit on in arriving packet
 */
void FullTcpAgent::recv(Packet *pkt, Handler *)
{
	slog::log5(debug_, addr(), "FullTcpAgent::recv() packet of size:", hdr_cmn::access(pkt)->size());
	assert(Scheduler::instance().clock() <= 10.0 || hdr_cmn::access(pkt)->size() <= MAX_ETHERNET_FRAME_ON_WIRE);
	// adjust size
	hdr_cmn::access(pkt)->size() -= (INTER_PKT_GAP_SIZE + ETHERNET_PREAMBLE_SIZE + ETHERNET_HEADER_SIZE);
	slog::log5(debug_, addr(), "FullTcpAgent::recv() packet of adjusted size:", hdr_cmn::access(pkt)->size());

	// Shuang: cancel probe mode
	prob_mode_ = false;
	prob_count_ = 0;

	hdr_tcp *tcph = hdr_tcp::access(pkt);	// TCP header
	hdr_cmn *th = hdr_cmn::access(pkt);		// common header (size, etc)
	hdr_flags *fh = hdr_flags::access(pkt); // flags (CWR, CE, bits)
	hdr_r2p2 *r2p2_hdr = nullptr;
	if (conctd_to_pfabric_app_)
	{
		r2p2_hdr = hdr_r2p2::access(pkt);
	}

	int needoutput = FALSE;
	int ourfinisacked = FALSE;
	int dupseg = FALSE; // recv'd dup data segment
	int todrop = 0;		// duplicate DATA cnt in seg

	last_state_ = state_;

	int datalen = th->size() - tcph->hlen(); // # payload bytes
											 // printf("fid2= %d datalen= %d\n",fid_,datalen);
	int ackno = tcph->ackno();				 // ack # from packet
	int tiflags = tcph->flags();			 // tcp flags from packet

	// if (state_ != TCPS_ESTABLISHED || (tiflags&(TH_SYN|TH_FIN))) {
	// fprintf(stdout, "%f(%s)in state %s recv'd this packet: ", now(), name(), statestr(state_));
	// prpkt(pkt);
	// }
	//  std::cout<< "recv(): " << " " << addr() << " " << port() << " " << daddr() << " " << dport() << "|" << state_ << "||" << th->size() << "<now> " << Scheduler::instance().clock() << std::endl;
	//  std::cout << "PRIO: " << hdr_ip::access(pkt)->prio() << std::endl;
	//  std::cout << "WINODW sZ: " << cwnd_ << std::endl;

	/*
	 * Acknowledge FIN from passive closer even in TCPS_CLOSED state
	 * (since we lack TIME_WAIT state and RST packets,
	 * the loss of the FIN packet from the passive closer will make that
	 * endpoint retransmit the FIN forever)
	 * -F. Hernandez-Campos 8/6/00
	 */
	if ((state_ == TCPS_CLOSED) && (tiflags & TH_FIN))
	{
		goto dropafterack;
	}

	/*
	 * Don't expect to see anything while closed
	 */

	if (state_ == TCPS_CLOSED)
	{
		if (debug_)
		{
			fprintf(stderr, "%f: FullTcp(%s): recv'd pkt in CLOSED state: ",
					now(), name());
			prpkt(pkt);
		}
		goto drop;
	}

	/*
	 *  Shuang: if fid does not match, drop packets
	 */
	if (fid_ != hdr_ip::access(pkt)->fid_)
	{
		// printf("extra!%d %d\n", fid_, hdr_ip::access(pkt)->fid_);
		goto drop;
	}

	/*
	 * Process options if not in LISTEN state,
	 * else do it below
	 */
	if (state_ != TCPS_LISTEN)
		dooptions(pkt);

	/*
	 * if we are using delayed-ACK timers and
	 * no delayed-ACK timer is set, set one.
	 * They are set to fire every 'interval_' secs, starting
	 * at time t0 = (0.0 + k * interval_) for some k such
	 * that t0 > now
	 */
	/*
	 *Mohammad: commented this out for more efficient
	 * delayed ack generation
	 */
	/*if (delack_interval_ > 0.0 &&
		(delack_timer_.status() != TIMER_PENDING)) {
		int last = int(now() / delack_interval_);
		delack_timer_.resched(delack_interval_ * (last + 1.0) - now());
		}*/

	// Mohammad
	if (ecnhat_)
		update_ecnhat_alpha(pkt);

	/* Mohammad: check if we need to inform
	 * pacer of ecnecho.
	 */
	if (!(tiflags & TH_SYN) && fh->ecnecho())
		informpacer = 1;

	/*if (datalen > 0)
	  printf("received data: datalen = %d seqno = %d, ackno = %d, ce = %d, ecn-echo = %d\n", datalen, tcph->seqno(), ackno, fh->ce(), fh->ecnecho());
	else
	  printf("received ack : datalen = %d seqno = %d, ackno = %d, ce = %d, ecn-echo = %d\n", datalen, tcph->seqno(), ackno, fh->ce(), fh->ecnecho());
	*/

	/*
	 * Try header prediction: in seq data or in seq pure ACK
	 *	with no funny business
	 */
	if (!nopredict_ && predict_ok(pkt))
	{
		/*
		 * If last ACK falls within this segment's sequence numbers,
		 * record the timestamp.
		 * See RFC1323 (now RFC1323 bis)
		 */
		if (ts_option_ && !fh->no_ts_ &&
			tcph->seqno() <= last_ack_sent_)
		{
			/*
			 * this is the case where the ts value is newer than
			 * the last one we've seen, and the seq # is the one
			 * we expect [seqno == last_ack_sent_] or older
			 */
			recent_age_ = now();
			recent_ = tcph->ts();
		}

		//
		// generate a stream of ecnecho bits until we see a true
		// cong_action bit
		//

		if (ecn_)
		{
			if (ecnhat_)
			{ // Mohammad
				if (fh->ce() && fh->ect())
				{
					// no CWR from peer yet... arrange to
					// keep sending ECNECHO
					if (recent_ce_ == FALSE)
					{
						ce_transition_ = 1;
						recent_ce_ = TRUE;
					}
					else
					{
						ce_transition_ = 0;
					}
				}
				else if (datalen > 0 && !fh->ce() && fh->ect())
				{
					if (recent_ce_ == TRUE)
					{
						ce_transition_ = 1;
						recent_ce_ = FALSE;
					}
					else
					{
						ce_transition_ = 0;
					}
				}
			}
			else
			{
				if (fh->ce() && fh->ect())
				{
					// no CWR from peer yet... arrange to
					// keep sending ECNECHO
					recent_ce_ = TRUE;
				}
				else if (fh->cwr())
				{
					// got CWR response from peer.. stop
					// sending ECNECHO bits
					recent_ce_ = FALSE;
				}
			}
		}

		// Header predication basically looks to see
		// if the incoming packet is an expected pure ACK
		// or an expected data segment

		if (datalen == 0)
		{
			// check for a received pure ACK in the correct range..
			// also checks to see if we are wnd_ limited
			// (we don't change cwnd at all below), plus
			// not being in fast recovery and not a partial ack.
			// If we are in fast
			// recovery, go below so we can remember to deflate
			// the window if we need to
			if (ackno > highest_ack_ && ackno < maxseq_ &&
				cwnd_ >= wnd_ && !fastrecov_)
			{
				newack(pkt); // update timers,  highest_ack_
				send_much(0, REASON_NORMAL, maxburst_);
				Packet::free(pkt);
				return;
			}
		}
		else if (ackno == highest_ack_ && rq_.empty())
		{
			// check for pure incoming segment
			// the next data segment we're awaiting, and
			// that there's nothing sitting in the reassem-
			// bly queue
			// 	give to "application" here
			//	note: DELACK is inspected only by
			//	tcp_fasttimo() in real tcp.  Every 200 ms
			//	this routine scans all tcpcb's looking for
			//	DELACK segments and when it finds them
			//	changes DELACK to ACKNOW and calls tcp_output()

			/* Mohammad: For DCTCP state machine */
			if (ecnhat_ && ce_transition_ && ((rcv_nxt_ - last_ack_sent_) > 0))
			{
				// Must send an immediate ACK with with previous ECN state
				// before transitioning to new state
				flags_ |= TF_ACKNOW;
				recent_ce_ = !recent_ce_;
				// printf("should be acking %d with recent_ce_ = %d\n", rcv_nxt_, recent_ce_);
				send_much(1, REASON_NORMAL, maxburst_);
				recent_ce_ = !recent_ce_;
			}

			rcv_nxt_ += datalen;

			flags_ |= TF_DELACK;
			// Mohammad
			delack_timer_.resched(delack_interval_);

			// printf("%f: receving data %d, rescheduling delayed ack\n", Scheduler::instance().clock(), rcv_nxt_);

			// hack for pfabric app
			if (conctd_to_pfabric_app_)
			{
				RequestIdTuple req_id = RequestIdTuple();
				req_id.app_level_id_ = r2p2_hdr->app_level_id();
				req_id.msg_bytes_ = r2p2_hdr->msg_bytes();
				req_id.is_request_ = (r2p2_hdr->msg_type() == hdr_r2p2::REQUEST);
				req_id.cl_thread_id_ = r2p2_hdr->cl_thread_id();
				req_id.cl_addr_ = r2p2_hdr->cl_addr();
				req_id.sr_addr_ = r2p2_hdr->sr_addr();
				req_id.client_port_ = dport();
				req_id.ts_ = r2p2_hdr->msg_creation_time();
				app_->recv_msg(datalen, std::move(req_id));
			}
			else
			{
				recvBytes(datalen); // notify application of "delivery"
			}

			// printf("flow_remaining before dec = %d\n" , flow_remaining_);
			if (flow_remaining_ > 0)
				flow_remaining_ -= datalen; // Mohammad

			if (flow_remaining_ == 0)
			{
				flags_ |= TF_ACKNOW;
				flow_remaining_ = -1;
			}
			// printf("flow_remaining after dec = %d\n" , flow_remaining_);

			//
			// special code here to simulate the operation
			// of a receiver who always consumes data,
			// resulting in a call to tcp_output
			Packet::free(pkt);
			if (need_send())
			{
				send_much(1, REASON_NORMAL, maxburst_);
				//				if(fid_==2352) printf("before2 need_send fid= %d, rcv_nxt_= %d highest_ack= %d, last_ack_sent= %d\n",fid_,(int)rcv_nxt_,(int)highest_ack_,last_ack_sent_);
			}
			return;
		}
	} /* header prediction */

	//
	// header prediction failed
	// (e.g. pure ACK out of valid range, SACK present, etc)...
	// do slow path processing

	//
	// the following switch does special things for these states:
	//	TCPS_LISTEN, TCPS_SYN_SENT
	//

	switch (state_)
	{

		/*
		 * If the segment contains an ACK then it is bad and do reset.
		 * If it does not contain a SYN then it is not interesting; drop it.
		 * Otherwise initialize tp->rcv_nxt, and tp->irs, iss is already
		 * selected, and send a segment:
		 *     <SEQ=ISS><ACK=RCV_NXT><CTL=SYN,ACK>
		 * Initialize tp->snd_nxt to tp->iss.
		 * Enter SYN_RECEIVED state, and process any other fields of this
		 * segment in this state.
		 */

	case TCPS_LISTEN: /* awaiting peer's SYN */

		if (tiflags & TH_ACK)
		{
			if (debug_)
			{
				fprintf(stderr,
						"%f: FullTcpAgent(%s): warning: recv'd ACK while in LISTEN: ",
						now(), name());
				prpkt(pkt);
			}
			// don't want ACKs in LISTEN
			goto dropwithreset;
		}
		if ((tiflags & TH_SYN) == 0)
		{
			if (debug_)
			{
				fprintf(stderr, "%f: FullTcpAgent(%s): warning: recv'd NON-SYN while in LISTEN\n",
						now(), name());
				prpkt(pkt);
			}
			// any non-SYN is discarded
			goto drop;
		}

		/*
		 * must by a SYN (no ACK) at this point...
		 * in real tcp we would bump the iss counter here also
		 */
		// std::cout << "SYN PKT RECVD. pkt size: " << th->size() <<std::endl;
		dooptions(pkt);
		irs_ = tcph->seqno();
		t_seqno_ = iss_; /* tcp_sendseqinit() macro in real tcp */
		rcv_nxt_ = rcvseqinit(irs_, datalen);
		flags_ |= TF_ACKNOW;
		// check for a ECN-SYN with ECE|CWR
		if (ecn_ && fh->ecnecho() && fh->cong_action())
		{
			ect_ = TRUE;
		}

		if (fid_ == 0)
		{
			// XXX: sort of hack... If we do not
			// have a special flow ID, pick up that
			// of the sender (active opener)
			hdr_ip *iph = hdr_ip::access(pkt);
			fid_ = iph->flowid();
		}

		newstate(TCPS_SYN_RECEIVED);
		goto trimthenstep6;

		/*
		 * If the state is SYN_SENT:
		 *      if seg contains an ACK, but not for our SYN, drop the input.
		 *      if seg does not contain SYN, then drop it.
		 * Otherwise this is an acceptable SYN segment
		 *      initialize tp->rcv_nxt and tp->irs
		 *      if seg contains ack then advance tp->snd_una
		 *      if SYN has been acked change to ESTABLISHED else SYN_RCVD state
		 *      arrange for segment to be acked (eventually)
		 *      continue processing rest of data/controls, beginning with URG
		 */

	case TCPS_SYN_SENT: /* we sent SYN, expecting SYN+ACK (or SYN) */

		/* drop if it's a SYN+ACK and the ack field is bad */
		if ((tiflags & TH_ACK) &&
			((ackno <= iss_) || (ackno > maxseq_)))
		{
			// not an ACK for our SYN, discard
			if (debug_)
			{
				fprintf(stderr, "%f: FullTcpAgent::recv(%s): bad ACK for our SYN: ",
						now(), name());
				prpkt(pkt);
			}
			goto dropwithreset;
		}

		if ((tiflags & TH_SYN) == 0)
		{
			if (debug_)
			{
				fprintf(stderr, "%f: FullTcpAgent::recv(%s): no SYN for our SYN: ",
						now(), name());
				prpkt(pkt);
			}
			goto drop;
		}

		/* looks like an ok SYN or SYN+ACK */
		// If ecn_syn_wait is set to 2:
		// Check if CE-marked SYN/ACK packet, then just send an ACK
		//  packet with ECE set, and drop the SYN/ACK packet.
		//  Don't update TCP state.
		if (tiflags & TH_ACK)
		{
			if (ecn_ && fh->ecnecho() && !fh->cong_action() && ecn_syn_wait_ == 2)
			// if SYN/ACK packet and ecn_syn_wait_ == 2
			{
				if (fh->ce())
				// If SYN/ACK packet is CE-marked
				{
					// cancel_rtx_timer();
					// newack(pkt);
					set_rtx_timer();
					sendpacket(t_seqno_, rcv_nxt_, TH_ACK | TH_ECE, 0, 0);
					goto drop;
				}
			}
		}

#ifdef notdef
		cancel_rtx_timer(); // cancel timer on our 1st SYN [does this belong!?]
#endif
		irs_ = tcph->seqno(); // get initial recv'd seq #
		rcv_nxt_ = rcvseqinit(irs_, datalen);

		if (tiflags & TH_ACK)
		{
			// std::cout << "SYNACK" << std::endl;
			// SYN+ACK (our SYN was acked)
			if (ecn_ && fh->ecnecho() && !fh->cong_action())
			{
				ect_ = TRUE;
				if (fh->ce())
					recent_ce_ = TRUE;
			}
			highest_ack_ = ackno;
			cwnd_ = initial_window();

#ifdef notdef
			/*
			 * if we didn't have to retransmit the SYN,
			 * use its rtt as our initial srtt & rtt var.
			 */
			if (t_rtt_)
			{
				double tao = now() - tcph->ts();
				rtt_update(tao);
			}
#endif

			/*
			 * if there's data, delay ACK; if there's also a FIN
			 * ACKNOW will be turned on later.
			 */
			if (datalen > 0)
			{
				flags_ |= TF_DELACK; // data there: wait
				// Mohammad
				delack_timer_.resched(delack_interval_);
			}
			else
			{
				flags_ |= TF_ACKNOW; // ACK peer's SYN
			}

			/*
			 * Received <SYN,ACK> in SYN_SENT[*] state.
			 * Transitions:
			 *      SYN_SENT  --> ESTABLISHED
			 *      SYN_SENT* --> FIN_WAIT_1
			 */

			if (flags_ & TF_NEEDFIN)
			{
				newstate(TCPS_FIN_WAIT_1);
				flags_ &= ~TF_NEEDFIN;
				tiflags &= ~TH_SYN;
			}
			else
			{
				newstate(TCPS_ESTABLISHED);
			}

			// special to ns:
			//  generate pure ACK here.
			//  this simulates the ordinary connection establishment
			//  where the ACK of the peer's SYN+ACK contains
			//  no data.  This is typically caused by the way
			//  the connect() socket call works in which the
			//  entire 3-way handshake occurs prior to the app
			//  being able to issue a write() [which actually
			//  causes the segment to be sent].
			sendpacket(t_seqno_, rcv_nxt_, TH_ACK, 0, 0);
		}
		else
		{
			// Check ECN-SYN packet
			if (ecn_ && fh->ecnecho() && fh->cong_action())
				ect_ = TRUE;

			// SYN (no ACK) (simultaneous active opens)
			flags_ |= TF_ACKNOW;
			cancel_rtx_timer();
			newstate(TCPS_SYN_RECEIVED);
			/*
			 * decrement t_seqno_: we are sending a
			 * 2nd SYN (this time in the form of a
			 * SYN+ACK, so t_seqno_ will have been
			 * advanced to 2... reduce this
			 */
			t_seqno_--; // CHECKME
		}

	trimthenstep6:
		/*
		 * advance the seq# to correspond to first data byte
		 */
		tcph->seqno()++;

		if (tiflags & TH_ACK)
			goto process_ACK;

		goto step6;

	case TCPS_LAST_ACK:
		/*
		 * The only way we're in LAST_ACK is if we've already
		 * received a FIN, so ignore all retranmitted FINS.
		 * -M. Weigle 7/23/02
		 */
		if (tiflags & TH_FIN)
		{
			goto drop;
		}
		break;
	case TCPS_CLOSING:
		break;
	} /* end switch(state_) */

	/*
	 * States other than LISTEN or SYN_SENT.
	 * First check timestamp, if present.
	 * Then check that at least some bytes of segment are within
	 * receive window.  If segment begins before rcv_nxt,
	 * drop leading data (and SYN); if nothing left, just ack.
	 *
	 * RFC 1323 PAWS: If we have a timestamp reply on this segment
	 * and it's less than ts_recent, drop it.
	 */

	if (ts_option_ && !fh->no_ts_ && recent_ && tcph->ts() < recent_)
	{
		if ((now() - recent_age_) > TCP_PAWS_IDLE)
		{
			/*
			 * this is basically impossible in the simulator,
			 * but here it is...
			 */
			/*
			 * Invalidate ts_recent.  If this segment updates
			 * ts_recent, the age will be reset later and ts_recent
			 * will get a valid value.  If it does not, setting
			 * ts_recent to zero will at least satisfy the
			 * requirement that zero be placed in the timestamp
			 * echo reply when ts_recent isn't valid.  The
			 * age isn't reset until we get a valid ts_recent
			 * because we don't want out-of-order segments to be
			 * dropped when ts_recent is old.
			 */
			recent_ = 0.0;
		}
		else
		{
			fprintf(stderr, "%f: FullTcpAgent(%s): dropped pkt due to bad ts\n",
					now(), name());
			goto dropafterack;
		}
	}

	// check for redundant data at head/tail of segment
	//	note that the 4.4bsd [Net/3] code has
	//	a bug here which can cause us to ignore the
	//	perfectly good ACKs on duplicate segments.  The
	//	fix is described in (Stevens, Vol2, p. 959-960).
	//	This code is based on that correction.
	//
	// In addition, it has a modification so that duplicate segments
	// with dup acks don't trigger a fast retransmit when dupseg_fix_
	// is enabled.
	//
	// Yet one more modification: make sure that if the received
	//	segment had datalen=0 and wasn't a SYN or FIN that
	//	we don't turn on the ACKNOW status bit.  If we were to
	//	allow ACKNOW to be turned on, normal pure ACKs that happen
	//	to have seq #s below rcv_nxt can trigger an ACK war by
	//	forcing us to ACK the pure ACKs
	//
	// Update: if we have a dataless FIN, don't really want to
	// do anything with it.  In particular, would like to
	// avoid ACKing an incoming FIN+ACK while in CLOSING
	//
	todrop = rcv_nxt_ - tcph->seqno(); // how much overlap?

	if (todrop > 0 && ((tiflags & (TH_SYN)) || datalen > 0))
	{
		// printf("%f(%s): trim 1..todrop:%d, dlen:%d\n",now(), name(), todrop, datalen);
		if (tiflags & TH_SYN)
		{
			tiflags &= ~TH_SYN;
			tcph->seqno()++;
			th->size()--; // XXX Must decrease packet size too!!
						  // Q: Why?.. this is only a SYN
			todrop--;
		}
		//
		// see Stevens, vol 2, p. 960 for this check;
		// this check is to see if we are dropping
		// more than this segment (i.e. the whole pkt + a FIN),
		// or just the whole packet (no FIN)
		//
		if ((todrop > datalen) ||
			(todrop == datalen && ((tiflags & TH_FIN) == 0)))
		{
			// printf("%f(%s): trim 2..todrop:%d, dlen:%d\n",now(), name(), todrop, datalen);
			/*
			 * Any valid FIN must be to the left of the window.
			 * At this point the FIN must be a duplicate or out
			 * of sequence; drop it.
			 */

			tiflags &= ~TH_FIN;

			/*
			 * Send an ACK to resynchronize and drop any data.
			 * But keep on processing for RST or ACK.
			 */

			flags_ |= TF_ACKNOW;
			todrop = datalen;
			dupseg = TRUE; // *completely* duplicate
		}

		/*
		 * Trim duplicate data from the front of the packet
		 */

		tcph->seqno() += todrop;
		th->size() -= todrop; // XXX Must decrease size too!!
							  // why? [kf]..prob when put in RQ
		datalen -= todrop;

	} /* data trim */

	/*
	 * If we are doing timstamps and this packet has one, and
	 * If last ACK falls within this segment's sequence numbers,
	 * record the timestamp.
	 * See RFC1323 (now RFC1323 bis)
	 */
	if (ts_option_ && !fh->no_ts_ && tcph->seqno() <= last_ack_sent_)
	{
		/*
		 * this is the case where the ts value is newer than
		 * the last one we've seen, and the seq # is the one we expect
		 * [seqno == last_ack_sent_] or older
		 */
		recent_age_ = now();
		recent_ = tcph->ts();
	}

	if (tiflags & TH_SYN)
	{
		if (debug_)
		{
			fprintf(stderr, "%f: FullTcpAgent::recv(%s) received unexpected SYN (state:%d): ",
					now(), name(), state_);
			prpkt(pkt);
		}
		goto dropwithreset;
	}

	if ((tiflags & TH_ACK) == 0)
	{
		/*
		 * Added check for state != SYN_RECEIVED.  We will receive a
		 * duplicate SYN in SYN_RECEIVED when our SYN/ACK was dropped.
		 * We should just ignore the duplicate SYN (our timeout for
		 * resending the SYN/ACK is about the same as the client's
		 * timeout for resending the SYN), but give no error message.
		 * -M. Weigle 07/24/01
		 */
		if (state_ != TCPS_SYN_RECEIVED)
		{
			if (debug_)
			{
				fprintf(stderr, "%f: FullTcpAgent::recv(%s) got packet lacking ACK (state:%d): ",
						now(), name(), state_);
				prpkt(pkt);
			}
		}
		goto drop;
	}

	/*
	 * Ack processing.
	 */

	switch (state_)
	{
	case TCPS_SYN_RECEIVED: /* want ACK for our SYN+ACK */
		if (ackno < highest_ack_ || ackno > maxseq_)
		{
			// not in useful range
			if (debug_)
			{
				fprintf(stderr, "%f: FullTcpAgent(%s): ack(%d) not in range while in SYN_RECEIVED: ",
						now(), name(), ackno);
				prpkt(pkt);
			}
			goto dropwithreset;
		}

		if (ecn_ && ect_ && ecn_syn_ && fh->ecnecho() && ecn_syn_wait_ == 2)
		{
			// The SYN/ACK packet was ECN-marked.
			// Reset the rtx timer, send another SYN/ACK packet
			//  immediately, and drop the ACK packet.
			// Do not move to TCPS_ESTB state or update TCP variables.
			cancel_rtx_timer();
			ecn_syn_next_ = 0;
			foutput(iss_, REASON_NORMAL);
			wnd_init_option_ = 1;
			wnd_init_ = 1;
			goto drop;
		}
		if (ecn_ && ect_ && ecn_syn_ && fh->ecnecho() && ecn_syn_wait_ < 2)
		{
			// The SYN/ACK packet was ECN-marked.
			if (ecn_syn_wait_ == 1)
			{
				// A timer will be called in ecn().
				cwnd_ = 1;
				use_rtt_ = 1; // KK, wait for timeout() period
			}
			else
			{
				// Congestion window will be halved in ecn().
				cwnd_ = 2;
			}
		}
		else
		{
			cwnd_ = initial_window();
		}

		/*
		 * Make transitions:
		 *      SYN-RECEIVED  -> ESTABLISHED
		 *      SYN-RECEIVED* -> FIN-WAIT-1
		 */
		if (flags_ & TF_NEEDFIN)
		{
			newstate(TCPS_FIN_WAIT_1);
			flags_ &= ~TF_NEEDFIN;
		}
		else
		{
			newstate(TCPS_ESTABLISHED);
		}

		/* fall into ... */

		/*
		 * In ESTABLISHED state: drop duplicate ACKs; ACK out of range
		 * ACKs.  If the ack is in the range
		 *      tp->snd_una < ti->ti_ack <= tp->snd_max
		 * then advance tp->snd_una to ti->ti_ack and drop
		 * data from the retransmission queue.
		 *
		 * note that state TIME_WAIT isn't used
		 * in the simulator
		 */

	case TCPS_ESTABLISHED:
	case TCPS_FIN_WAIT_1:
	case TCPS_FIN_WAIT_2:
	case TCPS_CLOSE_WAIT:
	case TCPS_CLOSING:
	case TCPS_LAST_ACK:

		//
		// look for ECNs in ACKs, react as necessary
		//

		if (fh->ecnecho() && (!ecn_ || !ect_))
		{
			fprintf(stderr,
					"%f: FullTcp(%s): warning, recvd ecnecho but I am not ECN capable! %d %d\n",
					now(), name(), ecn_);
		}

		//
		// generate a stream of ecnecho bits until we see a true
		// cong_action bit
		//

		if (ecn_)
		{
			if (ecnhat_)
			{ // Mohammad
				if (fh->ce() && fh->ect())
				{
					// no CWR from peer yet... arrange to
					// keep sending ECNECHO
					if (recent_ce_ == FALSE)
					{
						ce_transition_ = 1;
						recent_ce_ = TRUE;
					}
					else
					{
						ce_transition_ = 0;
					}
				}
				else if (datalen > 0 && !fh->ce() && fh->ect())
				{
					if (recent_ce_ == TRUE)
					{
						ce_transition_ = 1;
						recent_ce_ = FALSE;
					}
					else
					{
						ce_transition_ = 0;
					}
				}
			}
			else
			{
				if (fh->ce() && fh->ect())
				{
					// no CWR from peer yet... arrange to
					// keep sending ECNECHO
					recent_ce_ = TRUE;
				}
				else if (fh->cwr())
				{
					// got CWR response from peer.. stop
					// sending ECNECHO bits
					recent_ce_ = FALSE;
				}
			}
		}

		//
		// If ESTABLISHED or starting to close, process SACKS
		//

		if (state_ >= TCPS_ESTABLISHED && tcph->sa_length() > 0)
		{
			process_sack(tcph);
		}

		//
		// ACK indicates packet left the network
		//	try not to be fooled by data
		//

		if (fastrecov_ && (datalen == 0 || ackno > highest_ack_))
			pipe_ -= maxseg_;

		// look for dup ACKs (dup ack numbers, no data)
		//
		// do fast retransmit/recovery if at/past thresh
		// if (ackno <= highest_ack_) printf("dupi= %d\n",(int)dupacks_);
		// else printf("in fully\n");
		// Shuang:
		//		if (ackno <= highest_ack_ && cur_sqtotal_ <= last_sqtotal_) {
		if (ackno <= highest_ack_)
		{
			// a pure ACK which doesn't advance highest_ack_
			// printf("dupi= %d\n",dupacks_);
			if (datalen == 0 && (!dupseg_fix_ || !dupseg))
			{

				// Mohammad: check for dynamic dupack mode.
				if (dynamic_dupack_ > 0.0)
				{
					tcprexmtthresh_ = int(dynamic_dupack_ * window());
					if (tcprexmtthresh_ < 3)
						tcprexmtthresh_ = 3;
				}
				/*
				 * If we have outstanding data
				 * this is a completely
				 * duplicate ack,
				 * the ack is the biggest we've
				 * seen and we've seen exactly our rexmt
				 * threshhold of them, assume a packet
				 * has been dropped and retransmit it.
				 *
				 * We know we're losing at the current
				 * window size so do congestion avoidance.
				 *
				 * Dup acks mean that packets have left the
				 * network (they're now cached at the receiver)
				 * so bump cwnd by the amount in the receiver
				 * to keep a constant cwnd packets in the
				 * network.
				 */

				if ((rtx_timer_.status() != TIMER_PENDING) ||
					ackno < highest_ack_)
				{
					// Q: significance of timer not pending?
					// ACK below highest_ack_
					oldack();
				}
				else if (++dupacks_ == tcprexmtthresh_)
				{
					// ACK at highest_ack_ AND meets threshold
					// trace_event("FAST_RECOVERY");
					// Shuang: dupack_action
					dupack_action(); // maybe fast rexmt
					goto drop;
				}
				else if (dupacks_ > tcprexmtthresh_)
				{
					// ACK at highest_ack_ AND above threshole
					// trace_event("FAST_RECOVERY");
					extra_ack();

					// send whatever window allows
					send_much(0, REASON_DUPACK, maxburst_);
					goto drop;
				}
			}
			else
			{
				// non zero-length [dataful] segment
				// with a dup ack (normal for dataful segs)
				// (or window changed in real TCP).
				if (dupack_reset_)
				{
					dupacks_ = 0;
					fastrecov_ = FALSE;
				}
			}
			break; /* take us to "step6" */
		}		   /* end of dup/old acks */

		/*
		 * we've finished the fast retransmit/recovery period
		 * (i.e. received an ACK which advances highest_ack_)
		 * The ACK may be "good" or "partial"
		 */

	process_ACK:

		if (ackno > maxseq_)
		{
			// ack more than we sent(!?)
			if (debug_)
			{
				fprintf(stderr, "%f: FullTcpAgent::recv(%s) too-big ACK (maxseq:%d): ",
						now(), name(), int(maxseq_));
				prpkt(pkt);
			}
			goto dropafterack;
		}

		/*
		 * If we have a timestamp reply, update smoothed
		 * round trip time.  If no timestamp is present but
		 * transmit timer is running and timed sequence
		 * number was acked, update smoothed round trip time.
		 * Since we now have an rtt measurement, cancel the
		 * timer backoff (cf., Phil Karn's retransmit alg.).
		 * Recompute the initial retransmit timer.
		 *
		 * If all outstanding data is acked, stop retransmit
		 * If there is more data to be acked, restart retransmit
		 * timer, using current (possibly backed-off) value.
		 */
		newack(pkt); // handle timers, update highest_ack_

		/*
		 * if this is a partial ACK, invoke whatever we should
		 * note that newack() must be called before the action
		 * functions, as some of them depend on side-effects
		 * of newack()
		 */

		int partial = pack(pkt);

		if (partial)
			pack_action(pkt);
		else
			ack_action(pkt);

		/*
		 * if this is an ACK with an ECN indication, handle this
		 * but not if it is a syn packet
		 */
		if (fh->ecnecho() && !(tiflags & TH_SYN))
			if (fh->ecnecho())
			{
				// Note from Sally: In one-way TCP,
				// ecn() is called before newack()...
				// std::cout << "calling ecn() function" << std::endl;
				ecn(highest_ack_); // updated by newack(), above
				// "set_rtx_timer();" from T. Kelly.
				if (cwnd_ < 1)
					set_rtx_timer();
			}

		// Mohammad
		/*if (Random::uniform(1) < ecnhat_alpha_ && !(tiflags&TH_SYN) ) {
			ecn(highest_ack_);
			if (cwnd_ < 1)
				set_rtx_timer();
				}*/

		// CHECKME: handling of rtx timer
		if (ackno == maxseq_)
		{
			needoutput = TRUE;
		}

		/*
		 * If no data (only SYN) was ACK'd,
		 *    skip rest of ACK processing.
		 */
		if (ackno == (highest_ack_ + 1))
			goto step6;

		// if we are delaying initial cwnd growth (probably due to
		// large initial windows), then only open cwnd if data has
		// been received
		// Q: check when this happens
		/*
		 * When new data is acked, open the congestion window.
		 * If the window gives us less than ssthresh packets
		 * in flight, open exponentially (maxseg per packet).
		 * Otherwise open about linearly: maxseg per window
		 * (maxseg^2 / cwnd per packet).
		 */
		if ((!delay_growth_ || (rcv_nxt_ > 0)) &&
			last_state_ == TCPS_ESTABLISHED)
		{
			if (!partial || open_cwnd_on_pack_)
			{
				if (!ect_ || !hdr_flags::access(pkt)->ecnecho() || ecn_burst_)
					opencwnd();
			}
		}

		// Mohammad
		if (ect_)
		{
			if (!ecn_burst_ && hdr_flags::access(pkt)->ecnecho())
				ecn_burst_ = TRUE;
			else if (ecn_burst_ && !hdr_flags::access(pkt)->ecnecho())
				ecn_burst_ = FALSE;
		}

		if ((state_ >= TCPS_FIN_WAIT_1) && (ackno == maxseq_))
		{
			ourfinisacked = TRUE;
		}

		//
		// special additional processing when our state
		// is one of the closing states:
		//	FIN_WAIT_1, CLOSING, LAST_ACK

		switch (state_)
		{
			/*
			 * In FIN_WAIT_1 STATE in addition to the processing
			 * for the ESTABLISHED state if our FIN is now acknowledged
			 * then enter FIN_WAIT_2.
			 */
		case TCPS_FIN_WAIT_1: /* doing active close */
			if (ourfinisacked)
			{
				// got the ACK, now await incoming FIN
				newstate(TCPS_FIN_WAIT_2);
				cancel_timers();
				needoutput = FALSE;
			}
			break;

			/*
			 * In CLOSING STATE in addition to the processing for
			 * the ESTABLISHED state if the ACK acknowledges our FIN
			 * then enter the TIME-WAIT state, otherwise ignore
			 * the segment.
			 */
		case TCPS_CLOSING: /* simultaneous active close */;
			if (ourfinisacked)
			{
				newstate(TCPS_CLOSED);
				cancel_timers();
			}
			break;
			/*
			 * In LAST_ACK, we may still be waiting for data to drain
			 * and/or to be acked, as well as for the ack of our FIN.
			 * If our FIN is now acknowledged,
			 * enter the closed state and return.
			 */
		case TCPS_LAST_ACK: /* passive close */
			// K: added state change here
			if (ourfinisacked)
			{
				newstate(TCPS_CLOSED);
				finish(); // cancels timers, erc
				reset();  // for connection re-use (bug fix from ns-users list)
				goto drop;
			}
			else
			{
				// should be a FIN we've seen
				if (debug_)
				{
					fprintf(stderr, "%f: FullTcpAgent(%s)::received non-ACK (state:%d): ",
							now(), name(), state_);
					prpkt(pkt);
				}
			}
			break;

			/* no case for TIME_WAIT in simulator */
		} // inner state_ switch (closing states)
	}	  // outer state_ switch (ack processing)

step6:

	/*
	 * Processing of incoming DATAful segments.
	 * 	Code above has already trimmed redundant data.
	 *
	 * real TCP handles window updates and URG data here also
	 */

	/* dodata: this label is in the "real" code.. here only for reference */

	if ((datalen > 0 || (tiflags & TH_FIN)) &&
		TCPS_HAVERCVDFIN(state_) == 0)
	{

		//
		// the following 'if' implements the "real" TCP
		// TCP_REASS macro
		//

		if (tcph->seqno() == rcv_nxt_ && rq_.empty())
		{
			// got the in-order packet we were looking
			// for, nobody is in the reassembly queue,
			// so this is the common case...
			// note: in "real" TCP we must also be in
			// ESTABLISHED state to come here, because
			// data arriving before ESTABLISHED is
			// queued in the reassembly queue.  Since we
			// don't really have a process anyhow, just
			// accept the data here as-is (i.e. don't
			// require being in ESTABLISHED state)

			/* Mohammad: For DCTCP state machine */
			if (ecnhat_ && ce_transition_ && ((rcv_nxt_ - last_ack_sent_) > 0))
			{
				// Must send an immediate ACK with with previous ECN state
				// before transitioning to new state
				flags_ |= TF_ACKNOW;
				recent_ce_ = !recent_ce_;
				// printf("should be acking %d with recent_ce_ = %d\n", rcv_nxt_, recent_ce_);
				send_much(1, REASON_NORMAL, maxburst_);
				recent_ce_ = !recent_ce_;
			}

			flags_ |= TF_DELACK;
			// Mohammad
			delack_timer_.resched(delack_interval_);
			rcv_nxt_ += datalen;

			// printf("%f: receving data %d, rescheduling delayed ack\n", Scheduler::instance().clock(), rcv_nxt_);

			tiflags = tcph->flags() & TH_FIN;

			// give to "application" here
			// in "real" TCP, this is sbappend() + sorwakeup()
			if (datalen)
			{
				// hack for pfabric app
				if (conctd_to_pfabric_app_)
				{
					RequestIdTuple req_id = RequestIdTuple();
					req_id.app_level_id_ = r2p2_hdr->app_level_id();
					req_id.msg_bytes_ = r2p2_hdr->msg_bytes();
					req_id.is_request_ = (r2p2_hdr->msg_type() == hdr_r2p2::REQUEST);
					req_id.cl_thread_id_ = r2p2_hdr->cl_thread_id();
					req_id.cl_addr_ = r2p2_hdr->cl_addr();
					req_id.sr_addr_ = r2p2_hdr->sr_addr();
					req_id.client_port_ = dport();
					req_id.ts_ = r2p2_hdr->msg_creation_time();
					app_->recv_msg(datalen, std::move(req_id));
				}
				else
				{
					recvBytes(datalen); // notify application of "delivery"
				}

				// printf("flow_remaining before dec = %d\n" , flow_remaining_);
				if (flow_remaining_ > 0)
					flow_remaining_ -= datalen; // Mohammad
				if (flow_remaining_ == 0)
				{
					flags_ |= TF_ACKNOW;
					flow_remaining_ = -1;
				}
				// printf("flow_remaining after dec = %d\n" , flow_remaining_);
			}

			needoutput = need_send();
		}
		else
		{
			// see the "tcp_reass" function:
			// not the one we want next (or it
			// is but there's stuff on the reass queue);
			// do whatever we need to do for out-of-order
			// segments or hole-fills.  Also,
			// send an ACK (or SACK) to the other side right now.
			// Note that we may have just a FIN here (datalen = 0)

			/* Mohammad: the DCTCP receiver conveys the ECN-CE
			   received on each out-of-order data packet */

			int rcv_nxt_old_ = rcv_nxt_; // notify app. if changes
			tiflags = reass(pkt);
			if (rcv_nxt_ > rcv_nxt_old_)
			{
				// if rcv_nxt_ has advanced, must have
				// been a hole fill.  In this case, there
				// is something to give to application
				// hack for pfabric app
				if (conctd_to_pfabric_app_)
				{
					RequestIdTuple req_id = RequestIdTuple();
					req_id.app_level_id_ = r2p2_hdr->app_level_id();
					req_id.msg_bytes_ = r2p2_hdr->msg_bytes();
					req_id.is_request_ = (r2p2_hdr->msg_type() == hdr_r2p2::REQUEST);
					req_id.cl_thread_id_ = r2p2_hdr->cl_thread_id();
					req_id.cl_addr_ = r2p2_hdr->cl_addr();
					req_id.sr_addr_ = r2p2_hdr->sr_addr();
					req_id.client_port_ = dport();
					req_id.ts_ = r2p2_hdr->msg_creation_time();
					app_->recv_msg(rcv_nxt_ - rcv_nxt_old_, std::move(req_id));
				}
				else
				{
					recvBytes(rcv_nxt_ - rcv_nxt_old_); // notify application of "delivery"
				}

				// printf("flow_remaining before dec = %d\n" , flow_remaining_);
				if (flow_remaining_ > 0)
					flow_remaining_ -= datalen; // Mohammad

				if (flow_remaining_ == 0)
				{
					flags_ |= TF_ACKNOW;
					flow_remaining_ = -1;
				}

				// printf("flow_remaining after dec = %d\n" , flow_remaining_);
			}
			flags_ |= TF_ACKNOW;

			if (tiflags & TH_PUSH)
			{
				//
				// ???: does this belong here
				// K: APPLICATION recv
				needoutput = need_send();
			}
		}
	}
	else
	{
		/*
		 * we're closing down or this is a pure ACK that
		 * wasn't handled by the header prediction part above
		 * (e.g. because cwnd < wnd)
		 */
		// K: this is deleted
		tiflags &= ~TH_FIN;
	}

	/*
	 * if FIN is received, ACK the FIN
	 * (let user know if we could do so)
	 */

	if (tiflags & TH_FIN)
	{
		if (TCPS_HAVERCVDFIN(state_) == 0)
		{
			flags_ |= TF_ACKNOW;
			rcv_nxt_++;
		}
		switch (state_)
		{
		/*
		 * In SYN_RECEIVED and ESTABLISHED STATES
		 * enter the CLOSE_WAIT state.
		 * (passive close)
		 */
		case TCPS_SYN_RECEIVED:
		case TCPS_ESTABLISHED:
			newstate(TCPS_CLOSE_WAIT);
			break;

		/*
		 * If still in FIN_WAIT_1 STATE FIN has not been acked so
		 * enter the CLOSING state.
		 * (simultaneous close)
		 */
		case TCPS_FIN_WAIT_1:
			newstate(TCPS_CLOSING);
			break;
		/*
		 * In FIN_WAIT_2 state enter the TIME_WAIT state,
		 * starting the time-wait timer, turning off the other
		 * standard timers.
		 * (in the simulator, just go to CLOSED)
		 * (completion of active close)
		 */
		case TCPS_FIN_WAIT_2:
			newstate(TCPS_CLOSED);
			cancel_timers();
			break;
		}
	} /* end of if FIN bit on */

	if (needoutput || (flags_ & TF_ACKNOW))
		send_much(1, REASON_NORMAL, maxburst_);
	else if (curseq_ >= highest_ack_ || infinite_send_)
		send_much(0, REASON_NORMAL, maxburst_);
	// K: which state to return to when nothing left?

	if (!halfclose_ && state_ == TCPS_CLOSE_WAIT && highest_ack_ == maxseq_)
		usrclosed();

	Packet::free(pkt);

	// haoboy: Is here the place for done{} of active close?
	// It cannot be put in the switch above because we might need to do
	// send_much() (an ACK)
	if (state_ == TCPS_CLOSED)
		Tcl::instance().evalf("%s done", this->name());

	return;

	//
	// various ways of dropping (some also ACK, some also RST)
	//

dropafterack:
	flags_ |= TF_ACKNOW;
	send_much(1, REASON_NORMAL, maxburst_);
	goto drop;

dropwithreset:
	/* we should be sending an RST here, but can't in simulator */
	if (tiflags & TH_ACK)
	{
		sendpacket(ackno, 0, 0x0, 0, REASON_NORMAL);
	}
	else
	{
		int ack = tcph->seqno() + datalen;
		if (tiflags & TH_SYN)
			ack--;
		sendpacket(0, ack, TH_ACK, 0, REASON_NORMAL);
	}
drop:
	Packet::free(pkt);
	return;
}

/*
 * Dupack-action: what to do on a DUP ACK.  After the initial check
 * of 'recover' below, this function implements the following truth
 * table:
 *
 *      bugfix  ecn     last-cwnd == ecn        action
 *
 *      0       0       0                       full_reno_action
 *      0       0       1                       full_reno_action [impossible]
 *      0       1       0                       full_reno_action
 *      0       1       1                       1/2 window, return
 *      1       0       0                       nothing
 *      1       0       1                       nothing         [impossible]
 *      1       1       0                       nothing
 *      1       1       1                       1/2 window, return
 */

void FullTcpAgent::dupack_action()
{

	int recovered = (highest_ack_ > recover_);

	fastrecov_ = TRUE;
	rtxbytes_ = 0;

	if (recovered || (!bug_fix_ && !ecn_) || (last_cwnd_action_ == CWND_ACTION_DUPACK) || (highest_ack_ == 0))
	{
		goto full_reno_action;
	}

	if (ecn_ && last_cwnd_action_ == CWND_ACTION_ECN)
	{
		slowdown(CLOSE_CWND_HALF);
		cancel_rtx_timer();
		rtt_active_ = FALSE;
		(void)fast_retransmit(highest_ack_);
		return;
	}

	if (bug_fix_)
	{
		// The line below, for "bug_fix_" true, avoids
		// problems with multiple fast retransmits in one
		// window of data.
		return;
	}

full_reno_action:
	slowdown(CLOSE_SSTHRESH_HALF | CLOSE_CWND_HALF);
	cancel_rtx_timer();
	rtt_active_ = FALSE;
	recover_ = maxseq_;
	(void)fast_retransmit(highest_ack_);
	// we measure cwnd in packets,
	// so don't scale by maxseg_
	// as real TCP does
	cwnd_ = double(ssthresh_) + double(dupacks_);
	return;
}

void FullTcpAgent::timeout_action()
{
	recover_ = maxseq_;

	//	cwnd_ = 0.5 * cwnd_;
	// Shuang: comment all below
	if (cwnd_ < 1.0)
	{
		if (debug_)
		{
			fprintf(stderr, "%f: FullTcpAgent(%s):: resetting cwnd from %f to 1\n",
					now(), name(), double(cwnd_));
		}
		cwnd_ = 1.0;
	}

	if (last_cwnd_action_ == CWND_ACTION_ECN)
	{
		slowdown(CLOSE_CWND_ONE);
	}
	else
	{
		slowdown(CLOSE_SSTHRESH_HALF | CLOSE_CWND_RESTART);
		last_cwnd_action_ = CWND_ACTION_TIMEOUT;
	}

	// cwnd_ = initial_window();
	//	ssthresh_ = cwnd_;

	reset_rtx_timer(1);
	t_seqno_ = (highest_ack_ < 0) ? iss_ : int(highest_ack_);
	ecnhat_recalc_seq = t_seqno_;
	ecnhat_maxseq = ecnhat_recalc_seq;

	// printf("%f, fid %d took timeout, cwnd_ = %f\n", now(), fid_, (double)cwnd_);
	fastrecov_ = FALSE;
	dupacks_ = 0;
}
/*
 * deal with timers going off.
 * 2 types for now:
 *	retransmission timer (rtx_timer_)
 *  delayed ack timer (delack_timer_)
 *	delayed send (randomization) timer (delsnd_timer_)
 *
 * real TCP initializes the RTO as 6 sec
 *	(A + 2D, where A=0, D=3), [Stevens p. 305]
 * and thereafter uses
 *	(A + 4D, where A and D are dynamic estimates)
 *
 * note that in the simulator t_srtt_, t_rttvar_ and t_rtt_
 * are all measured in 'tcp_tick_'-second units
 */

void FullTcpAgent::timeout(int tno)
{

	/*
	 * Due to F. Hernandez-Campos' fix in recv(), we may send an ACK
	 * while in the CLOSED state.  -M. Weigle 7/24/01
	 */
	if (state_ == TCPS_LISTEN)
	{
		// shouldn't be getting timeouts here
		if (debug_)
		{
			fprintf(stderr, "%f: FullTcpAgent(%s): unexpected timeout %d in state %s\n",
					now(), name(), tno, statestr(state_));
		}
		return;
	}

	switch (tno)
	{

	case TCP_TIMER_RTX:
		/* retransmit timer */
		++nrexmit_;
		timeout_action();
		/* fall thru */
	case TCP_TIMER_DELSND:
		/* for phase effects */
		send_much(1, PF_TIMEOUT, maxburst_);
		break;

	case TCP_TIMER_DELACK:
		if (flags_ & TF_DELACK)
		{
			flags_ &= ~TF_DELACK;
			flags_ |= TF_ACKNOW;
			send_much(1, REASON_NORMAL, 0);
		}
		// Mohammad
		// delack_timer_.resched(delack_interval_);
		break;
	default:
		fprintf(stderr, "%f: FullTcpAgent(%s) Unknown Timeout type %d\n",
				now(), name(), tno);
	}
	return;
}

void FullTcpAgent::dooptions(Packet *pkt)
{
	// interesting options: timestamps (here),
	//	CC, CCNEW, CCECHO (future work perhaps?)

	hdr_flags *fh = hdr_flags::access(pkt);
	hdr_tcp *tcph = hdr_tcp::access(pkt);

	if (ts_option_ && !fh->no_ts_)
	{
		if (tcph->ts() < 0.0)
		{
			fprintf(stderr,
					"%f: FullTcpAgent(%s) warning: ts_option enabled in this TCP, but appears to be disabled in peer\n",
					now(), name());
		}
		else if (tcph->flags() & TH_SYN)
		{
			flags_ |= TF_RCVD_TSTMP;
			recent_ = tcph->ts();
			recent_age_ = now();
		}
	}

	return;
}

//
// this shouldn't ever happen
//
void FullTcpAgent::process_sack(hdr_tcp *)
{
	fprintf(stderr, "%f: FullTcpAgent(%s) Non-SACK capable FullTcpAgent received a SACK\n",
			now(), name());
	return;
}

int FullTcpAgent::byterm()
{
	return curseq_ - int(highest_ack_) - window() * maxseg_;
}

/*
 * ****** Tahoe ******
 *
 * for TCP Tahoe, we force a slow-start as the dup ack
 * action.  Also, no window inflation due to multiple dup
 * acks.  The latter is arranged by setting reno_fastrecov_
 * false [which is performed by the Tcl init function for Tahoe in
 * ns-default.tcl].
 */

/*
 * Tahoe
 * Dupack-action: what to do on a DUP ACK.  After the initial check
 * of 'recover' below, this function implements the following truth
 * table:
 *
 *      bugfix  ecn     last-cwnd == ecn        action
 *
 *      0       0       0                       full_tahoe_action
 *      0       0       1                       full_tahoe_action [impossible]
 *      0       1       0                       full_tahoe_action
 *      0       1       1                       1/2 window, return
 *      1       0       0                       nothing
 *      1       0       1                       nothing         [impossible]
 *      1       1       0                       nothing
 *      1       1       1                       1/2 window, return
 */

void TahoeFullTcpAgent::dupack_action()
{
	int recovered = (highest_ack_ > recover_);

	fastrecov_ = TRUE;
	rtxbytes_ = 0;

	if (recovered || (!bug_fix_ && !ecn_) || highest_ack_ == 0)
	{
		goto full_tahoe_action;
	}

	if (ecn_ && last_cwnd_action_ == CWND_ACTION_ECN)
	{
		// slow start on ECN
		last_cwnd_action_ = CWND_ACTION_DUPACK;
		slowdown(CLOSE_CWND_ONE);
		set_rtx_timer();
		rtt_active_ = FALSE;
		t_seqno_ = highest_ack_;
		return;
	}

	if (bug_fix_)
	{
		/*
		 * The line below, for "bug_fix_" true, avoids
		 * problems with multiple fast retransmits in one
		 * window of data.
		 */
		return;
	}

full_tahoe_action:
	// slow-start and reset ssthresh
	trace_event("FAST_RETX");
	recover_ = maxseq_;
	last_cwnd_action_ = CWND_ACTION_DUPACK;
	slowdown(CLOSE_SSTHRESH_HALF | CLOSE_CWND_ONE); // cwnd->1
	set_rtx_timer();
	rtt_active_ = FALSE;
	t_seqno_ = highest_ack_;
	send_much(0, REASON_NORMAL, 0);
	return;
}

/*
 * ****** Newreno ******
 *
 * for NewReno, a partial ACK does not exit fast recovery,
 * and does not reset the dup ACK counter (which might trigger fast
 * retransmits we don't want).  In addition, the number of packets
 * sent in response to an ACK is limited to recov_maxburst_ during
 * recovery periods.
 */

NewRenoFullTcpAgent::NewRenoFullTcpAgent() : save_maxburst_(-1)
{
	bind("recov_maxburst_", &recov_maxburst_);
}

void NewRenoFullTcpAgent::pack_action(Packet *)
{
	(void)fast_retransmit(highest_ack_);
	cwnd_ = double(ssthresh_);
	if (save_maxburst_ < 0)
	{
		save_maxburst_ = maxburst_;
		maxburst_ = recov_maxburst_;
	}
	return;
}

void NewRenoFullTcpAgent::ack_action(Packet *p)
{
	if (save_maxburst_ >= 0)
	{
		maxburst_ = save_maxburst_;
		save_maxburst_ = -1;
	}
	FullTcpAgent::ack_action(p);
	return;
}

/*
 *
 * ****** SACK ******
 *
 * for Sack, receiver part must report SACK data
 * sender part maintains a 'scoreboard' (sq_) that
 * records what it hears from receiver
 * sender fills holes during recovery and obeys
 * "pipe" style control until recovery is complete
 */

int SackFullTcpAgent::set_prio(int seq, int maxseq)
{
	int max = 100 * 1460;
	int prio;
	if (prio_scheme_ == 0)
	{
		if (seq - startseq_ > max)
			prio = max;
		else
			prio = seq - startseq_;
	}
	if (prio_scheme_ == 1)
		prio = maxseq - startseq_;
	if (prio_scheme_ == 2)
	{
		// printf("%d %d\n", maxseq, int(highest_ack_));
		// printf("%d %d %d %d\n", maxseq, int(highest_ack_), sq_.total(), maxseq - int(highest_ack_) - sq_.total() + 10);
		// fflush(stdout);
		if (maxseq - int(highest_ack_) - sq_.total() + 10 < 0)
			prio = 0;
		else
			prio = maxseq - int(highest_ack_) - sq_.total() + 10;
		// return maxseq - seq;
	}
	if (prio_scheme_ == 3)
	{
		// printf("3??\n");
		prio = seq - startseq_;
	}
	if (prio_scheme_ == 4)
	{ // in batch
		if (int(highest_ack_) >= seq_bound_)
		{
			seq_bound_ = maxseq_;
			if (maxseq - int(highest_ack_) - sq_.total() + 10 < 0)
				last_prio_ = 0;
			else
				last_prio_ = maxseq - int(highest_ack_) - sq_.total() + 10;
		}
		// printf("prio scheme 4: highest ack %d maxseq_ %d seq %d prio %d\n", int(highest_ack_), int(maxseq_), seq, last_prio_);
		prio = last_prio_;
	}

	if (prio_num_ == 0)
		return prio;
	else
		return calPrio(prio);
}

void SackFullTcpAgent::reset()
{
	sq_.clear(); // no SACK blocks
	/* Fixed typo.  -M. Weigle 6/17/02 */
	sack_min_ = h_seqno_ = -1; // no left edge of SACK blocks
	FullTcpAgent::reset();
}

int SackFullTcpAgent::hdrsize(int nsackblocks)
{
	int total = FullTcpAgent::headersize();
	// use base header size plus SACK option size
	if (nsackblocks > 0)
	{
		total += ((nsackblocks * sack_block_size_) + sack_option_size_);
	}
	return (total);
}

void SackFullTcpAgent::dupack_action()
{

	int recovered = (highest_ack_ > recover_);

	fastrecov_ = TRUE;
	rtxbytes_ = 0;
	pipe_ = maxseq_ - highest_ack_ - sq_.total();

	// printf("%f: SACK DUPACK-ACTION:pipe_:%d, sq-total:%d, bugfix:%d, cwnd:%d, highest_ack:%d, recover_:%d\n",
	// now(), pipe_, sq_.total(), bug_fix_, int(cwnd_), int(highest_ack_), recover_);

	if (recovered || (!bug_fix_ && !ecn_))
	{
		goto full_sack_action;
	}

	if (ecn_ && last_cwnd_action_ == CWND_ACTION_ECN)
	{
		/*
		 * Received ECN notification and 3 DUPACKs in same
		 * window. Don't cut cwnd again, but retransmit lost
		 * packet.   -M. Weigle  6/19/02
		 */
		last_cwnd_action_ = CWND_ACTION_DUPACK;
		/* Mohammad: cut window by half when we have 3 dup ack */
		if (ecnhat_)
			slowdown(CLOSE_SSTHRESH_HALF | CLOSE_CWND_HALF);
		cancel_rtx_timer();
		rtt_active_ = FALSE;
		int amt = fast_retransmit(highest_ack_);
		pipectrl_ = TRUE;
		h_seqno_ = highest_ack_ + amt;
		send_much(0, REASON_DUPACK, maxburst_);
		return;
	}

	if (bug_fix_)
	{
		/*
		 * The line below, for "bug_fix_" true, avoids
		 * problems with multiple fast retransmits in one
		 * window of data.
		 */

		// printf("%f: SACK DUPACK-ACTION BUGFIX RETURN:pipe_:%d, sq-total:%d, bugfix:%d, cwnd:%d\n",
		// now(), pipe_, sq_.total(), bug_fix_, int(cwnd_));
		return;
	}

full_sack_action:
	trace_event("FAST_RECOVERY");
	slowdown(CLOSE_SSTHRESH_HALF | CLOSE_CWND_HALF);
	cancel_rtx_timer();
	rtt_active_ = FALSE;

	// these initiate SACK-style "pipe" recovery
	pipectrl_ = TRUE;
	recover_ = maxseq_; // where I am when recovery starts

	int amt = fast_retransmit(highest_ack_);
	h_seqno_ = highest_ack_ + amt;

	// printf("%f: FAST-RTX seq:%d, h_seqno_ is now:%d, pipe:%d, cwnd:%d, recover:%d\n",
	// now(), int(highest_ack_), h_seqno_, pipe_, int(cwnd_), recover_);

	send_much(0, REASON_DUPACK, maxburst_);

	return;
}

void SackFullTcpAgent::pack_action(Packet *p)
{
	if (!sq_.empty() && sack_min_ < highest_ack_)
	{
		sack_min_ = highest_ack_;
		sq_.cleartonxt();
	}
	pipe_ -= maxseg_; // see comment in tcp-sack1.cc
	if (h_seqno_ < highest_ack_)
		h_seqno_ = highest_ack_;
}

void SackFullTcpAgent::ack_action(Packet *p)
{
	// printf("%f: EXITING fast recovery, recover:%d\n",
	// now(), recover_);

	// Shuang: not set pipectrol_ = false
	fastrecov_ = pipectrl_ = FALSE;
	fastrecov_ = FALSE;
	if (!sq_.empty() && sack_min_ < highest_ack_)
	{
		sack_min_ = highest_ack_;
		sq_.cleartonxt();
	}
	dupacks_ = 0;

	/*
	 * Update h_seqno_ on new ACK (same as for partial ACKS)
	 * -M. Weigle 6/3/05
	 */
	if (h_seqno_ < highest_ack_)
		h_seqno_ = highest_ack_;
}

//
// receiver side: if there are things in the reassembly queue,
// build the appropriate SACK blocks to carry in the SACK
//
int SackFullTcpAgent::build_options(hdr_tcp *tcph)
{
	int total = FullTcpAgent::build_options(tcph);

	if (!rq_.empty())
	{
		int nblk = rq_.gensack(&tcph->sa_left(0), max_sack_blocks_);
		tcph->sa_length() = nblk;
		total += (nblk * sack_block_size_) + sack_option_size_;
	}
	else
	{
		tcph->sa_length() = 0;
	}
	// Shuang: reduce ack size
	// return 0;
	return (total);
}

void SackFullTcpAgent::timeout_action()
{
	FullTcpAgent::timeout_action();

	/*recover_ = maxseq_;

	int progress = curseq_ - int(highest_ack_) - sq_.total();
	cwnd_ = min((last_timeout_progress_ - progress) / 1460 + 1, maxcwnd_);
	ssthresh_ = cwnd_;
	printf("%d %d", progress/1460, last_timeout_progress_ / 1460);
	last_timeout_progress_ = progress;

	reset_rtx_timer(1);
	t_seqno_ = (highest_ack_ < 0) ? iss_ : int(highest_ack_);
	ecnhat_recalc_seq = t_seqno_;
	ecnhat_maxseq = ecnhat_recalc_seq;

	printf("%f, fid %d took timeout, cwnd_ = %f\n", now(), fid_, (double)cwnd_);
	fastrecov_ = FALSE;
	dupacks_ = 0;*/

	//
	// original SACK spec says the sender is
	// supposed to clear out its knowledge of what
	// the receiver has in the case of a timeout
	// (on the chance the receiver has renig'd).
	// Here, this happens when clear_on_timeout_ is
	// enabled.
	//

	if (clear_on_timeout_)
	{
		sq_.clear();
		sack_min_ = highest_ack_;
	}

	return;
}

void SackFullTcpAgent::process_sack(hdr_tcp *tcph)
{
	//
	// Figure out how many sack blocks are
	// in the pkt.  Insert each block range
	// into the scoreboard
	//
	last_sqtotal_ = sq_.total();

	if (max_sack_blocks_ <= 0)
	{
		fprintf(stderr,
				"%f: FullTcpAgent(%s) warning: received SACK block but I am not SACK enabled\n",
				now(), name());
		return;
	}

	int slen = tcph->sa_length(), i;
	for (i = 0; i < slen; ++i)
	{
		/* Added check for FIN   -M. Weigle 5/21/02 */
		if ((tcph->flags() & TH_FIN == 0) &&
			tcph->sa_left(i) >= tcph->sa_right(i))
		{
			fprintf(stderr,
					"%f: FullTcpAgent(%s) warning: received illegal SACK block [%d,%d]\n",
					now(), name(), tcph->sa_left(i), tcph->sa_right(i));
			continue;
		}
		sq_.add(tcph->sa_left(i), tcph->sa_right(i), 0);
	}

	cur_sqtotal_ = sq_.total();
	return;
}

int SackFullTcpAgent::send_allowed(int seq)
{
	// Shuang: always pipe control and simple pipe function
	// pipectrl_ = true;
	// pipe_ = maxseq_ - highest_ack_ - sq_.total();

	// not in pipe control, so use regular control
	if (!pipectrl_)
		return (FullTcpAgent::send_allowed(seq));

	// don't overshoot receiver's advertised window
	int topawin = highest_ack_ + int(wnd_) * maxseg_;
	//	printf("%f: PIPECTRL: SEND(%d) AWIN:%d, pipe:%d, cwnd:%d highest_ack:%d sqtotal:%d\n",
	// now(), seq, topawin, pipe_, int(cwnd_), int(highest_ack_), sq_.total());

	if (seq >= topawin)
	{
		return FALSE;
	}

	/*
	 * If not in ESTABLISHED, don't send anything we don't have
	 *   -M. Weigle 7/18/02
	 */
	if (state_ != TCPS_ESTABLISHED && seq > curseq_)
		return FALSE;

	// don't overshoot cwnd_
	int cwin = int(cwnd_) * maxseg_;
	return (pipe_ < cwin);
}

//
// Calculate the next seq# to send by send_much.  If we are recovering and
// we have learned about data cached at the receiver via a SACK,
// we may want something other than new data (t_seqno)
//

int SackFullTcpAgent::nxt_tseq()
{

	int in_recovery = (highest_ack_ < recover_);
	int seq = h_seqno_;

	if (!in_recovery)
	{
		// if (int(t_seqno_) > 1)
		// printf("%f: non-recovery nxt_tseq called w/t_seqno:%d\n",
		// now(), int(t_seqno_));
		// sq_.dumplist();
		return (t_seqno_);
	}

	int fcnt;	// following count-- the
				// count field in the block
				// after the seq# we are about
				// to send
	int fbytes; // fcnt in bytes

	// if (int(t_seqno_) > 1)
	// printf("%f: recovery nxt_tseq called w/t_seqno:%d, seq:%d, mode:%d\n",
	// now(), int(t_seqno_), seq, sack_rtx_threshmode_);
	// sq_.dumplist();

	while ((seq = sq_.nexthole(seq, fcnt, fbytes)) > 0)
	{
		// if we have a following block
		// with a large enough count
		// we should use the seq# we get
		// from nexthole()
		if (sack_rtx_threshmode_ == 0 ||
			(sack_rtx_threshmode_ == 1 && fcnt >= sack_rtx_cthresh_) ||
			(sack_rtx_threshmode_ == 2 && fbytes >= sack_rtx_bthresh_) ||
			(sack_rtx_threshmode_ == 3 && (fcnt >= sack_rtx_cthresh_ || fbytes >= sack_rtx_bthresh_)) ||
			(sack_rtx_threshmode_ == 4 && (fcnt >= sack_rtx_cthresh_ && fbytes >= sack_rtx_bthresh_)))
		{

			// if (int(t_seqno_) > 1)
			// printf("%f: nxt_tseq<hole> returning %d\n",
			// now(), int(seq));
			//  adjust h_seqno, as we may have
			//  been "jumped ahead" by learning
			//  about a filled hole
			if (seq > h_seqno_)
				h_seqno_ = seq;
			return (seq);
		}
		else if (fcnt <= 0)
			break;
		else
		{
			// Shuang; probe
			if (prob_cap_ != 0)
			{
				seq++;
			}
			else
				seq += maxseg_;
		}
	}
	// if (int(t_seqno_) > 1)
	// printf("%f: nxt_tseq<top> returning %d\n",
	// now(), int(t_seqno_));
	return (t_seqno_);
}

int SackFullTcpAgent::byterm()
{
	return curseq_ - int(highest_ack_) - sq_.total() - window() * maxseg_;
}

MinTcpAgent::MinTcpAgent()
{
	bind("conctd_to_pfabric_app_", &conctd_to_pfabric_app_);
}

void MinTcpAgent::timeout_action()
{
	// Shuang: prob count when cwnd=1
	//  std::cout << "MinTcpAgent: at " << Scheduler::instance().clock() << " TIMEOUT" << std::endl;
	if (prob_cap_ != 0)
	{
		prob_count_++;
		if (prob_count_ == prob_cap_)
		{
			prob_mode_ = true;
		}
		// Shuang: h_seqno_?
		h_seqno_ = highest_ack_;
	}

	SackFullTcpAgent::timeout_action();
}

double
MinTcpAgent::rtt_timeout()
{
	return minrto_;
}

// void
// MinTcpAgent::advance_bytes(int nb)
//	SackFullTcpAgent::advance_bytes();
// }

void DDTcpAgent::slowdown(int how)
{

	double decrease; /* added for highspeed - sylvia */
	double win, halfwin, decreasewin;
	int slowstart = 0;
	++ncwndcuts_;
	if (!(how & TCP_IDLE) && !(how & NO_OUTSTANDING_DATA))
	{
		++ncwndcuts1_;
	}

	// Shuang: deadline-aware
	double penalty = ecnhat_alpha_;
	if (deadline != 0)
	{
		double tleft = deadline / 1e6 - (now() - start_time);

		// if (tleft < 0 && now() < 3) {
		//	cwnd_ = 1;
		//	printf("early termination now %.8lf start %.8lf deadline %d\n", now(), start_time, deadline);
		//	fflush(stdout);
		//	if (signal_on_empty_);
		//		bufferempty();
		//		return;
		// } else
		if (tleft < 0)
		{
			tleft = 1e10;
		}
		double rtt = int(t_srtt_ >> T_SRTT_BITS) * tcp_tick_;
		double Tc = byterm() / (0.75 * cwnd_ * maxseg_) * rtt;
		double d = Tc / tleft;
		if (d > 2)
			d = 2;
		if (d < 0.5)
			d = 0.5;
		if (d >= 0)
			penalty = pow(penalty, d);
		// printf("deadline left %.6lf d-factor %f Tc %f start %f rm %d cwnd %f\n", tleft, Tc/tleft, Tc, start_time, byterm(), double(cwnd_));
		// fflush(stdout);
	}
	else if (penalty > 0)
	{
		// non-deadline->TCP
		penalty = 1;
	}

	// ecnhat_alpha_ = 0.07;
	//  we are in slowstart for sure if cwnd < ssthresh
	if (cwnd_ < ssthresh_)
	{
		std::cout << "DCTCP is in SLOW START. cwnd = " << cwnd_ << "ssthresh_ = " << ssthresh_ << std::endl;
		slowstart = 1;
		assert(0);
	}
	if (precision_reduce_)
	{
		halfwin = windowd() / 2;
		if (wnd_option_ == 6)
		{
			/* binomial controls */
			decreasewin = windowd() - (1.0 - decrease_num_) * pow(windowd(), l_parameter_);
		}
		else if (wnd_option_ == 8 && (cwnd_ > low_window_))
		{
			/* experimental highspeed TCP */
			decrease = decrease_param();
			// if (decrease < 0.1)
			//	decrease = 0.1;
			decrease_num_ = decrease;
			decreasewin = windowd() - (decrease * windowd());
		}
		else
		{
			decreasewin = decrease_num_ * windowd();
		}
		win = windowd();
		// printf("decrease param = %f window = %f decwin = %f\n", decrease_num_, win, decreasewin);
	}
	else
	{
		int temp;
		temp = (int)(window() / 2);
		halfwin = (double)temp;
		if (wnd_option_ == 6)
		{
			/* binomial controls */
			temp = (int)(window() - (1.0 - decrease_num_) * pow(window(), l_parameter_));
		}
		else if ((wnd_option_ == 8) && (cwnd_ > low_window_))
		{
			/* experimental highspeed TCP */
			decrease = decrease_param();
			// if (decrease < 0.1)
			//        decrease = 0.1;
			decrease_num_ = decrease;
			temp = (int)(windowd() - (decrease * windowd()));
		}
		else
		{
			temp = (int)(decrease_num_ * window());
		}
		decreasewin = (double)temp;
		win = (double)window();
	}
	if (how & CLOSE_SSTHRESH_HALF)
		// For the first decrease, decrease by half
		// even for non-standard values of decrease_num_.
		if (first_decrease_ == 1 || slowstart ||
			last_cwnd_action_ == CWND_ACTION_TIMEOUT)
		{
			// Do we really want halfwin instead of decreasewin
			// after a timeout?
			ssthresh_ = (int)halfwin;
		}
		else
		{
			ssthresh_ = (int)decreasewin;
		}
	else if (how & CLOSE_SSTHRESH_ECNHAT)
		ssthresh_ = (int)((1 - penalty / 2.0) * windowd());
	// ssthresh_ = (int) (windowd() - sqrt(2*windowd())/2.0);
	else if (how & THREE_QUARTER_SSTHRESH)
		if (ssthresh_ < 3 * cwnd_ / 4)
			ssthresh_ = (int)(3 * cwnd_ / 4);
	if (how & CLOSE_CWND_HALF)
		// For the first decrease, decrease by half
		// even for non-standard values of decrease_num_.
		if (first_decrease_ == 1 || slowstart || decrease_num_ == 0.5)
		{
			cwnd_ = halfwin;
		}
		else
			cwnd_ = decreasewin;
	else if (how & CLOSE_CWND_ECNHAT)
	{
		cwnd_ = (1 - penalty / 2.0) * windowd();
		if (cwnd_ < 1)
			cwnd_ = 1;
	}
	// cwnd_ = windowd() - sqrt(2*windowd())/2.0;
	else if (how & CWND_HALF_WITH_MIN)
	{
		// We have not thought about how non-standard TCPs, with
		// non-standard values of decrease_num_, should respond
		// after quiescent periods.
		cwnd_ = decreasewin;
		if (cwnd_ < 1)
			cwnd_ = 1;
	}
	else if (how & CLOSE_CWND_RESTART)
		cwnd_ = int(wnd_restart_);
	else if (how & CLOSE_CWND_INIT)
		cwnd_ = int(wnd_init_);
	else if (how & CLOSE_CWND_ONE)
		cwnd_ = 1;
	else if (how & CLOSE_CWND_HALF_WAY)
	{
		// cwnd_ = win - (win - W_used)/2 ;
		cwnd_ = W_used + decrease_num_ * (win - W_used);
		if (cwnd_ < 1)
			cwnd_ = 1;
	}
	if (ssthresh_ < 2)
		ssthresh_ = 2;
	if (cwnd_ < 1)
		cwnd_ = 1; // Added by Mohammad
	if (how & (CLOSE_CWND_HALF | CLOSE_CWND_RESTART | CLOSE_CWND_INIT | CLOSE_CWND_ONE | CLOSE_CWND_ECNHAT))
		cong_action_ = TRUE;

	fcnt_ = count_ = 0;
	if (first_decrease_ == 1)
		first_decrease_ = 0;
	// for event tracing slow start
	if (cwnd_ == 1 || slowstart)
		// Not sure if this is best way to capture slow_start
		// This is probably tracing a superset of slowdowns of
		// which all may not be slow_start's --Padma, 07/'01.
		trace_event("SLOW_START");
}

int DDTcpAgent::byterm()
{
	return curseq_ - int(highest_ack_) - sq_.total();
}

int DDTcpAgent::foutput(int seqno, int reason)
{
	if (deadline != 0)
	{
		// 		double tleft = double(deadline)/1e6 - (now() - start_time) - byterm()*8/1e10;
		double tleft = deadline / 1e6 - (now() - start_time) - (curseq_ - int(maxseq_)) * 8 / 1e10;
		if (tleft < 0 && signal_on_empty_)
		{
			early_terminated_ = 1;
			bufferempty();
			printf("early termination V2 now %.8lf start %.8lf deadline %d byterm %d tleft %.8f\n", now(), start_time, deadline, curseq_ - int(maxseq_), tleft);
			fflush(stdout);
			return 0;
		}
		else if (tleft < 0)
		{
			return 0;
		}
		// printf("test foutput\n");
	}
	return SackFullTcpAgent::foutput(seqno, reason);
}

int DDTcpAgent::need_send()
{
	if (deadline != 0)
	{
		double tleft1 = deadline / 1e6 - (now() - start_time);
		if (tleft1 < 0)
			return 0;
		// printf("test need send\n");
	}
	return SackFullTcpAgent::need_send();
}
