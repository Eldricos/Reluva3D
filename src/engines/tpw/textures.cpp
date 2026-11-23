// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#include "engines/tpw/textures.h"

#include "common/fs.h"
#include "engines/tpw/game_files.h"
#include "image/fsh.h"

#include <vector>

namespace reluva::tpw {

std::string findTexture(const SearchSet &files, const std::string &model, const std::string &texture) {
    const size_t slash = model.rfind('/');
    const std::string folder = slash == std::string::npos ? std::string() : model.substr(0, slash + 1);
    std::vector<std::string> folders = {folder + "textures/"};
    // levels/<world>/...: the world's shared folders.
    std::string world;
    if (fs::toLower(model.substr(0, 7)) == "levels/") {
        const size_t end = model.find('/', 7);
        if (end != std::string::npos)
            world = model.substr(0, end + 1);
    }
    if (!world.empty())
        folders.push_back(world + "sharetex/");
    folders.push_back(folder + "stexture/");
    if (!world.empty())
        folders.push_back(world + "ssharete/");
    const size_t dot = texture.rfind('.');
    const std::string stem = dot == std::string::npos ? texture : texture.substr(0, dot);
    for (const std::string &f : folders)
        for (const std::string &name : {f + stem + ".fsh", f + texture})
            if (files.has(name))
                return name;
    return {};
}

Error loadPicture(const SearchSet &files, const std::string &name, image::Image &out) {
    std::vector<uint8_t> data;
    if (Error err = files.read(name, data))
        return err;
    if (fs::toLower(name.substr(name.size() >= 4 ? name.size() - 4 : 0)) != ".fsh")
        return image::decode(data, out);
    image::ShapeBank bank;
    if (Error err = image::parseShapeBank(std::move(data), bank, kShapeOptions))
        return err;
    if (bank.shapes.empty())
        return Error::fail("no shapes in " + name);
    return image::decodeShape(bank.shapes[0], out);
}

} // namespace reluva::tpw
