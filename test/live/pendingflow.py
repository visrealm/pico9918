#!/usr/bin/env python3
"""The display-change confirmation flow over the host bus, the way the configurator drives it.

    python pendingflow.py --port COM10

Saves a SCART mode change, which goes to the pending block rather than the main
config, then walks it through a host reset, a power cycle, a second host reset
and the configurator's confirm, reading the pending state and the field back after
each. A host reset at either stage must keep the change and its state: the
configurator only offers to confirm while it reads ARMED. The board is put back the
same way afterwards, so a run leaves the stored settings as it found them.

Needs `switch` power, since the flow depends on a real power cycle.
"""

import argparse
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import probe as probe_module
from busflash import config, identify, TIMEOUT_S
from probe import spec

SCART_MODE = 11
PENDING_STATE = 200
PENDING_SCART_MODE = 203
PENDING_CONFIRM = 254
SAVE_TO_FLASH = 255
STATE = {0xC0: "CONFIRMED", 0x9E: "PENDING", 0xA0: "ARMED"}


def set_config(p, index, value):
    p.register(58, index)
    p.register(59, value)


def command(p, key):
    """Raise a config action and wait for the GPU loop to take it and the flash write to land."""
    set_config(p, key, 1)
    deadline = time.time() + TIMEOUT_S
    while config(p, key):
        if time.time() > deadline:
            raise SystemExit("config action %d was never taken" % key)
    time.sleep(0.5)


def host_reset(p):
    p.command("RESET", 10)
    time.sleep(0.5)
    identify(p, wait_s=TIMEOUT_S)


def power_cycle(p, mode):
    p.off()
    time.sleep(0.5)
    p.arm(mode)
    identify(p, wait_s=TIMEOUT_S)


def check(p, label, want_state, want_mode, failures):
    state, mode, mirror = config(p, PENDING_STATE), config(p, SCART_MODE), config(p, PENDING_SCART_MODE)
    ok = state == want_state and mode == want_mode
    print("  %-34s state %-9s scart %d  mirror %d  %s"
          % (label, STATE.get(state, "%02x" % state), mode, mirror,
             "ok" if ok else "WANTED %s, scart %d" % (STATE[want_state], want_mode)))
    if not ok:
        failures.append(label)


def walk(p, mode, value, failures):
    set_config(p, SCART_MODE, value)
    command(p, SAVE_TO_FLASH)
    check(p, "saved", 0x9E, value, failures)
    host_reset(p)
    check(p, "host reset while PENDING", 0x9E, value, failures)
    power_cycle(p, mode)
    check(p, "power cycle", 0xA0, value, failures)
    host_reset(p)
    check(p, "host reset while ARMED", 0xA0, value, failures)
    command(p, PENDING_CONFIRM)
    check(p, "confirmed", 0xC0, value, failures)
    host_reset(p)
    check(p, "host reset after confirm", 0xC0, value, failures)
    power_cycle(p, mode)
    check(p, "power cycle after confirm", 0xC0, value, failures)


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    ap.add_argument("--port", default=None, metavar="PORT[:MODE]",
                    help="the probe's CDC port; otherwise LIVE9918_PROBE_CDC")
    args = ap.parse_args()
    where = spec(args.port)
    if not where:
        raise SystemExit("no probe port: pass --port COM10 or set LIVE9918_PROBE_CDC")
    failures = []
    with probe_module.opened(where) as p:
        if not p.state().get("switched"):
            raise SystemExit("the flow needs a power cycle, so the probe must hold switch power")
        identify(p)
        original = config(p, SCART_MODE)
        start = config(p, PENDING_STATE)
        if start != 0xC0:
            raise SystemExit("pending state reads %02x before the run, not CONFIRMED" % start)
        changed = 1 if original == 0 else 0
        print("SCART mode %d -> %d" % (original, changed))
        walk(p, where[1], changed, failures)
        print("SCART mode %d -> %d, restoring" % (changed, original))
        walk(p, where[1], original, failures)
    print("FAIL: " + ", ".join(failures) if failures else "PASS")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
