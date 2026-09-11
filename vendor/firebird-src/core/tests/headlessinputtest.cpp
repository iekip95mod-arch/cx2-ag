#define main firebird_headless_entry
#include "../../headless/main.cpp"
#undef main

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <unistd.h>
#include <vector>
#include "usblink_queue.h"

static std::mutex received_mutex;
static std::vector<std::string> received;
static unsigned checks;
static unsigned failures;

static void check(bool passed, const char *name) {
    ++checks;
    if (!passed) {
        ++failures;
        std::cout << "FAIL " << name << std::endl;
    }
}

static void capture(const char *line) {
    std::lock_guard<std::mutex> lock(received_mutex);
    received.emplace_back(line);
}

template<class Predicate>
static void await(Predicate ready, const char *name) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!ready()) {
        if (std::chrono::steady_clock::now() >= deadline) {
            check(false, name);
            std::_Exit(1);
        }
        std::this_thread::yield();
    }
}

static void queued_continue(const std::string &first, bool initially_waiting) {
    std::unique_ptr<FILE, decltype(&fclose)> source(tmpfile(), fclose);
    std::unique_ptr<FILE, decltype(&fclose)> saved(fdopen(dup(STDIN_FILENO), "r"), fclose);
    if (!source || !saved) std::_Exit(1);
    const std::string batch = first + "\nc\n";
    if (fwrite(batch.data(), 1, batch.size(), source.get()) != batch.size()) std::_Exit(1);
    rewind(source.get());
    if (dup2(fileno(source.get()), STDIN_FILENO) < 0) std::_Exit(1);
    clearerr(stdin);
    received.clear();
    break_requested = false;
    input_wanted = nullptr;
    input_have_spare = false;
    input_spare.clear();
    exiting = false;
    if (initially_waiting) gui_debugger_request_input(capture);
    std::thread reader(input_reader);
    if (!initially_waiting) {
        await([] { return break_requested.load(); }, "running input requests a debugger break");
        check(break_requested.load(), "input arriving during execution requests a break");
        gui_debugger_request_input(capture);
    }
    await([] {
        std::lock_guard<std::mutex> lock(received_mutex);
        return received.size() == 1;
    }, "first command arrives");
    await([] {
        std::lock_guard<std::mutex> lock(input_m);
        return input_have_spare;
    }, "continue waits for the next debugger request");
    check(break_requested.load(), "queued input wakes a debugger that has not requested its next line");
    gui_debugger_request_input(capture);
    reader.join();
    check(received.size() == 2 && received[0] == first + "\n" && received[1] == "c\n",
          "queued commands are delivered completely and in order");
    check(!break_requested.load(), "delivered Continue leaves no stale debugger break request");
    check(exiting, "the reader observes EOF after all queued input");
    if (dup2(fileno(saved.get()), STDIN_FILENO) < 0) std::_Exit(1);
    clearerr(stdin);
}

int main() {
    queued_continue("ln ?", true);
    queued_continue("ln ?", false);
    queued_continue(std::string(700, 'x'), true);
    usblink_connected = true;
    int key_status = 0;
    const auto key_done = [](int status, void *context) {
        *static_cast<int *>(context) = status;
    };
    usblink_queue_key(0x0d1000, key_done, &key_status);
    usblink_queue_do();
    usblink_received_packet(nullptr, 0);
    usblink_received_packet(nullptr, 0);
    check(key_status == 0 && usblink_queue_size() == 1, "queued OS key waits for disconnect acknowledgement");
    usblink_received_packet(nullptr, 0);
    check(key_status == 100 && usblink_queue_size() == 0, "queued OS key reports completion and releases the queue");
    usblink_queue_key(0x1000000, key_done, &key_status);
    usblink_queue_do();
    check(key_status == -1 && usblink_queue_size() == 0, "invalid queued OS key releases the queue with failure");
    unsigned raw_failures = 0;
    const auto raw_done = [](const uint8_t *bytes, uint32_t size, bool failed, void *context) {
        if (!bytes && !size && failed) ++*static_cast<unsigned *>(context);
    };
    usblink_queue_raw(0x4b45, {0, 0, 0, 20}, raw_done, &raw_failures);
    usblink_queue_do();
    usblink_queue_raw(0x4b45, {0, 0, 0, 20}, raw_done, &raw_failures);
    usblink_queue_reset();
    check(raw_failures == 2 && usblink_queue_size() == 0, "reset fails active and waiting raw requests");
    std::cout << "headless input: " << checks << " checks, " << failures << " failed\n";
    return failures ? 1 : 0;
}
