/*
 *    This file is part of libnspire.
 *
 *    libnspire is free software: you can redistribute it and/or modify
 *    it under the terms of the GNU General Public License as published by
 *    the Free Software Foundation, either version 3 of the License, or
 *    (at your option) any later version.
 *
 *    libnspire is distributed in the hope that it will be useful,
 *    but WITHOUT ANY WARRANTY; without even the implied warranty of
 *    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *    GNU General Public License for more details.
 *
 *    You should have received a copy of the GNU General Public License
 *    along with libnspire.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#ifdef DEBUG
#include <iostream>
#endif

#include <arpa/inet.h>
#include <sys/time.h>

#include <libusb.h>

#include "cx2.h"
#include "error.h"
#include "packet.h"
#include "trace.h"

enum Address {
	AddrAll		= 0xFF,
	AddrMe		= 0xFE,
	AddrCalc	= 0x01
};

enum Service {
	AddrReqService  = 0x01,
	TimeService     = 0x02,
	EchoService     = 0x03,
	StreamService   = 0x04,
	TransmitService = 0x05,
	LoopbackService = 0x06,
	StatsService    = 0x07,
	UnknownService  = 0x08,
	AckFlag         = 0x80
};

// Big endian!
struct NNSEMessage {
	uint8_t 	misc;		// Unused?
	uint8_t		service;	// Service number. If bit 7 set, an ACK
	uint8_t     src;		// Address of the source
	uint8_t     dest;		// Address of the destination
	uint8_t     unknown;	// No idea
	uint8_t     reqAck;		// 0x1: Whether an ack is expected, 0x9: Not the first try
	uint16_t    length;		// Length of the packet, including this header
	uint16_t    seqno;		// Sequence number. Increases by one for every non-ACK packet.
	uint16_t    csum;		// Checksum. Inverse of the 16bit modular sum with carry added.

	uint8_t     data[0];
} __attribute__((packed));

struct NNSEMessage_AddrReq {
	NNSEMessage hdr;
	uint8_t     code; // 00
	uint8_t     clientID[64];
} __attribute__((packed));

struct NNSEMessage_AddrResp {
	NNSEMessage hdr;
	uint8_t     addr;
} __attribute__((packed));

struct NNSEMessage_UnkResp {
	NNSEMessage hdr;
	uint8_t     noidea[2]; // 80 03
} __attribute__((packed));

struct NNSEMessage_TimeReq {
	NNSEMessage hdr;
	uint8_t     code;
} __attribute__((packed));

struct NNSEMessage_TimeResp {
	NNSEMessage hdr;
	uint8_t     noidea; // 80
	uint32_t    sec;
	uint64_t    frac;
	uint32_t    frac2;
} __attribute__((packed));

#ifdef DEBUG
static void dumpPacket(const NNSEMessage *message)
{
	printf("Misc:   \t%02x\n", message->misc);
	printf("Service:\t%02x\n", message->service);
	printf("Dest:   \t%02x\n", message->dest);
	printf("Src:    \t%02x\n", message->src);
	printf("Unknown:\t%02x\n", message->unknown);
	printf("ReqAck: \t%02x\n", message->reqAck);
	printf("Length: \t%04x\n", ntohs(message->length));
	printf("SeqNo:  \t%04x\n", ntohs(message->seqno));
	printf("Csum:   \t%04x\n", ntohs(message->csum));
	
	auto datalen = ntohs(message->length) - sizeof(NNSEMessage);
	for(int i = 0; i < datalen; ++i)
		printf("%02x ", message->data[i]);
	
	printf("\n");
}
#endif

static uint16_t compute_checksum(const uint8_t *data, uint32_t size)
{
	uint32_t acc = 0;

	if (size > 0)
	{
		for (uint32_t i = 0; i < size - 1; i += 2)
		{
			uint16_t cur = (((uint16_t)data[i]) << 8) | data[i + 1];
			acc += cur;
		}

		if (size & 1)
			acc += (((uint16_t)data[size - 1]) << 8);
	}

	while (acc >> 16)
		acc = (acc >> 16) + uint16_t(acc);

	return acc;
}

using PacketDeadline = std::chrono::steady_clock::time_point;

static uint64_t transferClock()
{
	return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
		std::chrono::steady_clock::now().time_since_epoch()).count());
}

int packet_prepare_cx2(struct nspire_handle *handle)
{
	if(!handle->device.dev)
		return -NSPIRE_ERR_DISCONNECTED;
	if(handle->device.last_transfer_ms
		&& transferClock() - handle->device.last_transfer_ms >= 1000) {
		nspire_trace("transport phase=prepare reason=idle-reconnect");
		return nspire_reconnect(handle);
	}
	return NSPIRE_ERR_SUCCESS;
}

static unsigned int remainingTimeout(PacketDeadline deadline, unsigned int limit)
{
	auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
	return remaining > 0 ? static_cast<unsigned int>(std::min(remaining, static_cast<decltype(remaining)>(limit))) : 0;
}

static int tracedTransfer(usb_device_t *handle, unsigned char endpoint, unsigned char *bytes,
		int length, int *transferred, unsigned int timeout, PacketDeadline deadline, const char *phase)
{
	auto started = std::chrono::steady_clock::now();
	int status = libusb_bulk_transfer(handle->dev, endpoint, bytes, length, transferred, timeout);
	handle->last_transfer_ms = transferClock();
	auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
	nspire_trace("usb phase=%s direction=%s usb_status=%d requested=%d transferred=%d timeout_ms=%u elapsed_ms=%lld remaining_ms=%u",
		phase, endpoint & LIBUSB_ENDPOINT_IN ? "in" : "out", status, length, *transferred, timeout,
		static_cast<long long>(elapsed), remainingTimeout(deadline, UINT32_MAX));
	return status;
}

static int readPacket(usb_device_t *handle, NNSEMessage *message, int maxlen, PacketDeadline deadline)
{
	if(maxlen < sizeof(NNSEMessage))
		return -NSPIRE_ERR_INVALID;

	memset(message, 0, sizeof(NNSEMessage));
	int received = 0;
	int completeLength = 0;
	int transferLength = maxlen;
	while(received < transferLength)
	{
		const char *phase = received ? "continuation" : "read";
		auto timeout = remainingTimeout(deadline, received ? 1000 : 5000);
		if(!timeout) {
			nspire_trace("usb phase=%s direction=in reason=deadline remaining_ms=0", phase);
			return -NSPIRE_ERR_TIMEOUT;
		}
		int transferred = 0;
		int capacity = transferLength - received;
		int r = tracedTransfer(handle, handle->ep_in,
			reinterpret_cast<unsigned char*>(message) + received, capacity,
			&transferred, timeout, deadline, phase);
		if(r < 0 && r != LIBUSB_ERROR_TIMEOUT)
			return usb_error(r);
		if(transferred < 0 || transferred > capacity)
			return -NSPIRE_ERR_INVALPKT;
		if(!transferred)
			return -NSPIRE_ERR_TIMEOUT;
		received += transferred;
		if(!completeLength && received >= sizeof(NNSEMessage)) {
			completeLength = ntohs(message->length);
			// Aligned frames carry one padding byte to terminate the USB transfer.
			transferLength = completeLength + (completeLength % 64 == 0);
			if(completeLength < sizeof(NNSEMessage) || transferLength > maxlen)
				return -NSPIRE_ERR_INVALPKT;
		}
		if(received > transferLength)
			return -NSPIRE_ERR_INVALPKT;
	}

#ifdef DEBUG
	printf("Got packet:\n");
	dumpPacket(message);
#endif

	// The sum covers the whole packet, not whatever the last bulk transfer happened to carry.
	if(compute_checksum(reinterpret_cast<uint8_t*>(message), completeLength) != 0xFFFF)
		return -NSPIRE_ERR_INVALPKT;

	nspire_trace("transport phase=frame service=0x%02x source=0x%02x destination=0x%02x seq=%u length=%u req_ack=0x%02x",
		static_cast<unsigned>(message->service), static_cast<unsigned>(message->src),
		static_cast<unsigned>(message->dest), static_cast<unsigned>(ntohs(message->seqno)),
		static_cast<unsigned>(completeLength), static_cast<unsigned>(message->reqAck));
	if(nspire_trace_enabled() && message->service == StreamService
		&& completeLength >= sizeof(NNSEMessage) + 16
		&& message->data[0] == 0x54 && message->data[1] == 0xfd) {
		const uint8_t *header = message->data;
		auto word = [header](size_t offset) {
			return (static_cast<unsigned>(header[offset]) << 8) | header[offset + 1];
		};
		nspire_trace("transport phase=navnet-header magic=0x%04x source=0x%04x source_service=0x%04x destination=0x%04x destination_service=0x%04x data_size=%u ack=0x%02x seq=%u",
			word(0), word(2), word(4), word(6), word(8), static_cast<unsigned>(header[12]),
			static_cast<unsigned>(header[13]), static_cast<unsigned>(header[14]));
	}
	return NSPIRE_ERR_SUCCESS;
}

static int writePacket(usb_device_t *handle, NNSEMessage *message, PacketDeadline deadline)
{
	auto length = ntohs(message->length);

	message->csum = 0;
	message->csum = htons(compute_checksum(reinterpret_cast<uint8_t*>(message), length) ^ 0xFFFF);

	if(compute_checksum(reinterpret_cast<uint8_t*>(message), length) != 0xFFFF)
		return -NSPIRE_ERR_INVALPKT;

#ifdef DEBUG
	printf("Sending packet:\n");
	dumpPacket(message);
#endif

	int transferred = 0;
	auto timeout = remainingTimeout(deadline, 1000);
	if(!timeout) {
		nspire_trace("usb phase=write direction=out reason=deadline remaining_ms=0");
		return -NSPIRE_ERR_TIMEOUT;
	}
	unsigned char padded[sizeof(NNSEMessage) + 1472 + 1];
	auto *bytes = reinterpret_cast<unsigned char*>(message);
	int transferLength = length;
	if(length % 64 == 0) {
		memcpy(padded, message, length);
		padded[length] = 0;
		bytes = padded;
		++transferLength;
	}
	int r = tracedTransfer(handle, handle->ep_out, bytes, transferLength, &transferred, timeout, deadline, "write");
	if(r < 0)
		return usb_error(r);
	if(transferLength != transferred)
		return -NSPIRE_ERR_LIBUSB;

	return NSPIRE_ERR_SUCCESS;
}

static uint16_t nextSeqno()
{
	static uint16_t seqno = 0;
	return seqno++;
}

template <typename T> int sendMessage(usb_device_t *handle, T &message, PacketDeadline deadline)
{
	message.hdr.src = AddrMe;
	message.hdr.dest = AddrCalc;
	message.hdr.length = htons(sizeof(T));
	message.hdr.seqno = htons(nextSeqno());

	return writePacket(handle, &message.hdr, deadline);
}

template <typename T> T* messageCast(NNSEMessage *message)
{
	if(ntohs(message->length) < sizeof(T))
		return nullptr;

	return reinterpret_cast<T*>(message);
}

static int handlePacket(struct nspire_handle *nsp_handle, NNSEMessage *message, PacketDeadline deadline)
{
	auto *handle = &nsp_handle->device;

	if(message->dest != AddrMe && message->dest != AddrAll)
	{
#ifdef DEBUG
		printf("Not for me?\n");
#endif
		return NSPIRE_ERR_SUCCESS;
	}

	if(message->service & AckFlag)
	{
#ifdef DEBUG
		printf("Got ack for %02x\n", ntohs(message->seqno));
#endif
		return NSPIRE_ERR_SUCCESS;
	}

	auto &accepted = nsp_handle->cx2_accepted_stream[message->src][message->dest == AddrAll];
	bool duplicate = message->service == StreamService && (message->reqAck & 8)
		&& accepted.valid && accepted.sequence == ntohs(message->seqno);
	if(message->service == StreamService && !duplicate
		&& nsp_handle->cx2_pending_count == NSPIRE_CX2_PENDING_CAPACITY)
		return -NSPIRE_ERR_NOMEM;

	if(message->reqAck & 1)
	{
		NNSEMessage ack = {
			.misc = message->misc,
			.service = static_cast<uint8_t>(message->service | AckFlag),
			.src = message->dest,
			.dest = message->src,
			.unknown = message->unknown,
			.reqAck = static_cast<uint8_t>(message->reqAck & ~1),
			.length = htons(sizeof(NNSEMessage)),
			.seqno = message->seqno,
		};

		int ret = writePacket(handle, &ack, deadline);
		if(ret)
			return ret;
	}

	if(duplicate) {
		nspire_trace("transport phase=receive reason=accepted-retry source=0x%02x destination=0x%02x seq=%u",
			static_cast<unsigned>(message->src), static_cast<unsigned>(message->dest),
			static_cast<unsigned>(ntohs(message->seqno)));
		return NSPIRE_ERR_SUCCESS;
	}

	switch(message->service & ~AckFlag)
	{
		case AddrReqService:
		{
			const NNSEMessage_AddrReq *req = messageCast<NNSEMessage_AddrReq>(message);
			if(!req || req->code != 0)
				goto drop;

#ifdef DEBUG
			printf("Got request from client %s (product id %c%c)\n", &req->clientID[12], req->clientID[10], req->clientID[11]);
#endif
/*			Sending this somehow introduces issues like the time request not
			arriving or the calc responding with yet another address request.
			// Address release request. Not sure how that works.
			NNSEMessage_AddrResp resp{};
			resp.hdr.service = message->service;
			resp.addr = AddrCalc;

			if(!sendMessage(resp))
				printf("Failed to send message\n");
*/

			NNSEMessage_AddrResp resp2{};
			resp2.hdr.service = message->service;
			resp2.addr = 0x80; // No idea

			// In some cases on HW and in Firebird always after reconnecting
			// it ignores the first packet for some reason. So just send it
			// twice (the seqno doesn't really matter at this point), if it
			// receives both it'll ignore the second one.
			int ret = sendMessage(handle, resp2, deadline);
			return ret ? ret : sendMessage(handle, resp2, deadline);
		}
		case TimeService:
		{
			const NNSEMessage_TimeReq *req = messageCast<NNSEMessage_TimeReq>(message);
			if(!req || req->code != 0)
				goto drop;

#ifdef DEBUG
			printf("Got time request\n");
#endif

			struct timeval val;
			gettimeofday(&val, nullptr);
			
			NNSEMessage_TimeResp resp = {
				.hdr = {
					.service = message->service,
				},
				.noidea = 0x80,
				.sec = htonl(static_cast<uint32_t>(val.tv_sec)),
				.frac = 0,
			};

			int ret = sendMessage(handle, resp, deadline);
			if(ret)
				return ret;

			nsp_handle->cx2_handshake_complete = true;
			break;
		}
		case UnknownService:
		{
			if(ntohs(message->length) != sizeof(NNSEMessage) + 1 || message->data[0] != 0x01)
				goto drop;
		
#ifdef DEBUG
			printf("Got packet for unknown service\n");
#endif
			
			NNSEMessage_UnkResp resp = {
				.hdr = {
					.service = message->service,
				},
				.noidea = {0x81, 0x03},
			};

			return sendMessage(handle, resp, deadline);
		}
		case StreamService:
		{
			auto payload_size = ntohs(message->length) - sizeof(NNSEMessage);
			if(payload_size > sizeof(packet))
				return -NSPIRE_ERR_INVALPKT;
			auto index = (nsp_handle->cx2_pending_head + nsp_handle->cx2_pending_count) % NSPIRE_CX2_PENDING_CAPACITY;
			auto &pending = nsp_handle->cx2_pending[index];
			pending.size = payload_size;
			memcpy(pending.bytes, message->data, pending.size);
			nsp_handle->cx2_pending_count++;
			accepted.sequence = ntohs(message->seqno);
			accepted.valid = true;
			break;
		}
		default:
			printf("Unhandled service %02x\n", message->service & ~AckFlag);
	}

	return NSPIRE_ERR_SUCCESS;

	drop:
	return -NSPIRE_ERR_INVALPKT;
}

static int assureReady(struct nspire_handle *nsp_handle,
	PacketDeadline deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10))
{
	if(nsp_handle->cx2_handshake_complete)
		return NSPIRE_ERR_SUCCESS;

	nsp_handle->cx2_pending_head = 0;
	nsp_handle->cx2_pending_count = 0;
	memset(nsp_handle->cx2_accepted_stream, 0, sizeof nsp_handle->cx2_accepted_stream);
	auto *handle = &nsp_handle->device;

	const int maxlen = sizeof(NNSEMessage) + NSPIRE_CX2_MAX_PAYLOAD;
	NNSEMessage * const message = reinterpret_cast<NNSEMessage*>(malloc(maxlen));
	if(!message)
		return -NSPIRE_ERR_NOMEM;
	int ret = NSPIRE_ERR_SUCCESS;
	unsigned int frames = 0;
	while(!nsp_handle->cx2_handshake_complete)
	{
		ret = readPacket(handle, message, maxlen, deadline);
		if(ret)
			break;
		ret = handlePacket(nsp_handle, message, deadline);
		nspire_trace("transport phase=handshake event=frame index=%u status=%d complete=%d pending=%u",
			++frames, ret, nsp_handle->cx2_handshake_complete, nsp_handle->cx2_pending_count);
		if(ret)
			break;
	}
	free(message);

	int status = ret ? ret : nsp_handle->cx2_handshake_complete ? NSPIRE_ERR_SUCCESS : -NSPIRE_ERR_TIMEOUT;
	nspire_trace("transport phase=handshake status=%d reason=%s remaining_ms=%u", status,
		nsp_handle->cx2_handshake_complete ? "complete" : ret == -NSPIRE_ERR_TIMEOUT
			? remainingTimeout(deadline, UINT32_MAX) ? "io-timeout" : "deadline" : "packet",
		remainingTimeout(deadline, UINT32_MAX));
	return status;
}

int packet_send_cx2(struct nspire_handle *nsp_handle, char *data, int size)
{
	if(size < 0 || size > NSPIRE_CX2_MAX_PAYLOAD)
		return -NSPIRE_ERR_INVALID;
	int ret = assureReady(nsp_handle);
	if(ret)
		return ret;

	auto *handle = &nsp_handle->device;

	int len = sizeof(NNSEMessage) + size;
	NNSEMessage *msg = reinterpret_cast<NNSEMessage*>(malloc(len));
	if(!msg)
		return -NSPIRE_ERR_NOMEM;
	*msg = {
		.service = StreamService,
		.src = AddrMe,
		.dest = AddrCalc,
		.reqAck = 1,
		.length = htons(len),
		.seqno = htons(nextSeqno()),
	};
	memcpy(msg->data, data, size);

	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	nspire_trace("transport phase=send expected_seq=%u requested=%d", ntohs(msg->seqno), size);
	ret = writePacket(handle, msg, deadline);
	if(!ret)
	{
		const int maxlen = sizeof(NNSEMessage) + NSPIRE_CX2_MAX_PAYLOAD;
		NNSEMessage * const message = reinterpret_cast<NNSEMessage*>(malloc(maxlen));
		if(!message) {
			free(msg);
			return -NSPIRE_ERR_NOMEM;
		}

		bool acked = false;
		for(int i = 10; i-- && !ret && !acked;)
		{
			ret = readPacket(handle, message, maxlen, deadline);
			if(ret) {
				nspire_trace("transport phase=ack reason=read expected_seq=%u status=%d remaining_ms=%u", ntohs(msg->seqno), ret, remainingTimeout(deadline, UINT32_MAX));
				break;
			}
			
			ret = handlePacket(nsp_handle, message, deadline);
			if(ret) {
				nspire_trace("transport phase=ack reason=handle expected_seq=%u received_seq=%u status=%d remaining_ms=%u", ntohs(msg->seqno), ntohs(message->seqno), ret, remainingTimeout(deadline, UINT32_MAX));
				break;
			}
			
			if(message->dest == AddrMe
				&& message->service == (StreamService | AckFlag)
				&& message->seqno == msg->seqno)
				acked = true;
			nspire_trace("transport phase=ack expected_seq=%u received_seq=%u service=%u destination=%u matched=%d",
				ntohs(msg->seqno), ntohs(message->seqno), message->service, message->dest, acked);
		}

		if(!ret && !acked) {
			nspire_trace("transport phase=ack reason=packet-limit expected_seq=%u remaining_ms=%u", ntohs(msg->seqno), remainingTimeout(deadline, UINT32_MAX));
			ret = -NSPIRE_ERR_TIMEOUT;
		}

		free(message);
	}

	free(msg);

	return ret;
}

int packet_recv_cx2(struct nspire_handle *nsp_handle, char *data, int size)
{
	if(size < 0)
		return -NSPIRE_ERR_INVALID;
	int ret = assureReady(nsp_handle);
	if(ret)
		return ret;

	if(!nsp_handle->cx2_pending_count) {
		const int maxlen = sizeof(NNSEMessage) + NSPIRE_CX2_MAX_PAYLOAD;
		NNSEMessage * const message = reinterpret_cast<NNSEMessage*>(malloc(maxlen));
		if(!message)
			return -NSPIRE_ERR_NOMEM;
		auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		for(int i = 10; i-- && !nsp_handle->cx2_pending_count;)
		{
			ret = readPacket(&nsp_handle->device, message, maxlen, deadline);
			if(ret)
				break;
			ret = handlePacket(nsp_handle, message, deadline);
			if(ret)
				break;
		}
		if(!ret && !nsp_handle->cx2_pending_count)
			nspire_trace("transport phase=receive reason=packet-limit remaining_ms=%u", remainingTimeout(deadline, UINT32_MAX));
		free(message);
		if(ret)
			return ret;
	}
	if(!nsp_handle->cx2_pending_count)
		return -NSPIRE_ERR_TIMEOUT;
	auto &pending = nsp_handle->cx2_pending[nsp_handle->cx2_pending_head];
	if(pending.size > size)
		return -NSPIRE_ERR_INVALPKT;
	if(pending.size)
		memcpy(data, pending.bytes, pending.size);
	nsp_handle->cx2_pending_head = (nsp_handle->cx2_pending_head + 1) % NSPIRE_CX2_PENDING_CAPACITY;
	nsp_handle->cx2_pending_count--;
	return size - pending.size;
}
