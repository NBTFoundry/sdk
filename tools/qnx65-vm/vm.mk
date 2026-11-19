# SPDX-License-Identifier: MPL-2.0
# Everything the QNX 6.5 SP1 VM (tools/qnx65-vm/run.sh) can check, in one
# boot: the feasibility probe in both modes, with a crash and a hang; then
# nbt-runner with the SDK smoke application and the car probe in both modes,
# GL and audio faked (the VM has neither). The guest GL stream and audio ring
# must give the same results as direct calls. Included from the top Makefile.

VM_ENV := NBT_DATA_DIR=/dev/shmem
VM_FAKE := NBT_LAUNCH=user NBT_SURFACE=5 NBT_RUNNER_FAKE_GL=1 NBT_RUNNER_FAKE_AUDIO=1 $(VM_ENV)
VM_SMOKE := NBT_LAUNCH=check NBT_SETTING_SPEED=fast $(VM_ENV) nbt-runner --stop-after-ms 300 --nbt-payload
# The car probe's draw loop, as the fake GL records it.
VM_GL_RECORD := fake-gl: closed: 600 frames, 600 draws \(2400 vertices\), 2 shaders \(230 bytes\), \
	1 buffers \(32 bytes, sum ebee6df8\)

# vm_background(command, seconds): run it, then stop it as the manager would.
vm_background = -c "$(1) &" -c "waitfor /none $(2)" -c "slay -s TERM nbt-runner" -c "waitfor /none 2"

.PHONY: vm-test
vm-test: $(RUNNER_PROBE_QNX) $(NBT_RUNNER) $(PROBE_MODULE) $(PROBE_GUEST) $(SMOKE_MODULE) $(SMOKE_GUEST) \
		$(CAR_PROBE) $(CAR_PROBE_GUEST)
	tools/qnx65-vm/run.sh \
		-c "$(VM_ENV) runner-probe-qnx /proc/boot/probe.so" \
		-c "$(VM_ENV) runner-probe-qnx --guest /proc/boot/probe.guest" \
		-c "runner-probe-qnx --guest --expect-fault /proc/boot/probe.guest --crash" \
		-c "runner-probe-qnx --guest --expect-fault --timeout 3 /proc/boot/probe.guest --hang" \
		-c "$(VM_SMOKE) /proc/boot/smoke.so" \
		-c "$(VM_SMOKE) /proc/boot/smoke.guest" \
		$(call vm_background,$(VM_FAKE) nbt-runner --nbt-payload /proc/boot/car-probe.so,25) \
		$(call vm_background,$(VM_FAKE) nbt-runner --nbt-payload /proc/boot/car-probe.guest,40) \
		-x 'settings gen=0 speed=fast' -x 'runner: guest exited status=0' -x '$(VM_GL_RECORD)' \
		-x 'audio: tone kept up +ok' -x 'neon: d16-d31 preserved +ok' \
		$^
