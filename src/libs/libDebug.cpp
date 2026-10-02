#include "common/abi.h"
#include "common/common.h"
#include "common/stringUtils.h"
#include "libs/libs.h"
#include "loader/symbolDatabase.h"

namespace Libs {

namespace LibRazorCpu {

LIB_VERSION("RazorCpu", 1, "RazorCpu", 1, 1);

static KYTY_SYSV_ABI uint32_t RazorCpuIsCapturing() {
	PRINT_NAME();

	return 0;
}

// Profiling markers used heavily by psvr2.prx; no-op success stubs.
static KYTY_SYSV_ABI int32_t RazorCpuPushMarker(const char* /*name*/) {
	PRINT_NAME();
	return 0;
}

static KYTY_SYSV_ABI int32_t RazorCpuPopMarker() {
	PRINT_NAME();
	return 0;
}

static KYTY_SYSV_ABI int32_t RazorCpuPushMarkerStatic(const char* /*name*/) {
	PRINT_NAME();
	return 0;
}

static KYTY_SYSV_ABI int32_t RazorCpuJobManagerJob(void* /*arg*/) {
	PRINT_NAME();
	return 0;
}

static KYTY_SYSV_ABI int32_t RazorCpuJobManagerDispatch(void* /*arg*/) {
	PRINT_NAME();
	return 0;
}

static KYTY_SYSV_ABI int32_t RazorCpuJobManagerSequence(void* /*arg*/) {
	PRINT_NAME();
	return 0;
}

LIB_DEFINE(InitRazorCpu_1) {
	LIB_FUNC("EboejOQvLL4", LibRazorCpu::RazorCpuIsCapturing);
	LIB_FUNC("zw+celG7zSI", LibRazorCpu::RazorCpuPushMarker);
	LIB_FUNC("YpkGsMXP3ew", LibRazorCpu::RazorCpuPopMarker);
	LIB_FUNC("uZrOwuNJX-M", LibRazorCpu::RazorCpuPushMarkerStatic);
	LIB_FUNC("KP+TBWGHlgs", LibRazorCpu::RazorCpuJobManagerJob);
	LIB_FUNC("dnEdyY4+klQ", LibRazorCpu::RazorCpuJobManagerDispatch);
	LIB_FUNC("9FowWFMEIM8", LibRazorCpu::RazorCpuJobManagerSequence);
}

} // namespace LibRazorCpu

} // namespace Libs
