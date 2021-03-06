#ifndef _NET_TIMER_LIST_H
#define _NET_TIMER_LIST_H

#include <functional>
#include <list>
#include <system_error>
#include "net/asio.h"

namespace net {

// A timer list is a timer and a list to support scheduling multiple work items
// with the same duration.
//
// The provided executor must be single-threaded, and all functions must be
// called in the executor thread.
template <
    typename ClockT,
    typename WaitTraitsT = wait_traits<ClockT>,
    typename ExecutorT = executor>
class BasicTimerList {
public:
    using Timer = basic_waitable_timer<ClockT, WaitTraitsT, ExecutorT>;
    using Duration = typename ClockT::duration;
    using TimePoint = typename ClockT::time_point;

    BasicTimerList(const ExecutorT &executor, const Duration &duration);

private:
    struct Entry;

public:
    using Handle = typename std::list<Entry>::iterator;

    // The callback will be executed in the provided executor. After the
    // callback is executed, the handle is invalidated.
    Handle schedule(std::function<void()> callback);
    void update(Handle handle);
    void cancel(Handle handle);
    Handle null_handle() { return list_.end(); }

private:
    struct Entry {
        TimePoint expiry;
        std::function<void()> callback;
    };

    void wait();

    Duration duration_;
    Timer timer_;
    std::list<Entry> list_;
};

using TimerList = BasicTimerList<std::chrono::steady_clock>;

template <typename ClockT, typename WaitTraitsT, typename ExecutorT>
BasicTimerList<ClockT, WaitTraitsT, ExecutorT>::BasicTimerList(
    const ExecutorT &executor, const Duration &duration)
    : duration_(duration), timer_(executor) {}

template <typename ClockT, typename WaitTraitsT, typename ExecutorT>
typename BasicTimerList<ClockT, WaitTraitsT, ExecutorT>::Handle
BasicTimerList<ClockT, WaitTraitsT, ExecutorT>::schedule(
    std::function<void()> callback) {
    list_.push_back({ClockT::now() + duration_, std::move(callback)});
    Handle handle = list_.end();
    --handle;
    if (list_.size() == 1) {
        wait();
    }
    return handle;
}

template <typename ClockT, typename WaitTraitsT, typename ExecutorT>
void BasicTimerList<ClockT, WaitTraitsT, ExecutorT>::update(Handle handle) {
    handle->expiry = ClockT::now() + duration_;
    list_.splice(list_.end(), list_, handle);
}

template <typename ClockT, typename WaitTraitsT, typename ExecutorT>
void BasicTimerList<ClockT, WaitTraitsT, ExecutorT>::cancel(Handle handle) {
    list_.erase(handle);
    if (list_.empty()) {
        timer_.cancel();
    }
}

template <typename ClockT, typename WaitTraitsT, typename ExecutorT>
void BasicTimerList<ClockT, WaitTraitsT, ExecutorT>::wait() {
    timer_.expires_at(list_.front().expiry);
    timer_.async_wait([this](std::error_code ec) {
        if (ec) {
            // TODO(iceboy): Error handling.
            return;
        }
        TimePoint now = ClockT::now();
        while (!list_.empty() && list_.front().expiry <= now) {
            list_.front().callback();
            list_.pop_front();
        }
        if (!list_.empty()) {
            wait();
        }
    });
}

}  // namespace net

#endif  // _NET_TIMER_LIST_H
