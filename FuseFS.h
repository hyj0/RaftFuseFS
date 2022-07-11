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
#include "gflags/gflags.h"

DECLARE_string(src_dir);

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

    virtual int readlink(const string &pathname, char *buffer, size_t size);

    virtual int mknod(const string &pathname, mode_t mode, dev_t dev);

    virtual int symlink(const string &target, const string &linkpath);

    virtual int rename(const string &oldpath, const string &newpath, unsigned int flags);

    virtual int link(const string &oldpath, const string &newpath);

    virtual int chmod(const string &pathname, mode_t mode);

    virtual int chown(const string &pathname, uid_t uid, gid_t gid);

    virtual int statfs(const string &path, struct statvfs *buf);

    virtual int flush(const string &pathname, struct fuse_file_info *fi);

    virtual int fsync(const string &pathname, int datasync, struct fuse_file_info *fi);

    virtual int setxattr(const string &path, const string &name, const string &value, size_t size, int flags);

    virtual int getxattr(const string &path, const string &name, char *value, size_t size);

    virtual int listxattr(const string &path, char *list, size_t size);

    virtual int removexattr(const string &path, const string &name);

    virtual int fsyncdir(const string &pathname, int datasync, struct fuse_file_info *fi);

    virtual void init();

    virtual void destroy();

    virtual int access(const string &pathname, int mode);

    virtual int lock(const string &pathname, struct fuse_file_info *fi, int cmd, struct flock *lock);

    virtual int bmap(const string &pathname, size_t blocksize, uint64_t *idx);

    virtual int
    ioctl(const string &pathname, int cmd, void *arg, struct fuse_file_info *fi, unsigned int flags, void *data);

    virtual int
    poll(const string &pathname, struct fuse_file_info *fi, struct fuse_pollhandle *ph, unsigned int *reventsp);

    virtual int write_buf(const string &pathname, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *fi);

    virtual int
    read_buf(const string &pathname, struct fuse_bufvec **bufp, size_t size, off_t off, struct fuse_file_info *fi);

    virtual int flock(const string &pathname, struct fuse_file_info *fi, int op);

    virtual int fallocate(const string &pathname, int mode, off_t offset, off_t len, struct fuse_file_info *fi);

protected:
    virtual int release(const string &pathname, struct fuse_file_info *fi);

private:
    string fixPath(string path) {
        string retStr = "";
        retStr = retStr + "/" + FLAGS_src_dir + "/" + path;
        return retStr;
    }
    RaftStateMachine &raftStateMachine;
};


#endif //RAFTFUSEFS_FUSEFS_H
