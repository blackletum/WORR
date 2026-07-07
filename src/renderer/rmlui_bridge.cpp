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
#include "renderer/ui_scale.h"

#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL

extern "C" {
#include "../rend_gl/gl.h"
}

#endif

#if UI_RML_HAS_RUNTIME

#include <limits>
#include <memory>
#include <unordered_map>
#include <vector>

#ifdef DotProduct
#undef DotProduct
#endif

#ifdef CrossProduct
#undef CrossProduct
#endif

#include <RmlUi/Core/RenderInterface.h>

#endif

#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL

class R_RmlUiOpenGLRenderInterface final : public Rml::RenderInterface {
public:
    Rml::CompiledGeometryHandle CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                Rml::Span<const int> indices) override
    {
        if (vertices.empty() || indices.empty() ||
            vertices.size() > static_cast<size_t>(TESS_MAX_VERTICES) ||
            indices.size() > static_cast<size_t>(TESS_MAX_INDICES)) {
            return {};
        }

        auto geometry = std::make_unique<R_RmlUiCompiledGeometry>();
        geometry->vertices.reserve(vertices.size());
        geometry->indices.reserve(indices.size());

        for (const Rml::Vertex &vertex : vertices) {
            const Rml::Colourb colour = vertex.colour.ToNonPremultiplied();
            glVertexDesc2D_t out = {};

            out.xy[0] = vertex.position.x;
            out.xy[1] = vertex.position.y;
            out.st[0] = vertex.tex_coord.x;
            out.st[1] = vertex.tex_coord.y;
            out.c = COLOR_U32_RGBA(colour.red, colour.green, colour.blue, colour.alpha);
            geometry->vertices.push_back(out);
        }

        for (int index : indices) {
            if (index < 0 || static_cast<size_t>(index) >= vertices.size()) {
                return {};
            }

            geometry->indices.push_back(static_cast<glIndex_t>(index));
        }

        return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry.release());
    }

    void RenderGeometry(Rml::CompiledGeometryHandle geometry,
                        Rml::Vector2f translation,
                        Rml::TextureHandle texture) override
    {
        auto *compiled = reinterpret_cast<R_RmlUiCompiledGeometry *>(geometry);
        if (!compiled || compiled->vertices.empty() || compiled->indices.empty()) {
            return;
        }

        if (compiled->vertices.size() > static_cast<size_t>(TESS_MAX_VERTICES) ||
            compiled->indices.size() > static_cast<size_t>(TESS_MAX_INDICES)) {
            return;
        }

        const GLuint texnum = TextureForHandle(texture);

        if (tess.numverts || tess.numindices) {
            GL_Flush2D();
        }

        tess.texnum[TMU_TEXTURE] = texnum;
        tess.flags |= GLS_BLEND_BLEND | GLS_SHADE_SMOOTH;

        auto *dst_vertices = reinterpret_cast<glVertexDesc2D_t *>(tess.vertices);
        dst_vertices += tess.numverts;

        for (const glVertexDesc2D_t &src : compiled->vertices) {
            *dst_vertices = src;
            dst_vertices->xy[0] += translation.x;
            dst_vertices->xy[1] += translation.y;
            ++dst_vertices;
        }

        glIndex_t *dst_indices = tess.indices + tess.numindices;
        for (glIndex_t index : compiled->indices) {
            *dst_indices++ = static_cast<glIndex_t>(tess.numverts + index);
        }

        tess.numverts += static_cast<int>(compiled->vertices.size());
        tess.numindices += static_cast<int>(compiled->indices.size());
        GL_Flush2D();
    }

    void ReleaseGeometry(Rml::CompiledGeometryHandle geometry) override
    {
        auto *compiled = reinterpret_cast<R_RmlUiCompiledGeometry *>(geometry);
        delete compiled;
    }

    Rml::TextureHandle LoadTexture(Rml::Vector2i &texture_dimensions,
                                   const Rml::String &source) override
    {
        texture_dimensions = {};

        if (source.empty()) {
            return {};
        }

        const image_t *image = IMG_Find(source.c_str(), IT_PIC, IF_NONE);
        if (!image || image == R_NOTEXTURE || !image->texnum ||
            !image->width || !image->height) {
            return {};
        }

        texture_dimensions = {image->width, image->height};
        return RegisterTexture(image->texnum, false);
    }

    Rml::TextureHandle GenerateTexture(Rml::Span<const Rml::byte> source,
                                       Rml::Vector2i source_dimensions) override
    {
        if (source.empty() ||
            source_dimensions.x <= 0 ||
            source_dimensions.y <= 0) {
            return {};
        }

        const size_t width = static_cast<size_t>(source_dimensions.x);
        const size_t height = static_cast<size_t>(source_dimensions.y);
        if (width > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()) ||
            height > static_cast<size_t>((std::numeric_limits<GLsizei>::max)()) ||
            width > (std::numeric_limits<size_t>::max)() / height / 4) {
            return {};
        }

        const size_t byte_count = width * height * 4;
        if (source.size() < byte_count) {
            return {};
        }

        std::vector<Rml::byte> rgba(source.data(), source.data() + byte_count);
        UnpremultiplyTexture(rgba);

        GLuint texnum = 0;
        qglGenTextures(1, &texnum);
        if (!texnum) {
            return {};
        }

        GL_ForceTexture(TMU_TEXTURE, texnum);
        qglPixelStorei(GL_UNPACK_ALIGNMENT, 1);
        qglTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA,
                      static_cast<GLsizei>(width),
                      static_cast<GLsizei>(height),
                      0, GL_RGBA, GL_UNSIGNED_BYTE, rgba.data());
        qglPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, ClampToEdge());
        qglTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, ClampToEdge());

        c.texUploads++;
        c.textureUploadBytes += byte_count;

        return RegisterTexture(texnum, true);
    }

    void ReleaseTexture(Rml::TextureHandle texture) override
    {
        auto it = r_rmlui_textures.find(texture);
        if (it == r_rmlui_textures.end()) {
            return;
        }

        if (it->second.owned && it->second.texnum) {
            GL_Flush2D();
            for (GLuint &bound : gls.texnums) {
                if (bound == it->second.texnum) {
                    bound = 0;
                }
            }
            qglDeleteTextures(1, &it->second.texnum);
        }

        r_rmlui_textures.erase(it);
    }

    void EnableScissorRegion(bool enable) override
    {
        GL_Flush2D();

        if (enable) {
            qglEnable(GL_SCISSOR_TEST);
            draw.scissor = true;
            return;
        }

        qglDisable(GL_SCISSOR_TEST);
        draw.scissor = false;
    }

    void SetScissorRegion(Rml::Rectanglei region) override
    {
        GL_Flush2D();

        const int x = region.Left();
        const int y = region.Top();
        const int width = region.Width();
        const int height = region.Height();
        clipRect_t clip = {};
        clip.left = x;
        clip.top = y;
        clip.right = x + width;
        clip.bottom = y + height;

        if (clip.right <= clip.left || clip.bottom <= clip.top) {
            qglScissor(0, 0, 0, 0);
            return;
        }

        clipRect_t pixel_clip = {};
        if (!R_UIScaleClipToPixels(&clip, draw.base_scale, draw.scale,
                                   r_config.width, r_config.height,
                                   &pixel_clip)) {
            qglScissor(0, 0, 0, 0);
            return;
        }

        qglEnable(GL_SCISSOR_TEST);
        qglScissor(pixel_clip.left, r_config.height - pixel_clip.bottom,
                   pixel_clip.right - pixel_clip.left,
                   pixel_clip.bottom - pixel_clip.top);
        draw.scissor = true;
    }

private:
    struct R_RmlUiCompiledGeometry {
        std::vector<glVertexDesc2D_t> vertices;
        std::vector<glIndex_t> indices;
    };

    struct R_RmlUiTexture {
        GLuint texnum = 0;
        bool owned = false;
    };

    static GLenum ClampToEdge()
    {
        return (gl_config.caps & QGL_CAP_TEXTURE_CLAMP_TO_EDGE) ?
            GL_CLAMP_TO_EDGE : GL_CLAMP;
    }

    static void UnpremultiplyTexture(std::vector<Rml::byte> &rgba)
    {
        for (size_t i = 0; i + 3 < rgba.size(); i += 4) {
            const unsigned alpha = rgba[i + 3];
            if (!alpha) {
                rgba[i] = 0;
                rgba[i + 1] = 0;
                rgba[i + 2] = 0;
                continue;
            }

            rgba[i] = static_cast<Rml::byte>(
                min(255u, (static_cast<unsigned>(rgba[i]) * 255u) / alpha));
            rgba[i + 1] = static_cast<Rml::byte>(
                min(255u, (static_cast<unsigned>(rgba[i + 1]) * 255u) / alpha));
            rgba[i + 2] = static_cast<Rml::byte>(
                min(255u, (static_cast<unsigned>(rgba[i + 2]) * 255u) / alpha));
        }
    }

    static Rml::TextureHandle RegisterTexture(GLuint texnum, bool owned)
    {
        if (!texnum) {
            return {};
        }

        const Rml::TextureHandle handle = r_rmlui_next_texture_handle++;
        r_rmlui_textures.emplace(handle, R_RmlUiTexture{texnum, owned});
        return handle;
    }

    static GLuint TextureForHandle(Rml::TextureHandle texture)
    {
        if (!texture) {
            return TEXNUM_WHITE;
        }

        auto it = r_rmlui_textures.find(texture);
        if (it == r_rmlui_textures.end() || !it->second.texnum) {
            return TEXNUM_WHITE;
        }

        return it->second.texnum;
    }

    static std::unordered_map<Rml::TextureHandle, R_RmlUiTexture> r_rmlui_textures;
    static Rml::TextureHandle r_rmlui_next_texture_handle;
};

std::unordered_map<Rml::TextureHandle, R_RmlUiOpenGLRenderInterface::R_RmlUiTexture>
    R_RmlUiOpenGLRenderInterface::r_rmlui_textures;
Rml::TextureHandle R_RmlUiOpenGLRenderInterface::r_rmlui_next_texture_handle = 1;

static R_RmlUiOpenGLRenderInterface r_rmlui_opengl_render_interface;

#endif

#if UI_RML_HAS_RUNTIME && defined(RENDERER_VULKAN_LEGACY)

class R_RmlUiVulkanRenderInterface final : public Rml::RenderInterface {};

#endif

#if UI_RML_HAS_RUNTIME && defined(RENDERER_VULKAN_RTX)

class R_RmlUiRtxVkptRenderInterface final : public Rml::RenderInterface {};

#endif

extern "C" renderer_rmlui_family_t R_RmlUiRendererFamily(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return R_RENDERER_RMLUI_FAMILY_OPENGL;
#elif defined(RENDERER_VULKAN_LEGACY)
    return R_RENDERER_RMLUI_FAMILY_VULKAN;
#elif defined(RENDERER_VULKAN_RTX)
    return R_RENDERER_RMLUI_FAMILY_RTX_VKPT;
#else
    return R_RENDERER_RMLUI_FAMILY_NONE;
#endif
}

extern "C" const char *R_RmlUiRendererName(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return "OpenGL RmlUi render-interface primitives";
#elif defined(RENDERER_VULKAN_LEGACY)
    return "Vulkan RmlUi render-interface inactive";
#elif defined(RENDERER_VULKAN_RTX)
    return "RTX/vkpt RmlUi render-interface inactive";
#else
    return "none";
#endif
}

extern "C" bool R_RmlUiCanRender(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return true;
#else
    return false;
#endif
}

extern "C" void *R_RmlUiNativeRenderInterface(void)
{
#if UI_RML_HAS_RUNTIME && USE_REF == REF_GL
    return &r_rmlui_opengl_render_interface;
#else
    return NULL;
#endif
}
