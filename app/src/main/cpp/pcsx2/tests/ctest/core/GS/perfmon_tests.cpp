// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#include "GS/GSPerfMon.h"

#include <gtest/gtest.h>

TEST(GSPerfMon, DisabledTrackingKeepsFrameWithoutUpdatingCounters)
{
	GSPerfMon perfmon;
	EXPECT_FALSE(perfmon.IsCounterTrackingEnabled());

	perfmon.SetFrame(40);
	perfmon.AddDisplayFramebufferSpriteBlit();
	perfmon.Put(GSPerfMon::Draw, 3.0);
	perfmon.Put(GSPerfMon::Prim, 12.0);
	perfmon.EndFrame(false);
	perfmon.Update();

	EXPECT_EQ(perfmon.GetFrame(), 41);
	EXPECT_EQ(perfmon.GetDisplayFramebufferSpriteBlits(), 1);
	EXPECT_EQ(perfmon.GetDisplayFramebufferSpriteBlits(), 0);
	EXPECT_EQ(perfmon.GetCounter(GSPerfMon::Draw), 0.0);
	EXPECT_EQ(perfmon.GetCounter(GSPerfMon::Prim), 0.0);
	EXPECT_EQ(perfmon.Get(GSPerfMon::Draw), 0.0);
}

TEST(GSPerfMon, EnabledTrackingPreservesCounterAverages)
{
	GSPerfMon perfmon;
	perfmon.SetCounterTrackingEnabled(true);

	perfmon.Put(GSPerfMon::Draw, 3.0);
	perfmon.Put(GSPerfMon::Prim, 12.0);
	perfmon.EndFrame(false);
	perfmon.EndFrame(true);
	perfmon.Update();

	EXPECT_EQ(perfmon.GetFrame(), 2);
	EXPECT_EQ(perfmon.Get(GSPerfMon::Draw), 3.0);
	EXPECT_EQ(perfmon.Get(GSPerfMon::Prim), 12.0);
}

TEST(GSPerfMon, TrackingTransitionsStartWithFreshCountersAndKeepFrame)
{
	GSPerfMon perfmon;
	perfmon.SetCounterTrackingEnabled(true);
	perfmon.Put(GSPerfMon::DrawCalls, 4.0);
	perfmon.EndFrame(false);

	perfmon.SetCounterTrackingEnabled(false);
	EXPECT_EQ(perfmon.GetFrame(), 1);
	EXPECT_EQ(perfmon.GetCounter(GSPerfMon::DrawCalls), 0.0);
	EXPECT_EQ(perfmon.Get(GSPerfMon::DrawCalls), 0.0);

	perfmon.Put(GSPerfMon::DrawCalls, 10.0);
	perfmon.EndFrame(false);
	perfmon.SetCounterTrackingEnabled(true);
	perfmon.Put(GSPerfMon::DrawCalls, 2.0);
	perfmon.EndFrame(false);
	perfmon.Update();

	EXPECT_EQ(perfmon.GetFrame(), 3);
	EXPECT_EQ(perfmon.Get(GSPerfMon::DrawCalls), 2.0);
}
