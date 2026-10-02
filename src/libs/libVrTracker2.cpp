#include "libs/agc.h"
#include "libs/errno.h"
#include "libs/libs.h"
#include "loader/symbolDatabase.h"

#include <atomic>
#include <cstdint>
#include <cinttypes>
#include <cstring>

namespace Libs {

namespace LibVrTracker2 {

LIB_VERSION("VrTracker2", 1, "VrTracker2", 1, 1);

struct MemoryInfo {
	uint64_t size;
	uint64_t align;
};

struct Pose {
	float orientation[4]; // xyzw quaternion
	float position[3];
	uint8_t reserved[4];
};

struct TrackerResult {
	uint32_t status;
	uint32_t reserved0;
	Pose pose;
	uint8_t reserved1[32];
};

static std::atomic<bool> g_initialized {false};
static constexpr uint64_t WORK_SIZE  = 0x10000;
static constexpr uint64_t WORK_ALIGN = 0x100;

static bool LooksLikeGuestPtr(const void* p) {
	return reinterpret_cast<uintptr_t>(p) > 0x10000u;
}

static void FillIdentityPose(Pose* pose) {
	*pose = {};
	pose->orientation[3] = 1.0f; // seated identity
}

static int32_t KYTY_SYSV_ABI VrTracker2QueryMemory(MemoryInfo* out_memory) {
	PRINT_NAME();
	if (!LooksLikeGuestPtr(out_memory)) {
		return OK;
	}
	out_memory->size  = WORK_SIZE;
	out_memory->align = WORK_ALIGN;
	LOGF("VrTracker2: QueryMemory size=0x%llx align=0x%llx\n",
	     static_cast<unsigned long long>(out_memory->size),
	     static_cast<unsigned long long>(out_memory->align));
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2Initialize(const void* param) {
	PRINT_NAME();
	(void)param;
	g_initialized.store(true);
	LOGF("VrTracker2: Initialize (seated identity pose stub)\n");
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2Finalize() {
	PRINT_NAME();
	g_initialized.store(false);
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2RegisterDevice(int32_t device_type, int32_t handle,
                                                      void* /*opt*/) {
	PRINT_NAME();
	LOGF("VrTracker2: RegisterDevice type=%d handle=%d\n", device_type, handle);
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2UnregisterDevice(int32_t handle) {
	PRINT_NAME();
	LOGF("VrTracker2: UnregisterDevice handle=%d\n", handle);
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2GetResult(int32_t handle, TrackerResult* result) {
	PRINT_NAME();
	(void)handle;
	if (!LooksLikeGuestPtr(result)) {
		return OK;
	}
	std::memset(result, 0, sizeof(*result));
	result->status = 0; // tracked / ok
	FillIdentityPose(&result->pose);
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2ResetLocalCoordinate() {
	PRINT_NAME();
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2SetCoordinateSystem(const void* /*cs*/) {
	PRINT_NAME();
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2GetCoordinateSystem(void* out_cs) {
	PRINT_NAME();
	if (LooksLikeGuestPtr(out_cs)) {
		std::memset(out_cs, 0, 64);
	}
	return OK;
}

static int32_t KYTY_SYSV_ABI VrTracker2GetPlayAreaBoundaryGeometry(void* out_geom) {
	PRINT_NAME();
	if (LooksLikeGuestPtr(out_geom)) {
		std::memset(out_geom, 0, 128);
	}
	return OK;
}

LIB_DEFINE(InitVrTracker2_1) {
	LIB_FUNC("TwqZnaIjWv4", LibVrTracker2::VrTracker2QueryMemory);
	LIB_FUNC("6Jy73SRfG-o", LibVrTracker2::VrTracker2Initialize);
	LIB_FUNC("IQ3UD6SZbXo", LibVrTracker2::VrTracker2Finalize);
	LIB_FUNC("Dog+g25QYjw", LibVrTracker2::VrTracker2RegisterDevice);
	LIB_FUNC("kFt4MB3SUEk", LibVrTracker2::VrTracker2UnregisterDevice);
	LIB_FUNC("J4Vh3VVX0iU", LibVrTracker2::VrTracker2GetResult);
	LIB_FUNC("IdI2f+xHIeA", LibVrTracker2::VrTracker2ResetLocalCoordinate);
	LIB_FUNC("UVCMLmS-Eas", LibVrTracker2::VrTracker2SetCoordinateSystem);
	LIB_FUNC("Y-3JCiU9bbU", LibVrTracker2::VrTracker2GetCoordinateSystem);
	LIB_FUNC("SCph4ZbkqzU", LibVrTracker2::VrTracker2GetPlayAreaBoundaryGeometry);
}

} // namespace LibVrTracker2

} // namespace Libs
