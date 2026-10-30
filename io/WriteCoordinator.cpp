#include "io/WriteCoordinator.h"
#include <QDir>
#include <QFileInfo>

namespace kantan::io {
WriteCoordinator::Ticket WriteCoordinator::reserve(const QString& path) {
    const QFileInfo file(path);
    QString key = file.canonicalFilePath();
    if (key.isEmpty()) {
        auto parent = file.dir().canonicalPath();
        if (parent.isEmpty())
            parent = file.absolutePath();
        key = QDir(parent).filePath(file.fileName());
    }
#ifdef _WIN32
    key = key.toCaseFolded();
#endif
    std::lock_guard guard(mutex_);
    std::erase_if(queues_, [](const auto& pair) { return pair.second.expired(); });
    auto& slot = queues_[key];
    auto queue = slot.lock();
    if (!queue) {
        if (queues_.size() > 64)
            throw std::length_error("Too many pending file destinations");
        queue = std::make_shared<Queue>();
        slot = queue;
    }
    std::lock_guard queueGuard(queue->mutex);
    if (queue->requests.size() >= 8)
        throw std::length_error("Too many queued saves for this file");
    auto request = std::make_shared<Request>();
    queue->requests.push_back(request);
    return Ticket(std::move(queue), std::move(request));
}
WriteCoordinator::Ticket::~Ticket() {
    if (!queue_ || !request_)
        return;
    std::lock_guard lock(queue_->mutex);
    request_->cancelled = true;
    while (!queue_->requests.empty() &&
           (queue_->requests.front()->cancelled || queue_->requests.front()->finished))
        queue_->requests.pop_front();
    queue_->changed.notify_all();
}
Result WriteCoordinator::Ticket::run(const std::function<Result()>& write) {
    std::unique_lock lock(queue_->mutex);
    queue_->changed.wait(lock, [&] {
        return request_->cancelled || request_->finished ||
               (!queue_->requests.empty() && queue_->requests.front() == request_);
    });
    if (request_->cancelled || request_->finished || request_->started)
        return {Status::Cancelled, "File request cancelled"};
    request_->started = true;
    lock.unlock(); // Reserving another ticket must never wait for filesystem work on the UI thread.
    Result result;
    try {
        result = write();
    } catch (const std::exception& e) {
        result = {Status::InvalidData, e.what()};
    }
    lock.lock();
    request_->finished = true;
    while (!queue_->requests.empty() &&
           (queue_->requests.front()->cancelled || queue_->requests.front()->finished))
        queue_->requests.pop_front();
    lock.unlock();
    queue_->changed.notify_all();
    return result;
}
} // namespace kantan::io
