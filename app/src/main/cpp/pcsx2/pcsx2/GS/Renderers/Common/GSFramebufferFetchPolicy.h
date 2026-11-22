// SPDX-FileCopyrightText: 2026 ARMSX2 Contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

// The framebuffer-fetch decisions, OpenGL's and then Vulkan's (DecideVulkanFramebufferFetch at
// the bottom), as pure functions.
//
// The GL decision lives in one place so that a driver blocklist or the user's setting cannot be
// overridden by a later check that tests the raw extension instead of the decision. All inputs
// explicit, no GL types, constexpr so the cases below are pinned at compile time and again in
// gs_framebuffer_fetch_policy_tests.cpp.

enum class GSFramebufferFetchBackend
{
	None,
	ARM, // gl_LastFragColorARM (GL_ARM_shader_framebuffer_fetch)
	EXT, // `inout` colour output (GL_EXT_shader_framebuffer_fetch / pixel local storage)
};

// Why fetch is off. Carried out of the policy so the caller can log the specific reason and raise
// the OSD message for the one case that is a user setting rather than a hardware fact.
enum class GSFramebufferFetchVeto
{
	None,
	NoExtension, // neither ARM nor EXT fetch is advertised
	DriverBlocklist, // a driver build known to render it incorrectly
	UserSetting, // GSConfig.DisableFramebufferFetch
};

struct GSFramebufferFetchDecision
{
	bool enabled = false;
	GSFramebufferFetchBackend backend = GSFramebufferFetchBackend::None;
	GSFramebufferFetchVeto veto = GSFramebufferFetchVeto::NoExtension;
};

// `driver_blocklisted` is the driver-bug database's UseRenderTargetCopyForFeedback workaround,
// which no GL rule sets today.
//
// The backend follows the extension set alone, never the GPU's name: GL_ARM_shader_framebuffer_fetch
// is preferred whenever it is advertised, EXT/PLS only when it is not. The ARM extension is the
// spec-ordered one (see FbFetchOrdersOverlappingPrims), so those draws are not split per primitive.
// On ANGLE (Pixel 10/11, Tensor G5/G6, PowerVR) both extensions are advertised, the shader header
// enables only GL_ARM_shader_framebuffer_fetch, and the EXT `inout` declaration then fails to
// compile ("'inout' : invalid qualifier"): every draw that reads the destination was dropped, which
// is where the missing reflections came from. Mali's EXT inout path is broken on its drivers too.
// Qualcomm's GL driver advertises EXT (and only ARM depth/stencil), so it keeps EXT.
constexpr GSFramebufferFetchDecision DecideGLFramebufferFetch(bool has_arm_fetch, bool has_ext_fetch,
	bool has_pls_fetch, bool driver_blocklisted, bool user_disabled)
{
	GSFramebufferFetchDecision decision;

	if (!has_arm_fetch && !has_ext_fetch)
		decision.veto = GSFramebufferFetchVeto::NoExtension;
	else if (driver_blocklisted)
		decision.veto = GSFramebufferFetchVeto::DriverBlocklist;
	else if (user_disabled)
		decision.veto = GSFramebufferFetchVeto::UserSetting;
	else
		decision.veto = GSFramebufferFetchVeto::None;

	decision.enabled = (decision.veto == GSFramebufferFetchVeto::None);

	if (!decision.enabled)
		decision.backend = GSFramebufferFetchBackend::None;
	else if (has_arm_fetch)
		decision.backend = GSFramebufferFetchBackend::ARM;
	else if (has_ext_fetch || has_pls_fetch)
		decision.backend = GSFramebufferFetchBackend::EXT;
	else
		decision.backend = GSFramebufferFetchBackend::ARM;

	return decision;
}

// Whether a GL framebuffer-fetch backend also orders overlapping primitives within one draw.
//
// This is per extension, not per API.
//
// ARM_shader_framebuffer_fetch's spec requires a sample covered by several primitives to be
// rendered in submission order, and a read of gl_LastFragColorARM to wait for earlier fragments on
// that pixel. That is the contract of Vulkan's rasterization-order attachment access and Metal's
// programmable blending, so the ARM path gets the same barrier-free treatment.
//
// EXT_shader_framebuffer_fetch gives no such guarantee: on Mesa (Apple M2) its reads were
// nondeterministic between identical replays, an ordering failure. So EXT keeps its barrier.
//
// A driver that violates the ARM guarantee is a driver bug and belongs in the driver-bug database
// as a fetch blocklist entry (see UseRenderTargetCopyForFeedback), not in a rule here.
constexpr bool FbFetchOrdersOverlappingPrims(GSFramebufferFetchBackend backend)
{
	return backend == GSFramebufferFetchBackend::ARM;
}
static_assert(FbFetchOrdersOverlappingPrims(GSFramebufferFetchBackend::ARM));
static_assert(!FbFetchOrdersOverlappingPrims(GSFramebufferFetchBackend::EXT));
static_assert(!FbFetchOrdersOverlappingPrims(GSFramebufferFetchBackend::None));

// Which shape the OpenGL backend's blend fallback takes when it has no texture barrier.
//
// With multidraw_fb_copy set, the backend copies the render target once per primitive group
// inside a full-barrier draw, giving per-primitive blend ordering. With it clear, GSRendererHW sees
// no feedback loop, drops require_full_barrier, and the backend takes one render-target copy per
// draw, the shape Vulkan, D3D12 and Metal use.
//
// The per-primitive copy is affordable on an immediate-mode GPU. On a tiler each readback flushes
// and resolves the tile to memory, so a draw with hundreds of primitive groups pays hundreds of
// full-screen flushes (well under 1 fps on Mali), so tilers take the per-draw copy.
//
// `tile_based_gpu` is the caller's detection. The OpenGL backend uses GLES as the proxy, which
// also classifies ANGLE as a tiler; that is fine, since ANGLE's per-primitive copy is no cheaper.
constexpr bool GLUsesPerPrimitiveFbCopy(bool has_texture_barrier, bool tile_based_gpu)
{
	// With a barrier the copy path is never entered. Report off anyway: call sites read the flag as
	// "copies are happening".
	if (has_texture_barrier)
		return false;

	return !tile_based_gpu;
}

// A tiler with no barrier takes the per-draw copy; an immediate-mode GPU keeps the per-primitive one.
static_assert(!GLUsesPerPrimitiveFbCopy(false, true));
static_assert(GLUsesPerPrimitiveFbCopy(false, false));
// A barrier means the copy path is unreachable either way.
static_assert(!GLUsesPerPrimitiveFbCopy(true, true));
static_assert(!GLUsesPerPrimitiveFbCopy(true, false));

// A blocklisted driver or the user's setting turns fetch off whatever is advertised.
static_assert(!DecideGLFramebufferFetch(true, true, true, true, false).enabled);
static_assert(!DecideGLFramebufferFetch(true, true, true, false, true).enabled);
static_assert(DecideGLFramebufferFetch(true, true, true, false, false).enabled);
// Both extensions advertised (Mali, PowerVR/Tensor under ANGLE): the ARM builtin, so the header's
// GL_ARM_shader_framebuffer_fetch directive and the shader agree, and overlap ordering applies.
static_assert(DecideGLFramebufferFetch(true, true, true, false, false).backend == GSFramebufferFetchBackend::ARM);
static_assert(DecideGLFramebufferFetch(true, true, false, false, false).backend == GSFramebufferFetchBackend::ARM);
static_assert(FbFetchOrdersOverlappingPrims(DecideGLFramebufferFetch(true, true, false, false, false).backend));
// Without the ARM extension (Qualcomm's native GLES driver, native IMG GLES drivers) EXT/PLS.
static_assert(DecideGLFramebufferFetch(false, true, true, false, false).backend == GSFramebufferFetchBackend::EXT);
static_assert(!DecideGLFramebufferFetch(false, false, true, false, false).enabled);
// The user's setting wins over the extension preference.
static_assert(!DecideGLFramebufferFetch(true, true, false, false, true).enabled);

// ---------------------------------------------------------------------------------------------
// The Vulkan spelling: rasterization-order attachment access, read in tile memory through
// subpassLoad.
//
// The shape is a deny list, deliberately (see the call site). Every device advertising the
// extension gets the fast path unless something is known to be wrong with its read:
//
//   * Samsung Xclipse, which has no working ROAA fetch at all;
//   * the Adreno 8xx proprietary blob, which returns stale reads above Basic blending;
//   * the parts the driver-bug database marks BrokenRoaaDestinationRead (MediaTek Mali,
//     Mali-G57), which return zero or stale destination colour.
//
// The last two are name-keyed suspicions, and a measurement outranks them: at device creation the
// backend runs a startup self-test (GSDeviceVK::RunFetchOverlapProbe) that draws many fully
// overlapping primitives in one draw call, each adding one to what the in-tile read returns, and
// requires the exact sum. A device that passes lifts both denials; a device that fails loses fetch
// whatever its name says. No probe (NotRun) keeps the name-keyed answer exactly. Xclipse stays a
// hard gate (never probed), and the user's setting still outranks everything. The Mali force key
// remains the way past BrokenRoaaDestinationRead for a user whose driver has been fixed.
enum class GSFetchOverlapProbe : unsigned char
{
	NotRun, // no measurement (no extension, user disabled, hard gate, or not taken yet)
	Passed, // overlapping primitives in one draw read each other's writes in order
	Failed, // the measurement was not exact, or could not be completed
};

struct GSVulkanFramebufferFetchInputs
{
	/// VK_EXT_rasterization_order_attachment_access is present. Without it there is no in-tile read.
	bool roaa_available = false;

	/// EmuCore/GS/DisableFramebufferFetch: returns to the copy path from any state.
	bool user_disabled = false;

	bool is_mali = false;
	bool is_adreno = false;

	/// Samsung Xclipse (Exynos, AMD RDNA2). No working ROAA fetch; a hard gate.
	bool is_xclipse = false;

	/// Adreno 8xx on the Qualcomm proprietary blob, which returns stale ROAA reads above Basic
	/// blending. Not seen on 6xx/7xx or Turnip, so this combination only, not the vendor.
	bool is_adreno8xx_proprietary = false;

	/// DriverBug::BrokenRoaaDestinationRead from the driver-bug database.
	bool broken_destination_read = false;

	/// EmuCore/GS/ForceMaliFramebufferFetch as set, not filtered by vendor, so the function can
	/// report that it was ignored.
	bool force_mali_fetch_key = false;

	/// Trust the extension on any vendor not denied below. True on Android builds, which makes the
	/// vendor terms a deny list there and keeps PowerVR and other unnamed vendors off the
	/// per-primitive barrier path. Elsewhere only Mali and Adreno are trusted.
	bool any_vendor_trusted = false;

	/// The device orders a single-sample subpassLoad of the attachment being written WITHOUT the
	/// extension (VulkanDeviceRules::ordered_input_attachment_read: PowerVR on Imagination's
	/// driver). Stands in for roaa_available only; every deny above still applies. The caller sets
	/// it only where the read is spelled as an input attachment.
	bool ordered_without_roaa = false;

	/// Result of the startup overlap self-test. Passed lifts the two name-keyed denials
	/// (is_adreno8xx_proprietary, broken_destination_read); Failed turns fetch off on any device.
	GSFetchOverlapProbe overlap_probe = GSFetchOverlapProbe::NotRun;
};

struct GSVulkanFramebufferFetchDecision
{
	bool enabled = false;

	/// The Mali force key was set on a non-Mali GPU and did nothing. Reported so the caller can log
	/// it once.
	bool force_key_ignored = false;
};

// The force key is Mali-only. Ungated, it would lift a BrokenRoaaDestinationRead deny on an
// Adreno part and put it on an in-tile road it cannot take. Gated here once rather than at each
// site that reads it. On Mali it lifts that deny. DisableFramebufferFetch is the way back for
// everyone.
constexpr GSVulkanFramebufferFetchDecision DecideVulkanFramebufferFetch(
	const GSVulkanFramebufferFetchInputs& in)
{
	GSVulkanFramebufferFetchDecision decision;
	decision.force_key_ignored = in.force_mali_fetch_key && !in.is_mali;

	const bool force_applies = in.force_mali_fetch_key && in.is_mali;
	const bool probe_passed = in.overlap_probe == GSFetchOverlapProbe::Passed;
	const bool probe_failed = in.overlap_probe == GSFetchOverlapProbe::Failed;
	const bool denied_destination_read = in.broken_destination_read && !force_applies && !probe_passed;
	const bool denied_adreno8xx = in.is_adreno8xx_proprietary && !probe_passed;

	const bool vendor_allows =
		!denied_destination_read && !in.is_xclipse && !denied_adreno8xx &&
		(in.is_mali || in.is_adreno || in.any_vendor_trusted);

	decision.enabled = vendor_allows && !probe_failed && (in.roaa_available || in.ordered_without_roaa) &&
		!in.user_disabled;
	return decision;
}

// PowerVR on Imagination's driver: no extension, ordered in tile anyway. On Android, and with the
// user's setting as the way back.
static_assert(DecideVulkanFramebufferFetch({.any_vendor_trusted = true, .ordered_without_roaa = true}).enabled);
static_assert(!DecideVulkanFramebufferFetch(
	{.user_disabled = true, .any_vendor_trusted = true, .ordered_without_roaa = true})
				   .enabled);
// The device fact replaces the extension, not the vendor gate.
static_assert(!DecideVulkanFramebufferFetch({.ordered_without_roaa = true}).enabled);
static_assert(!DecideVulkanFramebufferFetch(
	{.is_xclipse = true, .any_vendor_trusted = true, .ordered_without_roaa = true})
				   .enabled);

// Mali with the extension, not denied, no force key: the in-tile read is on.
static_assert(DecideVulkanFramebufferFetch({.roaa_available = true, .is_mali = true}).enabled);
// Denied by the database, and the key lifting that deny.
static_assert(!DecideVulkanFramebufferFetch(
	{.roaa_available = true, .is_mali = true, .broken_destination_read = true})
				  .enabled);
static_assert(DecideVulkanFramebufferFetch({.roaa_available = true, .is_mali = true,
	.broken_destination_read = true, .force_mali_fetch_key = true})
				  .enabled);

// The Mali key cannot force an Adreno part onto the in-tile road, and reports that it was ignored.
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .is_adreno = true,
	.broken_destination_read = true, .force_mali_fetch_key = true})
				   .enabled);
static_assert(DecideVulkanFramebufferFetch({.roaa_available = true, .is_adreno = true,
	.broken_destination_read = true, .force_mali_fetch_key = true})
				  .force_key_ignored);
// An Adreno part the database does not deny keeps its fetch.
static_assert(DecideVulkanFramebufferFetch(
	{.roaa_available = true, .is_adreno = true, .force_mali_fetch_key = true})
				  .enabled);
// And on Mali the key is not "ignored", whether or not there was anything to lift.
static_assert(!DecideVulkanFramebufferFetch(
	{.roaa_available = true, .is_mali = true, .force_mali_fetch_key = true})
				   .force_key_ignored);

// The hardware gates outrank the key on the vendors that have no working read at all.
static_assert(!DecideVulkanFramebufferFetch(
	{.roaa_available = true, .is_xclipse = true, .force_mali_fetch_key = true})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .is_adreno = true,
	.is_adreno8xx_proprietary = true, .force_mali_fetch_key = true})
				   .enabled);

// No extension, and the user's setting: both outrank everything, on every vendor.
static_assert(!DecideVulkanFramebufferFetch({.is_mali = true}).enabled);
static_assert(!DecideVulkanFramebufferFetch(
	{.roaa_available = true, .is_mali = true, .user_disabled = true})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .is_mali = true,
	.user_disabled = true, .force_mali_fetch_key = true})
				   .enabled);

// Deny list: an unnamed vendor gets the fast path on Android, which keeps PowerVR and others off the
// per-primitive barrier path.
static_assert(DecideVulkanFramebufferFetch({.roaa_available = true, .any_vendor_trusted = true}).enabled);
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true}).enabled);

// The overlap self-test outranks the name-keyed denials, in both directions.
// Passed lifts the Adreno 8xx proprietary and BrokenRoaaDestinationRead denials ...
static_assert(DecideVulkanFramebufferFetch({.roaa_available = true, .is_adreno = true,
	.is_adreno8xx_proprietary = true, .overlap_probe = GSFetchOverlapProbe::Passed})
				  .enabled);
static_assert(DecideVulkanFramebufferFetch({.roaa_available = true, .is_mali = true,
	.broken_destination_read = true, .overlap_probe = GSFetchOverlapProbe::Passed})
				  .enabled);
static_assert(DecideVulkanFramebufferFetch({.roaa_available = true, .is_adreno = true,
	.is_adreno8xx_proprietary = true, .broken_destination_read = true,
	.overlap_probe = GSFetchOverlapProbe::Passed})
				  .enabled);
// ... NotRun keeps them exactly ...
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .is_adreno = true,
	.is_adreno8xx_proprietary = true, .overlap_probe = GSFetchOverlapProbe::NotRun})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .is_mali = true,
	.broken_destination_read = true, .overlap_probe = GSFetchOverlapProbe::NotRun})
				   .enabled);
// ... and Failed turns fetch off on a device nothing denied (Mali, Adreno, an unnamed vendor).
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .is_mali = true,
	.overlap_probe = GSFetchOverlapProbe::Failed})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .is_adreno = true,
	.overlap_probe = GSFetchOverlapProbe::Failed})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .any_vendor_trusted = true,
	.overlap_probe = GSFetchOverlapProbe::Failed})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.is_mali = true, .any_vendor_trusted = true,
	.ordered_without_roaa = true, .overlap_probe = GSFetchOverlapProbe::Failed})
				   .enabled);
// Passed never lifts the hard gate, the user's setting, a missing extension, or Android trust.
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .is_xclipse = true,
	.overlap_probe = GSFetchOverlapProbe::Passed})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true, .user_disabled = true,
	.is_mali = true, .overlap_probe = GSFetchOverlapProbe::Passed})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.is_adreno = true, .is_adreno8xx_proprietary = true,
	.overlap_probe = GSFetchOverlapProbe::Passed})
				   .enabled);
static_assert(!DecideVulkanFramebufferFetch({.roaa_available = true,
	.overlap_probe = GSFetchOverlapProbe::Passed})
				   .enabled);
// A device nothing denied keeps its fetch when the probe passes.
static_assert(DecideVulkanFramebufferFetch({.roaa_available = true, .is_mali = true,
	.overlap_probe = GSFetchOverlapProbe::Passed})
				  .enabled);
