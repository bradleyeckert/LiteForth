"""Demo/peripheral firmware for mcu_top (fits in 128 words). Writes firmware.hex."""
import sys, os
sys.path.insert(0, os.path.dirname(__file__))
from rv import Asm

IO = 0x80000000
GPIO_OUT, GPIO_OE, GPIO_IN = 0x00, 0x04, 0x08
UART_DATA, UART_STAT, UART_DIV, TIMER, MBOX = 0x10, 0x14, 0x18, 0x20, 0x30

def build(div=8, msg=b"MCU OK\r\n"):
    a = Asm()
    a.lui(1, IO)                       # x1 = IO base
    a.li(2, div);     a.st('sw', 2, 1, UART_DIV)
    a.li(2, 0x00FF);  a.st('sw', 2, 1, GPIO_OE)
    a.ld('lw', 9, 1, TIMER)            # x9 = timer start
    # --- print message, string stored as immediates ---
    for ch in msg:
        a.li(3, ch)
        lbl = f'tx{a.pc()}'
        a.label(lbl)
        a.ld('lw', 4, 1, UART_STAT); a.i('andi', 4, 4, 1)
        a.br('bne', 4, 0, lbl)         # wait while tx_busy
        a.st('sw', 3, 1, UART_DATA)
        rl = f'rx{a.pc()}'                 # wait for the looped-back byte, read it (clears rx_valid)
        a.label(rl); a.ld('lw', 4, 1, UART_STAT); a.i('andi', 4, 4, 2); a.br('beq', 4, 0, rl)
        a.ld('lbu', 10, 1, UART_DATA)      # x10 = last received byte
    # --- fib(20) ---
    a.li(5, 0); a.li(6, 1); a.li(7, 20)
    a.label('fib'); a.r('add', 8, 5, 6); a.i('addi', 5, 6, 0); a.i('addi', 6, 8, 0)
    a.i('addi', 7, 7, -1); a.br('bne', 7, 0, 'fib')        # x5 = fib(20) = 6765
    # --- GPIO: out[7:0] = in[15:8] ^ 0x5A ---
    a.ld('lw', 11, 1, GPIO_IN); a.i('srli', 11, 11, 8); a.i('xori', 11, 11, 0x5A)
    a.st('sw', 11, 1, GPIO_OUT)
    # --- timer advanced? ---
    a.ld('lw', 12, 1, TIMER); a.r('sltu', 12, 9, 12)        # x12 = (start < now)
    # mailbox = fib<<16 | rx<<8 | timer_ok
    a.i('slli', 13, 5, 16); a.i('slli', 14, 10, 8); a.r('or', 13, 13, 14); a.r('or', 13, 13, 12)
    a.st('sw', 13, 1, MBOX)
    # also store result in RAM at last word, test byte/half stores
    a.li(15, 0x1F0)
    a.st('sw', 0, 15, 0); a.st('sb', 10, 15, 0); a.st('sh', 5, 15, 2)
    a.ecall()
    return a.link()

if __name__ == '__main__':
    words = build()
    assert len(words) <= 496, len(words)
    open(sys.argv[1] if len(sys.argv) > 1 else 'firmware.hex', 'w').write('\n'.join(f'{w:08x}' for w in words) + '\n')
    print(f'{len(words)} words; expected mailbox = {(6765 << 16) | (0x0a << 8) | 1:08x}, RAM[0x1F0] = {(6765 << 16) | 0x0a:08x}')
