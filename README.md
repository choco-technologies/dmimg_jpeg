# dmimg_jpeg

[![License](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
[![CI](https://github.com/choco-technologies/dmimg_jpeg/actions/workflows/ci.yml/badge.svg)](https://github.com/choco-technologies/dmimg_jpeg/actions/workflows/ci.yml)

The JPEG decoder of [dmimg](https://github.com/choco-technologies/dmimg).

## Description

A dmimg decoder plugin: once it is enabled - or when `dmimg_open_file()`
meets a `.jpg` / `.jpeg` / `.jpe` / `.jfif` file and loads it by the name -
every program that reads images through dmimg reads JPEG files.

- **Baseline JPEG**: gray or YCbCr, any chroma subsampling (4:4:4, 4:2:2,
  4:2:0). Progressive and arithmetic-coded JPEG are not supported
  (`-ENOTSUP`).
- **Decoded smaller in the IDCT**: at 1/2, 1/4 or 1/8 of the size
  (`info.scales`) - a 2400x1360 photo for a 480x272 screen is decoded at
  1/4, in a fraction of the time and memory of decoding it whole
  (todmvi asks for it by itself). A scale at which the image would be
  smaller than a pixel is not offered.
- **Small**: one MCU (8x8 ... 16x16 pixels) at a time; the decoder keeps a
  work area of 4 KiB and one MCU of pixels - about 5.5 KiB - never the
  image. Input is read in pieces of 512 bytes.
- TJpgDec rounds a scaled size down, dmimg up (`DMIMG_SCALED`): at a scale
  the size is not a multiple of, the last column / row repeats the one next
  to it.
- Pixels are opaque (`info.alpha` is false).

Built on [TJpgDec](http://elm-chan.org/fsw/tjpgd/) R0.03 by ChaN - see
[third_party/tjpgd](third_party/tjpgd).

## Usage

```c
#include "dmimg.h"

dmimg_info_t info;
dmimg_t image = dmimg_open_file("/sd/photo.jpg", &info, NULL);       /* loads dmimg_jpeg */
if (image != NULL)
{
    uint8_t scale = (info.scales & DMIMG_SCALE(2)) ? 2 : 0;         /* 1/4 */
    dmimg_decode(image, scale, put_block, ctx);
    dmimg_close(image);
}
```

## Building

### Using CMake

```bash
mkdir -p build
cd build
cmake ..
cmake --build .
```

Pass `-DDMOD_DIR=/path/to/local/dmod` to build against a local dmod checkout
instead of fetching `develop` from GitHub.

### Using Make

```bash
make DMOD_MODE=DMOD_MODULE DMOD_DIR=/path/to/dmod
```

## Testing

The tests decode the JPEG files in [tests/fixtures](tests/fixtures) - made
by `tests/fixtures/make_fixtures.py` (4:2:0, 4:4:4, gray, progressive, a
tiny one) - through dmimg, at every scale:

```bash
cd build
ctest --output-on-failure
```

## License

MIT - see [LICENSE](LICENSE); TJpgDec: [third_party/tjpgd/LICENSE](third_party/tjpgd/LICENSE).
