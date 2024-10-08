/** @file
  Serial I/O Port library functions with base address discovered from FDT

  Copyright (c) 2008 - 2010, Apple Inc. All rights reserved.<BR>
  Copyright (c) 2012 - 2013, ARM Ltd. All rights reserved.<BR>
  Copyright (c) 2014, Linaro Ltd. All rights reserved.<BR>
  Copyright (c) 2015, Intel Corporation. All rights reserved.<BR>

  SPDX-License-Identifier: BSD-2-Clause-Patent

**/

#include <Base.h>

#include <Library/PcdLib.h>
#include <Library/PL011UartLib.h>
#include <Library/SerialPortLib.h>
#include <Library/FdtSerialPortAddressLib.h>
#ifdef VBOX
# include <Library/VBoxArmPlatformLib.h>
#endif

RETURN_STATUS
EFIAPI
SerialPortInitialize (
  VOID
  )
{
  //
  // This SerialPortInitialize() function is completely empty, for a number of
  // reasons:
  // - if we are executing from flash, it is hard to keep state (i.e., store the
  //   discovered base address in a global), and the most robust way to deal
  //   with this is to discover the base address at every Write ();
  // - calls to the Write() function in this module may be issued before this
  //   initialization function is called: this is not a problem when the base
  //   address of the UART is hardcoded, and only the baud rate may be wrong,
  //   but if we don't know the base address yet, we may be poking into memory
  //   that does not tolerate being poked into;
  // - SEC and PEI phases produce debug output only, so with debug disabled, no
  //   initialization (or device tree parsing) is performed at all.
  //
  // Note that this means that on *every* Write () call, the device tree will be
  // parsed and the UART re-initialized. However, this is a small price to pay
  // for having serial debug output on a UART with no fixed base address.
  //
  return RETURN_SUCCESS;
}

STATIC
UINT64
SerialPortGetBaseAddress (
  VOID
  )
{
  UINT64              BaudRate;
  UINT32              ReceiveFifoDepth;
  EFI_PARITY_TYPE     Parity;
  UINT8               DataBits;
  EFI_STOP_BITS_TYPE  StopBits;
  VOID                *DeviceTreeBase;
  FDT_SERIAL_PORTS    Ports;
  UINT64              UartBase;
  RETURN_STATUS       Status;

#ifndef VBOX
  DeviceTreeBase = (VOID *)(UINTN)PcdGet64 (PcdDeviceTreeInitialBaseAddress);
#else
  DeviceTreeBase = VBoxArmPlatformFdtGet();
#endif

  if (DeviceTreeBase == NULL) {
    return 0;
  }

  Status = FdtSerialGetPorts (DeviceTreeBase, "arm,pl011", &Ports);
  if (RETURN_ERROR (Status)) {
    return 0;
  }

  //
  // Default to the first port found, but (if there are multiple ports) allow
  // the "/chosen" node to override it. Note that if FdtSerialGetConsolePort()
  // fails, it does not modify UartBase.
  //
  UartBase = Ports.BaseAddress[0];
  if (Ports.NumberOfPorts > 1) {
    FdtSerialGetConsolePort (DeviceTreeBase, &UartBase);
  }

  BaudRate         = (UINTN)FixedPcdGet64 (PcdUartDefaultBaudRate);
  ReceiveFifoDepth = 0; // Use the default value for Fifo depth
  Parity           = (EFI_PARITY_TYPE)FixedPcdGet8 (PcdUartDefaultParity);
  DataBits         = FixedPcdGet8 (PcdUartDefaultDataBits);
  StopBits         = (EFI_STOP_BITS_TYPE)FixedPcdGet8 (PcdUartDefaultStopBits);

  Status = PL011UartInitializePort (
             UartBase,
             FixedPcdGet32 (PL011UartClkInHz),
             &BaudRate,
             &ReceiveFifoDepth,
             &Parity,
             &DataBits,
             &StopBits
             );
  if (!RETURN_ERROR (Status)) {
    return UartBase;
  }

  return 0;
}

/**
  Write data to serial device.

  @param  Buffer           Point of data buffer which need to be written.
  @param  NumberOfBytes    Number of output bytes which are cached in Buffer.

  @retval 0                Write data failed.
  @retval !0               Actual number of bytes written to serial device.

**/
UINTN
EFIAPI
SerialPortWrite (
  IN UINT8  *Buffer,
  IN UINTN  NumberOfBytes
  )
{
  UINT64  SerialRegisterBase;

  SerialRegisterBase = SerialPortGetBaseAddress ();
  if (SerialRegisterBase != 0) {
    return PL011UartWrite ((UINTN)SerialRegisterBase, Buffer, NumberOfBytes);
  }

  return 0;
}

/**
  Read data from serial device and save the data in buffer.

  @param  Buffer           Point of data buffer which need to be written.
  @param  NumberOfBytes    Size of Buffer[].

  @retval 0                Read data failed.
  @retval !0               Actual number of bytes read from serial device.

**/
UINTN
EFIAPI
SerialPortRead (
  OUT UINT8  *Buffer,
  IN  UINTN  NumberOfBytes
  )
{
  return 0;
}

/**
  Check to see if any data is available to be read from the debug device.

  @retval TRUE       At least one byte of data is available to be read
  @retval FALSE      No data is available to be read

**/
BOOLEAN
EFIAPI
SerialPortPoll (
  VOID
  )
{
  return FALSE;
}

/**
  Sets the control bits on a serial device.

  @param[in] Control            Sets the bits of Control that are settable.

  @retval RETURN_SUCCESS        The new control bits were set on the serial device.
  @retval RETURN_UNSUPPORTED    The serial device does not support this operation.
  @retval RETURN_DEVICE_ERROR   The serial device is not functioning correctly.

**/
RETURN_STATUS
EFIAPI
SerialPortSetControl (
  IN UINT32  Control
  )
{
  return RETURN_UNSUPPORTED;
}

/**
  Retrieve the status of the control bits on a serial device.

  @param[out] Control           A pointer to return the current control signals from the serial device.

  @retval RETURN_SUCCESS        The control bits were read from the serial device.
  @retval RETURN_UNSUPPORTED    The serial device does not support this operation.
  @retval RETURN_DEVICE_ERROR   The serial device is not functioning correctly.

**/
RETURN_STATUS
EFIAPI
SerialPortGetControl (
  OUT UINT32  *Control
  )
{
  return RETURN_UNSUPPORTED;
}

/**
  Sets the baud rate, receive FIFO depth, transmit/receive time out, parity,
  data bits, and stop bits on a serial device.

  @param BaudRate           The requested baud rate. A BaudRate value of 0 will use the
                            device's default interface speed.
                            On output, the value actually set.
  @param ReceiveFifoDepth   The requested depth of the FIFO on the receive side of the
                            serial interface. A ReceiveFifoDepth value of 0 will use
                            the device's default FIFO depth.
                            On output, the value actually set.
  @param Timeout            The requested time out for a single character in microseconds.
                            This timeout applies to both the transmit and receive side of the
                            interface. A Timeout value of 0 will use the device's default time
                            out value.
                            On output, the value actually set.
  @param Parity             The type of parity to use on this serial device. A Parity value of
                            DefaultParity will use the device's default parity value.
                            On output, the value actually set.
  @param DataBits           The number of data bits to use on the serial device. A DataBits
                            value of 0 will use the device's default data bit setting.
                            On output, the value actually set.
  @param StopBits           The number of stop bits to use on this serial device. A StopBits
                            value of DefaultStopBits will use the device's default number of
                            stop bits.
                            On output, the value actually set.

  @retval RETURN_SUCCESS            The new attributes were set on the serial device.
  @retval RETURN_UNSUPPORTED        The serial device does not support this operation.
  @retval RETURN_INVALID_PARAMETER  One or more of the attributes has an unsupported value.
  @retval RETURN_DEVICE_ERROR       The serial device is not functioning correctly.

**/
RETURN_STATUS
EFIAPI
SerialPortSetAttributes (
  IN OUT UINT64              *BaudRate,
  IN OUT UINT32              *ReceiveFifoDepth,
  IN OUT UINT32              *Timeout,
  IN OUT EFI_PARITY_TYPE     *Parity,
  IN OUT UINT8               *DataBits,
  IN OUT EFI_STOP_BITS_TYPE  *StopBits
  )
{
  return RETURN_UNSUPPORTED;
}
