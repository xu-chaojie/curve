/*
 *  Copyright (c) 2021 NetEase Inc.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS,
 *  WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 *  See the License for the specific language governing permissions and
 *  limitations under the License.
 */


/*
 * Project: curve
 * Created Date: Thur May 27 2021
 * Author: xuchaojie
 */

#include <string>
#include <list>

#include "curvefs/src/client/fuse_volume_client.h"

namespace curvefs {
namespace client {


CURVEFS_ERROR FuseVolumeClient::Init(const FuseClientOption &option) {
    CURVEFS_ERROR ret = FuseClient::Init(option);
    if (ret != CURVEFS_ERROR::OK) {
        return ret;
    }
    ret = blockDeviceClient_->Init(option.bdevOpt);
    return ret;
}

void FuseVolumeClient::UnInit() {
    FuseClient::UnInit();
    blockDeviceClient_->UnInit();
}

void FuseVolumeClient::init(void *userdata, struct fuse_conn_info *conn) {
    struct MountOption *mOpts = (struct MountOption *) userdata;
    std::string mountPointStr =
        (mOpts->mountPoint == nullptr) ? "" : mOpts->mountPoint;
    std::string volName = (mOpts->volume == nullptr) ? "" : mOpts->volume;
    std::string fsName = (mOpts->fsName == nullptr) ? volName : mOpts->fsName;
    std::string user = (mOpts->user == nullptr) ? "" : mOpts->user;

    FsInfo fsInfo;
    CURVEFS_ERROR ret = mdsClient_->GetFsInfo(fsName, &fsInfo);
    if (ret != CURVEFS_ERROR::OK) {
        if (CURVEFS_ERROR::NOTEXIST == ret) {
            LOG(INFO) << "The fsName not exist, try to CreateFs"
                      << ", fsName = " << fsName;
            BlockDeviceStat stat;
            ret = blockDeviceClient_->Stat(volName, user, &stat);
            if (ret != CURVEFS_ERROR::OK) {
                LOG(ERROR) << "Stat volume failed, ret = " << ret
                           << ", volName = " << volName
                           << ", user = " << user;
                return;
            }

            Volume vol;
            vol.set_volumesize(stat.length);
            // TODO(xuchaojie) : where to get block size?
            vol.set_blocksize(4096);
            vol.set_volumename(volName);
            vol.set_user(user);

            ret = mdsClient_->CreateFs(fsName, 4096, vol);

            if (ret != CURVEFS_ERROR::OK) {
                LOG(ERROR) << "CreateFs failed, ret = " << ret
                           << ", fsName = " << fsName;
                return;
            }
        } else {
            LOG(ERROR) << "GetFsInfo failed, ret = " << ret
                       << ", fsName = " << fsName;
            return;
        }
    }

    ret = blockDeviceClient_->Open(volName, user);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "BlockDeviceClientImpl open failed, ret = " << ret
                   << ", volName = " << volName
                   << ", user = " << user;
        return;
    }

    MountPoint mp;
    GetMointPoint(mountPointStr, &mp);
    ret = mdsClient_->MountFs(fsName, mp, &fsInfo);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "MountFs failed, ret = " << ret
                   << ", fsName = " << fsName
                   << ", mountPoint = " << mountPointStr;
        return;
    }
    fsInfo_ = std::make_shared<FsInfo>(fsInfo);
    inodeManager_->Init(fsInfo.fsid());
    dentryManager_->Init(fsInfo.fsid());

    LOG(INFO) << "Mount " << fsName
              << " on " << mountPointStr
              << " success!";
    return;
}

void FuseVolumeClient::destroy(void *userdata) {
    struct MountOption *mOpts = (struct MountOption *) userdata;
    std::string fsName = (mOpts->fsName == nullptr) ? "" : mOpts->fsName;
    std::string mountPointStr =
        (mOpts->mountPoint == nullptr) ? "" : mOpts->mountPoint;
    MountPoint mp;
    GetMointPoint(mountPointStr, &mp);
    CURVEFS_ERROR ret = mdsClient_->UmountFs(fsInfo_->fsname(),
        mp);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "UmountFs failed, ret = " << ret
                   << ", fsName = " << fsName
                   << ", mountPoint = " << mountPointStr;
        return;
    }
    ret = blockDeviceClient_->Close();
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "BlockDeviceClientImpl close failed, ret = " << ret;
        return;
    }
    LOG(INFO) << "Umount " << fsName
              << " on " << mountPointStr
              << " success!";
    return;
}

const uint64_t kBigFileSize = 1024 * 1024u;

CURVEFS_ERROR FuseVolumeClient::write(fuse_req_t req, fuse_ino_t ino,
    const char *buf, size_t size, off_t off,
    struct fuse_file_info *fi, size_t *wSize) {
    Inode inode;
    CURVEFS_ERROR ret = inodeManager_->GetInode(ino, &inode);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "inodeManager get inode fail, ret = " << ret
                  << ", inodeid = " << ino;
        return ret;
    }
    std::list<ExtentAllocInfo> toAllocExtents;
    ret = extManager_->GetToAllocExtents(inode.volumeextentlist(),
        off, size, &toAllocExtents);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "GetToAllocExtents fail, ret = " << ret;
        return ret;
    }
    if (toAllocExtents.size() != 0) {
        AllocateType type = AllocateType::NONE;
        if (inode.length() >=kBigFileSize || size >=kBigFileSize) {
            type = AllocateType::BIG;
        } else {
            type = AllocateType::SMALL;
        }
        std::list<Extent> allocatedExtents;
        ret = spaceClient_->AllocExtents(
            fsInfo_->fsid(), toAllocExtents, type, &allocatedExtents);
        if (ret != CURVEFS_ERROR::OK) {
            LOG(ERROR) << "metaClient alloc extents fail, ret = " << ret;
            return ret;
        }
        ret = extManager_->MergeAllocedExtents(
            toAllocExtents,
            allocatedExtents,
            inode.mutable_volumeextentlist());
        if (ret != CURVEFS_ERROR::OK) {
            LOG(ERROR) << "toAllocExtents and allocatedExtents not match, "
                       << "ret = " << ret;
            CURVEFS_ERROR ret2 = spaceClient_->DeAllocExtents(
                fsInfo_->fsid(), allocatedExtents);
            if (ret2 != CURVEFS_ERROR::OK) {
                LOG(ERROR) << "DeAllocExtents fail, ret = " << ret;
            }
            return ret;
        }
    }

    std::list<PExtent> pExtents;
    ret = extManager_->DivideExtents(inode.volumeextentlist(),
        off, size,
        &pExtents);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "DivideExtents fail, ret = " << ret;
        return ret;
    }

    uint64_t writeLen = 0;
    for (const auto &ext : pExtents) {
        ret = blockDeviceClient_->Write(buf + writeLen,
            ext.pOffset, ext.len);
        writeLen += ext.len;
        if (ret != CURVEFS_ERROR::OK) {
            LOG(ERROR) << "block device write fail, ret = " << ret;
            return ret;
        }
    }

    ret = extManager_->MarkExtentsWritten(off, size,
        inode.mutable_volumeextentlist());
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "MarkExtentsWritten fail, ret =  " << ret;
        return ret;
    }
    *wSize = size;
    // update file len
    if (inode.length() < off + size) {
        inode.set_length(off + size);
    }

    LOG(INFO) << "UpdateInode inode = " << inode.DebugString();
    ret = inodeManager_->UpdateInode(inode);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "UpdateInode fail, ret = " << ret;
        return ret;
    }
    return ret;
}

CURVEFS_ERROR FuseVolumeClient::read(fuse_req_t req,
        fuse_ino_t ino, size_t size, off_t off,
        struct fuse_file_info *fi,
        char **buffer,
        size_t *rSize) {
    Inode inode;
    CURVEFS_ERROR ret = inodeManager_->GetInode(ino, &inode);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "inodeManager get inode fail, ret = " << ret
                  << ", inodeid = " << ino;
        return ret;
    }
    size_t len = 0;
    if (inode.length() < off + size) {
        len = inode.length() - off;
    } else {
        len = size;
    }
    *buffer = reinterpret_cast<char*>(malloc(len * sizeof(char)));
    memset(*buffer, 0, len);
    std::list<PExtent> pExtents;
    ret = extManager_->DivideExtents(inode.volumeextentlist(),
        off, len, &pExtents);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "DivideExtents fail, ret = " << ret;
        return ret;
    }
    uint64_t readLen = 0;
    for (const auto &ext : pExtents) {
        if (!ext.UnWritten) {
            ret = blockDeviceClient_->Read(*buffer + readLen,
                ext.pOffset, ext.len);
            readLen += ext.len;
            if (ret != CURVEFS_ERROR::OK) {
                LOG(ERROR) << "block device read fail, ret = " << ret;
                return ret;
            }
        }
    }
    *rSize = len;

    LOG(INFO) << "UpdateInode inode = " << inode.DebugString();
    ret = inodeManager_->UpdateInode(inode);
    if (ret != CURVEFS_ERROR::OK) {
        LOG(ERROR) << "UpdateInode fail, ret = " << ret;
        return ret;
    }
    LOG(INFO) << "read end, read size = " << *rSize;
    return ret;
}

CURVEFS_ERROR FuseVolumeClient::create(fuse_req_t req, fuse_ino_t parent,
    const char *name, mode_t mode, struct fuse_file_info *fi,
    fuse_entry_param *e) {
    return MakeNode(req, parent, name, mode, FsFileType::TYPE_FILE, e);
}

CURVEFS_ERROR FuseVolumeClient::mknod(fuse_req_t req, fuse_ino_t parent,
        const char *name, mode_t mode, dev_t rdev,
        fuse_entry_param *e) {
    return MakeNode(req, parent, name, mode, FsFileType::TYPE_FILE, e);
}

}  // namespace client
}  // namespace curvefs
