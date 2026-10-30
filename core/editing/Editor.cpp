#include "editing/Editor.h"
#include "editing/Colors.h"
#include "editing/Transforms.h"
#include <set>
#include <unordered_map>

namespace kantan {
namespace {
using SiblingMap = std::unordered_map<std::uint64_t, std::vector<ObjectRef>>;
SiblingMap siblings(const DocumentData& doc) {
    SiblingMap map;
    for (const auto& o : doc.objects)
        map[o->parent.value].push_back(o);
    return map;
}
void rebuild(DocumentData& doc, const SiblingMap& map) {
    std::vector<ObjectRef> ordered;
    ordered.reserve(doc.objects.size());
    const auto visit = [&](auto&& self, std::uint64_t parent) -> void {
        const auto found = map.find(parent);
        if (found == map.end())
            return;
        for (const auto& o : found->second) {
            ordered.push_back(o);
            self(self, o->id.value);
        }
    };
    visit(visit, 0);
    doc.objects = std::move(ordered);
}
std::set<std::uint64_t> subtreeIds(const DocumentData& doc, const std::vector<ObjectId>& roots) {
    std::set<std::uint64_t> ids;
    for (const auto root : roots) {
        const auto i = doc.indexOf(root);
        for (auto j = i; j < doc.subtreeEnd(i); ++j)
            ids.insert(doc.objects[j]->id.value);
    }
    return ids;
}
// Keep only ids whose ancestors are not also listed (acting on a group acts on its subtree).
std::vector<ObjectId> topmost(const DocumentData& doc, const std::vector<ObjectId>& ids) {
    std::vector<ObjectId> out;
    for (const auto id : ids) {
        if (!doc.find(id) || std::find(out.begin(), out.end(), id) != out.end())
            continue;
        bool nested = false;
        for (const auto other : ids)
            nested = nested || (other != id && doc.isAncestor(other, id));
        if (!nested)
            out.push_back(id);
    }
    // Document order (bottom to top).
    std::sort(out.begin(), out.end(),
              [&](ObjectId a, ObjectId b) { return doc.indexOf(a) < doc.indexOf(b); });
    return out;
}
bool onlyLockFieldsDiffer(Object a, Object b) {
    a.locked = b.locked;
    a.visible = b.visible;
    a.name = b.name;
    return a == b;
}
const char* containerName(ObjectKind kind, BooleanOp op) {
    if (kind == ObjectKind::Clip)
        return "Clip group";
    if (kind == ObjectKind::Group)
        return "Group";
    static const char* names[] = {"Union", "Subtract", "Intersect", "Exclude"};
    return names[static_cast<int>(op)];
}
} // namespace
Editor::Editor(Artboard artboard, std::shared_ptr<MemoryBudget> budget) : budget_(std::move(budget)) {
    auto doc = std::make_shared<DocumentData>();
    doc->artboard = std::move(artboard);
    committed_ = doc;
}
ObjectId Editor::nextId() {
    return {nextObject_++};
}
Result Editor::select(std::vector<ObjectId> ids) {
    const auto doc = preview();
    std::vector<ObjectId> valid;
    for (const auto id : ids)
        if (doc->find(id) && std::find(valid.begin(), valid.end(), id) == valid.end())
            valid.push_back(id);
    if (valid == selection_)
        return {Status::NoChange, {}};
    selection_ = std::move(valid);
    return {};
}
Result Editor::replace(Snapshot doc, bool saved) {
    if (busy())
        return {Status::Stale, "Finish the current edit first"};
    if (!doc)
        return {Status::InvalidData, "No document"};
    if (auto r = validateDocument(*doc); !r)
        return r;
    std::uint64_t maxId = 0;
    for (const auto& o : doc->objects)
        maxId = std::max(maxId, o->id.value);
    nextObject_ = maxId + 1;
    auto copy = std::make_shared<DocumentData>(*doc);
    copy->state = nextState_++;
    committed_ = copy;
    history_.clear();
    cursor_ = 0;
    selection_.clear();
    created_.clear();
    savedState_ = saved ? copy->state : 0;
    change_ = {++generation_, true, "Open"};
    return {};
}
Result Editor::publish(Snapshot before, std::shared_ptr<DocumentData> after, std::string label,
                       bool structural) {
    if (auto r = validateDocument(*after); !r)
        return r;
    after->state = nextState_++;
    auto selectionBefore = selection_;
    std::erase_if(selection_, [&](ObjectId id) { return !after->find(id); });
    history_.resize(cursor_);
    history_.push_back({label, std::move(before), after, std::move(selectionBefore), selection_});
    if (history_.size() > HistoryLimit)
        history_.erase(history_.begin());
    cursor_ = history_.size();
    committed_ = std::move(after);
    change_ = {++generation_, structural, std::move(label)};
    return {};
}
Result Editor::commit(std::string label, bool structural, const std::function<Result(DocumentData&)>& edit) {
    if (busy())
        return {Status::Stale, "Finish the current edit first"};
    created_.clear();
    auto copy = std::make_shared<DocumentData>(*committed_);
    Result r;
    try {
        r = edit(*copy);
    } catch (const std::exception& e) {
        return {Status::ResourceLimit, e.what()};
    }
    if (!r || r.status == Status::NoChange) {
        created_.clear();
        return r;
    }
    if (auto p = publish(committed_, std::move(copy), std::move(label), structural); !p) {
        created_.clear();
        return p;
    }
    return r;
}
Result Editor::add(std::vector<Object> objects, ObjectId parent, ObjectId above,
                   std::map<std::string, EmbeddedImage> images) {
    if (objects.empty())
        return {Status::NoChange, {}};
    return commit(objects.size() == 1 ? std::string("Add ") + kindName(objects[0].kind())
                                      : std::string("Add objects"),
                  true, [&](DocumentData& doc) -> Result {
                      if (parent) {
                          const auto* p = doc.find(parent);
                          if (!p || !p->container())
                              return {Status::InvalidTarget, "Objects can only be added to a group"};
                          if (doc.effectiveLocked(parent))
                              return {Status::InvalidTarget, "The group is locked"};
                      }
                      if (doc.objects.size() + objects.size() > MaxObjects)
                          return {Status::ResourceLimit, "Too many objects"};
                      for (auto& [key, image] : images) {
                          if (!image.pixels || image.pixels->empty())
                              return {Status::InvalidData, "Image has no pixels"};
                          if (const auto found = doc.images.find(key); found != doc.images.end()) {
                              const bool same = found->second.encoded && image.encoded &&
                                                *found->second.encoded == *image.encoded;
                              if (!same)
                                  return {Status::InvalidData, "Image key already exists"};
                              continue;
                          }
                          doc.images[key] = image;
                      }
                      // Remap ids so internal parent links survive; others attach to `parent`.
                      std::unordered_map<std::uint64_t, ObjectId> remap;
                      for (auto& o : objects) {
                          const auto id = nextId();
                          if (o.id)
                              remap[o.id.value] = id;
                          o.id = id;
                      }
                      std::vector<ObjectRef> inserted;
                      for (auto& o : objects) {
                          const auto found = o.parent ? remap.find(o.parent.value) : remap.end();
                          o.parent = found != remap.end() ? found->second : parent;
                          if (o.parent == parent)
                              created_.push_back(o.id);
                          inserted.push_back(std::make_shared<const Object>(o));
                      }
                      std::size_t at;
                      if (above) {
                          const auto i = doc.indexOf(above);
                          if (i >= doc.objects.size() || doc.objects[i]->parent != parent)
                              return {Status::InvalidTarget, "Insertion point is not in the target group"};
                          at = doc.subtreeEnd(i);
                      } else
                          at = parent ? doc.subtreeEnd(doc.indexOf(parent)) : doc.objects.size();
                      doc.objects.insert(doc.objects.begin() + static_cast<std::ptrdiff_t>(at),
                                         inserted.begin(), inserted.end());
                      normalizeOrder(doc);
                      selection_ = created_;
                      return {};
                  });
}
Result Editor::addFont(std::string key, EmbeddedFont font) {
    if (!font.data || font.data->empty())
        return {Status::InvalidData, "Font file is empty"};
    if (committed_->fonts.contains(key))
        return {Status::NoChange, {}};
    return commit("Import font", false, [&](DocumentData& doc) -> Result {
        doc.fonts[key] = std::move(font);
        return {};
    });
}
Result Editor::setFontEmbedding(const std::string& key, bool embed) {
    const auto found = committed_->fonts.find(key);
    if (found == committed_->fonts.end())
        return {Status::InvalidTarget, "Font is not part of this project"};
    if (found->second.embed == embed)
        return {Status::NoChange, {}};
    if (embed && !found->second.data)
        return {Status::InvalidData, "The font file is not available on this computer"};
    return commit(embed ? "Embed font" : "Link font", false, [&](DocumentData& doc) -> Result {
        doc.fonts[key].embed = embed;
        return {};
    });
}
Result Editor::remove(const std::vector<ObjectId>& requested) {
    const auto ids = topmost(*committed_, requested);
    if (ids.empty())
        return {Status::NoChange, {}};
    return commit(
        ids.size() == 1 ? "Delete object" : "Delete objects", true, [&](DocumentData& doc) -> Result {
            const auto doomed = subtreeIds(doc, ids);
            for (const auto id : doomed)
                if (doc.effectiveLocked({id}))
                    return {Status::InvalidTarget, "Unlock objects before deleting them"};
            std::erase_if(doc.objects, [&](const ObjectRef& o) { return doomed.contains(o->id.value); });
            // Drop embedded images no longer referenced; fonts stay in the project library.
            std::set<std::string> used;
            for (const auto& o : doc.objects)
                if (const auto* image = o->as<ImageData>())
                    used.insert(image->imageKey);
            std::erase_if(doc.images, [&](const auto& pair) { return !used.contains(pair.first); });
            return {};
        });
}
Result Editor::duplicate(const std::vector<ObjectId>& requested, Point offset) {
    const auto ids = topmost(*committed_, requested);
    if (ids.empty())
        return {Status::NoChange, {}};
    return commit("Duplicate", true, [&](DocumentData& doc) -> Result {
        auto map = siblings(doc);
        std::vector<ObjectRef> copies;
        for (const auto root : ids) {
            const auto i = doc.indexOf(root);
            const auto end = doc.subtreeEnd(i);
            if (doc.objects.size() + copies.size() + (end - i) > MaxObjects)
                return {Status::ResourceLimit, "Too many objects"};
            std::unordered_map<std::uint64_t, ObjectId> remap;
            for (auto j = i; j < end; ++j)
                remap[doc.objects[j]->id.value] = nextId();
            for (auto j = i; j < end; ++j) {
                Object o = *doc.objects[j];
                o.id = remap[o.id.value];
                if (j == i) {
                    const Affine parent = doc.parentMatrix(root);
                    const Point local = parent.invertible() ? parent.inverted().mapVector(offset) : offset;
                    o.transform.position = o.transform.position + local;
                    o.locked = false;
                    created_.push_back(o.id);
                } else
                    o.parent = remap[o.parent.value];
                copies.push_back(std::make_shared<const Object>(std::move(o)));
            }
            // Place the duplicate root directly above its original among its siblings.
            auto& list = map[doc.objects[i]->parent.value];
            const auto pos =
                std::find_if(list.begin(), list.end(), [&](const ObjectRef& r) { return r->id == root; });
            list.insert(pos + 1, copies[copies.size() - (end - i)]);
            for (auto j = copies.size() - (end - i) + 1; j < copies.size(); ++j)
                map[copies[j]->parent.value].push_back(copies[j]);
        }
        rebuild(doc, map);
        selection_ = created_;
        return {};
    });
}
Result Editor::applyReplacements(DocumentData& doc, std::vector<Object>& replacements,
                                 bool checkLocks) const {
    bool changed = false;
    for (auto& r : replacements) {
        const auto i = doc.indexOf(r.id);
        if (i >= doc.objects.size())
            return {Status::Stale, "Object no longer exists"};
        const auto& current = *doc.objects[i];
        if (r.parent != current.parent || r.kind() != current.kind())
            return {Status::InvalidData, "Property edits cannot change hierarchy or kind"};
        if (r == current)
            continue;
        if (checkLocks && doc.effectiveLocked(r.id) && !onlyLockFieldsDiffer(r, current))
            return {Status::InvalidTarget, "Object is locked"};
        doc.objects[i] = std::make_shared<const Object>(std::move(r));
        changed = true;
    }
    return changed ? Result{} : Result{Status::NoChange, {}};
}
Result Editor::update(std::string label, std::vector<Object> replacements) {
    return commit(std::move(label), false,
                  [&](DocumentData& doc) { return applyReplacements(doc, replacements, true); });
}
Result Editor::replaceSubtrees(std::string label, std::vector<Object> replacements) {
    if (replacements.empty())
        return {Status::NoChange, {}};
    return commit(std::move(label), true, [&](DocumentData& doc) -> Result {
        for (auto& r : replacements) {
            const auto i = doc.indexOf(r.id);
            if (i >= doc.objects.size())
                return {Status::Stale, "Object no longer exists"};
            if (doc.effectiveLocked(r.id))
                return {Status::InvalidTarget, "Object is locked"};
            if (r.container())
                return {Status::InvalidData, "Replacement must be a leaf object"};
            r.parent = doc.objects[i]->parent;
            const auto end = doc.subtreeEnd(i);
            doc.objects.erase(doc.objects.begin() + static_cast<std::ptrdiff_t>(i) + 1,
                              doc.objects.begin() + static_cast<std::ptrdiff_t>(end));
            doc.objects[i] = std::make_shared<const Object>(std::move(r));
        }
        return {};
    });
}
Result Editor::group(const std::vector<ObjectId>& requested, ObjectKind kind, BooleanOp op) {
    if (kind != ObjectKind::Group && kind != ObjectKind::Boolean && kind != ObjectKind::Clip)
        return {Status::InvalidData, "Not a container kind"};
    const auto ids = topmost(*committed_, requested);
    const std::size_t minimum = kind == ObjectKind::Group ? 1 : 2;
    if (ids.size() < minimum)
        return {Status::InvalidTarget,
                kind == ObjectKind::Group ? "Select objects to group" : "Select at least two objects"};
    return commit(containerName(kind, op), true, [&](DocumentData& doc) -> Result {
        const auto parent = doc.find(ids[0])->parent;
        for (const auto id : ids) {
            const auto* o = doc.find(id);
            if (o->parent != parent)
                return {Status::InvalidTarget, "Select objects inside the same group"};
            if (doc.effectiveLocked(id))
                return {Status::InvalidTarget, "Unlock objects before grouping them"};
            if (kind == ObjectKind::Boolean &&
                (o->kind() == ObjectKind::Image || o->kind() == ObjectKind::Clip))
                return {Status::InvalidTarget, "Booleans combine shapes, text and other booleans"};
        }
        Object container;
        container.id = nextId();
        container.parent = parent;
        container.name = containerName(kind, op);
        if (kind == ObjectKind::Group)
            container.data = GroupData{};
        else if (kind == ObjectKind::Clip)
            container.data = ClipData{};
        else {
            container.data = BooleanData{op};
            container.style = doc.find(ids[0])->style; // the result keeps the bottom operand's look
        }
        auto map = siblings(doc);
        auto& list = map[parent.value];
        // The container takes the position of the topmost member.
        const auto top =
            std::find_if(list.begin(), list.end(), [&](const ObjectRef& r) { return r->id == ids.back(); });
        const auto containerRef = std::make_shared<const Object>(container);
        list.insert(top + 1, containerRef);
        std::vector<ObjectRef> members;
        for (const auto id : ids) {
            Object o = *doc.find(id);
            o.parent = container.id;
            members.push_back(std::make_shared<const Object>(std::move(o)));
        }
        std::erase_if(
            list, [&](const ObjectRef& r) { return std::find(ids.begin(), ids.end(), r->id) != ids.end(); });
        // Children keep their own subtrees; re-key subtree members that pointed at the old refs.
        map[container.id.value] = members;
        rebuild(doc, map);
        created_.assign(1, container.id);
        selection_ = created_;
        return {};
    });
}
Result Editor::ungroup(const std::vector<ObjectId>& requested) {
    std::vector<ObjectId> containers;
    for (const auto id : topmost(*committed_, requested))
        if (const auto* o = committed_->find(id); o && o->container())
            containers.push_back(id);
    if (containers.empty())
        return {Status::NoChange, {}};
    return commit("Ungroup", true, [&](DocumentData& doc) -> Result {
        auto map = siblings(doc);
        for (const auto id : containers) {
            const Object container = *doc.find(id);
            if (doc.effectiveLocked(id))
                return {Status::InvalidTarget, "Unlock the group before ungrouping it"};
            auto& list = map[container.parent.value];
            auto pos =
                std::find_if(list.begin(), list.end(), [&](const ObjectRef& r) { return r->id == id; });
            std::vector<ObjectRef> released;
            for (const auto& child : map[id.value]) {
                Object o = *child;
                const auto composed = decompose(container.transform.matrix() * o.transform.matrix());
                if (!composed)
                    return {Status::Unsupported,
                            "Ungrouping would skew a child; reset the group's scale first"};
                o.transform = *composed;
                o.parent = container.parent;
                o.opacity *= container.opacity;
                released.push_back(std::make_shared<const Object>(std::move(o)));
                created_.push_back(child->id);
            }
            // Released children's own subtrees still refer to them by id; only refs change.
            pos = list.erase(pos);
            list.insert(pos, released.begin(), released.end());
            map.erase(id.value);
        }
        rebuild(doc, map);
        selection_ = created_;
        return {};
    });
}
Result Editor::arrange(const std::vector<ObjectId>& requested, Arrange how) {
    const auto ids = topmost(*committed_, requested);
    if (ids.empty())
        return {Status::NoChange, {}};
    return commit("Arrange", true, [&](DocumentData& doc) -> Result {
        auto map = siblings(doc);
        bool changed = false;
        std::set<std::uint64_t> parents;
        for (const auto id : ids)
            parents.insert(doc.find(id)->parent.value);
        for (const auto parent : parents) {
            auto& list = map[parent];
            const auto chosen = [&](const ObjectRef& r) {
                return std::find(ids.begin(), ids.end(), r->id) != ids.end();
            };
            auto before = list;
            if (how == Arrange::BringToFront || how == Arrange::SendToBack) {
                std::vector<ObjectRef> picked, rest;
                for (const auto& r : list)
                    (chosen(r) ? picked : rest).push_back(r);
                list = how == Arrange::SendToBack ? picked : rest;
                const auto& tail = how == Arrange::SendToBack ? rest : picked;
                list.insert(list.end(), tail.begin(), tail.end());
            } else if (how == Arrange::BringForward) {
                for (std::size_t i = list.size(); i-- > 1;)
                    if (chosen(list[i - 1]) && !chosen(list[i]))
                        std::swap(list[i - 1], list[i]);
            } else {
                for (std::size_t i = 1; i < list.size(); ++i)
                    if (chosen(list[i]) && !chosen(list[i - 1]))
                        std::swap(list[i - 1], list[i]);
            }
            changed = changed || list != before;
        }
        if (!changed)
            return {Status::NoChange, {}};
        rebuild(doc, map);
        return {};
    });
}
Result Editor::move(const std::vector<ObjectId>& requested, ObjectId parent, ObjectId before) {
    const auto ids = topmost(*committed_, requested);
    if (ids.empty())
        return {Status::NoChange, {}};
    return commit("Move in hierarchy", true, [&](DocumentData& doc) -> Result {
        if (parent) {
            const auto* p = doc.find(parent);
            if (!p || !p->container())
                return {Status::InvalidTarget, "Drop onto a group"};
            if (doc.effectiveLocked(parent))
                return {Status::InvalidTarget, "The group is locked"};
        }
        for (const auto id : ids) {
            if (id == parent || doc.isAncestor(id, parent))
                return {Status::InvalidTarget, "A group cannot be moved into itself"};
            if (doc.effectiveLocked(id))
                return {Status::InvalidTarget, "Object is locked"};
        }
        const Affine target = parent ? doc.worldMatrix(parent) : Affine{};
        if (!target.invertible())
            return {Status::InvalidTarget, "Target group has a degenerate transform"};
        auto map = siblings(doc);
        std::vector<ObjectRef> moved;
        for (const auto id : ids) {
            Object o = *doc.find(id);
            const auto local = decompose(target.inverted() * doc.worldMatrix(id));
            if (!local)
                return {Status::Unsupported, "Moving would skew the object; reset the group's scale first"};
            auto& from = map[o.parent.value];
            std::erase_if(from, [&](const ObjectRef& r) { return r->id == id; });
            o.transform = *local;
            o.parent = parent;
            moved.push_back(std::make_shared<const Object>(std::move(o)));
        }
        auto& list = map[parent.value];
        auto pos = list.end();
        if (before) {
            pos = std::find_if(list.begin(), list.end(), [&](const ObjectRef& r) { return r->id == before; });
            if (pos == list.end())
                return {Status::InvalidTarget, "Drop position is not inside the target group"};
        }
        list.insert(pos, moved.begin(), moved.end());
        rebuild(doc, map);
        return {};
    });
}
Result Editor::setArtboard(Artboard artboard) {
    if (artboard == committed_->artboard)
        return {Status::NoChange, {}};
    return commit("Artboard", true, [&](DocumentData& doc) -> Result {
        doc.artboard = std::move(artboard);
        return {};
    });
}
Result Editor::setExportSettings(ExportSettings settings) {
    if (settings == committed_->exportSettings)
        return {Status::NoChange, {}};
    return commit("Export settings", false, [&](DocumentData& doc) -> Result {
        doc.exportSettings = std::move(settings);
        return {};
    });
}
Result Editor::setPalette(std::vector<PaletteColor> palette) {
    if (palette == committed_->palette)
        return {Status::NoChange, {}};
    return commit("Palette", false, [&](DocumentData& doc) -> Result {
        doc.palette = std::move(palette);
        return {};
    });
}
Result Editor::editPaletteColor(std::size_t index, Color color) {
    if (index >= committed_->palette.size())
        return {Status::InvalidTarget, "No such palette colour"};
    const Color old = committed_->palette[index].color;
    if (old == color)
        return {Status::NoChange, {}};
    return commit("Recolour", false, [&](DocumentData& doc) -> Result {
        doc.palette[index].color = color;
        for (auto& o : recolored(doc, old, color)) {
            if (doc.effectiveLocked(o.id))
                continue;
            doc.objects[doc.indexOf(o.id)] = std::make_shared<const Object>(std::move(o));
        }
        return {};
    });
}
Result Editor::begin(std::string label) {
    if (busy())
        return {Status::Stale, "Another edit is in progress"};
    working_ = std::make_unique<Working>(Working{committed_, committed_, std::move(label), generation_});
    return {};
}
Result Editor::preview(std::vector<Object> replacements) {
    if (!working_)
        return {Status::Stale, "No edit in progress"};
    auto copy = std::make_shared<DocumentData>(*working_->base);
    try {
        auto r = applyReplacements(*copy, replacements, true);
        if (!r)
            return r;
        working_->document = std::move(copy);
    } catch (const std::exception& e) {
        return {Status::ResourceLimit, e.what()};
    }
    return {};
}
Result Editor::apply() {
    if (!working_)
        return {Status::NoChange, {}};
    auto w = std::move(working_);
    if (w->generation != generation_)
        return {Status::Stale, "Document changed during the edit"};
    if (w->document == w->base)
        return {Status::NoChange, {}};
    created_.clear();
    return publish(w->base, std::make_shared<DocumentData>(*w->document), w->label, false);
}
void Editor::cancel() {
    working_.reset();
}
Result Editor::undo() {
    if (!canUndo())
        return {Status::NoChange, {}};
    const auto& entry = history_[--cursor_];
    committed_ = entry.before;
    selection_ = entry.selectionBefore;
    change_ = {++generation_, true, "Undo " + entry.label};
    return {};
}
Result Editor::redo() {
    if (!canRedo())
        return {Status::NoChange, {}};
    const auto& entry = history_[cursor_++];
    committed_ = entry.after;
    selection_ = entry.selectionAfter;
    change_ = {++generation_, true, "Redo " + entry.label};
    return {};
}
} // namespace kantan
