// SPDX-License-Identifier: GPL-3.0+
// Independent raw-register sweep model and actual mixer-call-site benchmark.
extern "C" void EmuCoreXOracleUpdateMixerVolumes(V_VolumeSlideLR*, u32);
extern "C" void EmuCoreXOracleUpdateVolume(V_VolumeSlide*);
namespace
{
__attribute__((noinline)) void ReferenceVolumeSlide(V_VolumeSlide& state)
{
    const u32 control = state.Reg_VOL;
    if ((control & 0x8000) == 0)
        return;
    const u32 step = control & 3;
    const u32 shift = (control >> 2) & 31;
    const bool negative = (control & 0x1000) != 0;
    const bool decreasing = (control & 0x2000) != 0;
    const bool exponential = (control & 0x4000) != 0;
    const s32 base_step = decreasing ? -8 + static_cast<s32>(step) : 7 - static_cast<s32>(step);
    u32 increment = 0x8000u >> (shift > 11 ? shift - 11 : 0);
    s32 delta = base_step * (1 << (shift < 11 ? 11 - shift : 0));
    if (exponential)
    {
        if (!decreasing && state.Value > 0x6000)
            increment /= 4;
        if (decreasing)
            delta = static_cast<s16>((static_cast<s64>(delta) * state.Value) >> 15);
    }
    // Preserve the existing rate-field behavior, including zero increments.
    if (step != 3 && shift != 31)
        increment = std::max(1u, increment);
    state.Counter += increment;
    if (!(exponential && decreasing) && negative)
        delta = -delta;
    if (state.Counter >= 0x8000)
    {
        state.Counter = 0;
        const s32 low = decreasing && (exponential || !negative) ? 0 : INT16_MIN;
        const s32 high = decreasing && !exponential && negative ? 0 : INT16_MAX;
        state.Value = std::clamp(state.Value + delta, low, high);
    }
}

bool SameVolumeSlide(const V_VolumeSlide& actual, const V_VolumeSlide& expected)
{
    return actual.Reg_VOL == expected.Reg_VOL && actual.Counter == expected.Counter && actual.Value == expected.Value;
}

__attribute__((noinline)) void UpdateReferenceVolumes(V_VolumeSlideLR* volumes, u32 frames)
{
    for (u32 frame = 0; frame < frames; ++frame)
        for (u32 channel = 0; channel < 50; ++channel)
        {
            ReferenceVolumeSlide(volumes[channel].Left);
            ReferenceVolumeSlide(volumes[channel].Right);
        }
}
}

extern "C" void EmuCoreXOracleTestVolumeSlide(void (*check)(bool, const char*))
{
    bool disabled_same = true, enabled_same = true;
    u64 transitions = 0;
    for (u32 control = 0; control <= 0xffff; ++control)
    for (s32 value : {INT16_MIN, -16384, -1, 0, 1, 0x6000, 0x6001, INT16_MAX})
    for (u32 counter : {0u, 1u, 0x7fffu, 0xffffffffu})
    {
        V_VolumeSlide actual{}, expected{};
        actual.Reg_VOL = expected.Reg_VOL = static_cast<u16>(control);
        actual.Counter = expected.Counter = counter;
        actual.Value = expected.Value = value;
        for (u32 tick = 0; tick < 8; ++tick)
        {
            EmuCoreXOracleUpdateVolume(&actual);
            ReferenceVolumeSlide(expected);
            const bool same = SameVolumeSlide(actual, expected);
            if (!same && disabled_same && enabled_same)
                std::printf("VOLUME_DIFF control=%04x initial_value=%d initial_counter=%08x tick=%u actual=%d/%u expected=%d/%u\n",
                    control, value, counter, tick, actual.Value, actual.Counter, expected.Value, expected.Counter);
            ((control & 0x8000) ? enabled_same : disabled_same) &= same;
            ++transitions;
        }
    }
    std::printf("VOLUME_SWEEP raw_controls=65536 transitions=%llu\n", static_cast<unsigned long long>(transitions));
    check(disabled_same, "disabled SPU2 volume sweeps preserve all register, counter and volume bits");
    check(enabled_same, "SPU2 volume sweep state matches raw-register reference for every control pattern and boundary");

    bool stereo_same = true, register_same = true;
    V_VolumeSlideLR stereo{}, expected_stereo{};
    stereo.Left.Counter = expected_stereo.Left.Counter = 0x7fff;
    stereo.Right.Counter = expected_stereo.Right.Counter = 0xffffffffu;
    stereo.Left.Value = expected_stereo.Left.Value = INT16_MIN;
    stereo.Right.Value = expected_stereo.Right.Value = INT16_MAX;
    for (u32 control = 0; control <= 0xffff; ++control)
    {
        const u16 left = static_cast<u16>(control);
        const u16 right = static_cast<u16>(control ^ 0xd5a5);
        stereo.Left.RegSet(left);
        stereo.Right.RegSet(right);
        expected_stereo.Left.Reg_VOL = left;
        expected_stereo.Right.Reg_VOL = right;
        if (!(left & 0x8000))
            expected_stereo.Left.Value = static_cast<s16>(left << 1);
        if (!(right & 0x8000))
            expected_stereo.Right.Value = static_cast<s16>(right << 1);
        register_same &= SameVolumeSlide(stereo.Left, expected_stereo.Left) && SameVolumeSlide(stereo.Right, expected_stereo.Right);
        stereo.Update();
        ReferenceVolumeSlide(expected_stereo.Left);
        ReferenceVolumeSlide(expected_stereo.Right);
        stereo_same &= SameVolumeSlide(stereo.Left, expected_stereo.Left) && SameVolumeSlide(stereo.Right, expected_stereo.Right);
    }
    check(register_same, "SPU2 volume register writes preserve sweep counters and set signed fixed volumes");
    check(stereo_same, "SPU2 stereo volume updates preserve independent enabled and disabled channel state");

    // 48 voice pairs plus two master pairs, as in one SPU2 sample. Alternate
    // timing order to limit frequency/temperature bias; never time setup work.
    for (u32 enabled_channels : {0u, 2u, 25u, 100u})
    {
        std::array<V_VolumeSlideLR, 50> volumes{}, baseline{};
        for (u32 channel = 0; channel < volumes.size(); ++channel)
        {
            for (u32 side = 0; side < 2; ++side)
            {
                V_VolumeSlide& v = side ? volumes[channel].Right : volumes[channel].Left;
                v.Reg_VOL = static_cast<u16>(0x28 | (2 * channel + side < enabled_channels ? 0x8000 : 0));
                v.Value = 0x1234;
                v.Counter = channel;
            }
        }
        baseline = volumes;
        auto measure = [&](auto function, V_VolumeSlideLR* state) {
            const auto start = std::chrono::steady_clock::now();
            function(state, 1024);
            return static_cast<double>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::steady_clock::now() - start).count()) / 1024;
        };
        std::array<double, 11> current{}, original{};
        for (u32 sample = 0; sample < current.size(); ++sample)
        {
            if (sample % 2)
            {
                original[sample] = measure(UpdateReferenceVolumes, baseline.data());
                current[sample] = measure(EmuCoreXOracleUpdateMixerVolumes, volumes.data());
            }
            else
            {
                current[sample] = measure(EmuCoreXOracleUpdateMixerVolumes, volumes.data());
                original[sample] = measure(UpdateReferenceVolumes, baseline.data());
            }
        }
        bool same = true;
        for (u32 channel = 0; channel < volumes.size(); ++channel)
            same &= SameVolumeSlide(volumes[channel].Left, baseline[channel].Left) &&
                SameVolumeSlide(volumes[channel].Right, baseline[channel].Right);
        std::sort(current.begin(), current.end());
        std::sort(original.begin(), original.end());
        std::printf("VOLUME_MIXER enabled=%u channels=100 current_median_ns_per_sample=%.1f reference_median_ns_per_sample=%.1f\n",
            enabled_channels, current[5], original[5]);
        check(same, "SPU2 volume benchmark preserves all 100 channel states");
        if (enabled_channels == 0)
            check(current[5] < original[5] * 0.8,
                "disabled SPU2 volume updates avoid per-channel out-of-line calls");
    }
}
