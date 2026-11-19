#include "QueryScheduler.h"

#include <cstring>
#include <exception>

#include "ObjKind.h"

namespace {

void SetNote(InfoSnapshot& s, const char* text) {
    std::strncpy(s.note, text, sizeof s.note - 1);
    s.note[sizeof s.note - 1] = '\0';
}

}  // namespace

QueryScheduler::QueryScheduler(ICaptureSource* src, INxQuery* query, SnapshotChannel* channel,
                               Dispatcher dispatcher)
    : src_(src), query_(query), ch_(channel) {
    if (dispatcher) {
        dispatch_ = std::move(dispatcher);
        ownWorker_ = false;
        return;
    }

    ownWorker_ = true;
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

    if (ownWorker_) {
        {
            std::lock_guard<std::mutex> lock(qMutex_);
            stop_ = false;
        }
        worker_ = std::thread([this] { WorkerLoop(); });
    }

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

void QueryScheduler::WriteSnapshot(const InfoSnapshot& snapshot) {
    std::lock_guard<std::mutex> lock(writeMutex_);
    ch_->Write(snapshot);
}

void QueryScheduler::OnRef(const ObjRef& ref) {
    if (ref.tag == 0) {
        ++gen_;   // 取消选择：让在途的 L2 结果作废
        InfoSnapshot empty{};
        empty.kind = static_cast<unsigned short>(ObjKind::None);
        WriteSnapshot(empty);
        lastTag_.store(0);
        return;
    }

    // 选择没变：不动 gen，否则 50ms 一次的空轮询会把在途的 L2 结果全部作废
    if (ref.tag == lastTag_.load()) return;

    const unsigned long long gen = ++gen_;
    const unsigned previous = lastTag_.exchange(ref.tag);
    if (previous != 0 && query_) query_->SetDistanceTarget(previous);

    InfoSnapshot s{};
    try {
        s = query_->QueryL0(ref);
    } catch (const std::exception& e) {
        s = InfoSnapshot{};
        s.tag = ref.tag;
        SetNote(s, e.what());
        WriteSnapshot(s);
        return;
    } catch (...) {
        s = InfoSnapshot{};
        s.tag = ref.tag;
        SetNote(s, "L0 查询异常");
        WriteSnapshot(s);
        return;
    }

    ObjRef resolved = ref;
    resolved.kind = static_cast<ObjKind>(s.kind);   // L0 查出的真实类型，L1/L2 要用
    WriteSnapshot(s);

    try {
        query_->FillL1(resolved, s);
    } catch (const std::exception& e) {
        SetNote(s, e.what());
    } catch (...) {
        SetNote(s, "L1 查询异常");
    }
    WriteSnapshot(s);

    Dispatch([this, resolved, s, gen]() mutable {
        InfoSnapshot filled = s;
        try {
            query_->FillL2(resolved, filled);
        } catch (const std::exception& e) {
            SetNote(filled, e.what());
        } catch (...) {
            SetNote(filled, "L2 查询异常");
        }
        if (gen != gen_.load()) return;   // 早就过期了，直接丢
        std::lock_guard<std::mutex> lock(writeMutex_);
        if (gen != gen_.load()) return;
        ch_->Write(filled);
    });
}
