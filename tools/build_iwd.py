"""Builds the optional data pack of the VR mod from the player's own copy of Call of Duty 2.

usage: python build_iwd.py "<game folder>" [out folder]

zz_cod2vr.iwd (goes to <game folder>\\main):
  * xmodel/viewmodel_hands*: the first person arms, with every material replaced by
    mtl_vr_invisible, so only the gun is drawn (the arms stay in the skeleton the gun hangs on).
  * materials/mtl_vr_invisible: a phong_alphatest material (made from hill400_treeline01) whose
    colour map is images/vr_invisible.iwi, a 4x4 DXT1 image with every texel transparent.
Only the arms' material lists and the two small files above are the mod's own; the rest of each
xmodel is copied unchanged from the game's files.
"""
import glob
import os
import struct
import sys
import zipfile

INVISIBLE = b"mtl_vr_invisible"
IMAGE = b"vr_invisible"


def find(main, name):
    # later packs override earlier ones, as in the game
    found = None
    for f in sorted(glob.glob(os.path.join(main, "iw_*.iwd"))):
        with zipfile.ZipFile(f) as z:
            if name in z.namelist():
                found = z.read(name)
    return found


def all_names(main, prefix):
    names = set()
    for f in sorted(glob.glob(os.path.join(main, "iw_*.iwd"))):
        with zipfile.ZipFile(f) as z:
            names.update(n for n in z.namelist() if n.startswith(prefix))
    return sorted(names)


def invisible_xmodel(data):
    """xmodel: ... ff ff ff ff 00 00 00 00, u16 material count, NUL-terminated names, rest."""
    i = data.find(b"\xff\xff\xff\xff\x00\x00\x00\x00")
    if i < 0:
        raise ValueError("material list not found")
    p = i + 8
    count = struct.unpack_from("<H", data, p)[0]
    q = p + 2
    for _ in range(count):
        q = data.index(b"\0", q) + 1
    return data[:p + 2] + (INVISIBLE + b"\0") * count + data[q:]


def invisible_material(template):
    """Same header as the template, strings rebuilt: name, image, technique, texture names."""
    def s(off):
        return template[off:template.index(b"\0", off)]
    u32 = lambda off: struct.unpack_from("<I", template, off)[0]
    tech_off, table_off, strings_start = u32(0x38), u32(0x3C), u32(0x40)
    count = u32(0x34)
    head = bytearray(template[:strings_start])
    out = bytearray()

    def add(b):
        off = strings_start + len(out)
        out.extend(b + b"\0")
        return off

    struct.pack_into("<I", head, 0x38, add(s(tech_off)))       # phong_alphatest
    struct.pack_into("<I", head, 0x00, add(INVISIBLE))          # material name
    struct.pack_into("<I", head, 0x04, add(IMAGE))              # reference image
    for k in range(count):
        e = table_off + k * 12
        sem = s(u32(e))
        struct.pack_into("<I", head, e, add(sem))
        img = IMAGE if sem == b"colorMap" else s(u32(e + 8))
        struct.pack_into("<I", head, e + 8, add(img))
    return bytes(head + out)


def invisible_image():
    # IWi v5, DXT1, no mipmaps, 4x4: color0 = color1 = 0 (3-colour mode), every index 3 = transparent
    header = b"IWi" + bytes([5, 0x0B, 0x03]) + struct.pack("<HHH", 4, 4, 1) + struct.pack("<4I", 36, 36, 36, 36)
    return header[:28] + b"\x00\x00\x00\x00\xff\xff\xff\xff"


def main():
    game = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "mods", "out")
    main_dir = os.path.join(game, "main")
    os.makedirs(out, exist_ok=True)
    template = find(main_dir, "materials/hill400_treeline01")
    if not template or b"phong_alphatest" not in template:
        sys.exit("the template material was not found in the game's files")
    path = os.path.join(out, "zz_cod2vr.iwd")
    with zipfile.ZipFile(path, "w", zipfile.ZIP_DEFLATED) as z:
        z.writestr("materials/mtl_vr_invisible", invisible_material(template))
        z.writestr("images/vr_invisible.iwi", invisible_image())
        for name in all_names(main_dir, "xmodel/viewmodel_hands"):
            z.writestr(name, invisible_xmodel(find(main_dir, name)))
            print("arms hidden:", name)
    print("written", path)


if __name__ == "__main__":
    main()
