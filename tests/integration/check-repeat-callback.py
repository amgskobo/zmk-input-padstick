# SPDX-License-Identifier: MIT
"""Require raw repeat acknowledgement before mutating ZMK input listeners."""
from pathlib import Path
import re, sys
symbols={name:int(address,16) for address,name in re.findall(r'^([0-9a-fA-F]+)\s+\w\s+(\S+)$',Path(sys.argv[1]).read_text(),re.M)}
observer=symbols['_input_callback__a_padstick_repeat_ack']
listeners=[address for name,address in symbols.items() if name.startswith('_input_callback__input_handler_')]
assert listeners and all(observer < address for address in listeners), 'raw repeat observer must precede ZMK listeners'
print('PASS: linked raw repeat observer precedes every ZMK input listener')
