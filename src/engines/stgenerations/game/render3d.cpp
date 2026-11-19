// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/stgenerations/game/render3d.h"

#include <algorithm>
#include <climits>
#include <cmath>

#include "common/serializer.h"

namespace reluva::stgenerations {

namespace {

// SET_TEXTURE_SCROLL: the renderer offsets a texture by
// ((speed * ms) & 0x1FFFF) << 6 in 16.16, which wraps once round the
// texture's 128 units.
double scrollOffset(int16_t speed, uint32_t time) {
    if (!speed)
        return 0;
    return (double)(((int64_t)speed * time) & 0x1FFFF) / 1024.0;
}

const double kFixed = 65536.0;
const double kNear = 0.05;
const double kPi = 3.14159265358979323846;
// Walls and floors give texture coordinates in these units per texture:
// u and v lengths of 128 show a texture once, whether it is 32, 64, 128
// or 256 texels across (in verid1, halee1, amargos1 and arvada1 alike).
const double kTextureUnits = 128.0;
// Depths closer than this fraction count as the same.
const float kDepthTie = 1e-4f;

double fromFixed(int32_t v) {
    return v / kFixed;
}

} // namespace

struct LevelRenderer::Texture {
    int width = 0;
    int height = 0;
    // Column-major, each column top first: pixels[u * height + v].
    std::vector<uint8_t> pixels;
    // Has transparent texels, and each column at most one run of others.
    bool silhouette = false;
};

struct LevelRenderer::Vertex {
    // Camera space, then screen space after projection.
    double x;
    double y;
    double z;
    double u;
    double v;
};

LevelRenderer::LevelRenderer(const LevelFile &level) : _level(level) {
    _hasSky = _level.loadSky(_sky);
    buildBlendTable(_level.getPalette(), _blend);
    // The original builds its tables toward black when a level starts.
    setFogColor(0, 0, 0);
}

void LevelRenderer::buildShadeTables(const Palette &palette, uint8_t r, uint8_t g, uint8_t b,
                                     uint8_t tables[LevelFile::kLightLevels][256]) {
    for (int i = 0; i < 256; i++)
        tables[0][i] = (uint8_t)i;
    for (int n = 1; n < LevelFile::kLightLevels; n++) {
        for (int i = 0; i < 256; i++) {
            uint8_t pr;
            uint8_t pg;
            uint8_t pb;
            palette.getColor(i, pr, pg, pb);
            int tr = pr + (((int)r - pr) * n >> 3);
            int tg = pg + (((int)g - pg) * n >> 3);
            int tb = pb + (((int)b - pb) * n >> 3);
            // The nearest entry; the first one wins a tie.
            int best = 0;
            int bestDistance = INT32_MAX;
            for (int j = 0; j < 256; j++) {
                uint8_t qr;
                uint8_t qg;
                uint8_t qb;
                palette.getColor(j, qr, qg, qb);
                int distance = (tr - qr) * (tr - qr) + (tg - qg) * (tg - qg) + (tb - qb) * (tb - qb);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = j;
                }
            }
            tables[n][i] = (uint8_t)best;
        }
    }
}

void LevelRenderer::buildBlendTable(const Palette &palette, uint8_t table[kBlendColors][256]) {
    for (int k = 0; k < kBlendColors; k++) {
        uint8_t sr;
        uint8_t sg;
        uint8_t sb;
        palette.getColor(kBlendFirst + k, sr, sg, sb);
        for (int i = 0; i < 256; i++) {
            uint8_t pr;
            uint8_t pg;
            uint8_t pb;
            palette.getColor(i, pr, pg, pb);
            int tr = (pr + sr) >> 1;
            int tg = (pg + sg) >> 1;
            int tb = (pb + sb) >> 1;
            // The nearest entry; the first one wins a tie.
            int best = 0;
            int bestDistance = INT32_MAX;
            for (int j = 0; j < 256; j++) {
                uint8_t qr;
                uint8_t qg;
                uint8_t qb;
                palette.getColor(j, qr, qg, qb);
                int distance = (tr - qr) * (tr - qr) + (tg - qg) * (tg - qg) + (tb - qb) * (tb - qb);
                if (distance < bestDistance) {
                    bestDistance = distance;
                    best = j;
                }
            }
            table[k][i] = (uint8_t)best;
        }
    }
}

void LevelRenderer::setFogColor(uint8_t r, uint8_t g, uint8_t b) {
    buildShadeTables(_level.getPalette(), r, g, b, _shades);
    _fogIndex = _level.getPalette().findBestColor(r, g, b);
}

void LevelRenderer::syncLights(Serializer &s) {
    uint32_t count = (uint32_t)_groupBrightness.size();
    s.syncAsUint32(count);
    if (s.isLoading())
        _groupBrightness.clear();
    auto it = _groupBrightness.begin();
    for (uint32_t i = 0; i < count && !s.err(); i++) {
        int16_t group = s.isSaving() ? it->first : 0;
        int percent = s.isSaving() ? it->second : 0;
        s.syncAsSint16(group);
        s.syncAsInt(percent);
        if (s.isLoading())
            _groupBrightness[group] = percent;
        else
            ++it;
    }
}

void LevelRenderer::setGroupBrightness(int16_t group, int percent) {
    _groupBrightness[group] = percent;
}

const uint8_t *LevelRenderer::getShade(const LevelFile::LightInfo &light) const {
    if (!_lighting)
        return nullptr;
    int percent = 100;
    if (light.group != 0) {
        auto it = _groupBrightness.find(light.group);
        if (it != _groupBrightness.end())
            percent = it->second;
    }
    int level = light.getLevel(percent);
    return level == 0 ? nullptr : _shades[level];
}

LevelRenderer::~LevelRenderer() {
}

const LevelRenderer::Texture *LevelRenderer::getTexture(uint16_t id, uint16_t frames, uint16_t speed, uint32_t time) {
    // As the original: the frame advances by speed / 65536 per ms.
    if (frames > 1 && speed)
        id = (uint16_t)(id + ((uint64_t)time * speed >> 16) % frames);

    auto it = _textures.find(id);
    if (it != _textures.end())
        return it->second.get();

    std::unique_ptr<Texture> texture;
    LevelFile::Texture decoded;
    if (_level.loadTexture(id, decoded)) {
        texture.reset(new Texture());
        texture->width = decoded.surface.w;
        texture->height = decoded.surface.h;
        texture->pixels.resize((size_t)texture->width * texture->height);
        for (int u = 0; u < texture->width; u++) {
            for (int v = 0; v < texture->height; v++)
                texture->pixels[(size_t)u * texture->height + v] = (uint8_t)decoded.surface.getPixel(u, v);
        }
        bool transparent = false;
        bool oneRun = true;
        for (int u = 0; u < texture->width && oneRun; u++) {
            int runs = 0;
            bool previous = false;
            for (int v = 0; v < texture->height; v++) {
                bool solid = texture->pixels[(size_t)u * texture->height + v] != LevelFile::kTransparent;
                transparent |= !solid;
                runs += solid && !previous;
                previous = solid;
            }
            oneRun = runs <= 1;
        }
        texture->silhouette = transparent && oneRun;
    }
    const Texture *result = texture.get();
    _textures[id] = std::move(texture);
    return result;
}

void LevelRenderer::toCamera(double x, double y, double z, Vertex &out) const {
    double dx = x - _camX;
    double dy = y - _camY;
    out.x = dx * _sin - dy * _cos;
    out.z = dx * _cos + dy * _sin;
    out.y = z - _camZ;
}

void LevelRenderer::unproject(const Camera &camera, int w, int h, double x, double y, double depth, double &worldX,
                              double &worldY, double &worldZ) {
    double focal = w / 2.0;
    double right = (x - (w - 1) / 2.0) * depth / focal;
    double up = ((h - 1) / 2.0 + camera.pitch * focal - y) * depth / focal;
    double c = std::cos(camera.yaw);
    double s = std::sin(camera.yaw);
    // The inverse of toCamera: ahead is (c, s), right is (s, -c).
    worldX = camera.x + c * depth + s * right;
    worldY = camera.y + s * depth - c * right;
    worldZ = camera.z + up;
}

void LevelRenderer::render(const Camera &camera, Canvas &dst, uint32_t time) {
    _cos = std::cos(camera.yaw);
    _sin = std::sin(camera.yaw);
    _camX = camera.x;
    _camY = camera.y;
    _camZ = camera.z;
    _focal = dst.w / 2.0;
    _centerX = (dst.w - 1) / 2.0;
    _centerY = (dst.h - 1) / 2.0 + camera.pitch * _focal;

    _depth.assign((size_t)dst.w * dst.h, 0.0f);
    _pick = Pick();
    _owner = Pick();
    drawSky(camera, dst);
    drawFlats(dst, time);
    drawWalls(dst, time);
    drawBillboards(dst, time);
    drawSprites(dst);
    drawTranslucent(dst);
}

void LevelRenderer::queuePolygon(std::vector<Vertex> &poly, const Texture *texture, const uint8_t *shade, Canvas &dst,
                                 Wrap wrap, bool translucent) {
    if (!translucent) {
        drawPolygon(poly, texture, shade, dst, wrap);
        return;
    }
    double depth = 0;
    for (const Vertex &v : poly)
        depth += v.z;
    _translucent.push_back({poly, texture, shade, wrap, depth / poly.size(), _owner});
}

void LevelRenderer::drawTranslucent(Canvas &dst) {
    // The original paints far to near; what is behind must be there first.
    std::stable_sort(_translucent.begin(), _translucent.end(),
                     [](const Translucent &a, const Translucent &b) { return a.depth > b.depth; });
    for (Translucent &t : _translucent) {
        _owner = t.owner;
        drawPolygon(t.poly, t.texture, t.shade, dst, t.wrap, true);
    }
    _translucent.clear();
}

void LevelRenderer::drawSky(const Camera &camera, Canvas &dst) {
    if (!_hasSky || !_skyEnabled) {
        dst.fillRect(Rect(0, 0, dst.w, dst.h), _fogIndex);
        return;
    }
    // One sky pixel per screen pixel, so a 90 degree view covers dst.w
    // columns. The bottom row of the sky sits on the horizon.
    double turn = camera.yaw / (2 * kPi);
    int offset = (int)std::floor(-turn * 4 * dst.w);
    int horizon = (int)std::lround(_centerY);
    for (int y = 0; y < dst.h; y++) {
        int row = std::max(0, std::min<int>(_sky.h - 1, y - (horizon - _sky.h)));
        const uint8_t *src = (const uint8_t *)_sky.getBasePtr(0, row);
        uint8_t *out = (uint8_t *)dst.getBasePtr(0, y);
        for (int x = 0; x < dst.w; x++) {
            int column = (x + offset) % _sky.w;
            if (column < 0)
                column += _sky.w;
            out[x] = src[column];
        }
    }
}

void LevelRenderer::drawFlats(Canvas &dst, uint32_t time) {
    const std::vector<LevelFile::Vertex> &vertices = _level.getVertices();
    std::vector<Vertex> poly;
    const std::vector<LevelFile::Sector> &flats = _level.getSectors();
    for (size_t i = 0; i < flats.size(); i++) {
        const LevelFile::Sector &flat = flats[i];
        if (!(flat.flags & LevelFile::kFlagVisible) || (flat.flags & LevelFile::kFlagNotDrawn) ||
            !(flat.zones & _zones))
            continue;
        _owner = Pick();
        _owner.kind = Pick::kFlat;
        _owner.index = (int)i;
        double height = fromFixed(flat.height);
        // Floors are only seen from above and ceilings from below.
        if (flat.isCeiling() ? height <= _camZ : height >= _camZ)
            continue;

        double uScale = fromFixed(flat.uScale);
        double vScale = fromFixed(flat.vScale);
        double uOffset = fromFixed(flat.uOffset) + scrollOffset(flat.uScroll, time);
        double vOffset = fromFixed(flat.vOffset) + scrollOffset(flat.vScroll, time);
        double c = 1;
        double s = 0;
        if (flat.rotation) {
            double angle = flat.rotation * 2 * kPi / 65536.0;
            c = std::cos(angle);
            s = std::sin(angle);
        }

        // Texture coordinates are in 128ths of the texture, whatever its size.
        const Texture *texture = getTexture(flat.texture, flat.frames, flat.speed, time);
        double uUnit = texture ? texture->width / kTextureUnits : 1;
        double vUnit = texture ? texture->height / kTextureUnits : 1;
        poly.clear();
        for (uint16_t index : flat.vertices) {
            double x = fromFixed(vertices[index].x);
            double y = fromFixed(vertices[index].y);
            Vertex v;
            toCamera(x, y, height, v);
            v.u = ((x * c + y * s) * uScale + uOffset) * uUnit;
            // Stored rows count up from the bottom of the picture.
            v.v = -((y * c - x * s) * vScale + vOffset) * vUnit;
            poly.push_back(v);
        }
        drawPolygon(poly, texture, flat.fullbright ? nullptr : getShade(flat.light), dst);
    }
}

void LevelRenderer::drawWalls(Canvas &dst, uint32_t time) {
    const std::vector<LevelFile::Vertex> &vertices = _level.getVertices();
    std::vector<Vertex> poly(4);
    const std::vector<LevelFile::Wall> &walls = _level.getWalls();
    for (size_t i = 0; i < walls.size(); i++) {
        const LevelFile::Wall &wall = walls[i];
        if (!(wall.flags & LevelFile::kFlagVisible) || (wall.flags & LevelFile::kFlagNotDrawn) ||
            !(wall.zones & _zones))
            continue;
        _owner = Pick();
        _owner.kind = Pick::kWall;
        _owner.index = (int)i;
        double x1 = fromFixed(vertices[wall.vertex1].x);
        double y1 = fromFixed(vertices[wall.vertex1].y);
        double x2 = fromFixed(vertices[wall.vertex2].x);
        double y2 = fromFixed(vertices[wall.vertex2].y);
        // Walls face the side their normal points to and, like the
        // original, are only seen from there unless two-sided.
        bool front = fromFixed(wall.normalX) * _camX + fromFixed(wall.normalY) * _camY + fromFixed(wall.distance) >= 0;
        if (!front && !(wall.flags & LevelFile::kWallTwoSided))
            continue;
        double top = fromFixed(wall.top);
        double bottom = fromFixed(wall.bottom);
        // u at vertex 1 and vertex 2, as 0x4C9393 picks them. From behind
        // the ends swap unless 0x40000 xor 0x20 keeps them as from the
        // front: cap walls in halee1 then keep their dark texture edge on
        // the end that joins the rock.
        bool mirror = (wall.flags & LevelFile::kWallMirror) != 0;
        if (!front)
            mirror = !(mirror != ((wall.flags & LevelFile::kWallMirrorBack) != 0));
        double u0 = fromFixed(wall.uStart) + scrollOffset(wall.uScroll, time);
        double u1;
        double u2;
        if (wall.uEnd1 || wall.uEnd2) {
            double sign = mirror ? 1 : -1;
            u1 = u0 + sign * fromFixed(wall.uEnd2);
            u2 = u0 + sign * fromFixed(wall.uEnd1);
        } else {
            u1 = u0;
            u2 = u0 + fromFixed(wall.uLength);
            if (mirror)
                std::swap(u1, u2);
        }
        // v counts up from the bottom edge and from the bottom row of the
        // texture, so walls stacked on one line carry a picture on from one
        // to the next. Rows are stored top first: row = -v, wrapped.
        double vStart = fromFixed(wall.vStart) + scrollOffset(wall.vScroll, time);
        double v1 = -vStart - fromFixed(wall.vLength);
        double v2 = -vStart;
        // All in 128ths of the texture, whatever its size.
        const Texture *texture = getTexture(wall.texture, wall.frames, wall.speed, time);
        if (texture) {
            u1 *= texture->width / kTextureUnits;
            u2 *= texture->width / kTextureUnits;
            v1 *= texture->height / kTextureUnits;
            v2 *= texture->height / kTextureUnits;
        }

        poly.resize(4);
        toCamera(x1, y1, top, poly[0]);
        poly[0].u = u1;
        poly[0].v = v1;
        toCamera(x2, y2, top, poly[1]);
        poly[1].u = u2;
        poly[1].v = v1;
        toCamera(x2, y2, bottom, poly[2]);
        poly[2].u = u2;
        poly[2].v = v2;
        toCamera(x1, y1, bottom, poly[3]);
        poly[3].u = u1;
        poly[3].v = v2;
        // Like the original with flag 0x2 (0x4C0AD0), a column that is one
        // run of texels is drawn over that run only, not repeated.
        Wrap wrap =
            (wall.flags & LevelFile::kWallTrimColumns) && texture && texture->silhouette ? kWrapOnceDown : kWrapTile;
        queuePolygon(poly, texture, wall.fullbright ? nullptr : getShade(wall.light), dst, wrap,
                     (wall.flags & LevelFile::kFlagTranslucent) != 0);
    }
}

void LevelRenderer::drawBillboards(Canvas &dst, uint32_t time) {
    std::vector<Vertex> poly(4);
    const std::vector<LevelFile::Billboard> &billboards = _level.getBillboards();
    for (size_t i = 0; i < billboards.size(); i++) {
        const LevelFile::Billboard &billboard = billboards[i];
        if (billboard.hidden || !(billboard.zones & _zones))
            continue;
        _owner = Pick();
        _owner.kind = Pick::kBillboard;
        _owner.index = (int)i;
        // SET_BILLBOARD_UNPICKABLE: the pick test passes over flag 0x400.
        _ownerUnpickable = (billboard.flags & LevelFile::kBillboardUnpickable) != 0;
        const Texture *texture = getTexture(billboard.texture, billboard.frames, billboard.speed, time);
        if (!texture)
            continue;
        Vertex centre;
        toCamera(fromFixed(billboard.x), fromFixed(billboard.y), fromFixed(billboard.z), centre);
        double half = fromFixed(billboard.halfWidth);
        double height = fromFixed(billboard.height);

        poly.resize(4);
        const double xs[4] = {-half, half, half, -half};
        const double ys[4] = {height, height, 0, 0};
        for (int i = 0; i < 4; i++) {
            poly[i].x = centre.x + xs[i];
            poly[i].y = centre.y + ys[i];
            poly[i].z = centre.z;
            poly[i].u = (i == 1 || i == 2) ? texture->width : 0;
            poly[i].v = (i >= 2) ? texture->height : 0;
        }
        const uint32_t seeThrough = LevelFile::kFlagTranslucent | LevelFile::kFlagColumnTests;
        queuePolygon(poly, texture, billboard.fullbright ? nullptr : getShade(billboard.light), dst, kWrapStretch,
                     (billboard.flags & seeThrough) == seeThrough);
    }
    _ownerUnpickable = false;
}

const LevelRenderer::Texture *LevelRenderer::getSpriteTexture(const Canvas *surface) {
    std::unique_ptr<Texture> &texture = _spriteTextures[surface];
    if (!texture) {
        texture.reset(new Texture());
        texture->width = surface->w;
        texture->height = surface->h;
        texture->pixels.resize((size_t)surface->w * surface->h);
        for (int u = 0; u < surface->w; u++) {
            for (int v = 0; v < surface->h; v++)
                texture->pixels[(size_t)u * surface->h + v] = (uint8_t)surface->getPixel(u, v);
        }
    }
    return texture.get();
}

void LevelRenderer::drawSprites(Canvas &dst) {
    if (!_sprites)
        return;
    std::vector<Vertex> poly(4);
    for (size_t i = 0; i < _sprites->size(); i++) {
        const SpriteDraw &sprite = (*_sprites)[i];
        if (!sprite.surface || sprite.surface->w == 0 || sprite.surface->h == 0)
            continue;
        _owner = Pick();
        _owner.kind = Pick::kSprite;
        _owner.index = (int)i;
        _owner.object = sprite.object;
        const Texture *texture = getSpriteTexture(sprite.surface);
        Vertex centre;
        toCamera(sprite.x, sprite.y, sprite.bottom, centre);
        const double xs[4] = {-sprite.halfWidth, sprite.halfWidth, sprite.halfWidth, -sprite.halfWidth};
        const double ys[4] = {sprite.height, sprite.height, 0, 0};
        poly.resize(4);
        for (int i = 0; i < 4; i++) {
            bool right = i == 1 || i == 2;
            poly[i].x = centre.x + xs[i];
            poly[i].y = centre.y + ys[i];
            poly[i].z = centre.z;
            poly[i].u = right != sprite.mirrored ? texture->width : 0;
            poly[i].v = i >= 2 ? texture->height : 0;
        }
        queuePolygon(poly, texture, sprite.fullbright ? nullptr : getShade(sprite.light), dst, kWrapStretch,
                     sprite.translucent);
    }
}

void LevelRenderer::drawPolygon(std::vector<Vertex> &poly, const Texture *texture, const uint8_t *shade, Canvas &dst,
                                Wrap wrap, bool translucent) {
    // Clip against the near plane.
    std::vector<Vertex> clipped;
    clipped.reserve(poly.size() + 2);
    for (size_t i = 0; i < poly.size(); i++) {
        const Vertex &a = poly[i];
        const Vertex &b = poly[(i + 1) % poly.size()];
        bool aIn = a.z >= kNear;
        bool bIn = b.z >= kNear;
        if (aIn)
            clipped.push_back(a);
        if (aIn != bIn) {
            double t = (kNear - a.z) / (b.z - a.z);
            Vertex v;
            v.x = a.x + (b.x - a.x) * t;
            v.y = a.y + (b.y - a.y) * t;
            v.z = kNear;
            v.u = a.u + (b.u - a.u) * t;
            v.v = a.v + (b.v - a.v) * t;
            clipped.push_back(v);
        }
    }
    if (clipped.size() < 3)
        return;

    // Project: x and y to the screen; z becomes 1/z and u, v become u/z, v/z
    // so they interpolate linearly across the screen.
    double minY = 1e30;
    double maxY = -1e30;
    double minX = 1e30;
    double maxX = -1e30;
    for (Vertex &v : clipped) {
        double iz = 1.0 / v.z;
        v.x = _centerX + v.x * iz * _focal;
        v.y = _centerY - v.y * iz * _focal;
        v.z = iz;
        v.u *= iz;
        v.v *= iz;
        minY = std::min(minY, v.y);
        maxY = std::max(maxY, v.y);
        minX = std::min(minX, v.x);
        maxX = std::max(maxX, v.x);
    }
    if (maxX < 0 || minX >= dst.w || maxY < 0 || minY >= dst.h)
        return;

    int y0 = std::max(0, (int)std::ceil(minY - 0.5));
    int y1 = std::min<int>(dst.h - 1, (int)std::ceil(maxY - 0.5) - 1);
    size_t n = clipped.size();
    for (int y = y0; y <= y1; y++) {
        double sampleY = y + 0.5;
        // The polygon is convex, so each row crosses it in one span.
        bool found = false;
        Vertex left = {};
        Vertex right = {};
        for (size_t i = 0; i < n; i++) {
            const Vertex &a = clipped[i];
            const Vertex &b = clipped[(i + 1) % n];
            if ((a.y <= sampleY) == (b.y <= sampleY))
                continue;
            double t = (sampleY - a.y) / (b.y - a.y);
            Vertex e;
            e.x = a.x + (b.x - a.x) * t;
            e.z = a.z + (b.z - a.z) * t;
            e.u = a.u + (b.u - a.u) * t;
            e.v = a.v + (b.v - a.v) * t;
            if (!found) {
                left = right = e;
                found = true;
            } else if (e.x < left.x) {
                left = e;
            } else if (e.x > right.x) {
                right = e;
            }
        }
        if (!found || right.x <= left.x)
            continue;

        int x0 = std::max(0, (int)std::ceil(left.x - 0.5));
        int x1 = std::min<int>(dst.w - 1, (int)std::ceil(right.x - 0.5) - 1);
        double width = right.x - left.x;
        double dz = (right.z - left.z) / width;
        double du = (right.u - left.u) / width;
        double dv = (right.v - left.v) / width;
        double start = x0 + 0.5 - left.x;
        double iz = left.z + dz * start;
        double uz = left.u + du * start;
        double vz = left.v + dv * start;
        uint8_t *out = (uint8_t *)dst.getBasePtr(0, y);
        float *depth = &_depth[(size_t)y * dst.w];
        for (int x = x0; x <= x1; x++, iz += dz, uz += du, vz += dv) {
            // Ties go to what is drawn later, as with the original's
            // painting: levels put walls on the same line and height
            // (verid1 has 296 such pairs), which would otherwise take
            // turns column by column as rounding falls.
            if (iz < depth[x] * (1 - kDepthTie))
                continue;
            uint8_t color = 0;
            if (texture) {
                double z = 1.0 / iz;
                int u = (int)std::floor(uz * z);
                int v = (int)std::floor(vz * z);
                if (wrap != kWrapStretch) {
                    // Only one copy down a silhouette: v runs up from the
                    // picture's bottom row at 0 to its top at -height.
                    if (wrap == kWrapOnceDown && (v < -texture->height || v >= 0))
                        continue;
                    u %= texture->width;
                    v %= texture->height;
                    if (u < 0)
                        u += texture->width;
                    if (v < 0)
                        v += texture->height;
                } else {
                    // A sprite's edges would otherwise pick up the opposite edge.
                    u = std::max(0, std::min(u, texture->width - 1));
                    v = std::max(0, std::min(v, texture->height - 1));
                }
                color = texture->pixels[(size_t)u * texture->height + v];
                if (color == LevelFile::kTransparent)
                    continue;
                if (translucent && color >= kBlendFirst) {
                    // Blended over what is already there, which was
                    // shaded when drawn; it hides nothing behind it.
                    out[x] = _blend[color - kBlendFirst][out[x]];
                    continue;
                }
            }
            out[x] = shade ? shade[color] : color;
            depth[x] = (float)iz;
            if (_owner.kind == Pick::kWall && _mapWalls)
                (*_mapWalls)[_owner.index].flags |= LevelFile::kWallOnMap;
            if (x == _pickX && y == _pickY && !_ownerUnpickable) {
                _pick = _owner;
                _pick.depth = 1.0 / iz;
            }
        }
    }
}

Camera LevelRenderer::getStartCamera(double eyeHeight) const {
    struct Floor {
        double area;
        double x;
        double y;
        int32_t height;
    };
    std::vector<Floor> floors;
    std::map<int32_t, double> areaByHeight;
    std::map<int32_t, double> largestByHeight;
    const std::vector<LevelFile::Vertex> &vertices = _level.getVertices();
    for (const LevelFile::Sector &flat : _level.getSectors()) {
        if (flat.isCeiling() || flat.vertices.size() < 3)
            continue;
        Floor floor = {0, 0, 0, flat.height};
        size_t n = flat.vertices.size();
        for (size_t i = 0; i < n; i++) {
            const LevelFile::Vertex &a = vertices[flat.vertices[i]];
            const LevelFile::Vertex &b = vertices[flat.vertices[(i + 1) % n]];
            floor.area += fromFixed(a.x) * fromFixed(b.y) - fromFixed(b.x) * fromFixed(a.y);
            floor.x += fromFixed(a.x);
            floor.y += fromFixed(a.y);
        }
        floor.area = std::fabs(floor.area) / 2;
        floor.x /= n;
        floor.y /= n;
        floors.push_back(floor);
        areaByHeight[flat.height] += floor.area;
        largestByHeight[flat.height] = std::max(largestByHeight[flat.height], floor.area);
    }

    // Levels surround the play area with large single floors, so prefer the
    // height where the most floor area is made of several pieces, and stand
    // in the middle of its largest piece.
    Camera camera;
    double bestScore = -1;
    double bestArea = -1;
    for (const Floor &floor : floors) {
        double score = areaByHeight[floor.height] - largestByHeight[floor.height];
        if (score > bestScore || (score == bestScore && floor.area > bestArea)) {
            bestScore = score;
            bestArea = floor.area;
            camera.x = floor.x;
            camera.y = floor.y;
            camera.z = fromFixed(floor.height) + eyeHeight;
        }
    }
    return camera;
}

} // namespace reluva::stgenerations
