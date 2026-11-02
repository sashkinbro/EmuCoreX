// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
void GSDeviceVK::TestRoadModelParity(void (*check)(bool, const char*))
{
    using namespace GSVulkanRoad;
    const auto test=[&](const char* label,std::string_view name,u32 chip,u32 expected,bool ordering,std::string_view driver_name="Turnip"){
        Identity id{0x5143,Driver::Turnip,name,driver_name,"Mesa (git-axfl1-005)","",false};
        id.device_id=chip;
        const auto f=ResolveFacts(id,true,7);
        check(f.model==expected&&f.driver_orders_loop==ordering,label);
    };
    test("GPU canonical Adreno name remains authoritative over conflicting chip ID","Adreno (TM) 740",0x06050001,740,false);
    test("GPU FD650 resolves only the exact listed Qualcomm six-series chip ID","FD650",0x06050001,650,true);
    test("GPU FD740 never guesses a seven-series model from its unsupported chip encoding","FD740",0x43050a01,0,false);
    test("GPU explicit canonical Adreno name in driver context is recognized without FD guessing","FD740",0x43050a01,740,false,"Turnip Adreno 740");
    test("GPU marketing A12 maps to the reference listed Adreno613","Adreno (TM) A12",0,613,false);
    test("GPU unknown marketing name may fall back to a listed six-series chip","Adreno (TM) A99",0x06050001,650,true);
    test("GPU unlisted six-series chip635 never inherits a650 ordering","FD635",0x06030500,0,false);
    test("GPU tempting digit-shaped seven-series chip ID remains unsupported","FD740",0x07040000,0,false);
    test("GPU overlong named model rejects its numeric prefix before valid chip fallback","Adreno 74000",0x06050001,650,true);
}
