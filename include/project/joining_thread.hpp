#ifndef PROJECT_JOINING_THREAD_HPP_
#define PROJECT_JOINING_THREAD_HPP_

#include <thread>
#include <utility>

namespace project
{
  class joining_thread
  {
   private:
    std::thread thread_;

   public:
    joining_thread() noexcept = default;
    template<typename Callable, typename... Args>
    explicit joining_thread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func), std::forward<Args>(args)...)
    {
    }
    joining_thread(joining_thread&& other) noexcept : thread_(std::move(other.thread_))
    {
    }
    joining_thread& operator=(joining_thread&& other) noexcept
    {
      if (thread_.joinable())
      {
        thread_.join();
      }
      thread_ = std::move(other.thread_);
      return *this;
    }
    joining_thread(const joining_thread&) = delete;
    joining_thread& operator=(const joining_thread&) = delete;
    ~joining_thread()
    {
      if (thread_.joinable())
      {
        thread_.join();
      }
    }
    void join()
    {
      thread_.join();
    }
    [[nodiscard]] bool joinable() const noexcept
    {
      return thread_.joinable();
    }
    [[nodiscard]] std::thread::id get_id() const noexcept
    {
      return thread_.get_id();
    }
    [[nodiscard]] std::thread::native_handle_type native_handle()
    {
      return thread_.native_handle();
    }
    void swap(joining_thread& other) noexcept
    {
      thread_.swap(other.thread_);
    }
  };
  inline void swap(joining_thread& lhs, joining_thread& rhs) noexcept
  {
    lhs.swap(rhs);
  }
}  

#endif  
