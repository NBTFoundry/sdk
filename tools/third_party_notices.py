#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Prints the third-party notices an application built on this SDK carries:
the sections of legal/NOTICE.txt that name a component the SDK links,
without the SDK's own header, disclaimer and MPL-2.0 text (the application
has its own). An application's notice script appends them to its own.

    third_party_notices.py
"""
import os

RULE = "=" * 72
SDK = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def sections():
    with open(os.path.join(SDK, "legal/NOTICE.txt")) as f:
        parts = [s.strip("\n") for s in f.read().split(RULE)]
    # The MPL text spans several sections (its own rules): only those naming a component stay.
    return [s for s in parts if s.strip() and " used by " in s.splitlines()[0]]


def main():
    print(("\n\n" + RULE + "\n\n").join(sections()))


if __name__ == "__main__":
    main()
