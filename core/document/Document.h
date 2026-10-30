#pragma once
#include "document/Style.h"
#include "shapes/Shapes.h"
#include <map>
#include <memory>
#include <optional>
#include <variant>

namespace kantan {
struct Transform {
    Point position;
    double rotation{}; // degrees, clockwise on screen
    double scaleX{1}, scaleY{1};
    auto operator<=>(const Transform&) const = default;
    Affine matrix() const {
        return Affine::translation(position.x, position.y) * Affine::rotation(rotation) *
               Affine::scaling(scaleX, scaleY);
    }
};
struct ShapeData {
    double width{200}, height{200};
    ShapeGeometry geometry{RectangleParams{}};
    auto operator<=>(const ShapeData&) const = default;
};
enum class TextAlign { Left, Center, Right };
struct FontRef {
    std::string family{"Segoe UI"};
    std::string fileKey; // embedded project font; empty for an installed system font
    auto operator<=>(const FontRef&) const = default;
};
struct TextData {
    std::string text{"Logo"}; // UTF-8
    FontRef font;
    double size{96};
    int weight{700};
    bool italic{}, allCaps{};
    double tracking{};      // extra advance per glyph, in em/1000
    double lineHeight{1.2}; // multiple of the font size
    TextAlign align{TextAlign::Center};
    double arc{}; // -1 (bend down) .. +1 (bend up); 0 is straight
    auto operator<=>(const TextData&) const = default;
};
struct ImageAdjust {
    double brightness{}, contrast{}, saturation{}, hue{}; // -1..1, hue in turns
    Color tint{255, 255, 255, 255};
    double tintAmount{};
    bool colorKey{};
    Color keyColor{255, 255, 255, 255};
    double keyTolerance{.1}, keySoftness{.05};
    auto operator<=>(const ImageAdjust&) const = default;
};
struct ImageData {
    std::string imageKey;
    double width{}, height{}; // displayed size in artboard units
    Box crop;                 // in source pixels; empty means the whole image
    ImageAdjust adjust;
    auto operator<=>(const ImageData&) const = default;
};
struct BooleanData {
    BooleanOp op{BooleanOp::Union};
    auto operator<=>(const BooleanData&) const = default;
};
struct GroupData {
    auto operator<=>(const GroupData&) const = default;
};
// The bottom child is the clip shape; the remaining children are drawn masked by its geometry.
struct ClipData {
    auto operator<=>(const ClipData&) const = default;
};
using ObjectData = std::variant<ShapeData, TextData, ImageData, BooleanData, GroupData, ClipData>;
enum class ObjectKind { Shape, Text, Image, Boolean, Group, Clip };
struct RadialRepeat {
    int count{1}; // 1 means no repeat
    Point pivot;  // in the object's parent space, relative to the object position
    auto operator<=>(const RadialRepeat&) const = default;
};
struct Object {
    ObjectId id, parent;
    std::string name;
    bool visible{true}, locked{};
    double opacity{1};
    Blend blend{Blend::Normal};
    Transform transform;
    Style style;
    double minExportSize{}, maxExportSize{}; // inclusive output pixel bounds; 0 means unbounded
    RadialRepeat repeat;
    ObjectData data;
    auto operator<=>(const Object&) const = default;
    ObjectKind kind() const {
        return static_cast<ObjectKind>(data.index());
    }
    bool container() const {
        const auto k = kind();
        return k == ObjectKind::Boolean || k == ObjectKind::Group || k == ObjectKind::Clip;
    }
    template <class T> const T* as() const {
        return std::get_if<T>(&data);
    }
    template <class T> T* as() {
        return std::get_if<T>(&data);
    }
};
using ObjectRef = std::shared_ptr<const Object>;
struct Artboard {
    double width{512}, height{512};
    bool transparent{true};
    Color background{255, 255, 255, 255};
    std::vector<double> guidesX, guidesY; // vertical / horizontal guide positions in artboard units
    auto operator<=>(const Artboard&) const = default;
};
using Bytes = std::shared_ptr<const std::vector<std::uint8_t>>;
// Original encoded file bytes are kept so saving never re-encodes; `pixels` is their decoded,
// premultiplied RGBA8 sRGB form (always derived from `encoded` by the host's decoder).
struct EmbeddedImage {
    ImageRef pixels;
    Bytes encoded;
    std::string format; // e.g. "png", "jpg"
    std::string sourceName;
};
// `data` is empty when the font is linked rather than embedded and is not installed on this machine.
struct EmbeddedFont {
    Bytes data;
    std::string family, fileName;
    bool embed{true};
};
// Brand colours kept with the project. Editing one recolours every style colour that matches it.
struct PaletteColor {
    std::string name;
    Color color;
    auto operator<=>(const PaletteColor&) const = default;
};
inline constexpr std::size_t MaxPalette = 64;
enum class ExportFit { Contain, Cover };
// An explicitly named PNG output (platform presets). `path` may contain subfolders and {name}.
struct NamedOutput {
    int width{}, height{};
    std::string path;
    auto operator<=>(const NamedOutput&) const = default;
};
struct ExportSize {
    int width{}, height{};
    auto operator<=>(const ExportSize&) const = default;
};
struct ExportSettings {
    bool png{true}, ico{};
    std::vector<int> pngSizes{32, 64, 128, 256, 512};
    std::vector<int> icoSizes{16, 32, 48, 256};
    std::vector<ExportSize> custom;
    double padding{}; // percent of the output size kept empty on each side
    ExportFit fit{ExportFit::Contain};
    bool transparent{true};
    Color background{255, 255, 255, 255};
    bool selectionOnly{};
    std::string naming{"{name}_{size}"};
    std::string icoName{"{name}"};
    std::vector<NamedOutput> outputs;
    std::string preset; // name of the platform preset that filled these settings; presentation only
    std::string folder; // last used output folder; presentation metadata only
    auto operator<=>(const ExportSettings&) const = default;
};
struct DocumentData {
    Artboard artboard;
    std::uint64_t state{1};         // persistent-state identity for dirty comparison
    std::vector<ObjectRef> objects; // depth-first, bottom to top: a container is followed by its subtree
    std::map<std::string, EmbeddedImage> images;
    std::map<std::string, EmbeddedFont> fonts;
    std::vector<PaletteColor> palette;
    ExportSettings exportSettings;
    const Object* find(ObjectId id) const;
    std::size_t indexOf(ObjectId id) const; // objects.size() when missing
    std::vector<ObjectId> children(ObjectId parent) const;
    // Index one past the end of the subtree rooted at objects[index].
    std::size_t subtreeEnd(std::size_t index) const;
    bool effectiveVisible(ObjectId id) const;
    bool effectiveLocked(ObjectId id) const;
    bool isAncestor(ObjectId ancestor, ObjectId id) const;
    // Parent-to-artboard transform of an object (product of ancestor transforms, excluding its own).
    Affine parentMatrix(ObjectId id) const;
    Affine worldMatrix(ObjectId id) const;
};
using Snapshot = std::shared_ptr<const DocumentData>;
inline constexpr std::size_t MaxObjects = 5000;
inline constexpr double MaxArtboard = 16384;
// Structural validation used by the editor and by IO before a document is constructed.
Result validateDocument(const DocumentData& doc);
// Rebuild depth-first order preserving sibling order; drops nothing. Requires acyclic parent links.
void normalizeOrder(DocumentData& doc);
const char* kindName(ObjectKind kind);
} // namespace kantan
