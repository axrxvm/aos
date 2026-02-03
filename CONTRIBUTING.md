# Contributing to aOS

Thanks for your interest in contributing to aOS. This project is an OS-first codebase, so clarity, correctness, and documentation matter as much as new features.

## Ground rules

- Be respectful and constructive in all discussions and reviews.
- Keep changes focused; large refactors should be discussed first.
- Avoid breaking the build without a clear reason or follow-up plan.

## License

By contributing, you agree that your contributions are licensed under the CC BY-NC 4.0 license used by this project.

## Build setup

### Requirements

- GCC with 32-bit multilib support
- NASM
- GNU ld
- GRUB tools (`grub-mkrescue`)
- QEMU (qemu-system-i386 recommended)

Optional (for TAP networking):
- bridge-utils, iptables, dnsmasq, iproute2

On Linux, you can use:

```sh
sudo ./scripts/get-ready.sh
```

### Build and run

```sh
make run
```

Other useful targets:

```sh
make iso
make run-nographic
make run-debug
make run-s
make run-sn
make run-sn-user
```

## Coding style

- C code uses 4-space indentation and braces on the same line.
- Keep the existing file header format (copyright, license, version).
- Prefer clear, defensive code paths over cleverness (this is kernel code).
- Minimize dynamic allocation in early boot paths.
- Keep user-facing strings short and consistent with existing command output.

## Project structure

- `src/arch/` architecture-specific code 
- `src/kernel/` core kernel subsystems and shell commands
- `src/fs/` filesystems and VFS
- `src/net/` networking stack
- `src/dev/` drivers
- `include/` public headers

## Adding commands

- Register new shell commands in the appropriate module (for example `src/kernel/cmd_*.c`).
- Use `command_register_with_category` to ensure your command appears in `help`.
- Provide a concise syntax string and description.

## Testing

There is no automated test suite yet. Please:

- Build the kernel and boot it in QEMU.
- Exercise the affected subsystem with shell commands.
- Note any limitations or manual steps in your PR description.

## Pull requests

- Describe the problem and the approach in the PR summary.
- Include screenshots or serial logs when the output changes.
- If you add a new feature, update README.md or relevant docs.

## Security

If you find a security issue, report it privately to the maintainer rather than opening a public issue.
