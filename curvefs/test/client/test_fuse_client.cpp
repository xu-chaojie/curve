/*
 *  Copyright (c) 2020 NetEase Inc.
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

#include <gtest/gtest.h>
#include <gmock/gmock.h>

#include "curvefs/src/client/fuse_volume_client.h"
#include "curvefs/src/client/fuse_s3_client.h"
#include "curvefs/test/client/mock_mds_client.h"
#include "curvefs/test/client/mock_metaserver_client.h"
#include "curvefs/test/client/mock_block_device_client.h"
#include "curvefs/test/client/mock_extent_manager.h"
#include "curvefs/test/client/mock_space_client.h"
#include "curvefs/test/client/mock_inode_cache_manager.h"
#include "curvefs/test/client/mock_dentry_cache_mamager.h"
#include "curvefs/test/client/mock_client_s3_adaptor.h"

namespace curvefs {
namespace client {

using ::testing::Return;
using ::testing::_;
using ::testing::Contains;
using ::testing::SetArgPointee;
using ::curve::common::Configuration;

class TestFuseVolumeClient : public ::testing::Test {
 protected:
    TestFuseVolumeClient() {}
    ~TestFuseVolumeClient() {}

    virtual void SetUp() {
        mdsClient_ = std::make_shared<MockMdsClient>();
        metaClient_ = std::make_shared<MockMetaServerClient>();
        spaceClient_ = std::make_shared<MockSpaceClient>();
        blockDeviceClient_ = std::make_shared<MockBlockDeviceClient>();
        inodeManager_ = std::make_shared<MockInodeCacheManager>();
        dentryManager_ = std::make_shared<MockDentryCacheManager>();
        extManager_ = std::make_shared<MockExtentManager>();
        client_  = std::make_shared<FuseVolumeClient>(mdsClient_,
            metaClient_,
            spaceClient_,
            inodeManager_,
            dentryManager_,
            extManager_,
            blockDeviceClient_);
        PrepareFsInfo();
    }

    virtual void TearDown() {
        mdsClient_ = nullptr;
        metaClient_ = nullptr;
        spaceClient_ = nullptr;
        blockDeviceClient_ = nullptr;
        extManager_ = nullptr;
    }

    void PrepareFsInfo() {
        auto fsInfo = std::make_shared<FsInfo>();
        fsInfo->set_fsid(fsId);
        fsInfo->set_fsname("xxx");

        client_->SetFsInfo(fsInfo);
    }

 protected:
    const uint32_t fsId = 100u;

    std::shared_ptr<MockMdsClient> mdsClient_;
    std::shared_ptr<MockMetaServerClient> metaClient_;
    std::shared_ptr<MockSpaceClient> spaceClient_;
    std::shared_ptr<MockBlockDeviceClient> blockDeviceClient_;
    std::shared_ptr<MockInodeCacheManager> inodeManager_;
    std::shared_ptr<MockDentryCacheManager> dentryManager_;
    std::shared_ptr<MockExtentManager> extManager_;
    std::shared_ptr<FuseVolumeClient> client_;
};

TEST_F(TestFuseVolumeClient, init_when_fs_exist) {
    MountOption mOpts;
    memset(&mOpts, 0, sizeof(mOpts));
    mOpts.mountPoint = "host1:/test";
    mOpts.volume = "xxx";
    mOpts.user = "test";
    mOpts.fsType = "curve";

    std::string volName = mOpts.volume;
    std::string user = mOpts.user;
    std::string fsName = mOpts.volume;

    EXPECT_CALL(*mdsClient_, GetFsInfo(fsName, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    FsInfo fsInfoExp;
    fsInfoExp.set_fsid(200);
    fsInfoExp.set_fsname(fsName);
    EXPECT_CALL(*mdsClient_, MountFs(fsName, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(fsInfoExp),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*blockDeviceClient_, Open(volName, user))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    client_->init(&mOpts, nullptr);

    auto fsInfo = client_->GetFsInfo();
    ASSERT_NE(fsInfo, nullptr);

    ASSERT_EQ(fsInfo->fsid(), fsInfoExp.fsid());
    ASSERT_EQ(fsInfo->fsname(), fsInfoExp.fsname());
}

TEST_F(TestFuseVolumeClient, init_when_fs_not_exist) {
    MountOption mOpts;
    memset(&mOpts, 0, sizeof(mOpts));
    mOpts.mountPoint = "host1:/test";
    mOpts.volume = "xxx";
    mOpts.user = "test";
    mOpts.fsType = "curve";

    std::string volName = mOpts.volume;
    std::string user = mOpts.user;
    std::string fsName = mOpts.volume;

    EXPECT_CALL(*mdsClient_, GetFsInfo(fsName, _))
        .WillOnce(Return(CURVEFS_ERROR::NOTEXIST));

    EXPECT_CALL(*blockDeviceClient_, Stat(volName, user, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    EXPECT_CALL(*mdsClient_, CreateFs(_, _, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    FsInfo fsInfoExp;
    fsInfoExp.set_fsid(100);
    fsInfoExp.set_fsname(fsName);
    EXPECT_CALL(*mdsClient_, MountFs(fsName, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(fsInfoExp),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*blockDeviceClient_, Open(volName, user))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    client_->init(&mOpts, nullptr);

    auto fsInfo = client_->GetFsInfo();
    ASSERT_NE(fsInfo, nullptr);

    ASSERT_EQ(fsInfo->fsid(), fsInfoExp.fsid());
    ASSERT_EQ(fsInfo->fsname(), fsInfoExp.fsname());
}

TEST_F(TestFuseVolumeClient, destroy) {
    MountOption mOpts;
    memset(&mOpts, 0, sizeof(mOpts));
    mOpts.mountPoint = "host1:/test";
    mOpts.volume = "xxx";
    mOpts.fsType = "curve";

    std::string fsName = mOpts.volume;

    EXPECT_CALL(*mdsClient_, UmountFs(fsName, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    EXPECT_CALL(*blockDeviceClient_, Close())
        .WillOnce(Return(CURVEFS_ERROR::OK));

    client_->destroy(&mOpts);
}

TEST_F(TestFuseVolumeClient, lookup) {
    fuse_req_t req;
    fuse_ino_t parent = 1;
    std::string name = "test";

    fuse_ino_t inodeid = 2;

    Dentry dentry;
    dentry.set_fsid(fsId);
    dentry.set_name(name);
    dentry.set_parentinodeid(parent);
    dentry.set_inodeid(inodeid);

    EXPECT_CALL(*dentryManager_, GetDentry(parent, name, _))
        .WillOnce(DoAll(SetArgPointee<2>(dentry),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*inodeManager_, GetInode(inodeid, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    fuse_entry_param e;
    CURVEFS_ERROR ret = client_->lookup(req, parent, name.c_str(), &e);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
}

TEST_F(TestFuseVolumeClient, lookupFail) {
    fuse_req_t req;
    fuse_ino_t parent = 1;
    std::string name = "test";

    fuse_ino_t inodeid = 2;

    Dentry dentry;
    dentry.set_fsid(fsId);
    dentry.set_name(name);
    dentry.set_parentinodeid(parent);
    dentry.set_inodeid(inodeid);

    EXPECT_CALL(*dentryManager_, GetDentry(parent, name, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<2>(dentry),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*inodeManager_, GetInode(inodeid, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    fuse_entry_param e;
    CURVEFS_ERROR ret = client_->lookup(req, parent, name.c_str(), &e);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->lookup(req, parent, name.c_str(), &e);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

extern const uint64_t kMinAllocSize;

TEST_F(TestFuseVolumeClient, write) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    const char *buf = "xxx";
    size_t size = 4;
    off_t off = 0;
    struct fuse_file_info *fi;
    size_t wSize = 0;

    Inode inode;
    inode.set_inodeid(ino);
    inode.set_length(0);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    std::list<ExtentAllocInfo> toAllocExtents;
    ExtentAllocInfo allocInfo;
    allocInfo.lOffset = 0;
    allocInfo.pOffsetLeft = 0;
    allocInfo.len = kMinAllocSize;
    toAllocExtents.push_back(allocInfo);
    EXPECT_CALL(*extManager_, GetToAllocExtents(_, off, size, _))
        .WillOnce(DoAll(SetArgPointee<3>(toAllocExtents),
                Return(CURVEFS_ERROR::OK)));

    std::list<Extent> allocatedExtents;
    Extent ext;
    ext.set_offset(0);
    ext.set_length(kMinAllocSize);
    EXPECT_CALL(*spaceClient_, AllocExtents(fsId, _, AllocateType::SMALL, _))
        .WillOnce(DoAll(SetArgPointee<3>(allocatedExtents),
            Return(CURVEFS_ERROR::OK)));

    VolumeExtentList *vlist = new VolumeExtentList();
    VolumeExtent *vext = vlist->add_volumeextents();
    vext->set_fsoffset(0);
    vext->set_volumeoffset(0);
    vext->set_length(kMinAllocSize);
    vext->set_isused(false);
    inode.set_allocated_volumeextentlist(vlist);

    EXPECT_CALL(*extManager_, MergeAllocedExtents(_, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(*vlist),
            Return(CURVEFS_ERROR::OK)));

    std::list<PExtent> pExtents;
    PExtent pext;
    pext.pOffset = 0;
    pext.len = kMinAllocSize;
    pExtents.push_back(pext);
    EXPECT_CALL(*extManager_, DivideExtents(_, off, size, _))
        .WillOnce(DoAll(SetArgPointee<3>(pExtents),
            Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*blockDeviceClient_, Write(_, 0, kMinAllocSize))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    EXPECT_CALL(*extManager_, MarkExtentsWritten(off, size, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));


    EXPECT_CALL(*inodeManager_, UpdateInode(_))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    CURVEFS_ERROR ret = client_->write(req, ino, buf, size, off, fi, &wSize);

    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_EQ(size, wSize);
}

TEST_F(TestFuseVolumeClient, writeFailed) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    const char *buf = "xxx";
    size_t size = 4;
    off_t off = 0;
    struct fuse_file_info *fi;
    size_t wSize = 0;

    Inode inode;
    inode.set_inodeid(ino);
    inode.set_length(0);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    std::list<ExtentAllocInfo> toAllocExtents;
    ExtentAllocInfo allocInfo;
    allocInfo.lOffset = 0;
    allocInfo.pOffsetLeft = 0;
    allocInfo.len = kMinAllocSize;
    toAllocExtents.push_back(allocInfo);
    EXPECT_CALL(*extManager_, GetToAllocExtents(_, off, size, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<3>(toAllocExtents),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(toAllocExtents),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(toAllocExtents),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(toAllocExtents),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(toAllocExtents),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(toAllocExtents),
                Return(CURVEFS_ERROR::OK)));

    std::list<Extent> allocatedExtents;
    Extent ext;
    ext.set_offset(0);
    ext.set_length(kMinAllocSize);
    EXPECT_CALL(*spaceClient_, AllocExtents(fsId, _, AllocateType::SMALL, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<3>(allocatedExtents),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(allocatedExtents),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(allocatedExtents),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(allocatedExtents),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(allocatedExtents),
            Return(CURVEFS_ERROR::OK)));

    VolumeExtentList *vlist = new VolumeExtentList();
    VolumeExtent *vext = vlist->add_volumeextents();
    vext->set_fsoffset(0);
    vext->set_volumeoffset(0);
    vext->set_length(kMinAllocSize);
    vext->set_isused(false);
    inode.set_allocated_volumeextentlist(vlist);

    EXPECT_CALL(*extManager_, MergeAllocedExtents(_, _, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<2>(*vlist),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<2>(*vlist),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<2>(*vlist),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<2>(*vlist),
            Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*spaceClient_, DeAllocExtents(_, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    std::list<PExtent> pExtents;
    PExtent pext;
    pext.pOffset = 0;
    pext.len = kMinAllocSize;
    pExtents.push_back(pext);
    EXPECT_CALL(*extManager_, DivideExtents(_, off, size, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<3>(pExtents),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(pExtents),
            Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<3>(pExtents),
            Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*blockDeviceClient_, Write(_, 0, kMinAllocSize))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(Return(CURVEFS_ERROR::OK))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    EXPECT_CALL(*extManager_, MarkExtentsWritten(off, size, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(Return(CURVEFS_ERROR::OK));


    EXPECT_CALL(*inodeManager_, UpdateInode(_))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    CURVEFS_ERROR ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

TEST_F(TestFuseVolumeClient, read) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    size_t size = 4;
    off_t off = 0;
    struct fuse_file_info *fi;
    char *buffer;
    size_t rSize = 0;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4096);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    std::list<PExtent> pExtents;
    PExtent pext1, pext2;
    pext1.pOffset = 0;
    pext1.len = 4;
    pext1.UnWritten = false;
    pext2.pOffset = 4;
    pext2.len = 4096;
    pext2.UnWritten = true;
    pExtents.push_back(pext1);
    pExtents.push_back(pext2);

    EXPECT_CALL(*extManager_, DivideExtents(_, off, size, _))
        .WillOnce(DoAll(SetArgPointee<3>(pExtents),
            Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*blockDeviceClient_, Read(_, 0, 4))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    CURVEFS_ERROR ret = client_->read(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_EQ(size, rSize);
}

TEST_F(TestFuseVolumeClient, readFailed) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    size_t size = 4;
    off_t off = 0;
    struct fuse_file_info *fi;
    char *buffer;
    size_t rSize = 0;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4096);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    std::list<PExtent> pExtents;
    PExtent pext1, pext2;
    pext1.pOffset = 0;
    pext1.len = 4;
    pext1.UnWritten = false;
    pext2.pOffset = 4;
    pext2.len = 4096;
    pext2.UnWritten = true;
    pExtents.push_back(pext1);
    pExtents.push_back(pext2);

    EXPECT_CALL(*extManager_, DivideExtents(_, off, size, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<3>(pExtents),
            Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*blockDeviceClient_, Read(_, 0, 4))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    CURVEFS_ERROR ret = client_->read(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->read(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->read(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

TEST_F(TestFuseVolumeClient, open) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    struct fuse_file_info fi;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4096);
    inode.set_type(FsFileType::TYPE_FILE);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    CURVEFS_ERROR ret = client_->open(req, ino, &fi);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
}

TEST_F(TestFuseVolumeClient, openFailed) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    struct fuse_file_info fi;

    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    CURVEFS_ERROR ret = client_->open(req, ino, &fi);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

TEST_F(TestFuseVolumeClient, create) {
    fuse_req_t req;
    fuse_ino_t parent = 1;
    const char *name = "xxx";
    mode_t mode = 1;
    struct fuse_file_info *fi;

    fuse_ino_t ino = 2;
    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4096);
    EXPECT_CALL(*inodeManager_, CreateInode(_, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*dentryManager_, CreateDentry(_))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    fuse_entry_param e;
    CURVEFS_ERROR ret = client_->create(req, parent, name, mode, fi, &e);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
}

TEST_F(TestFuseVolumeClient, createFailed) {
    fuse_req_t req;
    fuse_ino_t parent = 1;
    const char *name = "xxx";
    mode_t mode = 1;
    struct fuse_file_info *fi;

    fuse_ino_t ino = 2;
    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4096);
    EXPECT_CALL(*inodeManager_, CreateInode(_, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*dentryManager_, CreateDentry(_))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    fuse_entry_param e;
    CURVEFS_ERROR ret = client_->create(req, parent, name, mode, fi, &e);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->create(req, parent, name, mode, fi, &e);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

TEST_F(TestFuseVolumeClient, unlink) {
    fuse_req_t req;
    fuse_ino_t parent = 1;
    std::string name = "xxx";

    fuse_ino_t inodeid = 2;

    Dentry dentry;
    dentry.set_fsid(fsId);
    dentry.set_name(name);
    dentry.set_parentinodeid(parent);
    dentry.set_inodeid(inodeid);

    EXPECT_CALL(*dentryManager_, GetDentry(parent, name, _))
        .WillOnce(DoAll(SetArgPointee<2>(dentry),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*dentryManager_, DeleteDentry(parent, name))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    EXPECT_CALL(*inodeManager_, DeleteInode(inodeid))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    CURVEFS_ERROR ret = client_->unlink(req, parent, name.c_str());
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
}

TEST_F(TestFuseVolumeClient, unlinkFailed) {
    fuse_req_t req;
    fuse_ino_t parent = 1;
    std::string name = "xxx";

    fuse_ino_t inodeid = 2;

    Dentry dentry;
    dentry.set_fsid(fsId);
    dentry.set_name(name);
    dentry.set_parentinodeid(parent);
    dentry.set_inodeid(inodeid);

    EXPECT_CALL(*dentryManager_, GetDentry(parent, name, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<2>(dentry),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<2>(dentry),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*dentryManager_, DeleteDentry(parent, name))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    EXPECT_CALL(*inodeManager_, DeleteInode(inodeid))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    CURVEFS_ERROR ret = client_->unlink(req, parent, name.c_str());
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->unlink(req, parent, name.c_str());
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->unlink(req, parent, name.c_str());
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

TEST_F(TestFuseVolumeClient, opendir) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    struct fuse_file_info fi;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4);
    inode.set_type(FsFileType::TYPE_DIRECTORY);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    CURVEFS_ERROR ret = client_->opendir(req, ino, &fi);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
}

TEST_F(TestFuseVolumeClient, opendirFaild) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    struct fuse_file_info fi;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4);
    inode.set_type(FsFileType::TYPE_DIRECTORY);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::FAILED)));

    CURVEFS_ERROR ret = client_->opendir(req, ino, &fi);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

TEST_F(TestFuseVolumeClient, openAndreaddir) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    size_t size = 100;
    off_t off = 0;
    struct fuse_file_info *fi = new fuse_file_info();
    fi->fh = 0;
    char *buffer;
    size_t rSize = 0;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(0);
    inode.set_type(FsFileType::TYPE_DIRECTORY);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .Times(2)
        .WillRepeatedly(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    CURVEFS_ERROR ret = client_->opendir(req, ino, fi);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);


    std::list<Dentry> dentryList;
    Dentry dentry;
    dentry.set_fsid(fsId);
    dentry.set_name("xxx");
    dentry.set_parentinodeid(ino);
    dentry.set_inodeid(2);
    dentryList.push_back(dentry);

    EXPECT_CALL(*dentryManager_, ListDentry(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(dentryList),
                Return(CURVEFS_ERROR::OK)));

    ret = client_->readdir(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);

    delete fi;
}

TEST_F(TestFuseVolumeClient, openAndreaddirFailed) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    size_t size = 100;
    off_t off = 0;
    struct fuse_file_info *fi = new fuse_file_info();
    fi->fh = 0;
    char *buffer;
    size_t rSize = 0;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(0);
    inode.set_type(FsFileType::TYPE_DIRECTORY);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .Times(2)
        .WillRepeatedly(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    CURVEFS_ERROR ret = client_->opendir(req, ino, fi);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);


    std::list<Dentry> dentryList;
    Dentry dentry;
    dentry.set_fsid(fsId);
    dentry.set_name("xxx");
    dentry.set_parentinodeid(ino);
    dentry.set_inodeid(2);
    dentryList.push_back(dentry);

    EXPECT_CALL(*dentryManager_, ListDentry(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(dentryList),
                Return(CURVEFS_ERROR::FAILED)));

    ret = client_->readdir(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    delete fi;
}

TEST_F(TestFuseVolumeClient, getattr) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    struct fuse_file_info *fi;
    struct stat attr;

    Inode inode;
    inode.set_inodeid(ino);
    inode.set_length(0);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    CURVEFS_ERROR ret = client_->getattr(req, ino, fi, &attr);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
}

TEST_F(TestFuseVolumeClient, getattrFailed) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    struct fuse_file_info *fi;
    struct stat attr;

    Inode inode;
    inode.set_inodeid(ino);
    inode.set_length(0);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::FAILED)));

    CURVEFS_ERROR ret = client_->getattr(req, ino, fi, &attr);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

TEST_F(TestFuseVolumeClient, setattr) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    struct stat attr;
    int to_set;
    struct fuse_file_info *fi;
    struct stat attrOut;

    Inode inode;
    inode.set_inodeid(ino);
    inode.set_length(0);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*inodeManager_, UpdateInode(_))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    attr.st_mode = 1;
    attr.st_uid = 2;
    attr.st_gid = 3;
    attr.st_size = 4;
    attr.st_atime = 5;
    attr.st_mtime = 6;
    attr.st_ctime = 7;

    to_set = FUSE_SET_ATTR_MODE |
             FUSE_SET_ATTR_UID |
             FUSE_SET_ATTR_GID |
             FUSE_SET_ATTR_SIZE |
             FUSE_SET_ATTR_ATIME |
             FUSE_SET_ATTR_MTIME |
             FUSE_SET_ATTR_CTIME;

    CURVEFS_ERROR ret = client_->setattr(req, ino, &attr, to_set, fi, &attrOut);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_EQ(attr.st_mode,  attrOut.st_mode);
    ASSERT_EQ(attr.st_uid,  attrOut.st_uid);
    ASSERT_EQ(attr.st_gid,  attrOut.st_gid);
    ASSERT_EQ(attr.st_size,  attrOut.st_size);
    ASSERT_EQ(attr.st_atime,  attrOut.st_atime);
    ASSERT_EQ(attr.st_mtime,  attrOut.st_mtime);
    ASSERT_EQ(attr.st_ctime,  attrOut.st_ctime);
}

TEST_F(TestFuseVolumeClient, setattrFailed) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    struct stat attr;
    int to_set;
    struct fuse_file_info *fi;
    struct stat attrOut;

    Inode inode;
    inode.set_inodeid(ino);
    inode.set_length(0);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*inodeManager_, UpdateInode(_))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    attr.st_mode = 1;
    attr.st_uid = 2;
    attr.st_gid = 3;
    attr.st_size = 4;
    attr.st_atime = 5;
    attr.st_mtime = 6;
    attr.st_ctime = 7;

    to_set = FUSE_SET_ATTR_MODE |
             FUSE_SET_ATTR_UID |
             FUSE_SET_ATTR_GID |
             FUSE_SET_ATTR_SIZE |
             FUSE_SET_ATTR_ATIME |
             FUSE_SET_ATTR_MTIME |
             FUSE_SET_ATTR_CTIME;

    CURVEFS_ERROR ret = client_->setattr(req, ino, &attr, to_set, fi, &attrOut);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->setattr(req, ino, &attr, to_set, fi, &attrOut);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

class TestFuseS3Client : public ::testing::Test {
 protected:
    TestFuseS3Client() {}
    ~TestFuseS3Client() {}

    virtual void SetUp() {
        mdsClient_ = std::make_shared<MockMdsClient>();
        metaClient_ = std::make_shared<MockMetaServerClient>();
        spaceClient_ = std::make_shared<MockSpaceClient>();
        s3ClientAdaptor_ = std::make_shared<MockS3ClientAdaptor>();
        inodeManager_ = std::make_shared<MockInodeCacheManager>();
        dentryManager_ = std::make_shared<MockDentryCacheManager>();
        extManager_ = std::make_shared<MockExtentManager>();
        client_  = std::make_shared<FuseS3Client>(mdsClient_,
            metaClient_,
            spaceClient_,
            inodeManager_,
            dentryManager_,
            extManager_,
            s3ClientAdaptor_);
        PrepareFsInfo();
    }

    virtual void TearDown() {
        mdsClient_ = nullptr;
        metaClient_ = nullptr;
        spaceClient_ = nullptr;
        s3ClientAdaptor_ = nullptr;
        extManager_ = nullptr;
    }

    void PrepareFsInfo() {
        auto fsInfo = std::make_shared<FsInfo>();
        fsInfo->set_fsid(fsId);
        fsInfo->set_fsname("s3fs");

        client_->SetFsInfo(fsInfo);
    }

 protected:
    const uint32_t fsId = 100u;

    std::shared_ptr<MockMdsClient> mdsClient_;
    std::shared_ptr<MockMetaServerClient> metaClient_;
    std::shared_ptr<MockSpaceClient> spaceClient_;
    std::shared_ptr<MockS3ClientAdaptor> s3ClientAdaptor_;
    std::shared_ptr<MockInodeCacheManager> inodeManager_;
    std::shared_ptr<MockDentryCacheManager> dentryManager_;
    std::shared_ptr<MockExtentManager> extManager_;
    std::shared_ptr<FuseS3Client> client_;
};

TEST_F(TestFuseS3Client, init_when_fs_exist) {
    MountOption mOpts;
    memset(&mOpts, 0, sizeof(mOpts));
    mOpts.fsName = "s3fs";
    mOpts.mountPoint = "host1:/test";
    mOpts.user = "test";
    mOpts.fsType = "s3";

    std::string fsName = mOpts.fsName;

    EXPECT_CALL(*mdsClient_, GetFsInfo(fsName, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    FsInfo fsInfoExp;
    fsInfoExp.set_fsid(200);
    fsInfoExp.set_fsname(fsName);
    EXPECT_CALL(*mdsClient_, MountFs(fsName, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(fsInfoExp),
                Return(CURVEFS_ERROR::OK)));

    client_->init(&mOpts, nullptr);

    auto fsInfo = client_->GetFsInfo();
    ASSERT_NE(fsInfo, nullptr);

    ASSERT_EQ(fsInfo->fsid(), fsInfoExp.fsid());
    ASSERT_EQ(fsInfo->fsname(), fsInfoExp.fsname());
}

TEST_F(TestFuseS3Client, init_when_fs_not_exist) {
    MountOption mOpts;
    memset(&mOpts, 0, sizeof(mOpts));
    mOpts.fsName = "s3fs";
    mOpts.mountPoint = "host1:/test";
    mOpts.user = "test";
    mOpts.fsType = "s3";

    std::string fsName = mOpts.fsName;

    EXPECT_CALL(*mdsClient_, GetFsInfo(fsName, _))
        .WillOnce(Return(CURVEFS_ERROR::NOTEXIST));

    EXPECT_CALL(*mdsClient_, CreateFsS3(_, _, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    FsInfo fsInfoExp;
    fsInfoExp.set_fsid(100);
    fsInfoExp.set_fsname(fsName);
    EXPECT_CALL(*mdsClient_, MountFs(fsName, _, _))
        .WillOnce(DoAll(SetArgPointee<2>(fsInfoExp),
                Return(CURVEFS_ERROR::OK)));

    client_->init(&mOpts, nullptr);

    auto fsInfo = client_->GetFsInfo();
    ASSERT_NE(fsInfo, nullptr);

    ASSERT_EQ(fsInfo->fsid(), fsInfoExp.fsid());
    ASSERT_EQ(fsInfo->fsname(), fsInfoExp.fsname());
}

TEST_F(TestFuseS3Client, destroy) {
    MountOption mOpts;
    memset(&mOpts, 0, sizeof(mOpts));
    mOpts.fsName = "s3fs";
    mOpts.mountPoint = "host1:/test";
    mOpts.user = "test";
    mOpts.fsType = "s3";

    std::string fsName = mOpts.fsName;

    EXPECT_CALL(*mdsClient_, UmountFs(fsName, _))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    client_->destroy(&mOpts);
}

TEST_F(TestFuseS3Client, write) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    const char *buf = "xxx";
    size_t size = 4;
    off_t off = 0;
    struct fuse_file_info *fi;
    size_t wSize = 0;

    Inode inode;
    inode.set_inodeid(ino);
    inode.set_length(0);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*s3ClientAdaptor_, Write(_, _, _, _))
        .WillOnce(Return(size));

    EXPECT_CALL(*inodeManager_, UpdateInode(_))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    CURVEFS_ERROR ret = client_->write(req, ino, buf, size, off, fi, &wSize);

    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_EQ(size, wSize);
}

TEST_F(TestFuseS3Client, writeFailed) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    const char *buf = "xxx";
    size_t size = 4;
    off_t off = 0;
    struct fuse_file_info *fi;
    size_t wSize = 0;

    Inode inode;
    inode.set_inodeid(ino);
    inode.set_length(0);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*s3ClientAdaptor_, Write(_, _, _, _))
        .WillOnce(Return(-1))
        .WillOnce(Return(size));

    EXPECT_CALL(*inodeManager_, UpdateInode(_))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    CURVEFS_ERROR ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->write(req, ino, buf, size, off, fi, &wSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

TEST_F(TestFuseS3Client, read) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    size_t size = 4;
    off_t off = 0;
    struct fuse_file_info *fi;
    char *buffer;
    size_t rSize = 0;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4096);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*s3ClientAdaptor_, Read(_, _, _, _))
        .WillOnce(Return(size));

    EXPECT_CALL(*inodeManager_, UpdateInode(_))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    CURVEFS_ERROR ret = client_->read(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_EQ(size, rSize);
}

TEST_F(TestFuseS3Client, readFailed) {
    fuse_req_t req;
    fuse_ino_t ino = 1;
    size_t size = 4;
    off_t off = 0;
    struct fuse_file_info *fi;
    char *buffer;
    size_t rSize = 0;

    Inode inode;
    inode.set_fsid(fsId);
    inode.set_inodeid(ino);
    inode.set_length(4096);
    EXPECT_CALL(*inodeManager_, GetInode(ino, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<1>(inode),
                Return(CURVEFS_ERROR::OK)));

    EXPECT_CALL(*s3ClientAdaptor_, Read(_, _, _, _))
        .WillOnce(Return(-1))
        .WillOnce(Return(size));

    EXPECT_CALL(*inodeManager_, UpdateInode(_))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    CURVEFS_ERROR ret = client_->read(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->read(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = client_->read(req, ino, size, off, fi,
        &buffer, &rSize);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
}

}  // namespace client
}  // namespace curvefs