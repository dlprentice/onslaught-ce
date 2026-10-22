<!-- SPDX-License-Identifier: GPL-3.0-only -->

# port/d3dx: the D3DX 9 subset for the native port

Status: active (headers, maths, assembler and DXGetErrorString9 done; texture loader exact on 837 of 871 default-path cases, the rest sub-4x4 DXT mips)
Last updated: 2026-10-15
Summary: what the port's D3DX replacement covers, how it is checked against retail d3dx9, and what it leaves out.

Licence: GPL-3.0-only. Written for the port from the public D3DX 9 API; retail
`d3dx9.lib` (DirectX 9.0 SDK, pinned in the Onslaught lab) is used only as a black-box oracle, never copied.

## What the game calls

| Area | Functions | Notes |
|---|---|---|
| Maths | `D3DXMatrixMultiply`, `Transpose`, `Inverse`, `Translation`, `D3DXVec3Transform`, `Vec3TransformCoord`, `Vec4Transform`, `Vec3Project`; inline `D3DXMatrixIdentity` and the vector/matrix class operators | SDK layouts; scalar division is one reciprocal then multiplies, as the SDK's inline operators do |
| Shaders | `D3DXAssembleShader` (returns `ID3DXBuffer`) | DX8-style `vs.1.0`/`vs.1.1` text from `.aya` resources, rewritten at runtime by `CVertexShader::CompileShader`, plus `ps_2_0` (water, particles, landscape) |
| Textures | `D3DXCreateTextureFromFileInMemoryEx`, `D3DXCreateTextureFromFileExA` | zlib-inflated DDS from the resources (DXTexture.cpp); point filter, optional smaller size, format conversion |
| Errors | `DXGetErrorString9A` | DxErr9 shipped beside d3dx; the game never asks for descriptions |
| Not implemented | `D3DXSaveSurfaceToFileA`, `D3DXLoadSurfaceFromFileA` | only the debug screen dump (DebugLog.cpp) calls them; they return `D3DERR_NOTAVAILABLE` |

## How it is checked

Each function is compared bit for bit with retail d3dx9: a small Windows harness linked against the pinned
`d3dx9.lib` with the pinned VC6 toolchain runs under Wine in its own prefix (`BEA_D3DX_WINEPREFIX`, no display),
and a native i686 driver runs the port's code on the same inputs. Maths uses fixed and edge inputs; the assembler uses
every shader text in the data and the sources; textures use a fake system-memory `IDirect3DDevice9`, so real D3DX runs
headless. Retail inputs and harness outputs stay in the ignored `local-data/`.

Headers: `tests/check_headers.sh` compiles each header alone and `tests/headers_compile.cpp` under the port's x87-class
flags for both targets.

## Maths

Bit-exact with retail on this machine (13th-gen Intel): 0 of 90,000 records differ on Linux and on Windows (the mingw
build run under Wine), at both 24- and 53-bit x87 precision, over 15 operations (aliased and in-place forms included) and
four input classes: game-like transforms and projections, uniform values, the whole float exponent range, and edges
(zeros, denormals, infinities, NaNs with distinct signs and payloads, signalling NaNs). The order was fitted on two seeds
and held on a third that played no part in fitting.

Retail runs its SSE implementations here, so `maths.cpp` is compiled with SSE (`flags.txt`); what it reproduces:

- Multiply: rows 0 and 3 sum pairwise, `(p0+p1)+(p2+p3)`, rows 1 and 2 left to right; row 3 multiplies with its
  operands the other way round. x86 returns the first operand's NaN, so operand order shows in NaN payloads; the
  scalar operations are inline `addss`/`subss`/`mulss`, because the compiler may commute C operators and intrinsics.
- Vec3Transform, Vec4Transform, Vec3TransformCoord: pairwise sums of matrix-times-component products;
  TransformCoord divides by `w` as `r = rcpss(w); r = (r+r) - w*r*r; x*r`.
- Inverse: Cramer's rule in the evaluation order of Intel's SSE inverse (application note AP-928) with the same
  refined `rcpss` reciprocal; a zero or NaN determinant returns NULL and writes neither the matrix nor the determinant.
- Transpose copies output elements 1, 4, 8, 9, 12, 13 and 14 through the x87 (a signalling NaN comes out quiet) and the
  rest as integers. Translation copies its arguments unchanged.
- Vec3Project (`maths_x87.cpp`, x87 class) multiplies `(world*view)*projection`, transforms with TransformCoord and
  applies the viewport on the x87, so its result follows the precision control as retail's does. It needs all three
  matrices, as every game call passes them; retail's behaviour with a NULL matrix was not measured.

Limits: `rcpss` is an approximation whose bits are the processor's, and Intel and AMD tables differ, so the port matches
retail run on the same processor; the measurement here is Intel only. Retail on a pre-SSE processor took other paths,
which nobody now plays on.

Run: `python3 tests/maths_cases.py local-data/port/d3dx/maths/input.bin 6000 3` writes inputs;
`tests/wine/oracle.py build`, then `tests/wine/oracle.py run maths pc24|pc53 Z:<input> Z:<output>` records retail into
`local-data/port/d3dx/maths/retail-pc24.bin` and `retail-pc53.bin`; `tests/run_maths.sh` (or `TARGET=windows`) builds the
port, runs it and compares (`tests/maths_compare.py` reports mismatches by operation and input class).

## Assembler

`D3DXAssembleShader` (`assembler.cpp`, with `ID3DXBuffer` in `buffer.cpp`) matches retail token for token, on Linux
and on Windows under Wine: same HRESULT, same code bytes, and a message buffer exactly when retail returns one.

| Set | Shaders | Match | Message text identical |
|---|---|---|---|
| corpus: the 44 vertex shaders in the retail resources (as stored, and as `CompileShader` rewrites them, which is what the game assembles), the 10 shader strings in `src/`, 367 written cases | 465 | 465 | 147 of 147 |
| holdout `--fuzz-valid` seeds 21, 22, 23 (mostly valid programs for each version) | 12,000 | 12,000 | all 1,032 |
| holdout `--fuzz` seeds 7, 8 (random programs, half mutated; nearly all fail) | 6,000 | 6,000 | 2,010 of 5,979 |

Seeds 22, 23 and 8 were generated after the last change and never looked at before they passed. Retail's rules, as
measured: newlines are whitespace to the grammar; mnemonics and registers are lower case; `;`, `//` and `/* */`
comments; `vs.1.1`/`vs_1_1`, `vs_1_0` with warning X2015, `vs_2_0`, `ps_1_0` to `ps_1_4`, `ps_2_0`; `sub` becomes `add`
with the second source negated in vs and ps_2_0 (ps_1_x keeps `sub`); vs scalar instructions read `.w` from an
unswizzled or `.xyzw` source; a full mask on a vs matrix destination becomes the matrix's rows; partial swizzles repeat
their last component; masks must be in order; vs constants take any index, `c[a0.x+n]` sets the relative bit;
`def` numbers are 32-bit integers or decimals with an optional `e`/`e-` exponent and `f`; there is no preprocessor
(`#` is a syntax error), and `pDefines`, `pInclude` and `Flags` are ignored, as every game call passes none.

Known differences, on invalid input only (HRESULT and code are exact everywhere): after a syntax error retail recovers
and goes on reporting later lines, where the port stops at the first; and in about one error-fuzz shader in ten the
first differing line comes before any syntax error, because retail chose or worded an earlier diagnostic differently
(X2000 against X2005, a register named `t7_bias` against `t7`, X2029 against X2000). Message text only reaches the
game's TRACE output, and every shader the game assembles is valid.

Diagnostic text: `assembler.cpp` reproduces 16 retail diagnostic format strings verbatim (for example
`syntax error : unexpected token '%s'`). They are kept as interoperability strings: short functional messages observed
from retail's black-box output by the oracle harness, as Wine's d3dcompiler does, and not copied from SDK files.

Run: `tests/asm_corpus.py` writes the corpus (`--fuzz-valid` or `--fuzz COUNT SEED` writes a holdout set);
`tests/wine/oracle.py run assemble Z:<list-wine.txt> Z:<retail.bin>` records retail; `tests/run_asm.sh` (`ASM_SET=` a
set's folder, `TARGET=windows`, `--messages`) builds, runs and compares.

## Textures

`D3DXCreateTextureFromFileInMemoryEx` and `D3DXCreateTextureFromFileExA` (`texture.cpp`) read DDS (DXT1 to DXT5,
uncompressed RGB and luminance) and TGA. They are compared with retail through a headless fake device
(`tests/fakedev.c`, C89, built by VC6 into the oracle and by clang into the port's driver, so both D3DX
implementations make their calls on the same device). Its caps and texture formats come from a profile:
`tests/profiles/dxvk.txt` is dxvk-native 3.1.1 on this machine (`tests/dxvk_caps.cpp`); `tex_corpus.py` derives a
pow2-only profile without DXT2/DXT4 and one without DXT. A case passes when the HRESULT, `D3DXIMAGE_INFO`, the
`CreateTexture` parameters and every level's bytes are equal.

Retail's DXT encoder runs on the x87, so its results follow the precision control; the game creates its device
without `D3DCREATE_FPU_PRESERVE`, so it loads textures at 24-bit precision (pc24). The oracle and the port's driver
both set the precision (`TEX_PC=pc24|pc53`); other sets ran at Wine's default, 53-bit.

| Set | Cases | Match |
|---|---|---|
| the game's own call (DXTexture.cpp LoadFrame: default size, the file's level count, `D3DFMT_UNKNOWN`, managed, point filter) for all 847 resource textures, plus `splash.tga` through both entry points with box and point mip filters, at pc24 | 871 | 837 |
| the same at pc53 | 871 | 797 |
| random 4x4 A8R8G8B8 blocks encoded to DXT1 (`tex_synth.py`), pc24 / pc53 | 4,000 | 3,805 / 3,869 |
| random DXT1, DXT3, DXT5 blocks decoded to A8R8G8B8 (`tex_synth.py --source`) | 1,800 | 1,800 |
| the same for DXT2 and DXT4 (premultiplied) | 1,200 | 1,029 |
| `tex_corpus.py --sweep` seed 1: other formats, sizes, level counts, pools, filters and the three profiles | 3,000 | 983 |

Measured, and reproduced:

- The resource files (`.aya`) are a series of u32-size-prefixed zlib chunks of up to 1 MB (`tests/aya.py`).
- Format choice: for each requested format retail ranks the formats it accepts and takes the device's best-ranked
  supported one. The table in `texture.cpp` was measured with `D3DXCheckTextureRequirements` (every requested format
  against every single and pair of supported formats, 41,871 probes, no ordering violation) and predicted 20,000
  random supported sets exactly. `D3DFMT_UNKNOWN` ranks as A8R8G8B8; float formats are never chosen.
- DDS levels of 4x4 and larger load as stored. X8R8G8B8 writes 0 into its unused byte.
- DXT decoding: endpoints as `c * (1/31)` and `c * (1/63)`; every DXT format's colour block follows DXT1's rule
  (c0 <= c1 is the 3-colour mode, index 3 black), DXT3 alpha `a * (1/15)`, DXT5 alpha 8- and 6-step ramps.
- Box mips: each level filters the previous level as stored, in float32, packing `v * max + 0.5`; for 8-bit channels
  that is `(sum + 2) >> 2` exactly. `texture.cpp` is SSE (`flags.txt`): on the x87 the same expressions round differently.

- DXT levels narrower or shorter than 4 pixels are not copied: retail decodes them, repeats the pixels across the
  block and re-encodes them. The encoder (`bc.cpp`) is adapted from DirectXTex's BC.cpp (MIT; Microsoft's open
  successor of D3DX's encoder, pinned by commit and full SHA-256 in the file), always dithering colour and alpha, weighting colour by
  luminance, with D3DX's sign in the alpha optimiser; DXT2 and DXT4 are premultiplied before encoding.

Not reproduced, and not pursued further:

- 34 of the game's textures still differ at pc24, all in sub-4x4 DXT mips. In 18 the difference is one step of one
  endpoint (every DXT1 case among them): retail's unquantised endpoint lands a few ulps the other side of a half from
  DirectXTex's, in either direction (exact halves round up in some blocks and down in others); matching it needs
  retail's own floating-point order, which black-box measurement cannot pin down. The other 16 are DXT2 blocks whose
  colour endpoints differ by 5 to more than 15 steps (for example -25/-50/-25) with alpha identical: most likely the
  premultiplied-colour round trip below (retail's un-premultiply and its re-premultiply before encoding), not the
  encoder. The levels are 2x2 and 1x1; neither item is pursued further.
- DXT2 and DXT4 decoding: retail un-premultiplies and rounds exact halves down where every float model tried rounds
  up (54 to 90 channels in 460,800).
- Resampling to other sizes (the texture-resolution loss shift), linear and triangle filters, conversions into 16-bit
  formats with dithering, and some HRESULTs in the sweep; none is on the default path.

## DXGetErrorString9

`DXGetErrorString9A` (`dxerr.cpp`) returns retail DxErr9's name for 262,159 of 262,159 codes on both targets: every
code in facilities 0x876 (both severities) and 7 (0x8007xxxx), all of 0x8004xxxx and the generic COM codes, as listed
by `tests/errstr_table.py codes`. `dxerr_names.inc` (2,349 names) is
generated by `tests/errstr_table.py` from the oracle's output; other codes are "Unknown", as in retail. The names are
public identifiers from the SDK and Windows headers, observed from DxErr9's black-box output as interoperability
strings; DxErr9's descriptions are not carried, since the game never asks for them.
