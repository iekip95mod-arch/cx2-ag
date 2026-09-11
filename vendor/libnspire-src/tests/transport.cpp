#include <climits>
#include <cstdlib>
#include <deque>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>
#include <stdlib.h> // Load allocator declarations before fault injection.
#include <unistd.h>

extern "C" {
#include "../src/api/nspire.h"
#include "../src/packet.h"
#include "../src/service.h"
}

static int fail_allocation = -1;

static void *transport_malloc(size_t size) {
	if(fail_allocation == 0)
		return nullptr;
	if(fail_allocation > 0)
		fail_allocation--;
	return std::malloc(size);
}

#define malloc transport_malloc
#include "../src/cx2.cpp"
#undef malloc

struct libusb_context {};
struct libusb_device { uint16_t pid; uint8_t port; bool attached = true; int references = 0; };
struct libusb_device_handle { libusb_device *device; };

struct Transfer {
	int status = 0;
	std::vector<unsigned char> bytes;
	int written = -1;
	unsigned int delay_us = 0;
};

static struct {
	std::deque<Transfer> reads;
	std::deque<Transfer> writes;
	std::vector<unsigned char> last_write;
	std::vector<uint8_t> write_services;
	unsigned char expected_ep_in = 0x81;
	unsigned char expected_ep_out = 0x01;
	std::deque<int> resets;
	std::deque<int> claims;
	int open_status = 0;
	int detach_status = 0;
	int config_status = 0;
	int set_status = 0;
	int descriptor_status = 0;
	int list_status = 0;
	int kernel_status = 0;
	int get_alternate_status = 1;
	int set_alternate_status = 0;
	uint8_t current_alternate = 0;
	int current_config = 1;
	int absent_lists = 0;
	int absent_after_reset = 0;
	int open_calls = 0;
	int ref_calls = 0;
	int unref_calls = 0;
	std::vector<libusb_device *> opened_devices;
	int reset_calls = 0;
	int claim_calls = 0;
	int close_calls = 0;
	int release_calls = 0;
	int delay_calls = 0;
	int bulk_calls = 0;
	int set_calls = 0;
	int detach_calls = 0;
	int get_alternate_calls = 0;
	int set_alternate_calls = 0;
	int free_descriptor_calls = 0;
	int init_calls = 0;
	int exit_calls = 0;
	bool visible = true;
	bool vanish_on_reset = false;
	bool move_on_reset = false;
	bool no_ports = false;
	bool auto_ack = false;
	bool no_altsetting = false;
	bool no_input = false;
} usb;

static libusb_device calculator{NSP_PID_CX2, 2};
static libusb_device_handle connection{&calculator};
static libusb_device *devices[] = {&calculator, nullptr, nullptr};

#if defined(__APPLE__) && !defined(NSPIRE_TEST_NONAPPLE_USB)
static constexpr bool cx2_interface_only = true;
#else
static constexpr bool cx2_interface_only = false;
#endif

#define CHECK(expression) do { if (!(expression)) throw std::runtime_error(std::string(__func__) + ": " + #expression); } while (false)

static int pop(std::deque<int> &statuses) {
	if(statuses.empty())
		return 0;
	int status = statuses.front();
	statuses.pop_front();
	return status;
}

static std::vector<unsigned char> frame(uint8_t service, std::vector<unsigned char> bytes = {}, uint8_t reqAck = 0) {
	std::vector<unsigned char> packet(sizeof(NNSEMessage) + bytes.size());
	auto *message = reinterpret_cast<NNSEMessage*>(packet.data());
	message->service = service;
	message->src = AddrCalc;
	message->dest = AddrMe;
	message->reqAck = reqAck;
	message->length = htons(static_cast<uint16_t>(packet.size()));
	std::copy(bytes.begin(), bytes.end(), packet.begin() + sizeof(NNSEMessage));
	message->csum = htons(compute_checksum(packet.data(), static_cast<uint32_t>(packet.size())) ^ 0xFFFF);
	return packet;
}

int LIBUSB_CALL libusb_init(libusb_context **ctx) {
	static libusb_context context;
	*ctx = &context;
	usb.init_calls++;
	return 0;
}

void LIBUSB_CALL libusb_exit(libusb_context *) { usb.exit_calls++; }

ssize_t LIBUSB_CALL libusb_get_device_list(libusb_context *, libusb_device ***list) {
	*list = devices;
	if(usb.list_status)
		return usb.list_status;
	if(usb.absent_lists > 0) {
		usb.absent_lists--;
		return 0;
	}
	ssize_t count = 0;
	while(usb.visible && devices[count])
		++count;
	return count;
}

void LIBUSB_CALL libusb_free_device_list(libusb_device **, int) {}

int LIBUSB_CALL libusb_get_device_descriptor(libusb_device *device, libusb_device_descriptor *descriptor) {
	*descriptor = {};
	descriptor->idVendor = NSP_VID;
	descriptor->idProduct = device->pid;
	return 0;
}

int LIBUSB_CALL libusb_open(libusb_device *device, libusb_device_handle **handle) {
	usb.open_calls++;
	usb.opened_devices.push_back(device);
	if(!device->attached)
		return LIBUSB_ERROR_NO_DEVICE;
	if(usb.open_status)
		return usb.open_status;
	connection.device = device;
	*handle = &connection;
	return 0;
}

void LIBUSB_CALL libusb_close(libusb_device_handle *) { usb.close_calls++; }
libusb_device *LIBUSB_CALL libusb_get_device(libusb_device_handle *handle) { return handle->device; }
libusb_device *LIBUSB_CALL libusb_ref_device(libusb_device *device) {
	++usb.ref_calls;
	++device->references;
	return device;
}
void LIBUSB_CALL libusb_unref_device(libusb_device *device) {
	++usb.unref_calls;
	CHECK(device->references > 0);
	--device->references;
}
uint8_t LIBUSB_CALL libusb_get_bus_number(libusb_device *) { return 1; }

int LIBUSB_CALL libusb_get_port_numbers(libusb_device *device, uint8_t *ports, int) {
	ports[0] = device->port;
	return usb.no_ports ? LIBUSB_ERROR_NOT_SUPPORTED : 1;
}

int LIBUSB_CALL libusb_kernel_driver_active(libusb_device_handle *, int) { return usb.kernel_status; }
int LIBUSB_CALL libusb_detach_kernel_driver(libusb_device_handle *, int) {
	usb.detach_calls++;
	return usb.detach_status;
}

int LIBUSB_CALL libusb_get_configuration(libusb_device_handle *, int *current) {
	*current = usb.current_config;
	return usb.config_status;
}

int LIBUSB_CALL libusb_set_configuration(libusb_device_handle *, int) {
	usb.set_calls++;
	return usb.set_status;
}

int LIBUSB_CALL libusb_reset_device(libusb_device_handle *) {
	usb.reset_calls++;
	usb.absent_lists = usb.absent_after_reset;
	if(usb.vanish_on_reset)
		usb.visible = false;
	if(usb.move_on_reset)
		calculator.port++;
	return pop(usb.resets);
}

int LIBUSB_CALL libusb_claim_interface(libusb_device_handle *, int) {
	usb.claim_calls++;
	return pop(usb.claims);
}

int LIBUSB_CALL libusb_release_interface(libusb_device_handle *, int) {
	usb.release_calls++;
	return 0;
}

int LIBUSB_CALL libusb_control_transfer(libusb_device_handle *, uint8_t request_type, uint8_t request,
		uint16_t value, uint16_t index, unsigned char *bytes, uint16_t length, unsigned int timeout) {
	CHECK(usb.claim_calls == usb.release_calls + 1);
	CHECK(request_type == 0x81);
	CHECK(request == LIBUSB_REQUEST_GET_INTERFACE && value == 0 && index == 0);
	CHECK(length == 1 && timeout == 1000);
	usb.get_alternate_calls++;
	*bytes = usb.current_alternate;
	return usb.get_alternate_status;
}

int LIBUSB_CALL libusb_set_interface_alt_setting(libusb_device_handle *, int interface, int alternate) {
	CHECK(usb.claim_calls == usb.release_calls + 1);
	CHECK(usb.get_alternate_calls == usb.set_alternate_calls + 1);
	CHECK(interface == 0 && alternate == usb.current_alternate);
	usb.set_alternate_calls++;
	return usb.set_alternate_status;
}

int LIBUSB_CALL libusb_handle_events_timeout(libusb_context *, timeval *) {
	usb.delay_calls++;
	return 0;
}

int LIBUSB_CALL libusb_get_active_config_descriptor(libusb_device *, libusb_config_descriptor **config) {
	static libusb_endpoint_descriptor endpoints[2][2];
	static libusb_interface_descriptor alternates[2];
	static libusb_interface interface;
	static libusb_config_descriptor descriptor;
	for(int i = 0; i < 2; i++) {
		endpoints[i][0] = {};
		endpoints[i][0].bEndpointAddress = usb.no_input ? 0x02 : 0x81 + i;
		endpoints[i][0].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
		endpoints[i][1] = {};
		endpoints[i][1].bEndpointAddress = 0x01 + i;
		endpoints[i][1].bmAttributes = LIBUSB_TRANSFER_TYPE_BULK;
		alternates[i] = {};
		alternates[i].bAlternateSetting = i * 3;
		alternates[i].bNumEndpoints = 2;
		alternates[i].endpoint = endpoints[i];
	}
	interface = {};
	interface.num_altsetting = usb.no_altsetting ? 0 : 2;
	interface.altsetting = alternates;
	descriptor = {};
	descriptor.bNumInterfaces = 1;
	descriptor.interface = &interface;
	*config = &descriptor;
	return usb.descriptor_status;
}

void LIBUSB_CALL libusb_free_config_descriptor(libusb_config_descriptor *) { usb.free_descriptor_calls++; }

int LIBUSB_CALL libusb_bulk_transfer(libusb_device_handle *, unsigned char endpoint, unsigned char *bytes,
		int length, int *transferred, unsigned int timeout) {
	CHECK(timeout > 0);
	CHECK(length >= 0);
	usb.bulk_calls++;
	bool reading = endpoint & LIBUSB_ENDPOINT_IN;
	CHECK(endpoint == (reading ? usb.expected_ep_in : usb.expected_ep_out));
	if(!reading) {
		usb.last_write.assign(bytes, bytes + length);
		if(length >= sizeof(NNSEMessage))
			usb.write_services.push_back(reinterpret_cast<NNSEMessage*>(bytes)->service);
	}
	auto &transfers = reading ? usb.reads : usb.writes;
	if(transfers.empty()) {
		if(reading) {
			*transferred = 0;
			return LIBUSB_ERROR_TIMEOUT;
		}
		*transferred = length;
		if(usb.auto_ack && length >= sizeof(NNSEMessage)) {
			auto *request = reinterpret_cast<NNSEMessage*>(bytes);
			if(request->service == StreamService) {
				auto ack = frame(StreamService | AckFlag);
				auto *header = reinterpret_cast<NNSEMessage*>(ack.data());
				header->seqno = request->seqno;
				header->csum = 0;
				header->csum = htons(compute_checksum(ack.data(), static_cast<uint32_t>(ack.size())) ^ 0xFFFF);
				usb.reads.push_back({0, ack});
			}
		}
		return 0;
	}
	auto transfer = transfers.front();
	transfers.pop_front();
	if(transfer.delay_us)
		usleep(transfer.delay_us);
	if(reading) {
		CHECK(transfer.bytes.size() <= static_cast<size_t>(length));
		std::copy(transfer.bytes.begin(), transfer.bytes.end(), bytes);
		*transferred = static_cast<int>(transfer.bytes.size());
	} else
		*transferred = transfer.written < 0 ? length : transfer.written;
	return transfer.status;
}

static void reset() {
	usb = {};
	fail_allocation = -1;
	calculator = {NSP_PID_CX2, 2};
	connection.device = &calculator;
	devices[0] = &calculator;
	devices[1] = nullptr;
	devices[2] = nullptr;
}

static void reset_legacy() {
	reset();
	calculator.pid = NSP_PID;
}

static nspire_handle ready() {
	nspire_handle handle{};
	handle.device.dev = &connection;
	handle.device.ep_in = 0x81;
	handle.device.ep_out = 0x01;
	handle.cx2_handshake_complete = true;
	handle.is_cx2 = true;
	return handle;
}

static void discovery_errors() {
	usb_device_t handle{};
	usb.visible = false;
	CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == -NSPIRE_ERR_NODEVICE);
	CHECK(usb.open_calls == 0);
	usb.visible = true;
	usb.open_status = LIBUSB_ERROR_ACCESS;
	CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == -NSPIRE_ERR_ACCESS);
	usb.open_status = LIBUSB_ERROR_BUSY;
	CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == -NSPIRE_ERR_USB_BUSY);
	CHECK(usb.reset_calls == 0);
	usb.open_status = 0;
	usb.list_status = LIBUSB_ERROR_NO_MEM;
	CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == -NSPIRE_ERR_NOMEM);
}

static void setup_errors() {
	reset_legacy();
	usb_device_t handle{};
	usb.kernel_status = 1;
	usb.detach_status = LIBUSB_ERROR_ACCESS;
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == -NSPIRE_ERR_ACCESS);
	CHECK(usb.reset_calls == 0 && usb.close_calls == 1);
	CHECK(usb.detach_calls == 1);
	reset_legacy();
	usb.config_status = LIBUSB_ERROR_NO_DEVICE;
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == -NSPIRE_ERR_DISCONNECTED);
	CHECK(usb.set_calls == 0);
	reset_legacy();
	usb.current_config = 0;
	usb.set_status = LIBUSB_ERROR_ACCESS;
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == -NSPIRE_ERR_ACCESS);
	CHECK(usb.set_calls == 1);
	reset_legacy();
	usb.resets = {LIBUSB_ERROR_ACCESS};
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == -NSPIRE_ERR_ACCESS);
	CHECK(usb.claim_calls == 0);
	reset_legacy();
	usb.claims = {LIBUSB_ERROR_BUSY};
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == -NSPIRE_ERR_USB_BUSY);
	CHECK(usb.release_calls == 0 && usb.close_calls == 1);
}

static void reset_recovery() {
	for(int status : {LIBUSB_ERROR_NOT_FOUND, LIBUSB_ERROR_NO_DEVICE}) {
		reset_legacy();
		usb_device_t handle{};
		usb.resets = {status};
		CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == 0);
		CHECK(usb.open_calls == 2 && usb.reset_calls == 1 && usb.claim_calls == 1);
		CHECK(handle.ep_in == 0x81 && handle.ep_out == 0x01);
		usb_free_device(&handle);
		CHECK(usb.close_calls == 2 && usb.release_calls == 1);
	}
	reset_legacy();
	usb_device_t handle{};
	usb.resets = {LIBUSB_ERROR_NOT_FOUND};
	usb.absent_after_reset = 3;
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == 0);
	CHECK(usb.delay_calls == 3 && usb.open_calls == 2 && usb.reset_calls == 1);
	usb_free_device(&handle);
}

static void reset_recovery_bounded() {
	reset_legacy();
	usb_device_t handle{};
	usb.resets = {LIBUSB_ERROR_NOT_FOUND};
	usb.vanish_on_reset = true;
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == -NSPIRE_ERR_DISCONNECTED);
	CHECK(usb.delay_calls == 20 && usb.open_calls == 1 && usb.reset_calls == 1);
	reset_legacy();
	usb.resets = {LIBUSB_ERROR_NOT_FOUND};
	usb.move_on_reset = true;
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == -NSPIRE_ERR_DISCONNECTED);
	CHECK(usb.open_calls == 1 && usb.delay_calls == 20);
	reset_legacy();
	usb.resets = {LIBUSB_ERROR_NOT_FOUND};
	usb.no_ports = true;
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == -NSPIRE_ERR_DISCONNECTED);
	CHECK(usb.delay_calls == 0);
}

static void interface_startup() {
	usb_device_t handle{};
	if(!cx2_interface_only) {
		usb.kernel_status = 1;
		usb.current_config = 0;
		CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == 0);
		CHECK(usb.detach_calls == 1 && usb.set_calls == 1 && usb.reset_calls == 1);
		CHECK(usb.get_alternate_calls == 0 && usb.set_alternate_calls == 0);
		usb_free_device(&handle);
		return;
	}
	for(int alternate : {0, 3}) {
		reset();
		usb.current_alternate = alternate;
		usb.resets = {LIBUSB_ERROR_NO_DEVICE};
		CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == 0);
		CHECK(usb.get_alternate_calls == 1 && usb.set_alternate_calls == 1);
		CHECK(usb.reset_calls == 0 && usb.detach_calls == 0 && usb.set_calls == 0);
		CHECK(usb.open_calls == 1 && usb.claim_calls == 1 && usb.delay_calls == 0);
		CHECK(handle.ep_in == (alternate ? 0x82 : 0x81));
		CHECK(handle.ep_out == (alternate ? 0x02 : 0x01));
		CHECK(usb.free_descriptor_calls == 1);
		usb_free_device(&handle);
		CHECK(usb.close_calls == 1 && usb.release_calls == 1);
	}
	reset_legacy();
	usb.kernel_status = 1;
	usb.current_config = 0;
	CHECK(usb_get_device(&handle, NSP_VID, calculator.pid) == 0);
	CHECK(usb.detach_calls == 1 && usb.set_calls == 1 && usb.reset_calls == 1);
	CHECK(usb.get_alternate_calls == 0 && usb.set_alternate_calls == 0);
	usb_free_device(&handle);
}

static void configuration_preflight() {
	usb_device_t handle{};
	for(int configuration : {0, 2}) {
		reset();
		usb.current_config = configuration;
		int status = usb_get_device(&handle, NSP_VID, NSP_PID_CX2);
		if(cx2_interface_only) {
			CHECK(status == -NSPIRE_ERR_USB_CONFIG);
			CHECK(std::string(nspire_strerror(status)) == "USB configuration unavailable");
			CHECK(status != -NSPIRE_ERR_INVALID);
			CHECK(handle.dev == nullptr);
			CHECK(usb.open_calls == 1 && usb.close_calls == 1);
			CHECK(usb.claim_calls == 0 && usb.release_calls == 0);
			CHECK(usb.detach_calls == 0 && usb.set_calls == 0 && usb.reset_calls == 0);
			CHECK(usb.get_alternate_calls == 0 && usb.set_alternate_calls == 0);
			CHECK(usb.bulk_calls == 0 && usb.delay_calls == 0);
		} else {
			CHECK(status == 0 && usb.set_calls == 1 && usb.reset_calls == 1);
			usb_free_device(&handle);
		}
	}
	reset();
	usb.config_status = LIBUSB_ERROR_TIMEOUT;
	CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == -NSPIRE_ERR_TIMEOUT);
	CHECK(handle.dev == nullptr && usb.close_calls == 1);
	CHECK(usb.claim_calls == 0 && usb.set_calls == 0 && usb.reset_calls == 0);
	CHECK(usb.bulk_calls == 0);
}

static void interface_startup_errors() {
	if(!cx2_interface_only)
		return;
	usb_device_t handle{};
	struct {
		int kernel, config, current, error;
	} preflight[] = {
		{1, 0, 1, NSPIRE_ERR_USB_BUSY},
		{LIBUSB_ERROR_ACCESS, 0, 1, NSPIRE_ERR_ACCESS},
		{LIBUSB_ERROR_NOT_SUPPORTED, 0, 1, NSPIRE_ERR_LIBUSB},
		{LIBUSB_ERROR_NO_DEVICE, 0, 1, NSPIRE_ERR_DISCONNECTED},
		{0, 0, 0, NSPIRE_ERR_USB_CONFIG},
		{0, LIBUSB_ERROR_ACCESS, 1, NSPIRE_ERR_ACCESS},
		{0, LIBUSB_ERROR_NO_DEVICE, 1, NSPIRE_ERR_DISCONNECTED}
	};
	auto check_failure = [&](int error, int claimed, int queried, int selected) {
		CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == -error);
		CHECK(handle.dev == nullptr);
		CHECK(usb.reset_calls == 0 && usb.detach_calls == 0 && usb.set_calls == 0);
		CHECK(usb.open_calls == 1 && usb.close_calls == 1 && usb.delay_calls == 0);
		CHECK(usb.release_calls == claimed);
		CHECK(usb.get_alternate_calls == queried && usb.set_alternate_calls == selected);
	};
	for(auto failure : preflight) {
		reset();
		usb.kernel_status = failure.kernel;
		usb.config_status = failure.config;
		usb.current_config = failure.current;
		check_failure(failure.error, 0, 0, 0);
		CHECK(usb.claim_calls == 0);
	}
	reset();
	usb.claims = {LIBUSB_ERROR_BUSY};
	check_failure(NSPIRE_ERR_USB_BUSY, 0, 0, 0);
	CHECK(usb.claim_calls == 1);
	struct {
		int status, error;
	} queries[] = {
		{0, NSPIRE_ERR_INVALPKT}, {2, NSPIRE_ERR_INVALPKT},
		{LIBUSB_ERROR_ACCESS, NSPIRE_ERR_ACCESS},
		{LIBUSB_ERROR_NO_DEVICE, NSPIRE_ERR_DISCONNECTED},
		{LIBUSB_ERROR_TIMEOUT, NSPIRE_ERR_TIMEOUT}
	};
	for(auto failure : queries) {
		reset();
		usb.get_alternate_status = failure.status;
		check_failure(failure.error, 1, 1, 0);
	}
	for(auto failure : queries) {
		if(failure.status >= 0)
			continue;
		reset();
		usb.set_alternate_status = failure.status;
		check_failure(failure.error, 1, 1, 1);
	}
	reset();
	usb.current_alternate = 7;
	check_failure(NSPIRE_ERR_LIBUSB, 1, 1, 1);
	CHECK(usb.free_descriptor_calls == 1);
	reset();
	usb.descriptor_status = LIBUSB_ERROR_NO_DEVICE;
	check_failure(NSPIRE_ERR_DISCONNECTED, 1, 1, 1);
	CHECK(usb.free_descriptor_calls == 0);
}

static void descriptor_cleanup() {
	usb_device_t handle{};
	usb.no_altsetting = true;
	CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == -NSPIRE_ERR_LIBUSB);
	CHECK(usb.release_calls == 1 && usb.close_calls == 1);
	reset();
	usb.no_input = true;
	CHECK(usb_get_device(&handle, NSP_VID, NSP_PID_CX2) == -NSPIRE_ERR_LIBUSB);
	CHECK(usb.release_calls == 1 && usb.close_calls == 1);
}

static void receive_error_classes() {
	auto handle = ready();
	char bytes[100];
	for(auto failure : {std::pair{LIBUSB_ERROR_TIMEOUT, NSPIRE_ERR_TIMEOUT},
		std::pair{LIBUSB_ERROR_NO_DEVICE, NSPIRE_ERR_DISCONNECTED},
		std::pair{LIBUSB_ERROR_ACCESS, NSPIRE_ERR_ACCESS},
		std::pair{LIBUSB_ERROR_BUSY, NSPIRE_ERR_USB_BUSY}}) {
		reset();
		usb.reads.push_back({failure.first, {}});
		CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == -failure.second);
		CHECK(usb.bulk_calls == 1);
	}
	reset();
	auto corrupt = frame(StreamService, {1, 2, 3});
	corrupt.back() ^= 1;
	usb.reads.push_back({0, corrupt});
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == -NSPIRE_ERR_INVALPKT);
	CHECK(usb.bulk_calls == 1);
}

static void fragmented_receive() {
	auto handle = ready();
	char bytes[20]{};
	auto packet = frame(StreamService, {1, 2, 3, 4});
	usb.reads.push_back({0, {packet.begin(), packet.begin() + 13}});
	usb.reads.push_back({0, {packet.begin() + 13, packet.end()}});
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == 16);
	CHECK(bytes[0] == 1 && bytes[3] == 4 && usb.bulk_calls == 2);
}

static void partial_timeout_receive() {
	unsigned failures = 0;
	auto expect = [&failures](bool passed, const char *label) {
		if(!passed) { std::cerr << "partial timeout: " << label << '\n'; ++failures; }
	};
	auto wire = frame(StreamService, std::vector<unsigned char>(100, 0x5a));
	for(auto cuts : {std::vector<size_t>{64, 112}, {5, 9, 12, 64, 112}, {12, 64, 100, 112}, {112}}) {
		reset();
		auto handle = ready();
		size_t offset = 0;
		for(size_t end : cuts) {
			usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, {wire.begin() + offset, wire.begin() + end}});
			offset = end;
		}
		unsigned char received[100]{};
		expect(packet_recv_cx2(&handle, reinterpret_cast<char*>(received), sizeof received) == 0,
			"initial/header/body/complete timeout bytes must form one packet");
		expect(std::all_of(std::begin(received), std::end(received), [](auto byte) { return byte == 0x5a; }),
			"complete payload must reach caller exactly");
		expect(usb.reads.empty() && usb.bulk_calls == static_cast<int>(cuts.size()),
			"assembly must consume each fragment once without replay");
	}
	for(int status : std::vector<int>{0, LIBUSB_ERROR_TIMEOUT}) {
		reset();
		auto handle = ready();
		usb.reads.push_back({status, {wire.begin(), wire.begin() + 5}});
		usb.reads.push_back({0, {wire.begin() + 5, wire.end()}});
		unsigned char received[100]{};
		expect(packet_recv_cx2(&handle, reinterpret_cast<char*>(received), sizeof received) == 0,
			"header fragmentation must not depend on USB status");
	}
	for(int status : std::vector<int>{0, LIBUSB_ERROR_TIMEOUT, LIBUSB_ERROR_NO_DEVICE, LIBUSB_ERROR_OVERFLOW}) {
		reset();
		auto handle = ready();
		usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, {wire.begin(), wire.begin() + 64}});
		usb.reads.push_back({status, {}});
		usb.reads.push_back({0, wire});
		unsigned char received[100]{};
		int expected = status == 0 || status == LIBUSB_ERROR_TIMEOUT ? -NSPIRE_ERR_TIMEOUT : usb_error(status);
		expect(packet_recv_cx2(&handle, reinterpret_cast<char*>(received), sizeof received) == expected,
			"zero progress and terminal USB errors must remain distinct");
		expect(usb.bulk_calls == 2 && usb.reads.size() == 1 && received[0] == 0,
			"terminal reads must stop without publishing incomplete bytes");
	}
	reset();
	auto handle = ready();
	unsigned char storage[200]{};
	auto *message = reinterpret_cast<NNSEMessage*>(storage);
	usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, {wire.begin(), wire.begin() + 64}, -1, 20000});
	usb.reads.push_back({0, {wire.begin() + 64, wire.end()}});
	expect(readPacket(&handle.device, message, sizeof storage,
		std::chrono::steady_clock::now() + std::chrono::milliseconds(5)) == -NSPIRE_ERR_TIMEOUT,
		"partial progress must not extend the absolute deadline");
	expect(usb.bulk_calls == 1 && usb.reads.size() == 1, "expired deadline must prevent continuation I/O");
	reset();
	usb.reads.push_back({0, wire});
	expect(readPacket(&handle.device, message, sizeof storage, std::chrono::steady_clock::now()) == -NSPIRE_ERR_TIMEOUT,
		"expired entry must fail");
	expect(usb.bulk_calls == 0, "expired entry must not read");
	for(int status : {LIBUSB_ERROR_NO_DEVICE, LIBUSB_ERROR_OVERFLOW}) {
		reset();
		usb.reads.push_back({status, wire});
		expect(readPacket(&handle.device, message, sizeof storage,
			std::chrono::steady_clock::now() + std::chrono::seconds(1)) == usb_error(status),
			"non-timeout errors must not publish even a complete frame");
	}
	reset();
	usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, wire, -1, 20000});
	expect(readPacket(&handle.device, message, sizeof storage,
		std::chrono::steady_clock::now() + std::chrono::milliseconds(5)) == 0,
		"complete timeout frame needs validation, not another read after deadline");
	expect(usb.bulk_calls == 1, "complete timeout frame must require one read");
	reset();
	for(unsigned char byte : wire)
		usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, {byte}});
	expect(readPacket(&handle.device, message, sizeof storage,
		std::chrono::steady_clock::now() + std::chrono::seconds(1)) == 0,
		"single-byte progress must assemble a validated frame");
	expect(usb.bulk_calls == static_cast<int>(wire.size()), "positive progress must remain bounded by frame bytes");
	reset();
	auto padded = frame(StreamService, std::vector<unsigned char>(52, 0x63));
	usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, padded});
	usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, {0}});
	expect(readPacket(&handle.device, message, sizeof storage,
		std::chrono::steady_clock::now() + std::chrono::seconds(1)) == 0,
		"aligned frame timeout must consume required USB padding");
	expect(usb.bulk_calls == 2 && ntohs(message->length) == 64, "padding must not change the frame length");
	for(bool bad_length : {false, true}) {
		reset();
		auto invalid = wire;
		if(bad_length) reinterpret_cast<NNSEMessage*>(invalid.data())->length = htons(11);
		else invalid.back() ^= 1;
		usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, {invalid.begin(), invalid.begin() + 5}});
		usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, {invalid.begin() + 5, invalid.end()}});
		expect(readPacket(&handle.device, message, sizeof storage,
			std::chrono::steady_clock::now() + std::chrono::seconds(1)) == -NSPIRE_ERR_INVALPKT,
			"timeout bytes must still pass frame length and checksum validation");
	}
	CHECK(failures == 0);
}

static void zero_progress_and_truncation() {
	auto handle = ready();
	char bytes[20]{};
	auto packet = frame(StreamService, {1, 2, 3, 4});
	usb.reads.push_back({0, {packet.begin(), packet.begin() + 13}});
	usb.reads.push_back({0, {}});
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == -NSPIRE_ERR_TIMEOUT);
	CHECK(usb.bulk_calls == 2);
	reset();
	usb.reads.push_back({0, {packet.begin(), packet.begin() + 13}});
	usb.reads.push_back({LIBUSB_ERROR_NO_DEVICE, {}});
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == -NSPIRE_ERR_DISCONNECTED);
	reset();
	usb.reads.push_back({0, {1, 2, 3}});
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == -NSPIRE_ERR_TIMEOUT);
	CHECK(usb.bulk_calls == 2);
	reset();
	usb.reads.push_back({0, packet});
	CHECK(packet_recv_cx2(&handle, bytes, 3) == -NSPIRE_ERR_INVALPKT);
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == sizeof(bytes) - 4);
	CHECK(bytes[0] == 1 && bytes[3] == 4);
	reset();
	packet.push_back(0);
	usb.reads.push_back({0, packet});
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == -NSPIRE_ERR_INVALPKT);
}

static void ack_and_handshake_failures() {
	auto handle = ready();
	char bytes[20]{};
	usb.reads.push_back({0, frame(StreamService, {1}, 1)});
	usb.writes.push_back({LIBUSB_ERROR_NO_DEVICE, {}});
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == -NSPIRE_ERR_DISCONNECTED);
	CHECK(bytes[0] == 0);
	reset();
	handle.cx2_handshake_complete = false;
	usb.reads.push_back({0, frame(TimeService, {0})});
	usb.writes.push_back({LIBUSB_ERROR_TIMEOUT, {}});
	CHECK(packet_recv_cx2(&handle, bytes, sizeof(bytes)) == -NSPIRE_ERR_TIMEOUT);
	CHECK(!handle.cx2_handshake_complete);
	reset();
	usb.reads.push_back({LIBUSB_ERROR_NO_DEVICE, {}});
	CHECK(packet_send_cx2(&handle, bytes, 1) == -NSPIRE_ERR_DISCONNECTED);
	CHECK(usb.bulk_calls == 1);
}

static void send_errors_and_deadlines() {
	auto handle = ready();
	char byte = 1;
	CHECK(packet_send_cx2(&handle, &byte, -1) == -NSPIRE_ERR_INVALID);
	CHECK(packet_send_cx2(&handle, &byte, 1473) == -NSPIRE_ERR_INVALID);
	CHECK(packet_recv_cx2(&handle, &byte, -1) == -NSPIRE_ERR_INVALID);
	CHECK(usb.bulk_calls == 0);
	usb.writes.push_back({LIBUSB_ERROR_NO_DEVICE, {}});
	CHECK(packet_send_cx2(&handle, &byte, 1) == -NSPIRE_ERR_DISCONNECTED);
	CHECK(usb.bulk_calls == 1);
	reset();
	usb.writes.push_back({0, {}, 1});
	CHECK(packet_send_cx2(&handle, &byte, 1) == -NSPIRE_ERR_LIBUSB);
	CHECK(usb.bulk_calls == 1);
	reset();
	CHECK(packet_send_cx2(&handle, &byte, 1) == -NSPIRE_ERR_TIMEOUT);
	CHECK(usb.bulk_calls == 2);
	reset();
	for(int i = 0; i < 10; i++)
		usb.reads.push_back({0, frame(StreamService | AckFlag)});
	CHECK(packet_recv_cx2(&handle, &byte, 1) == -NSPIRE_ERR_TIMEOUT);
	CHECK(usb.bulk_calls == 10);
	NNSEMessage message{};
	CHECK(readPacket(&handle.device, &message, sizeof(message), std::chrono::steady_clock::now()) == -NSPIRE_ERR_TIMEOUT);
	CHECK(usb.bulk_calls == 10);
}

static void allocation_failures() {
	auto handle = ready();
	char byte = 1;
	fail_allocation = 0;
	CHECK(packet_recv_cx2(&handle, &byte, 1) == -NSPIRE_ERR_NOMEM);
	CHECK(packet_send_cx2(&handle, &byte, 1) == -NSPIRE_ERR_NOMEM);
	CHECK(usb.bulk_calls == 0);
	fail_allocation = 1;
	CHECK(packet_send_cx2(&handle, &byte, 1) == -NSPIRE_ERR_NOMEM);
	CHECK(usb.bulk_calls == 1);
	handle.cx2_handshake_complete = false;
	fail_allocation = 0;
	CHECK(packet_recv_cx2(&handle, &byte, 1) == -NSPIRE_ERR_NOMEM);
	CHECK(!handle.cx2_handshake_complete && usb.bulk_calls == 1);
}

static void early_stream_reply() {
	for(bool early : {false, true}) {
		reset();
		auto handle = ready();
		usb.auto_ack = true;
		std::vector<unsigned char> reply{'k', 1, 2, 7};
		if(early)
			usb.reads.push_back({0, frame(StreamService, reply, 1)});
		char request[4]{};
		CHECK(packet_send_cx2(&handle, request, sizeof request) == 0);
		if(!early)
			usb.reads.push_back({0, frame(StreamService, reply, 1)});
		char received[4]{};
		CHECK(packet_recv_cx2(&handle, received, sizeof received) == 0);
		CHECK(memcmp(received, reply.data(), reply.size()) == 0);
		CHECK(usb.reads.empty());
		CHECK((usb.write_services == std::vector<uint8_t>{StreamService, StreamService | AckFlag}));
	}
}

static void early_stream_fifo() {
	auto handle = ready();
	usb.auto_ack = true;
	for(unsigned round = 0; round < 3; ++round) {
		std::vector<std::vector<unsigned char>> replies{{'k', 1, 2, 7},
			std::vector<unsigned char>(sizeof(packet), static_cast<unsigned char>(round)),
			std::vector<unsigned char>(52, 0x53), {}};
		for(const auto &reply : replies) {
			auto incoming = frame(StreamService, reply, 1);
			if(incoming.size() % 64 == 0)
				incoming.push_back(0xA5);
			usb.reads.push_back({0, {incoming.begin(), incoming.begin() + 12}});
			if(incoming.size() > 12)
				usb.reads.push_back({0, {incoming.begin() + 12, incoming.end()}});
		}
		char request = 1;
		CHECK(packet_send_cx2(&handle, &request, 1) == 0);
		int transfers = usb.bulk_calls;
		fail_allocation = 0;
		for(const auto &reply : replies) {
			char received[1472]{};
			CHECK(packet_recv_cx2(&handle, reply.empty() ? nullptr : received,
				static_cast<int>(reply.size())) == 0);
			if(!reply.empty())
				CHECK(memcmp(received, reply.data(), reply.size()) == 0);
		}
		CHECK(usb.bulk_calls == transfers && usb.reads.empty());
		fail_allocation = -1;
	}
}

static void pending_stream_capacity() {
	auto handle = ready();
	usb.auto_ack = true;
	char request = 1;
	for(unsigned i = 0; i < NSPIRE_CX2_PENDING_CAPACITY; ++i) {
		usb.reads.push_back({0, frame(StreamService, {static_cast<unsigned char>(i)}, 1)});
		CHECK(packet_send_cx2(&handle, &request, 1) == 0);
	}
	auto acknowledged = usb.write_services.size();
	usb.reads.push_back({0, frame(StreamService, {0x53}, 1)});
	CHECK(packet_send_cx2(&handle, &request, 1) == -NSPIRE_ERR_NOMEM);
	CHECK(usb.write_services.size() == acknowledged + 1);
	CHECK(usb.write_services.back() == StreamService);
	for(unsigned i = 0; i < NSPIRE_CX2_PENDING_CAPACITY; ++i) {
		char received = -1;
		CHECK(packet_recv_cx2(&handle, &received, 1) == 0);
		CHECK(received == i);
	}
	usb.reads.push_back({0, frame(StreamService, {0x53}, 1)});
	char received = 0;
	CHECK(packet_recv_cx2(&handle, &received, 1) == 0 && received == 0x53);
	CHECK(usb.reads.empty());
}

static void pending_stream_lifetime() {
	usb.auto_ack = true;
	usb.reads.push_back({0, frame(StreamService, {7}, 1)});
	usb.reads.push_back({0, frame(TimeService, {0})});
	nspire_handle_t *handle = nullptr;
	CHECK(nspire_init(&handle) == 0);
	auto other = ready();
	char received = 0;
	CHECK(packet_recv_cx2(&other, &received, 1) == -NSPIRE_ERR_TIMEOUT);
	CHECK(packet_recv_cx2(handle, &received, 1) == 0 && received == 7);
	usb.reads.push_back({0, frame(StreamService, {8}, 1)});
	CHECK(packet_send_cx2(handle, &received, 1) == 0);
	handle->cx2_handshake_complete = false;
	usb.reads.push_back({0, frame(TimeService, {0})});
	usb.reads.push_back({0, frame(StreamService, {9}, 1)});
	CHECK(packet_recv_cx2(handle, &received, 1) == 0 && received == 9);
	usb.reads.push_back({0, frame(StreamService, {10}, 1)});
	CHECK(packet_send_cx2(handle, &received, 1) == 0);
	nspire_free(handle);
	usb.reads.push_back({0, frame(TimeService, {0})});
	CHECK(nspire_init(&handle) == 0);
	CHECK(packet_recv_cx2(handle, &received, 1) == -NSPIRE_ERR_TIMEOUT);
	nspire_free(handle);
}

static void early_reply_without_send_ack() {
	auto handle = ready();
	usb.reads.push_back({0, frame(StreamService, {7}, 1)});
	char request = 1;
	CHECK(packet_send_cx2(&handle, &request, 1) == -NSPIRE_ERR_TIMEOUT);
	int transfers = usb.bulk_calls;
	char received = 0;
	CHECK(packet_recv_cx2(&handle, &received, 1) == 0 && received == 7);
	CHECK(usb.bulk_calls == transfers);
}

static void oversized_stream_recovery() {
	for(bool during_send : {false, true}) {
		for(size_t length = sizeof(packet) + 1; length <= NSPIRE_CX2_MAX_PAYLOAD; ++length) {
			reset();
			auto handle = ready();
			packet received{};
			std::vector<unsigned char> valid(16);
			valid[0] = 0x54;
			valid[1] = 0xFD;
			valid[15] = 0x51;
			usb.reads.push_back({0, frame(StreamService, std::vector<unsigned char>(length), 1)});
			if(during_send) {
				usb.auto_ack = true;
				char request = 1;
				CHECK(packet_send_cx2(&handle, &request, 1) == -NSPIRE_ERR_INVALPKT);
			} else
				CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_INVALPKT);
			usb.reads.push_back({0, frame(StreamService, valid, 1)});
			CHECK(packet_recv(&handle, &received) == 0);
			CHECK(received.magic == 0x54FD && received.data_size == 0);
			CHECK(usb.reads.empty());
		}
	}
}

static void packet_length_validation() {
	auto handle = ready();
	packet received{};
	std::vector<unsigned char> empty_packet(16);
	empty_packet[0] = 0x54;
	empty_packet[1] = 0xFD;
	empty_packet[15] = 0x51;
	usb.reads.push_back({0, frame(StreamService, empty_packet)});
	CHECK(packet_recv(&handle, &received) == 0);
	CHECK(received.magic == 0x54FD && received.data_size == 0);
	usb.reads.push_back({0, frame(StreamService, {1, 2, 3})});
	CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_INVALPKT);
	for(uint32_t declared : {1441U, UINT_MAX}) {
		reset();
		std::vector<unsigned char> bytes(offsetof(packet, data) + sizeof(uint32_t));
		auto *header = reinterpret_cast<packet*>(bytes.data());
		header->data_size = 0xFF;
		header->bigdatasize = htonl(declared);
		usb.reads.push_back({0, frame(StreamService, bytes)});
		CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_INVALPKT);
	}
	reset();
	std::vector<unsigned char> bytes(offsetof(packet, data));
	auto *header = reinterpret_cast<packet*>(bytes.data());
	header->data_size = 4;
	usb.reads.push_back({0, frame(StreamService, bytes)});
	CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_INVALPKT);
	reset();
	handle.is_cx2 = false;
	usb.reads.push_back({0, empty_packet});
	CHECK(packet_recv(&handle, &received) == 0);
	usb.reads.push_back({0, {1, 2, 3}});
	CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_INVALPKT);
	usb.reads.push_back({LIBUSB_ERROR_NO_DEVICE, {}});
	CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_DISCONNECTED);
}

static void padded_screenshot_packets() {
	auto handle = ready();
	packet sent = packet_new(&handle);
	sent.data_size = 0xFF;
	sent.bigdatasize = htonl(sizeof(sent.bigdata));
	for(size_t i = 0; i < sizeof(sent.bigdata); i++)
		sent.bigdata[i] = static_cast<uint8_t>(i);
	sent.bigdata[0] = 2;
	usb.auto_ack = true;
	CHECK(packet_send(&handle, sent) == 0);
	CHECK(usb.last_write.size() == 1473 && usb.last_write.back() == 0);
	auto *written = reinterpret_cast<NNSEMessage*>(usb.last_write.data());
	CHECK(ntohs(written->length) == 1472);
	CHECK(compute_checksum(usb.last_write.data(), 1472) == 0xFFFF);
	std::vector<unsigned char> navnet(usb.last_write.begin() + sizeof(NNSEMessage), usb.last_write.end() - 1);
	auto screenshot = frame(StreamService, navnet, 1);
	CHECK(screenshot.size() == 1472);
	screenshot.push_back(0xA5);
	packet received{};
	for(int split : {0, 64, 1472}) {
		reset();
		if(split) {
			usb.reads.push_back({0, {screenshot.begin(), screenshot.begin() + split}});
			usb.reads.push_back({0, {screenshot.begin() + split, screenshot.end()}});
		} else
			usb.reads.push_back({0, screenshot});
		CHECK(packet_recv(&handle, &received) == 0);
		CHECK(packet_datasize(&received) == sizeof(sent.bigdata));
		CHECK(memcmp(received.bigdata, sent.bigdata, sizeof(sent.bigdata)) == 0);
		CHECK(usb.reads.empty());
	}
	reset();
	auto excess = screenshot;
	excess.push_back(0);
	usb.reads.push_back({0, excess});
	CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_INVALPKT);
	reset();
	auto corrupt = screenshot;
	corrupt[sizeof(NNSEMessage) + 20] ^= 1;
	usb.reads.push_back({0, corrupt});
	CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_INVALPKT);
	reset();
	screenshot.pop_back();
	usb.reads.push_back({0, screenshot});
	CHECK(packet_recv(&handle, &received) == -NSPIRE_ERR_TIMEOUT);
}

static void init_fallback_and_reconnect() {
	calculator.pid = NSP_PID;
	usb.open_status = LIBUSB_ERROR_ACCESS;
	nspire_handle_t *handle = nullptr;
	CHECK(nspire_init(&handle) == -NSPIRE_ERR_ACCESS);
	CHECK(usb.open_calls == 1 && handle == nullptr);
	reset();
	usb.auto_ack = true;
	for(int i = 0; i < 3; i++) {
		usb.reads.push_back({0, frame(TimeService, {0})});
		CHECK(nspire_init(&handle) == 0);
		nspire_free(handle);
	}
	CHECK(usb.reset_calls == (cx2_interface_only ? 0 : 3));
	CHECK(usb.set_alternate_calls == (cx2_interface_only ? 3 : 0));
	CHECK(usb.close_calls == 3 && usb.exit_calls == 0);
	CHECK(usb.init_calls == 0);
}

static void idle_service_reconnect() {
	usb.auto_ack = true;
	usb.reads.push_back({0, frame(TimeService, {0})});
	nspire_handle_t *handle = nullptr;
	CHECK(nspire_init(&handle) == 0);
	CHECK(service_connect(handle, 0x4024) == 0);
	CHECK(service_disconnect(handle) == 0);
	CHECK(service_connect(handle, 0x4024) == 0);
	CHECK(usb.close_calls == 0 && usb.open_calls == 1);
	CHECK(service_disconnect(handle) == 0);
	usleep(1100000);
	usb.reads.push_back({0, frame(TimeService, {0})});
	CHECK(service_connect(handle, 0x4024) == 0);
	CHECK(usb.close_calls == 1 && usb.open_calls == 2);
	CHECK(handle->connected && handle->host_sid == 0x8000 && handle->device_sid == 0x4024);
	CHECK(usb.reads.empty());
	CHECK(service_disconnect(handle) == 0);
	nspire_free(handle);
	CHECK(usb.close_calls == 2);
}

static void idle_service_reconnect_errors() {
	for(int scenario = 0; scenario < 3; ++scenario) {
		reset();
		usb.auto_ack = true;
		usb.reads.push_back({0, frame(TimeService, {0})});
		nspire_handle_t *handle = nullptr;
		CHECK(nspire_init(&handle) == 0);
		handle->device.last_transfer_ms = transferClock() - 1100;
		usb.write_services.clear();
		if(scenario == 0) {
			handle->connected = 1;
			CHECK(service_connect(handle, 0x4024) == -NSPIRE_ERR_BUSY);
			CHECK(usb.close_calls == 0 && usb.open_calls == 1);
		} else {
			if(scenario == 1) usb.open_status = LIBUSB_ERROR_ACCESS;
			else usb.reads.push_back({LIBUSB_ERROR_TIMEOUT, {}});
			CHECK(service_connect(handle, 0x4024) ==
				(scenario == 1 ? -NSPIRE_ERR_ACCESS : -NSPIRE_ERR_TIMEOUT));
			CHECK(!handle->connected && !handle->device.dev);
			CHECK(service_connect(handle, 0x4024) == -NSPIRE_ERR_DISCONNECTED);
			CHECK(usb.open_calls == 2);
		}
		CHECK(usb.write_services.empty());
		int closes = usb.close_calls;
		nspire_free(handle);
		CHECK(usb.close_calls == closes + (scenario == 0));
	}
}

static void idle_reconnect_identity() {
	unsigned failures = 0;
	for(int scenario = 0; scenario < 4; ++scenario) {
		reset();
		usb.auto_ack = true;
		usb.reads.push_back({0, frame(TimeService, {0})});
		nspire_handle_t *handle = nullptr;
		CHECK(nspire_init(&handle) == 0);
		libusb_device other{NSP_PID_CX2, static_cast<uint8_t>(scenario == 2 ? 2 : 7)};
		devices[0] = &other;
		bool original_present = scenario == 0 || scenario == 3;
		if(original_present)
			devices[1] = &calculator;
		else
			calculator.attached = false;
		if(scenario == 3)
			usb.no_ports = true;
		handle->device.last_transfer_ms = transferClock() - 1100;
		usb.write_services.clear();
		usb.opened_devices.clear();
		usb.reads.push_back({0, frame(TimeService, {0})});
		int status = service_connect(handle, 0x4024);
		bool correct = original_present
			? status == 0 && handle->device.dev && handle->device.dev->device == &calculator
			: status == -NSPIRE_ERR_DISCONNECTED && !handle->device.dev && !handle->connected &&
				usb.write_services.empty();
		correct = correct && usb.opened_devices == std::vector<libusb_device *>{&calculator};
		if(!correct) {
			std::cerr << "idle identity scenario=" << scenario << " status=" << status << '\n';
			++failures;
		}
		if(!original_present && status != 0)
			CHECK(service_connect(handle, 0x4024) == -NSPIRE_ERR_DISCONNECTED);
		nspire_free(handle);
		CHECK(calculator.references == 0 && other.references == 0 && usb.ref_calls == usb.unref_calls);
	}
	CHECK(failures == 0);
}

static void idle_reconnect_reset_failure() {
	if(cx2_interface_only)
		return;
	usb.auto_ack = true;
	usb.reads.push_back({0, frame(TimeService, {0})});
	nspire_handle_t *handle = nullptr;
	CHECK(nspire_init(&handle) == 0);
	handle->device.last_transfer_ms = transferClock() - 1100;
	usb.write_services.clear();
	usb.resets.push_back(LIBUSB_ERROR_NO_DEVICE);
	usb.move_on_reset = true;
	CHECK(service_connect(handle, 0x4024) == -NSPIRE_ERR_DISCONNECTED);
	CHECK(!handle->device.dev && !handle->connected && usb.write_services.empty());
	CHECK(usb.open_calls == 2 && usb.close_calls == 2 && usb.delay_calls == 0);
	CHECK(calculator.references == 0 && usb.ref_calls == usb.unref_calls);
	nspire_free(handle);
	CHECK(usb.close_calls == 2);
}

static void alternate_endpoint_traffic() {
	for(uint8_t alternate : {0, 3}) {
		if(alternate && !cx2_interface_only)
			continue;
		reset();
		usb.current_alternate = alternate;
		usb.expected_ep_in = alternate ? 0x82 : 0x81;
		usb.expected_ep_out = alternate ? 0x02 : 0x01;
		usb.auto_ack = true;
		usb.reads.push_back({0, frame(AddrReqService,
			std::vector<unsigned char>(sizeof(NNSEMessage_AddrReq) - sizeof(NNSEMessage)))});
		usb.reads.push_back({0, frame(UnknownService, {1}, 1)});
		usb.reads.push_back({0, frame(TimeService, {0}, 1)});
		nspire_handle_t *handle = nullptr;
		CHECK(nspire_init(&handle) == 0);
		CHECK(handle->device.ep_in == usb.expected_ep_in && handle->device.ep_out == usb.expected_ep_out);
		CHECK(handle->cx2_handshake_complete && usb.reads.empty());
		CHECK((usb.write_services == std::vector<uint8_t>{AddrReqService, AddrReqService,
			UnknownService | AckFlag, UnknownService, TimeService | AckFlag, TimeService, StreamService}));
		CHECK(usb.bulk_calls == 11);

		char request[] = {9, 8, 7, 6};
		CHECK(packet_send_cx2(handle, request, sizeof request) == 0);
		CHECK(usb.last_write.size() == sizeof(NNSEMessage) + sizeof request);
		CHECK(memcmp(usb.last_write.data() + sizeof(NNSEMessage), request, sizeof request) == 0);
		std::vector<unsigned char> response(100);
		for(size_t i = 0; i < response.size(); i++)
			response[i] = static_cast<unsigned char>(i);
		auto packet = frame(StreamService, response, 1);
		usb.reads.push_back({0, {packet.begin(), packet.begin() + 13}});
		usb.reads.push_back({0, {packet.begin() + 13, packet.end()}});
		char received[100]{};
		CHECK(packet_recv_cx2(handle, received, sizeof received) == 0);
		CHECK(memcmp(received, response.data(), response.size()) == 0);
		CHECK(usb.reads.empty() && usb.bulk_calls == 16);
		CHECK(usb.write_services.back() == (StreamService | AckFlag));
		nspire_free(handle);
		CHECK(usb.close_calls == 1 && usb.release_calls == 1);
	}
}

template <typename F> static std::string capture_trace(F run) {
	FILE *capture = tmpfile();
	CHECK(capture);
	fflush(stderr);
	int saved = dup(STDERR_FILENO);
	CHECK(saved >= 0 && dup2(fileno(capture), STDERR_FILENO) >= 0);
	try {
		run();
	} catch(...) {
		fflush(stderr);
		dup2(saved, STDERR_FILENO);
		close(saved);
		fclose(capture);
		throw;
	}
	fflush(stderr);
	CHECK(dup2(saved, STDERR_FILENO) >= 0);
	close(saved);
	rewind(capture);
	char bytes[32768];
	size_t size = fread(bytes, 1, sizeof bytes, capture);
	CHECK(!ferror(capture) && feof(capture));
	fclose(capture);
	return {bytes, size};
}

static void diagnostic_metadata() {
	setenv("NSPIRE_TRACE", "1", 1);
	unsigned missing = 0;
	for(unsigned scenario = 0; scenario < 6; ++scenario) {
		reset();
		auto handle = ready();
		char request[] = "PRIVATE-DOCUMENT-CONTENT";
		std::string log = capture_trace([&] {
			if(scenario == 0) {
				usb.writes.push_back({LIBUSB_ERROR_TIMEOUT, {}, 7});
				CHECK(packet_send_cx2(&handle, request, sizeof request) == -NSPIRE_ERR_TIMEOUT);
				CHECK(usb.bulk_calls == 1);
			} else if(scenario == 1) {
				CHECK(packet_send_cx2(&handle, request, sizeof request) == -NSPIRE_ERR_TIMEOUT);
				CHECK(usb.bulk_calls == 2);
			} else if(scenario == 2) {
				for(int i = 0; i < 10; ++i) usb.reads.push_back({0, frame(EchoService | AckFlag)});
				CHECK(packet_recv_cx2(&handle, request, sizeof request) == -NSPIRE_ERR_TIMEOUT);
				CHECK(usb.bulk_calls == 10);
			} else if(scenario == 3) {
				NNSEMessage message{};
				message.length = htons(sizeof message);
				CHECK(writePacket(&handle.device, &message, std::chrono::steady_clock::now()) == -NSPIRE_ERR_TIMEOUT);
				CHECK(usb.bulk_calls == 0);
			} else if(scenario == 4) {
				auto packet = frame(StreamService, {1, 2});
				packet.resize(sizeof(NNSEMessage));
				usb.reads.push_back({0, packet});
				CHECK(packet_recv_cx2(&handle, request, sizeof request) == -NSPIRE_ERR_TIMEOUT);
				CHECK(usb.bulk_calls == 2);
			} else {
				usb.auto_ack = true;
				usb.reads.push_back({0, frame(EchoService | AckFlag)});
				CHECK(packet_send_cx2(&handle, request, sizeof request) == 0);
				CHECK(usb.bulk_calls == 3);
			}
		});
		const char *fields[][3] = {
			{"direction=out", "usb_status=-7", "transferred=7"},
			{"phase=ack", "reason=read", "expected_seq="},
			{"phase=receive", "reason=packet-limit", "remaining_ms="},
			{"direction=out", "reason=deadline", "remaining_ms=0"},
			{"direction=in", "phase=continuation", "requested=2"},
			{"expected_seq=", "received_seq=", "matched=1"}
		};
		for(auto field : fields[scenario]) {
			if(log.find(field) == std::string::npos) {
				std::cerr << "diagnostic scenario " << scenario << ": missing " << field << '\n';
				++missing;
			}
		}
		CHECK(log.find("PRIVATE") == std::string::npos);
		CHECK(log.find("50 52 49 56") == std::string::npos);
	}
	unsetenv("NSPIRE_TRACE");
	reset();
	auto handle = ready();
	CHECK(capture_trace([&] {
		char request = 1;
		CHECK(packet_send_cx2(&handle, &request, 1) == -NSPIRE_ERR_TIMEOUT);
	}).empty());
	CHECK(missing == 0);
}

static void handshake_after_retries() {
	auto handle = ready();
	handle.cx2_handshake_complete = false;
	for(unsigned i = 0; i < 10; ++i) {
		auto incoming = frame(StreamService, std::vector<unsigned char>(18, 0), i ? 9 : 1);
		auto *header = reinterpret_cast<NNSEMessage*>(incoming.data());
		header->src = 0xff;
		header->seqno = htons(7925);
		header->csum = 0;
		header->csum = htons(compute_checksum(incoming.data(), static_cast<uint32_t>(incoming.size())) ^ 0xffff);
		usb.reads.push_back({0, incoming});
	}
	usb.reads.push_back({0, frame(AddrReqService, std::vector<unsigned char>(65))});
	usb.reads.push_back({0, frame(UnknownService, {1})});
	usb.reads.push_back({0, frame(TimeService, {0})});
	CHECK(assureReady(&handle) == 0);
	CHECK(handle.cx2_handshake_complete && usb.reads.empty() && handle.cx2_pending_count == 1);
	CHECK(std::count(usb.write_services.begin(), usb.write_services.end(), StreamService | AckFlag) == 10);
	CHECK(std::count(usb.write_services.begin(), usb.write_services.end(), AddrReqService) == 2);
	CHECK(std::count(usb.write_services.begin(), usb.write_services.end(), UnknownService) == 1);
	CHECK(std::count(usb.write_services.begin(), usb.write_services.end(), TimeService) == 1);
	CHECK(usb.write_services.size() == 14);
}

static void handshake_deadline_and_errors() {
	for(bool streams : {false, true}) {
		reset();
		auto handle = ready();
		handle.cx2_handshake_complete = false;
		for(unsigned i = 0; i < 1000; ++i)
			usb.reads.push_back({0, streams ? frame(StreamService, {0}, i ? 9 : 1)
				: frame(UnknownService, {1}), -1, 1000});
		auto started = std::chrono::steady_clock::now();
		setenv("NSPIRE_TRACE", "1", 1);
		std::string log = capture_trace([&] {
			CHECK(assureReady(&handle, started + std::chrono::milliseconds(50)) == -NSPIRE_ERR_TIMEOUT);
		});
		unsetenv("NSPIRE_TRACE");
		auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
			std::chrono::steady_clock::now() - started).count();
		CHECK(elapsed >= 40 && elapsed < 1000);
		CHECK(!handle.cx2_handshake_complete && !usb.reads.empty());
		CHECK(handle.cx2_pending_count == (streams ? 1 : 0));
		CHECK(log.find("reason=deadline") != std::string::npos);
		CHECK(std::count(usb.write_services.begin(), usb.write_services.end(), TimeService) == 0);
	}
	for(unsigned scenario = 0; scenario < 7; ++scenario) {
		reset();
		auto handle = ready();
		handle.cx2_handshake_complete = false;
		int expected = -NSPIRE_ERR_TIMEOUT;
		if(scenario == 0) {
			usb.reads.push_back({0, {}});
		} else if(scenario == 1) {
			auto corrupt = frame(TimeService, {0}, 1);
			corrupt.back() ^= 1;
			usb.reads.push_back({0, corrupt});
			expected = -NSPIRE_ERR_INVALPKT;
		} else if(scenario == 2) {
			usb.reads.push_back({0, frame(TimeService, {0}, 1)});
			usb.writes.push_back({LIBUSB_ERROR_NO_DEVICE, {}});
			expected = -NSPIRE_ERR_DISCONNECTED;
		} else if(scenario == 3) {
			usb.reads.push_back({0, frame(TimeService, {0})});
			usb.writes.push_back({LIBUSB_ERROR_TIMEOUT, {}});
		} else if(scenario == 4) {
			for(unsigned i = 0; i <= NSPIRE_CX2_PENDING_CAPACITY; ++i)
				usb.reads.push_back({0, frame(StreamService, {0}, 1)});
			expected = -NSPIRE_ERR_NOMEM;
		} else if(scenario == 6) {
			usb.reads.push_back({0, frame(TimeService, {1})});
			expected = -NSPIRE_ERR_INVALPKT;
		}
		usb.reads.push_back({0, frame(TimeService, {0})});
		auto deadline = std::chrono::steady_clock::now();
		if(scenario != 5) deadline += std::chrono::seconds(1);
		CHECK(assureReady(&handle, deadline) == expected);
		CHECK(!handle.cx2_handshake_complete && usb.reads.size() == 1);
		CHECK(usb.write_services.size() == (scenario == 2 || scenario == 3 ? 1u
			: scenario == 4 ? NSPIRE_CX2_PENDING_CAPACITY : 0u));
		CHECK(usb.bulk_calls == (scenario == 5 ? 0 : scenario == 4
			? NSPIRE_CX2_PENDING_CAPACITY * 2 + 1 : scenario == 2 || scenario == 3 ? 2 : 1));
	}
}

static void handshake_frame_metadata() {
	unsigned missing = 0;
	for(bool enabled : {true, false}) {
		if(enabled) setenv("NSPIRE_TRACE", "1", 1);
		else unsetenv("NSPIRE_TRACE");
		reset();
		auto handle = ready();
		handle.cx2_handshake_complete = false;
		std::string secret = "PRIVATE-NNSE-BYTES";
		for(unsigned i = 0; i < 10; ++i) {
			auto incoming = frame(StreamService, {secret.begin(), secret.end()}, 9);
			auto *message = reinterpret_cast<NNSEMessage*>(incoming.data());
			message->seqno = htons(static_cast<uint16_t>(0x1234 + i));
			message->csum = 0;
			message->csum = htons(compute_checksum(incoming.data(), incoming.size()) ^ 0xFFFF);
			CHECK(incoming.size() == 30);
			usb.reads.push_back({0, incoming});
		}
		std::string log = capture_trace([&] {
			CHECK(assureReady(&handle) == -NSPIRE_ERR_TIMEOUT);
			CHECK(!handle.cx2_handshake_complete && handle.cx2_pending_count == 10);
			CHECK(usb.bulk_calls == 21 && usb.reads.empty());
			CHECK(usb.write_services == std::vector<uint8_t>(10, StreamService | AckFlag));
		});
		if(!enabled) {
			CHECK(log.empty());
			continue;
		}
		for(unsigned i = 0; i < 10; ++i) {
			const std::string fields[] = {
				"transport phase=frame service=0x04 source=0x01 destination=0xfe seq=" +
					std::to_string(0x1234 + i) + " length=30 req_ack=0x09",
				"transport phase=handshake event=frame index=" + std::to_string(i + 1) +
					" status=0 complete=0 pending=" + std::to_string(i + 1)
			};
			for(const auto &field : fields) {
				if(log.find(field) == std::string::npos) {
					std::cerr << "handshake diagnostic missing " << field << '\n';
					++missing;
				}
			}
		}
		CHECK(log.find("reason=io-timeout") != std::string::npos);
		CHECK(log.find(secret) == std::string::npos);
		CHECK(log.find("50 52 49 56") == std::string::npos);
	}
	setenv("NSPIRE_TRACE", "1", 1);
	reset();
	auto handle = ready();
	handle.cx2_handshake_complete = false;
	usb.reads.push_back({0, frame(TimeService, {0}, 1)});
	std::string log = capture_trace([&] {
		CHECK(assureReady(&handle) == 0);
		CHECK(handle.cx2_handshake_complete && handle.cx2_pending_count == 0);
		CHECK(usb.bulk_calls == 3 && usb.reads.empty());
	});
	for(const char *field : {
		"transport phase=frame service=0x02 source=0x01 destination=0xfe seq=0 length=13 req_ack=0x01",
		"transport phase=handshake event=frame index=1 status=0 complete=1 pending=0"}) {
		if(log.find(field) == std::string::npos) {
			std::cerr << "handshake diagnostic missing " << field << '\n';
			++missing;
		}
	}
	unsetenv("NSPIRE_TRACE");
	CHECK(missing == 0);
}

static void navnet_header_metadata() {
	setenv("NSPIRE_TRACE", "1", 1);
	const std::vector<unsigned char> header{0x54, 0xfd, 0x64, 0x01, 0x40, 0x03,
		0x64, 0x00, 0x80, 0x12, 0xab, 0xcd, 2, 0x0a, 0x47, 0x38};
	unsigned missing = 0;
	for(unsigned scenario = 0; scenario < 3; ++scenario) {
		bool complete = scenario != 0;
		reset();
		auto handle = ready();
		auto incoming = header;
		if(complete) incoming.insert(incoming.end(), {0x58, 0x59});
		else incoming.pop_back();
		if(scenario == 2) incoming[0] = 0;
		usb.reads.push_back({0, frame(StreamService, incoming, 1)});
		std::string log = capture_trace([&] {
			char received[18]{};
			CHECK(packet_recv_cx2(&handle, received, sizeof received) == sizeof received - incoming.size());
			CHECK(memcmp(received, incoming.data(), incoming.size()) == 0);
			CHECK(usb.bulk_calls == 2);
		});
		const char *expected = "transport phase=navnet-header magic=0x54fd source=0x6401 source_service=0x4003 destination=0x6400 destination_service=0x8012 data_size=2 ack=0x0a seq=71";
		if(scenario == 1 && log.find(expected) == std::string::npos) {
			std::cerr << "NavNet diagnostic missing " << expected << '\n';
			++missing;
		}
		if(scenario != 1) CHECK(log.find("phase=navnet-header") == std::string::npos);
		CHECK(log.find("XY") == std::string::npos && log.find("58 59") == std::string::npos);
	}
	unsetenv("NSPIRE_TRACE");
	CHECK(missing == 0);
}

static int receive_stream(nspire_handle &handle, uint8_t source, uint8_t destination,
		uint16_t sequence, uint8_t flags) {
	auto incoming = frame(StreamService, std::vector<unsigned char>(18, 0x71), flags);
	auto *message = reinterpret_cast<NNSEMessage*>(incoming.data());
	message->src = source;
	message->dest = destination;
	message->seqno = htons(sequence);
	message->csum = 0;
	message->csum = htons(compute_checksum(incoming.data(), incoming.size()) ^ 0xffff);
	usb.reads.push_back({0, incoming});
	unsigned char received[30];
	message = reinterpret_cast<NNSEMessage*>(received);
	auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
	int status = readPacket(&handle.device, message, sizeof received, deadline);
	return status ? status : handlePacket(&handle, message, deadline);
}

static void accepted_stream_retransmissions() {
	unsigned failed = 0;
	auto expect = [&](bool condition, const char *label) {
		if(!condition) {
			std::cerr << "stream retry: " << label << '\n';
			++failed;
		}
	};
	for(bool dequeued : {false, true}) {
		reset();
		auto handle = ready();
		CHECK(receive_stream(handle, 0xff, 0xfe, 1900, 1) == 0);
		if(dequeued) {
			char received[18];
			CHECK(packet_recv_cx2(&handle, received, sizeof received) == 0);
			CHECK(received[0] == 0x71 && handle.cx2_pending_count == 0);
		}
		for(unsigned i = 0; i < 9; ++i)
			CHECK(receive_stream(handle, 0xff, 0xfe, 1900, 9) == 0);
		expect(handle.cx2_pending_count == (dequeued ? 0 : 1),
			dequeued ? "accepted retry must not be delivered again after dequeue" :
			"accepted retries must occupy only one queue slot");
		CHECK(usb.bulk_calls == 20 && usb.write_services.size() == 10);
		auto *ack = reinterpret_cast<const NNSEMessage*>(usb.last_write.data());
		CHECK(ack->src == 0xfe && ack->dest == 0xff && ntohs(ack->seqno) == 1900 && ack->reqAck == 8);
	}
	reset();
	auto handle = ready();
	CHECK(receive_stream(handle, 0xff, 0xfe, 1900, 9) == 0);
	expect(handle.cx2_pending_count == 1, "first observed retry must be delivered");
	CHECK(receive_stream(handle, 1, 0xfe, 1900, 1) == 0);
	CHECK(receive_stream(handle, 0xff, 0xff, 1900, 1) == 0);
	CHECK(receive_stream(handle, 0xff, 0xfe, 1900, 9) == 0);
	CHECK(receive_stream(handle, 1, 0xfe, 1900, 9) == 0);
	CHECK(receive_stream(handle, 0xff, 0xff, 1900, 9) == 0);
	expect(handle.cx2_pending_count == 3, "source and destination histories must remain independent");
	reset();
	handle = ready();
	CHECK(receive_stream(handle, 1, 0xfe, 65535, 1) == 0);
	CHECK(receive_stream(handle, 1, 0xfe, 0, 9) == 0);
	CHECK(receive_stream(handle, 1, 0xfe, 0, 9) == 0);
	expect(handle.cx2_pending_count == 2, "sequence wrap must accept the new retry once");
	auto queued = handle.cx2_pending_count;
	CHECK(receive_stream(handle, 1, 0xfe, 0, 1) == 0);
	expect(handle.cx2_pending_count == queued + 1, "an unflagged packet is not a retry");
	reset();
	handle = ready();
	usb.writes.push_back({LIBUSB_ERROR_TIMEOUT, {}});
	CHECK(receive_stream(handle, 1, 0xfe, 1900, 1) == -NSPIRE_ERR_TIMEOUT);
	CHECK(handle.cx2_pending_count == 0);
	CHECK(receive_stream(handle, 1, 0xfe, 1900, 9) == 0);
	expect(handle.cx2_pending_count == 1, "failed ACK must not mark a packet accepted");
	usb.writes.push_back({LIBUSB_ERROR_TIMEOUT, {}});
	CHECK(receive_stream(handle, 1, 0xfe, 1900, 9) == -NSPIRE_ERR_TIMEOUT);
	expect(handle.cx2_pending_count == 1, "failed duplicate ACK must retain the accepted packet");
	CHECK(receive_stream(handle, 1, 0xfe, 1900, 9) == 0);
	expect(handle.cx2_pending_count == 1, "retry after duplicate ACK failure must not redeliver");
	reset();
	handle = ready();
	for(unsigned i = 0; i < NSPIRE_CX2_PENDING_CAPACITY; ++i)
		CHECK(receive_stream(handle, static_cast<uint8_t>(i), 0xfe, 1900, 1) == 0);
	int calls = usb.bulk_calls;
	expect(receive_stream(handle, 0, 0xfe, 1900, 9) == 0,
		"accepted duplicate must be ACKed even with a full queue");
	expect(usb.bulk_calls == calls + 2 && handle.cx2_pending_count == NSPIRE_CX2_PENDING_CAPACITY,
		"full queue duplicate must not add an entry or skip its ACK");
	CHECK(receive_stream(handle, 20, 0xfe, 1900, 9) == -NSPIRE_ERR_NOMEM);
	char received[18];
	CHECK(packet_recv_cx2(&handle, received, sizeof received) == 0);
	CHECK(receive_stream(handle, 20, 0xfe, 1900, 9) == 0);
	expect(handle.cx2_pending_count == NSPIRE_CX2_PENDING_CAPACITY,
		"queue refusal must not mark an unseen retry accepted");
	reset();
	handle = ready();
	CHECK(receive_stream(handle, 1, 0xfe, 1900, 1) == 0);
	auto other = ready();
	CHECK(receive_stream(other, 1, 0xfe, 1900, 9) == 0);
	expect(other.cx2_pending_count == 1, "accepted state must belong to one handle");
	handle.cx2_handshake_complete = false;
	usb.reads.push_back({0, frame(TimeService, {0})});
	CHECK(assureReady(&handle) == 0);
	CHECK(receive_stream(handle, 1, 0xfe, 1900, 9) == 0);
	expect(handle.cx2_pending_count == 1, "new handshake must clear prior accepted state");
	CHECK(failed == 0);
}

static void error_strings() {
	for(int i = 0; i < NSPIRE_ERR_MAX; i++)
		CHECK(nspire_strerror(-i) != nullptr);
	CHECK(std::string(nspire_strerror(-NSPIRE_ERR_MAX)) == "Unknown error");
	CHECK(std::string(nspire_strerror(INT_MIN)) == "Unknown error");
	CHECK(std::string(nspire_strerror(1)) == "Unknown error");
	CHECK(std::string(nspire_strerror(-NSPIRE_ERR_ACCESS)) == "USB access denied");
	CHECK(std::string(nspire_strerror(-NSPIRE_ERR_USB_BUSY)) == "USB interface busy");
	CHECK(std::string(nspire_strerror(-NSPIRE_ERR_DISCONNECTED)) == "USB device disconnected");
	CHECK(std::string(nspire_strerror(-NSPIRE_ERR_USB_CONFIG)) == "USB configuration unavailable");
}

int main() {
	void (*tests[])() = {discovery_errors, setup_errors, reset_recovery, reset_recovery_bounded,
		interface_startup, configuration_preflight, interface_startup_errors, descriptor_cleanup, receive_error_classes,
		fragmented_receive, partial_timeout_receive, zero_progress_and_truncation,
		ack_and_handshake_failures, send_errors_and_deadlines, packet_length_validation,
		allocation_failures, early_stream_reply, early_stream_fifo, pending_stream_capacity,
		pending_stream_lifetime, early_reply_without_send_ack, padded_screenshot_packets, init_fallback_and_reconnect,
		idle_service_reconnect, idle_service_reconnect_errors, idle_reconnect_identity, idle_reconnect_reset_failure, alternate_endpoint_traffic, oversized_stream_recovery, error_strings, diagnostic_metadata,
		handshake_after_retries, handshake_deadline_and_errors, handshake_frame_metadata, navnet_header_metadata, accepted_stream_retransmissions};
	try {
		for(auto test : tests) {
			reset();
			test();
		}
	} catch(const std::exception &failure) {
		std::cerr << failure.what() << '\n';
		return 1;
	}
	std::cout << sizeof(tests) / sizeof(*tests) << " transport fault-injection groups passed\n";
}
