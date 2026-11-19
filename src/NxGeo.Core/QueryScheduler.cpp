#include "QueryScheduler.h"

#include "ObjKind.h"

QueryScheduler::QueryScheduler(ICaptureSource* src, INxQuery* query, SnapshotChannel* channel,
                               Dispatcher dispatcher)
    : src_(src), query_(query), ch_(channel) {
    if (dispatcher) {
        dispatch_ = std::move(dispatcher);
        return;
    }

    worker_ = std::thread([this] { WorkerLoop(); });
    dispatch_ = [this](Task task) {
        {
            std::lock_guard<std::mutex> lock(qMutex_);
            pending_ = std::move(task);   // 只保留最新一个
        }
        qCv_.notify_one();
    };
}

QueryScheduler::~QueryScheduler() {
    Stop();
}

void QueryScheduler::Start() {
    if (started_) return;
    started_ = true;
    src_->Start([this](const ObjRef& ref) { OnRef(ref); });
}

void QueryScheduler::Stop() {
    if (started_) {
        src_->Stop();
        started_ = false;
    }
    {
        std::lock_guard<std::mutex> lock(qMutex_);
        stop_ = true;
    }
    qCv_.notify_all();
    if (worker_.joinable()) worker_.join();
}

void QueryScheduler::WorkerLoop() {
    for (;;) {
        Task task;
        {
            std::unique_lock<std::mutex> lock(qMutex_);
            qCv_.wait(lock, [this] { return stop_ || static_cast<bool>(pending_); });
            if (stop_ && !pending_) return;
            task = std::move(pending_);
            pending_ = nullptr;
        }
        if (task) task();
    }
}

void QueryScheduler::Dispatch(Task task) {
    if (dispatch_) dispatch_(std::move(task));
}

void QueryScheduler::OnRef(const ObjRef& ref) {
    const unsigned long long gen = ++gen_;

    if (ref.tag == 0) {
        InfoSnapshot empty{};
        empty.kind = static_cast<unsigned short>(ObjKind::None);
        std::lock_guard<std::mutex> lock(writeMutex_);
        ch_->Write(empty);
        lastTag_.store(0);
        return;
    }

    if (ref.tag == lastTag_.load()) return;   // 同一对象连续出现：不重查
    lastTag_.store(ref.tag);

    InfoSnapshot s = query_->QueryL0(ref);
    ObjRef resolved = ref;
    resolved.kind = static_cast<ObjKind>(s.kind);   // L0 查出的真实类型，L1/L2 要用
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        ch_->Write(s);
    }

    query_->FillL1(resolved, s);
    {
        std::lock_guard<std::mutex> lock(writeMutex_);
        ch_->Write(s);
    }

    Dispatch([this, resolved, s, gen]() mutable {
        InfoSnapshot filled = s;
        query_->FillL2(resolved, filled);
        if (gen != gen_.load()) return;   // 早就过期了，直接丢
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (gen != gen_.load()) return;
        ch_->Write(filled);
    });
}
