#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-3.0-only
"""Exact named native load/script/pre-run guards and automatic-script refusal joined to the real abort bridge."""
import sys
from network_session_tests import main
CASES=('caller-offline','caller-normal','caller-afterload-refuse','caller-afterload-native-close',
       'caller-script-refuse','caller-script-native-close','caller-prerun-refuse','caller-prerun-native-close')
if __name__=='__main__':
    sys.argv.append('--callers')
    raise SystemExit(main())
