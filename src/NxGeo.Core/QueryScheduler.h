#pragma once

#include <atomic>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include "ICaptureSource.h"
#include "INxQuery.h"
#include "SnapshotChannel.h"

// 把捕获到的一串 ObjRef 变成通道里的快照：
//  - 同一 tag 连续出现只查一次
//  - gen 递增，L2 结果回来时若已过期就丢弃（latest-wins）
//  - 空 tag 写出 kind=None 的空快照，浮窗据此隐藏
class QueryScheduler {
public:
    using Task = std::function<void()>;
    using Dispatcher = std::function<void(Task)>;

    // dispatcher 为空时用内置的单工作线程（合并为"只跑最新一个任务"）
    QueryScheduler(ICaptureSource* src, INxQuery* query, SnapshotChannel* channel,
                   Dispatcher dispatcher = Dispatcher());
    ~QueryScheduler();

    QueryScheduler(const QueryScheduler&) = delete;
    QueryScheduler& operator=(const QueryScheduler&) = delete;

    void Start();
    void Stop();

    unsigned long long CurrentGen() const { return gen_.load(); }

private:
    void OnRef(const ObjRef& ref);
    void Dispatch(Task task);
    void WorkerLoop();

    ICaptureSource*                src_;
    INxQuery*                      query_;
    SnapshotChannel*               ch_;
    Dispatcher                     dispatch_;

    std::atomic<unsigned long long> gen_{ 0 };
    std::atomic<unsigned>           lastTag_{ 0 };
    std::mutex                      writeMutex_;
    bool                            started_ = false;

    std::thread                     worker_;
    std::mutex                      qMutex_;
    std::condition_variable         qCv_;
    Task                            pending_;
    bool                            stop_ = false;
};
