// SPDX-License-Identifier: GPL-3.0-or-later
// Copyright (C) 2026 The Reluva contributors

#pragma once

#include "common/archive.h"
#include "engines/age/city_mesh.h"
#include "engines/age/collision.h"
#include "engines/age/instances.h"
#include "engines/age/psdl.h"

#include <vector>

namespace reluva::age {

// A city's collisions: the streets, ground and walls generated from its
// PSDL; its facade bounds as walls up to their height (they stand where
// placed building fronts are, which have no bounds of their own); and the
// bounds (bound/<model>_BOUND.bnd) of the models it places. Returns how
// many placed models had a bound.
size_t buildCityCollision(const SearchSet& files, const Psdl& psdl, const CityMesh& mesh,
                          const std::vector<Instance>& instances, CollisionWorld& out);

} // namespace reluva::age
