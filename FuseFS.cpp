//
// Created by projector on 6/29/22.
//
#include <unistd.h>
#include <cstring>
#include <sys/xattr.h>
extern "C" {
#include <ulockmgr.h>
}
#include <sys/file.h>

#include "FuseFS.h"
#include "raft_log.pb.h"
#include "Utils.h"
using namespace RaftLog;

DEFINE_string(src_dir, "/tmp/FuseSrc/", "src dir");

//base code from https://github.com/libfuse/libfuse/blob/master/example/passthrough_fh.c

int FuseFS::getattr(const std::string &pathname, struct stat *buf) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname));

    res = lstat(fixPath(pathname).c_str(), buf);
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::opendir(const string &name, struct fuse_file_info *fi) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(name) << LVAR(fixPath(name)) ;

    struct xmp_dirp *d = static_cast<xmp_dirp *>(malloc(sizeof(struct xmp_dirp)));
    if (d == NULL)
        return -ENOMEM;

    d->dp = ::opendir(fixPath(name).c_str());
    if (d->dp == NULL) {
        res = -errno;
        free(d);
        return res;
    }
    d->offset = 0;
    d->entry = NULL;

    fi->fh = (unsigned long) d;
    return 0;
}

int FuseFS::readdir(const std::string &pathname, off_t off, struct fuse_file_info *fi, fuse::readdir_flags flags) {
    struct xmp_dirp *d = (struct xmp_dirp *) (uintptr_t) fi->fh;
    //LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;

    off_t offset = off;
    if (offset != d->offset) {
#ifndef __FreeBSD__
        seekdir(d->dp, offset);
#else
        /* Subtract the one that we add when calling
		   telldir() below */
		seekdir(d->dp, offset-1);
#endif
        d->entry = NULL;
        d->offset = offset;
    }
    while (1) {
        struct stat st;
        off_t nextoff;
        enum fuse_fill_dir_flags fill_flags = static_cast<fuse_fill_dir_flags>(0);

        if (!d->entry) {
            d->entry = ::readdir(d->dp);
            if (!d->entry)
                break;
        }
#ifdef HAVE_FSTATAT
        if (flags & FUSE_READDIR_PLUS) {
			int res;

			res = fstatat(dirfd(d->dp), d->entry->d_name, &st,
				      AT_SYMLINK_NOFOLLOW);
			if (res != -1)
				fill_flags |= FUSE_FILL_DIR_PLUS;
		}
#endif
        if (!(fill_flags & FUSE_FILL_DIR_PLUS)) {
            memset(&st, 0, sizeof(st));
            st.st_ino = d->entry->d_ino;
            st.st_mode = d->entry->d_type << 12;
        }
        nextoff = telldir(d->dp);
#ifdef __FreeBSD__
        /* Under FreeBSD, telldir() may return 0 the first time
		   it is called. But for libfuse, an offset of zero
		   means that offsets are not supported, so we shift
		   everything by one. */
		nextoff++;
#endif
		if (fill_dir(d->entry->d_name, &st)) {
            break;
		}

        d->entry = NULL;
        d->offset = nextoff;
    }

    return 0;
}

int FuseFS::releasedir(const string &pathname, struct fuse_file_info *fi) {
    struct xmp_dirp *d = (struct xmp_dirp *) (uintptr_t) fi->fh;
    //LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;

    closedir(d->dp);
    free(d);
    return 0;
}

int FuseFS::open(const std::string &pathname, struct fuse_file_info *fi) {
    int fd;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) << LVAR(fi->flags) ;

    //read only
    int flgWrite = (fi->flags & O_WRONLY) || (fi->flags & O_RDWR) || (fi->flags & O_CREAT)
            || (fi->flags & O_APPEND) || (fi->flags & O_TRUNC) || (fi -> flags & O_EXCL);
    if (!IsCallBack() && !flgWrite) {
        fd = ::open(fixPath(pathname).c_str(), fi->flags);
        if (fd == -1)
            return -errno;

        fi->fh = fd;
        return 0;
    }
    if (!IsCallBack()) {
        RaftLog::LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_OPEN);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_flags(fi->flags);
        int ret = raftStateMachine.apply(logData, fi);
        if (ret < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "open err" << LVAR(pathname);
            return ret;
        }
        return ret;
    } else {
        fd = ::open(fixPath(pathname).c_str(), fi->flags);
        if (fd == -1)
            return -errno;

        fi->fh = fd;

        if (IsFollowerCallBack()) {
            close(fd);
        }
    }
    
    return 0;
}

int FuseFS::read(const std::string &pathname, char *buf, size_t count, off_t offset, struct fuse_file_info *fi) {
    int res;
    //LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;
    res = pread(fi->fh, buf, count, offset);
    if (res == -1)
        res = -errno;

    return res;
}

int FuseFS::write(const std::string &pathname, const char *buf, size_t count, off_t offset, struct fuse_file_info *fi) {
    int res;
    if (!IsCallBack()) {
        LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname));
        RaftLog::LogData logData;
        logData.set_func_name(__FUNCTION__ );
        logData.set_op_type(RaftLog::OP_TYPE_WRITE);
        logData.set_pathname(pathname);
        logData.set_buf(buf, count);
        logData.set_count(count);
        logData.set_offset(offset);
        res = raftStateMachine.apply(logData, fi);
        if (res < 0) {
            LOG(ERROR) << "app err " << LVAR(res) << LVAR(__FUNCTION__) << LVAR(pathname)  ;
            return res;
        }
        return res;
    } else {
        LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname));
        int fd;
        if(fi == NULL) {
            fd = ::open(fixPath(pathname).c_str(), O_WRONLY|O_CREAT, 0644);
            //todo:delay close(fd)
        } else {
            fd = fi->fh;
        }
        res = pwrite(fd, buf, count, offset);
        if (IsFollowerCallBack()) {
            close(fd);
        }
        if (res == -1)
            res = -errno;
        //todo:
        if (res != count) {
            return -ENOSYS;
        }
        return res;
    }

    return res;
}

int FuseFS::create(const std::string &pathname, mode_t mode, struct fuse_file_info *fi) {
    int fd;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;
    if (!IsCallBack()) {
        RaftLog::LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_CREATE);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_flags(fi->flags);
        logData.set_mode(mode);
        fd = raftStateMachine.apply(logData, fi);
        if (fd < 0) {
            LOG(ERROR) << "apply err " << LVAR(fd) << LVAR(__FUNCTION__) << LVAR(pathname)  ;
            return fd;
        }
        return 0;
    } else {
        fd = ::open(fixPath(pathname).c_str(), fi->flags, mode);
        if (fd == -1)
            return -errno;

        fi->fh = fd;
        if (IsFollowerCallBack()) {
            close(fd);
        }
        return 0;
    }
    return 0;
}

int FuseFS::mkdir(const string &pathname, mode_t mode) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;
    if (!IsCallBack()) {
        RaftLog::LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_MKDIR);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_mode(mode);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(ERROR) << "apply err " << LVAR(res) << LVAR(__FUNCTION__) << LVAR(pathname)  ;
            return res;
        }
        return res;
    } else {
        res = ::mkdir(fixPath(pathname).c_str(), mode);
        if (res == -1)
            return -errno;
        return res;
    }

    return 0;
}

int FuseFS::unlink(const string &pathname) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;
    if (!IsCallBack()) {
        RaftLog::LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_UNLINK);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;
    } else {
        res = ::unlink(fixPath(pathname).c_str());
        if (res == -1)
            return -errno;
        return 0;
    }

    return 0;
}

int FuseFS::rmdir(const string &pathname) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;
    if (!IsCallBack()) {
        RaftLog::LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_RMDIR);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;
    } else {
        res = ::rmdir(fixPath(pathname).c_str());
        if (res == -1)
            return -errno;
        return res;
    }

    return 0;
}

int FuseFS::utimens(const string &pathname, const struct timespec tv[2]) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;
    if (!IsCallBack()) {
        RaftLog::LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_UTIMENS);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_tv(tv, sizeof(struct timespec)*2);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;
    } else {
        res = utimensat(0, fixPath(pathname).c_str(), tv, AT_SYMLINK_NOFOLLOW);
        if (res == -1)
            return -errno;
        return res;
    }

    return 0;
}

int FuseFS::truncate(const string &pathname, off_t length) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;
    if (!IsCallBack()) {
        RaftLog::LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_TRUNCATE);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_length(length);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;
    } else {
        res = ::truncate(fixPath(pathname).c_str(), length);
        if (res == -1)
            return -errno;
        return res;
    }

    return 0;
}

int FuseFS::release(const string &pathname, struct fuse_file_info *fi) {
    close(fi->fh);
}

int FuseFS::readlink(const string &pathname, char *buffer, size_t size) {
    int res;

    res = ::readlink(fixPath(pathname).c_str(), buffer, size - 1);
    if (res == -1)
        return -errno;

    buffer[res] = '\0';
    return 0;
}

int FuseFS::mknod(const string &pathname, mode_t mode, dev_t dev) {
    LOG(ERROR) << "no implement" << LVAR(__FUNCTION__);
    return fuse::mknod(pathname, mode, dev);
}

int FuseFS::symlink(const string &from, const string &to) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(from) << LVAR(to);
    if (!IsCallBack()) {
        RaftLog::LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_SYMLINK);
        logData.set_func_name(__FUNCTION__);
        logData.set_from(from);
        logData.set_to(to);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(from) << LVAR(to);
            return res;
        }
        return res;
    } else {
        res = ::symlink(from.c_str(), fixPath(to).c_str());
        if (res == -1)
            return -errno;
        return 0;
    }
    return 0;
}

int FuseFS::rename(const string &from, const string &to, unsigned int flags) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(from) << LVAR(to);

    /* When we have renameat2() in libc, then we can implement flags */
    if (flags)
        return -EINVAL;

    if (!IsCallBack()) {
        LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_RENAME);
        logData.set_func_name(__FUNCTION__);
        logData.set_from(from);
        logData.set_to(to);
        logData.set_flags(flags);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(from) << LVAR(to);
            return res;
        }
        return res;
    } else {
        res = ::rename(fixPath(from).c_str(), fixPath(to).c_str());
        if (res == -1)
            return -errno;
        return res;
    }

    return 0;
}

int FuseFS::link(const string &from, const string &to) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(from) << LVAR(to);
    if (!IsCallBack()) {
        LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_LINK);
        logData.set_func_name(__FUNCTION__);
        logData.set_from(from);
        logData.set_to(to);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(from) << LVAR(to);
            return res;
        }
        return res;
    } else {
        res = ::link(fixPath(from).c_str(), fixPath(to).c_str());
        if (res == -1)
            return -errno;
        return 0;
    }

    return 0;
}

int FuseFS::chmod(const string &pathname, mode_t mode) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname);

    if (!IsCallBack()) {
        LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_CHMODE);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_mode(mode);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;
    } else {
        res = ::chmod(fixPath(pathname).c_str(), mode);
        if (res == -1)
            return -errno;
        return 0;
    }

    return 0;
}

int FuseFS::chown(const string &pathname, uid_t uid, gid_t gid) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname);
    if (!IsCallBack()) {
        LogData logData;
        logData.set_op_type(RaftLog::OP_TYPE_CHOWN);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_uid(uid);
        logData.set_gid(gid);
        res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;

    } else {
        res = ::lchown(fixPath(pathname).c_str(), uid, gid);
        if (res == -1)
            return -errno;
        return 0;
    }
    return 0;
}

int FuseFS::statfs(const string &pathname, struct statvfs *buf) {
    int res;
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname);
    res = statvfs(fixPath(pathname).c_str(), buf);
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::flush(const string &pathname, struct fuse_file_info *fi) {
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname);
    int res;

    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname);
    /* This is called from every close on an open file, so call the
       close on the underlying filesystem.	But since flush may be
       called multiple times for an open file, this must not really
       close the file.  This is important if used on a network
       filesystem like NFS which flush the data/metadata on close() */
    res = close(dup(fi->fh));
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::fsync(const string &pathname, int datasync, struct fuse_file_info *fi) {
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname);
    int res;

#ifndef HAVE_FDATASYNC

#else
    if (datasync)
		res = ::fdatasync(fi->fh);
	else
#endif
    res = ::fsync(fi->fh);
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::setxattr(const string &pathname, const string &name, const string &value, size_t size, int flags) {
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname);
    if (!IsCallBack()) {
        LogData logData;
        logData.set_op_type(OP_TYPE_SETXATTR);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_name(name);
        logData.set_value(value);
        logData.set_size(size);
        logData.set_flags(flags);
        int res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;
    } else {
        int res = ::lsetxattr(fixPath(pathname).c_str(), name.c_str(), value.c_str(), size, flags);
        if (res == -1)
            return -errno;
        return 0;
    }
    return 0;
}

int FuseFS::getxattr(const string &path, const string &name, char *value, size_t size) {
    int res = ::lgetxattr(fixPath(path).c_str(), name.c_str(), value, size);
    if (res == -1)
        return -errno;
    return res;
}

int FuseFS::listxattr(const string &path, char *list, size_t size) {
    int res = ::llistxattr(fixPath(path).c_str(), list, size);
    if (res == -1)
        return -errno;
    return res;
}

int FuseFS::removexattr(const string &pathname, const string &name) {
    if (!IsCallBack()) {
        LogData logData;
        logData.set_op_type(OP_TYPE_REMOVEATTR);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_name(name);
        int res = raftStateMachine.apply(logData, NULL);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;
    } else {
        int res = ::lremovexattr(fixPath(pathname).c_str(), name.c_str());
        if (res == -1)
            return -errno;
        return 0;
    }
    return 0;
}

int FuseFS::fsyncdir(const string &pathname, int datasync, struct fuse_file_info *fi) {
    LOG(ERROR) << "no implement" << LVAR(__FUNCTION__);
    return fuse::fsyncdir(pathname, datasync, fi);
}

void FuseFS::init() {
    fuse::init();
}

void FuseFS::destroy() {
    fuse::destroy();
}

int FuseFS::access(const string &pathname, int mode) {
    int res;

    res = ::access(fixPath(pathname).c_str(), mode);
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::lock(const string &pathname, struct fuse_file_info *fi, int cmd, struct flock *lock) {
//    LOG(ERROR) << "no implement" << LVAR(__FUNCTION__);
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(cmd) <<LVAR(fi->lock_owner);
#if 1
    int res = fcntl(fi->fh, cmd, lock);
    if (res == -1) {
        return -errno;
    }
    return 0;
//    fuse_fs_lock(fd, pathname.c_str(), fi, cmd, lock);
//    return ulockmgr_op(fi->fh, cmd, lock, &fi->lock_owner,
//                       sizeof(fi->lock_owner));
#else
    return fuse::lock(pathname, fi, cmd, lock);
#endif
}

int FuseFS::bmap(const string &pathname, size_t blocksize, uint64_t *idx) {
    LOG(ERROR) << "no implement" << LVAR(__FUNCTION__);
    return fuse::bmap(pathname, blocksize, idx);
}

int
FuseFS::ioctl(const string &pathname, int cmd, void *arg, struct fuse_file_info *fi, unsigned int flags, void *data) {
    LOG(ERROR) << "no implement" << LVAR(__FUNCTION__);
    return fuse::ioctl(pathname, cmd, arg, fi, flags, data);
}

int
FuseFS::poll(const string &pathname, struct fuse_file_info *fi, struct fuse_pollhandle *ph, unsigned int *reventsp) {
    LOG(ERROR) << "no implement" << LVAR(__FUNCTION__);
    return fuse::poll(pathname, fi, ph, reventsp);
}

int FuseFS::write_buf(const string &pathname, struct fuse_bufvec *buf, off_t off, struct fuse_file_info *fi) {
    return fuse::write_buf(pathname, buf, off, fi);
}

int
FuseFS::read_buf(const string &pathname, struct fuse_bufvec **bufp, size_t size, off_t off, struct fuse_file_info *fi) {
    return fuse::read_buf(pathname, bufp, size, off, fi);
}

int FuseFS::flock(const string &pathname, struct fuse_file_info *fi, int op) {
    int res;

    res = ::flock(fi->fh, op);
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::fallocate(const string &pathname, int mode, off_t offset, off_t length, struct fuse_file_info *fi) {

    if (mode)
        return -EOPNOTSUPP;
    if (!IsCallBack()) {
        LogData logData;
        logData.set_op_type(OP_TYPE_FALLOCATE);
        logData.set_func_name(__FUNCTION__);
        logData.set_pathname(pathname);
        logData.set_mode(mode);
        logData.set_offset(offset);
        logData.set_length(length);
        int res = raftStateMachine.apply(logData, fi);
        if (res < 0) {
            LOG(INFO) << LVAR(__FUNCTION__) << "err " << LVAR(res) << LVAR(pathname);
            return res;
        }
        return res;
    } else {
        if (IsLeaderCallBack()) {
            return -posix_fallocate(fi->fh, offset, length);
        } else if (IsFollowerCallBack()) {
            int fd = ::open(fixPath(pathname).c_str(), O_WRONLY);
            if (fd < 0) {
                LOG(INFO) << LVAR(__FUNCTION__) << "open err " << LVAR(fd) << LVAR(pathname);
                return fd;
            }
            int ret =  -posix_fallocate(fd, offset, length);
            close(fd);
            return ret;
        }
    }
}

