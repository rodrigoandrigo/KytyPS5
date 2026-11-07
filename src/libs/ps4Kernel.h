#pragma once
#include "loader/symbolDatabase.h"

#include <filesystem>
#include <memory>

namespace Loader {
struct Program;
}
namespace Libs::Ps4 {
struct RuntimeStats {
	uint64_t allocations     = 0;
	uint64_t file_reads      = 0;
	uint64_t file_writes     = 0;
	uint64_t threads_started = 0;
	uint64_t threads_joined  = 0;
	uint64_t tls_queries     = 0;
};
void RegisterKernelExports(Loader::SymbolDatabase& symbols);
class KernelSession {
public:
	struct State;
	static std::unique_ptr<KernelSession> Create(Loader::Program*             program,
	                                             const std::filesystem::path& app0,
	                                             const std::filesystem::path& data,
	                                             std::string&                 error);
	~KernelSession();
	bool         JoinAll(std::string& error);
	bool         Finish(std::string& error);
	RuntimeStats Stats() const;

private:
	explicit KernelSession(std::unique_ptr<State> state);
	std::unique_ptr<State> m_state;
};
} // namespace Libs::Ps4
