// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "engines/idtech3/client/ref_entity.h"
#include "engines/idtech3/client/scene.h"
#include "engines/idtech3/render/light_grid.h"
#include "engines/idtech3/render/model_cache.h"
#include "engines/idtech3/render/shader_cache.h"
#include "engines/idtech3/vm/vm_services.h"
#include "engines/idtech3/render/world_renderer.h"

#include "graphics/gfx3d.h"

#include <array>
#include <cstdint>
#include <map>
#include <random>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace reluva::idtech3 {

// refdef_t's flag for views without the level (the HUD's heads, the
// menus' models).
constexpr int32_t kNoWorldModel = 1;

// Reading the structures the programs hand over, kept as bytes.
int32_t readInt(const std::vector<uint8_t> &bytes, size_t at);
float readFloat(const std::vector<uint8_t> &bytes, size_t at);
gfx::Vec3 readVec(const std::vector<uint8_t> &bytes, size_t at);

// Draws what a game program (the client game, the menus) hands over:
// its 2D pictures, and each RenderScene's view of its entities, polygons
// and lights, the level with it when the view has it.
class SceneDrawer {
public:
    // `layout`: how the game lays out the entities it adds (kept).
    SceneDrawer(gfx::Renderer3D &renderer, ShaderCache &shaders, ModelCache &models, const RefEntityLayout &layout)
        : _r(renderer), _shaders(shaders), _models(models), _layout(layout) {}
    ~SceneDrawer();
    SceneDrawer(const SceneDrawer &) = delete;
    SceneDrawer &operator=(const SceneDrawer &) = delete;

    // The program's handles, by which it names models, skins and shaders.
    struct Handles {
        const HandleTable *models = nullptr;
        const HandleTable *skins = nullptr;
        const HandleTable *shaders = nullptr;
    };
    // Each program's handles have their own caches (the game's shader 5
    // isn't the menus' shader 5): switching keeps the other's.
    void setHandles(const Handles &handles);
    // Forgets what the handles were loaded as (a new program, or the
    // caches cleared).
    void reset();

    // Pictures [first, last) over the frame, in the program's screen
    // (screenW x screenH, scaled to the frame); `time` in seconds.
    void drawPictures(const std::vector<Picture> &pictures, size_t first, size_t last, int screenW, int screenH,
                      float time);

    // An RGBA image (cols x rows, rows top first) over (x, y, w, h) of the
    // program's screen: a movie's frame, a saved game's picture.
    void drawImage(const uint8_t *rgba, int cols, int rows, float x, float y, float w, float h, int screenW,
                   int screenH);
    // Loads the models and skins the program has registered so far, ahead
    // of drawing them (as its loading screen updates, so the loading takes
    // place there, stage by stage, as the original's).
    void preload();
    // Each frame, before drawing: images' textures from a few frames ago
    // (the GPU done with them) are let go.
    void newFrame();

    // `view` of `scene`, its rectangle (refdef_t's x, y, width, height in
    // the program's screen) scaled to the frame. With the level (`world`,
    // `grid`) unless the view says it has none; then lit as the original
    // lights models without one. After beginFrame(); `firstView` says
    // whether depths are already clear. `time` in seconds is the shaders'
    // clock; dynamic lights only if `dynamicLights`.
    void drawView(const Scene &scene, const View &view, int screenW, int screenH, float time, bool firstView,
                  WorldRenderer *world, const LightGrid *grid, bool dynamicLights);

    // What the last view drew, for the info line.
    const gfx::Vec3 &eye() const { return _eye; }
    size_t placedCount() const { return _placed.size(); }
    size_t effectCount() const { return _effects.size(); }
    size_t lightCount() const { return _lights.size(); }
    int otherEntities() const { return _otherEntities; }
    // The last view's effects (sprites, beams, marks) by shader, the most
    // first: "name x count, ...", the first few.
    std::string effectSummary() const;
    // What's drawn of the program's entities (the original's
    // r_drawentities, and Reluva's own switch for effects alone).
    bool showModels = true;
    bool showEffects = true;
    int portalViews() const { return _portalViews; }
    const std::set<std::string> &unloadable() const { return _unloadable; }

private:
    void placeEntities(const Scene &scene, const View &view, const LightGrid &grid, float time);
    void addEffect(const std::vector<uint8_t> &entity, const gfx::Vec3 &left, const gfx::Vec3 &up, float time);
    void drawEffects(float time, float sortMin, float sortMax);
    std::array<float, 4> entityColor(const std::vector<uint8_t> &entity) const;
    float shaderTime(const std::vector<uint8_t> &entity, float time) const;
    int modelId(int32_t handle);
    // The level of detail to draw `model` at, where it is in the view.
    int lodLevel(int model, int frame, const gfx::Vec3 &origin) const;
    int material(std::vector<int> &cache, int32_t handle, Lighting lighting);
    void addDynamicLights(PointLight &light, const gfx::Vec3 &at) const;
    static const std::string &name(const HandleTable *table, int32_t handle);

    gfx::Renderer3D &_r;
    ShaderCache &_shaders;
    ModelCache &_models;
    const RefEntityLayout &_layout;
    Handles _handles;
    std::map<int32_t, int> _modelIds; // the program's model handle -> _models id (-1: can't load)
    // by shader handle; -2 not made yet
    std::vector<int> _pictureMaterials;
    std::vector<int> _effectMaterials;
    int _beamMaterial = -2; // Quake III's beam's (-2 not made yet)
    int beamMaterial();
    // Those of the other programs drawn with, by their shader table.
    struct Caches {
        std::map<int32_t, int> modelIds;
        std::vector<int> pictureMaterials;
        std::vector<int> effectMaterials;
    };
    std::map<const HandleTable *, Caches> _others;
    std::array<std::vector<gfx::TextureId>, 3> _images; // this frame's, then older
    // This view's model entities, ready to draw.
    struct Placed {
        int model = -1;
        int frame = 0;
        int oldFrame = 0;
        float backLerp = 0;
        gfx::Mat4 transform{};
        PointLight light;
        const Skin *skin = nullptr;
        std::string shader; // in place of the model's own, if set
        std::array<float, 4> color{1, 1, 1, 1};
        float shaderTime = 0;   // seconds: when its shader animations start
        bool depthHack = false; // kept in front (the player's weapon)
        // only through mirrors; not through them
        bool thirdPerson = false;
        bool firstPerson = false;
    };
    std::vector<Placed> _placed;
    std::vector<std::pair<int, gfx::Mat4>> _brushes; // the level's models (*N) placed this view
    // Mirrors' and portals' cameras this view.
    struct PortalEntity {
        gfx::Vec3 origin{};
        gfx::Vec3 oldOrigin{};
        std::array<gfx::Vec3, 3> axes{};
        int32_t frame = 0;
        int32_t oldFrame = 0;
        int32_t skinNum = 0;
    };
    std::vector<PortalEntity> _portalEntities;
    std::vector<uint8_t> _mainRefdef;
    int _portalViews = 0; // drawn so far, for the info line
    void drawLayers(float time, WorldRenderer *world, bool throughPortal);
    void drawPortalView(WorldRenderer &world, const std::array<uint8_t, 32> &areaMask, const gfx::Vec3 &left,
                        float fovY, float aspect, const gfx::Mat4 &mainViewProj, float time);

    // This view's effects (sprites, beams, marks): in one mesh, each with
    // the program's shader handle.
    struct Effect {
        int32_t shader = 0;
        uint32_t firstIndex = 0;
        uint32_t indexCount = 0;
        std::array<float, 4> color{1, 1, 1, 1};
        float shaderTime = 0;
        bool depthHack = false;
        int material = -1; // set when drawn (-1: not drawn)
        bool beam = false; // drawn with beamMaterial()
        float sort = 0;    // its material's
    };
    std::vector<Effect> _effects;
    gfx::MeshId _effectMesh = 0; // _effects' vertices, while a view is drawn
    std::vector<gfx::Vertex> _effectVertices;
    std::vector<uint32_t> _effectIndices;
    std::minstd_rand _effectRandom;         // electricity's jitter
    std::vector<gfx::DynamicLight> _lights; // this view's
    gfx::Vec3 _eye{};
    gfx::Vec3 _forward{1, 0, 0};
    float _focal = 1;                  // the view's 1 / tan(half its height's angle)
    int _otherEntities = 0;            // entities not drawn (kinds not done yet)
    std::set<std::string> _unloadable; // models this view couldn't draw
};

} // namespace reluva::idtech3
