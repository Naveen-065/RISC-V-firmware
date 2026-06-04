#!/usr/bin/env python3
"""
Full-file Qwen SEU and X1 parity experiment.

The ECC used here is a 2D parity code over each 4096-byte model block. Each
block is treated as 64 rows x 512 bit-columns:

* 64 row parity bits
* 512 column parity bits
* 576 parity bits = 72 parity bytes per 4096-byte data block

That corrects one flipped bit per block and detects, but cannot correct, most
multi-bit corruptions in the same block.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
import platform
import random
import shutil
import struct
import subprocess
import sys
import time
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


BLOCK_SIZE = 4096
ROW_BYTES = 64
ROWS = BLOCK_SIZE // ROW_BYTES
COL_BITS = ROW_BYTES * 8
PARITY_BYTES_PER_BLOCK = 8 + 64
X1_QWEN_BASE = 0xA0
X1_QWEN_STATUS_RESTORED = 0x51A25601


@dataclass(frozen=True)
class ModelFile:
    path: Path
    size_bytes: int
    blocks: int


@dataclass(frozen=True)
class Injection:
    file_index: int
    block_index: int
    byte_index: int
    bit_index: int


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        while True:
            chunk = handle.read(16 * 1024 * 1024)
            if not chunk:
                break
            digest.update(chunk)
    return digest.hexdigest()


def discover_model_files(model_dir: Path) -> list[ModelFile]:
    paths = sorted(model_dir.glob("model*.safetensors"))
    if not paths:
        single = model_dir / "model.safetensors"
        if single.exists():
            paths = [single]
    files = [
        ModelFile(path=path, size_bytes=path.stat().st_size, blocks=math.ceil(path.stat().st_size / BLOCK_SIZE))
        for path in paths
    ]
    if not files:
        raise FileNotFoundError(f"No model*.safetensors files found under {model_dir}")
    return files


def _parity_for_block(block: bytes) -> bytes:
    if len(block) < BLOCK_SIZE:
        block = block + bytes(BLOCK_SIZE - len(block))

    row_bits = 0
    col = bytearray(64)
    for row in range(ROWS):
        row_chunk = block[row * ROW_BYTES : (row + 1) * ROW_BYTES]
        row_parity = 0
        for index, value in enumerate(row_chunk):
            row_parity ^= value.bit_count() & 1
            col[index] ^= value
        if row_parity:
            row_bits |= 1 << row

    return row_bits.to_bytes(8, "little") + bytes(col)


def _iter_blocks(path: Path) -> Iterable[bytes]:
    with path.open("rb") as handle:
        while True:
            block = handle.read(BLOCK_SIZE)
            if not block:
                break
            yield block


def build_parity_file(model_file: ModelFile, parity_file: Path, force: bool = False) -> dict:
    expected_size = model_file.blocks * PARITY_BYTES_PER_BLOCK
    if parity_file.exists() and parity_file.stat().st_size == expected_size and not force:
        return {
            "path": str(parity_file),
            "bytes": expected_size,
            "reused": True,
        }

    parity_file.parent.mkdir(parents=True, exist_ok=True)
    with parity_file.open("wb") as out:
        for block in _iter_blocks(model_file.path):
            out.write(_parity_for_block(block))
    return {
        "path": str(parity_file),
        "bytes": parity_file.stat().st_size,
        "reused": False,
    }


def _parity_record_at(parity_file: Path, block_index: int) -> bytes:
    with parity_file.open("rb") as handle:
        handle.seek(block_index * PARITY_BYTES_PER_BLOCK)
        record = handle.read(PARITY_BYTES_PER_BLOCK)
    if len(record) != PARITY_BYTES_PER_BLOCK:
        raise ValueError(f"Short parity record for block {block_index} in {parity_file}")
    return record


def _syndrome(stored: bytes, block: bytes) -> tuple[int, bytes]:
    current = _parity_for_block(block)
    stored_rows = int.from_bytes(stored[:8], "little")
    current_rows = int.from_bytes(current[:8], "little")
    row_syndrome = stored_rows ^ current_rows
    col_syndrome = bytes(a ^ b for a, b in zip(stored[8:], current[8:]))
    return row_syndrome, col_syndrome


def _single_set_bit_index(value: int) -> int | None:
    if value == 0 or value & (value - 1):
        return None
    return value.bit_length() - 1


def _single_column_bit(col_syndrome: bytes) -> int | None:
    found = None
    for byte_index, value in enumerate(col_syndrome):
        bit_index = _single_set_bit_index(value)
        if bit_index is None:
            if value != 0:
                return None
            continue
        if found is not None:
            return None
        found = byte_index * 8 + bit_index
    return found


def choose_injections(
    files: list[ModelFile],
    seu_bits: int,
    seed: int,
    unique_blocks: bool,
    data_only: bool,
) -> list[Injection]:
    rng = random.Random(seed)
    weighted_blocks: list[tuple[int, int]] = []
    for file_index, model_file in enumerate(files):
        first_offset = safetensors_data_start(model_file.path) if data_only else 0
        first_block = first_offset // BLOCK_SIZE
        for block_index in range(first_block, model_file.blocks):
            weighted_blocks.append((file_index, block_index))

    if unique_blocks and seu_bits > len(weighted_blocks):
        raise ValueError("Cannot choose more unique SEU blocks than model blocks")

    if unique_blocks:
        chosen_blocks = rng.sample(weighted_blocks, seu_bits)
    else:
        chosen_blocks = [rng.choice(weighted_blocks) for _ in range(seu_bits)]

    injections = []
    for file_index, block_index in chosen_blocks:
        model_file = files[file_index]
        block_start = block_index * BLOCK_SIZE
        valid_len = min(BLOCK_SIZE, model_file.size_bytes - block_start)
        if data_only:
            data_start = safetensors_data_start(model_file.path)
            if block_start < data_start:
                low = data_start - block_start
            else:
                low = 0
        else:
            low = 0
        if valid_len <= low:
            continue
        byte_index = rng.randrange(low, valid_len)
        bit_index = rng.randrange(8)
        injections.append(Injection(file_index, block_index, byte_index, bit_index))
    return injections


def safetensors_data_start(path: Path) -> int:
    with path.open("rb") as handle:
        header_len_raw = handle.read(8)
    if len(header_len_raw) != 8:
        return 0
    header_len = struct.unpack("<Q", header_len_raw)[0]
    data_start = 8 + header_len
    if data_start >= path.stat().st_size:
        return 0
    return data_start


def copy_and_inject(
    files: list[ModelFile],
    injections: list[Injection],
    run_dir: Path,
) -> list[Path]:
    run_dir.mkdir(parents=True, exist_ok=True)
    copies = []
    for model_file in files:
        target = run_dir / model_file.path.name
        shutil.copy2(model_file.path, target)
        copies.append(target)

    with_handles = [path.open("r+b") for path in copies]
    try:
        for injection in injections:
            handle = with_handles[injection.file_index]
            offset = injection.block_index * BLOCK_SIZE + injection.byte_index
            handle.seek(offset)
            value = handle.read(1)
            if not value:
                raise ValueError(f"Injection offset outside file: {offset}")
            handle.seek(offset)
            handle.write(bytes([value[0] ^ (1 << injection.bit_index)]))
    finally:
        for handle in with_handles:
            handle.close()
    return copies


def repair_file_in_place(corrupt_file: Path, parity_file: Path) -> dict:
    corrected_bits = 0
    clean_blocks = 0
    uncorrectable_blocks = 0
    detected_blocks = 0
    file_size = corrupt_file.stat().st_size
    blocks = math.ceil(file_size / BLOCK_SIZE)

    with corrupt_file.open("r+b") as handle, parity_file.open("rb") as parity:
        for block_index in range(blocks):
            block_offset = block_index * BLOCK_SIZE
            handle.seek(block_offset)
            block = bytearray(handle.read(BLOCK_SIZE))
            stored = parity.read(PARITY_BYTES_PER_BLOCK)
            if len(stored) != PARITY_BYTES_PER_BLOCK:
                raise ValueError(f"Missing parity for block {block_index} of {corrupt_file}")

            row_syndrome, col_syndrome = _syndrome(stored, bytes(block))
            if row_syndrome == 0 and not any(col_syndrome):
                clean_blocks += 1
                continue

            detected_blocks += 1
            row = _single_set_bit_index(row_syndrome)
            col = _single_column_bit(col_syndrome)
            if row is None or col is None:
                uncorrectable_blocks += 1
                continue

            byte_offset = row * ROW_BYTES + (col // 8)
            bit_index = col % 8
            if byte_offset >= len(block):
                uncorrectable_blocks += 1
                continue

            block[byte_offset] ^= 1 << bit_index
            handle.seek(block_offset)
            handle.write(block)
            corrected_bits += 1

    return {
        "corrected_bits": corrected_bits,
        "clean_blocks": clean_blocks,
        "detected_blocks": detected_blocks,
        "uncorrectable_blocks": uncorrectable_blocks,
    }


def occupancy_failure_sweep(
    files: list[ModelFile],
    seed: int,
    max_seu: int,
    step: int,
    trials: int,
) -> dict:
    total_blocks = sum(file.blocks for file in files)
    rng = random.Random(seed)
    results = []
    first_failure = None

    for seu_bits in range(step, max_seu + 1, step):
        failures = 0
        avg_uncorrectable = 0.0
        for _ in range(trials):
            block_hits: dict[int, int] = {}
            for _bit in range(seu_bits):
                block = rng.randrange(total_blocks)
                block_hits[block] = block_hits.get(block, 0) + 1
            uncorrectable = sum(1 for count in block_hits.values() if count > 1)
            if uncorrectable:
                failures += 1
            avg_uncorrectable += uncorrectable / trials
        fail_probability = failures / trials
        row = {
            "seu_bits": seu_bits,
            "trials": trials,
            "failure_probability": fail_probability,
            "avg_uncorrectable_blocks": avg_uncorrectable,
        }
        results.append(row)
        if first_failure is None and fail_probability > 0:
            first_failure = row

    theoretical_seu_50pct = math.sqrt(2.0 * total_blocks * math.log(2.0))
    return {
        "seed": seed,
        "max_seu": max_seu,
        "step": step,
        "trials": trials,
        "total_blocks": total_blocks,
        "first_observed_failure": first_failure,
        "theoretical_random_seu_50pct_failure_bits": theoretical_seu_50pct,
        "rows": results,
    }


def first_duplicate_block_failure(files: list[ModelFile], seed: int, max_seu: int = 100_000) -> dict:
    total_blocks = sum(file.blocks for file in files)
    rng = random.Random(seed)
    seen: dict[int, int] = {}
    for seu_index in range(1, max_seu + 1):
        block = rng.randrange(total_blocks)
        if block in seen:
            return {
                "seed": seed,
                "first_uncorrectable_at_seu_bits": seu_index,
                "duplicate_block": block,
                "previous_hit_at_seu_bits": seen[block],
                "why": "2D parity corrects one bit per block; the duplicate block has two SEUs.",
            }
        seen[block] = seu_index
    return {
        "seed": seed,
        "first_uncorrectable_at_seu_bits": None,
        "searched_to_seu_bits": max_seu,
    }


def x1_words_from_result(total_size: int, total_blocks: int, total_parity_bytes: int, repair: dict, original_sha: str, restored_sha: str) -> list[dict]:
    parity_bits = total_parity_bytes * 8
    words = [
        ("model_size_low", total_size & 0xFFFFFFFF),
        ("model_size_high", (total_size >> 32) & 0xFFFFFFFF),
        ("parity_bits_low", parity_bits & 0xFFFFFFFF),
        ("parity_bits_high", (parity_bits >> 32) & 0xFFFFFFFF),
        ("block_count", total_blocks & 0xFFFFFFFF),
        ("injected_bits", repair["injected_bits"] & 0xFFFFFFFF),
        ("corrected_bits", repair["corrected_bits"] & 0xFFFFFFFF),
        ("uncorrectable_blocks", repair["uncorrectable_blocks"] & 0xFFFFFFFF),
        ("original_sha32", int(original_sha[:8], 16)),
        ("restored_sha32", int(restored_sha[:8], 16)),
        ("status", X1_QWEN_STATUS_RESTORED if original_sha == restored_sha else 0xBAD00001),
    ]
    return [
        {"addr": X1_QWEN_BASE + index, "name": name, "data": data}
        for index, (name, data) in enumerate(words)
    ]


def sample_parity_words(parity_file: Path, addr_base: int = 0xB0, words: int = 8) -> list[dict]:
    with parity_file.open("rb") as handle:
        raw = handle.read(words * 4)
    raw = raw.ljust(words * 4, b"\x00")
    out = []
    for index in range(words):
        value = int.from_bytes(raw[index * 4 : (index + 1) * 4], "little")
        out.append({"addr": addr_base + index, "name": f"sample_parity_word_{index}", "data": value})
    return out


def capture_hardware() -> dict:
    hardware = {
        "platform": platform.platform(),
        "python": sys.version.split()[0],
    }
    try:
        proc = subprocess.run(
            ["nvidia-smi", "--query-gpu=name,memory.total,driver_version", "--format=csv,noheader"],
            check=False,
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
            timeout=10,
        )
        hardware["nvidia_smi"] = proc.stdout.strip() or proc.stderr.strip()
    except Exception as exc:
        hardware["nvidia_smi"] = f"not available: {exc}"

    try:
        import torch  # type: ignore

        hardware["torch_version"] = torch.__version__
        hardware["torch_cuda_available"] = bool(torch.cuda.is_available())
        if torch.cuda.is_available():
            hardware["torch_cuda_device"] = torch.cuda.get_device_name(0)
            hardware["torch_cuda_memory_bytes"] = torch.cuda.get_device_properties(0).total_memory
    except Exception as exc:
        hardware["torch"] = f"not importable: {exc}"
    return hardware


def run_full_experiment(args: argparse.Namespace) -> dict:
    model_dir = Path(args.model_dir).resolve()
    out_dir = Path(args.out_dir).resolve()
    out_dir.mkdir(parents=True, exist_ok=True)

    files = discover_model_files(model_dir)
    parity_dir = out_dir / "parity"
    parity_records = []
    file_records = []
    original_digest = hashlib.sha256()

    started = time.time()
    for model_file in files:
        file_sha = sha256_file(model_file.path)
        original_digest.update(bytes.fromhex(file_sha))
        parity_file = parity_dir / f"{model_file.path.name}.2dparity.bin"
        parity_info = build_parity_file(model_file, parity_file, force=args.force_parity)
        parity_records.append(parity_info)
        file_records.append(
            {
                "name": model_file.path.name,
                "path": str(model_file.path),
                "size_bytes": model_file.size_bytes,
                "blocks": model_file.blocks,
                "sha256": file_sha,
                "safetensors_data_start": safetensors_data_start(model_file.path),
                "parity_file": str(parity_file),
                "parity_bytes": parity_info["bytes"],
            }
        )

    injections = choose_injections(
        files,
        seu_bits=args.seu_bits,
        seed=args.seed,
        unique_blocks=args.unique_blocks,
        data_only=args.data_only,
    )
    run_dir = out_dir / f"seu_{args.seu_bits}_seed_{args.seed}"
    corrupt_files = copy_and_inject(files, injections, run_dir)

    repair_totals = {
        "injected_bits": len(injections),
        "corrected_bits": 0,
        "detected_blocks": 0,
        "uncorrectable_blocks": 0,
        "clean_blocks": 0,
    }
    restored_digest = hashlib.sha256()
    for corrupt_file, parity_info in zip(corrupt_files, parity_records):
        stats = repair_file_in_place(corrupt_file, Path(parity_info["path"]))
        for key in ("corrected_bits", "detected_blocks", "uncorrectable_blocks", "clean_blocks"):
            repair_totals[key] += stats[key]
        restored_digest.update(bytes.fromhex(sha256_file(corrupt_file)))

    original_sha = original_digest.hexdigest()
    restored_sha = restored_digest.hexdigest()
    repair_totals["sha256_restored"] = original_sha == restored_sha
    repair_totals["original_model_tree_sha256"] = original_sha
    repair_totals["restored_model_tree_sha256"] = restored_sha
    repair_totals["log_line"] = (
        f"skew Injected {repair_totals['injected_bits']} SEU bits, "
        f"corrected {repair_totals['corrected_bits']}, "
        f"{repair_totals['uncorrectable_blocks']} uncorrectable blocks, "
        f"SHA-256 {'restored' if repair_totals['sha256_restored'] else 'mismatch'}."
    )

    total_size = sum(file.size_bytes for file in files)
    total_blocks = sum(file.blocks for file in files)
    total_parity_bytes = sum(record["bytes"] for record in parity_records)

    sweep = occupancy_failure_sweep(
        files,
        seed=args.seed + 17,
        max_seu=args.sweep_max_seu,
        step=args.sweep_step,
        trials=args.sweep_trials,
    )

    manifest = {
        "experiment": "qwen_x1_2d_parity_tmr_uart",
        "created_utc": time.strftime("%Y-%m-%dT%H:%M:%SZ", time.gmtime()),
        "model_dir": str(model_dir),
        "block_size_bytes": BLOCK_SIZE,
        "row_bytes": ROW_BYTES,
        "rows_per_block": ROWS,
        "column_bits_per_block": COL_BITS,
        "parity_bytes_per_block": PARITY_BYTES_PER_BLOCK,
        "correction_limit": "one corrected bit per 4096-byte block",
        "files": file_records,
        "total_model_bytes": total_size,
        "total_model_bits": total_size * 8,
        "total_blocks": total_blocks,
        "total_parity_bytes": total_parity_bytes,
        "total_parity_bits": total_parity_bytes * 8,
        "effective_x1_parity_mbit": (total_parity_bytes * 8) / 1_000_000,
        "tmr_physical_x1_parity_mbit": (total_parity_bytes * 8 * 3) / 1_000_000,
        "repair_demo": repair_totals,
        "failure_sweep": sweep,
        "deterministic_first_failure": first_duplicate_block_failure(files, args.seed),
        "x1_words": x1_words_from_result(total_size, total_blocks, total_parity_bytes, repair_totals, original_sha, restored_sha),
        "x1_sample_parity_words": sample_parity_words(Path(parity_records[0]["path"])),
        "hardware": capture_hardware(),
        "elapsed_seconds": time.time() - started,
    }

    out_json = Path(args.out_json).resolve() if args.out_json else out_dir / "qwen_x1_ecc_manifest.json"
    out_json.parent.mkdir(parents=True, exist_ok=True)
    out_json.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(repair_totals["log_line"])
    print(f"manifest: {out_json}")
    print(f"effective parity: {manifest['effective_x1_parity_mbit']:.3f} Mbit")
    print(f"TMR physical parity: {manifest['tmr_physical_x1_parity_mbit']:.3f} Mbit")
    return manifest


def run_inference(args: argparse.Namespace) -> None:
    import torch  # type: ignore
    from transformers import AutoModelForCausalLM, AutoTokenizer  # type: ignore

    model_dir = Path(args.model_dir).resolve()
    device = args.device
    if device == "auto":
        device = "cuda" if torch.cuda.is_available() else "cpu"
    dtype = "auto"
    tokenizer = AutoTokenizer.from_pretrained(model_dir, local_files_only=True, trust_remote_code=True)
    model = AutoModelForCausalLM.from_pretrained(
        model_dir,
        local_files_only=True,
        trust_remote_code=True,
        torch_dtype=dtype,
    )
    model.to(device)
    model.eval()

    prompt = args.prompt
    inputs = tokenizer(prompt, return_tensors="pt").to(device)
    started = time.time()
    with torch.no_grad():
        output = model.generate(
            **inputs,
            max_new_tokens=args.max_new_tokens,
            do_sample=False,
            pad_token_id=tokenizer.eos_token_id,
        )
    elapsed = time.time() - started
    print(tokenizer.decode(output[0], skip_special_tokens=True))
    print(json.dumps({"device": device, "elapsed_seconds": elapsed, "torch": torch.__version__}, indent=2))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    run = sub.add_parser("run", help="Build parity, inject/correct SEUs, sweep random failure probability, and emit manifest")
    run.add_argument("--model-dir", default=r"E:\hf_models\Qwen--Qwen3-0.6B")
    run.add_argument("--out-dir", default=r"E:\hf_models\Qwen--Qwen3-0.6B\.x1_ecc\qwen_x1_ecc_20260604")
    run.add_argument("--out-json", default="")
    run.add_argument("--seu-bits", type=int, default=64)
    run.add_argument("--seed", type=int, default=20260604)
    run.add_argument("--unique-blocks", action="store_true", default=True)
    run.add_argument("--allow-block-collisions", action="store_false", dest="unique_blocks")
    run.add_argument("--data-only", action="store_true", help="Inject only in safetensors tensor payloads")
    run.add_argument("--force-parity", action="store_true")
    run.add_argument("--sweep-max-seu", type=int, default=4096)
    run.add_argument("--sweep-step", type=int, default=64)
    run.add_argument("--sweep-trials", type=int, default=64)
    run.set_defaults(func=run_full_experiment)

    infer = sub.add_parser("infer", help="Run the full local Qwen model through PyTorch/Transformers")
    infer.add_argument("--model-dir", default=r"E:\hf_models\Qwen--Qwen3-0.6B")
    infer.add_argument("--prompt", default="Explain in one sentence why ECC helps reliable AI inference.")
    infer.add_argument("--device", default="auto", choices=["auto", "cpu", "cuda"])
    infer.add_argument("--max-new-tokens", type=int, default=24)
    infer.set_defaults(func=run_inference)

    args = parser.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
