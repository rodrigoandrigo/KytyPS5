#pragma once
#include "graphics/guest_gpu/ps4Raster.h"

#include <memory>
#include <vector>
namespace Libs::Graphics::Ps4 {
class GnmRenderer {
public:
	virtual ~GnmRenderer()                         = default;
	virtual bool        Render(const RasterState& state, std::span<const uint32_t> vertex,
	                           std::span<const uint32_t> pixel, std::vector<uint8_t>& output,
	                           std::string& error) = 0;
	virtual const char* Name() const               = 0;
};
// Headless device owned by the PS4 session, using the selected existing host
// backend's shader/raster adapter. No window, presenter or PS5 context required.
std::unique_ptr<GnmRenderer> CreateGnmRenderer(std::string& error, bool software = false);
} // namespace Libs::Graphics::Ps4
