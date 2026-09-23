# Old_PandaBox

Firmware workspace for the PandaBox on the **GigaDevice GD32F305** (Cortex-M4, connectivity line).

## Layout

| Path | Contents |
|---|---|
| `GD32F30x_Firmware_Library/` | GD32F30x firmware library V3.0.3: CMSIS, standard peripheral drivers, USB device (USBD) and USB FS libraries |
| `GD32305R_START_Demo_Suites/` | GD32305R-START board demos (GPIO, EXTI, TIMER, USBFS) with Keil MDK and IAR EWARM projects, BSP (`Utilities/gd32f305r_start.c/.h`), schematic and user guide |

## Build notes

- GD32F305 is a **connectivity line** part. Define `GD32F30X_CL` and `USE_STDPERIPH_DRIVER`, and use `startup_gd32f30x_cl.s`.
- The demo projects find the library through `..\..\..\..\GD32F30x_Firmware_Library`, so keep the two top-level folders side by side.

Source: GigaDevice GD32F30x Firmware Library / Demo Suites V3.0.3. See the GigaDevice software license agreement.
