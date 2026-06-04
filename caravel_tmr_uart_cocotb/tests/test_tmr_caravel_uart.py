import os
import json
from pathlib import Path

import cocotb
from cocotb.clock import Clock
from cocotb.triggers import ClockCycles, FallingEdge, RisingEdge, Timer
from cocotb.triggers import with_timeout


CLK_PERIOD_NS = 10
CLKS_PER_BIT = int(os.environ.get("CLKS_PER_BIT", "8"))
BIT_TIME_NS = CLK_PERIOD_NS * CLKS_PER_BIT

SYNC = 0xA5
OP_PROGRAM = 0x50
OP_READ = 0x52
OP_MATMUL = 0x4D
RESP = 0x5A

AI_STATUS_ADDR = 0xF0
AI_DONE_WORD = 0xA1000001
X1_AI_C_BASE = 0x80
X1_QWEN_STATUS_RESTORED = 0x51A25601


def _lane_value(bit, invert_mask=0):
    value = 0
    for lane in range(3):
        lane_bit = bit ^ ((invert_mask >> lane) & 1)
        value |= lane_bit << lane
    return value


def _response(addr, data):
    return [
        RESP,
        addr & 0xFF,
        (data >> 24) & 0xFF,
        (data >> 16) & 0xFF,
        (data >> 8) & 0xFF,
        data & 0xFF,
    ]


def _u8(value):
    return value & 0xFF


def _u32(value):
    return value & 0xFFFFFFFF


def _response_data(response):
    assert response[0] == RESP
    return (
        (response[2] << 24)
        | (response[3] << 16)
        | (response[4] << 8)
        | response[5]
    )


def _matmul_2x2(matrix_a, matrix_b):
    return [
        [
            _u32(matrix_a[row][0] * matrix_b[0][col] + matrix_a[row][1] * matrix_b[1][col])
            for col in range(2)
        ]
        for row in range(2)
    ]


def _load_qwen_manifest():
    manifest_path = os.environ.get("QWEN_X1_ECC_MANIFEST")
    if manifest_path:
        path = Path(manifest_path)
    else:
        path = Path(__file__).resolve().parents[1] / "data" / "qwen_x1_ecc_manifest.json"

    if path.exists():
        with path.open("r", encoding="utf-8") as handle:
            return json.load(handle)

    return {
        "repair_demo": {
            "injected_bits": 64,
            "corrected_bits": 64,
            "uncorrectable_blocks": 0,
            "sha256_restored": True,
        },
        "x1_words": [
            {"addr": 0xA0, "name": "model_size_low", "data": 0x599C18A8},
            {"addr": 0xA1, "name": "model_size_high", "data": 0x00000000},
            {"addr": 0xA2, "name": "parity_bits_low", "data": 0x0C996900},
            {"addr": 0xA3, "name": "parity_bits_high", "data": 0x00000000},
            {"addr": 0xA4, "name": "block_count", "data": 0x000599A9},
            {"addr": 0xA5, "name": "injected_bits", "data": 64},
            {"addr": 0xA6, "name": "corrected_bits", "data": 64},
            {"addr": 0xA7, "name": "uncorrectable_blocks", "data": 0},
            {"addr": 0xAA, "name": "status", "data": X1_QWEN_STATUS_RESTORED},
        ],
        "x1_sample_parity_words": [
            {"addr": 0xB0, "name": "sample_parity_word_0", "data": 0x00000000},
            {"addr": 0xB1, "name": "sample_parity_word_1", "data": 0x00000000},
        ],
    }


async def _drive_user_uart_bit(dut, bit, invert_mask=0):
    await FallingEdge(dut.clk_i)
    dut.user_tx_lanes_i.value = _lane_value(bit, invert_mask)
    await ClockCycles(dut.clk_i, CLKS_PER_BIT)


async def _write_uart_byte(dut, byte, invert_mask=0):
    await _drive_user_uart_bit(dut, 0, invert_mask)
    for bit_index in range(8):
        await _drive_user_uart_bit(dut, (byte >> bit_index) & 1, invert_mask)
    await _drive_user_uart_bit(dut, 1, invert_mask)


async def _send_bytes(dut, payload, invert_mask=0):
    for byte in payload:
        await _write_uart_byte(dut, byte, invert_mask)
        await _drive_user_uart_bit(dut, 1, 0)


async def _send_program(dut, addr, data, invert_mask=0):
    payload = [
        SYNC,
        OP_PROGRAM,
        addr & 0xFF,
        (data >> 24) & 0xFF,
        (data >> 16) & 0xFF,
        (data >> 8) & 0xFF,
        data & 0xFF,
    ]
    await _send_bytes(dut, payload, invert_mask)


async def _send_read(dut, addr, invert_mask=0):
    await _send_bytes(dut, [SYNC, OP_READ, addr & 0xFF], invert_mask)


async def _send_matmul(dut, matrix_a, matrix_b, invert_mask=0):
    payload = [
        SYNC,
        OP_MATMUL,
        _u8(matrix_a[0][0]),
        _u8(matrix_a[0][1]),
        _u8(matrix_a[1][0]),
        _u8(matrix_a[1][1]),
        _u8(matrix_b[0][0]),
        _u8(matrix_b[0][1]),
        _u8(matrix_b[1][0]),
        _u8(matrix_b[1][1]),
    ]
    await _send_bytes(dut, payload, invert_mask)


async def _read_uart_byte(dut, label="byte"):
    await with_timeout(FallingEdge(dut.user_rx_o), BIT_TIME_NS * 80, "ns")
    await Timer(BIT_TIME_NS + (BIT_TIME_NS / 2), units="ns")

    value = 0
    for bit_index in range(8):
        value |= int(dut.user_rx_o.value) << bit_index
        await Timer(BIT_TIME_NS, units="ns")

    stop_bit = int(dut.user_rx_o.value)
    assert stop_bit == 1, f"UART stop bit was corrupted while reading {label}"
    return value


async def _read_uart_byte_with_tx_data_fault(dut, fault_mask, label="faulted byte"):
    await with_timeout(FallingEdge(dut.user_rx_o), BIT_TIME_NS * 80, "ns")

    await Timer(BIT_TIME_NS, units="ns")
    dut.caravel_tx_fault_mask_i.value = fault_mask
    await Timer(BIT_TIME_NS / 2, units="ns")

    value = 0
    for bit_index in range(8):
        value |= int(dut.user_rx_o.value) << bit_index
        if bit_index != 7:
            await Timer(BIT_TIME_NS, units="ns")

    await Timer(BIT_TIME_NS / 2, units="ns")
    dut.caravel_tx_fault_mask_i.value = 0
    await Timer(BIT_TIME_NS / 2, units="ns")
    assert int(dut.user_rx_o.value) == 1, f"UART stop bit did not recover while reading {label}"
    return value


async def _read_response(dut, label):
    response = []
    for byte_index in range(6):
        response.append(await _read_uart_byte(dut, f"{label}[{byte_index}]"))
    dut._log.info("%s response: %s", label, " ".join(f"0x{byte:02X}" for byte in response))
    return response


async def _read_word(dut, addr, label, invert_mask=0):
    await _send_read(dut, addr, invert_mask)
    response = await _read_response(dut, label)
    assert response[0] == RESP
    assert response[1] == (addr & 0xFF)
    return _response_data(response)


async def _reset(dut):
    dut.rst_i.value = 1
    dut.user_tx_lanes_i.value = 0b111
    dut.caravel_tx_fault_mask_i.value = 0
    dut.caravel_ai_fault_mask_i.value = 0
    await ClockCycles(dut.clk_i, 8)
    dut.rst_i.value = 0
    await ClockCycles(dut.clk_i, 8)


async def _set_user_lanes_for_voter_check(dut, correct_bit, invert_mask):
    dut.user_tx_lanes_i.value = _lane_value(correct_bit, invert_mask)
    await RisingEdge(dut.clk_i)


@cocotb.test()
async def tmr_uart_program_read_and_faults(dut):
    cocotb.start_soon(Clock(dut.clk_i, CLK_PERIOD_NS, units="ns").start())
    await _reset(dut)

    dut._log.info("Checking RX-side voter: one bad lane is corrected, two bad lanes corrupt.")
    for correct_bit in (0, 1):
        await _set_user_lanes_for_voter_check(dut, correct_bit, 0b001)
        assert int(dut.voted_caravel_rx_o.value) == correct_bit
        assert int(dut.rx_vote_error_o.value) == 1

        await _set_user_lanes_for_voter_check(dut, correct_bit, 0b011)
        assert int(dut.voted_caravel_rx_o.value) == (correct_bit ^ 1)
        assert int(dut.rx_vote_error_o.value) == 1

    dut.user_tx_lanes_i.value = 0b111
    await ClockCycles(dut.clk_i, 4)

    dut._log.info("Checking TX-side voter while the three Caravel UARTs are idle.")
    dut.caravel_tx_fault_mask_i.value = 0b001
    await RisingEdge(dut.clk_i)
    assert int(dut.user_rx_o.value) == 1
    assert int(dut.tx_vote_error_o.value) == 1

    dut.caravel_tx_fault_mask_i.value = 0b011
    await RisingEdge(dut.clk_i)
    assert int(dut.user_rx_o.value) == 0
    assert int(dut.tx_vote_error_o.value) == 1
    dut.caravel_tx_fault_mask_i.value = 0
    await ClockCycles(dut.clk_i, 4)

    addr = 0x12
    data = 0xC21000FF
    expected = _response(addr, data)

    dut._log.info("Programming X1 memory through the voted RX path with one injected RX-lane error.")
    await _send_program(dut, addr, data, invert_mask=0b001)
    write_resp = await _read_response(dut, "program")
    assert write_resp == expected

    dut._log.info("Reading X1 memory back through the voted TX path.")
    await _send_read(dut, addr)
    read_resp = await _read_response(dut, "read")
    assert read_resp == expected

    dut._log.info("Reading with one injected Caravel TX error: TMR output must remain correct.")
    dut.caravel_tx_fault_mask_i.value = 0b001
    await _send_read(dut, addr)
    tx_single_fault_resp = await _read_response(dut, "single_tx_fault")
    assert tx_single_fault_resp == expected
    dut.caravel_tx_fault_mask_i.value = 0
    await ClockCycles(dut.clk_i, 8)

    dut._log.info("Reading with two injected Caravel TX data errors: voted byte must be incorrect.")
    await _send_read(dut, addr)
    corrupted_first_byte = await _read_uart_byte_with_tx_data_fault(dut, 0b011, "double_tx_fault[0]")
    remaining = [await _read_uart_byte(dut, f"double_tx_fault[{index}]") for index in range(1, 6)]

    assert corrupted_first_byte != expected[0]
    assert remaining == expected[1:]


@cocotb.test()
async def tmr_ai_systolic_reram_reliable_uart(dut):
    cocotb.start_soon(Clock(dut.clk_i, CLK_PERIOD_NS, units="ns").start())
    await _reset(dut)

    matrix_a = [
        [2, -1],
        [3, 4],
    ]
    matrix_b = [
        [5, 6],
        [-2, 7],
    ]
    expected = _matmul_2x2(matrix_a, matrix_b)

    dut._log.info("Running 2x2 systolic matmul through TMR RX, then storing C into X1/ReRAM.")
    await _send_matmul(dut, matrix_a, matrix_b, invert_mask=0b001)
    ai_ack = await _read_response(dut, "ai_run")
    assert ai_ack == _response(AI_STATUS_ADDR, AI_DONE_WORD)

    dut._log.info("Reading X1/ReRAM result words through voted Caravel UART TX lanes.")
    c00 = await _read_word(dut, X1_AI_C_BASE + 0, "ai_c00")
    c01 = await _read_word(dut, X1_AI_C_BASE + 1, "ai_c01")
    c10 = await _read_word(dut, X1_AI_C_BASE + 2, "ai_c10")
    c11 = await _read_word(dut, X1_AI_C_BASE + 3, "ai_c11")
    assert [[c00, c01], [c10, c11]] == expected

    dut._log.info("Injecting one bad Caravel AI result: TMR UART output should still be correct.")
    dut.caravel_ai_fault_mask_i.value = 0b001
    await _send_matmul(dut, matrix_a, matrix_b)
    single_fault_ack = await _read_response(dut, "single_ai_fault_ack")
    assert single_fault_ack == _response(AI_STATUS_ADDR, AI_DONE_WORD)
    single_fault_c00 = await _read_word(dut, X1_AI_C_BASE + 0, "single_ai_fault_c00")
    assert single_fault_c00 == expected[0][0]

    dut._log.info("Injecting two bad Caravel AI results: voted external data should become incorrect.")
    dut.caravel_ai_fault_mask_i.value = 0b011
    await _send_matmul(dut, matrix_a, matrix_b)
    double_fault_ack = await _read_response(dut, "double_ai_fault_ack")
    assert double_fault_ack == _response(AI_STATUS_ADDR, AI_DONE_WORD)
    double_fault_c00 = await _read_word(dut, X1_AI_C_BASE + 0, "double_ai_fault_c00")
    assert double_fault_c00 != expected[0][0]
    dut.caravel_ai_fault_mask_i.value = 0


@cocotb.test()
async def tmr_qwen_x1_parity_metadata_uart_tmr(dut):
    cocotb.start_soon(Clock(dut.clk_i, CLK_PERIOD_NS, units="ns").start())
    await _reset(dut)

    manifest = _load_qwen_manifest()
    words = manifest["x1_words"] + manifest.get("x1_sample_parity_words", [])[:8]
    expected_by_name = {item["name"]: _u32(int(item["data"])) for item in manifest["x1_words"]}

    dut._log.info("Programming Qwen X1 parity/correction records through one faulted user RX lane.")
    for item in words:
        addr = int(item["addr"]) & 0xFF
        data = _u32(int(item["data"]))
        await _send_program(dut, addr, data, invert_mask=0b010)
        response = await _read_response(dut, f"qwen_program_{item['name']}")
        assert response == _response(addr, data)

    dut._log.info("Reading Qwen X1 records with one faulted Caravel TX lane; TMR must preserve UART data.")
    dut.caravel_tx_fault_mask_i.value = 0b001
    readback = {}
    for item in manifest["x1_words"]:
        addr = int(item["addr"]) & 0xFF
        readback[item["name"]] = await _read_word(dut, addr, f"qwen_read_{item['name']}")
    dut.caravel_tx_fault_mask_i.value = 0

    assert readback["injected_bits"] == expected_by_name["injected_bits"]
    assert readback["corrected_bits"] == expected_by_name["corrected_bits"]
    assert readback["uncorrectable_blocks"] == 0
    assert readback["status"] == X1_QWEN_STATUS_RESTORED
    assert readback["injected_bits"] == readback["corrected_bits"]
