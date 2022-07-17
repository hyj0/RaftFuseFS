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

    int64_t getHasAppliedIndex() const {
        return hasAppliedIndex;
    }

    //���ò��־û�hasAppliedIndex
    void setHasAppliedIndex(int64_t hasAppliedIndex);

    int loadHasAppliedIndex();

private:
    braft::Node* volatile _node;
    butil::atomic<int64_t> _leader_term;
    int64_t hasAppliedIndex = 0; //�Ѿ��طŵ�λ�ã����������Ѿ��طŵ���־
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
    braft::SynchronizedClosure *synchronizedClosure; //ͬ���ȴ���closure�������ΪNULL��˵����ͬ���Ƚ��
    int *nRetCode;
    struct fuse_file_info *fi;
};
#endif //RAFTFUSEFS_RAFTSTATEMACHINE_H
