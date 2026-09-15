#!/usr/bin/env python3
"""One long-lived firebird-headless process, driven over its stdin debugger.

Firebird's headless build reads debugger commands with fgets on stdin (firebird/headless/main.cpp,
gui_debugger_request_input) and writes everything back on stdout. There is no prompt string to
synchronise on and no framing, so this reads until the emulator goes quiet rather than until it
matches a pattern. Quiet is the only signal the protocol actually offers, and a pattern guessed from
the source would break the first time a command printed something unexpected.

The emulator is not always listening. It runs freely and only reads stdin once it is inside the
debugger, which happens on --debug-on-start, on a breakpoint, or on a warning with --debug-on-warn.
A command written while it is running sits in the pipe until it next stops. That is a property of
the emulator, not a bug here, but it means a caller who has issued `c` should not expect the next
command to be answered until something stops execution again.
"""
from __future__ import annotations

import os
import queue
import signal
import subprocess
import threading
import time

# How long output has to stay silent before a command counts as finished. Firebird prints in bursts
# while a command runs, so this is a gap between bursts rather than a total budget. Only used for
# commands that resume execution, which never come back to a prompt; everything else waits for READY.
QUIET_SECONDS = 0.35

# Printed by headless/main.cpp every time the debugger is about to wait for a command. Reading up to
# this is exact, where reading up to a gap in the output is a guess that cannot tell a command which
# printed nothing from one whose output has not arrived yet.
READY = "<<fb-ready>>"

# Printed by headless just before a command runs. Entering the debugger prints a READY of its own
# before the command is delivered, so READY alone is not a start marker: reading up to it would stop
# on that prompt and report the command as having printed nothing.
ECHO = "<<fb-cmd>>"



class Session:
    def __init__(self, binary: str, boot1: str, flash: str, extra: list | None = None):
        self.binary = binary
        self.boot1 = boot1
        self.flash = flash
        self.argv = [binary, "--boot1", boot1, "--flash", flash] + list(extra or [])
        self.proc: subprocess.Popen | None = None
        self.started_at = 0.0
        self._out: queue.Queue = queue.Queue()
        self._reader: threading.Thread | None = None
        self._transcript: list = []
        # send() writes stdin and then drains the shared output queue, so two callers interleaving
        # would attribute each other's output. The background linker and a foreground dbg call are
        # exactly that pair.
        self._io_lock = threading.RLock()
        self._pump_error: str | None = None

    # --- lifecycle ---

    def start(self, on_spawn=None) -> str:
        with self._io_lock:
            return self._start(on_spawn)

    def _start(self, on_spawn) -> str:
        if self.alive():
            raise RuntimeError("a session is already running, stop it first")
        if self.proc is not None:
            self.stop()
        # errors="replace" is load bearing. The debugger's `d` prints the raw bytes of memory in its
        # ASCII column, and the OS emits its own non-UTF-8 bytes, so strict decoding raises inside
        # the reader thread. That killed the thread and the session went blind for the rest of its
        # life: commands still ran, files they wrote still appeared, and nothing was ever printed
        # again. It reads exactly like the emulator ignoring you.
        self.proc = subprocess.Popen(
            self.argv, stdin=subprocess.PIPE, stdout=subprocess.PIPE,
            stderr=subprocess.STDOUT, text=True, bufsize=1,
            encoding="utf-8", errors="replace",
            start_new_session=True)
        self.started_at = time.monotonic()
        self._pump_error = None
        try:
            if on_spawn is not None:
                on_spawn(self.proc)
            self._out = queue.Queue()
            self._reader = threading.Thread(target=self._pump, daemon=True)
            self._reader.start()
            return self.read(timeout=5.0, budget=5.0)
        except BaseException:
            self.stop()
            raise

    def _pump(self) -> None:
        """Move stdout into a queue so reads can time out.

        A pipe read blocks with no timeout, and the emulator may legitimately print nothing for
        minutes while it runs. Without this thread a single silent command would hang the server for
        the rest of the session."""
        proc = self.proc
        if proc is None or proc.stdout is None:
            return
        try:
            for line in proc.stdout:
                self._out.put(line)
        except Exception as exc:
            # Never die quietly. A reader that stops without saying so is indistinguishable from an
            # emulator that stopped talking, and that cost most of a session to find once already.
            self._pump_error = repr(exc)
            self._out.put("\n[output reader died: {}]\n".format(exc))
        self._out.put(None)

    def alive(self) -> bool:
        return self.proc is not None and self.proc.poll() is None

    def stop(self) -> str:
        with self._io_lock:
            if self.proc is None:
                return "no session was running"
            proc = self.proc
            already_exited = proc.poll() is not None
            failure = None
            for sig in (signal.SIGTERM, signal.SIGKILL):
                if proc.poll() is not None:
                    break
                try:
                    os.killpg(proc.pid, sig)
                except OSError as exc:
                    failure = exc
                try:
                    proc.wait(timeout=5)
                except (subprocess.TimeoutExpired, OSError) as exc:
                    failure = exc
            if proc.poll() is None:
                raise RuntimeError("emulator process {} did not exit. Session retained: {}".format(proc.pid, failure))
            if self._reader is not None and self._reader.ident is not None:
                self._reader.join(timeout=5)
                if self._reader.is_alive():
                    raise RuntimeError("emulator exited but its output reader did not stop. Session retained")
            for stream in (proc.stdin, proc.stdout):
                if stream is not None:
                    try:
                        stream.close()
                    except OSError:
                        pass
            self._reader = None
            self.proc = None
            if already_exited:
                return "session had already exited with code {}".format(proc.returncode)
            return "session stopped after {:.1f}s".format(time.monotonic() - self.started_at)

    # --- io ---

    def read(self, timeout: float = QUIET_SECONDS, quiet: float = QUIET_SECONDS,
             budget: float | None = None) -> str:
        """Everything printed until the emulator has been silent for `quiet` seconds.

        `timeout` is how long to wait for the FIRST line. A command that produces nothing at all
        returns empty rather than blocking for the whole quiet window repeatedly.

        `budget` caps the total wait. Without it a burst that never pauses for `quiet` extends the
        deadline on every line and this does not return at all, which is what a booting OS produces:
        it prints continuously for the better part of a minute."""
        chunks: list = []
        deadline = time.monotonic() + timeout
        hard_stop = time.monotonic() + budget if budget is not None else None
        while True:
            remaining = deadline - time.monotonic()
            if hard_stop is not None:
                remaining = min(remaining, hard_stop - time.monotonic())
            if remaining <= 0:
                break
            try:
                line = self._out.get(timeout=remaining)
            except queue.Empty:
                break
            if line is None:
                chunks.append("\n[emulator exited]\n")
                break
            chunks.append(line)
            deadline = time.monotonic() + quiet
        text = "".join(chunks)
        if text:
            self._transcript.append(text)
            del self._transcript[:-200]
        return text

    def drain(self) -> str:
        """Take whatever is already queued without waiting.

        A prompt the emulator printed before this command was written is not this command's output,
        and leaving it in the queue makes the next read stop on a stale READY."""
        chunks: list = []
        while True:
            try:
                line = self._out.get_nowait()
            except queue.Empty:
                break
            if line is None:
                break
            if line.strip() == READY or line.startswith(ECHO):
                continue  # protocol, not output
            chunks.append(line)
        text = "".join(chunks)
        if text:
            self._transcript.append(text)
            del self._transcript[:-200]
        return text

    def read_until_ready(self, timeout: float = 10.0, quiet: float = QUIET_SECONDS,
                         budget: float | None = None) -> tuple:
        """Read one command's output, from its echo to the next ready marker.

        Returns (text, saw_ready). Anything before the echo belongs to whatever ran previously and
        is kept separately. saw_ready False means the command never came back to a prompt, either
        because it resumed the guest or because something went wrong, and the text is whatever the
        quiet gap caught."""
        before: list = []
        chunks: list = []
        started = False
        deadline = time.monotonic() + timeout
        hard_stop = time.monotonic() + budget if budget is not None else None
        while True:
            remaining = deadline - time.monotonic()
            if hard_stop is not None:
                remaining = min(remaining, hard_stop - time.monotonic())
            if remaining <= 0:
                break
            try:
                line = self._out.get(timeout=remaining)
            except queue.Empty:
                break
            if line is None:
                chunks.append("\n[emulator exited]\n")
                break
            if line.startswith(ECHO):
                started = True
                deadline = time.monotonic() + quiet
                continue
            if line.strip() == READY:
                if started:
                    return self._keep(chunks), True
                continue  # the prompt printed on the way in, before our command was delivered
            (chunks if started else before).append(line)
            deadline = time.monotonic() + quiet
        # No echo means the command never reached the debugger, so nothing read belongs to it.
        return self._keep(chunks if started else before), False

    def _keep(self, chunks: list) -> str:
        text = "".join(chunks)
        if text:
            self._transcript.append(text)
            del self._transcript[:-200]
        return text

    def send(self, commands: list, timeout: float = 10.0, budget: float | None = None) -> str:
        """Write debugger commands and return what came back.

        Commands go one per write with a read between them, because Firebird answers each on its own
        and batching them makes a failure impossible to attribute to a line."""
        with self._io_lock:
            if not self.alive():
                raise RuntimeError("no emulator session is running")
            stdin = self.proc.stdin
            if stdin is None:
                raise RuntimeError("session has no stdin")
            out = []
            for cmd in commands:
                pending = self.drain()
                try:
                    stdin.write(cmd.rstrip("\n") + "\n")
                    stdin.flush()
                except (BrokenPipeError, OSError) as exc:
                    raise RuntimeError("emulator closed its input: {}".format(exc)) from exc

                # A resuming command never comes back to a prompt, so no READY follows it and this
                # falls back on the quiet gap, which is what the old read did for every command.
                text, _ = self.read_until_ready(timeout=timeout, budget=budget)
                if pending:
                    # Left over from something earlier, so say so rather than letting it read as
                    # this command's answer.
                    out.append("[carried over from a previous command]\n{}".format(pending))
                out.append("> {}\n{}".format(cmd, text))
            return "\n".join(out)

    def transcript(self, lines: int = 80) -> str:
        text = "".join(self._transcript)
        kept = text.splitlines()[-lines:]
        return "\n".join(kept)
