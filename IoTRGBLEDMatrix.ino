/**
 * File:    IoTRGBLEDMatrix.h
 * Author:  Karl Kangur <karl.kangur@gmail.com>
 * Licnece: GNU General Public License
 * URL:     https://github.com/Nurgak/IoT-RGB-LED-Matrix
 */

#include <WiFi.h>
#include <ArduinoOTA.h>
#include <PubSubClient.h>
#include "buffer.h"
#include "config.h"

// Double buffer to avoid flickering
Buffer matrix_a(ROWS * COLUMNS * 2 + 1);
Buffer matrix_b(ROWS * COLUMNS * 2 + 1);
Buffer * matrix_display = &matrix_b;

// Timer for display update callback
hw_timer_t * displayUpdateTimer = NULL;

// Forward declarations
void mqtt_callback(char* topic, byte* payload, unsigned int length);
void checkUpdate(void *pvParameter);
void drawRow();
void displayUpdate();

// Setup WiFi and MQTT client
WiFiClient espClient;
PubSubClient client(MQTT_SERVER, MQTT_PORT, mqtt_callback, espClient, matrix_a);

void setup()
{
  Serial.begin(115200);

  // Configure over-the-air updates
  ArduinoOTA.onStart([]()
  {
    // Start the over-the-air update

    // Disable the screen update timer, otherwise the upload will fail
    timerAlarmDisable(displayUpdateTimer);
    timerDetachInterrupt(displayUpdateTimer);
  });

  ArduinoOTA.onEnd([]()
  {
    // End
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total)
  {
    // Progress
  });

  ArduinoOTA.onError([](ota_error_t error)
  {
    // Error
  });
  
  ArduinoOTA.setHostname(HOST_NAME);
  ArduinoOTA.begin();

  pinMode(R1, OUTPUT);
  pinMode(G1, OUTPUT);
  pinMode(BL1, OUTPUT);
 
  pinMode(R2, OUTPUT);
  pinMode(G2, OUTPUT);
  pinMode(BL2, OUTPUT);

  pinMode(CH_A, OUTPUT);
  pinMode(CH_B, OUTPUT);
  pinMode(CH_C, OUTPUT);
  pinMode(CH_D, OUTPUT);
  
  pinMode(LAT, OUTPUT);
  pinMode(CLK, OUTPUT);
  pinMode(OE, OUTPUT);
  
  // MQTT message checking and OTA update check are done on core 0 (protocol CPU)
  xTaskCreatePinnedToCore(&checkUpdate, "checkUpdate", 2048, NULL, 1, NULL, 0);
  
  // Allocate memory for the incoming matrix buffer
  matrix_a.begin();
  matrix_b.begin();
  
  // Load white startup screen
  load_screen(screen_wifi_connect, 0xfff);
  
  // Screen updating is timing-critical so it must be hard real-time, use an interrupt
  displayUpdateTimer = timerBegin(0, 80, true);
  timerAttachInterrupt(displayUpdateTimer, &displayUpdate, true);
  // CLK: 80MHz
  // DIV: 80
  // INT: 1
  // MAX_ROW_LOOPS: 40
  // ROWS: 16 (two rows updated at the same time)
  // COLOR_DEPTH: 16
  // FPS: CLK/DIV/INT/MAX_ROW_LOOPS/ROWS/COLOR_DEPTH
  // 80000000/80/1/40/16/16 = 100Hz
  timerAlarmWrite(displayUpdateTimer, 1, true);

  // Enabling this immediately messes with the WiFi connection
  //timerAlarmEnable(displayUpdateTimer);
}

void load_screen(const uint32_t * screen, uint16_t mask)
{
  // Preload the startup image to the display matrix
  matrix_display->seek(0);
  uint8_t pixel;
  for(uint8_t y = 0; y < ROWS; ++y)
  {
    for(uint8_t x = 0; x < COLUMNS; ++x)
    {
      // Set every "on" pixel to white, apply the mask to set the color
      pixel = ((screen[y] >> (COLUMNS - 1 - x)) & 1);
      matrix_display->write(pixel ? (mask >> 8) : 0);
      matrix_display->write(pixel ? (mask & 0xff) : 0);
    }
  }
}

// MQTT callback function
void mqtt_callback(char* topic, byte* payload, unsigned int length)
{
  // Reset pointer of current display matrix
  matrix_display->seek(0);
  // Switch display matrix
  matrix_display = matrix_display == &matrix_b ? &matrix_a : &matrix_b;
  // Switch buffer matrix
  client.setStream(matrix_display == &matrix_b ? matrix_a : matrix_b);

  // Inform the broker that the frame has been loaded
  client.publish("frame_ready", "1");
}

// Update checking task running on to core 0 (Protocol CPU)
void checkUpdate(void *pvParameter)
//void checkUpdate(void *pvParameter)
{ 
  // Subscribe and check MQTT messages
  for(;;)
  {
    if(WiFi.status() != WL_CONNECTED)
    {
      // Disabling the timer alarm creates a boot-crash loop for some reason
      //timerAlarmDisable(displayUpdateTimer);
      
      Serial.print("Connecting to ");
      Serial.println(WIFI_SSID);
    
      WiFi.mode(WIFI_STA);
      WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

      uint8_t wait = 10;
      while(WiFi.status() != WL_CONNECTED && wait--)
      {
        vTaskDelay(500);
        Serial.print(".");
      }
      Serial.println("");

      // Still not connected
      if(WiFi.status() != WL_CONNECTED)
      {
        // Load a red cross
        load_screen(screen_mqtt_error, 0xf00);
        continue;
      }

      Serial.println("WiFi connected");
      Serial.print("IP address: ");
      Serial.println(WiFi.localIP());

      // Re-enable the display update timer
      timerAlarmEnable(displayUpdateTimer);
    }
    
    // MQTT routine
    if(!client.connected())
    {
      while(!client.connected())
      {
        Serial.println("Attempting MQTT connection...");
        load_screen(screen_mqtt_connect, 0xfff);
          
        // This might trigger a watchdog reset at it is a blocking function
#if defined(MQTT_USERNAME) && defined(MQTT_PASSWORD)
        if(client.connect(HOST_NAME, MQTT_USERNAME, MQTT_PASSWORD))
#else
        if(client.connect(HOST_NAME))
#endif
        {
          Serial.println("Connected");
          client.subscribe(MQTT_TOPIC_DISPLAY);
        }
        else
        {
          Serial.print("Failed to connect to MQTT broker, state: ");
          Serial.println(client.state());
          Serial.println("Trying again in 5 seconds");
          vTaskDelay(5000);
          
          load_screen(screen_mqtt_error, 0xf00);
        }
      }
    }

    // Take care of Over-The-Air updates
    ArduinoOTA.handle();
    
    client.loop();

    // Needed to feed the watchdog
    vTaskDelay(1);
  }
}

void drawRow()
{
  static uint8_t row = 0;
  static uint8_t layer = 0;
  static uint32_t gpio;
  static uint16_t address;
  
  // Turn off display (output enable high)
  GPIO.out_w1ts = ((uint32_t)1 << OE);

  // Select display rows (upper and lower halves)
  gpio = GPIO.out;
  (row & 1) ? gpio |= 1 << CH_A : gpio &= ~(1 << CH_A);
  (row & 1 << 1) ? gpio |= 1 << CH_B : gpio &= ~(1 << CH_B);
  (row & 1 << 2) ? gpio |= 1 << CH_C : gpio &= ~(1 << CH_C);
  (row & 1 << 3) ? gpio |= 1 << CH_D : gpio &= ~(1 << CH_D);
  GPIO.out = gpio;

  // Draw one row
  for(uint8_t column = 0; column < COLUMNS; ++column)
  {
    // Set the pixel value for the high half of the matrix, depending on color depth (layer)
    
    // Hardcoded for performance
    //address = (row * COLUMNS + column) << 1;
    address = ((row << 5) + column) << 1;
    (matrix_display->data[address] & 0xf) > layer ? gpio |= 1 << R1 : gpio &= ~(1 << R1);
    ((matrix_display->data[address + 1] >> 4) & 0xf) > layer ? gpio |= 1 << G1 : gpio &= ~(1 << G1);
    (matrix_display->data[address + 1] & 0xf) > layer ? gpio |= 1 << BL1 : gpio &= ~(1 << BL1);
    
    // Hardcoded for performance
    //address = ((row + 16) * COLUMNS + column) << 1;
    address = (((row + 16) << 5) + column) << 1;
    (matrix_display->data[address] & 0xf) > layer ? gpio |= 1 << R2 : gpio &= ~(1 << R2);
    ((matrix_display->data[address + 1] >> 4) & 0xf) > layer ? gpio |= 1 << G2 : gpio &= ~(1 << G2);
    (matrix_display->data[address + 1] & 0xf) > layer ? gpio |= 1 << BL2 : gpio &= ~(1 << BL2);

    GPIO.out = gpio;
    
    // Clock in the pixels
    GPIO.out_w1ts = ((uint32_t)1 << CLK);
    GPIO.out_w1tc = ((uint32_t)1 << CLK);
  }

  // Latch the pixels
  gpio |= 1 << LAT;
  GPIO.out = gpio;
  gpio &= ~(1 << LAT);
  GPIO.out = gpio;

  // Layering for color depth (color shades)
  if(layer < COLOR_DEPTH)
  {
    // Increase layer
    layer++;
  }
  else
  {
    // Proceed to next row
    layer = 0;
    row++;
    if(row >= 16)
    {
      row = 0;
    }
  }
}

// Display update routine, running on to core 1 (Application CPU), triggered by timer
void displayUpdate()
{
  static uint8_t loop_row = 0;
  
  if(loop_row == 0)
  {
    // Draw the row
    drawRow();
    // Enable output
    //digitalWrite(OE, LOW);
    GPIO.out_w1tc = ((uint32_t)1 << OE);
  }

  // Stall before drawing next row to increase brightness
  loop_row++;
  if(loop_row >= MAX_ROW_LOOPS)
  {
    loop_row = 0;
  }
}

void loop()
{
  // Empty loop, doesn't get called reliably (or at all) because the high update rate of the display hogs core 1
}

