#include "project/sys_utils.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <windows.h>
#else
#include <unistd.h>
#endif

namespace project
{
  FileDescriptor::~FileDescriptor() { close(); }

  void FileDescriptor::close() noexcept
  {
    if (!is_valid()) return;
#ifdef _WIN32
    ::CloseHandle(reinterpret_cast<HANDLE>(fd_));
#else
    ::close(static_cast<int>(fd_));
#endif
    fd_ = -1;
  }

  SocketHandle::SocketHandle() noexcept
#ifdef _WIN32
      : socket_(static_cast<native_socket_handle>(INVALID_SOCKET))
#else
      : socket_(-1)
#endif
  {
  }

  SocketHandle::~SocketHandle() { close(); }

  bool SocketHandle::is_valid() const noexcept
  {
#ifdef _WIN32
    return socket_ != static_cast<native_socket_handle>(INVALID_SOCKET);
#else
    return socket_ >= 0;
#endif
  }

  void SocketHandle::close() noexcept
  {
    if (!is_valid()) return;
#ifdef _WIN32
    ::closesocket(static_cast<SOCKET>(socket_));
    socket_ = static_cast<native_socket_handle>(INVALID_SOCKET);
#else
    ::close(socket_);
    socket_ = -1;
#endif
  }

  native_socket_handle SocketHandle::release() noexcept
  {
    const auto socket = socket_;
#ifdef _WIN32
    socket_ = static_cast<native_socket_handle>(INVALID_SOCKET);
#else
    socket_ = -1;
#endif
    return socket;
  }
}
