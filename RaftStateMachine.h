//
// Created by projector on 7/9/22.
//

#ifndef RAFTFUSEFS_RAFTSTATEMACHINE_H
#define RAFTFUSEFS_RAFTSTATEMACHINE_H


#include <braft/raft.h>
#include "Utils.h"
#include "raft_log.pb.h"

DECLARE_int32(port);

class RaftStateMachine : public braft::StateMachine {
public:
    RaftStateMachine(){}

    void setPFuseFs(void *pFuseFs) {
        pFuseFS = pFuseFs;
    }

    int start();
    int apply(RaftLog::LogData &logData, struct fuse_file_info *fi);

    virtual void on_apply(braft::Iterator &iter);

    virtual void on_leader_start(int64_t term);

    virtual void on_leader_stop(const butil::Status &status);

private:
    int nNode;// 1--30
    string strGroupId;
    string strConfig;
    int nRaftPort;

    braft::Node* volatile _node;
    butil::atomic<int64_t> _leader_term;
    int64_t hasAppliedIndex = 0; //已经回放的位置，用于跳过已经回放的日志
    void *pFuseFS;
};


class RaftClosure : public braft::Closure {
public:
    RaftClosure(RaftStateMachine *raftStateMachine, braft::SynchronizedClosure *synchronizedClosure, int *nRetCode,
                struct fuse_file_info *fi)
            : raftStateMachine(raftStateMachine)
            ,synchronizedClosure(synchronizedClosure)
            ,nRetCode(nRetCode)
            ,fi(fi)
    {}

    void Run();

    fuse_file_info *getFi() const {
        return fi;
    }

    void setNRetCode(int nRetCode) {
        *(this->nRetCode) = nRetCode;
    }

private:
    RaftStateMachine* raftStateMachine;
    braft::SynchronizedClosure *synchronizedClosure; //同步等待的closure，如果不为NULL，说明是同步等结果
    int *nRetCode;
    struct fuse_file_info *fi;
};
#endif //RAFTFUSEFS_RAFTSTATEMACHINE_H
