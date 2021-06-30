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
#include <google/protobuf/util/message_differencer.h>

#include "curvefs/test/client/mock_metaserver_client.h"
#include "curvefs/src/client/dentry_cache_manager.h"

namespace curvefs {
namespace client {

using ::testing::Return;
using ::testing::_;
using ::testing::Contains;
using ::testing::SetArgPointee;
using ::testing::DoAll;

class TestDentryCacheManager : public ::testing::Test {
 protected:
    TestDentryCacheManager() {}
    ~TestDentryCacheManager() {}

    virtual void SetUp() {
        metaClient_ = std::make_shared<MockMetaServerClient>();
        dCacheManager_ = std::make_shared<DentryCacheManagerImpl>(metaClient_);
        dCacheManager_->Init(fsId_);
    }


    virtual void TearDown() {
        metaClient_ = nullptr;
        dCacheManager_ = nullptr;
    }

 protected:
    std::shared_ptr<DentryCacheManagerImpl> dCacheManager_;
    std::shared_ptr<MockMetaServerClient> metaClient_;
    uint32_t fsId_ = 888;
};

TEST_F(TestDentryCacheManager, GetDentry) {
    uint64_t parent = 99;
    uint64_t inodeid = 100;
    const std::string name = "test";
    Dentry out;

    Dentry dentryExp;
    dentryExp.set_fsid(fsId_);
    dentryExp.set_name(name);
    dentryExp.set_parentinodeid(parent);
    dentryExp.set_inodeid(inodeid);

    EXPECT_CALL(*metaClient_, GetDentry(fsId_, parent, name, _))
        .WillOnce(Return(CURVEFS_ERROR::NOTEXIST))
        .WillOnce(DoAll(SetArgPointee<3>(dentryExp),
                Return(CURVEFS_ERROR::OK)));

    CURVEFS_ERROR ret = dCacheManager_->GetDentry(parent, name, &out);
    ASSERT_EQ(CURVEFS_ERROR::NOTEXIST, ret);

    ret = dCacheManager_->GetDentry(parent, name, &out);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_TRUE(
        google::protobuf::util::MessageDifferencer::Equals(dentryExp, out));

    ret = dCacheManager_->GetDentry(parent, name, &out);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_TRUE(
        google::protobuf::util::MessageDifferencer::Equals(dentryExp, out));
}

TEST_F(TestDentryCacheManager, CreateAndGetDentry) {
    uint64_t parent = 99;
    uint64_t inodeid = 100;
    const std::string name = "test";
    Dentry out;

    Dentry dentryExp;
    dentryExp.set_fsid(fsId_);
    dentryExp.set_name(name);
    dentryExp.set_parentinodeid(parent);
    dentryExp.set_inodeid(inodeid);

    EXPECT_CALL(*metaClient_, CreateDentry(_))
        .WillOnce(Return(CURVEFS_ERROR::FAILED))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    CURVEFS_ERROR ret = dCacheManager_->CreateDentry(dentryExp);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);

    ret = dCacheManager_->CreateDentry(dentryExp);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);

    ret = dCacheManager_->GetDentry(parent, name, &out);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_TRUE(
        google::protobuf::util::MessageDifferencer::Equals(dentryExp, out));
}

TEST_F(TestDentryCacheManager, DeleteDentry) {
    uint64_t parent = 99;
    const std::string name = "test";

    EXPECT_CALL(*metaClient_, DeleteDentry(fsId_, parent, name))
        .WillOnce(Return(CURVEFS_ERROR::NOTEXIST))
        .WillOnce(Return(CURVEFS_ERROR::OK));

    CURVEFS_ERROR ret = dCacheManager_->DeleteDentry(parent, name);
    ASSERT_EQ(CURVEFS_ERROR::NOTEXIST, ret);

    ret = dCacheManager_->DeleteDentry(parent, name);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
}

extern const uint32_t kMaxListDentryCount;

TEST_F(TestDentryCacheManager, ListDentryNomal) {
    uint64_t parent = 99;

    std::list<Dentry> part1, part2;
    part1.resize(kMaxListDentryCount);
    part2.resize(kMaxListDentryCount - 1);

    EXPECT_CALL(*metaClient_, ListDentry(fsId_, parent, _, _, _))
        .WillOnce(DoAll(SetArgPointee<4>(part1),
                Return(CURVEFS_ERROR::OK)))
        .WillOnce(DoAll(SetArgPointee<4>(part2),
                Return(CURVEFS_ERROR::OK)));

    std::list<Dentry> out;
    CURVEFS_ERROR ret = dCacheManager_->ListDentry(parent, &out);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_EQ(2 * kMaxListDentryCount - 1, out.size());
}

TEST_F(TestDentryCacheManager, ListDentryEmpty) {
    uint64_t parent = 99;

    EXPECT_CALL(*metaClient_, ListDentry(fsId_, parent, _, _, _))
        .WillOnce(Return(CURVEFS_ERROR::NOTEXIST));

    std::list<Dentry> out;
    CURVEFS_ERROR ret = dCacheManager_->ListDentry(parent, &out);
    ASSERT_EQ(CURVEFS_ERROR::OK, ret);
    ASSERT_EQ(0, out.size());
}

TEST_F(TestDentryCacheManager, ListDentryFailed) {
    uint64_t parent = 99;

    EXPECT_CALL(*metaClient_, ListDentry(fsId_, parent, _, _, _))
        .WillOnce(Return(CURVEFS_ERROR::FAILED));

    std::list<Dentry> out;
    CURVEFS_ERROR ret = dCacheManager_->ListDentry(parent, &out);
    ASSERT_EQ(CURVEFS_ERROR::FAILED, ret);
    ASSERT_EQ(0, out.size());
}








}  // namespace client
}  // namespace curvefs
