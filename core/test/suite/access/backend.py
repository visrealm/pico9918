#!/usr/bin/env python3
"""Which VDP a standalone run of one of these tests drives.

The library's answer is the only one it can have: an in-process instance behind
the shim. A board is the firmware repository's business, and `runner.py --only
<stage>` there drives any one of these against hardware - so nothing is lost by
this being desktop-only, and the library gains a suite that runs anywhere.
"""

import argparse

from suite.access.desktop import Desktop


def backend_args(ap):
    """The arguments every standalone entry point here understands. The shim is
    located by LIVE9918_SHIM or the default build directory, so a caller that has
    just built one needs no path."""
    ap.add_argument("--shim", default=None,
                    help="path to live_shim (default: $LIVE9918_SHIM, else the "
                         "build tree beside this suite)")
    return ap


def open_backend(args=None):
    shim = getattr(args, "shim", None) if args else None
    return Desktop(shim) if shim else Desktop()


def main_args():
    ap = argparse.ArgumentParser()
    backend_args(ap)
    return ap.parse_args()
