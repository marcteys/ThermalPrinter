# Download BMP and Print Example

This example demonstrates how to download a BMP image from a URL and print it on a thermal printer using the `printBitmapOld()` function.

## Features

- Downloads BMP images from HTTP/HTTPS URLs
- Parses standard BMP file formats:
  - 1-bit monochrome
  - 4-bit indexed color
  - 8-bit indexed color
  - 24-bit RGB
- Converts BMP to thermal printer bitmap format
- Prints using the older GS v 0 ESC/POS command
- Supports both bottom-up and top-down BMP storage

## Hardware Requirements

- ESP32 development board
- Thermal printer (ESC/POS compatible)
- WiFi connection

## Wiring

```
ESP32 GPIO 16 (RX) → Printer TX
ESP32 GPIO 17 (TX) → Printer RX
ESP32 GPIO 14      → Printer DTR (optional)
ESP32 GND          → Printer GND
Printer VCC        → External 5-9V power supply
```

**Important:** Do not power the printer from the ESP32! Use an external power supply.

## Configuration

Before uploading, edit these settings in the sketch:

### WiFi Settings
```cpp
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
```

### BMP URL
```cpp
const char* bmpUrl = "http://localhost/phototicket/images/last.bmp";
```

Change this to point to your BMP image. Examples:
- Local server: `http://192.168.1.100/image.bmp`
- Remote server: `http://example.com/path/to/image.bmp`
- HTTPS: Change `WiFiClient` to `WiFiClientSecure` in the code

### Printer Settings
```cpp
const byte rxPin = 16;    // ESP32 RX pin
const byte txPin = 17;    // ESP32 TX pin
const byte dtrPin = 14;   // DTR pin (optional)
const int printerBaudrate = 9600;  // Usually 9600 or 19200
```

## Usage

1. Install the ThermalPrinter library
2. Open `download_bmp_print.ino` in Arduino IDE
3. Configure WiFi and URL settings
4. Upload to ESP32
5. Open Serial Monitor at 115200 baud
6. Watch the progress as it downloads and prints

## Print Modes

The example uses `printBitmapOld()` with different modes:

```cpp
// Normal size, centered
myPrinter.printBitmapOld(bitmap, w, h, 0, true);

// Double width
myPrinter.printBitmapOld(bitmap, w, h, 1, true);

// Double height
myPrinter.printBitmapOld(bitmap, w, h, 2, true);

// Quadruple (2x width, 2x height)
myPrinter.printBitmapOld(bitmap, w, h, 3, true);

// Not centered
myPrinter.printBitmapOld(bitmap, w, h, 0, false);
```

## Image Requirements

- **Width:** Up to 384 pixels (printer paper width)
- **Format:** BMP file (*.bmp)
- **Color:** Works with any color BMP - automatically converted to monochrome
  - Pixels with RGB > 128 are treated as white (background)
  - Darker pixels are treated as black (printed)
- **Recommended:** Create images at 384 pixels wide for best results

## Troubleshooting

### "Failed to connect" or "HTTP GET failed"
- Check WiFi connection
- Verify URL is accessible from your network
- For localhost URLs, ensure the server is running
- Try accessing the URL in a web browser first

### "Invalid BMP file"
- Ensure the file is actually a BMP (not renamed JPEG/PNG)
- Some BMP variants may not be supported
- Try re-saving the image as a standard BMP in an image editor

### "Failed to allocate bitmap buffer"
- Image is too large for ESP32 memory
- Try a smaller image
- Current limit: approximately 384x800 pixels

### Print quality issues
- Adjust heat settings: `myPrinter.setHeat(1, 224, 40);`
- Enable DTR pin for better timing: `myPrinter.enableDtr(dtrPin, LOW);`
- Check power supply - weak power causes faded prints

## Code Structure

The example is organized into sections:

1. **Configuration** - WiFi, URL, pins
2. **Helper Functions** - BMP binary reading utilities
3. **Main Function** - `downloadAndPrintBMP()` handles everything
4. **Setup** - Initializes WiFi and printer, triggers download

## Based On

- **PhotoTicket firmware** - Network download structure
- **miniweather firmware** - BMP parsing algorithm

## License

Same license as ThermalPrinter library.
