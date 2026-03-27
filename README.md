Fg3pico is 3 channels frequency generator based on Raspberry Pi Pico 2, it should be compatible with any RP3250 board (but not tested). It can be used for various purposes, for example, it can drive 3 phase motors or can be used for 3 phase AC generation

### Features ###

* Frequency outputs are digital, the pins used for output are GP2, GP3, GP4
* Generator is driven by UART commads. UART pins used are default GP0 and GP1
* UART over USB can be used. It automatically switches over to USB when USB host is connected
* Each frequency has startup delay, 'on' period and 'off' period adjustable parameters. Also generation can be turned off independently for each channel
* Frequency parameters can be stored in on-board flash memory and loaded from flash.
* At board power-up it checks if there are frequencies stored. They are loaded from flash memory and generation starts automatically, no need to connect and setup by UART

### Capabilities ###

Thanks to rp2350 pio features this board has excellent capabilities

* Min timing units 3
* Max timing units 4294967295
* CPU ticks per one time unit 1
* CPU ticks per one second 200000000

So this device has 5ns resolution per one unit. Max frequency it is able to generate is 33 MHz

### UART commands

Each command contains additional CRC bytes at the end. The checksum algorithm used is CRC16/MODBUS.  
The UART speed is 19200 bps. All values use big endian byte ordering (highest byte comes first)

* Command 01 - SET FREQUENCIES  
  if ON period equals to 0 then frequency is muted (off)  
  Command contains 39 bytes. F1, F2, F3 - three frequencies  

  <table>
    <tr>
      <td>Command</td>
      <td>0x01</td>        
    </tr>
    <tr>
      <td>DELAY for F1</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>ON period for F1</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>OFF period for F1</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>DELAY for F2</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>ON period for F2</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>OFF period for F2</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>DELAY for F3</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>ON period for F3</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>OFF period for F3</td>
      <td>32 bit value</td>
    </tr>
    <tr>
      <td>CRC16 Checksum</td>
      <td>16 bits</td>
    </tr>
  </table>
   
* Command 02 - STORE TO FLASH  
  Stores current frequencies in flash memory
  |0x02|0x81|0x3E|
  |---|---|---|

* Command 03 - LOAD FROM FLASH  
  Loads frequencies from the flash, this is done also at the reset (startup)
  |0x03|0x41|0xFF|
  |---|---|---|

* Command 04 - CHECK CAPABILITIES  
  Request device capabilities (timing range, ticks per one unit, cpu ticks per second)
  |0x04|0x83|0xBE|
  |---|---|---|

* Possible device responses:  
  
  OK, COMMAND EXECUTED
  <table>
<tr>
  <td>0x00</td><td>CRC checksum 16 bits</td>
</tr>
</table>

  BAD COMMAND (CRC ERROR)
  <table>
<tr>
  <td>0x01</td><td>CRC checksum 16 bits</td>
</tr>
</table>

  BAD DATA IN FLASH (CRC ERROR)
  <table>
<tr>
  <td>0x02</td><td>CRC checksum 16 bits</td>
</tr>
</table>

  CAPABILITIES (Response to command 04)
  <table>
    <tr>
      <td>Command</td><td>0x04</td>
    </tr>
    <tr>
      <td>Min units 3</td><td>0x00</td><td>0x00</td><td>0x00</td><td>0x03</td>
    </tr>  
    <tr>
      <td>Max units 4294967295</td><td>0xff</td><td>0xff</td><td>0xff</td><td>0xff</td>
    </tr>  
    <tr>
      <td>Ticks per one unit 1</td><td>0x00</td><td>0x00</td><td>0x00</td><td>0x01</td>
    </tr>  
    <tr>
      <td>CPU ticks per one second 200000000</td><td>0x0b</td><td>0xeb</td><td>0xc2</td><td>0x00</td>
    </tr>  
    <tr>
      <td>CRC 16 bits</td><td>CRC high byte</td><td>CRC low byte</td>
    </tr>  
  </table>
