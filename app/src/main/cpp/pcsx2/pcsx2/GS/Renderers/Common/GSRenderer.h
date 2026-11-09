// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#define GS_FRAMEGEN_GEOMETRY_PUBLISH_SUPPORTED 1

#define GS_PRESENT_CAP_MERGE_MODES 1

#include "GS/GSState.h"
#include "GS/Renderers/Common/GSFieldShiftDetector.h"
#include <memory>
#include <string>

MULTI_ISA_DEF(class GSSwPrimRenderFunctions;)

class GSRenderer : public GSState
{
	MULTI_ISA_FRIEND(GSSwPrimRenderFunctions)
private:
	enum class MergeMode { Full, SkipFinalComposition, InterlaceHistoryOnly };
	template<MergeMode mode> bool Merge(int field);
	bool Merge(int field);
	static void NotePresentGeometryForCurrentFrame(const GSVector4i& src_rect,const GSVector4& draw_rect);
	MergeMode SelectMergeModeForVSync(bool cap_created_skip);
	bool BeginPresentFrame(bool frame_skip);
	void EndPresentFrame();

	GSFieldShiftDetector m_field_shift;
	static constexpr int VIDEO_MERGES = 30;
	u32 m_video_decode_count = 0;
	int m_video_merges_left = 0;
	u64 m_shader_time_start = 0;

	std::string m_snapshot;
#ifndef EMUCOREX_DISABLE_GS_DIAGNOSTICS
	u32 m_dump_frames = 0;
#endif
	u32 m_skipped_duplicate_frames = 0;
	u32 m_manual_frameskip_counter = 0;
	u32 m_consecutive_blank_frames = 0;

	// Tracking draw counters for idle frame detection.
	u64 m_last_draw_n = 0;
	u64 m_last_transfer_n = 0;
	u64 m_fps_cap_next_present = 0;
	u64 m_last_gpu_reset_time = 0;
	u64 m_last_surface_reattach_time = 0;
	bool m_gpu_device_failed = false;

protected:
	GSVector2i m_real_size{0, 0};
	bool m_texture_shuffle = false;
	bool m_process_texture = false;
	bool m_copy_16bit_to_target_shuffle = false;
	bool m_same_group_texture_shuffle = false;
	bool m_downscale_source = false;

	virtual GSTexture* GetOutput(int i, float& scale, int& y_offset) = 0;
	virtual GSTexture* GetFeedbackOutput(float& scale) { return nullptr; }

public:
	GSRenderer();
#ifdef EMUCOREX_ENABLE_NATIVE_SELF_TESTS
	static void TestPresentationParity(void (*check)(bool, const char*));
	static void TestFrameGenGeometry(void(*check)(bool,const char*));
	static void TestPresentationGeometry(void(*check)(bool,const char*));
	static void TestCommonComposition(void(*check)(bool,const char*));
	static void TestMovieComb(void(*check)(bool,const char*));
#endif
	virtual ~GSRenderer();

	virtual void Reset(bool hardware_reset) override;

	virtual void Destroy();

	virtual void UpdateRenderFixes();

	virtual void VSync(u32 field, bool registers_written, bool idle_frame);
	virtual bool CanUpscale() { return false; }
	virtual float GetUpscaleMultiplier() { return 1.0f; }
	virtual float GetTextureScaleFactor() { return 1.0f; }
	GSVector2i GetInternalResolution();
	float GetModXYOffset();

	virtual GSTexture* LookupPaletteSource(u32 CBP, u32 CPSM, u32 CBW, GSVector2i& offset, float* scale, const GSVector2i& size);

	bool IsIdleFrame() const;

	bool SaveSnapshotToMemory(u32 window_width, u32 window_height, bool apply_aspect, bool crop_borders,
		u32* width, u32* height, std::vector<u32>* pixels);

	void QueueSnapshot(const std::string& path, const u32 gsdump_frames);
	void StopGSDump();
	void PresentCurrentFrame();
	static GSVector4 GetLastDrawRect();
	bool BeginCapture(std::string filename, const GSVector2i& size = GSVector2i(0, 0));
	void EndCapture();
};

extern std::unique_ptr<GSRenderer> g_gs_renderer;
