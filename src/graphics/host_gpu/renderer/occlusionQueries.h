#ifndef EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_OCCLUSIONQUERIES_H_
#define EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_OCCLUSIONQUERIES_H_

#include "common/common.h"
#include "graphics/host_gpu/vulkanCommon.h"

#include <cstdint>
#include <unordered_map>
#include <vector>

namespace Libs::Graphics {

class CommandScheduler;
struct GraphicContext;

// Maps AGC/GCN PixelPipeStatDump ZPASS_DONE (event 0x39 / index 1) onto a host
// VkQueryPool. Address bit 0x8 selects begin vs end of a query window; results
// are published in the PS5 interleaved begin/end-per-DB layout with bit 63 set.
//
// Vulkan requires begin/end of a query in the same command buffer, so an open
// query is split across submits: ended before submit and restarted afterward.
class OcclusionQueries {
public:
	static constexpr uint32_t kQueryCount = 256;
	static constexpr uint32_t kDbCount    = 16;

	OcclusionQueries(GraphicContext& graphics, CommandScheduler& scheduler);
	~OcclusionQueries();
	KYTY_CLASS_NO_COPY(OcclusionQueries);

	void HandleZpassDump(uint64_t event_address);

	// Called around host command-buffer submit so open queries stay valid.
	void CloseForSubmit();
	void ResumeAfterSubmit();

private:
	[[nodiscard]] uint32_t AllocateIndex();
	void                   BeginCurrent();
	void                   EndCurrent();
	void                   StartQuery(uint64_t begin_address);
	void                   EndQuery(uint64_t begin_address, uint64_t dump_address);
	void                   WriteDbCounters(uint64_t dump_address, uint64_t sample_count) const;
	[[nodiscard]] uint64_t ReadSamples(uint32_t index);

	GraphicContext&   m_graphics;
	CommandScheduler& m_scheduler;
	vk::QueryPool     m_pool = nullptr;
	uint32_t          m_next = 0;

	bool     m_window_active = false;
	uint64_t m_begin_address = 0;
	int32_t  m_open_index    = -1;
	std::vector<uint32_t> m_pending_indices;
	uint64_t m_slot_ticks[kQueryCount] {};
};

} // namespace Libs::Graphics

#endif // EMULATOR_SRC_GRAPHICS_HOST_GPU_RENDERER_OCCLUSIONQUERIES_H_