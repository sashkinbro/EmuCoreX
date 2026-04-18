// SPDX-FileCopyrightText: 2002-2026 PCSX2 Dev Team
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "IPU.h"

struct IPUDMAStatus {
	bool InProgress;
	bool DMAFinished;

	__fi void Reset()
	{
		InProgress = false;
		DMAFinished = true;
	}

	__fi bool IsTransferActive() const { return InProgress; }
	__fi void SetTransferActive(bool active) { InProgress = active; }
	__fi bool IsChainComplete() const { return DMAFinished; }
	__fi void SetChainComplete(bool complete) { DMAFinished = complete; }
	__fi bool CanCompleteInterrupt() const { return DMAFinished && !InProgress; }
	__fi void SetTransferState(bool active, bool complete)
	{
		InProgress = active;
		DMAFinished = complete;
	}
};

struct IPUStatus {
	bool DataRequested;
	bool WaitingOnIPUFrom;
	bool WaitingOnIPUTo;

	__fi void Reset()
	{
		DataRequested = false;
		WaitingOnIPUFrom = false;
		WaitingOnIPUTo = false;
	}

	__fi bool IsInputRequested() const { return DataRequested; }
	__fi void SetInputRequested(bool requested) { DataRequested = requested; }
	__fi bool IsWaitingOnOutputDrain() const { return WaitingOnIPUFrom; }
	__fi void SetWaitingOnOutputDrain(bool waiting) { WaitingOnIPUFrom = waiting; }
	__fi bool IsWaitingOnInputFeed() const { return WaitingOnIPUTo; }
	__fi void SetWaitingOnInputFeed(bool waiting) { WaitingOnIPUTo = waiting; }
	__fi bool HasAnyTransferWait() const { return WaitingOnIPUFrom || WaitingOnIPUTo; }
	__fi void ClearTransferWaits()
	{
		WaitingOnIPUFrom = false;
		WaitingOnIPUTo = false;
	}
};

extern void ipuCMDProcess();
extern void ipu0Interrupt();
extern void ipu1Interrupt();

extern void dmaIPU0();
extern void dmaIPU1();
extern void IPU0dma();
extern void IPU1dma();

extern void ipuDmaReset();
extern IPUDMAStatus IPU1Status;
extern IPUStatus IPUCoreStatus;
