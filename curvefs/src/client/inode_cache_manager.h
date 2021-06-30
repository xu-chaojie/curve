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

#ifndef CURVEFS_SRC_CLIENT_INODE_CACHE_MANAGER_H_
#define CURVEFS_SRC_CLIENT_INODE_CACHE_MANAGER_H_

#include <memory>
#include <unordered_map>

#include "curvefs/src/client/metaserver_client.h"
#include "curvefs/src/client/error_code.h"
#include "src/common/concurrent/concurrent.h"

using ::curvefs::metaserver::Inode;

namespace curvefs {
namespace client {

class InodeCacheManager {
 public:
    InodeCacheManager()
      : fsId_(0) {}
    virtual ~InodeCacheManager() {}

    CURVEFS_ERROR Init(uint32_t fsId) {
        fsId_ = fsId;
    }

    virtual CURVEFS_ERROR GetInode(uint64_t inodeid, Inode *out) = 0;

    virtual CURVEFS_ERROR UpdateInode(const Inode &inode) = 0;

    virtual CURVEFS_ERROR CreateInode(const InodeParam &param, Inode *out) = 0;

    virtual CURVEFS_ERROR DeleteInode(uint64_t inodeid) = 0;

 protected:
    uint32_t fsId_;
};

class InodeCacheManagerImpl : public InodeCacheManager {
 public:
    InodeCacheManagerImpl()
      : metaClient_(std::make_shared<MetaServerClientImpl>()) {}

    explicit InodeCacheManagerImpl(
        const std::shared_ptr<MetaServerClient> &metaClient)
      : metaClient_(metaClient) {}

    CURVEFS_ERROR GetInode(uint64_t inodeid, Inode *out) override;

    CURVEFS_ERROR UpdateInode(const Inode &inode) override;

    CURVEFS_ERROR CreateInode(const InodeParam &param, Inode *out) override;

    CURVEFS_ERROR DeleteInode(uint64_t inodeid) override;

 private:
    std::shared_ptr<MetaServerClient> metaClient_;
    std::unordered_map<uint64_t, Inode> iCache_;
    curve::common::Mutex mtx_;
};


}  // namespace client
}  // namespace curvefs

#endif  // CURVEFS_SRC_CLIENT_INODE_CACHE_MANAGER_H_
