#include <cstdlib>
#include <cstring>
#include <iostream>
#include <vector>

static bool fail_alloc;
static void *packet_alloc(size_t size) {
    if (fail_alloc) return nullptr;
    void *packet = std::malloc(size);
    if (packet) std::memset(packet, 0xa5, size);
    return packet;
}

#define malloc packet_alloc
#include "../usblink_cx2.cpp"
#undef malloc

static unsigned checks, failures, acknowledgments, deliveries;
static bool accept_write = true;
static std::vector<uint8_t> sent;
bool usblink_connected;

void error(const char *, ...) { std::abort(); }
void gui_status_printf(const char *, ...) {}
void gui_usblink_changed(bool) {}
bool usb_cx2_packet_to_calc(uint8_t, const uint8_t *packet, size_t size) {
    sent.assign(packet, packet + size);
    return accept_write;
}
void usblink_received_packet(const uint8_t *packet, uint32_t) {
    if (packet) ++deliveries;
    else ++acknowledgments;
}
static void check(bool passed, const char *name) {
    ++checks;
    if (!passed) { ++failures; std::cout << "FAIL " << name << '\n'; }
}
static void receive(uint8_t service, uint16_t sequence, uint8_t flags = 0, uint8_t source = AddrCalc) {
    uint8_t bytes[sizeof(NNSEMessage) + 1]{};
    auto *message = reinterpret_cast<NNSEMessage *>(bytes);
    message->service = service;
    message->src = source;
    message->dest = AddrMe;
    message->reqAck = flags;
    const auto length = sizeof(NNSEMessage) + (service == StreamService ? 1 : 0);
    message->length = htons(length);
    message->seqno = htons(sequence);
    message->csum = htons(compute_checksum(bytes, length) ^ 0xffff);
    check(usblink_cx2_handle_packet(bytes, length), "valid packet accepted");
}
int main() {
    usblink_cx2_reset();
    usblink_cx2_state.handshake_complete = true;
    const uint8_t request = 42;
    check(usblink_cx2_send_navnet(&request, 1), "stream request sent");
    const uint16_t first = ntohs(reinterpret_cast<const NNSEMessage *>(sent.data())->seqno);
    check(sent[4] == 0, "reserved header byte initialized");
    receive(StreamService | AckFlag, first + 1);
    check(acknowledgments == 0, "unrelated acknowledgment ignored");
    receive(StreamService | AckFlag, first, 0, 2);
    check(acknowledgments == 0, "wrong sender acknowledgment ignored");
    receive(StreamService | AckFlag, first);
    check(acknowledgments == 1, "matching acknowledgment delivered");
    receive(StreamService | AckFlag, first);
    check(acknowledgments == 1, "duplicate acknowledgment ignored");
    check(usblink_cx2_send_navnet(&request, 1), "second stream request sent");
    const uint16_t second = ntohs(reinterpret_cast<const NNSEMessage *>(sent.data())->seqno);
    receive(StreamService | AckFlag, first);
    check(acknowledgments == 1, "old acknowledgment cannot finish new request");
    receive(StreamService | AckFlag, second);
    check(acknowledgments == 2, "second matching acknowledgment delivered");
    receive(StreamService, 19, 9);
    check(deliveries == 1, "unseen retry delivered");
    receive(StreamService, 19, 9);
    check(deliveries == 1, "accepted retry not delivered twice");
    receive(StreamService, 20, 9);
    check(deliveries == 2, "next unseen retry delivered");
    usblink_cx2_reset();
    receive(StreamService | AckFlag, second);
    check(acknowledgments == 2, "reset clears pending acknowledgment");
    receive(StreamService, 20, 9);
    check(deliveries == 3, "reset clears accepted retry history");
    usblink_cx2_state.handshake_complete = true;
    accept_write = false;
    check(!usblink_cx2_send_navnet(&request, 1), "write failure returned");
    const uint16_t failed = ntohs(reinterpret_cast<const NNSEMessage *>(sent.data())->seqno);
    receive(StreamService | AckFlag, failed);
    check(acknowledgments == 2, "failed send has no pending acknowledgment");
    fail_alloc = true;
    check(!usblink_cx2_send_navnet(&request, 1), "allocation failure returned");
    std::cout << "usblinkcx2test: " << checks << " checks, " << failures << " failures\n";
    return failures ? 1 : 0;
}
