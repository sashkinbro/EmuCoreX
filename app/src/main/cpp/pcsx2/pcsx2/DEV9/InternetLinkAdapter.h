// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+

#pragma once

#include "net.h"

#include <cstddef>
#include <cstdint>

class InternetLinkAdapter final : public NetAdapter
{
public:
	InternetLinkAdapter();
	~InternetLinkAdapter() override = default;

	bool blocks() override;
	bool isInitialised() override;
	bool recv(NetPacket* pkt) override;
	bool send(NetPacket* pkt) override;
	void reloadSettings() override;
	void close() override;

private:
	bool m_initialized = false;
};

namespace InternetLinkBridge
{
void Reset();
void SetTransportReady(bool ready);
bool PushInbound(const std::uint8_t* data, std::size_t size);
bool PopOutbound(std::uint8_t* data, std::size_t capacity, std::size_t* size);
}
