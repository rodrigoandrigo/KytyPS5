#pragma once

#include "loader/symbolDatabase.h"

#include <optional>
#include <span>

namespace Loader {

struct Ps4Segment {
	uint64_t offset;
	uint64_t size;
	uint32_t flags;
};
struct Ps4Module {
	uint64_t                 base   = 0;
	uint64_t                 entry  = 0;
	bool                     shared = false;
	std::vector<uint8_t>     image;
	std::vector<Ps4Segment>  segments;
	std::vector<uint64_t>    initializers;
	std::vector<uint64_t>    finalizers;
	std::vector<uint8_t>     tls_template;
	uint64_t                 tls_memory_size = 0;
	uint64_t                 tls_alignment   = 1;
	uint64_t                 tls_static_size = 0;
	std::vector<std::string> resolved_imports;
};

// Builds a checked, relocated image. Does not allocate guest memory or execute code.
// Only the explicitly supplied PS4 export registry participates in resolution.
std::optional<Ps4Module> PreparePs4Module(std::span<const uint8_t> file, uint64_t base,
                                          const SymbolDatabase& ps4_exports, std::string& error,
                                          uint64_t tls_module_id = 1);

} // namespace Loader
