// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/sith/formats/model.h"

#include <cstdlib>
#include <string>

namespace reluva::sith {

namespace {

class ModelReader {
public:
    ModelReader(const std::vector<uint8_t> &data, Model &out) : _lines(data), _out(out) {}

    Error read() {
        while (!_lines.done()) {
            const std::string_view line = _lines.next();
            std::vector<std::string_view> w = words(line);
            if (startsWith(line, "SECTION:") || startsWith(line, "3DO"))
                continue;
            if (startsWith(line, "MATERIALS")) {
                if (Error err = readMaterials(line))
                    return err;
            } else if (startsWith(line, "RADIUS")) {
                float r = 0;
                if (w.size() < 2 || !toFloat(w[1], r))
                    return bad("a radius");
                (_mesh ? _mesh->radius : _out.radius) = r;
            } else if (startsWith(line, "INSERT")) {
                if (w.size() < 5 || !vec(w, 2, _out.insertOffset))
                    return bad("the insertion offset");
            } else if (startsWith(line, "GEOSETS") || startsWith(line, "MESHES")) {
                continue;
            } else if (startsWith(line, "GEOSET")) {
                _out.geosets.emplace_back();
                _mesh = nullptr;
            } else if (startsWith(line, "MESH")) {
                if (_out.geosets.empty())
                    return bad("a mesh before its geometry set");
                _mesh = &_out.geosets.back().emplace_back();
            } else if (startsWith(line, "NAME")) {
                if (!_mesh)
                    return bad("a name outside a mesh");
                _mesh->name = after(line, "NAME");
            } else if (startsWith(line, "GEOMETRYMODE") || startsWith(line, "LIGHTINGMODE") ||
                       startsWith(line, "TEXTUREMODE")) {
                int v = 0;
                if (!_mesh || w.size() < 2 || !toInt(w[1], v))
                    return bad("a mode");
                (w[0][0] == 'G' || w[0][0] == 'g'   ? _mesh->geometryMode
                 : w[0][0] == 'L' || w[0][0] == 'l' ? _mesh->lightingMode
                                                    : _mesh->textureMode) = v;
            } else if (startsWith(line, "VERTICES")) {
                if (Error err = readVertices(w))
                    return err;
            } else if (startsWith(line, "TEXTURE")) {
                if (Error err = readTextureVertices(w))
                    return err;
            } else if (startsWith(line, "VERTEX")) {
                if (Error err =
                        readNormals(_mesh ? _mesh->vertexNormals : _scratch, _mesh ? _mesh->vertices.size() : 0))
                    return err;
            } else if (startsWith(line, "FACES")) {
                if (Error err = readFaces(w))
                    return err;
            } else if (startsWith(line, "FACE")) {
                if (!_mesh)
                    return bad("face normals outside a mesh");
                std::vector<Vec3> normals;
                if (Error err = readNormals(normals, _mesh->faces.size()))
                    return err;
                for (size_t i = 0; i < normals.size(); ++i)
                    _mesh->faces[i].normal = normals[i];
            } else if (startsWith(line, "HIERARCHY")) {
                if (Error err = readNodes(w))
                    return err;
            } else
                return bad("an unknown line");
        }
        if (_out.geosets.empty())
            return Error::fail("model without geometry");
        for (const Model::Node &n : _out.nodes)
            if (n.mesh >= int(_out.geosets[0].size()))
                return Error::fail("model node " + n.name + " names a mesh it hasn't");
        return Error::success();
    }

private:
    Error bad(const std::string &what) const {
        return Error::fail("model line " + std::to_string(_lines.lineNumber()) + ": can't read " + what);
    }

    static bool vec(const std::vector<std::string_view> &w, size_t at, Vec3 &out) {
        return w.size() >= at + 3 && toFloat(w[at], out.x) && toFloat(w[at + 1], out.y) && toFloat(w[at + 2], out.z);
    }

    int count(const std::vector<std::string_view> &w) const {
        int n = -1;
        if (w.empty() || !toInt(w.back(), n) || n < 0)
            return -1;
        return n;
    }

    Error readMaterials(std::string_view line) {
        const int n = count(words(line));
        if (n < 0)
            return bad("the material count");
        for (int i = 0; i < n; ++i) {
            int num = 0;
            std::vector<std::string_view> f;
            if (!numbered(_lines.next(), num, f) || f.empty())
                return bad("a material");
            _out.materials.emplace_back(f[0]);
        }
        return Error::success();
    }

    Error readVertices(const std::vector<std::string_view> &w) {
        const int n = count(w);
        if (!_mesh || n < 0)
            return bad("the vertex count");
        for (int i = 0; i < n; ++i) {
            int num = 0;
            std::vector<std::string_view> f;
            Vec3 v;
            float light = 0;
            if (!numbered(_lines.next(), num, f) || !vec(f, 0, v) || (f.size() > 3 && !toFloat(f[3], light)))
                return bad("a vertex");
            _mesh->vertices.push_back(v);
            _mesh->vertexLight.push_back(light);
        }
        return Error::success();
    }

    Error readTextureVertices(const std::vector<std::string_view> &w) {
        const int n = count(w);
        if (!_mesh || n < 0)
            return bad("the texture vertex count");
        for (int i = 0; i < n; ++i) {
            int num = 0;
            std::vector<std::string_view> f;
            std::array<float, 2> uv{};
            if (!numbered(_lines.next(), num, f) || f.size() < 2 || !toFloat(f[0], uv[0]) || !toFloat(f[1], uv[1]))
                return bad("a texture vertex");
            _mesh->texVertices.push_back(uv);
        }
        return Error::success();
    }

    // As many normals as there are vertices or faces: the line gives no count.
    Error readNormals(std::vector<Vec3> &out, size_t n) {
        for (size_t i = 0; i < n; ++i) {
            int num = 0;
            std::vector<std::string_view> f;
            Vec3 v;
            if (!numbered(_lines.next(), num, f) || !vec(f, 0, v))
                return bad("a normal");
            out.push_back(v);
        }
        return Error::success();
    }

    Error readFaces(const std::vector<std::string_view> &w) {
        const int n = count(w);
        if (!_mesh || n < 0)
            return bad("the face count");
        for (int i = 0; i < n; ++i) {
            const std::string_view line = _lines.next();
            int num = 0;
            std::vector<std::string_view> f;
            Model::Face face;
            int corners = 0;
            if (!numbered(line, num, f) || f.size() < 7 || !toInt(f[0], face.material) || !toFlags(f[1], face.type) ||
                !toInt(f[2], face.geometryMode) || !toInt(f[3], face.lightingMode) || !toInt(f[4], face.textureMode) ||
                !toFloat(f[5], face.extraLight) || !toInt(f[6], corners) || corners < 0)
                return bad("a face");
            // The corners are "vertex, texture vertex" pairs, with or
            // without a space after the comma.
            std::string rest;
            for (size_t k = 7; k < f.size(); ++k)
                rest.append(f[k]).push_back(' ');
            for (char &c : rest)
                if (c == ',')
                    c = ' ';
            const std::vector<std::string_view> pairs = words(rest);
            if (pairs.size() < size_t(corners) * 2)
                return bad("a face's corners");
            for (int c = 0; c < corners; ++c) {
                int v = 0;
                int t = 0;
                if (!toInt(pairs[size_t(c) * 2], v) || !toInt(pairs[size_t(c) * 2 + 1], t) || v < 0 ||
                    v >= int(_mesh->vertices.size()))
                    return bad("a face's corner");
                // Untextured meshes have no texture vertices, and their faces
                // name the first all the same.
                if (_mesh->texVertices.empty())
                    t = -1;
                else if (t >= int(_mesh->texVertices.size()))
                    return bad("a face's texture vertex");
                face.vertices.push_back(v);
                face.texVertices.push_back(t);
            }
            if (face.material >= int(_out.materials.size()))
                return bad("a face's material");
            _mesh->faces.push_back(std::move(face));
        }
        return Error::success();
    }

    Error readNodes(const std::vector<std::string_view> &w) {
        const int n = count(w);
        if (n < 0)
            return bad("the node count");
        for (int i = 0; i < n; ++i) {
            const std::string_view line = _lines.next();
            int num = 0;
            std::vector<std::string_view> f;
            Model::Node node;
            if (!numbered(line, num, f) || f.size() < 17 || !toFlags(f[0], node.flags) || !toFlags(f[1], node.type) ||
                !toInt(f[2], node.mesh) || !toInt(f[3], node.parent) || !toInt(f[4], node.child) ||
                !toInt(f[5], node.sibling) || !toInt(f[6], node.childCount) || !vec(f, 7, node.position) ||
                !vec(f, 10, node.rotation) || !vec(f, 13, node.pivot))
                return bad("a hierarchy node");
            // The name is the rest of the line: one has a space in it.
            const size_t at = size_t(f[16].data() - line.data());
            node.name = line.substr(at);
            if (node.parent >= n || node.child >= n || node.sibling >= n)
                return bad("a hierarchy node's links");
            _out.nodes.push_back(std::move(node));
        }
        return Error::success();
    }

    TextLines _lines;
    Model &_out;
    Model::Mesh *_mesh = nullptr;
    std::vector<Vec3> _scratch;
};

} // namespace

Error parseModel(const std::vector<uint8_t> &data, Model &out) {
    out = Model{};
    return ModelReader(data, out).read();
}

} // namespace reluva::sith
