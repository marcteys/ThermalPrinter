/*
 * Download BMP and Print - ThermalPrinter Example
 *
 * This example demonstrates:
 * - Downloading a BMP image from a URL
 * - Parsing BMP file format (1-bit, 4-bit, 8-bit indexed, and 24-bit RGB)
 * - Converting to thermal printer bitmap format
 * - Printing using printBitmapOld() function
 *
 * Based on:
 * - PhotoTicket firmware structure
 * - miniweather BMP parsing code
 *
 * Hardware: ESP32
 * Network: WiFi connection required
 */

#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include "TPrinter.h"

// ===================================
// CONFIGURATION
// ===================================

// WiFi credentials
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";

// BMP image URL
const char* bmpUrl = "http://localhost/phototicket/images/last.bmp";

// Printer pins (ESP32)
const byte rxPin = 16;
const byte txPin = 17;
const byte dtrPin = 14;  // optional
const int printerBaudrate = 9600;

// Printer dimensions
const uint16_t PRINTER_WIDTH = 384;  // 384 dots = 48 bytes

// ===================================
// GLOBALS
// ===================================

HardwareSerial printerSerial(2);
Tprinter myPrinter(&printerSerial, printerBaudrate);

// Buffers for BMP processing
uint8_t input_buffer[512];  // Stream reading buffer
uint8_t mono_palette_buffer[32];  // For indexed color BMPs
uint8_t color_palette_buffer[32];

// ===================================
// HELPER FUNCTIONS - BMP PARSING
// ===================================

uint16_t read16(WiFiClient& client) {
  uint16_t result;
  ((uint8_t*)&result)[0] = client.read();
  ((uint8_t*)&result)[1] = client.read();
  return result;  // Little-endian
}

uint32_t read32(WiFiClient& client) {
  uint32_t result;
  ((uint8_t*)&result)[0] = client.read();
  ((uint8_t*)&result)[1] = client.read();
  ((uint8_t*)&result)[2] = client.read();
  ((uint8_t*)&result)[3] = client.read();
  return result;  // Little-endian
}

uint32_t skip(WiFiClient& client, int32_t bytes) {
  int32_t remain = bytes;
  uint32_t start = millis();
  while ((client.connected() || client.available()) && (remain > 0)) {
    if (client.available()) {
      client.read();
      remain--;
    } else {
      delay(1);
    }
    if (millis() - start > 5000) break;  // 5 second timeout
  }
  return bytes - remain;
}

uint32_t read8n(WiFiClient& client, uint8_t* buffer, int32_t bytes) {
  int32_t remain = bytes;
  uint32_t start = millis();
  while ((client.connected() || client.available()) && (remain > 0)) {
    if (client.available()) {
      *buffer++ = (uint8_t)client.read();
      remain--;
    } else {
      delay(1);
    }
    if (millis() - start > 5000) break;  // 5 second timeout
  }
  return bytes - remain;
}

// ===================================
// MAIN BMP DOWNLOAD AND PRINT FUNCTION
// ===================================

void downloadAndPrintBMP(const char* url) {
  WiFiClient client;
  HTTPClient http;

  Serial.print("Downloading BMP from: ");
  Serial.println(url);

  if (!http.begin(client, url)) {
    Serial.println("Failed to connect!");
    return;
  }

  int httpCode = http.GET();
  if (httpCode != HTTP_CODE_OK) {
    Serial.printf("HTTP GET failed, code: %d\n", httpCode);
    http.end();
    return;
  }

  Serial.println("HTTP 200 OK - Starting BMP parse");

  // Get the stream
  WiFiClient* stream = http.getStreamPtr();

  // Read BMP signature (0x4D42 = "BM")
  uint16_t signature = read16(*stream);
  if (signature != 0x4D42) {
    Serial.println("Invalid BMP file! (signature mismatch)");
    http.end();
    return;
  }

  Serial.println("Valid BMP signature found");

  // Read BMP header
  uint32_t fileSize = read32(*stream);
  uint32_t creatorBytes = read32(*stream);
  uint32_t imageOffset = read32(*stream);
  uint32_t headerSize = read32(*stream);
  uint32_t width = read32(*stream);
  int32_t height = (int32_t)read32(*stream);
  uint16_t planes = read16(*stream);
  uint16_t depth = read16(*stream);
  uint32_t format = read32(*stream);

  Serial.printf("BMP Info:\n");
  Serial.printf("  Size: %lu bytes\n", fileSize);
  Serial.printf("  Dimensions: %lu x %d\n", width, abs(height));
  Serial.printf("  Bit depth: %u\n", depth);
  Serial.printf("  Format: %lu\n", format);

  // Check if format is supported
  if (planes != 1 || (format != 0 && format != 3)) {
    Serial.println("Unsupported BMP format!");
    http.end();
    return;
  }

  // Determine if BMP is stored bottom-up or top-down
  bool flip = (height > 0);
  if (height < 0) height = -height;

  // Limit width to printer width
  uint16_t w = min((uint16_t)width, PRINTER_WIDTH);
  uint16_t h = (uint16_t)height;

  Serial.printf("Processing size: %u x %u\n", w, h);

  // Calculate row size (padded to 4-byte boundary)
  uint32_t rowSize = (width * depth / 8 + 3) & ~3;
  if (depth < 8) rowSize = ((width * depth + 8 - depth) / 8 + 3) & ~3;

  uint8_t bitmask = 0xFF;
  uint8_t bitshift = 8 - depth;
  if (depth < 8) bitmask >>= depth;

  uint32_t bytes_read = 7 * 4 + 3 * 2;  // Bytes read so far

  // Read color palette for indexed color images
  if (depth <= 8) {
    bytes_read += skip(*stream, imageOffset - (4 << depth) - bytes_read);

    Serial.printf("Reading %u color palette entries...\n", 1 << depth);

    for (uint16_t pn = 0; pn < (1 << depth); pn++) {
      uint8_t blue = stream->read();
      uint8_t green = stream->read();
      uint8_t red = stream->read();
      stream->read();  // Skip alpha
      bytes_read += 4;

      // Classify color as white (background) or black (foreground)
      bool whitish = (red > 0x80) && (green > 0x80) && (blue > 0x80);

      if (pn % 8 == 0) {
        mono_palette_buffer[pn / 8] = 0;
      }
      mono_palette_buffer[pn / 8] |= whitish << (pn % 8);
    }
  }

  // Allocate bitmap buffer for thermal printer
  // Thermal printer format: 1 bit per pixel, MSB first
  uint32_t bitmap_size = ((uint32_t)w * h + 7) / 8;
  uint8_t* bitmap = (uint8_t*)malloc(bitmap_size);

  if (!bitmap) {
    Serial.println("Failed to allocate bitmap buffer!");
    http.end();
    return;
  }

  // Initialize to white (all bits set)
  memset(bitmap, 0xFF, bitmap_size);

  Serial.println("Processing image data...");

  // Position to start of image data
  uint32_t rowPosition = flip ? imageOffset + (height - h) * rowSize : imageOffset;
  bytes_read += skip(*stream, rowPosition - bytes_read);

  // Process each row
  for (uint16_t row = 0; row < h; row++) {
    if (!(stream->connected() || stream->available())) {
      Serial.printf("Connection lost at row %u\n", row);
      break;
    }

    if (row % 50 == 0) {
      Serial.printf("Row %u/%u\n", row, h);
    }

    uint32_t in_remain = rowSize;
    uint32_t in_idx = 0;
    uint32_t in_bytes = 0;
    uint8_t in_byte = 0;
    uint8_t in_bits = 0;

    // Calculate row offset in bitmap
    uint32_t row_offset = ((uint32_t)row * w) / 8;

    // Process each pixel in the row
    for (uint16_t col = 0; col < w; col++) {
      // Read more data if needed
      if (in_idx >= in_bytes) {
        uint32_t get = min(in_remain, (uint32_t)sizeof(input_buffer));
        uint32_t got = read8n(*stream, input_buffer, get);
        in_bytes = got;
        in_remain -= got;
        in_idx = 0;
        bytes_read += got;
      }

      bool whitish = false;

      // Decode pixel based on bit depth
      if (depth <= 8) {
        // Indexed color - lookup in palette
        if (in_bits == 0) {
          in_byte = input_buffer[in_idx++];
          in_bits = 8;
        }
        uint16_t pn = (in_byte >> bitshift) & bitmask;
        whitish = mono_palette_buffer[pn / 8] & (0x1 << (pn % 8));
        in_byte <<= depth;
        in_bits -= depth;
      } else if (depth == 24) {
        // RGB24 - read 3 bytes per pixel
        uint8_t blue = input_buffer[in_idx++];
        uint8_t green = input_buffer[in_idx++];
        uint8_t red = input_buffer[in_idx++];
        whitish = (red > 0x80) && (green > 0x80) && (blue > 0x80);
      }

      // Set pixel in bitmap (thermal printer format: 0=black, 1=white)
      if (!whitish) {
        uint32_t byte_idx = row_offset + (col / 8);
        uint8_t bit_mask = 0x80 >> (col % 8);
        bitmap[byte_idx] &= ~bit_mask;  // Set bit to 0 (black)
      }
    }
  }

  Serial.println("Image processing complete!");
  Serial.println("Printing...");

  // Print using printBitmapOld
  // Mode 0 = normal size, mode 1 = double width, mode 2 = double height, mode 3 = quadruple
  myPrinter.printBitmapOld(bitmap, w, h, 0, true);  // Normal size, centered

  // Free the bitmap buffer
  free(bitmap);

  Serial.println("Print complete!");
  http.end();
}

// ===================================
// SETUP
// ===================================

void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n================================");
  Serial.println("BMP Download & Print Example");
  Serial.println("ThermalPrinter Library");
  Serial.println("================================\n");

  // Initialize printer
  Serial.println("Initializing printer...");
  printerSerial.begin(printerBaudrate, SERIAL_8N1, rxPin, txPin);
  myPrinter.begin();

  // Optional: Enable DTR pin for better timing
  // myPrinter.enableDtr(dtrPin, LOW);

  // Optional: Set heat parameters for better print quality
  // myPrinter.setHeat(1, 224, 40);

  Serial.println("Printer ready!");

  // Connect to WiFi
  Serial.print("Connecting to WiFi");
  WiFi.mode(WIFI_STA);
  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());

  // Download and print the BMP
  Serial.println("\nStarting BMP download and print...");
  downloadAndPrintBMP(bmpUrl);

  // Feed some paper
  myPrinter.feed(3);

  Serial.println("\n================================");
  Serial.println("Done!");
  Serial.println("================================");
}

void loop() {
  // Nothing to do
}
