This project randomizes an NTSC Lagoon ROM. It's a LoROM game. There's a legally dumped Lagoon (USA).smc file, including header, present on my local harddrive (not in git).
main.cpp checks for a header and if so, removes the first 0x200 bytes. Any addresses listed in main.cpp assume there is no header and this should not be changed. It also contains a lot of commented out code due to experimentation, keep that code there.

Place any major findings in the reverse-engineering-docs directory

The CDL is Mesen2 format: 9-byte header ("CDLv2" + ROM CRC), then 1 flag byte per ROM byte. Crucially, bit 0 = Code and bit 5 (0x20) = MemoryMode8 (M flag), bit 4 (0x10) = IndexMode8 (X flag) — that gives the accumulator/index widths needed to disassemble accurately.
