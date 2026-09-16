#pragma once
// ★ 1.3.6 (2026-09-15) THE TASK RELAY - never AddTask from inside an event sink.
//
// Found by the independent review of 1.3.5 (session ea7f897d, review135.json; the user approved "the
// freeze-risk handoff"). SKSE VR runs every queued task INSIDE its task-queue lock
// (sksevr Hooks_Threads.cpp:17-26) and AddTask takes the same lock (:29-34). A mod-event sink runs while
// the event source holds its own lock for the whole sink loop (CommonLib BSTEvent.h:131-152; the engine's
// copy is inferred to do the same). So a sink that calls AddTask while a task that SENDS a mod event is
// draining takes the two locks in the opposite order from that task = a possible hard freeze with no crash
// log (the VRTE master reference 01 §7.1 family). DD SN's GestureSink had done it since 09-03, and 1.3.5's
// OrgasmSink added it on every DD orgasm.
//
// The fix: a sink only pushes the work into a queue guarded by a mutex of our own (held for a push/pop and
// NEVER while calling anything else) and wakes one worker thread; the worker, which holds no engine lock,
// calls AddTask. The work still runs as an ordinary SKSE task - just from a thread that cannot be part of
// the cycle. Cost: one thread wake (well under a frame).
#include <condition_variable>
#include <deque>
#include <functional>
#include <mutex>
#include <thread>

namespace TaskRelay
{
    namespace detail
    {
        inline std::mutex                        mtx;
        inline std::condition_variable           cv;
        inline std::deque<std::function<void()>> queue;
        inline std::once_flag                    started;

        inline void Worker()
        {
            for (;;) {
                std::function<void()> fn;
                {
                    std::unique_lock lk(mtx);
                    cv.wait(lk, [] { return !queue.empty(); });
                    fn = std::move(queue.front());
                    queue.pop_front();
                }
                // ⚠ outside our mutex: AddTask may block on SKSE's task lock, and nothing may be held then
                if (auto* tasks = SKSE::GetTaskInterface())
                    tasks->AddTask(std::move(fn));
            }
        }
    }

    // Queue `fn` to run as an SKSE task, safely from ANY context - an event sink included.
    inline void Add(std::function<void()> fn)
    {
        std::call_once(detail::started, [] { std::thread(detail::Worker).detach(); });
        {
            std::scoped_lock lk(detail::mtx);
            detail::queue.push_back(std::move(fn));
        }
        detail::cv.notify_one();
    }
}
