//
// Created by projector on 6/28/22.
//

#include <butil/at_exit.h>
#include "Server.h"
#include "FuseFS.h"
#include "RaftStateMachine.h"
#include "brpc/server.h"
#include <butil/logging.h>
#include "butil/string_splitter.h"

DEFINE_string(fuse_args, "", "fuse args");

void ParseFuseArgs(int *argc, char *** argv)
{
    char **pArgv = *argv;
    static char *args[100] = {};
    string fuse_args(string(pArgv[0])+ ' ' + FLAGS_fuse_args);
    LOG(INFO) << LVAR(fuse_args);
    butil::StringMultiSplitter splitter(fuse_args.c_str(), " ");
    *argc = 0;
    while (splitter.operator const void *()) {
        int nLen = splitter.length();
        char *p = new char[nLen+1];
        memset(p, 0, nLen+1);
        strncpy(p, splitter.field(), nLen);
        LOG(INFO) << LVAR(splitter.field()) << LVAR(splitter.length());
        args[*argc] = p;
        (*argc) ++ ;
        ++splitter;
    }
    *argv = args;
}
/*
mkdir -p  /tmp/FuseSrc/  /tmp/FuseTarget
sudo umount /tmp/FuseTarget
sudo ./cmake-build-debug/Server    -fuse_args='-d -o uid=1003 -o gid=1003 -o allow_other /tmp/FuseTarget'
ls /tmp/FuseTarget/
 vim /tmp/FuseTarget/test.txt

 */

int main(int argc, char ** argv)
{
    gflags::ParseCommandLineFlags(&argc, &argv, false);
    butil::AtExitManager exit_manager;

    LOG(INFO) << LVAR(FLAGS_fuse_args);
    ParseFuseArgs(&argc, &argv);
    for (int i = 0; i < argc; i++) {
        LOG(INFO) << LVAR(argv[i]);
    }

    brpc::Server server;
    RaftStateMachine  raftStateMachine;

    if (braft::add_service(&server, FLAGS_port) != 0) {
        LOG(ERROR) << "Fail to add raft service";
        return -1;
    }
    if (server.Start(FLAGS_port, NULL) != 0) {
        LOG(ERROR) << "Fail to start Server";
        return -1;
    }

    int ret = raftStateMachine.start();
    if (ret != 0) {
        LOG(ERROR) << LVAR(ret);
        return ret;
    }

    FuseFS fuseFs(raftStateMachine);
    fuseFs.main(argc, argv);
    LOG(INFO) << "end";
}
