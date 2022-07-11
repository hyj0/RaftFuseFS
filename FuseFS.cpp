//
// Created by projector on 6/29/22.
//
#include <unistd.h>
#include <cstring>
#include "FuseFS.h"
#include "raft_log.pb.h"
#include "Utils.h"

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
    LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;
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
        return 0;
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
    //LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;

    res = ::mkdir(fixPath(pathname).c_str(), mode);
    if (res == -1)
        return -errno;

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
    //LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;

    res = ::rmdir(fixPath(pathname).c_str());
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::utimens(const string &pathname, const struct timespec tv[2]) {
    int res;
    //LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;

    res = utimensat(0, fixPath(pathname).c_str(), tv, AT_SYMLINK_NOFOLLOW);
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::truncate(const string &path, off_t length) {
    int res;
    //LOG(INFO) << LVAR(__FUNCTION__) << LVAR(pathname) << LVAR(fixPath(pathname)) ;

    res = ::truncate(fixPath(path).c_str(), length);
    if (res == -1)
        return -errno;

    return 0;
}

int FuseFS::release(const string &pathname, struct fuse_file_info *fi) {
    close(fi->fh);
}

