# Controller serial link

The controller emits binary frames on UART at 115200 baud, 8 data bits, no
parity, one stop bit. The payload carries a short printable status message.
A receiver can start listening mid-round. It ignores boot text until it sees
the next frame marker and only displays a payload whose length and CRC pass.

## Read telemetry

From the project root, the prepared PlatformIO virtual environment includes
pySerial. Use the port shown by `pio device list` for your own board:

```sh
.venv/bin/python tools/monitor.py --port /dev/ttyUSB0
```

For a raw binary capture, Python's standard library is sufficient. Input on
stdin also works in a pipe:

```sh
python3 tools/monitor.py --file uart.bin
python3 tools/monitor.py < uart.bin
```

Each output line starts with the decimal packet sequence. Diagnostic messages
go to stderr. A CRC error, invalid length, malformed escape or truncated packet
makes the capture command exit with status 1, even if a later packet is valid.
A sequence discontinuity is reported with the expected and received numbers.
It may indicate dropped bytes or a controller restart. The counter wraps from
65535 to zero. Its scope is the serial stream; journal event sequence numbers
belong to the durable ledger.

## Wire format

| Field | Bytes | Encoding |
| --- | ---: | --- |
| Start | 1 | `7e` |
| Payload length | 1 | Unsigned, 0 through 96 |
| Sequence | 2 | Unsigned, least significant byte first |
| Payload | 0–96 | Exactly the number of bytes in the length field |
| CRC | 2 | Least significant byte first |

CRC-16/CCITT-FALSE covers the length, sequence and payload before escaping.
It starts at `ffff`, uses polynomial `1021`, processes the most significant
bit first and applies no final XOR. The check value for ASCII `123456789` is
`29b1`. The payload can contain any byte.

The encoder writes the start marker literally. In every subsequent field it
replaces `7e` with `7d 5e` and `7d` with `7d 5d`. This makes an unescaped
`7e` unambiguous even after a cable disconnect, bad length or partial write.
At most 203 wire bytes fit one frame. A receiver retains its partial frame
across reads; a new marker or the end of a capture detects a truncation.

For sequence `1234` in hex and ASCII `Hello`, the complete wire bytes are:

```text
7e 05 34 12 48 65 6c 6c 6f d9 bb
```

The [C++ codec](../core/src/serial.cpp) and [Python monitor](../tools/monitor.py)
use this packet in their regression tests. The monitor also escapes terminal
control bytes when displaying binary payloads.

```sh
pio test -e native -f test_serial
python3 tools/test_monitor.py
```
