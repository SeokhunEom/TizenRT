# QEMU ARMv8M Console Duplicate Output Candidates

This checklist tracks isolated fix candidates for the duplicated console output
seen only on `qemu-armv8m` with QEMU `mps2-an505`.

Observed runtime pattern:

- No input is required to reproduce the issue.
- `System Information` appears more often than the lines that follow it.
- The repeated output is prefix-shaped, for example `System Information` 9
  times, `Version` 8 times, `Platform` 7 times.
- This points to repeated flushing/transmission of existing stdout data, not to
  repeated execution of `sysinfo()`.

## Candidates

- [x] 1. Protect the fake TX interrupt drain in `qemu_uart_txint()`.
  - Scope: `os/arch/arm/src/qemu-armv8m/qemu_armv8m_serial.c`
  - Reason: Other UART drivers that call `uart_xmitchars()` directly from
    `txint(true)` do so with interrupts disabled. The QEMU driver called it
    without that protection, allowing the TX interrupt path to re-enter the
    serial upper-half while the xmit ring is being drained.
  - Expected effect: Reduce or eliminate duplicated prefix output caused by
    concurrent `uart_xmitchars()` execution.
  - Verification: Rebuild, then run QEMU without input and confirm
    `System Information`, `Version`, `Platform`, and `TASH>>` appear once.

- [ ] 2. Gate TX/RX handling in `qemu_uart_interrupt()` by `INTSTATUS`.
  - Scope: `os/arch/arm/src/qemu-armv8m/qemu_armv8m_serial.c`
  - Reason: The current handler clears interrupt status but dispatches RX by
    FIFO state and TX by `txready`, even if the pending interrupt cause was not
    RX or TX.
  - Expected effect: Avoid spurious `uart_xmitchars()` calls when QEMU reports
    the UART as TX-ready without a real TX interrupt cause.
  - Verification: Compare no-input QEMU output before and after the change.

- [ ] 3. Avoid enabling TX overrun interrupts for normal TX flow.
  - Scope: `qemu_uart_txint()`
  - Reason: TX overrun is an error condition, not the normal signal that the
    transmit buffer should drain. Enabling it together with TX interrupt may
    cause unnecessary combined IRQ activity on the QEMU UART model.
  - Expected effect: Reduce unrelated UART interrupt noise.
  - Verification: Confirm normal console output and command echo still work.

- [ ] 4. Use a polling-style transmit path for the QEMU console.
  - Scope: QEMU UART serial ops or board config
  - Reason: QEMU's emulated UART is effectively always writable from the guest
    point of view. A polling TX path avoids dependence on emulated TX IRQ
    timing.
  - Expected effect: Make QEMU console output deterministic at the cost of less
    interrupt-driven TX behavior.
  - Verification: Confirm boot output, TASH input/output, and `kernel_tc`
    output remain stable.
