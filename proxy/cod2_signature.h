// CoD2SP_s.exe 1.3 recognised by its code, for copies whose PE timestamp is not 0x442ca3f7 (the DVD copy's exe has
// "DEV\0" there: the same 1.3 code and data, only the header and some padding between functions rewritten). Bytes at
// the places the mod hooks or patches, the entry point and two strings, taken from the supported exe and checked
// against the DVD one (2026-11-06). Used by the proxy (in memory, before any patch) and the installer (in the file).
#pragma once

typedef struct { unsigned int va; const char *bytes; } cod2_sig_t;

#define COD2SIG_LEN 16

static const cod2_sig_t g_cod2Sig[] = {
    { 0x00555b43, "\x6a\x60\x68\x88\xa6\x56\x00\xe8\x45\x29\x00\x00\xbf\x94\x00\x00" },   // entry point
    { 0x0040da80, "\xca\x44\x00\xc7\x84\x24\xec\x01\x00\x00\xd0\xca\x44\x00\xc7\x84" },   // CL_InitRef
    { 0x0040fb00, "\x56\xff\x15\x70\x7b\x61\x00\xff\x15\xf8\x7a\x61\x00\x8b\x35\x58" },   // SCR_UpdateScreen
    { 0x004bfbf0, "\xd9\x05\xc8\x91\xf7\x00\xa1\xdc\x08\xf7\x00\xd9\x05\x00\x1e\x59" },   // CG_DrawActive
    { 0x0041afc0, "\x53\x56\x57\x8b\xf0\x8d\x50\x01\x8a\x08\x40\x84\xc9\x75\xf9\x2b" },   // Cbuf_AddText
    { 0x004500d0, "\x8b\x0d\xec\x6f\x82\x00\x8b\x15\x34\x8a\x82\x00\x56\x57\x8b\xf8" },   // Sys_QueEvent
    { 0x004096d0, "\xf6\x05\x04\x7f\x61\x00\x08\x74\x05\xe9\x92\xcd\x0b\x00\x8b\x0d" },   // CL_MouseEvent
    { 0x0042e7e0, "\x75\x67\xa1\xfc\x7e\x74\x00\x80\x78\x06\x07\x8b\xe8\x75\x25\x68" },   // intro check
    { 0x004af65b, "\xe8\x90\xfb\xff\xff\xe8\xdb\x1e\x00\x00\xe8\x56\xf2\xff\xff\x5f" },   // reload camera call
    { 0x00403070, "\xa0\xd0\xa0\x59\x00\x84\xc0\x74\x28\x8b\x0d\x1c\x4c\x5a\x00\x85" },   // cinematic stop
    { 0x00575ce4, "\x75\x6e\x73\x6b\x69\x70\x70\x61\x62\x6c\x65\x63\x69\x6e\x65\x6d" },   // "unskippablecinematic legal"
    { 0x0057a158, "\x53\x79\x73\x5f\x51\x75\x65\x45\x76\x65\x6e\x74\x3a\x20\x6f\x76" },   // "Sys_QueEvent: overflow"
};
