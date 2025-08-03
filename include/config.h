#ifndef CONFIG_H
#define CONFIG_H

// Hardware Configuration
#define RGB_LED_PIN 27
#define BUTTON_PIN 39
#define I2C_SDA_PIN 32
#define I2C_SCL_PIN 26
#define MPU6050_INT_PIN 33

// WiFi Configuration
#define WIFI_SSID "IhrWiFiName"
#define WIFI_PASSWORD "IhrWiFiPasswort"
#define HOSTNAME "seismograph"

// MQTT Configuration
#define MQTT_SERVER "192.168.0.0" // Replace with your MQTT broker IP
#define MQTT_PORT 1884
#define MQTT_CLIENT_ID "seismograph"
#define MQTT_USER "username"
#define MQTT_PASSWORD "password"

// MQTT Topics
#define TOPIC_DATA "tele/seismograph/data"
#define TOPIC_EVENT "tele/seismograph/event"
#define TOPIC_STATUS "tele/seismograph/status"
#define TOPIC_COMMAND "cmnd/seismograph/"

// MQTT Publishing Intervals
#define MQTT_DATA_INTERVAL 300000      // 5 minutes - Sensor data summary
#define MQTT_STATUS_INTERVAL 600000    // 10 minutes - Status updates
#define MQTT_HEARTBEAT_INTERVAL 1800000 // 30 minutes - Heartbeat
#define MQTT_EVENT_IMMEDIATE true      // Events sent immediately

// Seismograph Configuration
#define SAMPLING_RATE 500  // Hz - Increased for better seismic detection (Nyquist theorem: >2x highest frequency of interest)
#define SAMPLING_INTERVAL (1000 / SAMPLING_RATE)  // ms

// Event Detection Thresholds (in g) - Optimized for scientific accuracy
#define THRESHOLD_MICRO 0.001f    // Level 1: Mikrobewegungen (lowered for better sensitivity)
#define THRESHOLD_LIGHT 0.005f    // Level 2: Leichte Erschütterungen (adjusted for realistic detection)
#define THRESHOLD_STRONG 0.02f    // Level 3: Starke Erschütterungen (more realistic threshold)

// STA/LTA Configuration - Optimized for seismic event detection
#define STA_WINDOW 25     // Short-term average window (samples) - 0.05s at 500Hz
#define LTA_WINDOW 2500   // Long-term average window (samples) - 5s at 500Hz
#define STA_LTA_RATIO 2.5f // Trigger ratio (lowered for better sensitivity)

// Event Configuration
#define MIN_EVENT_DURATION 100  // ms
#define MAX_EVENTS_MEMORY 50    // Maximum events in memory

// Debug Configuration
#define DEBUG_MODE_TIMEOUT 3600000  // 1 hour in ms

// Data Storage Configuration
#define DATA_RETENTION_DAYS 90
#define LOG_LEVEL_INFO 1
#define LOG_LEVEL_DEBUG 2
#define LOG_LEVEL_ERROR 3

// NTP Configuration
#define NTP_SERVER1 "de.pool.ntp.org"
#define NTP_SERVER2 "pool.ntp.org"
#define NTP_SERVER3 "time.nist.gov"
#define NTP_SYNC_INTERVAL 3600000  // 1 hour in ms
#define TIMEZONE_OFFSET 0       // Zero offset for UTC

// Task Configuration
#define SENSOR_TASK_PRIORITY 3
#define BACKGROUND_TASK_PRIORITY 1
#define SENSOR_TASK_STACK_SIZE 4096
#define BACKGROUND_TASK_STACK_SIZE 8192

// Task Watchdog Configuration
#define TASK_WATCHDOG_TIMEOUT_S 30        // 30 seconds timeout (increased from default 5s)
#define ASYNC_TCP_STACK_SIZE 8192         // Increased stack size for async_tcp
#define ASYNC_TCP_PRIORITY 5              // Higher priority for async_tcp
#define WEB_SERVER_TASK_STACK_SIZE 8192   // Dedicated stack size for web server
#define MQTT_TASK_STACK_SIZE 6144         // Stack size for MQTT tasks

// Queue Configuration
#define SENSOR_DATA_QUEUE_SIZE 50
#define EVENT_QUEUE_SIZE 20

// Web Server Configuration
#define WEB_SERVER_PORT 80

// Performance Monitoring
#define HEALTH_CHECK_INTERVAL 5000  // ms
#define PERFORMANCE_LOG_INTERVAL 3600000  // 1 hour in ms

// Memory Management
#define MIN_FREE_HEAP 10000  // bytes
#define HEAP_CHECK_INTERVAL 1000  // ms

// OTA Configuration
#define OTA_HOSTNAME "seismograph"
#define OTA_PASSWORD "IhrOTAPasswort"
#define OTA_PORT 3232

// MPU6050 Constants
#define MPU6050_ACCEL_SCALE 16384.0f  // LSB/g for ±2g range
#define MPU6050_GYRO_SCALE 131.0f     // LSB/°/s for ±250°/s range

// Scientific Constants
#define RICHTER_SCALE_FACTOR 1000.0f  // Conversion factor for Richter scale approximation
#define LOCAL_MAGNITUDE_OFFSET 0.0f   // Local magnitude offset for calibration (corrected from 3.0f)

// Spike Filter Constants
#define SPIKE_MEDIAN_MULTIPLIER 5.0f  // Multiplier for median-based spike detection
#define SPIKE_THRESHOLD_MULTIPLIER 2.0f // Multiplier for threshold-based spike detection
#define SPIKE_FILTER_BUFFER_SIZE 5    // Size of magnitude buffer for spike detection

// Calibration Constants
#define CALIBRATION_SAMPLES 200       // Number of samples for calibration
#define STABILITY_CHECK_SAMPLES 50    // Samples for stability check
#define MAX_CALIBRATION_STDDEV 0.01f  // Maximum allowed standard deviation during calibration
#define MAX_XY_OFFSET 0.5f           // Maximum reasonable X/Y axis offset
#define MIN_Z_OFFSET 0.8f            // Minimum Z-axis offset (gravity)
#define MAX_Z_OFFSET 1.5f            // Maximum Z-axis offset (gravity)
#define MIN_Z_VALUE 0.8f             // Minimum Z-axis raw value
#define MAX_Z_VALUE 1.5f             // Maximum Z-axis raw value

// Drift Detection Constants
#define DRIFT_CHECK_INTERVAL 300000   // 5 minutes in ms
#define WARNING_DRIFT_PERCENT 20.0f   // Warning threshold for calibration drift
#define CRITICAL_DRIFT_PERCENT 50.0f  // Critical threshold for calibration drift
#define HIGH_BASELINE_THRESHOLD 0.1f  // High baseline warning threshold
#define MAX_CALIBRATION_AGE 86400000  // 24 hours in ms

#endif // CONFIG_H
