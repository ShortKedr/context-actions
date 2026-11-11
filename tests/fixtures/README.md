# Archive repacking test fixtures

These fixtures contain project-created test data and two samples from libarchive.

| Fixture | Contents |
| --- | --- |
| `stored-rar4.rar` | Stored RAR4 archive with `folder/data.bin`, `empty.txt` and two directory entries |
| `stored-rar5.rar` | Same entries in the [RAR5 format](https://www.rarlab.com/technote.htm) |
| `compressed-rar4.rar` | One compressed text file adapted from libarchive's RAR4 fixture |
| `solid-rar4.rar` | Same entries as the stored RAR4 fixture, with the solid flags set. Bundled libarchive rejects this RAR4 variant |
| `solid-rar5.rar` | Four compressed data files from libarchive's solid RAR5 fixture |
| `incomplete.part1.rar` | First RAR5 volume with a stored entry flagged as continuing in a second volume |
| `encrypted.zip` | ZIP AES-256 fixture created with 7-Zip, with the password `fixture-pass` |
| `encrypted.7z` | 7z fixture with encrypted headers, with the same test password |
| `deflate.zip` | Deflate ZIP created with Python's zipfile |
| `lzma2.7z` | LZMA2 7z created with 7-Zip, with the same contents as the Deflate ZIP |

The stored and incomplete RAR files use small constructed headers and their own checksums. The payload in both stored RAR archives is the 15-byte sequence `RAR fixture\x00\xff\r\n`. All complete archives were checked with 7-Zip, which also reports the missing second volume for the incomplete RAR.

The compressed RAR4 and solid RAR5 fixtures come from the libarchive 3.8.11 test suite. The former retains only the `testdir/test.txt` entry from `test_read_format_rar_compress_normal.rar.uu`. The latter is decoded from `test_read_format_rar5_multiple_files_solid.rar.uu`. Their license notices are in [LICENSE-libarchive.txt](LICENSE-libarchive.txt).
