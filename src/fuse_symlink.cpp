/*
  Copyright (c) 2016, Antonio SJ Musumeci <trapexit@spawn.link>

  Permission to use, copy, modify, and/or distribute this software for any
  purpose with or without fee is hereby granted, provided that the above
  copyright notice and this permission notice appear in all copies.

  THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
  WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
  MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
  ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
  WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
  ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
  OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/

#include "config.hpp"
#include "errno.hpp"
#include "fs_clonepath.hpp"
#include "fs_inode.hpp"
#include "fs_lstat.hpp"
#include "fs_path.hpp"
#include "fs_symlink.hpp"
#include "fuse_getattr.hpp"
#include "syslog.hpp"
#include "ugid.hpp"

#include "fuse.h"

#include <string>

#include <sys/types.h>
#include <unistd.h>

using std::string;


namespace error
{
  static
  inline
  int
  calc(const int rv_,
       const int prev_,
       const int cur_)
  {
    if(rv_ == -1)
      {
        if(prev_ == 0)
          return 0;
        return cur_;
      }

    return 0;
  }
}

namespace l
{
  void
  set_branches_mode_to_ro(const std::string path_to_set_ro_)
  {
    Config::Write cfg;

    for(auto &branch : *cfg->branches())
      {
        if(branch.path != path_to_set_ro_)
          continue;
        branch.mode = Branch::Mode::RO;
        syslog_warning("Error opening file for write: EROFS - branch %s mode set to RO",branch.path.c_str());
      }
  }

  static
  int
  symlink(const string &newbasepath_,
          const char   *target_,
          const char   *linkpath_,
          struct stat  *st_)
  {
    int rv;
    string fullnewpath;

    fullnewpath = fs::path::make(newbasepath_,linkpath_);

    rv = fs::symlink(target_,fullnewpath);
    if((rv != -1) && (st_ != NULL) && (st_->st_ino == 0))
      {
        fs::lstat(fullnewpath,st_);
        if(st_->st_ino != 0)
          fs::inode::calc(linkpath_,st_);
      }

    return rv;
  }

  static
  int
  symlink(const std::string &existingpath_,
          const std::string &newbasepath_,
          const char        *target_,
          const char        *linkpath_,
          const std::string &newdirpath_,
          struct stat       *st_)
  {
    int rv;

    rv = fs::clonepath_as_root(existingpath_,newbasepath_,newdirpath_);
    if(rv == -1)
      return rv;

    return l::symlink(newbasepath_,target_,linkpath_,st_);
  }

  static
  int
  symlink(const string &existingpath_,
          const StrVec &newbasepaths_,
          const char   *target_,
          const char   *linkpath_,
          const string &newdirpath_,
          struct stat  *st_)
  {
    int rv;
    int error;

    error = -1;
    for(auto const &newbasepath : newbasepaths_)
      {
        rv = l::symlink(existingpath_,newbasepath,target_,linkpath_,newdirpath_,st_);
        if((rv == -1) && (errno == EROFS))
          l::set_branches_mode_to_ro(newbasepath);

        error = error::calc(rv,error,errno);
      }

    return -error;
  }

  static
  int
  symlink(const Policy::Search &searchFunc_,
          const Policy::Create &createFunc_,
          const Branches       &branches_,
          const char           *target_,
          const char           *linkpath_,
          struct stat          *st_)
  {
    int rv;
    string newdirpath;
    StrVec newbasepaths;
    StrVec existingpaths;

    newdirpath = fs::path::dirname(linkpath_);

    rv = searchFunc_(branches_,newdirpath,&existingpaths);
    if(rv == -1)
      return -errno;

    rv = createFunc_(branches_,newdirpath,&newbasepaths);
    if(rv == -1)
      return -errno;

    rv = l::symlink(existingpaths[0],newbasepaths,target_,linkpath_,newdirpath,st_);
    if(rv == -EROFS)
      {
        newbasepaths.clear();
        rv = createFunc_(branches_,newdirpath,&newbasepaths);
        if(rv == -1)
          return -errno;
        rv = l::symlink(existingpaths[0],newbasepaths,target_,linkpath_,newdirpath,st_);
      }

    return rv;
  }
}

namespace FUSE
{
  int
  symlink(const char      *target_,
          const char      *linkpath_,
          struct stat     *st_,
          fuse_timeouts_t *timeouts_)
  {
    int rv;
    Config::Read cfg;
    const fuse_context *fc  = fuse_get_context();
    const ugid::Set     ugid(fc->uid,fc->gid);

    rv = l::symlink(cfg->func.getattr.policy,
                    cfg->func.symlink.policy,
                    cfg->branches,
                    target_,
                    linkpath_,
                    st_);

    if(timeouts_ != NULL)
      {
        switch(cfg->follow_symlinks)
          {
          case FollowSymlinks::ENUM::NEVER:
            timeouts_->entry = ((rv >= 0) ?
                                cfg->cache_entry :
                                cfg->cache_negative_entry);
            timeouts_->attr  = cfg->cache_attr;
            break;
          default:
            timeouts_->entry = 0;
            timeouts_->attr  = 0;
            break;
          }
      }

    return rv;
  }
}
