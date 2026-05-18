/**
 * @file sys_utils_test.cpp
 * @brief Unit tests for system utilities
 */

#include "project/sys_utils.hpp"

#include <gtest/gtest.h>

#include <fcntl.h>
#include <unistd.h>

using namespace project;

TEST(FileDescriptorTest, DefaultConstruction)
{
  FileDescriptor fd;
  EXPECT_FALSE(fd.is_valid());
  EXPECT_EQ(fd.get(), -1);
  EXPECT_FALSE(static_cast<bool>(fd));
}

TEST(FileDescriptorTest, ConstructWithValidDescriptor)
{
  int pipe_fds[2];
  ASSERT_EQ(pipe(pipe_fds), 0);

  {
    FileDescriptor fd(pipe_fds[0]);
    EXPECT_TRUE(fd.is_valid());
    EXPECT_EQ(fd.get(), pipe_fds[0]);
    EXPECT_TRUE(static_cast<bool>(fd));
  }  // fd closes here

  // Try to close again to verify it was closed
  EXPECT_EQ(::close(pipe_fds[0]), -1);  // Should fail because already closed
  ::close(pipe_fds[1]);                 // Clean up the other end
}

TEST(FileDescriptorTest, MoveConstruction)
{
  int pipe_fds[2];
  ASSERT_EQ(pipe(pipe_fds), 0);

  FileDescriptor fd1(pipe_fds[0]);
  int original_fd = fd1.get();

  FileDescriptor fd2(std::move(fd1));

  EXPECT_FALSE(fd1.is_valid());
  EXPECT_EQ(fd1.get(), -1);
  EXPECT_TRUE(fd2.is_valid());
  EXPECT_EQ(fd2.get(), original_fd);

  ::close(pipe_fds[1]);
}

TEST(FileDescriptorTest, MoveAssignment)
{
  int pipe_fds1[2];
  int pipe_fds2[2];
  ASSERT_EQ(pipe(pipe_fds1), 0);
  ASSERT_EQ(pipe(pipe_fds2), 0);

  FileDescriptor fd1(pipe_fds1[0]);
  FileDescriptor fd2(pipe_fds2[0]);

  int original_fd1 = fd1.get();

  fd2 = std::move(fd1);

  EXPECT_FALSE(fd1.is_valid());
  EXPECT_TRUE(fd2.is_valid());
  EXPECT_EQ(fd2.get(), original_fd1);

  // pipe_fds2[0] should have been closed by the assignment
  EXPECT_EQ(::close(pipe_fds2[0]), -1);

  ::close(pipe_fds1[1]);
  ::close(pipe_fds2[1]);
}

TEST(FileDescriptorTest, ExplicitClose)
{
  int pipe_fds[2];
  ASSERT_EQ(pipe(pipe_fds), 0);

  FileDescriptor fd(pipe_fds[0]);
  EXPECT_TRUE(fd.is_valid());

  fd.close();
  EXPECT_FALSE(fd.is_valid());
  EXPECT_EQ(fd.get(), -1);

  // Closing again should be safe
  fd.close();
  EXPECT_FALSE(fd.is_valid());

  ::close(pipe_fds[1]);
}

TEST(FileDescriptorTest, Release)
{
  int pipe_fds[2];
  ASSERT_EQ(pipe(pipe_fds), 0);

  FileDescriptor fd(pipe_fds[0]);
  int released_fd = fd.release();

  EXPECT_EQ(released_fd, pipe_fds[0]);
  EXPECT_FALSE(fd.is_valid());
  EXPECT_EQ(fd.get(), -1);

  // Now we're responsible for closing it
  EXPECT_EQ(::close(released_fd), 0);
  ::close(pipe_fds[1]);
}

TEST(FileDescriptorTest, Reset)
{
  int pipe_fds1[2];
  int pipe_fds2[2];
  ASSERT_EQ(pipe(pipe_fds1), 0);
  ASSERT_EQ(pipe(pipe_fds2), 0);

  FileDescriptor fd(pipe_fds1[0]);
  EXPECT_EQ(fd.get(), pipe_fds1[0]);

  fd.reset(pipe_fds2[0]);
  EXPECT_EQ(fd.get(), pipe_fds2[0]);

  // pipe_fds1[0] should have been closed
  EXPECT_EQ(::close(pipe_fds1[0]), -1);

  ::close(pipe_fds1[1]);
  ::close(pipe_fds2[1]);
}

TEST(FileDescriptorTest, ResetToInvalid)
{
  int pipe_fds[2];
  ASSERT_EQ(pipe(pipe_fds), 0);

  FileDescriptor fd(pipe_fds[0]);
  EXPECT_TRUE(fd.is_valid());

  fd.reset();
  EXPECT_FALSE(fd.is_valid());
  EXPECT_EQ(fd.get(), -1);

  ::close(pipe_fds[1]);
}

TEST(SocketHandleTest, DefaultConstruction)
{
  SocketHandle sock;
  EXPECT_FALSE(sock.is_valid());
  EXPECT_EQ(sock.get(), -1);
  EXPECT_FALSE(static_cast<bool>(sock));
}

TEST(SocketHandleTest, ConstructWithValidSocket)
{
  int pipe_fds[2];
  ASSERT_EQ(pipe(pipe_fds), 0);

  {
    SocketHandle sock(pipe_fds[0]);
    EXPECT_TRUE(sock.is_valid());
    EXPECT_EQ(sock.get(), pipe_fds[0]);
    EXPECT_TRUE(static_cast<bool>(sock));
  }  // sock closes here

  EXPECT_EQ(::close(pipe_fds[0]), -1);
  ::close(pipe_fds[1]);
}

TEST(SocketHandleTest, MoveSemantics)
{
  int pipe_fds[2];
  ASSERT_EQ(pipe(pipe_fds), 0);

  SocketHandle sock1(pipe_fds[0]);
  int original_fd = sock1.get();

  SocketHandle sock2(std::move(sock1));

  EXPECT_FALSE(sock1.is_valid());
  EXPECT_TRUE(sock2.is_valid());
  EXPECT_EQ(sock2.get(), original_fd);

  ::close(pipe_fds[1]);
}

TEST(SocketHandleTest, Release)
{
  int pipe_fds[2];
  ASSERT_EQ(pipe(pipe_fds), 0);

  SocketHandle sock(pipe_fds[0]);
  int released_fd = sock.release();

  EXPECT_EQ(released_fd, pipe_fds[0]);
  EXPECT_FALSE(sock.is_valid());

  EXPECT_EQ(::close(released_fd), 0);
  ::close(pipe_fds[1]);
}

TEST(SocketHandleTest, Reset)
{
  int pipe_fds1[2];
  int pipe_fds2[2];
  ASSERT_EQ(pipe(pipe_fds1), 0);
  ASSERT_EQ(pipe(pipe_fds2), 0);

  SocketHandle sock(pipe_fds1[0]);
  sock.reset(pipe_fds2[0]);

  EXPECT_EQ(sock.get(), pipe_fds2[0]);
  EXPECT_EQ(::close(pipe_fds1[0]), -1);

  ::close(pipe_fds1[1]);
  ::close(pipe_fds2[1]);
}

TEST(SystemExceptionTest, Construction)
{
  SystemException ex("Test error", 42);
  EXPECT_STREQ(ex.what(), "Test error");
  EXPECT_EQ(ex.error_code(), 42);
}

TEST(NetworkExceptionTest, Construction)
{
  NetworkException ex("Network error", 100);
  EXPECT_STREQ(ex.what(), "Network error");
  EXPECT_EQ(ex.error_code(), 100);
}

TEST(NetworkExceptionTest, IsSystemException)
{
  NetworkException ex("Network error", 100);
  EXPECT_NO_THROW({
    SystemException& base = ex;
    EXPECT_EQ(base.error_code(), 100);
  });
}

TEST(FileExceptionTest, Construction)
{
  FileException ex("File error", 200);
  EXPECT_STREQ(ex.what(), "File error");
  EXPECT_EQ(ex.error_code(), 200);
}

int main(int argc, char** argv)
{
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

