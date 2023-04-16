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
#include "fs_mknod.hpp"
#include "fs_path.hpp"
#include "ugid.hpp"

#include "fuse.h"

#include <string>
#include <vector>


namespace l
{
  static
  inline
  int
  mknod(const std::string &fullpath_,
        mode_t             mode_,
        const mode_t       umask_,
        const dev_t        dev_)
  {
    if(!fs::acl::dir_has_defaults(fullpath_))
      mode_ &= ~umask_;

    return fs::mknod(fullpath_,mode_,dev_);
  }

  static
  int
  mknod(const std::string &createpath_,
        const char        *fusepath_,
        const mode_t       mode_,
        const mode_t       umask_,
        const dev_t        dev_)
  {
    int rv;
    std::string fullpath;

    fullpath = fs::path::make(createpath_,fusepath_);

    rv = l::mknod(fullpath,mode_,umask_,dev_);

    return rv;
  }

  static
  int
  mknod(const std::string &existingpath_,
        const std::string &createpath_,
        const std::string &fusedirpath_,
        const char        *fusepath_,
        const mode_t       mode_,
        const mode_t       umask_,
        const dev_t        dev_)
  {
    int rv;

    rv = fs::clonepath_as_root(existingpath_,createpath_,fusedirpath_);
    if(rv == -1)
      return rv;

    return l::mknod(createpath_,fusepath_,mode_,umask_,dev_);
  }

  static
  int
  mknod(const Policy::Search &searchFunc_,
        const Policy::Create &createFunc_,
        const Branches       &branches_,
        const char           *fusepath_,
        const mode_t          mode_,
        const mode_t          umask_,
        const dev_t           dev_)
  {
    int rv;
    std::string fusedirpath;
    std::string createpath;
    std::string existingpath;

    fusedirpath = fs::path::dirname(fusepath_);

    rv = searchFunc_(branches_,fusedirpath,&existingpath);
    if(rv == -1)
      return -errno;

    rv = createFunc_(branches_,fusedirpath,&createpath);
    if(rv == -1)
      return -errno;

    rv = l::mknod(existingpath,
                  createpath,
                  fusedirpath,
                  fusepath_,
                  mode_,
                  umask_,
                  dev_);

    return rv;
  }
}

namespace FUSE
{
  int
  mknod(const char *fusepath_,
        mode_t      mode_,
        dev_t       rdev_)
  {
    int rv;
    Config::Read cfg;
    const fuse_context *fc = fuse_get_context();
    const ugid::Set     ugid(fc->uid,fc->gid);

    rv = l::mknod(cfg->func.getattr.policy,
                  cfg->func.mknod.policy,
                  cfg->branches,
                  fusepath_,
                  mode_,
                  fc->umask,
                  rdev_);
    if(rv == -EROFS)
      {
        Config::Write()->branches.find_and_set_mode_ro();
        rv = l::mknod(cfg->func.getattr.policy,
                      cfg->func.mknod.policy,
                      cfg->branches,
                      fusepath_,
                      mode_,
                      fc->umask,
                      rdev_);
      }

    return rv;
  }
}
