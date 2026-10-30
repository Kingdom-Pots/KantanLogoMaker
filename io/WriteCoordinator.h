#pragma once
#include "base/Types.h"
#include <QString>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>

namespace kantan::io {
// Owned by the host, shared between its document windows. No document globals.
class WriteCoordinator {
    struct Request {
        bool cancelled{}, finished{}, started{};
    };
    struct Queue {
        std::mutex mutex;
        std::condition_variable changed;
        std::deque<std::shared_ptr<Request>> requests;
    };

  public:
    class Ticket {
      public:
        Ticket(std::shared_ptr<Queue> queue, std::shared_ptr<Request> request)
            : queue_(std::move(queue)), request_(std::move(request)) {}
        Ticket(Ticket&&) = default;
        Ticket& operator=(Ticket&&) = delete;
        Ticket(const Ticket&) = delete;
        ~Ticket();
        Result run(const std::function<Result()>& write);

      private:
        std::shared_ptr<Queue> queue_;
        std::shared_ptr<Request> request_;
    };
    Ticket reserve(const QString& path);

  private:
    std::mutex mutex_;
    std::map<QString, std::weak_ptr<Queue>> queues_;
};
} // namespace kantan::io
