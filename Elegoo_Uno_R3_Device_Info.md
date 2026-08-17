# Elegoo UNO R3 (CH340) Device Identification and Properties

## Detected device
- COM port: `COM7`
- Device name: `USB-SERIAL CH340 (COM7)`
- Description: `USB-SERIAL CH340`
- Manufacturer: `wch.cn`
- Status: `OK`
- PNP class: `Ports`
- PNP service/driver: `CH341SER_A64`

## USB identifiers
- Vendor ID (VID): `1A86`
- Product ID (PID): `7523`
- USB device ID: `USB\VID_1A86&PID_7523\5&2E4A5A8&0&3`
- Hardware IDs:
  - `USB\VID_1A86&PID_7523&REV_0264`
  - `USB\VID_1A86&PID_7523`
- Compatible IDs:
  - `USB\COMPAT_VID_1A86&Class_FF&SubClass_01&Prot_02`
  - `USB\COMPAT_VID_1A86&Class_FF&SubClass_01`
  - `USB\COMPAT_VID_1A86&Class_FF`
  - `USB\Class_FF&SubClass_01&Prot_02`

## Driver details
- Driver provider: `wch.cn`
- Driver description: `USB-SERIAL CH340`
- Driver INF path: `oem3.inf`
- Driver INF section: `CH341SER_Inst.NTamd64`
- Driver version: `3.9.2024.9`
- Driver date: `9/15/2024`

## Device location
- Location info: `Port_#0003.Hub_#0002`
- Reported device description: `USB Serial`
- Physical device location path: `PCIROOT(80)#PCI(1400)#USBROOT(0)#USB(3), ACPI(_SB_)#ACPI(PC02)#ACPI(XHCI)#ACPI(RHUB)#ACPI(HS03)`

## Device context
This device is a USB-to-serial adapter chip commonly used on clone Arduino boards such as the Elegoo UNO R3. It is recognized by Windows as a CH340 serial device and exposed on `COM7`.

### Notes
- The COM port assignment may change if the board is unplugged and reconnected or if another serial device is attached.
- Use `COM7` in the Arduino IDE or any serial terminal when programming or communicating with this board while it is attached.
- The underlying USB vendor/product pair identifies the CH340 chip, not the exact board vendor.
