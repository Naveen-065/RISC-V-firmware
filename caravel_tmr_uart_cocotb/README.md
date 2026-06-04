# Three-Caravel TMR UART, PARTCL Systolic Compute, X1 Parity Storage

This directory contains a cocotb simulation for a reliable-AI data path built
around three replicated Caravel/PARTCL endpoints. The simulation models:

- three Caravel replicas,
- a majority voter on the external UART RX path into the replicas,
- a majority voter on the three Caravel UART TX paths back to the host,
- one X1/ReRAM-like memory map per replica,
- the real PARTCL `mat_mult_wb.v` systolic array RTL from
  `BMsemi/partcl_neuromorphic_compute`,
- a full-file Qwen safetensors parity experiment whose generated X1 records are
  written/read through the TMR UART path.

The X1 macro in the PARTCL repository is present as hard-macro collateral
(`.lef`, `.lib`, `.gds`) but not as synthesizable RTL. For that reason this
harness instantiates the real PARTCL `mat_mult_wb.v` RTL and keeps the X1 array
as a behavioral 32-bit word memory. If a synthesizable `Neuromorphic_X1_wb`
module is added later, it can replace the behavioral X1 memory behind the same
UART command path.

## Data Flow

```text
local PC / GPU runtime
  -> UART-like command stream
  -> TMR RX voter
  -> Caravel 0, Caravel 1, Caravel 2
       -> internal Wishbone firmware bridge
       -> real PARTCL mat_mult_wb systolic array
       -> C results copied into local X1/ReRAM words
       -> Qwen parity / correction records stored in X1 words
  -> three Caravel UART TX streams
  -> TMR TX voter
  -> external host RX
```

The systolic test sends a 2x2 signed matrix payload over UART. Inside each
Caravel replica the bridge writes that payload into the top-left corner of the
real 8x8 PARTCL matrix accelerator at `0x3100_0000`, zeroes the unused entries,
starts the accelerator, polls `STATUS[3]`, reads the real C-cache entries, and
copies them into X1 words:

```text
0x80 -> C00
0x81 -> C01
0x82 -> C10
0x83 -> C11
```

The external user then reads those X1 result words over the voted UART path.
One bad Caravel or one bad UART lane is corrected by majority vote. Two bad
Caravels form the wrong majority, which is the expected TMR limit.

## UART Protocol

UART is 8N1, LSB-first, idle-high.

Program an X1/ReRAM word:

```text
0xA5 0x50 <addr> <data[31:24]> <data[23:16]> <data[15:8]> <data[7:0]>
```

Read an X1/ReRAM word:

```text
0xA5 0x52 <addr>
```

Run a signed PARTCL systolic matrix multiply:

```text
0xA5 0x4D <A00> <A01> <A10> <A11> <B00> <B01> <B10> <B11>
```

Responses are always:

```text
0x5A <addr> <data[31:24]> <data[23:16]> <data[15:8]> <data[7:0]>
```

For matrix completion:

```text
0x5A 0xF0 0xA1 0x00 0x00 0x01
```

## Qwen X1 Parity Experiment

The local model used for the full-file run is:

```text
E:\hf_models\Qwen--Qwen3-0.6B\model.safetensors
```

This is the real Qwen3-0.6B safetensors shard, size `1,503,300,328` bytes. It is
not a toy tensor or smoke-test file.

The script `scripts/qwen_x1_ecc_experiment.py` builds a 2D parity image over the
full safetensors file. Each 4096-byte model block is treated as 64 rows x 512
bit-columns:

```text
64 row parity bits + 512 column parity bits = 576 bits = 72 bytes per block
```

That corrects one SEU bit per 4096-byte block. It detects, but does not correct,
multi-bit corruption in the same block. The generated parity file is the payload
that would be stored in X1. The cocotb test writes the manifest words plus
sample parity words into the modeled X1 over UART; streaming all 26.4 MB of
parity through the same UART protocol is linear but slow for RTL simulation.

Actual local run:

```text
skew Injected 64 SEU bits, corrected 64, 0 uncorrectable blocks, SHA-256 restored.
```

Result data:

```text
Manifest: caravel_tmr_uart_cocotb/data/qwen_x1_ecc_manifest.json
Parity bytes: 26,425,224
Parity bits: 211,401,792
Effective X1 capacity needed: 211.402 Mbit
Physical X1 capacity if triplicated across three Caravels: 634.205 Mbit
```

The manifest maps these correction facts into X1 words starting at `0xA0`:

```text
0xA0 model_size_low
0xA1 model_size_high
0xA2 parity_bits_low
0xA3 parity_bits_high
0xA4 block_count
0xA5 injected_bits
0xA6 corrected_bits
0xA7 uncorrectable_blocks
0xA8 original_sha32
0xA9 restored_sha32
0xAA status = 0x51A25601 when SHA is restored
```

The remote cocotb test writes these words and the first eight sample parity
words (`0xB0` to `0xB7`) through one faulted user RX lane, then reads them back
with one faulted Caravel TX lane. The TMR voters preserve the external UART
data.

## Capacity and Failure Limit

For this 2D parity design, the important X1 parameter is parity Mbit capacity:

| Model | Safetensors bytes | Blocks | Effective parity | TMR physical parity |
| --- | ---: | ---: | ---: | ---: |
| Qwen3-0.6B | 1,503,300,328 | 367,017 | 211.402 Mbit | 634.205 Mbit |
| Qwen3-1.7B | 4,063,515,592 | 992,071 | 571.433 Mbit | 1,714.299 Mbit |

The 64-bit injected demo used unique blocks, so every flipped bit was corrected.
With random SEUs, the first uncorrectable event happens when two SEUs land in
the same 4096-byte block. For the Qwen3-0.6B run:

```text
Deterministic seed 20260604 first uncorrectable: 120 injected bits
Duplicate block: 122154
Previous hit: bit 59
Theoretical 50% random failure point: about 713 injected bits
```

Design parameters to consider for X1:

- Parity capacity in Mbit is the first-order sizing number.
- Smaller blocks reduce the probability of two SEUs in one block but increase
  parity overhead.
- Larger blocks reduce parity overhead but fail earlier under random SEU
  accumulation.
- TMR protects the UART/control/result path against one bad replica or lane; it
  does not increase the correction strength of a single X1 parity block.
- To tolerate two or more errors per block, replace 2D parity with a stronger
  BCH/RS/LDPC/Hsiao SECDED-per-word design and budget additional X1 Mbits.
- The PARTCL systolic array can help with compression or linear transforms over
  weight chunks, but bit-level ECC syndrome generation is usually more efficient
  in dedicated logic or firmware unless the compression algorithm is naturally
  matrix/vector based.

## Local Full-Model PyTorch Probe

The full Qwen3-0.6B model was loaded locally through Transformers/PyTorch:

```text
Device used by this uv environment: cpu
Torch: 2.12.0+cpu
Elapsed generation time after load: 5.203 s
GPU visible to nvidia-smi: NVIDIA GeForce GTX 950M, 4096 MiB
```

The GPU is present, but the current `uv` PyTorch environment is CPU-only. The
script accepts `--device cuda`; that requires installing a CUDA-enabled PyTorch
build compatible with this laptop. The data file is:

```text
data/qwen_full_inference_probe.json
```

## Run Locally

Build or reuse parity, inject and repair 64 SEU bits, and emit the manifest:

```bash
uv run --python 3.12 --with torch --with transformers --with safetensors --with accelerate \
  caravel_tmr_uart_cocotb/scripts/qwen_x1_ecc_experiment.py run \
  --model-dir E:\hf_models\Qwen--Qwen3-0.6B \
  --out-dir E:\hf_models\Qwen--Qwen3-0.6B\.x1_ecc\qwen_x1_ecc_20260604 \
  --out-json caravel_tmr_uart_cocotb/data/qwen_x1_ecc_manifest.json \
  --seu-bits 64 \
  --seed 20260604 \
  --data-only
```

Run a full local Qwen inference probe:

```bash
uv run --python 3.12 --with torch --with transformers --with safetensors --with accelerate \
  caravel_tmr_uart_cocotb/scripts/qwen_x1_ecc_experiment.py infer \
  --model-dir E:\hf_models\Qwen--Qwen3-0.6B \
  --device auto \
  --max-new-tokens 24
```

## Run Verilog/Cocotb Remotely

All Verilog simulation for this experiment was run on the remote Ubuntu machine:

```text
Host: Ubuntu-rv5-tci-BMServer
IP: 100.115.20.54
User: vboxuser
Remote workdir: /home/vboxuser/caravel_tmr_uart_cocotb_qwen_tmr_20260604
PARTCL RTL: /home/vboxuser/partcl_neuromorphic_compute/verilog/rtl
```

Command:

```bash
cd /home/vboxuser/caravel_tmr_uart_cocotb_qwen_tmr_20260604
PATH=$HOME/.local/bin:$PATH \
PARTCL_RTL_DIR=$HOME/partcl_neuromorphic_compute/verilog/rtl \
make
```

Remote result:

```text
TESTS=3 PASS=3 FAIL=0 SKIP=0
```

The machine-readable result files are:

```text
data/remote_cocotb_results_20260604.json
data/remote_cocotb_results_20260604.xml
```

## Files

```text
rtl/tmr_caravel_uart_system.v
  TMR voters, UART RX/TX, three endpoint replicas, behavioral X1 memory,
  internal Wishbone bridge, and real PARTCL mat_mult_wb instances.

tests/test_tmr_caravel_uart.py
  Cocotb tests for UART TMR, real systolic-to-X1 storage, and Qwen parity
  records through the TMR UART path.

scripts/qwen_x1_ecc_experiment.py
  Full-file Qwen parity generation, SEU injection, correction, SHA restore
  check, failure sweep, and full-model PyTorch inference helper.

data/qwen_x1_ecc_manifest.json
  Real Qwen3-0.6B parity/correction/capacity results and X1 record map.

data/qwen_full_inference_probe.json
  Real local Qwen3-0.6B PyTorch probe result.

data/remote_cocotb_results_20260604.json
  Remote Icarus/cocotb pass summary.
```
