#!/usr/bin/env python3
"""Demo application for the eBPF Linux Day talk.

It looks like it hangs at startup because the configuration file does not exist.
Run it as an ordinary user. Creating config.json next to this script lets it finish.
"""

import json
import os
import sys
import time

RETRY_SECONDS = 1.0
CONFIG_PATH = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.json")


def try_read_config():
    """Return the parsed configuration, or None if it is not available yet."""
    try:
        with open(CONFIG_PATH, encoding="utf-8") as handle:
            return json.load(handle)
    except FileNotFoundError:
        return None
    except json.JSONDecodeError:
        # The file exists but may still be half-written; treat it as not ready.
        return None


def main():
    print("Starting…", flush=True)
    print(f"PID: {os.getpid()}", flush=True)

    attempt = 0
    while True:
        config = try_read_config()
        if config is not None:
            break
        attempt += 1
        print(f"Still starting… (attempt {attempt})", flush=True)
        time.sleep(RETRY_SECONDS)

    print("Ready!", flush=True)
    print(json.dumps(config, indent=2), flush=True)
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except KeyboardInterrupt:
        print("\nInterrupted.", flush=True)
        sys.exit(130)
