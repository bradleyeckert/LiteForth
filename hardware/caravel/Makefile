# Simulations for the Caravel RV32E MCU (needs iverilog/vvp and python3).
#   make sim       firmware test (UART loopback, GPIO, timer, mailbox) on RTL
#   make sram22    same, with SRAM22's own behavioral model in place of mcu_ram's
#   make random    200 random programs, RTL vs. the Python reference model (sw/rv.py)
RTL  := verilog/rtl/mcu_top.v verilog/rtl/mcu_cpu.v verilog/rtl/mcu_uart.v verilog/rtl/mcu_ram.v
TB   := verilog/dv/tb_mcu.v
SRAM := macros/sram22_512x32m4w8/sram22_512x32m4w8.v
B    := build

.PHONY: sim sram22 random clean
sim: $(B)/firmware.hex
	iverilog -g2012 -o $(B)/tb_rtl.vvp $(TB) $(RTL)
	vvp -n $(B)/tb_rtl.vvp +HEX=$< +DUMP=$(B)/fw.dump +LOOPBACK +BITNS=360 | tee $(B)/sim.log
	@grep -q 'MAILBOX = 1a6d0a01' $(B)/sim.log && echo PASS || (echo FAIL; false)

sram22: $(B)/firmware.hex
	iverilog -g2012 -DMCU_SRAM22 -o $(B)/tb_sram22.vvp $(TB) $(RTL) $(SRAM)
	vvp -n $(B)/tb_sram22.vvp +HEX=$< +DUMP=$(B)/fw22.dump +LOOPBACK +BITNS=360 | grep -E 'MAILBOX|halted|ERROR'

random: | $(B)
	iverilog -g2012 -DRAM_WORDS=1024 -o $(B)/tb_rtl.vvp $(TB) $(RTL)
	python3 sw/randtest.py 0 200

$(B)/firmware.hex: sw/firmware.py sw/rv.py | $(B)
	python3 sw/firmware.py $@

$(B):
	mkdir -p $@

clean:
	rm -rf $(B)

