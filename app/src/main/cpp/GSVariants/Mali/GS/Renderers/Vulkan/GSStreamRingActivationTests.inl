// SPDX-FileCopyrightText: 2026 EmuCoreX Team
// SPDX-License-Identifier: GPL-3.0+
// Unreferenced RED contract. The future member adapter MUST call the actual
// startup selector and actual six VKStreamBuffer/VMA allocation consumers.
// Synthetic Vulkan function tables end at CPU-backed buffers; never GPU-submit.
#pragma once
namespace GSStreamRingActivationOracle
{
    struct Identity
    {
        u32 vendor=0x5143,device_id=0;
        GSVulkanRoad::Driver driver=GSVulkanRoad::Driver::Unknown;
        std::string_view name="Adreno (TM) 610",driver_name,info;
    };
    struct Ring
    {
        bool created=false,bytes_intact=false,canaries_intact=false;
        u32 type_index=GS_INVALID_MEMORY_TYPE,actual_create_calls=0;
    };
    struct PolicyObservation
    {
        bool actual_startup_called=false,preferred=false;
        GSStreamRingMemoryDecision decision;
        std::array<Ring,6> rings;
        bool no_live_fake_resources=false;
    };
    struct ContractObservation
    {
        bool actual_vma_and_stream_methods=false,all_three_roads=false,all_six_roles=false;
        bool restricted_type_retry=false,allocation_failure_retry=false,total_failure_clean=false,replacement_preserves_old=false;
        bool commits_deferred=false,alignment_gap_coalesced=false,atom_ranges_exact=false;
        bool coherent_no_flush=false,zero_commit_no_flush=false,destroy_drains_pending=false;
        bool mapped_bytes_exact=false,canaries_intact=false,no_live_fake_resources=false;
    };
    struct Table
    {
        const char* label;
        std::array<u32,4> flags;
        u32 count;
        GSStreamRingMemoryDecision cached,legacy;
    };
    struct Case{Identity input;bool cached=false;const char* label;};
    template<class ActualPolicyAndAllocations,class ActualConsumerContracts>
    void Run(ActualPolicyAndAllocations&& actualPolicy,ActualConsumerContracts&& actualContracts,void(*check)(bool,const char*))
    {
        using Driver=GSVulkanRoad::Driver;using Road=GSStreamRingMemoryRoad;
        constexpr u32 invalid=GS_INVALID_MEMORY_TYPE;
        // Literal masks: D1 V2 coherent4 cached8. No expected value is produced
        // by GSDecideStreamRingMemory/GSPickStreamRingMemoryType.
        const std::array<Table,7> tables={{
            {"cached noncoherent",{1,7,11,0},3,{Road::CachedNonCoherent,2,11},{Road::WriteCombined,1,0}},
            {"cached coherent",{1,7,11,15},4,{Road::CachedCoherent,3,15},{Road::WriteCombined,1,0}},
            {"cached host local excluded",{1,7,14,0},3,{Road::WriteCombined,1,0},{Road::WriteCombined,1,0}},
            {"sole visible noncoherent",{11,0,0,0},1,{Road::CachedNonCoherent,0,11},{Road::WriteCombined,0,0}},
            {"coherent host versus cached device tie",{6,11,0,0},2,{Road::CachedNonCoherent,1,11},{Road::WriteCombined,0,0}},
            {"no visible type",{1,0,0,0},2,{Road::WriteCombined,invalid,0},{Road::WriteCombined,invalid,0}},
            {"lowest cached coherent tie",{15,15,7,0},3,{Road::CachedCoherent,0,15},{Road::WriteCombined,0,0}},
        }};
        const std::array<Case,14> cases={{
            {{0x5143,0,Driver::Turnip,"Adreno (TM) 610","Turnip",""},true,"canonical610 rawTurnip"},
            {{0x5143,0,Driver::Unknown,"Adreno (TM) 610","Mesa Turnip",""},true,"unknownID resolvedTurnip"},
            {{0x5143,0x06050000,Driver::Turnip,"Adreno (TM) 610","Turnip",""},true,"canonical610 wins conflicting650chip"},
            {{0x5143,0x06010000,Driver::Turnip,"FD610","Turnip",""},true,"listed610chip fallback"},
            {{0x5143,0,Driver::Qualcomm,"Adreno (TM) 610","Qualcomm",""},false,"610 proprietary"},
            {{0x5143,0,Driver::Unknown,"Adreno (TM) 610","Qualcomm",""},false,"610 unknownID proprietary hint"},
            {{0x5143,0,Driver::Turnip,"Adreno (TM) 650","Turnip",""},false,"650 Turnip coherent-cache counterexample"},
            {{0x5143,0,Driver::Turnip,"Adreno (TM) 612","Turnip",""},false,"612 Turnip unmeasured"},
            {{0x5143,0,Driver::Turnip,"Adreno (TM) 615","Turnip",""},false,"615 Turnip unmeasured"},
            {{0x13b5,0,Driver::Arm,"Mali-G57","ARM","malisx2"},false,"RG556 MaliG57"},
            {{0x13b5,0,Driver::Arm,"Mali-G615","ARM","r44p1"},false,"G615 identical table opposite result"},
            {{0x13b5,0,Driver::Turnip,"Mali-G615","Turnip",""},false,"wrong vendor Turnip"},
            {{0x5143,0,Driver::Turnip,"Adreno","Turnip",""},false,"unknown model"},
            {{0x5143,0,Driver::Turnip,"Adreno (TM) A12","Turnip",""},false,"marketingA12 resolves613"},
        }};
        bool gate=true,integration=true,storage=true;u64 observed=0;
        for(const auto& item:cases)for(const auto& table:tables)
        {
            // Expected fields are deliberately not passed to this adapter: it
            // can consume only identity and the literal memory-property table.
            const auto result=actualPolicy(item.input,table.flags.data(),table.count);++observed;
            const auto expected=item.cached?table.cached:table.legacy;
            const bool decision=result.decision.road==expected.road&&result.decision.type_index==expected.type_index&&
                result.decision.extra_required_flags==expected.extra_required_flags;
            gate&=result.actual_startup_called&&result.preferred==item.cached&&decision;
            for(const auto& ring:result.rings)
            {
                const bool create=expected.type_index!=invalid;
                integration&=ring.created==create&&ring.type_index==(create?expected.type_index:invalid)&&ring.actual_create_calls>0;
                if(create)storage&=ring.bytes_intact&&ring.canaries_intact;
            }
            storage&=result.no_live_fake_resources;
            if(result.preferred!=item.cached||!decision)
                std::printf("GS_CACHED_STREAM case=%s table=%s preferred=%u/%u road=%u/%u type=%u/%u flags=%x/%x\n",
                    item.label,table.label,unsigned(result.preferred),unsigned(item.cached),unsigned(result.decision.road),unsigned(expected.road),
                    result.decision.type_index,expected.type_index,result.decision.extra_required_flags,expected.extra_required_flags);
        }
        // This second adapter explicitly seeds literal decisions only for
        // LOWER-consumer tests. It must never be used as startup-gate evidence.
        const auto contract=actualContracts();
        check(gate,"GS actual startup cached-stream preference matches exact610 resolvedTurnip only across independent identity and memory tables");
        check(integration,"GS actual startup decision reaches all six actual stream allocations with expected memory type or clean failure");
        check(contract.actual_vma_and_stream_methods&&contract.all_three_roads&&contract.all_six_roles,
            "GS literal lower-consumer contracts execute actual VMA and all six stream methods across all three memory roads");
        check(contract.restricted_type_retry&&contract.allocation_failure_retry&&contract.total_failure_clean&&contract.replacement_preserves_old,
            "GS actual stream cached-type and allocation retries preserve default fallback old allocation and failure cleanup");
        check(contract.commits_deferred&&contract.alignment_gap_coalesced&&contract.atom_ranges_exact&&contract.zero_commit_no_flush&&contract.destroy_drains_pending,
            "GS actual noncoherent commit ranges defer coalesce and flush exact VMA atom bounds including zero commit and destruction");
        check(contract.coherent_no_flush,"GS actual coherent cached and default allocation roads issue no unnecessary cache clean");
        check(storage&&contract.mapped_bytes_exact&&contract.canaries_intact&&contract.no_live_fake_resources,
            "GS actual stream mapped bytes guards and fake-resource lifetime remain exact before real physical tests resume");
        std::printf("GS_CACHED_STREAM cases=%llu consumerRoles=6 memoryRoads=3 syntheticCPUonly=1\n",static_cast<unsigned long long>(observed));
    }
}
