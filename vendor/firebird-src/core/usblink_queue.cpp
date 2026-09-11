#include <cassert>
#include <atomic>
#include <queue>
#include <chrono>
#include <thread>

#include "usblink_queue.h"

struct usblink_queue_action {
    enum {
        PUT_FILE,
        SEND_OS,
        DIRLIST,
        MOVE,
        DEL_FILE,
        NEW_DIR,
        DEL_DIR,
        GET_FILE,
        RAW,
        KEY
    } action;

    //Members only used if the appropriate action is set
    std::string local;
    std::string remote;
    uint16_t sid = 0;
    uint32_t key = 0;
    std::vector<uint8_t> payload;
    usblink_progress_cb progress_callback = nullptr;
    usblink_dirlist_cb dirlist_callback = nullptr;
    usblink_raw_cb raw_callback = nullptr;
    void *user_data;
};

static std::atomic_bool busy;
static std::queue<usblink_queue_action> usblink_queue;

// Nothing bounds how long an in-flight action may take, and the calculator answers a request it
// cannot serve, a GET for a file that does not exist being the easy one, by saying nothing at all.
// busy then stays set for the life of the session and every later transfer is dropped behind it,
// which reads from outside exactly like the link having gone down. Give the front of the queue a
// deadline instead, refreshed whenever it makes progress.
static std::chrono::steady_clock::time_point action_deadline;
static const std::chrono::seconds action_timeout{20};

static void action_started()
{
    action_deadline = std::chrono::steady_clock::now() + action_timeout;
}

static void dirlist_callback(struct usblink_file *f, bool is_error, void *user_data)
{
    assert(!usblink_queue.empty());
    assert(usblink_queue.front().user_data == user_data);
    assert(usblink_queue.front().action == usblink_queue_action::DIRLIST);

    if(usblink_queue.front().dirlist_callback != nullptr)
        usblink_queue.front().dirlist_callback(f, is_error, user_data);

    if(!f)
    {
        usblink_queue.pop();
        busy = false;
    }
}

static void raw_callback(const uint8_t *data, uint32_t size, bool is_error, void *user_data)
{
    assert(!usblink_queue.empty());
    assert(usblink_queue.front().user_data == user_data);
    assert(usblink_queue.front().action == usblink_queue_action::RAW);

    if(usblink_queue.front().raw_callback != nullptr)
        usblink_queue.front().raw_callback(data, size, is_error, user_data);

    usblink_queue.pop();
    busy = false;
}

static void progress_callback(int progress, void *user_data)
{
    assert(!usblink_queue.empty());
    assert(usblink_queue.front().user_data == user_data);
    if(usblink_queue.front().progress_callback != nullptr)
    {
        auto action = usblink_queue.front().action;
        if(action == usblink_queue_action::PUT_FILE
                || action == usblink_queue_action::SEND_OS
                || action == usblink_queue_action::MOVE
                || action == usblink_queue_action::NEW_DIR
                || action == usblink_queue_action::DEL_DIR
                || action == usblink_queue_action::DEL_FILE
                || action == usblink_queue_action::GET_FILE
                || action == usblink_queue_action::KEY)
            usblink_queue.front().progress_callback(progress, user_data);
        else
            assert(false);
    }

    if(progress < 0 || progress == 100)
    {
        usblink_queue.pop();
        busy = false;
    }
    else
        action_started(); // it is still moving, so give it another window
}

void usblink_queue_do()
{
    bool b = false;
    if(!usblink_connected || usblink_queue.empty())
        return;

    if(!busy.compare_exchange_strong(b, true))
    {
        if(std::chrono::steady_clock::now() < action_deadline)
            return;

        // Clear the half-finished transfer first, or a late reply lands on an action that has
        // already been popped and the callback asserts on the mismatched user_data.
        usblink_abandon_transfer();

        // Fail it the way a refusal would, so the caller waiting on the callback hears back.
        usblink_queue_action stuck = usblink_queue.front();
        if(stuck.dirlist_callback)
            dirlist_callback(nullptr, true, stuck.user_data);
        else if(stuck.action == usblink_queue_action::RAW)
            raw_callback(nullptr, 0, true, stuck.user_data);
        else
            progress_callback(-1, stuck.user_data);
        return;
    }

    action_started();
    usblink_queue_action action = usblink_queue.front();
    switch(action.action)
    {
    case usblink_queue_action::PUT_FILE:
        if(!usblink_put_file(action.local.c_str(), action.remote.c_str(), progress_callback, action.user_data))
        {
            progress_callback(-1, action.user_data);
            busy = false;
        }
        break;
    case usblink_queue_action::SEND_OS:
        if(!usblink_send_os(action.local.c_str(), progress_callback, action.user_data))
        {
            progress_callback(-1, action.user_data);
            busy = false;
        }
        break;
    case usblink_queue_action::DIRLIST:
        usblink_dirlist(action.remote.c_str(), dirlist_callback, action.user_data);
        break;
    case usblink_queue_action::MOVE:
        usblink_move(action.local.c_str(), action.remote.c_str(), progress_callback, action.user_data);
        break;
    case usblink_queue_action::NEW_DIR:
        usblink_new_dir(action.remote.c_str(), progress_callback, action.user_data);
        break;
    case usblink_queue_action::DEL_DIR:
        usblink_delete(action.remote.c_str(), true, progress_callback, action.user_data);
        break;
    case usblink_queue_action::DEL_FILE:
        usblink_delete(action.remote.c_str(), false, progress_callback, action.user_data);
        break;
    case usblink_queue_action::GET_FILE:
        if(!usblink_get_file(action.remote.c_str(), action.local.c_str(), progress_callback, action.user_data))
        {
            progress_callback(-1, action.user_data);
            busy = false;
        }
        break;
    case usblink_queue_action::RAW:
        usblink_raw(action.sid, action.payload.data(), action.payload.size(), raw_callback, action.user_data);
        break;
    case usblink_queue_action::KEY:
        usblink_send_key(action.key, progress_callback, action.user_data);
        break;
    }
}

void usblink_queue_reset()
{
    while(!usblink_queue.empty())
    {
        // Treat as error
        usblink_queue_action action = usblink_queue.front();
        if(action.dirlist_callback)
            action.dirlist_callback(nullptr, true, action.user_data);
        else if(action.raw_callback)
            action.raw_callback(nullptr, 0, true, action.user_data);
        else if(action.progress_callback)
            action.progress_callback(-1, action.user_data);

        usblink_queue.pop();
    }

    busy = false;

    usblink_reset();
}

void usblink_queue_add(usblink_queue_action &action)
{
    usblink_queue.push(action);

    if(!usblink_connected)
        usblink_connect();
}

void usblink_queue_delete(std::string path, bool is_dir, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = is_dir ? usblink_queue_action::DEL_DIR : usblink_queue_action::DEL_FILE;
    action.user_data = user_data;
    action.remote = path;
    action.progress_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_dirlist(std::string path, usblink_dirlist_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::DIRLIST;
    action.user_data = user_data;
    action.remote = path;
    action.dirlist_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_download(std::string path, std::string destpath, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::GET_FILE;
    action.user_data = user_data;
    action.local = destpath;
    action.remote = path;
    action.progress_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_put_file(std::string local, std::string remote, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::PUT_FILE;
    action.user_data = user_data;
    action.local = local;
    action.remote = remote;
    action.progress_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_send_os(std::string filepath, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::SEND_OS;
    action.user_data = user_data;
    action.local = filepath;
    action.progress_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_new_dir(std::string path, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::NEW_DIR;
    action.user_data = user_data;
    action.remote = path;
    action.progress_callback = callback,

    usblink_queue_add(action);
}

void usblink_queue_move(std::string old_path, std::string new_path, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::MOVE;
    action.user_data = user_data;
    action.local = old_path;
    action.remote = new_path;
    action.progress_callback = callback,

    usblink_queue_add(action);
}

void usblink_queue_raw(uint16_t sid, std::vector<uint8_t> payload, usblink_raw_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::RAW;
    action.user_data = user_data;
    action.sid = sid;
    action.payload = std::move(payload);
    action.raw_callback = callback;

    usblink_queue_add(action);
}

void usblink_queue_key(uint32_t key, usblink_progress_cb callback, void *user_data)
{
    usblink_queue_action action;
    action.action = usblink_queue_action::KEY;
    action.key = key;
    action.user_data = user_data;
    action.progress_callback = callback;
    usblink_queue_add(action);
}

unsigned int usblink_queue_size()
{
    return usblink_queue.size();
}
