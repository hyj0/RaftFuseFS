//
// Created by projector on 6/29/22.
//

#ifndef RAFTFUSEFS_FUSEFS_H
#define RAFTFUSEFS_FUSEFS_H

#define FUSE_USE_VERSION 31
#define  _FILE_OFFSET_BITS 64

#include <iostream>
#include <sstream>
using namespace std;
#define LVAR(var)   " " << #var << "="<< var
#define LOG_COUT cout << __FILE__ << ":" << __LINE__ <<  " at " << __FUNCTION__ << " "
#define LOG_ENDL_ERR      LVAR(errno) << " err=" << strerror(errno) << endl
#define LOG_ENDL " " << endl;

#include <dirent.h>
#include <fuse3/fuse.h>
#include "fuse++"
#include <string>
#include <braft/util.h>
#include "RaftStateMachine.h"

using namespace std;

struct xmp_dirp {
    DIR *dp;
    struct dirent *entry;
    off_t offset;
};

class FuseFS : public fuse {
public:
    FuseFS(RaftStateMachine &raftStateMachine) : raftStateMachine(raftStateMachine) {
        raftStateMachine.setPFuseFs(this);
    }
    ~FuseFS() {}

public:
    virtual int getattr(const std::string &pathname, struct stat *buf);

    virtual int opendir(const string &name, struct fuse_file_info *fi);

    virtual int readdir(const std::string &pathname, off_t off, struct fuse_file_info *fi, readdir_flags flags);

    virtual int releasedir(const string &pathname, struct fuse_file_info *fi);

    virtual int mkdir(const string &pathname, mode_t mode);

    virtual int rmdir(const string &pathname);

    virtual int open(const std::string &pathname, struct fuse_file_info *fi);

    virtual int read(const std::string &pathname, char *buf, size_t count, off_t offset, struct fuse_file_info *fi);

    virtual int
    write(const std::string &pathname, const char *buf, size_t count, off_t offset, struct fuse_file_info *fi);

    virtual int create(const std::string &pathname, mode_t mode, struct fuse_file_info *fi);

    virtual int unlink(const string &pathname);

    virtual int utimens(const string &pathname, const struct timespec tv[2]);

    virtual int truncate(const string &path, off_t length);

protected:
    virtual int release(const string &pathname, struct fuse_file_info *fi);

private:
    string fixPath(string path) {
        string retStr = "";
        retStr = retStr + "/tmp/FuseSrc/" + path;
        return retStr;
    }
    RaftStateMachine &raftStateMachine;
};


#endif //RAFTFUSEFS_FUSEFS_H
