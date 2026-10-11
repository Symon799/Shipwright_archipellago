import importlib.util
import struct
import unittest
import zlib
from pathlib import Path

spec = importlib.util.spec_from_file_location(
    "exporter", Path(__file__).resolve().parents[1] / "export_map_tracker_references.py")
exporter = importlib.util.module_from_spec(spec)
spec.loader.exec_module(exporter)


class ReferenceExportTests(unittest.TestCase):
    def texture(self, endian="<", version=0):
        offset = 76 if version == 0 else 88
        data = bytearray(offset + 5)
        data[0] = int(endian == ">")
        struct.pack_into(endian + "I", data, 8, version)
        struct.pack_into(endian + "III", data, 64, 7, 2, 1)
        struct.pack_into(endian + "I", data, offset, 1)
        data[-1] = 0xF0
        return bytes(data)

    def test_native_ia4_and_byte_orders(self):
        for endian in ("<", ">"):
            for version in (0, 1):
                self.assertEqual(exporter.decode_texture(self.texture(endian, version)),
                                 (2, 1, bytes((8, 255, 255, 8, 16, 20))))

    def test_truncated_texture(self):
        with self.assertRaises(ValueError):
            exporter.decode_texture(self.texture()[:-1])

    def test_png_dimensions_pixels_and_checksums(self):
        rgb = bytes((8, 255, 255, 8, 16, 20))
        data = exporter.png_bytes(2, 1, rgb)
        self.assertEqual(data[:8], b"\x89PNG\r\n\x1a\n")
        offset = 8
        while offset < len(data):
            size = struct.unpack_from(">I", data, offset)[0]
            kind = data[offset + 4:offset + 8]
            payload = data[offset + 8:offset + 8 + size]
            checksum = struct.unpack_from(">I", data, offset + 8 + size)[0]
            self.assertEqual(checksum, zlib.crc32(kind + payload))
            if kind == b"IHDR":
                self.assertEqual(struct.unpack_from(">II", payload), (2, 1))
            elif kind == b"IDAT":
                self.assertEqual(zlib.decompress(payload), b"\0" + rgb)
            offset += size + 12


if __name__ == "__main__":
    unittest.main()
