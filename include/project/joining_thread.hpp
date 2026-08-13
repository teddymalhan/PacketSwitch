/**
 * @file joining_thread.hpp
 * @brief RAII wrapper for std::thread that automatically joins on destruction
 *
 * This class follows the C++ Core Guidelines pattern for thread management,
 * ensuring threads are always joined and never left detached.
 */

#ifndef PROJECT_JOINING_THREAD_HPP_
#define PROJECT_JOINING_THREAD_HPP_

#include <thread>
#include <utility>

namespace project
{
  /**
   * @brief RAII wrapper for std::thread that joins automatically on destruction
   *
   * Unlike std::thread, joining_thread will automatically join the thread
   * when the object is destroyed, preventing the common error of forgetting
   * to join or detaching threads. This follows the RAII principle and ensures
   * proper resource cleanup.
   *
   * Example usage:
   * @code
   * void worker_function() { }
   *
   * void example()
   * {
   *   joining_thread t(worker_function);
   *   // Thread automatically joins when t goes out of scope
   * }
   * @endcode
   */
  class joining_thread
  {
   private:
    std::thread thread_;

   public:
    /**
     * @brief Default constructor - creates a non-joinable thread
     */
    joining_thread() noexcept = default;

    /**
     * @brief Constructs a thread from a callable and arguments
     *
     * @tparam Callable The type of the callable object
     * @tparam Args The types of arguments to pass to the callable
     * @param func The callable to execute in the thread
     * @param args Arguments to forward to the callable
     */
    template<typename Callable, typename... Args>
    explicit joining_thread(Callable&& func, Args&&... args)
        : thread_(std::forward<Callable>(func), std::forward<Args>(args)...)
    {
    }

    /**
     * @brief Move constructor
     *
     * @param other The thread to move from
     */
    joining_thread(joining_thread&& other) noexcept : thread_(std::move(other.thread_))
    {
    }

    /**
     * @brief Move assignment operator
     *
     * Joins the current thread (if joinable) before moving
     *
     * @param other The thread to move from
     * @return Reference to this object
     */
    joining_thread& operator=(joining_thread&& other) noexcept
    {
      if (thread_.joinable())
      {
        thread_.join();
      }
      thread_ = std::move(other.thread_);
      return *this;
    }

    /**
     * @brief Deleted copy constructor - threads cannot be copied
     */
    joining_thread(const joining_thread&) = delete;

    /**
     * @brief Deleted copy assignment - threads cannot be copied
     */
    joining_thread& operator=(const joining_thread&) = delete;

    /**
     * @brief Destructor - automatically joins the thread if joinable
     */
    ~joining_thread()
    {
      if (thread_.joinable())
      {
        thread_.join();
      }
    }

    /**
     * @brief Explicitly join the thread
     *
     * Waits for the thread to finish execution. The thread must be joinable.
     */
    void join()
    {
      thread_.join();
    }

    /**
     * @brief Check if the thread is joinable
     *
     * @return true if the thread is joinable, false otherwise
     */
    [[nodiscard]] bool joinable() const noexcept
    {
      return thread_.joinable();
    }

    /**
     * @brief Get the thread ID
     *
     * @return The thread ID
     */
    [[nodiscard]] std::thread::id get_id() const noexcept
    {
      return thread_.get_id();
    }

    /**
     * @brief Get the native thread handle
     *
     * @return The native thread handle
     */
    [[nodiscard]] std::thread::native_handle_type native_handle()
    {
      return thread_.native_handle();
    }

    /**
     * @brief Swap this thread with another
     *
     * @param other The thread to swap with
     */
    void swap(joining_thread& other) noexcept
    {
      thread_.swap(other.thread_);
    }
  };

  /**
   * @brief Swap two joining_threads
   *
   * @param lhs The first thread
   * @param rhs The second thread
   */
  inline void swap(joining_thread& lhs, joining_thread& rhs) noexcept
  {
    lhs.swap(rhs);
  }

}  // namespace project

#endif  // PROJECT_JOINING_THREAD_HPP_
