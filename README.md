# Embedded Linux Drivers on i.MX93 FRDM

Lab projects by **Imel23** from Bootlin's *Linux Kernel and Driver Development* training. I cross-compiled Linux 6.12 for the NXP i.MX93 FRDM board, adapted its Device Tree, and wrote three kernel modules in C. [Bootlin training](https://bootlin.com/training/linux-kernel/)

| Project | What it covers |
| --- | --- |
| [Hello module](modules/hello/hello_version.c) | Module loading and unloading, the `who` module parameter, and kernel version and uptime logging. |
| [Nunchuk driver](modules/nunchuk/nunchuk.c) | Initializes the Wii Nunchuk on LPI2C4 at address `0x52`, polls it every 50 ms, and reports X/Y axes and C/Z buttons through the Linux input subsystem. |
| [LPUART driver](modules/serial/serial.c) | Configures LPUART3 and LPUART8 through MMIO registers, transmits at 115,200 baud, and exposes each port as a `miscdevice`. Two [userspace tools](tools/) read and reset the transmit counter via `ioctl`. |
| [Board integration](kernel-patches/) | i.MX93 FRDM support, an ARM64 kernel configuration, a Device Tree description for the Nunchuk, and I²C and UART pin multiplexing. |

## On-board tests

- **Nunchuk.** The `evtest` capture shows presses and releases for the C and Z buttons. [Screenshot](docs/nunchuk-evtest.png)
- **LPUART.** Minicom received `Hello!` from the serial device. The transmit counter went from 0 to 7 after the write and returned to 0 after the reset. [UART test](docs/board-testing.md#uart-transmission-and-counter)

I reached the **Sleeping and handling interrupts** lab. The UART driver's `read()` currently returns `-EINVAL`. Interrupt-driven reception, a ring buffer, and blocking reads remain to be added.

## Build

On your development machine, install `make`, a host compiler, and an ARM64 cross-toolchain with a C library for static linking. Use the build tree for the kernel running on the board, including its configuration and `Module.symvers`. You can reproduce that build from Linux 6.12.103. [Kernel preparation](docs/board-testing.md#prepare-the-kernel)

```bash
make ARCH=arm64 CROSS_COMPILE=aarch64-linux-gnu- KDIR=/path/to/linux
```

The build puts the `.ko` files under `modules/` and the ARM64 executables under `tools/`. [On-board test instructions](docs/board-testing.md#test-the-modules-on-the-board)

## Credits

The UART driver and userspace tools build on starter code from the Bootlin labs. The base board support comes from the NXP Device Tree provided by Bootlin. SPDX identifiers in the source files and patches specify the applicable licenses. The adapted userspace tools are covered by the training materials' CC BY-SA 3.0 license. [Bootlin training materials](https://github.com/bootlin/training-materials), [training materials license](https://github.com/bootlin/training-materials/blob/master/LICENSE), [GPL version 2](LICENSE)
