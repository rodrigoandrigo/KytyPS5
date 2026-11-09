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
// Bounded metadata only. Host completion is the renderer contract, not proof
// of guest publication or actual writes by each shader invocation.
struct GnmDrawEvidence {
	uint64_t attempt = 0, ordinal = 0, target = 0, resource_bytes = 0;
	uint32_t command_buffer = 0, command_dword = 0, vertex_words = 0, pixel_words = 0;
	bool host_completed = false, guest_published = false, batch_rejected = false;
	uint64_t command_address = 0;
};
enum class GnmSubmitPhase { Validation, Host, Publication, Completed };
struct GnmSubmissionEvidence {
	uint64_t attempt = 0, command_address = 0, target = 0;
	uint32_t command_dword = 0, opcode = 0;
	GnmSubmitPhase phase = GnmSubmitPhase::Validation;
	bool rejected = false, success = false;
	std::string error; // at most1024 bytes, no guest payload
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
	int32_t DrawIndexed(uint64_t commands, uint32_t size, uint32_t count, uint64_t indices,
	                    uint32_t flags, uint32_t type);
	int32_t Done();
	void    Finish();
	GnmStats    Stats() const;
	std::vector<GnmDrawEvidence> DrawEvidence() const;
	std::vector<GnmSubmissionEvidence> SubmissionEvidence() const;
	std::string LastError() const;

private:
	int32_t SubmitOnWorker(uint32_t count, uint64_t dcbs, uint64_t sizes, uint64_t ccbs, uint64_t cc_sizes);
	explicit GnmSession(std::unique_ptr<State> state);
	std::unique_ptr<State> m_state;
};
void RegisterGnmExports(Loader::SymbolDatabase& symbols);
} // namespace Libs::Ps4
