# VM testing preferences

- Run `vm_monitor.py` with `--timeout 30 --stall 6`. Never use a timeout above 30 seconds or a stall timeout above 6 seconds.
- Run without a display by setting `ROS_QEMU_DISPLAY=none`; omit screen arguments.
- Leave changes uncommitted unless the user explicitly requests a commit.

# Shared code and architecture support

- Keep common code architecture neutral and suitable for existing ports and the upcoming RISC-V64 port.
- Do not use common code for experiments or application-specific workarounds. Add special cases only when their necessity is demonstrated, and put architecture-specific behavior in the architecture layer where possible.
- Prefer fixes to the underlying behavior. Keep doubtful or incomplete changes unstaged, and remove temporary diagnostics before committing.
