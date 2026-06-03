import os

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
RESP = 0x5A


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


async def _reset(dut):
    dut.rst_i.value = 1
    dut.user_tx_lanes_i.value = 0b111
    dut.caravel_tx_fault_mask_i.value = 0
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
