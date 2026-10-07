// Sonic Superstars (PPSA06888) stalls on "Downloading data" unless PlayGo chunk
// enumeration is two-pass and a missing install manifest looks like a local install.
// The metadata provider is substituted; the calls are the production guest functions.
#include "common/abi.h"
#include "libs/errno.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace Libs::PlayGo {
int KYTY_SYSV_ABI PlayGoTerminate();
int KYTY_SYSV_ABI PlayGoOpen(int* out_handle, const void* param);
int KYTY_SYSV_ABI PlayGoGetLocus(int handle, const uint16_t* chunk_ids, uint32_t number_of_entries,
                                 int8_t* out_loci);
int KYTY_SYSV_ABI PlayGoGetChunkId(int handle, uint16_t* out_chunk_id_list,
                                   uint32_t number_of_entries, uint32_t* out_entries);
int KYTY_SYSV_ABI PlayGoGetInstallChunkId(int handle, uint16_t* out_chunk_id_list,
                                          uint32_t number_of_entries, uint32_t* out_entries);
int KYTY_SYSV_ABI PlayGoPrefetch(int handle, const uint16_t* chunk_ids, uint32_t number_of_entries,
                                 int8_t minimum_locus);
int KYTY_SYSV_ABI PlayGoGetEta(int handle, const uint16_t* chunk_ids, uint32_t number_of_entries,
                               int64_t* out_eta);
struct PlayGoProgress {
	uint64_t progress_size;
	uint64_t total_size;
};
int KYTY_SYSV_ABI PlayGoGetProgress(int handle, const uint16_t* chunk_ids,
                                    uint32_t number_of_entries, PlayGoProgress* out_progress);
} // namespace Libs::PlayGo

namespace {
bool     available        = false;
uint32_t manifest_chunks  = 0;
uint32_t metadata_queries = 0;

void Check(bool condition, const char* message) {
	if (!condition) {
		std::fprintf(stderr, "PlayGo API failure: %s\n", message);
		std::abort();
	}
}

using Enumerate = int(KYTY_SYSV_ABI*)(int, uint16_t*, uint32_t, uint32_t*);

void TestEnumeration(Enumerate enumerate, int handle, uint32_t expected) {
	uint32_t count = 0xffffffffu;
	Check(enumerate(handle, nullptr, 0, &count) == OK && count == expected,
	      "count-only pass did not return total chunks");
	Check(enumerate(handle, nullptr, 1, &count) == OK && count == expected,
	      "null-buffer count query depends on capacity");
	std::vector<uint16_t> ids(expected + 1, 0xffff);
	Check(enumerate(handle, ids.data(), expected + 1, &count) == OK && count == expected,
	      "second enumeration pass failed");
	for (uint32_t i = 0; i < expected; i++) {
		Check(ids[i] == i, "wrong chunk ID");
	}
	Check(ids[expected] == 0xffff, "wrote beyond available chunks");
	uint16_t small[] {0xffff, 0xffff, 0xbeef};
	Check(enumerate(handle, small, 2, &count) == OK && count == 2,
	      "capacity-limited enumeration failed");
	Check(small[0] == 0 && small[1] == 1 && small[2] == 0xbeef, "buffer overflow");
	Check(enumerate(handle, small, 0, &count) == Libs::PlayGo::PLAYGO_ERROR_BAD_SIZE,
	      "accepted nonnull zero-capacity buffer");
	Check(enumerate(handle, nullptr, 0, nullptr) == Libs::PlayGo::PLAYGO_ERROR_BAD_POINTER,
	      "accepted null count pointer");
	Check(enumerate(99, nullptr, 0, &count) == Libs::PlayGo::PLAYGO_ERROR_BAD_HANDLE,
	      "accepted invalid handle");
}
} // namespace

namespace Loader {
bool SystemContentGetChunksNum(uint32_t* count) {
	++metadata_queries;
	if (!available) {
		return false;
	}
	*count = manifest_chunks;
	return true;
}
} // namespace Loader

int main() {
	using namespace Libs::PlayGo;
	int handle = 0;
	Check(PlayGoOpen(&handle, nullptr) == OK && handle == 1, "open failed");
	TestEnumeration(PlayGoGetChunkId, handle, 1000);
	TestEnumeration(PlayGoGetInstallChunkId, handle, 1000);
	Check(metadata_queries == 1, "repeated missing metadata lookups");
	const uint16_t ids[] {0, 6, 999, 1000, 65535};
	int8_t         loci[5] {};
	Check(PlayGoGetLocus(handle, ids, 5, loci) == OK, "fallback rejected requested chunk");
	Check(std::all_of(loci, loci + 5, [](int8_t locus) { return locus == 3; }), "not LOCAL_FAST");
	PlayGoProgress value {};
	Check(PlayGoGetProgress(handle, ids, 5, &value) == OK && value.total_size != 0 &&
	          value.progress_size == value.total_size,
	      "incomplete installed progress");
	int64_t remaining = -1;
	Check(PlayGoGetEta(handle, ids, 5, &remaining) == OK && remaining == 0, "ETA not zero");
	Check(PlayGoPrefetch(handle, ids, 5, 3) == OK, "local prefetch failed");
	Check(PlayGoGetLocus(handle, ids, 0, loci) == Libs::PlayGo::PLAYGO_ERROR_BAD_SIZE, "accepted empty locus");
	Check(PlayGoGetLocus(handle, nullptr, 5, loci) == Libs::PlayGo::PLAYGO_ERROR_BAD_POINTER,
	      "accepted null IDs");
	Check(PlayGoPrefetch(handle, ids, 5, 1) == Libs::PlayGo::PLAYGO_ERROR_BAD_LOCUS,
	      "accepted invalid locus");
	Check(PlayGoGetProgress(handle, ids, 5, nullptr) == Libs::PlayGo::PLAYGO_ERROR_BAD_POINTER,
	      "accepted null progress output");
	Check(PlayGoTerminate() == OK, "termination failed");

	available       = true;
	manifest_chunks = 7;
	Check(PlayGoOpen(&handle, nullptr) == OK, "manifest-backed open failed");
	TestEnumeration(PlayGoGetChunkId, handle, 7);
	TestEnumeration(PlayGoGetInstallChunkId, handle, 7);
	Check(PlayGoGetLocus(handle, ids, 2, loci) == OK && loci[0] == 3 && loci[1] == 3,
	      "valid manifest chunks not installed");
	const uint16_t invalid = 7;
	Check(PlayGoGetLocus(handle, &invalid, 1, loci) == Libs::PlayGo::PLAYGO_ERROR_BAD_CHUNK_ID,
	      "ignored manifest bounds");
	Check(PlayGoGetProgress(handle, &invalid, 1, &value) == Libs::PlayGo::PLAYGO_ERROR_BAD_CHUNK_ID,
	      "accepted nonexistent chunk progress");
	Check(metadata_queries == 2, "termination did not reset metadata");
	Check(PlayGoTerminate() == OK, "second termination failed");

	manifest_chunks = 0;
	Check(PlayGoOpen(&handle, nullptr) == OK, "empty manifest open failed");
	uint32_t count = 1;
	Check(PlayGoGetChunkId(handle, nullptr, 0, &count) == OK && count == 0,
	      "empty manifest used invented fallback");
	Check(PlayGoGetLocus(handle, ids, 1, loci) == Libs::PlayGo::PLAYGO_ERROR_BAD_CHUNK_ID,
	      "empty manifest accepted nonexistent chunk");
	std::puts("PlayGo API regression tests passed");
	return 0;
}
