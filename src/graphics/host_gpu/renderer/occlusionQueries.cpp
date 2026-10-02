#include "graphics/host_gpu/renderer/occlusionQueries.h"

#include "common/assert.h"
#include "common/logging/log.h"
#include "graphics/host_gpu/graphicContext.h"
#include "graphics/host_gpu/renderer/commandScheduler.h"

namespace Libs::Graphics {

OcclusionQueries::OcclusionQueries(GraphicContext& graphics, CommandScheduler& scheduler)
    : m_graphics(graphics), m_scheduler(scheduler) {
	vk::QueryPoolCreateInfo create {};
	create.queryType  = vk::QueryType::eOcclusion;
	create.queryCount = kQueryCount;
	const auto result = m_graphics.device.createQueryPool(&create, nullptr, &m_pool);
	EXIT_NOT_IMPLEMENTED(result != vk::Result::eSuccess || m_pool == nullptr);
	m_graphics.device.resetQueryPool(m_pool, 0, kQueryCount);
}

OcclusionQueries::~OcclusionQueries() {
	if (m_pool != nullptr) {
		// RenderContext drains the scheduler before destroying members.
		m_graphics.device.destroyQueryPool(m_pool, nullptr);
		m_pool = nullptr;
	}
}

void OcclusionQueries::WriteDbCounters(uint64_t dump_address, uint64_t sample_count) const {
	EXIT_IF(dump_address == 0 || (dump_address & 0x7u) != 0);
	constexpr uint64_t ready_bit    = 1ull << 63u;
	constexpr uint64_t counter_mask = ready_bit - 1u;
	auto*              results      = reinterpret_cast<volatile uint64_t*>(dump_address);
	const auto         value        = ready_bit | (sample_count & counter_mask);
	for (uint32_t db = 0; db < kDbCount; db++) {
		results[db * 2u] = value;
	}
}

uint32_t OcclusionQueries::AllocateIndex() {
	const auto index = m_next;
	m_next           = (m_next + 1u) % kQueryCount;
	const auto tick  = m_slot_ticks[index];
	if (tick != 0 && !m_scheduler.IsFree(tick)) {
		if (tick < m_scheduler.CurrentTick()) {
			m_scheduler.GetMasterSemaphore().Wait(tick);
		} else {
			// Slot still belongs to the in-flight command buffer; submit and wait.
			m_scheduler.Finish();
		}
	}
	return index;
}

void OcclusionQueries::BeginCurrent() {
	EXIT_IF(!m_scheduler.Active());
	EXIT_IF(m_open_index >= 0);
	const auto index = AllocateIndex();
	m_scheduler.EndRendering();
	auto cmd = m_scheduler.Current().Handle();
	cmd.resetQueryPool(m_pool, index, 1);
	cmd.beginQuery(m_pool, index, vk::QueryControlFlags {});
	m_open_index        = static_cast<int32_t>(index);
	m_slot_ticks[index] = m_scheduler.CurrentTick();
}

void OcclusionQueries::EndCurrent() {
	if (m_open_index < 0) {
		return;
	}
	EXIT_IF(!m_scheduler.Active());
	const auto index = static_cast<uint32_t>(m_open_index);
	m_scheduler.EndRendering();
	m_scheduler.Current().Handle().endQuery(m_pool, index);
	m_slot_ticks[index] = m_scheduler.CurrentTick();
	m_pending_indices.push_back(index);
	m_open_index = -1;
}

void OcclusionQueries::CloseForSubmit() {
	EndCurrent();
}

void OcclusionQueries::ResumeAfterSubmit() {
	if (m_window_active && m_open_index < 0 && m_scheduler.Active() &&
	    !m_scheduler.Current().IsInvalid()) {
		BeginCurrent();
	}
}

uint64_t OcclusionQueries::ReadSamples(uint32_t index) {
	uint64_t   samples = 0;
	const auto result  = m_graphics.device.getQueryPoolResults(
	    m_pool, index, 1, sizeof(samples), &samples, sizeof(samples),
	    vk::QueryResultFlagBits::e64 | vk::QueryResultFlagBits::eWait);
	if (result != vk::Result::eSuccess) {
		static uint32_t fail_log = 0;
		if (fail_log++ < 16) {
			LOGF("occlusion getQueryPoolResults failed: %s (index=%" PRIu32 ")\n",
			     vk::to_string(result).c_str(), index);
		}
		return 0;
	}
	return samples;
}

void OcclusionQueries::StartQuery(uint64_t begin_address) {
	if (m_window_active) {
		EndCurrent();
		m_pending_indices.clear();
		m_window_active = false;
	}
	m_begin_address = begin_address;
	m_pending_indices.clear();
	BeginCurrent();
	m_window_active = true;
	// Begin dump mirrors AMD ZPASS_DONE writing the current counter. Vulkan
	// counts from zero inside the query window, so publish ready|0 for all DBs.
	WriteDbCounters(begin_address, 0);
}

void OcclusionQueries::EndQuery(uint64_t begin_address, uint64_t dump_address) {
	if (!m_window_active || m_begin_address != begin_address) {
		static uint32_t unmatched_log = 0;
		if (unmatched_log++ < 16) {
			LOGF("occlusion end without matching begin: begin=0x%016" PRIx64
			     " dump=0x%016" PRIx64 " active=%u active_begin=0x%016" PRIx64 "\n",
			     begin_address, dump_address, m_window_active ? 1u : 0u, m_begin_address);
		}
		WriteDbCounters(dump_address, 0);
		return;
	}

	EndCurrent();
	m_window_active = false;

	// Wait for every fragment of this query window (possibly split across CBs).
	m_scheduler.Finish();

	uint64_t samples = 0;
	for (const auto index: m_pending_indices) {
		samples += ReadSamples(index);
	}
	m_pending_indices.clear();
	WriteDbCounters(dump_address, samples);
}

void OcclusionQueries::HandleZpassDump(uint64_t event_address) {
	EXIT_IF(event_address == 0 || (event_address & 0x7u) != 0);

	// Bit 3 of the dump address selects the interleaved end slots (base+8).
	// Matches AMD PixelPipeStatDump / shadPS4 begin vs end mapping.
	if ((event_address & 0x8u) == 0) {
		StartQuery(event_address);
	} else {
		EndQuery(event_address & ~0xFull, event_address);
	}
}

} // namespace Libs::Graphics