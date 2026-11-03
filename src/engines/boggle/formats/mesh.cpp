// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors
#include "engines/boggle/formats/mesh.h"
#include <charconv>
#include <cmath>
#include <cctype>
#include <locale>
#include <sstream>

namespace reluva::boggle {
namespace {
// Counts are bounded both by input bytes and fixed limits before allocation.
// Separators are validated at each grammar production, including list endings.
class Reader {
public:
    explicit Reader(std::string_view input) : rest_(input) {}
    Error read(Mesh& result) {
        if (rest_.size() > 16 * 1024 * 1024 || !rest_.starts_with("xof 0302txt 0064"))
            return fail("expected xof 0302txt 0064");
        rest_.remove_prefix(16);
        if (take("Header")) {
            uint32_t a = 0, b = 0, c = 0;
            if (!take("{") || !integer(a) || !take(";") || !integer(b) || !take(";") ||
                !integer(c) || !take(";") || !take("}") || a != 1 || b != 0 || c != 1)
                return fail("unsupported Header");
        }
        if (!take("Mesh")) return fail("expected Mesh");
        skip();
        if (!rest_.starts_with("{")) { // optional identifier, not a reference
            if (!identifier()) return fail("invalid mesh name");
        }
        if (!take("{") || !vectors(result.positions) || !faces(result.faces, result.positions.size()))
            return fail("invalid vertices or faces");
        bool materials = false, normals = false, coords = false;
        while (!take("}")) {
            if (take("MeshMaterialList") && !materials) {
                materials = true;
                if (!materialList(result)) return fail("invalid material list");
            } else if (take("MeshNormals") && !normals) {
                normals = true;
                if (!take("{") || !vectors(result.normals) || !faces(result.normalFaces, result.normals.size()) ||
                    !take("}") || result.normalFaces.size() != result.faces.size())
                    return fail("invalid normals");
                for (size_t i = 0; i < result.faces.size(); ++i)
                    if (result.faces[i].size() != result.normalFaces[i].size()) return fail("normal topology mismatch");
            } else if (take("MeshTextureCoords") && !coords) {
                coords = true;
                if (!take("{") || !vectors(result.texcoords) || !take("}") ||
                    result.texcoords.size() != result.positions.size()) return fail("invalid texture coordinates");
            } else {
                return fail("unsupported or repeated mesh block");
            }
        }
        skip();
        if (!rest_.empty()) return fail("unexpected trailing data");
        return Error::success();
    }
private:
    Error fail(const char* why) const { return Error::fail(std::string("Text .x mesh: ") + why); }
    void skip() {
        for (;;) {
            while (!rest_.empty() && std::isspace(static_cast<unsigned char>(rest_.front()))) rest_.remove_prefix(1);
            if (rest_.starts_with("//") || rest_.starts_with("#")) {
                auto at = rest_.find('\n');
                rest_ = at == rest_.npos ? std::string_view{} : rest_.substr(at + 1);
            } else break;
        }
    }
    bool take(std::string_view token) {
        skip();
        if (!rest_.starts_with(token)) return false;
        if (token.size() > 1 && rest_.size() > token.size()) {
            const auto c = static_cast<unsigned char>(rest_[token.size()]);
            if (std::isalnum(c) || c == '_') return false;
        }
        rest_.remove_prefix(token.size());
        return true;
    }
    bool identifier() {
        skip();
        if (rest_.empty() || !(std::isalpha(static_cast<unsigned char>(rest_.front())) || rest_.front() == '_')) return false;
        size_t n = 1;
        while (n < rest_.size() && (std::isalnum(static_cast<unsigned char>(rest_[n])) || rest_[n] == '_')) ++n;
        rest_.remove_prefix(n);
        return true;
    }
    bool integer(uint32_t& value) {
        skip();
        if (rest_.empty()) return false;
        const auto r = std::from_chars(rest_.data(), rest_.data() + rest_.size(), value);
        if (r.ec != std::errc{}) return false;
        rest_.remove_prefix(size_t(r.ptr - rest_.data()));
        return true;
    }
    bool real(double& value) {
        skip();
        if (rest_.empty()) return false;
        // Classic-locale streams also work with standard libraries that do
        // not yet implement floating-point from_chars (notably older libc++).
        size_t n = 0;
        while (n < rest_.size() && n <= 128) {
            const char c = rest_[n];
            if (!(c >= '0' && c <= '9') && c != '+' && c != '-' && c != '.' && c != 'e' && c != 'E') break;
            ++n;
        }
        if (!n || n > 128) return false;
        std::istringstream input{std::string(rest_.substr(0, n))};
        input.imbue(std::locale::classic());
        if (!(input >> value) || input.peek() != std::char_traits<char>::eof() || !std::isfinite(value)) return false;
        rest_.remove_prefix(n);
        return true;
    }
    bool count(uint32_t& value, uint32_t maximum = 65536) {
        return integer(value) && value <= maximum && value <= rest_.size() && take(";");
    }
    template<size_t N> bool values(std::array<double, N>& out) {
        for (auto& v : out) if (!real(v) || !take(";")) return false;
        return true;
    }
    template<size_t N> bool vectors(std::vector<std::array<double, N>>& out) {
        uint32_t n = 0;
        if (!count(n) || !n) return false;
        out.resize(n);
        for (uint32_t i = 0; i < n; ++i)
            if (!values(out[i]) || !take(i + 1 == n ? ";" : ",")) return false;
        return true;
    }
    bool faces(std::vector<Mesh::Face>& out, size_t vertices) {
        uint32_t n = 0;
        if (!count(n) || !n) return false;
        out.resize(n);
        for (uint32_t i = 0; i < n; ++i) {
            uint32_t corners = 0;
            if (!count(corners, 256) || corners < 3 || corners > 1048576 - corners_) return false;
            corners_ += corners;
            out[i].resize(corners);
            for (uint32_t j = 0; j < corners; ++j)
                if (!integer(out[i][j]) || out[i][j] >= vertices || !take(j + 1 == corners ? ";" : ",")) return false;
            if (!take(i + 1 == n ? ";" : ",")) return false;
        }
        return true;
    }
    bool materialList(Mesh& out) {
        uint32_t n = 0, faces = 0;
        if (!take("{") || !count(n, 1024) || !n || !count(faces) || faces != out.faces.size()) return false;
        out.faceMaterials.resize(faces);
        for (uint32_t i = 0; i < faces; ++i)
            if (!integer(out.faceMaterials[i]) || out.faceMaterials[i] >= n || !take(i + 1 == faces ? ";" : ",")) return false;
        out.materials.resize(n);
        for (auto& material : out.materials) {
            if (!take("Material") || !take("{") || !values(material.diffuse) || !take(";") ||
                !real(material.power) || material.power < 0 || !take(";") || !values(material.specular) || !take(";")) return false;
            // Shadow.x omits the emissive tuple; its default is black.
            if (!take("}")) {
                if (!values(material.emissive) || !take(";") || !take("}")) return false;
            }
        }
        return take("}");
    }
    std::string_view rest_;
    size_t corners_ = 0;
};
}
Error readMesh(std::string_view text, Mesh& out) {
    out = {};
    Mesh mesh;
    if (auto err = Reader(text).read(mesh)) return err;
    out = std::move(mesh);
    return Error::success();
}
} // namespace reluva::boggle
