#include "common/assert.h"
#include "common/emulatorConfig.h"
#include "graphics/guest_gpu/tile.h"
#include "kernel/memory.h"
#include "libs/agc.h"
#include "libs/errno.h"
#include "libs/hmd2.h"
#include "libs/libs.h"
#include "loader/symbolDatabase.h"
#include "loader/timer.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <mutex>
#include <new>

namespace Libs {

LIB_VERSION("Hmd2", 1, "Hmd2", 1, 1);

namespace Hmd2 {

constexpr int32_t ERROR_ALREADY_INITIALIZED = -1972240383; // 0x8a720001
constexpr int32_t ERROR_NOT_INITIALIZED     = -1972240382; // 0x8a720002
constexpr int32_t ERROR_PARAMETER_NULL      = -1972240376; // 0x8a720008
constexpr int32_t ERROR_REPROJECTION_ALREADY_INITIALIZED = -1972240372; // 0x8a72000c
constexpr int32_t ERROR_REPROJECTION_IN_VR_MODE = -1972240356; // 0x8a72001c

struct SceHmd2InitializeParam {
	void*   reserved0;
	uint8_t reserved[8];
};

struct SceHmd2DeviceInformation {
	uint32_t status;
	uint32_t reserve0;
	struct {
		struct {
			uint32_t width;
			uint32_t height;
		} panelResolution;
		struct {
			uint16_t refreshRate90Hz;
			uint16_t refreshRate120Hz;
		} flipToDisplayLatency;
	} deviceInfo;
	uint8_t hmuMount;
	uint8_t lensSeparationDistance;
	uint8_t reserve1[2];
	float   virtualImageDistance;
};

struct SceHmd2FieldOfView {
	float tanOut;
	float tanIn;
	float tanTop;
	float tanBottom;
};

struct SceHmd2ReprojectionInitializeParam {
	void*    pReprojectionBuff;
	void*    pDisplayBuff;
	int32_t  threadPriority;
	int32_t  cpuAffinityMask;
	uint32_t pipeId;
	uint32_t queueId;
	uint32_t type;
	void*    pSeeThroughBuff;
	int32_t  reprojectionTiming;
	uint32_t reserved[5];
};

static_assert(sizeof(SceHmd2InitializeParam) == 16);
static_assert(offsetof(SceHmd2InitializeParam, reserved) == 8);
static_assert(sizeof(SceHmd2DeviceInformation) == 28);
static_assert(offsetof(SceHmd2DeviceInformation, deviceInfo) == 8);
static_assert(offsetof(SceHmd2DeviceInformation, hmuMount) == 20);
static_assert(offsetof(SceHmd2DeviceInformation, virtualImageDistance) == 24);
static_assert(sizeof(SceHmd2FieldOfView) == 16);
static_assert(alignof(SceHmd2FieldOfView) == 4);
static_assert(sizeof(SceHmd2ReprojectionInitializeParam) == 72);
static_assert(alignof(SceHmd2ReprojectionInitializeParam) == 8);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, threadPriority) == 16);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, type) == 32);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, pSeeThroughBuff) == 40);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, reprojectionTiming) == 48);
static_assert(offsetof(SceHmd2ReprojectionInitializeParam, reserved) == 52);

struct SceFVector3 {
	float x;
	float y;
	float z;
};

// SDK gaze result. SceFVector3 is 12 bytes; the trailing flags pad the struct to 40.
struct SceHmd2GazeResult {
	SceFVector3 gazeOrigin;
	SceFVector3 gazeDirection;
	float       leftPupilDiameter;
	float       rightPupilDiameter;
	bool        leftBlink;
	bool        rightBlink;
	uint8_t     reserve0[2];
	bool        isValid;
	uint8_t     reserve1[3];
};

// One combined gaze point for the foveated region, not the full gaze ray.
struct SceHmd2GazeResultForFoveatedRendering {
	float   x;
	float   y;
	bool    isValid;
	uint8_t reserve[3];
};

static_assert(sizeof(SceFVector3) == 12);
static_assert(alignof(SceFVector3) == 4);
static_assert(sizeof(SceHmd2GazeResult) == 40);
static_assert(alignof(SceHmd2GazeResult) == 4);
static_assert(offsetof(SceHmd2GazeResult, gazeDirection) == 12);
static_assert(offsetof(SceHmd2GazeResult, leftPupilDiameter) == 24);
static_assert(offsetof(SceHmd2GazeResult, leftBlink) == 32);
static_assert(offsetof(SceHmd2GazeResult, isValid) == 36);
static_assert(sizeof(SceHmd2GazeResultForFoveatedRendering) == 12);
static_assert(alignof(SceHmd2GazeResultForFoveatedRendering) == 4);

static std::atomic<bool> g_initialized = false;
static std::mutex g_reprojection_mutex;
static std::atomic<ReprojectionState*> g_reprojection_state {nullptr};
static std::atomic<int32_t> g_hmd_handle {0};
static constexpr int32_t HMD_HANDLE = 0x0F000000;
static constexpr int32_t ERROR_ALREADY_OPENED = -1972240381; // 0x8a720003
static constexpr int32_t ERROR_INVALID_HANDLE = -1972240375; // 0x8a720009

template <typename T>
static int32_t FillMappedGuest(T* out) {
	if (out == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	if (!LibKernel::Memory::IsGuestRangeMapped(reinterpret_cast<uint64_t>(out), sizeof(T))) {
		return OK;
	}
	*out = {};
	return OK;
}

static void FillDeviceInformation(SceHmd2DeviceInformation* info) {
	*info = {};
	// Always report a seated virtual headset so VR init can proceed without OpenXR.
	info->status                 = 0; // READY
	info->hmuMount               = 1;
	info->lensSeparationDistance = 63;
	info->deviceInfo.panelResolution = {PANEL_WIDTH, PANEL_HEIGHT};
	info->virtualImageDistance       = 2.0f;
}

ReprojectionState* GetReprojectionState() {
	return g_reprojection_state.load(std::memory_order_acquire);
}

static Graphics::SizeAlign ReprojectionDisplaySize() {
	// The host display target is one uncompressed tiled 10-bit panel surface.
	Graphics::TileSizeAlign size {};
	Graphics::TileGetTextureTotalSize(Graphics::Prospero::BufferFormat::k10_10_10_2UNorm,
	                                  PANEL_WIDTH, PANEL_HEIGHT, 1, 1,
	                                  Graphics::Prospero::TileMode::kRenderTarget, false, size);
	EXIT_IF(size.size == 0 || size.align == 0);
	return {size.size, size.align};
}

static int32_t KYTY_SYSV_ABI Hmd2Initialize(const SceHmd2InitializeParam* param) {
	PRINT_NAME();
	if (param == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	if (g_initialized.exchange(true)) {
		return ERROR_ALREADY_INITIALIZED;
	}
	LOGF("Hmd2: initialized, virtual headset=%s\n", Config::VrEnabled() ? "enabled" : "disabled");
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2GetDeviceInformation(SceHmd2DeviceInformation* info) {
	PRINT_NAME();
	if (!g_initialized.load()) {
		return ERROR_NOT_INITIALIZED;
	}
	if (info == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	FillDeviceInformation(info);
	LOGF("Hmd2: device status=%u panel=%ux%u mounted=%u vr_flag=%s\n", info->status,
	     info->deviceInfo.panelResolution.width, info->deviceInfo.panelResolution.height,
	     info->hmuMount, Config::VrEnabled() ? "enabled" : "disabled");
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2GetFieldOfViewWithoutHandle(SceHmd2FieldOfView* fov) {
	PRINT_NAME();
	if (!g_initialized.load()) {
		return ERROR_NOT_INITIALIZED;
	}
	if (fov == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	// Virtual ER15 field of view: 55/42.5/51.2/51.2 degrees.
	// These tangents describe that geometry, not bit-exact firmware output.
	*fov = {1.4281480312f, 0.9163311720f, 1.2437491417f, 1.2437491417f};
	LOGF("Hmd2: FOV tangents out=%f in=%f top=%f bottom=%f\n", fov->tanOut, fov->tanIn,
	     fov->tanTop, fov->tanBottom);
	return OK;
}

static Graphics::SizeAlign KYTY_SYSV_ABI Hmd2ReprojectionQueryBufferSizeAlign() {
	const Graphics::SizeAlign size {sizeof(ReprojectionState), alignof(ReprojectionState)};
	LOGF("Hmd2: reprojection work size=%" PRIu64 " alignment=%zu\n", size.m_size, size.m_align);
	return size;
}

static Graphics::SizeAlign KYTY_SYSV_ABI Hmd2ReprojectionQueryDisplayBufferSizeAlign() {
	const auto size = ReprojectionDisplaySize();
	LOGF("Hmd2: reprojection display size=%" PRIu64 " alignment=%zu\n", size.m_size,
	     size.m_align);
	return size;
}

static int32_t KYTY_SYSV_ABI
Hmd2ReprojectionInitialize(const SceHmd2ReprojectionInitializeParam* param, void*) {
	std::scoped_lock lock {g_reprojection_mutex};
	if (GetReprojectionState() != nullptr) {
		return ERROR_REPROJECTION_ALREADY_INITIALIZED;
	}
	if (param == nullptr || param->pReprojectionBuff == nullptr || param->pDisplayBuff == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	EXIT_NOT_IMPLEMENTED(param->pSeeThroughBuff != nullptr);
	const auto display_size = ReprojectionDisplaySize();
	auto* state = new (param->pReprojectionBuff) ReprojectionState;
	if (param->reprojectionTiming != 0) {
		state->timing_us.store(static_cast<uint32_t>(param->reprojectionTiming),
		                      std::memory_order_relaxed);
	}
	g_reprojection_state.store(state, std::memory_order_release);
	LOGF("Hmd2: reprojection initialized work=%p display=%p size=%" PRIu64 " timing=%u\n",
	     static_cast<void*>(state), param->pDisplayBuff, display_size.m_size,
	     state->timing_us.load(std::memory_order_relaxed));
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionEnableVrMode(uint64_t output_mode) {
	auto* state = GetReprojectionState();
	if (state == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	if (output_mode != OUTPUT_MODE_89_91HZ && output_mode != OUTPUT_MODE_119_88HZ) {
		return VideoOut::VIDEO_OUT_ERROR_INVALID_VALUE;
	}
	// Soft virtual headset: allow VR mode even without --vr so psvr2 init can proceed.
	uint64_t expected = 0;
	if (!state->output_mode.compare_exchange_strong(expected, output_mode)) {
		return ERROR_REPROJECTION_IN_VR_MODE;
	}
	LOGF("Hmd2: VR output mode=0x%" PRIx64 " (vr_flag=%s)\n", output_mode,
	     Config::VrEnabled() ? "enabled" : "disabled");
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionSetTiming(int32_t timing) {
	auto* state = GetReprojectionState();
	if (state == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	EXIT_NOT_IMPLEMENTED(timing < 2000 || timing > 7000);
	const auto rounded = static_cast<uint32_t>((timing + 99) / 100 * 100);
	state->timing_us.store(rounded, std::memory_order_release);
	LOGF("Hmd2: reprojection timing=%u us\n", rounded);
	return OK;
}


static int32_t KYTY_SYSV_ABI Hmd2Open(int32_t user_id, int32_t type, int32_t index,
                                      const void* param) {
	PRINT_NAME();
	if (!g_initialized.load()) {
		return ERROR_NOT_INITIALIZED;
	}
	(void)param;
	LOGF("Hmd2: open user=%d type=%d index=%d\n", user_id, type, index);
	int32_t expected = 0;
	if (!g_hmd_handle.compare_exchange_strong(expected, HMD_HANDLE)) {
		return ERROR_ALREADY_OPENED;
	}
	return HMD_HANDLE;
}

static int32_t KYTY_SYSV_ABI Hmd2Close(int32_t handle) {
	PRINT_NAME();
	if (handle != HMD_HANDLE || g_hmd_handle.load() != HMD_HANDLE) {
		return ERROR_INVALID_HANDLE;
	}
	g_hmd_handle.store(0);
	LOGF("Hmd2: closed handle=0x%x\n", handle);
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2Terminate() {
	PRINT_NAME();
	g_hmd_handle.store(0);
	g_initialized.store(false);
	LOGF("Hmd2: terminated\n");
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2GetDeviceInformationByHandle(int32_t handle,
                                                              SceHmd2DeviceInformation* info) {
	PRINT_NAME();
	if (!g_initialized.load()) {
		return ERROR_NOT_INITIALIZED;
	}
	if (handle != HMD_HANDLE && g_hmd_handle.load() != handle) {
		return ERROR_INVALID_HANDLE;
	}
	if (info == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	FillDeviceInformation(info);
	LOGF("Hmd2: device-by-handle status=%u panel=%ux%u\n", info->status,
	     info->deviceInfo.panelResolution.width, info->deviceInfo.panelResolution.height);
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionSetRenderConfig(const void* config) {
	PRINT_NAME();
	if (GetReprojectionState() == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	LOGF("Hmd2: SetRenderConfig config=%p\n", config);
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionGetStatus(uint32_t* status) {
	PRINT_NAME();
	if (GetReprojectionState() == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	if (status == nullptr) {
		return ERROR_PARAMETER_NULL;
	}
	if (LibKernel::Memory::IsGuestRangeMapped(reinterpret_cast<uint64_t>(status),
	                                          sizeof(*status))) {
		*status = 0; // idle / ready
	}
	return OK;
}

// Returns predicted display time in microseconds (by value). Writing through a
// mistaken out-pointer previously faulted on address 0x1 during psvr2 frame setup.
static uint64_t KYTY_SYSV_ABI Hmd2ReprojectionGetPredictedDisplayTime() {
	PRINT_NAME();
	auto* state = GetReprojectionState();
	uint32_t timing = 3000;
	if (state != nullptr) {
		timing = state->timing_us.load(std::memory_order_relaxed);
	}
	return static_cast<uint64_t>(Loader::Timer::GetTimeMs() * 1000.0) + timing;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionSetParam(const void* param) {
	PRINT_NAME();
	if (GetReprojectionState() == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	(void)param;
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionBeginFrame(const void* param) {
	PRINT_NAME();
	if (GetReprojectionState() == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	(void)param;
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionDisableVrMode() {
	PRINT_NAME();
	auto* state = GetReprojectionState();
	if (state == nullptr) {
		return ERROR_REPROJECTION_NOT_INITIALIZED;
	}
	state->output_mode.store(0, std::memory_order_release);
	LOGF("Hmd2: VR mode disabled\n");
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2ReprojectionTerminate() {
	PRINT_NAME();
	std::scoped_lock lock {g_reprojection_mutex};
	if (auto* state = g_reprojection_state.exchange(nullptr)) {
		state->~ReprojectionState();
	}
	LOGF("Hmd2: reprojection terminated\n");
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2SetVibration(int32_t handle, const void* param) {
	PRINT_NAME();
	(void)handle;
	(void)param;
	return OK;
}

static int32_t KYTY_SYSV_ABI Hmd2GazeGetResult(SceHmd2GazeResult* result) {
	PRINT_NAME();
	return FillMappedGuest(result);
}

static int32_t KYTY_SYSV_ABI
Hmd2GazeGetResultForFoveatedRendering(SceHmd2GazeResultForFoveatedRendering* result) {
	PRINT_NAME();
	return FillMappedGuest(result);
}

} // namespace Hmd2

LIB_DEFINE(InitHmd2_1) {
	LIB_FUNC("c812oYs7Vsc", Hmd2::Hmd2Initialize);
	LIB_FUNC("bIi4YUfSRys", Hmd2::Hmd2GetDeviceInformation);
	LIB_FUNC("4BlE4IPXP0Q", Hmd2::Hmd2GetDeviceInformationByHandle);
	LIB_FUNC("f3kPeoTZnIE", Hmd2::Hmd2Open);
	LIB_FUNC("oPhtjySuHa8", Hmd2::Hmd2Close);
	LIB_FUNC("QU2M1pPNbaY", Hmd2::Hmd2Terminate);
	LIB_FUNC("gF8+lvc7GuQ", Hmd2::Hmd2GetFieldOfViewWithoutHandle);
	LIB_FUNC("U-CnbmeyYaA", Hmd2::Hmd2ReprojectionQueryBufferSizeAlign);
	LIB_FUNC("-C2nkoEYOnU", Hmd2::Hmd2ReprojectionQueryDisplayBufferSizeAlign);
	LIB_FUNC("C0rPwER-yxg", Hmd2::Hmd2ReprojectionInitialize);
	LIB_FUNC("4Q11W4M2h5Q", Hmd2::Hmd2ReprojectionTerminate);
	LIB_FUNC("VVvFh51o20s", Hmd2::Hmd2ReprojectionEnableVrMode);
	LIB_FUNC("wj1kOyNF4vM", Hmd2::Hmd2ReprojectionDisableVrMode);
	LIB_FUNC("FkQX7rjFomk", Hmd2::Hmd2ReprojectionSetTiming);
	LIB_FUNC("hA9LshbSkzw", Hmd2::Hmd2ReprojectionSetRenderConfig);
	LIB_FUNC("8GkaY2B7opM", Hmd2::Hmd2ReprojectionGetStatus);
	LIB_FUNC("SVEG+1D7qHA", Hmd2::Hmd2ReprojectionGetPredictedDisplayTime);
	LIB_FUNC("xMo9ENEu2E0", Hmd2::Hmd2ReprojectionSetParam);
	LIB_FUNC("Ocf081WpBpA", Hmd2::Hmd2ReprojectionBeginFrame);
	LIB_FUNC("Al4qjNREVQQ", Hmd2::Hmd2SetVibration);
	LIB_FUNC("lAoFUedcfqA", Hmd2::Hmd2GazeGetResult);
	LIB_FUNC("retc+-uRMhk", Hmd2::Hmd2GazeGetResultForFoveatedRendering);
}

} // namespace Libs
