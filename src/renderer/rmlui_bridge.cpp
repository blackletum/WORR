/*
Copyright (C) 2026

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License along
with this program; if not, write to the Free Software Foundation, Inc.,
51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
*/

#include "shared/shared.h"
#include "renderer/renderer.h"

#if UI_RML_HAS_RUNTIME

#ifdef DotProduct
#undef DotProduct
#endif

#ifdef CrossProduct
#undef CrossProduct
#endif

#include <RmlUi/Core/RenderInterface.h>

#endif

#if UI_RML_HAS_RUNTIME

class R_RmlUiOpenGLRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override
    {
        (void)vertices;
        (void)indices;
        return {};
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture) override
    {
        (void)geometry;
        (void)translation;
        (void)texture;
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
    {
        (void)geometry;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
                                   const Rml::String &source) override
    {
        (void)source;
        texture_dimensions = {};
        return {};
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i source_dimensions) override
    {
        (void)source;
        (void)source_dimensions;
        return {};
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        (void)texture;
    }

    void EnableScissorRegion(bool enable) override
    {
        (void)enable;
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        (void)region;
    }
};

static R_RmlUiOpenGLRenderInterface r_rmlui_opengl_render_interface;

#endif

extern "C" renderer_rmlui_family_t R_RmlUiRendererFamily(void)
{
#if UI_RML_HAS_RUNTIME
    return R_RENDERER_RMLUI_FAMILY_OPENGL;
#else
    return R_RENDERER_RMLUI_FAMILY_NONE;
#endif
}

extern "C" const char *R_RmlUiRendererName(void)
{
#if UI_RML_HAS_RUNTIME
    return "OpenGL RmlUi render-interface scaffold";
#else
    return "none";
#endif
}

extern "C" bool R_RmlUiCanRender(void)
{
    return false;
}

extern "C" void *R_RmlUiNativeRenderInterface(void)
{
#if UI_RML_HAS_RUNTIME
    return &r_rmlui_opengl_render_interface;
#else
    return NULL;
#endif
}
