// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/awe/model.h"

#include "common/fs.h"
#include "engines/awe/bink_texture.h"
#include "engines/awe/curves.h"
#include "engines/awe/granny_dll.h"

#include <algorithm>
#include <cmath>
#include <map>

namespace reluva::awe {

namespace {

Mat4 multiply(const Mat4& a, const Mat4& b) {
    Mat4 r{};
    for (int c = 0; c < 4; c++)
        for (int row = 0; row < 4; row++) {
            float s = 0;
            for (int k = 0; k < 4; k++)
                s += a[size_t(k * 4 + row)] * b[size_t(c * 4 + k)];
            r[size_t(c * 4 + row)] = s;
        }
    return r;
}

Mat4 identityMatrix() { return {1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1}; }

BoneTransform fromGr2(const Gr2Transform& t) {
    BoneTransform b;
    std::copy(std::begin(t.position), std::end(t.position), b.position);
    std::copy(std::begin(t.orientation), std::end(t.orientation), b.orientation);
    std::copy(std::begin(t.scaleShear), std::end(t.scaleShear), b.scaleShear);
    return b;
}

Curve readCurve(const Gr2Object& o) {
    Curve c;
    if (!o.valid())
        return c;
    c.degree = int(o.number("Degree"));
    const uint32_t knots = o.count("Knots"), controls = o.count("Controls");
    c.knots.reserve(knots);
    for (uint32_t i = 0; i < knots; i++)
        c.knots.push_back(float(o.element("Knots", i).number("Real32")));
    c.controls.reserve(controls);
    for (uint32_t i = 0; i < controls; i++)
        c.controls.push_back(float(o.element("Controls", i).number("Real32")));
    return c;
}

// The texture a mesh's material draws with: its own, or its diffuse map's.
Gr2Object materialTexture(const Gr2Object& material) {
    if (!material.valid())
        return {};
    Gr2Object t = material.object("Texture");
    if (t.valid())
        return t;
    for (uint32_t i = 0; i < material.count("Maps"); i++) {
        const Gr2Object map = material.element("Maps", i);
        t = materialTexture(map.object("Map"));
        if (t.valid())
            return t;
    }
    return {};
}

} // namespace

Mat4 toMatrix(const BoneTransform& t) {
    const float x = t.orientation[0], y = t.orientation[1], z = t.orientation[2], w = t.orientation[3];
    // The rotation's rows.
    const float r[9] = {1 - 2 * (y * y + z * z), 2 * (x * y - z * w),     2 * (x * z + y * w),
                        2 * (x * y + z * w),     1 - 2 * (x * x + z * z), 2 * (y * z - x * w),
                        2 * (x * z - y * w),     2 * (y * z + x * w),     1 - 2 * (x * x + y * y)};
    // Rotation times scale/shear, then the translation.
    Mat4 m = identityMatrix();
    for (int row = 0; row < 3; row++)
        for (int col = 0; col < 3; col++) {
            float s = 0;
            for (int k = 0; k < 3; k++)
                s += r[row * 3 + k] * t.scaleShear[k * 3 + col];
            m[size_t(col * 4 + row)] = s;
        }
    m[12] = t.position[0];
    m[13] = t.position[1];
    m[14] = t.position[2];
    return m;
}

namespace {

// The bones' TrackMask weights (their ExtendedData's "TrackMask<name>"
// members), by mask name and bone name.
std::map<std::string, std::map<std::string, float>> readTrackMasks(const Gr2Object& skeleton) {
    std::map<std::string, std::map<std::string, float>> out;
    for (uint32_t i = 0; i < skeleton.count("Bones"); i++) {
        const Gr2Object b = skeleton.element("Bones", i);
        const Gr2Object extended = b.object("ExtendedData");
        if (!extended.valid())
            continue;
        for (const Gr2Member& m : extended.members())
            if (m.name.rfind("TrackMask", 0) == 0)
                out[m.name.substr(9)][b.string("Name")] = float(extended.number(m.name));
    }
    return out;
}

} // namespace

std::vector<uint8_t> textureBytes(const Gr2File& file, const Gr2Object& texture) {
    const Gr2Object::Array pixels = texture.element("Images", 0).element("MIPLevels", 0).array("Pixels");
    if (!pixels.count || pixels.stride != 1 || pixels.address + uint64_t(pixels.count) > file.memory().size())
        return {};
    return std::vector<uint8_t>(file.memory().begin() + pixels.address,
                                file.memory().begin() + pixels.address + pixels.count);
}

uint32_t binkTextureFlags(const Gr2Object& texture) {
    const bool alpha = texture.object("Layout").number("BitsForComponent", 3) > 0;
    return uint32_t(texture.number("SubFormat")) | (alpha ? 1u : 0u);
}

Error loadModel(const Gr2File& file, Model& out) {
    out = Model();
    const Gr2Object root = file.root();
    if (const Gr2Object art = root.object("ArtToolInfo"); art.valid() && art.number("UnitsPerMeter") > 0)
        out.unitsPerMeter = float(art.number("UnitsPerMeter"));
    if (root.count("Models") == 0)
        return Error::fail("The file has no model");
    const Gr2Object model = root.element("Models", 0);
    out.name = model.string("Name");
    const Gr2Object skeleton = model.object("Skeleton");
    std::map<std::string, int> boneByName;
    for (uint32_t i = 0; i < skeleton.count("Bones"); i++) {
        const Gr2Object b = skeleton.element("Bones", i);
        Bone bone;
        bone.name = b.string("Name");
        bone.parent = int(b.number("ParentIndex"));
        if (bone.parent >= int(i))
            bone.parent = -1;
        bone.rest = fromGr2(b.transform("Transform"));
        for (uint32_t k = 0; k < 16; k++)
            bone.inverseWorld[k] = float(b.number("InverseWorldTransform", k));
        boneByName.emplace(bone.name, int(out.bones.size()));
        out.bones.push_back(std::move(bone));
    }
    out.masks = readTrackMasks(skeleton);

    std::map<uint32_t, int> textureByAddress;
    auto texture = [&](const Gr2Object& t) -> int {
        if (!t.valid())
            return -1;
        if (auto it = textureByAddress.find(t.address()); it != textureByAddress.end())
            return it->second;
        ModelTexture tex;
        tex.name = t.string("FromFileName");
        if (const size_t slash = tex.name.find_last_of("\\/"); slash != std::string::npos)
            tex.name = tex.name.substr(slash + 1);
        tex.width = int(t.number("Width"));
        tex.height = int(t.number("Height"));
        const int encoding = int(t.number("Encoding"));
        const std::vector<uint8_t> data = textureBytes(file, t);
        if (!data.empty()) {
            const Gr2Object layout = t.object("Layout");
            if (encoding == 3) {
                if (decodeBinkTexture(tex.width, tex.height, binkTextureFlags(t), data, tex.rgba))
                    tex.rgba.clear();
            } else if (encoding == 1 && layout.number("BytesPerPixel") >= 3) {
                // Raw: each pixel's components where the layout says.
                const int bpp = int(layout.number("BytesPerPixel"));
                if (data.size() >= size_t(tex.width) * size_t(tex.height) * size_t(bpp)) {
                    tex.rgba.resize(size_t(tex.width) * size_t(tex.height) * 4);
                    for (size_t p = 0; p < size_t(tex.width) * size_t(tex.height); p++) {
                        uint32_t v = 0;
                        for (int k = 0; k < bpp && k < 4; k++)
                            v |= uint32_t(data[p * size_t(bpp) + size_t(k)]) << (8 * k);
                        for (uint32_t c = 0; c < 4; c++) {
                            const int bits = int(layout.number("BitsForComponent", c));
                            const int shift = int(layout.number("ShiftForComponent", c));
                            tex.rgba[p * 4 + c] =
                                bits ? uint8_t(((v >> shift) & ((1u << bits) - 1)) * 255 / ((1u << bits) - 1)) : 255;
                        }
                    }
                }
            }
        }
        const int index = int(out.textures.size());
        out.textures.push_back(std::move(tex));
        textureByAddress.emplace(t.address(), index);
        return index;
    };

    for (uint32_t m = 0; m < model.count("MeshBindings"); m++) {
        const Gr2Object mesh = model.element("MeshBindings", m).object("Mesh");
        if (!mesh.valid())
            continue;
        ModelMesh out_mesh;
        out_mesh.name = mesh.string("Name");
        // Vertices name their bones by index into the mesh's bindings.
        std::vector<uint8_t> bindings;
        for (uint32_t b = 0; b < mesh.count("BoneBindings"); b++) {
            const auto it = boneByName.find(mesh.element("BoneBindings", b).string("BoneName"));
            bindings.push_back(it == boneByName.end() ? 0 : uint8_t(it->second));
        }
        const Gr2Object vertexData = mesh.object("PrimaryVertexData");
        const uint32_t vertexCount = vertexData.count("Vertices");
        out_mesh.vertices.reserve(vertexCount);
        for (uint32_t v = 0; v < vertexCount; v++) {
            const Gr2Object src = vertexData.element("Vertices", v);
            ModelVertex dst{};
            for (uint32_t k = 0; k < 3; k++) {
                dst.position[k] = float(src.number("Position", k));
                dst.normal[k] = float(src.number("Normal", k));
            }
            dst.uv[0] = float(src.number("TextureCoordinates0", 0));
            dst.uv[1] = float(src.number("TextureCoordinates0", 1));
            const bool weighted = src.member("BoneWeights") != nullptr;
            for (uint32_t k = 0; k < 4; k++) {
                const uint32_t b = uint32_t(src.number("BoneIndices", k));
                dst.bones[k] = b < bindings.size() ? bindings[b] : 0;
                dst.weights[k] = weighted ? float(src.number("BoneWeights", k)) : (k == 0 ? 1.0f : 0.0f);
            }
            // A rigid mesh (no weights) follows its first binding.
            if (!src.member("BoneIndices")) {
                out_mesh.rigid = true;
                if (!bindings.empty())
                    dst.bones[0] = bindings[0];
            }
            out_mesh.vertices.push_back(dst);
        }
        const Gr2Object topology = mesh.object("PrimaryTopology");
        const uint32_t n32 = topology.count("Indices"), n16 = topology.count("Indices16");
        for (uint32_t i = 0; i < n32; i++)
            out_mesh.indices.push_back(uint32_t(topology.element("Indices", i).number("Int32")));
        for (uint32_t i = 0; i < n16; i++)
            out_mesh.indices.push_back(uint32_t(topology.element("Indices16", i).number("UInt16")));
        for (uint32_t& i : out_mesh.indices)
            if (i >= vertexCount)
                i = 0;
        for (uint32_t g = 0; g < topology.count("Groups"); g++) {
            const Gr2Object group = topology.element("Groups", g);
            ModelMesh::Group dst;
            const uint32_t material = uint32_t(group.number("MaterialIndex"));
            if (material < mesh.count("MaterialBindings"))
                dst.texture = texture(materialTexture(mesh.element("MaterialBindings", material).object("Material")));
            dst.firstIndex = uint32_t(group.number("TriFirst")) * 3;
            dst.indexCount = uint32_t(group.number("TriCount")) * 3;
            if (dst.firstIndex > out_mesh.indices.size())
                dst.firstIndex = uint32_t(out_mesh.indices.size());
            dst.indexCount = std::min<uint32_t>(dst.indexCount, uint32_t(out_mesh.indices.size()) - dst.firstIndex);
            out_mesh.groups.push_back(dst);
        }
        out.meshes.push_back(std::move(out_mesh));
    }
    return Error::success();
}

void Curve::evaluate(float t, int dimension, float* out, const CurveLoop& loop) const {
    constexpr int kMaxDimension = 16;
    const ptrdiff_t n = ptrdiff_t(knots.size());
    if (n == 0 || dimension > kMaxDimension || controls.size() < size_t(n) * size_t(dimension)) {
        std::fill(out, out + dimension, 0.0f);
        return;
    }
    auto copy = [&](ptrdiff_t i) { std::copy_n(controls.begin() + i * dimension, dimension, out); };
    const bool looping = (loop.back || loop.forward) && loop.duration > 0;
    // Played once it holds its end values (the animation's clock stays
    // within it).
    if (n == 1 || (!looping && t <= knots[0])) {
        copy(0);
        return;
    }
    if (!looping && t >= knots[size_t(n) - 1]) {
        copy(n - 1);
        return;
    }
    // Granny's way (granny2.dll: GrannyEvaluateCurveAtT, 0x50017cd0; its
    // window, 0x50017810): k is the first knot after t (at most the last).
    // Each knot ends its control's interval; the B-spline at t is de
    // Boor's algorithm over controls k - p .. k with knots k - p .. k + p - 1.
    const ptrdiff_t k =
        std::min<ptrdiff_t>(std::upper_bound(knots.begin(), knots.end(), t) - knots.begin(), n - 1);
    const int p = std::clamp(degree, 0, 3);
    if (p == 0) {
        copy(k);
        return;
    }
    // Past the ends the window is filled in. Played once, with the end
    // knot and control repeated. Looping, with the curve again a loop
    // before or after: the last knot (at the loop's end) stands for the
    // first, so the knots before the start are the last but one, the one
    // before... less the loop's length, and those after the end the first,
    // the second... plus it (once more each time round).
    const ptrdiff_t real = loop.forward && looping ? n - 1 : n; // knots used as they are
    const ptrdiff_t period = std::max<ptrdiff_t>(1, n - 1);
    bool filled = false;
    double window[6], values[4][kMaxDimension];
    for (ptrdiff_t j = k - p; j < k + p; j++) {
        ptrdiff_t from = j;
        float offset = 0;
        if (j < 0 || j >= real) {
            filled = true;
            const bool back = j < 0;
            if (back ? !(loop.back && looping) : !(loop.forward && looping)) {
                from = back ? 0 : real - 1;
            } else {
                const ptrdiff_t q = back ? -j - 1 : j - real; // how far beyond, from 0
                from = back ? period - 1 - q % period : q % period;
                // The loop's length added as floats, as the DLL does.
                for (ptrdiff_t times = 0; times <= q / period; times++)
                    offset = back ? offset - loop.duration : offset + loop.duration;
            }
        }
        window[j - (k - p)] = double(knots[size_t(from)] + offset);
        if (j <= k)
            for (int c = 0; c < dimension; c++)
                values[j - (k - p)][c] = controls[size_t(from) * size_t(dimension) + size_t(c)];
    }
    // Where it filled in, each quaternion is put on the side of the one
    // before (0x50030780).
    if (filled && dimension == 4)
        for (int j = 1; j <= p; j++) {
            double dot = 0;
            for (int c = 0; c < 4; c++)
                dot += values[j][c] * values[j - 1][c];
            if (dot < 0)
                for (int c = 0; c < 4; c++)
                    values[j][c] = -values[j][c];
        }
    // de Boor's algorithm, in doubles (the DLL's x87 maths keeps more than
    // floats); window[i] is knot k - p + i.
    for (int r = 1; r <= p; r++)
        for (int j = p; j >= r; j--) {
            const double lo = window[j - 1], hi = window[j + p - r];
            const double a = hi > lo ? (double(t) - lo) / (hi - lo) : 0.0;
            for (int c = 0; c < dimension; c++)
                values[j][c] = (1 - a) * values[j - 1][c] + a * values[j][c];
        }
    for (int c = 0; c < dimension; c++)
        out[c] = float(values[p][c]);
}

namespace {

const char* const kCurves[3] = {"PositionCurve", "OrientationCurve", "ScaleShearCurve"};
const int kDimensions[3] = {3, 4, 9};
const std::vector<float> kIdentities[3] = {{0, 0, 0}, {0, 0, 0, 1}, {1, 0, 0, 0, 1, 0, 0, 0, 1}};

} // namespace

Error loadAnimation(const Gr2File& file, Animation& out) {
    out = Animation();
    const Gr2Object root = file.root();
    // Its mask: how much each bone follows it (0x4298e2 reads it from the
    // file's skeleton as "TrackMaskTrackMask", whatever its case: the files
    // have "TrackMasktrackmask" and "TrackMaskTrackMask"). Others
    // ("TrackMasksitidle1") aren't read by the game.
    if (root.count("Skeletons") > 0)
        for (auto& [name, weights] : readTrackMasks(root.element("Skeletons", 0)))
            if (fs::toLower(name) == "trackmask")
                out.mask = std::move(weights);
    if (root.count("Animations") == 0)
        return Error::fail("The file has no animation");
    const Gr2Object anim = root.element("Animations", 0);
    out.name = anim.string("Name");
    out.duration = float(anim.number("Duration"));
    out.timeStep = float(anim.number("TimeStep"));
    if (anim.count("TrackGroups") == 0)
        return Error::fail("The animation has no tracks");
    const Gr2Object group = anim.element("TrackGroups", 0);
    for (uint32_t i = 0; i < group.count("TransformTracks"); i++) {
        const Gr2Object t = group.element("TransformTracks", i);
        AnimationTrack track;
        track.bone = t.string("Name");
        Curve* curves[3] = {&track.position, &track.orientation, &track.scaleShear};
        for (int c = 0; c < 3; c++) {
            const Gr2Object curve = t.object(kCurves[c]);
            if (curve.member("Knots")) {
                *curves[c] = readCurve(curve);
                continue;
            }
            if (!curve.valid())
                continue;
            // A compressed format (a CurveData variant), expanded to a
            // plain curve.
            CurveData data;
            Error e = readCurveData(file, curve, data);
            if (!e)
                e = expandCurve(data, kIdentities[c], *curves[c]);
            if (e)
                return Error::fail(track.bone + "'s " + kCurves[c] + ": " + e.message);
            if (curves[c]->controls.size() != curves[c]->knots.size() * size_t(kDimensions[c]))
                return Error::fail(track.bone + "'s " + kCurves[c] + " has " +
                                   std::to_string(curves[c]->controls.size()) + " values for " +
                                   std::to_string(curves[c]->knots.size()) + " knots");
        }
        out.tracks.push_back(std::move(track));
    }
    return Error::success();
}

Error dllCurveAddresses(const Gr2File& file, GrannyDll& dll, uint32_t fileInfo,
                        std::vector<std::array<uint32_t, 3>>& out) {
    out.clear();
    // The DLL's structures are its own types, which it converts the older
    // files to: their members' offsets lead to the curves in its memory.
    const uint32_t infoType = dll.type("GrannyFileInfoType"), animType = dll.type("GrannyAnimationType"),
                   groupType = dll.type("GrannyTrackGroupType"), trackType = dll.type("GrannyTransformTrackType");
    uint32_t animations = 0, groups = 0, tracks = 0, curves[3] = {0, 0, 0};
    bool found = infoType && animType && groupType && trackType && dll.memberOffset(infoType, "Animations", animations) &&
                 dll.memberOffset(animType, "TrackGroups", groups) &&
                 dll.memberOffset(groupType, "TransformTracks", tracks);
    for (int c = 0; c < 3; c++)
        found = found && dll.memberOffset(trackType, kCurves[c], curves[c]);
    if (!found)
        return Error::fail("granny2.dll's types aren't as expected");
    // file info -> Animations[0] -> TrackGroups[0] -> TransformTracks
    const uint32_t animation = dll.read32(dll.read32(fileInfo + animations + 4));
    const uint32_t trackGroup = dll.read32(dll.read32(animation + groups + 4));
    const uint32_t first = dll.read32(trackGroup + tracks + 4), count = dll.read32(trackGroup + tracks);
    const uint32_t stride = dll.typeSize(trackType);
    const Gr2Object group = file.root().element("Animations", 0).element("TrackGroups", 0);
    if (!animation || !trackGroup || !first || count != group.count("TransformTracks"))
        return Error::fail("the DLL's file differs");
    for (uint32_t i = 0; i < count; i++) {
        const Gr2Object t = group.element("TransformTracks", i);
        std::array<uint32_t, 3> a{};
        for (int c = 0; c < 3; c++)
            if (t.object(kCurves[c]).valid())
                a[size_t(c)] = first + i * stride + curves[c];
        out.push_back(a);
    }
    return Error::success();
}

Error expandCurvesWithDll(const Gr2File& file, GrannyDll& dll, Animation& out) {
    const Gr2Object group = file.root().element("Animations", 0).element("TrackGroups", 0);
    if (group.count("TransformTracks") != out.tracks.size())
        return Error::fail("the animation wasn't loaded from this file");
    // The DLL loads the file.
    GrannyDll::LoadedFile loaded;
    if (Error e = dll.loadFile(file.bytes(), loaded))
        return Error::fail("expanding compressed curves: " + e.message);
    struct Free {
        GrannyDll& dll;
        GrannyDll::LoadedFile& f;
        ~Free() { dll.freeFile(f); }
    } freeLater{dll, loaded};
    std::vector<std::array<uint32_t, 3>> addresses;
    if (Error e = dllCurveAddresses(file, dll, loaded.info, addresses))
        return Error::fail("expanding compressed curves: " + e.message);
    for (uint32_t i = 0; i < addresses.size(); i++) {
        const Gr2Object t = group.element("TransformTracks", i);
        Curve* curves[3] = {&out.tracks[i].position, &out.tracks[i].orientation, &out.tracks[i].scaleShear};
        for (int c = 0; c < 3; c++) {
            const Gr2Object curve = t.object(kCurves[c]);
            if (!curve.valid() || curve.member("Knots"))
                continue;
            GrannyDll::PlainCurve plain;
            if (Error e = dll.expandCurve(addresses[i][size_t(c)], kIdentities[c], plain))
                return Error::fail("expanding " + out.tracks[i].bone + "'s " + kCurves[c] + ": " + e.message);
            if (plain.dimension != kDimensions[c])
                return Error::fail(out.tracks[i].bone + "'s " + kCurves[c] + " has " + std::to_string(plain.dimension) +
                                   " values a knot, not " + std::to_string(kDimensions[c]));
            curves[c]->degree = plain.degree;
            curves[c]->knots = std::move(plain.knots);
            curves[c]->controls = std::move(plain.controls);
        }
    }
    return Error::success();
}

void sampleLocal(const Model& model, const Animation* animation, float time, std::vector<BoneTransform>& local,
                 const CurveLoop& loop, bool loopRoot) {
    const size_t n = model.bones.size();
    local.resize(n);
    for (size_t i = 0; i < n; i++)
        local[i] = model.bones[i].rest;
    if (!animation)
        return;
    for (const AnimationTrack& track : animation->tracks) {
        size_t b = 0;
        while (b < n && model.bones[b].name != track.bone)
            b++;
        if (b == n)
            continue;
        const CurveLoop& l = loopRoot || model.bones[b].parent >= 0 ? loop : CurveLoop();
        if (!track.position.empty())
            track.position.evaluate(time, 3, local[b].position, l);
        if (!track.orientation.empty()) {
            float* q = local[b].orientation;
            track.orientation.evaluate(time, 4, q, l);
            const float len = std::sqrt(q[0] * q[0] + q[1] * q[1] + q[2] * q[2] + q[3] * q[3]);
            if (len > 0)
                for (int c = 0; c < 4; c++)
                    q[c] /= len;
            else
                q[3] = 1;
        }
        if (!track.scaleShear.empty())
            track.scaleShear.evaluate(time, 9, local[b].scaleShear, l);
    }
}

void poseFromLocal(const Model& model, std::vector<BoneTransform> local, Pose& out, const std::vector<BoneTurn>* turns) {
    const size_t n = model.bones.size();
    local.resize(n);
    out.world.assign(n, identityMatrix());
    out.skinning.assign(n, identityMatrix());
    if (turns)
        for (const BoneTurn& t : *turns) {
            if (t.bone < 0 || size_t(t.bone) >= n)
                continue;
            // q * turn: the turn in the bone's own axes.
            float* q = local[size_t(t.bone)].orientation;
            const float* r = t.rotation;
            const float x = q[3] * r[0] + q[0] * r[3] + q[1] * r[2] - q[2] * r[1];
            const float y = q[3] * r[1] - q[0] * r[2] + q[1] * r[3] + q[2] * r[0];
            const float z = q[3] * r[2] + q[0] * r[1] - q[1] * r[0] + q[2] * r[3];
            const float w = q[3] * r[3] - q[0] * r[0] - q[1] * r[1] - q[2] * r[2];
            q[0] = x, q[1] = y, q[2] = z, q[3] = w;
        }
    for (size_t i = 0; i < n; i++) {
        const Mat4 m = toMatrix(local[i]);
        const int parent = model.bones[i].parent;
        out.world[i] = parent >= 0 ? multiply(out.world[size_t(parent)], m) : m;
        out.skinning[i] = multiply(out.world[i], model.bones[i].inverseWorld);
    }
}

void computePose(const Model& model, const Animation* animation, float time, Pose& out,
                 const std::vector<BoneTurn>* turns) {
    std::vector<BoneTransform> local;
    sampleLocal(model, animation, time, local);
    poseFromLocal(model, std::move(local), out, turns);
}

void skin(const ModelMesh& mesh, const Pose& pose, std::vector<std::array<float, 3>>& positions,
          std::vector<std::array<float, 3>>& normals) {
    positions.resize(mesh.vertices.size());
    normals.resize(mesh.vertices.size());
    for (size_t v = 0; v < mesh.vertices.size(); v++) {
        const ModelVertex& src = mesh.vertices[v];
        float p[3] = {0, 0, 0}, nrm[3] = {0, 0, 0}, total = 0;
        for (int k = 0; k < 4; k++) {
            const float w = src.weights[k];
            if (w <= 0 || src.bones[k] >= pose.skinning.size())
                continue;
            const Mat4& m = pose.skinning[src.bones[k]];
            for (int r = 0; r < 3; r++) {
                p[r] += w * (m[size_t(r)] * src.position[0] + m[size_t(4 + r)] * src.position[1] +
                             m[size_t(8 + r)] * src.position[2] + m[size_t(12 + r)]);
                nrm[r] += w * (m[size_t(r)] * src.normal[0] + m[size_t(4 + r)] * src.normal[1] +
                               m[size_t(8 + r)] * src.normal[2]);
            }
            total += w;
        }
        if (total <= 0) {
            std::copy(src.position, src.position + 3, p);
            std::copy(src.normal, src.normal + 3, nrm);
        } else if (total != 1) {
            for (int r = 0; r < 3; r++)
                p[r] /= total;
        }
        const float len = std::sqrt(nrm[0] * nrm[0] + nrm[1] * nrm[1] + nrm[2] * nrm[2]);
        for (int r = 0; r < 3; r++) {
            positions[v][size_t(r)] = p[r];
            normals[v][size_t(r)] = len > 0 ? nrm[r] / len : 0;
        }
    }
}

} // namespace reluva::awe
