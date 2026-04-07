#pragma once

// Plain-C++-safe factory for the MetalRenderer.
//
// The full MetalRenderer class definition lives in metal_renderer.h and
// references Objective-C types (id<MTL...>, ObjCRef<...>). To prevent the
// previous dual-layout hazard — where the same struct had different member
// types in ObjC++ and plain C++ TUs — metal_renderer.h is now ObjC++-only.
// Plain C++ translation units (renderer_factory.cpp) instead call this free
// function, which is implemented in metal_renderer.mm and returns the
// concrete renderer behind the public IGridRenderer interface.

#include <draxul/renderer.h>
#include <memory>

namespace draxul
{

std::unique_ptr<IGridRenderer> create_metal_renderer(int atlas_size, RendererOptions options);

} // namespace draxul
