#include "Config.h"
#include "ACUART.h"
#include "ACJV.h"
#include "ACCORE.h"
#include "common/Console.h"
#include <deque>
#include <string>

#define ACUART_LOG(fmt, ...) if (EmuConfig.Arcade.UARTVerbose) Console.WriteLn(Color_Gray, "ACUART:" fmt __VA_OPT__(,) __VA_ARGS__)
#define ACUART_WARN(fmt, ...) if (EmuConfig.Arcade.UARTVerbose) Console.Warning("ACUART:" fmt __VA_OPT__(,) __VA_ARGS__)

// 16550 UART register emulation for Namco System 246 arcade I/O
// Ref: ps2sdk iop/arcade/acuart/src/uart.c
// Register map (16-bit access, 2-byte stride from base 0x12418000):
//   +0x00  THR/RBR/DLL  (TX/RX data or divisor latch low when DLAB=1)
//   +0x02  IER/DLH      (interrupt enable or divisor latch high when DLAB=1)
//   +0x04  IIR/FCR      (interrupt ID read / FIFO control write)
//   +0x06  LCR          (line control, bit 7 = DLAB)
//   +0x08  MCR          (modem control)
//   +0x0A  LSR          (line status)
//   +0x0C  MSR          (modem status)
//   +0x0E  SCR          (scratch)

static u16 uart_ier = 0;
static u16 uart_lcr = 0;
static u16 uart_mcr = 0;
static u16 uart_scr = 0;
static u16 uart_dll = 0;
static u16 uart_dlh = 0;
static u16 uart_fcr_shadow = 0;

// State for Battle Gear 3's boot handshake with its steering ("HANDLE") board; cleared each boot by ResetBg3State.
static int s_bg3TxCnt = 0;
static u8 s_bg3PrevTx = 0;
static int s_bg3HandleCycles = 0;
static bool s_bg3HandleDone = false; // false during the boot HANDLE handshake, true once past it

static std::deque<u8> s_v257RxFifo;                   // bytes Ridge Racer V reads back from its drive board
static u32 s_v257Accum = 0;                           // throttles the periodic refill
static constexpr u8 V257_STATUS[3] = {'C', '0', '1'}; // drive-board OK status; accepted by every RRV build

u16 ACUART::Read16(u32 addr) {
	const u32 reg = addr & 0xFFF;
	ACUART_LOG("Read16  %03X", reg);
	switch (reg) {
	case 0x000: // RBR or DLL
		if (uart_lcr & 0x80)
			return uart_dll;
		if (!s_v257RxFifo.empty()) // RRV reading the serial port: hand it the next status byte we queued ("E00")
		{
			const u8 b = s_v257RxFifo.front();
			s_v257RxFifo.pop_front();
			return b;
		}
		return 0; // no RX data
	case 0x002: // IER or DLH
		if (uart_lcr & 0x80)
			return uart_dlh;
		return uart_ier;
	case 0x004: // IIR (read-only)
		return 0x01; // no interrupt pending, 16550 mode
	case 0x006: // LCR
		return uart_lcr;
	case 0x008: // MCR
		return uart_mcr;
	case 0x00A: // LSR
		// bit 5 = THRE (TX holding register empty)
		// bit 6 = TEMT (TX shift register empty)
		// both set = transmitter idle, ready to accept data
		// bit 0 = DR (RX data ready) — set while the V257 status FIFO has bytes (RRV)
		return 0x60 | (s_v257RxFifo.empty() ? 0x00 : 0x01);
	case 0x00C: // MSR
		return 0;
	case 0x00E: // SCR
		return uart_scr;
	default:
		ACUART_WARN("Unhandled read: %03X", reg);
		return 0;
	}
}

// Battle Gear 3 / Tuned: answer the steering board's boot handshake (BGRLOAD FUN_00133e60) so the game
// doesn't stall on "HANDLE ERROR". With no emulated FFB board we reply ready and keep the drive-board flag
// (EE 0x2694b0) CLEAR, so steering stays on the JVS analog wheel. FFB GROUNDWORK: set s_bg3FfbEnabled=true
// when a real drive board is emulated to run the full calibration handshake.
// (Thanks to Hydreigon223 for the dump.)
static void Bg3HandleReply(u8 curTx)
{
	s_bg3TxCnt++;
	if ((s_bg3TxCnt & 1) != 0) { s_bg3PrevTx = curTx; return; } // a {reg,val} command is 2 bytes; reply on the 2nd

	if (s_bg3PrevTx == 0x20 && curTx == 0x00) // {0x20,0} starts an init -> re-arm so a re-init replies fresh
	{
		s_bg3HandleDone = false;
		s_bg3HandleCycles = 0;
	}
	u8 hi = 0x00;
	if (!s_bg3HandleDone)
	{
		static constexpr bool s_bg3FfbEnabled = false;    // true once a real FFB drive board is emulated
		if (s_bg3PrevTx == 0x20 || s_bg3PrevTx == 0x1f)
			hi = s_bg3FfbEnabled ? 0x80 : 0x01;           // 0x01 = ready (skip calibrate while FFB off)
		else if (s_bg3PrevTx == 0x14 && curTx == 0x1a)
			hi = (++s_bg3HandleCycles > 4) ? 0x00 : 0x80; // calibrate busy-loop (FFB on only)
		if (s_bg3PrevTx == 0x11 && curTx == 0x03)
			s_bg3HandleDone = true;                       // last init command
	}
	s_v257RxFifo.push_back(hi); // byte0 = busy/ready status
	s_v257RxFifo.push_back(0);  // byte1
	ACUART_LOG("INTR");
	ACCORE::intr(ACCORE::INTRN_UART);
	s_bg3PrevTx = curTx;
}

void ACUART::Write16(u32 addr, u16 val) {
	const u32 reg = addr & 0xFFF;
	ACUART_LOG("Write16 %03X:%04X", reg, val);
	switch (reg) {
	case 0x000: // THR or DLL
		if (uart_lcr & 0x80)
			uart_dll = val;
		else // TX byte to the drive board
		{
			const std::string& gdrv = ACJV::GetGameId();
			if (gdrv == "NM00010" || gdrv == "NM00015")
				Bg3HandleReply((u8)val); // BG3/Tuned: answer the HANDLE handshake (other games ignore TX)
		}
		break;
	case 0x002: // IER or DLH — set to 0 on module stop, bits toggled during xmit (acUartModuleStop, uart_xmit)
		if (uart_lcr & 0x80)
			uart_dlh = val;
		else
			uart_ier = val;
		break;
	case 0x004: // FCR (write-only) — val=7 on module stop: enable FIFO + reset RX/TX (acUartModuleStop)
		uart_fcr_shadow = val & 0xC9; // preserve trigger level + enable bits
		break;
	case 0x006: // LCR
		uart_lcr = val;
		break;
	case 0x008: // MCR
		uart_mcr = val;
		break;
	case 0x00E: // SCR
		uart_scr = val;
		break;
	default:
		ACUART_WARN("Unhandled write:%03X:%04X", reg, val);
		break;
	}
}

void ACUART::ResetBg3State()
{
	// Re-arm the BG3 HANDLE handshake on each game boot/reset, else a reset stalls on "HANDLE ERROR".
	s_bg3TxCnt = 0;
	s_bg3PrevTx = 0;
	s_bg3HandleCycles = 0;
	s_bg3HandleDone = false;
}

// Ridge Racer V drive-board status streamer (called each DEV9 tick): refill the receive buffer with the
// board's OK status and raise the RX interrupt. Only RRV needs this.
void ACUART::StreamV257(u32 cycles)
{
	const std::string& gid2 = ACJV::GetGameId();
	if (gid2 == "NM00010" || gid2 == "NM00015")
		return; // BG3: the HANDLE responder is driven synchronously from Write16; leave the RX FIFO intact
	if (gid2 != "NM00001") // only Ridge Racer V needs the streaming responder
	{
		s_v257RxFifo.clear();
		return;
	}
	if (!(uart_ier & 0x01))        // host hasn't enabled the RX-data interrupt yet
		return;
	s_v257Accum += cycles;
	if (s_v257Accum < 240)         // throttle (DEV9async ticks ~tens of kHz with cycles=1) -> a few hundred Hz
		return;
	s_v257Accum = 0;
	// Keep raising the RX IRQ every tick; reload the status only once the ISR has drained the previous copy.
	if (s_v257RxFifo.empty())
		s_v257RxFifo.assign(V257_STATUS, V257_STATUS + 3);
	ACUART_LOG("INTR");
	ACCORE::intr(ACCORE::INTRN_UART); // raise the UART RX interrupt
}
