# dmimg_jpeg API Reference

dmimg_jpeg has no API of its own: it implements the dmimg DIF, and programs
use it through dmimg (`dmimg_open()`, `dmimg_open_file()` - see dmimg's
api-reference.md).

| DIF function | Behavior |
|--------------|----------|
| `_probe` | The start of image marker and a marker after it (`FF D8 FF`) |
| `_open` | Reads the headers up to the first scan: width, height, `alpha` false, `scales` - 1/1 ... 1/8, each at which the image is at least a pixel. `-ENOTSUP` for progressive or arithmetic-coded JPEG and other variants TJpgDec does not have, `-EBADMSG` for damaged or truncated headers, `-EIO`, `-ENOMEM` |
| `_decode` | One block per MCU (8x8, 16x8, 16x16 pixels at 1/1; smaller at a scale), left to right, top to bottom. At a scale the size is not a multiple of, the block at the right / bottom end is one pixel wider / taller, repeating its last column / row. `-EBADMSG` for damaged or truncated data, `-EINVAL` for a scale not offered, `-EIO` |
| `_close` | Releases the decoder |

## Memory

One allocation of about 5.5 KiB per open image: TJpgDec's work area (4 KiB)
and one MCU of 0xAARRGGBB pixels (17 x 17). Nothing depends on the image's
size.
