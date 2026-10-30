#pragma once
#include "base/Types.h"
#include <atomic>
#include <memory>
#include <vector>

namespace kantan {
// Process-wide accounting for raster allocations: render targets, effect scratch, caches and images.
class MemoryBudget {
  public:
    explicit MemoryBudget(std::size_t limit = 1024U * 1024U * 1024U) : limit_(limit) {}
    void reserve(std::size_t bytes) {
        auto used = used_.load();
        do {
            if (bytes > limit_ || used > limit_ - bytes)
                throw std::length_error("Memory budget reached; close another document or export smaller");
        } while (!used_.compare_exchange_weak(used, used + bytes));
    }
    void release(std::size_t bytes) noexcept {
        used_.fetch_sub(bytes);
    }
    std::size_t used() const {
        return used_.load();
    }
    std::size_t limit() const {
        return limit_;
    }

  private:
    std::atomic<std::size_t> used_{};
    std::size_t limit_;
};
class Reservation {
  public:
    Reservation(std::shared_ptr<MemoryBudget> budget, std::size_t bytes)
        : budget_(std::move(budget)), bytes_(bytes) {
        budget_->reserve(bytes_);
    }
    ~Reservation() {
        budget_->release(bytes_);
    }
    Reservation(const Reservation&) = delete;
    Reservation& operator=(const Reservation&) = delete;

  private:
    std::shared_ptr<MemoryBudget> budget_;
    std::size_t bytes_;
};
// Plain owned premultiplied RGBA8 buffer, rows top to bottom, packed stride. Counted against a budget.
struct Image {
    Coord width{}, height{};
    std::shared_ptr<Reservation> memory;
    std::vector<Pixel> pixels;
    static Image make(Coord width, Coord height, const std::shared_ptr<MemoryBudget>& budget) {
        const auto count = checkedArea(width, height);
        Image image{width, height, std::make_shared<Reservation>(budget, count * sizeof(Pixel)), {}};
        image.pixels.assign(count, Pixel{});
        return image;
    }
    bool empty() const {
        return width <= 0 || height <= 0;
    }
    Pixel at(Coord x, Coord y) const {
        return x >= 0 && y >= 0 && x < width && y < height ? pixels[index(x, y)] : Pixel{};
    }
    Pixel& ref(Coord x, Coord y) {
        return pixels[index(x, y)];
    }
    std::size_t index(Coord x, Coord y) const {
        return static_cast<std::size_t>(y) * static_cast<std::size_t>(width) + static_cast<std::size_t>(x);
    }
};
using ImageRef = std::shared_ptr<const Image>;
// Single-channel 8-bit coverage, same layout as Image.
struct Coverage {
    Coord width{}, height{};
    std::shared_ptr<Reservation> memory;
    std::vector<std::uint8_t> values;
    static Coverage make(Coord width, Coord height, const std::shared_ptr<MemoryBudget>& budget) {
        const auto count = checkedArea(width, height);
        Coverage c{width, height, std::make_shared<Reservation>(budget, count), {}};
        c.values.assign(count, 0);
        return c;
    }
    std::uint8_t at(Coord x, Coord y) const {
        return x >= 0 && y >= 0 && x < width && y < height
                   ? values[static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                            static_cast<std::size_t>(x)]
                   : 0;
    }
};
} // namespace kantan
