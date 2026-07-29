// engine/renderer/itexture.hpp
//
// Abstract texture interface for renderer-agnostic texture handling.
// Both GL (Texture2D) and software (soft::Texture) textures implement this.

#pragma once

#include <cstdint>
#include <span>

namespace resf2::renderer {

class ITexture {
public:
    virtual ~ITexture() = default;

    [[nodiscard]] virtual int width() const noexcept = 0;
    [[nodiscard]] virtual int height() const noexcept = 0;
    [[nodiscard]] virtual std::span<const std::uint8_t> pixels() const noexcept = 0;
};

}  // namespace resf2::renderer
