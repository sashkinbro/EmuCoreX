// SPDX-FileCopyrightText: 2026 EmuCoreX contributors
// SPDX-License-Identifier: GPL-3.0+

#include "InternetLinkAdapter.h"

#include "DEV9.h"
#include "common/Console.h"

#include <algorithm>
#include <atomic>
#include <cstring>
#include <deque>
#include <mutex>
#include <vector>

namespace
{
constexpr std::size_t MAX_ETHERNET_FRAME = 1514;
constexpr std::size_t MAX_QUEUED_FRAMES = 256;
using Frame = std::vector<std::uint8_t>;

std::mutex s_queue_mutex;
std::deque<Frame> s_inbound;
std::deque<Frame> s_outbound;
std::atomic<bool> s_transport_ready{false};

bool PushFrame(std::deque<Frame>& queue, const std::uint8_t* data, std::size_t size)
{
	if (data == nullptr || size == 0 || size > MAX_ETHERNET_FRAME)
		return false;
	if (queue.size() >= MAX_QUEUED_FRAMES)
		queue.pop_front();
	queue.emplace_back(data, data + size);
	return true;
}
} // namespace

InternetLinkAdapter::InternetLinkAdapter()
	: NetAdapter()
{
	if (!EmuConfig.DEV9.EthEnable)
		return;

	const u32 peer_id = EmuConfig.DEV9.LocalLinkHost ? 1u :
		std::clamp<u32>(EmuConfig.DEV9.LocalLinkPeerId, 2, 65533);
	PacketReader::MAC_Address mac = defaultMAC;
	mac.bytes[4] = static_cast<u8>((peer_id >> 8) & 0xff);
	mac.bytes[5] = static_cast<u8>(peer_id & 0xff);
	SetMACAddress(&mac);

	u32 host_part = peer_id;
	if (host_part >= 513)
		++host_part;
	const PacketReader::IP::IP_Address ps2_ip{{{192, 0,
		static_cast<u8>((host_part >> 8) & 0xff), static_cast<u8>(host_part & 0xff)}}};
	const PacketReader::IP::IP_Address subnet{{{255, 255, 0, 0}}};
	InitInternalServer(nullptr, true, ps2_ip, subnet, internalIP);
	m_initialized = true;
	Console.WriteLn("DEV9: Internet Link bridge ready as peer %u", peer_id);
}

bool InternetLinkAdapter::blocks() { return false; }
bool InternetLinkAdapter::isInitialised() { return m_initialized; }

bool InternetLinkAdapter::recv(NetPacket* pkt)
{
	if (!m_initialized || pkt == nullptr)
		return false;
	std::lock_guard lock(s_queue_mutex);
	if (s_inbound.empty())
		return false;
	Frame frame = std::move(s_inbound.front());
	s_inbound.pop_front();
	pkt->size = static_cast<int>(frame.size());
	std::memcpy(pkt->buffer, frame.data(), frame.size());
	return true;
}

bool InternetLinkAdapter::send(NetPacket* pkt)
{
	if (!m_initialized || pkt == nullptr || !s_transport_ready.load(std::memory_order_acquire))
		return false;
	std::lock_guard lock(s_queue_mutex);
	return PushFrame(s_outbound, reinterpret_cast<const std::uint8_t*>(pkt->buffer), static_cast<std::size_t>(pkt->size));
}

void InternetLinkAdapter::reloadSettings() {}
void InternetLinkAdapter::close() { m_initialized = false; }

void InternetLinkBridge::Reset()
{
	std::lock_guard lock(s_queue_mutex);
	s_inbound.clear();
	s_outbound.clear();
	s_transport_ready.store(false, std::memory_order_release);
}

void InternetLinkBridge::SetTransportReady(bool ready)
{
	s_transport_ready.store(ready, std::memory_order_release);
}

bool InternetLinkBridge::PushInbound(const std::uint8_t* data, std::size_t size)
{
	std::lock_guard lock(s_queue_mutex);
	return PushFrame(s_inbound, data, size);
}

bool InternetLinkBridge::PopOutbound(std::uint8_t* data, std::size_t capacity, std::size_t* size)
{
	if (data == nullptr || size == nullptr)
		return false;
	std::lock_guard lock(s_queue_mutex);
	if (s_outbound.empty())
		return false;
	Frame frame = std::move(s_outbound.front());
	s_outbound.pop_front();
	if (frame.size() > capacity)
		return false;
	std::memcpy(data, frame.data(), frame.size());
	*size = frame.size();
	return true;
}
