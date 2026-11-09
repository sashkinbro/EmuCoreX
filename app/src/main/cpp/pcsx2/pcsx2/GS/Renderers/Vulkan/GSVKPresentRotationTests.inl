// SPDX-License-Identifier: GPL-3.0+
// SPDX-FileCopyrightText: 2026 EmuCoreX Team
extern "C" void EmuCoreXOracleVKPresentRotation(void(*check)(bool,const char*))
{
#ifdef GS_VK_PRESENT_ROTATION_PARITY
    const float expected[4][2]={{0.5f,0.25f},{0.25f,-0.5f},{-0.5f,-0.25f},{-0.25f,0.5f}};
    for(unsigned turn=0;turn<4;turn++)
    {
        float x=0.5f,y=0.25f;RotateNDCForPresent(static_cast<DisplayRotation>(turn),x,y);
        check(x==expected[turn][0]&&y==expected[turn][1],"GS actual Vulkan final-present helper rotates NDC clockwise with exact quarter-turns");
        for(unsigned n=0;n<4;n++)RotateNDCForPresent(static_cast<DisplayRotation>(turn),x,y);
        check(x==expected[turn][0]&&y==expected[turn][1],"GS actual Vulkan present transform preserves orientation after four turns");
    }
#else
    check(false,"GS actual Vulkan present NDC rotation consumer is available");
#endif
}
