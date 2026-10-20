# The DX machine's built-in cartridges

The DX machine's built-in bank (`dx_cart`, 63 voices, made by
`tools/gen_dx_tables.py` into `src/dx_tables.inc`) starts with these two
cartridges, kept here as 4096-byte voice banks: the data of a DX7 32-voice
bulk dump (packed 128-byte voices, VMEM), without its SysEx header and
checksum.

| file | voices in the bank | what it is |
|---|---|---|
| `Legowelt_magic_DX7.vmem` | 1-32 | Legowelt's "Magic DX7" cartridge (Tooropulse ... Swissynth) |
| `Richard_DX9.vmem` | 33-52 | a DX9 cartridge, voices DX9. 1 to DX9.20 (21 to 32 of the file repeat DX9.20 and are left out) |

Both are free patch sets, given out for anyone to use without attribution;
the owner of this fork supplied them and confirmed that. They are credited
here anyway. Voices 53-63 are this project's own (INIT VOICE first; see
`tools/gen_dx_tables.py`).

To use other cartridges, replace a file here with the 4096 voice bytes of
another bulk dump (bytes 6..4101 of a 4104-byte `.syx`, masked to 7 bits),
adjust `BANK_CARTS` in the generator (and `DX_BANKV` in `src/dx.inc` if the
count changes), and run `python3 tools/gen_dx_tables.py`.
