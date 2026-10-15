# PD negotiation — code review findings and status (2026-10-13)

**Confirmed working on hardware (2026-10-13)** with the Phreeze 100W charger
and external 5.1kΩ Rd resistors on CC1/CC2: the charger sent
Source_Capabilities unprompted at ~700ms after boot, PDO 5 (20V @ 5A) was
requested, and the 20V/5A contract was in place at ~970ms. The LED cap went
to 100%. The firmware no longer enables the FUSB302's internal pull-downs, so
the external resistors are required.

Not yet re-tested on this build: the laptop OTG port, and Freeze in two
windows with a camera connected.

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

## Whole-firmware review: other persisting faults fixed (2026-10-13)

1. **Loads ran before there was a contract.** The LED boards and the fans'
   12V buck both run straight off VBUS. The LEDs (at the stored 70%) and fans
   were switched on at boot, while VBUS was still 5V and the source may only
   allow 0.5-1.5A. They also stayed loaded through the 5V→20V transition, and
   after 20V the LEDs (65W) plus fans could exceed a 3A/60W contract. Any of
   these can make the source cut VBUS. Now (`led_control.c`):
   - LEDs and both fans stay at 0 until a **20V** contract exists.
   - LED duty is capped to `(contract W − 10W for the rest of the board) / 65W`:
     about 76% on 20V@3A, 100% on 20V@5A.
   - Load is shed while renegotiating and on Hard Reset or VBUS loss.
   - The firmware now requests the PDO's full current (up to 5A) instead of a
     fixed 3.25A.
2. **The schedule destroyed the brightness setpoint.** Each sunrise/sunset
   ramp step was written back as *the* brightness and saved to flash. Steps
   compounded down to almost nothing, and after the first sunset the stored
   brightness was 0, so the lights never came back on. Ramps now drive only
   the output, never the setpoint. The output is re-applied every minute, so
   changing brightness at night no longer leaves the lights on until morning.
3. **Freeze in two windows hung the whole web server** until power-cycle.
   esp_http_server handles all requests on one task. `/stream` never returned
   while open, and when frozen with no cached frame (camera not sending) it
   waited forever without sending anything, so a closed tab was never
   noticed. Every later request, including unfreeze, queued behind it. Now:
   - Each stream runs in its own task (`httpd_req_async_handler_begin`), so
     the API stays responsive while streaming.
   - A frozen stream with nothing cached ends instead of idling.
   - Streams are capped at 3.
   - Viewers share a latest-frame cache with a sequence number. Before, a
     single-consumer queue split frames between viewers, and one viewer
     leaving stopped the camera for everyone. The camera is now
     reference-counted and streams while anyone watches live.

4. **Schedule clock** (decided: follow real time; if WiFi fails, keep going on
   the built-in clock). There's no RTC battery, so a power cut, which a PD
   Hard Reset is on this board, wipes the time. Before this fix, a reboot
   without WiFi ran the schedule on a 1970 clock starting at midnight UTC.
   Now (`schedule.c`):
   - The current time is saved to NVS every 10 minutes and restored at boot
     if there's no sync yet. The schedule then runs behind by only the
     power-off duration, and the next sync corrects it.
   - The timezone is set at boot, not only once WiFi connects.
   - If there's no valid time at all (first boot, before the first sync),
     lights stay off rather than follow a fake clock.

### Open items (not changed)

- **LED DIM pin during reset.** GPIO11 floats until `led_control_init` runs.
  If the TX6120 boards pull DIM high, the panels run at full brightness for
  that moment (only matters if VBUS is already at 20V, e.g. after a
  watchdog reset). It's a hardware fix: a pull-down on DIM.
- **Default dashboard code is `4712`** unless changed in NVS.

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
