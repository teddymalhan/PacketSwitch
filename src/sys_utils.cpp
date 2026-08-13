/**
 * @file sys_utils.cpp
 * @brief Implementation of system utilities
 */

#include "project/sys_utils.hpp"

#include <unistd.h>

namespace project
{
  FileDescriptor::~FileDescriptor()
  {
    close();
  }

  void FileDescriptor::close() noexcept
  {
    if (is_valid())
    {
      ::close(fd_);
      fd_ = -1;
    }
  }

}  // namespace project
