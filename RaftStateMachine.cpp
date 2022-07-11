//
// Created by projector on 7/9/22.
//
#include "FuseFS.h"
#include <braft/util.h>
#include "brpc/server.h"
#include "RaftStateMachine.h"
#include "butil/sys_byteorder.h"
#include "Utils.h"

DEFINE_int32(port, 8100, "Listen port of this peer");
DEFINE_string(conf, "", "Initial configuration of the replication group");
DEFINE_int32(election_timeout_ms, 5000,
             "Start election in such milliseconds if disconnect with the leader");
DEFINE_int32(snapshot_interval, -1, "Interval between each snapshot");
DEFINE_string(data_path, "./raft_data", "Path of data stored on, use abs path");
DEFINE_string(group, "fuse", "Id of the replication group");

int RaftStateMachine::start() {
    butil::EndPoint addr(butil::my_ip(), FLAGS_port);
    braft::NodeOptions node_options;
    if (FLAGS_conf.empty()) {
        FLAGS_conf = string(butil::ip2str(addr.ip).c_str()) + ":" + to_string(FLAGS_port)+":0";
    }
    if (node_options.initial_conf.parse_from(FLAGS_conf) != 0) {
        LOG(ERROR) << LVAR(__FUNCTION__) << "Fail to parse configuration `" << FLAGS_conf << '\'';
        return -1;
    }
    node_options.election_timeout_ms = FLAGS_election_timeout_ms;
    node_options.fsm = this;
    node_options.node_owns_fsm = false;
    node_options.snapshot_interval_s = FLAGS_snapshot_interval;
    std::string prefix = "local://" + FLAGS_data_path;
    node_options.log_uri = prefix + "/log";
    node_options.raft_meta_uri = prefix + "/raft_meta";
    node_options.snapshot_uri = prefix + "/snapshot";
    node_options.disable_cli = false;
    braft::Node* node = new braft::Node(FLAGS_group, braft::PeerId(addr));
    if (node->init(node_options) != 0) {
        LOG(ERROR) << LVAR(__FUNCTION__) << "Fail to init raft node";
        delete node;
        return -1;
    }
    _node = node;
    return 0;
}
void RaftStateMachine::on_apply(braft::Iterator &iter) {
    for (; iter.valid(); iter.next()) {
        // This guard helps invoke iter.done()->Run() asynchronously to
        // avoid that callback blocks the StateMachine.
        braft::AsyncClosureGuard done_guard(iter.done());

        LOG(INFO) << LVAR(__FUNCTION__) << LVAR(iter.term()) << LVAR(iter.index()) ;
        // Have to parse BlockRequest from this log.
        uint32_t meta_size = 0;
        butil::IOBuf saved_log = iter.data();
        saved_log.cutn(&meta_size, sizeof(uint32_t));
        // Remember that meta_size is in network order which hould be
        // covert to host order
        meta_size = butil::NetToHost32(meta_size);
        butil::IOBuf meta;
        saved_log.cutn(&meta, meta_size);
        butil::IOBufAsZeroCopyInputStream wrapper(meta);
        RaftLog::LogData logData;
        CHECK(logData.ParseFromZeroCopyStream(&wrapper));

        RaftClosure *raftClosure = NULL;
        struct fuse_file_info *pFI = NULL;
        if (iter.done()) {
            raftClosure = dynamic_cast<RaftClosure *>(iter.done());
            pFI = raftClosure ->getFi();
        } else {
            pFI = NULL;
        }

        LOG(INFO) << LVAR(__FUNCTION__) << LVAR(logData.func_name());
        switch(logData.op_type()) {
            case RaftLog::OP_TYPE_WRITE: {
                CallBackLock callBackLock(raftClosure == NULL? 2:1);
                int ret = ((FuseFS*)pFuseFS)->write(logData.pathname(), logData.buf().c_str(), logData.count(),
                        logData.offset(), pFI);
                if (raftClosure) {
                    raftClosure->setNRetCode(ret);
                }
                if (ret < 0) {
                    LOG(ERROR) <<LVAR(__FUNCTION__)<< " callback err"  << LVAR(ret) ;
                    iter.set_error_and_rollback();
                    return;
                }
            }
                break;
            case RaftLog::OP_TYPE_OPEN: {
                CallBackLock callBackLock(raftClosure == NULL? 2:1);
                struct fuse_file_info tFI;
                tFI.flags = logData.flags();
                int ret = ((FuseFS*)pFuseFS)->open(logData.pathname(), pFI!=NULL ? pFI:&tFI);
                if (raftClosure) {
                    raftClosure->setNRetCode(ret);
                }
                if (ret < 0) {
                    LOG(ERROR) <<LVAR(__FUNCTION__)<< " callback err"  << LVAR(ret) ;
                    iter.set_error_and_rollback();
                    return;
                }
            }
                break;
            case RaftLog::OP_TYPE_UNLINK:{
                CallBackLock callBackLock(raftClosure == NULL? 2:1);
                int ret = ((FuseFS*)pFuseFS)->unlink(logData.pathname());
                if (raftClosure) {
                    raftClosure->setNRetCode(ret);
                }
                if (ret < 0) {
                    LOG(ERROR) <<LVAR(__FUNCTION__)<< " callback err"  << LVAR(ret) ;
                    iter.set_error_and_rollback();
                    return;
                }
            }
                break;
            case RaftLog::OP_TYPE_CREATE: {
                CallBackLock callBackLock(raftClosure == NULL? 2:1);
                struct fuse_file_info tFI;
                tFI.flags = logData.flags();
                int ret = ((FuseFS*)pFuseFS)->create(logData.pathname(), logData.mode(), pFI!=NULL ? pFI:&tFI);
                if (raftClosure) {
                    raftClosure->setNRetCode(ret);
                }
                if (ret < 0) {
                    LOG(ERROR) <<LVAR(__FUNCTION__)<< " callback err"  << LVAR(ret) ;
                    iter.set_error_and_rollback();
                    return;
                }
            }
                break;
            default :
            {
                LOG(ERROR) <<LVAR(__FUNCTION__)<< " unknown op_type "  << LVAR(logData.op_type()) ;
                //todo:set_error_and_rollback之后raft会退出，这里还需要考虑如何优化
                iter.set_error_and_rollback();
                return;
            }
        }
    }
}

int RaftStateMachine::apply(RaftLog::LogData &logData, struct fuse_file_info *fi) {
    LOG(INFO) << LVAR(__FUNCTION__) <<LVAR(logData.func_name());
    // Serialize request to IOBuf
    const int64_t term = _leader_term.load(butil::memory_order_relaxed);
    if (term < 0) {
        return -99;
    }
    butil::IOBuf log;
    const uint32_t meta_size_raw = butil::HostToNet32(logData.ByteSize());
    log.append(&meta_size_raw, sizeof(uint32_t));
    butil::IOBufAsZeroCopyOutputStream wrapper(&log);
    if (!logData.SerializeToZeroCopyStream(&wrapper)) {
        LOG(ERROR) << "Fail to serialize request";
        return -100;
    }

    int nRetCode=0;
    braft::SynchronizedClosure synchronizedClosure;
    braft::Task task;
    task.data = &log;
    task.done = new RaftClosure(this, &synchronizedClosure, &nRetCode, fi);
    if (true) {
        // ABA problem can be avoid if expected_term is set
        task.expected_term = term;
    }
    // Now the task is applied to the group, waiting for the result.
    _node->apply(task);
    synchronizedClosure.wait();

    return nRetCode;
}

void RaftStateMachine::on_leader_start(int64_t term) {
    _leader_term.store(term, butil::memory_order_release);
    LOG(INFO) << LVAR(__FUNCTION__) << "Node becomes leader";
}

void RaftStateMachine::on_leader_stop(const butil::Status &status) {
    _leader_term.store(-1, butil::memory_order_release);
    LOG(INFO) << LVAR(__FUNCTION__) << "Node stepped down : " << status;
}


void RaftClosure::Run() {
    std::unique_ptr<RaftClosure> self_guard(this);

    if (status().error_code() == 0) {
        LOG(INFO) << LVAR(__FUNCTION__) << LVAR(status().error_str()) << LVAR(status().error_code());
    } else {
        LOG(ERROR) << LVAR(__FUNCTION__) << LVAR(status().error_str()) << LVAR(status().error_code());
        *nRetCode = status().error_code()>0? -status().error_code():status().error_code();
    }
    synchronizedClosure->Run();
}
