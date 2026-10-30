#pragma once
#include "history/History.h"
#include "raster/Image.h"
#include <functional>

namespace kantan {
struct Change {
    std::uint64_t generation{};
    bool structural{};
    std::string label;
};
enum class Arrange { BringToFront, BringForward, SendBackward, SendToBack };
// One document session: committed snapshot, selection, history and the private Working state used by
// drags and slider scrubs. Every mutation is validated and published through one path.
class Editor {
  public:
    explicit Editor(Artboard artboard = {},
                    std::shared_ptr<MemoryBudget> budget = std::make_shared<MemoryBudget>());
    Snapshot snapshot() const {
        return committed_;
    }
    Snapshot preview() const {
        return working_ ? working_->document : committed_;
    }
    std::shared_ptr<MemoryBudget> budget() const {
        return budget_;
    }
    std::uint64_t generation() const {
        return generation_;
    }
    bool dirty() const {
        return committed_->state != savedState_;
    }
    void markSaved(std::uint64_t state) {
        savedState_ = state;
    }
    bool busy() const {
        return static_cast<bool>(working_);
    }
    const Change& lastChange() const {
        return change_;
    }
    bool canUndo() const {
        return cursor_ > 0 && !busy();
    }
    bool canRedo() const {
        return cursor_ < history_.size() && !busy();
    }
    std::string undoLabel() const {
        return canUndo() ? history_[cursor_ - 1].label : std::string{};
    }
    std::string redoLabel() const {
        return canRedo() ? history_[cursor_].label : std::string{};
    }
    // Selection is UI state: it is captured by history entries but changing it creates no entry.
    const std::vector<ObjectId>& selection() const {
        return selection_;
    }
    Result select(std::vector<ObjectId> ids);
    // Ids created by the most recent successful command (new objects, duplicates, groups).
    const std::vector<ObjectId>& created() const {
        return created_;
    }
    Result replace(Snapshot document, bool saved = true);
    // Insert as the topmost child of `parent` (root when empty), or directly above `above` if given.
    // `images` are embedded originals the objects refer to; keys must be new or hold the same bytes.
    Result add(std::vector<Object> objects, ObjectId parent = {}, ObjectId above = {},
               std::map<std::string, EmbeddedImage> images = {});
    Result addFont(std::string key, EmbeddedFont font);
    Result setFontEmbedding(const std::string& key, bool embed);
    Result remove(const std::vector<ObjectId>& ids);
    Result duplicate(const std::vector<ObjectId>& ids, Point offset);
    // Replace existing objects by id (property edits). Locked objects only accept lock/visibility/name.
    Result update(std::string label, std::vector<Object> replacements);
    // Replace whole subtrees (root id kept, descendants dropped): Convert to path, Flatten boolean.
    Result replaceSubtrees(std::string label, std::vector<Object> roots);
    Result group(const std::vector<ObjectId>& ids, ObjectKind kind, BooleanOp op = BooleanOp::Union);
    Result ungroup(const std::vector<ObjectId>& ids);
    Result arrange(const std::vector<ObjectId>& ids, Arrange how);
    // Move objects (with subtrees) into `parent`, placed directly below `before` (topmost when empty).
    Result move(const std::vector<ObjectId>& ids, ObjectId parent, ObjectId before);
    Result setArtboard(Artboard artboard);
    Result setExportSettings(ExportSettings settings);
    Result setPalette(std::vector<PaletteColor> palette);
    // Change one palette colour and recolour every matching style colour (skips locked objects): one step.
    Result editPaletteColor(std::size_t index, Color color);
    // Gesture lifecycle: begin -> preview* -> apply | cancel. One undo step on apply.
    Result begin(std::string label);
    Result preview(std::vector<Object> replacements);
    Result apply();
    void cancel();
    Result undo();
    Result redo();

  private:
    struct Working {
        Snapshot base;
        Snapshot document;
        std::string label;
        std::uint64_t generation{};
    };
    Result commit(std::string label, bool structural, const std::function<Result(DocumentData&)>& edit);
    Result publish(Snapshot before, std::shared_ptr<DocumentData> after, std::string label, bool structural);
    Result applyReplacements(DocumentData& doc, std::vector<Object>& replacements, bool checkLocks) const;
    ObjectId nextId();
    std::shared_ptr<MemoryBudget> budget_;
    Snapshot committed_;
    std::vector<ObjectId> selection_, created_;
    std::unique_ptr<Working> working_;
    std::vector<HistoryEntry> history_;
    std::size_t cursor_{};
    std::uint64_t generation_{1}, nextState_{2}, nextObject_{1}, savedState_{1};
    Change change_;
};
} // namespace kantan
