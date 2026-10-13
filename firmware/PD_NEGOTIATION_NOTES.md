# PD negotiation — code review findings and status (2026-10-13)

Built, **not yet flashed**. Requires the external 5.1kΩ Rd resistors on CC1/CC2
to be fitted first: the firmware no longer enables the FUSB302's internal
pull-downs, so without the resistors nothing will attach.

Goal: negotiate 20V with any USB-C PD source. Tested so far against a laptop
OTG port (offers 5V only) and a Phreeze 100W GaN charger (5/9/12/15/20V fixed +
3.3-21V PPS, SPR only, no EPR needed).

## Root cause: the PD message header was misencoded

Every register bit and the message format were re-checked against the Linux
kernel's `fusb302` driver and the USB PD spec. The header layout in
`fusb302_pd.c` was off by one bit throughout. Correct layout:
`[4:0] type, [5] data role, [7:6] spec rev, [8] power role, [11:9] msg ID,
[14:12] object count, [15] extended`.

- **Receiving:** the object count was read from bit 11 instead of 12. The OTG
  port's `header=0x1DA1` really carries **1** PDO (5V). We parsed it as 3 and
  read CRC/garbage as power data. That's where "no 20V PDO" came from.
- **Sending:** every message we sent claimed the DFP data role with spec rev
  1.0, and the Request's object count landed in the msg ID field. A source that
  sees its own data role echoed back must do error recovery, which is a Hard
  Reset. That matches the OTG log exactly: `Get_Source_Cap -> ESP_OK`, then an
  immediate Hard Reset. No Request we sent could ever have been valid.

## Other must-fix bugs found and fixed

1. **InterruptA bits were wrong.** Our `TXSENT` was really `I_SOFTRST`, and
   `RETRYFAIL`/`SOFTFAIL` were swapped. The `irq_a=0x11` in the OTG log was
   really HARDRST + RETRYFAIL (the source never acknowledged our malformed
   message).
2. **STATUS0 bits 3-6 were wrong** (`ACTIVITY` was really `ALERT`, and so on).
3. **The interrupt could get stuck forever.** INT_N is level-low until read,
   but the ISR was edge-only and interrupts were only read when an edge
   arrived. VBUSOK latches during init, before the ISR is installed, so INT_N
   could already be low with no edge ever coming. After that, no received
   message would ever be processed. This is the likely reason the charger
   looked "silent". Fixed by clearing pending interrupts at init and servicing
   for as long as the pin is held low, not just on an edge.
4. **Logging before replying.** A source waits only 24-30ms for a reply. We
   logged several lines (blocking at 115200 baud) before sending the Request.
   Every reply is now sent first and logged after.
5. **The RX FIFO was flushed every 500ms while waiting for capabilities**,
   which could delete the very Source_Capabilities we were waiting for.
   Removed.
6. **Source_Capabilities were only answered in the WAIT_CAPS state.** Sources
   may re-send them after a contract exists, and an unanswered one gets a Hard
   Reset. They're now answered in any state.
7. **Messages that need an answer were ignored.** Each of these now gets a
   reply instead of a Hard Reset:
   - `Get_Sink_Cap` → Sink_Capabilities (5V and 20V).
   - `Soft_Reset` → Accept, then wait for new capabilities.
   - `Get_Source_Cap`, `DR_Swap`, `PR_Swap`, `VCONN_Swap` → Reject.
8. **TX token sequence** now matches the Linux driver: TXON last, starting
   transmission only once the whole packet is in the FIFO. The old sequence put
   TXON first and also wrote TX_START. The FIFO is flushed before each send.
9. **The PD protocol logic is reset on attach** (`RESET_PD_RESET`, as in the
   IronOS/Pinecil FUSB302B driver), and the message ID resets on attach, Soft
   Reset and Hard Reset.
10. **INT_N internal pull-up enabled** (harmless if an external one exists).
11. **Fallback when 20V isn't offered:** request the highest fixed PDO up to
    20V with the Capability Mismatch bit set, instead of staying silent.
    Note: on a source without 20V the LED rail will run at that lower voltage
    (5V on the laptop OTG port).

## Earlier changes kept

- `pd_task` runs on core 1 (`main.c`), not core 0 alongside WiFi init.
- Internal PDWN1/PDWN2 pull-downs disabled in favour of the external 5.1kΩ Rd.
- `Get_Source_Cap` is still sent if no capabilities arrive within 1s of attach,
  then every 2s, for up to 12s before re-detecting. Never a Hard Reset: on a
  board powered only by this port, that would cut our own power.

## Theories superseded by the above

- Core-0 starvation making us miss the capability window: still a reasonable
  change, but a spec-compliant source retries for 5-10s, so it wasn't the root
  cause.
- Stuck or full TX/RX FIFO, settle delays, WiFi I2C contention: all symptoms
  of the malformed header and the stuck interrupt.
- Hardware hold-up capacitance: not needed if the source never has a reason
  to Hard Reset.

## What to look for when flashing

Good run, as logs:
```
attached source on CC1 (bc_cc1=.. bc_cc2=..)
  PDO[1] ... 5000mV ...  (one line per PDO the source offers)
Request PDO n: 20000 mV @ 3250 mA -> ESP_OK
Request accepted, waiting for PS_RDY
PD negotiated: 20000 mV @ 3250 mA
```
If capabilities still never arrive from the charger, check whether
`Get_Source_Cap -> ESP_FAIL` persists. With the corrected token sequence that
write should succeed. If it still fails, that's the next lead.

## References

- Linux `drivers/usb/typec/tcpm/fusb302.c` / `fusb302_reg.h`: register bits,
  TX token sequence, RX parsing.
- Linux `include/linux/usb/pd.h`: PD header bit layout.
- Ralim `usb-pd` (IronOS/Pinecil FUSB302B sink): init order, PD logic reset
  after CC selection.
- LKML 2026-01 "Typc fusb302 powerloss issue on Radxa Rock 5b" and Sebastian
  Reichel's LPC 2025 talk on boards that are not self-powered.
