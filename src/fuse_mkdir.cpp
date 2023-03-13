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
#include "fs_acl.hpp"
#include "fs_clonepath.hpp"
#include "fs_mkdir.hpp"
#include "fs_path.hpp"
#include "policy.hpp"
#include "syslog.hpp"
#include "ugid.hpp"

#include "fuse.h"

#include <string>

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
  static
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
  mkdir(const string &fullpath_,
        mode_t        mode_,
        const mode_t  umask_)
  {
    if(!fs::acl::dir_has_defaults(fullpath_))
      mode_ &= ~umask_;

    return fs::mkdir(fullpath_,mode_);
  }

  static
  int
  mkdir(const string &createpath_,
        const char   *fusepath_,
        const mode_t  mode_,
        const mode_t  umask_)
  {
    string fullpath;

    fullpath = fs::path::make(createpath_,fusepath_);

    return l::mkdir(fullpath,mode_,umask_);
  }

  static
  int
  mkdir(const std::string &existingpath_,
        const std::string &createpath_,
        const std::string &fusedirpath_,
        const char        *fusepath_,
        const mode_t       mode_,
        const mode_t       umask_)
  {
    int rv;

    rv = fs::clonepath_as_root(existingpath_,createpath_,fusedirpath_);
    if(rv == -1)
      return rv;

    return l::mkdir(createpath_,fusepath_,mode_,umask_);
  }

  static
  int
  mkdir(const string &existingpath_,
        const StrVec &createpaths_,
        const string &fusedirpath_,
        const char   *fusepath_,
        const mode_t  mode_,
        const mode_t  umask_)
  {
    int rv;
    int error;

    error = -1;
    for(auto const &createpath : createpaths_)
      {
        rv = l::mkdir(existingpath_,createpath,fusedirpath_,fusepath_,mode_,umask_);
        if((rv == -1) && (errno == EROFS))
          l::set_branches_mode_to_ro(createpath);

        error = error::calc(rv,error,errno);
      }

    return -error;
  }

  static
  int
  mkdir(const Policy::Search &getattrPolicy_,
        const Policy::Create &mkdirPolicy_,
        const Branches       &branches_,
        const char           *fusepath_,
        const mode_t          mode_,
        const mode_t          umask_)
  {
    int rv;
    string fusedirpath;
    StrVec createpaths;
    StrVec existingpaths;

    fusedirpath = fs::path::dirname(fusepath_);

    rv = getattrPolicy_(branches_,fusedirpath,&existingpaths);
    if(rv == -1)
      return -errno;

    rv = mkdirPolicy_(branches_,fusedirpath,&createpaths);
    if(rv == -1)
      return -errno;

    rv = l::mkdir(existingpaths[0],createpaths,fusedirpath,fusepath_,mode_,umask_);
    if(rv == -EROFS)
      {
        createpaths.clear();
        rv = mkdirPolicy_(branches_,fusedirpath,&createpaths);
        if(rv == -1)
          return -errno;
        rv = l::mkdir(existingpaths[0],createpaths,fusedirpath,fusepath_,mode_,umask_);
      }

    return rv;
  }
}

namespace FUSE
{
  int
  mkdir(const char *fusepath_,
        mode_t      mode_)
  {
    Config::Read cfg;
    const fuse_context *fc = fuse_get_context();
    const ugid::Set     ugid(fc->uid,fc->gid);

    return l::mkdir(cfg->func.getattr.policy,
                    cfg->func.mkdir.policy,
                    cfg->branches,
                    fusepath_,
                    mode_,
                    fc->umask);
  }
}
