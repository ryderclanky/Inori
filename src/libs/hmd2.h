#ifndef EMULATOR_SRC_LIBS_HMD2_H_
#define EMULATOR_SRC_LIBS_HMD2_H_

#include <atomic>
#include <cstdint>

namespace Libs::Hmd2 {

inline constexpr uint32_t PANEL_WIDTH  = 4000;
inline constexpr uint32_t PANEL_HEIGHT = 2040;
inline constexpr uint64_t OUTPUT_MODE_89_91HZ  = 0x2000c;
inline constexpr uint64_t OUTPUT_MODE_119_88HZ = 0x2000f;
inline constexpr int32_t ERROR_REPROJECTION_NOT_INITIALIZED = -1972240373; // 0x8a72000b

struct ReprojectionState {
	std::atomic<uint64_t> output_mode {0};
	std::atomic<uint32_t> timing_us {3000};
};

// Guest color surface chosen for the desktop window when no OpenXR headset is connected.
// guest_format is Prospero::BufferFormat. tile_mode is Prospero::TileMode.
struct FlatPresentSource {
	uint64_t address            = 0;
	uint32_t width              = 0;
	uint32_t height             = 0;
	uint32_t guest_format       = 0;
	uint32_t tile_mode          = 0;
	bool     from_render_config = false;
};

[[nodiscard]] ReprojectionState* GetReprojectionState();
// True after sceHmd2Initialize until terminate. Video-out uses this to accept headset
// modes for the flat desktop mirror without requiring --vr.
[[nodiscard]] bool HeadsetInitialized();
// Display buffer, or the eye texture from the latest non-null render config.
[[nodiscard]] bool CopyFlatPresentSource(FlatPresentSource& out);

} // namespace Libs::Hmd2

#endif // EMULATOR_SRC_LIBS_HMD2_H_
