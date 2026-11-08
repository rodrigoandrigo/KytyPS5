#pragma once
#include "graphics/guest_gpu/ps4GnmRenderer.h"
#include "loader/symbolDatabase.h"

#include <functional>
#include <mutex>

namespace Libs::Ps4 {
struct GnmMemory {
	std::function<bool(uint64_t, std::span<uint8_t>)>       read;
	std::function<bool(uint64_t, std::span<const uint8_t>)> write;
	std::function<bool(uint64_t, size_t)>                   writable;
	// Pins the profile's mapping lifetime across snapshots, GPU fences and commit.
	std::function<std::shared_ptr<void>()> hold;
};
struct GnmStats {
	uint64_t submissions = 0, draws = 0, rejected = 0;
};
class GnmSession {
public:
	struct State;
	static std::shared_ptr<GnmSession>
	Create(GnmMemory memory, std::string& error,
	       std::unique_ptr<Graphics::Ps4::GnmRenderer> renderer = {});
	~GnmSession();
	int32_t Submit(uint32_t count, uint64_t dcbs, uint64_t sizes, uint64_t ccbs, uint64_t cc_sizes);
	int32_t SetShader(bool vertex, uint64_t commands, uint32_t size, uint64_t registers,
	                  uint32_t modifier = 0);
	int32_t Draw(uint64_t commands, uint32_t size, uint32_t count, uint32_t flags);
	int32_t Done();
	void    Finish();
	GnmStats    Stats() const;
	std::string LastError() const;

private:
	int32_t SubmitOnWorker(uint32_t count, uint64_t dcbs, uint64_t sizes, uint64_t ccbs, uint64_t cc_sizes);
	explicit GnmSession(std::unique_ptr<State> state);
	std::unique_ptr<State> m_state;
};
void RegisterGnmExports(Loader::SymbolDatabase& symbols);
} // namespace Libs::Ps4
