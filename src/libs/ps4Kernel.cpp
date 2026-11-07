#include "libs/ps4Kernel.h"

#include "common/archive.h"
#include "common/file.h"
#include "common/logging/log.h"
#include "common/platform/sysFileIO.h"
#include "common/threads.h"
#include "common/utf8.h"
#include "kernel/fileSystem.h"
#include "kernel/memory.h"
#include "kernel/pthread.h"
#include "libs/errno.h"
#include "loader/runtimeLinker.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <map>
#include <set>

namespace Libs::Ps4 {
namespace K = LibKernel;
namespace M = LibKernel::Memory;
namespace F = LibKernel::FileSystem;
using namespace LibKernel;
using Mode = Common::VirtualMemory::Mode;
struct KernelSession::State {
	Loader::Program*           program  = nullptr;
	bool                       finished = false;
	std::filesystem::path      app0, data;
	Common::Mutex              mutex;
	std::map<uint64_t, size_t> mappings;
	std::map<int, int>         files;
	struct ThreadRecord {
		uint64_t generation;
		bool     joining;
	};
	uint64_t                           next_thread_generation = 0;
	std::map<K::Pthread, ThreadRecord> threads;
	std::set<K::PthreadMutex>          mutexes;
	std::atomic<uint64_t> allocations {0}, reads {0}, writes {0}, started {0}, joined {0}, tls {0};
};
namespace {
std::atomic<KernelSession::State*> active {nullptr};
bool                               Accessible(const void* pointer, size_t bytes, Mode mode) {
    const auto address = reinterpret_cast<uint64_t>(pointer);
    return M::IsGuestRangeAccessible(address, bytes, mode) &&
           (mode == Mode::Execute || M::EnsureGuestCommitted(address, bytes));
}
std::optional<std::string> Text(const char* pointer, size_t limit = 1024) {
	std::string text;
	for (size_t i = 0; i < limit; ++i) {
		const auto address = reinterpret_cast<uint64_t>(pointer);
		if (!pointer || i > UINT64_MAX - address ||
		    !Accessible(reinterpret_cast<const void*>(address + i), 1, Mode::Read))
			return std::nullopt;
		char ch = 0;
		std::memcpy(&ch, reinterpret_cast<const void*>(address + i), 1);
		if (!ch) {
			if (!Common::IsValidUtf8({reinterpret_cast<const uint8_t*>(text.data()), text.size()}))
				return std::nullopt;
			return text;
		}
		text += ch;
	}
	return std::nullopt;
}
bool SafePath(const std::filesystem::path& root, const std::string& relative) {
	if (relative.empty() || relative.find_first_of("\\:*?\"<>|") != std::string::npos) return false;
	std::filesystem::path walked = root;
	bool                  link   = false;
	if (!Common::IsArchivePath(root) && SysFileIsSymbolicLink(root, &link) && link) return false;
	for (const auto& part: Common::PathFromUtf8(relative)) {
		if (part == "." || part == ".." || part.empty() || part.has_root_path()) return false;
		const auto component = Common::PathToString(part);
		if (component.back() == '.' || component.back() == ' ') return false;
		auto stem = component.substr(0, component.find('.'));
		for (auto& ch: stem)
			if (ch >= 'a' && ch <= 'z') ch -= 'a' - 'A';
		if (stem == "CON" || stem == "PRN" || stem == "AUX" || stem == "NUL" || stem == "CONIN$" ||
		    stem == "CONOUT$" ||
		    (stem.size() == 4 && (stem.starts_with("COM") || stem.starts_with("LPT")) &&
		     stem[3] >= '1' && stem[3] <= '9'))
			return false;
		walked /= part;
		if (!Common::IsArchivePath(root) && SysFileIsSymbolicLink(walked, &link) && link)
			return false;
	}
	return true;
}
int KYTY_SYSV_ABI Open(const char* path, int flags, uint16_t mode) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	const auto text = Text(path);
	if (!text) return KERNEL_ERROR_EFAULT;
	if ((flags & ~(3 | 0x200 | 0x400)) || (flags & 3) == 3) return KERNEL_ERROR_ENOTSUP;
	const bool app = text->starts_with("/app0/"), data = text->starts_with("/data/");
	if (!app && !data) return KERNEL_ERROR_EACCES;
	if (app && flags != 0) return KERNEL_ERROR_EROFS;
	if (!SafePath(app ? s->app0 : s->data, text->substr(6))) return KERNEL_ERROR_EACCES;
	Common::LockGuard lock(s->mutex);
	if (s->files.size() >= 64) return KERNEL_ERROR_EMFILE;
	const auto info = Common::File::GetInfo(F::GetRealFilename(*text));
	if (info && !info->is_file) return KERNEL_ERROR_EISDIR;
	const int fd = F::KernelOpen(text->c_str(), flags, mode);
	if (fd >= 0) s->files.emplace(fd, flags & 3);
	return fd;
}
int KYTY_SYSV_ABI Close(int fd) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	Common::LockGuard lock(s->mutex);
	if (!s->files.contains(fd)) return KERNEL_ERROR_EBADF;
	const int result = F::KernelClose(fd);
	if (!result) s->files.erase(fd);
	return result;
}
int64_t KYTY_SYSV_ABI Read(int fd, void* buffer, size_t bytes) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	Common::LockGuard lock(s->mutex);
	if (!s->files.contains(fd) || s->files.at(fd) == 1) return KERNEL_ERROR_EBADF;
	if (!bytes) return 0;
	if (bytes > 64u * 1024u * 1024u || !Accessible(buffer, bytes, Mode::Write))
		return KERNEL_ERROR_EFAULT;
	const auto result = F::KernelRead(fd, buffer, bytes);
	if (result >= 0) ++s->reads;
	return result;
}
int64_t KYTY_SYSV_ABI Write(int fd, const void* buffer, size_t bytes) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	Common::LockGuard lock(s->mutex);
	if (!s->files.contains(fd) || s->files.at(fd) == 0) return KERNEL_ERROR_EBADF;
	if (!bytes) return 0;
	if (bytes > 64u * 1024u * 1024u || !Accessible(buffer, bytes, Mode::Read))
		return KERNEL_ERROR_EFAULT;
	const auto result = F::KernelWrite(fd, buffer, bytes);
	if (result >= 0) ++s->writes;
	return result;
}
int KYTY_SYSV_ABI Map(void** output, size_t bytes, int protection, int flags, const char* name) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	if (!Accessible(output, 8, Mode::Write) || !Accessible(output, 8, Mode::Read))
		return KERNEL_ERROR_EFAULT;
	if (!bytes || (bytes & 0x3fff) || bytes > 64u * 1024u * 1024u) return KERNEL_ERROR_EINVAL;
	if (protection != 3 || flags != 0 || *output) return KERNEL_ERROR_ENOTSUP;
	const auto text = name ? Text(name, 32) : std::optional(std::string("ps4-flexible"));
	if (!text) return KERNEL_ERROR_EFAULT;
	Common::LockGuard lock(s->mutex);
	size_t            total = bytes;
	for (const auto& [address, size]: s->mappings)
		total += size;
	if (total > 64u * 1024u * 1024u) return KERNEL_ERROR_ENOMEM;
	const int result =
	    M::KernelMapNamedFlexibleMemory(output, bytes, protection, flags, text->c_str());
	if (!result) {
		s->mappings.emplace(reinterpret_cast<uint64_t>(*output), bytes);
		++s->allocations;
	}
	return result;
}
int KYTY_SYSV_ABI Unmap(uint64_t address, size_t bytes) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	Common::LockGuard lock(s->mutex);
	const auto        it = s->mappings.find(address);
	if (it == s->mappings.end() || it->second != bytes) return KERNEL_ERROR_EINVAL;
	const int result = M::KernelMunmap(address, bytes);
	if (!result) s->mappings.erase(it);
	return result;
}
int KYTY_SYSV_ABI CreateThread(K::Pthread* output, const K::PthreadAttr* attributes,
                               K::pthread_entry_func_t entry, void* arg, const char* name) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	if (!Accessible(output, 8, Mode::Write) ||
	    !Accessible(reinterpret_cast<void*>(entry), 1, Mode::Execute))
		return KERNEL_ERROR_EFAULT;
	if (attributes) return KERNEL_ERROR_ENOTSUP;
	const auto text = name ? Text(name, 32) : std::optional(std::string("PS4 worker"));
	if (!text) return KERNEL_ERROR_EFAULT;
	Common::LockGuard lock(s->mutex);
	if (s->threads.size() >= 16) return KERNEL_ERROR_EAGAIN;
	const int result = K::PthreadCreate(output, nullptr, entry, arg, text->c_str());
	if (!result) {
		// The native pool may reuse a just-joined handle before its old join wrapper returns.
		s->threads.insert_or_assign(
		    *output, KernelSession::State::ThreadRecord {++s->next_thread_generation, false});
		++s->started;
	}
	return result;
}
int JoinHandle(KernelSession::State* s, K::Pthread thread, void** value) {
	uint64_t generation = 0;
	{
		Common::LockGuard lock(s->mutex);
		const auto        it = s->threads.find(thread);
		if (it == s->threads.end() || it->second.joining) return KERNEL_ERROR_EINVAL;
		if (thread == K::PthreadSelfOrNull()) return KERNEL_ERROR_EDEADLK;
		generation         = it->second.generation;
		it->second.joining = true;
	}
	const int         result = K::PthreadJoin(thread, value);
	Common::LockGuard lock(s->mutex);
	const auto        it = s->threads.find(thread);
	if (it != s->threads.end() && it->second.generation == generation) {
		if (!result)
			s->threads.erase(it);
		else
			it->second.joining = false;
	}
	if (!result) ++s->joined;
	return result;
}
int KYTY_SYSV_ABI JoinThread(K::Pthread thread, void** value) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	if (value && !Accessible(value, 8, Mode::Write)) return KERNEL_ERROR_EFAULT;
	return JoinHandle(s, thread, value);
}
void* KYTY_SYSV_ABI TlsAddress(const uint64_t* index) {
	auto* s = active.load();
	if (!s || !Accessible(index, 16, Mode::Read) || index[0] != 1 ||
	    index[1] >= s->program->ps4_module->tls_memory_size)
		return nullptr;
	++s->tls;
	return Loader::RuntimeLinker::TlsGetAddr(s->program) + index[1];
}
int KYTY_SYSV_ABI MutexInit(K::PthreadMutex* output, const K::PthreadMutexattr* attributes,
                            const char* name) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	if (!Accessible(output, 8, Mode::Write)) return KERNEL_ERROR_EFAULT;
	if (attributes) return KERNEL_ERROR_ENOTSUP;
	const auto text = name ? Text(name, 32) : std::optional(std::string("PS4 mutex"));
	if (!text) return KERNEL_ERROR_EFAULT;
	Common::LockGuard lock(s->mutex);
	if (s->mutexes.size() >= 64) return KERNEL_ERROR_EAGAIN;
	K::PthreadMutex mutex  = nullptr;
	const int       result = K::PthreadMutexInit(&mutex, nullptr, text->c_str());
	if (!result) {
		*output = mutex;
		s->mutexes.insert(mutex);
	}
	return result;
}
int MutexOperation(K::PthreadMutex* pointer, int operation) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	if (!Accessible(pointer, 8, Mode::Read) ||
	    (operation == 2 && !Accessible(pointer, 8, Mode::Write)))
		return KERNEL_ERROR_EFAULT;
	Common::LockGuard lock(s->mutex);
	auto              mutex = *pointer;
	if (!s->mutexes.contains(mutex)) return KERNEL_ERROR_EINVAL;
	return operation == 1 ? K::PthreadMutexUnlock(&mutex) : [&] {
		const int result = K::PthreadMutexDestroy(&mutex);
		if (!result) {
			s->mutexes.erase(*pointer);
			*pointer = nullptr;
		}
		return result;
	}();
}
int KYTY_SYSV_ABI MutexLock(K::PthreadMutex* pointer) {
	auto* s = active.load();
	if (!s) return KERNEL_ERROR_EINVAL;
	if (!Accessible(pointer, 8, Mode::Read)) return KERNEL_ERROR_EFAULT;
	K::PthreadMutex mutex;
	{
		Common::LockGuard lock(s->mutex);
		mutex = *pointer;
		if (!s->mutexes.contains(mutex)) return KERNEL_ERROR_EINVAL;
	}
	return K::PthreadMutexLock(&mutex);
}
int KYTY_SYSV_ABI MutexUnlock(K::PthreadMutex* pointer) {
	return MutexOperation(pointer, 1);
}
int KYTY_SYSV_ABI MutexDestroy(K::PthreadMutex* pointer) {
	return MutexOperation(pointer, 2);
}
} // namespace

void RegisterKernelExports(Loader::SymbolDatabase& symbols) {
	const auto add = [&](const char* nid, auto function, const char* name) {
		Loader::SymbolResolve query {
		    nid, "libkernel", 1, "libkernel", 1, 0, Loader::SymbolType::Func};
		if (!symbols.Find(query)) symbols.Add(query, reinterpret_cast<uint64_t>(function), name);
	};
	add("1G3lF1Gg1k8", Open, "sceKernelOpen");
	add("UK2Tl2DWUns", Close, "sceKernelClose");
	add("Cg4srZ6TKbU", Read, "sceKernelRead");
	add("4wSze92BhLI", Write, "sceKernelWrite");
	add("mL8NDH86iQI", Map, "sceKernelMapNamedFlexibleMemory");
	add("cQke9UuBQOk", Unmap, "sceKernelMunmap");
	add("6UgtwV+0zb4", CreateThread, "scePthreadCreate");
	add("onNY9Byn-W8", JoinThread, "scePthreadJoin");
	add("vNe1w4diLCs", TlsAddress, "__tls_get_addr");
	add("qH1gXoq71RY", MutexInit, "scePthreadMutexInit");
	add("9UK1vLZQft4", MutexLock, "scePthreadMutexLock");
	add("tn3VlD0hG60", MutexUnlock, "scePthreadMutexUnlock");
	add("2Of0f+3mhhE", MutexDestroy, "scePthreadMutexDestroy");
	add("4J2sUJmuHZQ", K::KernelGetProcessTime, "sceKernelGetProcessTime");
	add("1jfXLRVzisc", K::KernelUsleep, "sceKernelUsleep");
}
KernelSession::KernelSession(std::unique_ptr<State> state): m_state(std::move(state)) {}
std::unique_ptr<KernelSession> KernelSession::Create(Loader::Program*             program,
                                                     const std::filesystem::path& app0,
                                                     const std::filesystem::path& data,
                                                     std::string&                 error) {
	if (!program || !program->ps4_module || active.load()) {
		error = "A PS4 kernel session is already active or the program is invalid.";
		return nullptr;
	}
	if (!Common::File::IsDirectoryExisting(data) && !Common::File::CreateDirectories(data)) {
		error = "Cannot create PS4 data folder.";
		return nullptr;
	}
	bool link = false;
	if (SysFileIsSymbolicLink(data, &link) && link) {
		error = "PS4 data folder cannot be a symbolic link.";
		return nullptr;
	}
	auto state      = std::make_unique<State>();
	state->program  = program;
	state->app0     = app0;
	state->data     = data;
	State* expected = nullptr;
	if (!active.compare_exchange_strong(expected, state.get())) {
		error = "A PS4 kernel session is already active.";
		return nullptr;
	}
	F::Mount(app0, "/app0");
	F::Mount(data, "/data");
	return std::unique_ptr<KernelSession>(new KernelSession(std::move(state)));
}
bool KernelSession::JoinAll(std::string& error) {
	for (;;) {
		K::Pthread next       = nullptr;
		uint64_t   generation = 0;
		{
			Common::LockGuard lock(m_state->mutex);
			if (m_state->threads.empty()) return true;
			for (auto& [thread, record]: m_state->threads) {
				if (!record.joining) {
					next           = thread;
					record.joining = true;
					generation     = record.generation;
					break;
				}
			}
		}
		if (!next) {
			Common::Thread::SleepMicro(1000);
			continue;
		}
		const int         result = K::PthreadJoin(next, nullptr);
		Common::LockGuard lock(m_state->mutex);
		if (result) {
			const auto it = m_state->threads.find(next);
			if (it != m_state->threads.end() && it->second.generation == generation)
				it->second.joining = false;
			error = "Cannot join PS4 worker: " + std::to_string(result);
			return false;
		}
		const auto it = m_state->threads.find(next);
		if (it != m_state->threads.end() && it->second.generation == generation)
			m_state->threads.erase(it);
		++m_state->joined;
	}
}
RuntimeStats KernelSession::Stats() const {
	return {m_state->allocations.load(), m_state->reads.load(),  m_state->writes.load(),
	        m_state->started.load(),     m_state->joined.load(), m_state->tls.load()};
}
bool KernelSession::Finish(std::string& error) {
	if (m_state->finished) return true;
	if (!JoinAll(error)) return false;
	bool       ok    = true;
	const auto check = [&](int result, const char* operation) {
		if (result) {
			if (ok)
				error = std::string("PS4 cleanup failed: ") + operation + " (" +
				        std::to_string(result) + ")";
			ok = false;
		}
	};
	for (auto& [fd, mode]: m_state->files)
		check(F::KernelClose(fd), "close");
	for (auto& [address, size]: m_state->mappings)
		check(M::KernelMunmap(address, size), "unmap");
	for (auto mutex: m_state->mutexes)
		check(K::PthreadMutexDestroy(&mutex), "mutex destroy");
	F::Umount("/data");
	F::Umount("/app0");
	active.store(nullptr);
	m_state->finished = true;
	return ok;
}
KernelSession::~KernelSession() {
	std::string error;
	if (!Finish(error)) {
		LOGF("PS4 teardown failed: %s\n", error.c_str());
		// Never release guest code while a worker may still be executing it.
		if (!m_state->finished) std::abort();
	}
}
} // namespace Libs::Ps4
