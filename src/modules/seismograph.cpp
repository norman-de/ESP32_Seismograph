#include "seismograph.h"
#include "dual_core_manager.h"
#include "time_manager.h"
#include "data_logger.h"

// Externe Referenz auf TimeManager
extern TimeManager timeManager;
// Externe Referenz auf DataLogger
extern DataLogger* globalDataLogger;

Seismograph::Seismograph() : mpu() {
    initialized = false;
    calibrated = false;
    
    // Initialize calibration offsets
    offsetX = 0.0f;
    offsetY = 0.0f;
    offsetZ = 0.0f;
    
    // Initialize STA/LTA buffers
    staIndex = 0;
    ltaIndex = 0;
    staSum = 0.0f;
    ltaSum = 0.0f;
    staFull = false;
    ltaFull = false;
    
    // Initialize event detection
    eventActive = false;
    eventStartTime = 0;
    eventMaxMagnitude = 0.0f;
    eventSumMagnitude = 0.0f;
    eventSampleCount = 0;
    
    // Initialize adaptive thresholds (disabled by default)
    adaptiveThresholdMicro = THRESHOLD_MICRO;
    adaptiveThresholdLight = THRESHOLD_LIGHT;
    adaptiveThresholdStrong = THRESHOLD_STRONG;
    backgroundNoise = 0.001f;
    lastAdaptiveUpdate = 0;
    adaptiveThresholdEnabled = true; // Enabled by default
    
    // Initialize spike filtering
    magnitudeIndex = 0;
    magnitudeBufferFull = false;
    for (int i = 0; i < 5; i++) {
        lastMagnitudes[i] = 0.0f;
    }
    
    // Initialize statistics
    totalSamples = 0;
    eventsDetected = 0;
    spikesFiltered = 0;
    lastMagnitude = 0.0f;
    
    // Initialize detailed logging
    detailedLoggingInterval = 5000; // Default: 5 seconds
    detailedLoggingEnabled = false;  // Disabled by default
    
    // Initialize enhanced calibration monitoring
    lastCalibrationOffsets[0] = 0.0f; // X
    lastCalibrationOffsets[1] = 0.0f; // Y
    lastCalibrationOffsets[2] = 0.0f; // Z
    lastCalibrationTime = 0;
    baselineLTA = 0.0f;
    lastDriftCheck = 0;
    calibrationValid = false;
    
    // Clear buffers
    for (int i = 0; i < STA_WINDOW; i++) {
        staBuffer[i] = 0.0f;
    }
    for (int i = 0; i < LTA_WINDOW; i++) {
        ltaBuffer[i] = 0.0f;
    }
}

bool Seismograph::begin() {
    Serial.println("Initializing MPU6050...");
    
    mpu.initialize();
    
    if (!mpu.testConnection()) {
        Serial.println("ERROR: MPU6050 connection failed");
        return false;
    }
    
    Serial.println("MPU6050 found, performing automatic sensor calibration...");
    Serial.println("Please ensure the sensor is on a stable, level surface during calibration...");
    
    // Wait a moment for sensor to stabilize
    delay(1000);
    
    // Perform automatic calibration
    if (!calibrate()) {
        Serial.println("WARNING: Automatic sensor calibration failed");
        Serial.println("System will continue with default calibration (no offsets)");
        Serial.println("Event detection may be less accurate until proper calibration");
        
        // Set default calibration values
        offsetX = 0.0f;
        offsetY = 0.0f;
        offsetZ = 0.0f;
        calibrated = false;
        calibrationValid = false;
    }
    
    initialized = true;
    
    if (calibrated) {
        Serial.println("MPU6050 initialized successfully with automatic calibration");
    } else {
        Serial.println("MPU6050 initialized with default calibration (uncalibrated mode)");
        Serial.println("Recommendation: Check sensor mounting and restart for proper calibration");
    }
    Serial.printf("Adaptive thresholds: %s (can be enabled via setAdaptiveThresholdEnabled(true))\n", 
                  adaptiveThresholdEnabled ? "Enabled" : "Disabled");
    
    return true;
}

bool Seismograph::calibrate() {
    Serial.println("Starting automatic sensor calibration...");
    
    if(detailedLoggingEnabled) {
        Serial.println("=== ENHANCED SENSOR CALIBRATION ===");
        Serial.println("Starting automatic sensor calibration with validation...");
    }
    
    // Enhanced calibration with more samples and stability checking
    const int samples = 200; // Increased from 100
    const int stabilityCheckSamples = 50;
    float readings[samples][3]; // Store all readings for analysis
    float sumX = 0, sumY = 0, sumZ = 0;
    int16_t ax, ay, az, gx, gy, gz;
    
    // First, check sensor stability over a short period
    if (detailedLoggingEnabled) Serial.println("Phase 1: Checking sensor stability...");
    float stabilityReadings[stabilityCheckSamples][3];
    
    for (int i = 0; i < stabilityCheckSamples; i++) {
        mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        stabilityReadings[i][0] = (float)ax / 16384.0f;
        stabilityReadings[i][1] = (float)ay / 16384.0f;
        stabilityReadings[i][2] = (float)az / 16384.0f;
        delay(20); // Longer delay for stability
    }
    
    // Calculate standard deviation for stability check
    float meanX = 0, meanY = 0, meanZ = 0;
    for (int i = 0; i < stabilityCheckSamples; i++) {
        meanX += stabilityReadings[i][0];
        meanY += stabilityReadings[i][1];
        meanZ += stabilityReadings[i][2];
    }
    meanX /= stabilityCheckSamples;
    meanY /= stabilityCheckSamples;
    meanZ /= stabilityCheckSamples;
    
    float stdDevX = 0, stdDevY = 0, stdDevZ = 0;
    for (int i = 0; i < stabilityCheckSamples; i++) {
        stdDevX += pow(stabilityReadings[i][0] - meanX, 2);
        stdDevY += pow(stabilityReadings[i][1] - meanY, 2);
        stdDevZ += pow(stabilityReadings[i][2] - meanZ, 2);
    }
    stdDevX = sqrt(stdDevX / stabilityCheckSamples);
    stdDevY = sqrt(stdDevY / stabilityCheckSamples);
    stdDevZ = sqrt(stdDevZ / stabilityCheckSamples);
    
    if (detailedLoggingEnabled) Serial.printf("Stability check - StdDev: X=%.6f, Y=%.6f, Z=%.6f g\n", stdDevX, stdDevY, stdDevZ);
    
    // Check if sensor is stable enough for calibration
    const float maxStdDev = 0.01f; // Maximum allowed standard deviation
    if (stdDevX > maxStdDev || stdDevY > maxStdDev || stdDevZ > maxStdDev) {
        Serial.println(">>> CALIBRATION FAILED: Sensor too unstable <<<");
        if (detailedLoggingEnabled) {
            Serial.println("Possible causes:");
            Serial.println("  - Sensor not on stable surface");
            Serial.println("  - Vibrations present");
            Serial.println("  - Mechanical interference");
            Serial.printf("  - Required stability: <%.3f g, Current: X=%.6f, Y=%.6f, Z=%.6f g\n", 
                          maxStdDev, stdDevX, stdDevY, stdDevZ);
        }
        calibrationValid = false;
        return false;
    }
    
    if (detailedLoggingEnabled) {
        Serial.println("✓ Sensor stability check passed");
        Serial.println("Phase 2: Collecting calibration samples...");
    }
    
    // Collect calibration samples
    for (int i = 0; i < samples; i++) {
        mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        
        // Convert to g units and store
        readings[i][0] = (float)ax / 16384.0f;
        readings[i][1] = (float)ay / 16384.0f;
        readings[i][2] = (float)az / 16384.0f;
        
        sumX += readings[i][0];
        sumY += readings[i][1];
        sumZ += readings[i][2];
        
        if (detailedLoggingEnabled && (i % 50 == 0)) {
            Serial.printf("Progress: %d/%d samples collected\n", i, samples);
        }
        delay(10);
    }
    
    // Calculate proposed offsets
    float proposedOffsetX = sumX / samples;
    float proposedOffsetY = sumY / samples;
    float proposedOffsetZ = sumZ / samples; // FIXED: Don't subtract 1g - we want calibrated Z to be 0g
    
    if (detailedLoggingEnabled) {
        Serial.println("Phase 3: Validating calibration values...");
        Serial.printf("Proposed offsets: X=%.6f, Y=%.6f, Z=%.6f g\n", 
                      proposedOffsetX, proposedOffsetY, proposedOffsetZ);
    }
    
    // Validate calibration values with separate limits for X/Y and Z axes
    const float maxOffsetXY = 0.5f; // Maximum reasonable offset for X/Y axes
    const float minZOffset = 0.8f;  // Minimum Z-axis offset (gravity)
    const float maxZOffset = 1.5f;  // Maximum Z-axis offset (gravity)
    const float minZValue = 0.8f;   // Minimum Z-axis raw value (should be close to 1g)
    const float maxZValue = 1.5f;   // Maximum Z-axis raw value
    
    float rawZMean = sumZ / samples;
    
    // Check if X/Y offsets are reasonable (should be small)
    if (abs(proposedOffsetX) > maxOffsetXY || abs(proposedOffsetY) > maxOffsetXY) {
        Serial.println(">>> CALIBRATION FAILED: Unreasonable X/Y offset values <<<");
        if (detailedLoggingEnabled) {
            Serial.printf("Maximum allowed X/Y offset: ±%.2f g\n", maxOffsetXY);
            Serial.printf("Calculated X/Y offsets: X=%.6f, Y=%.6f g\n", 
                          proposedOffsetX, proposedOffsetY);
            Serial.println("Possible causes:");
            Serial.println("  - Sensor not level/horizontal");
            Serial.println("  - Sensor orientation incorrect");
            Serial.println("  - Hardware malfunction");
        }
        calibrationValid = false;
        return false;
    }
    
    // Check if Z offset is reasonable (should be close to 1g for gravity)
    if (abs(proposedOffsetZ) < minZOffset || abs(proposedOffsetZ) > maxZOffset) {
        Serial.println(">>> CALIBRATION FAILED: Unreasonable Z-axis offset value <<<");
        if (detailedLoggingEnabled) {
            Serial.printf("Expected Z-axis offset: %.1f-%.1f g (gravity), Calculated: %.6f g\n", 
                          minZOffset, maxZOffset, proposedOffsetZ);
            Serial.printf("Raw Z-axis reading: %.6f g\n", rawZMean);
            Serial.println("Possible causes:");
            Serial.println("  - Sensor not horizontal (Z-axis not pointing up/down)");
            Serial.println("  - Wrong sensor orientation");
            Serial.println("  - Gravity vector not aligned with Z-axis");
            Serial.println("  - Hardware malfunction");
        }
        calibrationValid = false;
        return false;
    }
    
    // Check if Z-axis reading is reasonable (should be close to 1g when level)
    if (rawZMean < minZValue || rawZMean > maxZValue) {
        Serial.println(">>> CALIBRATION FAILED: Z-axis reading unreasonable <<<");
        if (detailedLoggingEnabled) {
            Serial.printf("Expected Z-axis: %.1f-%.1f g, Measured: %.6f g\n", minZValue, maxZValue, rawZMean);
            Serial.println("Possible causes:");
            Serial.println("  - Sensor not horizontal");
            Serial.println("  - Wrong sensor orientation");
            Serial.println("  - Gravity vector not aligned with Z-axis");
        }
        calibrationValid = false;
        return false;
    }
    
    // Compare with previous calibration if available
    if (detailedLoggingEnabled && lastCalibrationTime > 0) {
        float deltaX = abs(proposedOffsetX - lastCalibrationOffsets[0]);
        float deltaY = abs(proposedOffsetY - lastCalibrationOffsets[1]);
        float deltaZ = abs(proposedOffsetZ - lastCalibrationOffsets[2]);
        
        Serial.println("Comparison with previous calibration:");
        Serial.printf("Previous offsets: X=%.6f, Y=%.6f, Z=%.6f g\n", 
                      lastCalibrationOffsets[0], lastCalibrationOffsets[1], lastCalibrationOffsets[2]);
        Serial.printf("Offset changes: X=%.6f, Y=%.6f, Z=%.6f g\n", deltaX, deltaY, deltaZ);
        
        const float maxDrift = 0.1f; // Maximum allowed drift
        if (deltaX > maxDrift || deltaY > maxDrift || deltaZ > maxDrift) {
            Serial.println(">>> WARNING: Large calibration drift detected <<<");
            Serial.printf("Maximum expected drift: %.3f g\n", maxDrift);
            Serial.println("This may indicate:");
            Serial.println("  - Sensor mounting has changed");
            Serial.println("  - Temperature effects");
            Serial.println("  - Sensor aging/drift");
        }
    }
    
    // Apply calibration
    offsetX = proposedOffsetX;
    offsetY = proposedOffsetY;
    offsetZ = proposedOffsetZ;
    
    // Store calibration for future comparison
    lastCalibrationOffsets[0] = offsetX;
    lastCalibrationOffsets[1] = offsetY;
    lastCalibrationOffsets[2] = offsetZ;
    lastCalibrationTime = millis();
    
    calibrated = true;
    calibrationValid = true;
    
    Serial.println(">>> CALIBRATION SUCCESSFUL <<<");
    if (detailedLoggingEnabled) {
        Serial.printf("Final offsets: X=%.6f, Y=%.6f, Z=%.6f g\n", offsetX, offsetY, offsetZ);
        Serial.printf("Calibration timestamp: %lu ms\n", lastCalibrationTime);
    }
    
    // Test calibration by taking a few test readings
    if (detailedLoggingEnabled) Serial.println("Phase 4: Testing calibration...");
    float testSumMagnitude = 0;
    for (int i = 0; i < 10; i++) {
        mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
        float testX = ((float)ax / 16384.0f) - offsetX;
        float testY = ((float)ay / 16384.0f) - offsetY;
        float testZ = ((float)az / 16384.0f) - offsetZ;
        float testMagnitude = sqrt(testX*testX + testY*testY + testZ*testZ);
        testSumMagnitude += testMagnitude;
        delay(10);
    }
    
    float avgTestMagnitude = testSumMagnitude / 10;
    baselineLTA = avgTestMagnitude; // Store baseline for drift detection
    
    if (detailedLoggingEnabled) {
        Serial.printf("Post-calibration test magnitude: %.6f g\n", avgTestMagnitude);
    
        if (avgTestMagnitude > 0.1f) {
            Serial.println(">>> WARNING: High post-calibration magnitude <<<");
            Serial.printf("Expected: <0.1g, Measured: %.6f g\n", avgTestMagnitude);
            Serial.println("Calibration may not be optimal");
        } else {
            Serial.println("✓ Calibration test passed");
        }
        
        Serial.println("=== CALIBRATION COMPLETE ===\n");
    }
    return true;
}

SensorData Seismograph::readSensor() {
    SensorData data;
    data.timestamp = millis();
    
    if (!initialized) {
        data.accelX = data.accelY = data.accelZ = data.magnitude = 0.0f;
        return data;
    }
    
    int16_t ax, ay, az, gx, gy, gz;
    mpu.getMotion6(&ax, &ay, &az, &gx, &gy, &gz);
    
    // Convert to g units using MPU6050 scale constant - RAW VALUES
    float rawX = (float)ax / MPU6050_ACCEL_SCALE;
    float rawY = (float)ay / MPU6050_ACCEL_SCALE;
    float rawZ = (float)az / MPU6050_ACCEL_SCALE;
    float rawMagnitude = sqrt(rawX*rawX + rawY*rawY + rawZ*rawZ);
    
    // Apply calibration
    data.accelX = rawX - offsetX;
    data.accelY = rawY - offsetY;
    data.accelZ = rawZ - offsetZ;
    
    // Calculate magnitude after calibration
    data.magnitude = calculateMagnitude(data.accelX, data.accelY, data.accelZ);
    
    // Log raw vs calibrated values periodically for debugging
    static unsigned long lastRawLog = 0;
    if (detailedLoggingEnabled && (millis() - lastRawLog > detailedLoggingInterval)) {
        Serial.printf("=== RAW vs CALIBRATED COMPARISON ===\n");
        Serial.printf("RAW values: X=%.6f, Y=%.6f, Z=%.6f g (magnitude: %.6f g)\n", 
                      rawX, rawY, rawZ, rawMagnitude);
        Serial.printf("CALIBRATED values: X=%.6f, Y=%.6f, Z=%.6f g (magnitude: %.6f g)\n", 
                      data.accelX, data.accelY, data.accelZ, data.magnitude);
        Serial.printf("Applied offsets: X=%.6f, Y=%.6f, Z=%.6f g\n", 
                      offsetX, offsetY, offsetZ);
        Serial.printf("Magnitude reduction: %.6f g -> %.6f g (%.2f%% reduction)\n", 
                      rawMagnitude, data.magnitude, 
                      ((rawMagnitude - data.magnitude) / rawMagnitude) * 100.0f);
        Serial.println("=== RAW vs CALIBRATED END ===\n");
        lastRawLog = millis();
    }
    
    lastMagnitude = data.magnitude;
    totalSamples++;
    
    return data;
}

void Seismograph::processData(SensorData data) {
    // Detailed logging for debugging
    static unsigned long lastDetailedLog = 0;
    static unsigned long sampleCounter = 0;
    sampleCounter++;
    
    // Use configurable logging interval and enable/disable flag
    bool shouldLogDetails = detailedLoggingEnabled && 
                           (millis() - lastDetailedLog > detailedLoggingInterval);
    
    if (shouldLogDetails) {
        Serial.printf("=== SENSOR ANALYSIS (Sample #%lu) ===\n", sampleCounter);
        Serial.printf("Raw magnitude: %.6f g (AFTER calibration)\n", data.magnitude);
        Serial.printf("Raw components: X=%.6f, Y=%.6f, Z=%.6f g (AFTER calibration)\n", 
                      data.accelX, data.accelY, data.accelZ);
        Serial.printf("Calibration offsets: X=%.6f, Y=%.6f, Z=%.6f g\n", 
                      offsetX, offsetY, offsetZ);
        Serial.printf("Detailed logging: %s (interval: %lu ms)\n", 
                      detailedLoggingEnabled ? "ENABLED" : "DISABLED", detailedLoggingInterval);
        lastDetailedLog = millis();
    }
    
    // Apply spike filtering with detailed logging
    if (isSpikeFiltered(data.magnitude)) {
        spikesFiltered++;
        if (shouldLogDetails) {
            Serial.printf("SPIKE FILTERED: Magnitude %.6f g rejected\n", data.magnitude);
        }
        return; // Skip this sample
    }
    
    if (shouldLogDetails) {
        Serial.printf("Sample ACCEPTED: Magnitude %.6f g passed spike filter\n", data.magnitude);
    }
    
    // Update magnitude buffer for spike detection
    lastMagnitudes[magnitudeIndex] = data.magnitude;
    magnitudeIndex = (magnitudeIndex + 1) % 5;
    if (magnitudeIndex == 0) magnitudeBufferFull = true;
    
    // Update adaptive thresholds periodically
    updateAdaptiveThresholds();
    
    // Update STA/LTA algorithm
    updateSTALTA(data.magnitude);
    
    // Log STA/LTA analysis
    if (shouldLogDetails && staFull && ltaFull) {
        float sta = staSum / STA_WINDOW;
        float lta = ltaSum / LTA_WINDOW;
        float ratio = (lta > 0) ? sta / lta : 0;
        Serial.printf("STA/LTA Analysis: STA=%.6f, LTA=%.6f, Ratio=%.2f (Trigger at %.2f)\n", 
                      sta, lta, ratio, STA_LTA_RATIO);
        
        if (ratio > STA_LTA_RATIO) {
            Serial.printf(">>> STA/LTA TRIGGER CONDITION MET! <<<\n");
        } else {
            Serial.printf("STA/LTA below trigger threshold\n");
        }
    }
    
    // Check for event trigger with detailed logging
    bool triggerCondition = checkEventTrigger();
    if (triggerCondition) {
        if (!eventActive) {
            if (shouldLogDetails) {
                Serial.printf(">>> NEW EVENT TRIGGERED! <<<\n");
            }
            startEvent(data.magnitude);
        } else {
            // Update ongoing event
            if (data.magnitude > eventMaxMagnitude) {
                eventMaxMagnitude = data.magnitude;
                if (shouldLogDetails) {
                    Serial.printf("Event magnitude updated: %.6f g (new max)\n", data.magnitude);
                }
            }
            eventSumMagnitude += data.magnitude;
            eventSampleCount++;
        }
    } else if (eventActive) {
        // Check if event should end
        unsigned long eventDuration = millis() - eventStartTime;
        if (eventDuration >= MIN_EVENT_DURATION) {
            if (shouldLogDetails) {
                Serial.printf("Event ending: Duration %lu ms >= minimum %d ms\n", 
                              eventDuration, MIN_EVENT_DURATION);
            }
            endEvent();
        } else if (shouldLogDetails) {
            Serial.printf("Event continues: Duration %lu ms < minimum %d ms\n", 
                          eventDuration, MIN_EVENT_DURATION);
        }
    }
    
    // Check for calibration drift periodically
    checkCalibrationDrift();
    
    // Log threshold comparison
    if (shouldLogDetails) {
        Serial.printf("Threshold Analysis:\n");
        Serial.printf("  Magnitude: %.6f g\n", data.magnitude);
        Serial.printf("  Micro threshold (%.6f g): %s\n", THRESHOLD_MICRO, 
                      data.magnitude >= THRESHOLD_MICRO ? "EXCEEDED" : "below");
        Serial.printf("  Light threshold (%.6f g): %s\n", THRESHOLD_LIGHT, 
                      data.magnitude >= THRESHOLD_LIGHT ? "EXCEEDED" : "below");
        Serial.printf("  Strong threshold (%.6f g): %s\n", THRESHOLD_STRONG, 
                      data.magnitude >= THRESHOLD_STRONG ? "EXCEEDED" : "below");
        
        // Add calibration status to detailed logging
        Serial.printf("Calibration Status:\n");
        Serial.printf("  Calibration valid: %s\n", calibrationValid ? "YES" : "NO");
        Serial.printf("  Calibration age: %lu ms\n", millis() - lastCalibrationTime);
        if (ltaFull && baselineLTA > 0) {
            float currentLTA = ltaSum / LTA_WINDOW;
            float driftPercent = ((currentLTA - baselineLTA) / baselineLTA) * 100.0f;
            Serial.printf("  Baseline LTA: %.6f g\n", baselineLTA);
            Serial.printf("  Current LTA: %.6f g\n", currentLTA);
            Serial.printf("  Drift: %.2f%%\n", driftPercent);
        }
        Serial.println("=== END ANALYSIS ===\n");
    }
}

void Seismograph::simulateEvent(float richterMagnitude) {
    // Convert Richter magnitude to realistic PGA using inverse formula
    float realisticPGA = calculatePGAFromRichter(richterMagnitude);
    
    Serial.printf("Simulating seismic event: Richter %.2f -> PGA %.6f g\n", richterMagnitude, realisticPGA);
    
    // Force trigger an event directly for simulation
    if (!eventActive) {
        startEvent(realisticPGA);
        
        // Simulate realistic event duration based on Richter magnitude
        unsigned long simulatedDuration = calculateEventDuration(richterMagnitude);
        
        // Simulate event duration with multiple samples
        for (int i = 0; i < 10; i++) {
            float sampleMagnitude = realisticPGA * (0.8f + (i * 0.02f)); // Vary magnitude slightly
            if (sampleMagnitude > eventMaxMagnitude) {
                eventMaxMagnitude = sampleMagnitude;
            }
            eventSumMagnitude += sampleMagnitude;
            eventSampleCount++;
        }
        
        // Simulate the passage of time for realistic duration
        delay(simulatedDuration / 10); // Brief delay to simulate event duration
        
        // End the event - this will trigger the scientific analysis
        endEvent();
    }
    
    // Also process the data normally for STA/LTA algorithm updates
    SensorData simulatedData;
    simulatedData.timestamp = millis();
    simulatedData.accelX = realisticPGA * 0.6f;
    simulatedData.accelY = realisticPGA * 0.3f;
    simulatedData.accelZ = realisticPGA * 0.1f;
    simulatedData.magnitude = realisticPGA;
    
    // Process the simulated data for algorithm updates
    processData(simulatedData);
}

float Seismograph::calculateMagnitude(float x, float y, float z) {
    return sqrt(x * x + y * y + z * z);
}

void Seismograph::updateSTALTA(float magnitude) {
    // Update STA (Short-Term Average)
    staSum -= staBuffer[staIndex];
    staBuffer[staIndex] = magnitude;
    staSum += magnitude;
    staIndex = (staIndex + 1) % STA_WINDOW;
    if (staIndex == 0) staFull = true;
    
    // Update LTA (Long-Term Average)
    ltaSum -= ltaBuffer[ltaIndex];
    ltaBuffer[ltaIndex] = magnitude;
    ltaSum += magnitude;
    ltaIndex = (ltaIndex + 1) % LTA_WINDOW;
    if (ltaIndex == 0) ltaFull = true;
}

bool Seismograph::checkEventTrigger() {
    if (!staFull || !ltaFull) return false;
    
    float sta = staSum / STA_WINDOW;
    float lta = ltaSum / LTA_WINDOW;
    
    if (lta == 0) return false;
    
    float ratio = sta / lta;
    return ratio > STA_LTA_RATIO;
}

void Seismograph::startEvent(float magnitude) {
    eventActive = true;
    eventStartTime = millis();
    eventMaxMagnitude = magnitude;
    eventSumMagnitude = magnitude;
    eventSampleCount = 1;
    
    int level = classifyEvent(magnitude);
    Serial.printf("Seismic event detected! Level: %d, Magnitude: %.4f g\n", level, magnitude);
}

void Seismograph::endEvent() {
    if (!eventActive) return;
    
    unsigned long eventDuration = millis() - eventStartTime;
    float avgMagnitude = eventSumMagnitude / eventSampleCount;
    int level = classifyEvent(eventMaxMagnitude);
    
    eventsDetected++;
    eventActive = false;
    
    Serial.printf("Event ended. Duration: %lu ms, Max: %.4f g, Avg: %.4f g, Level: %d\n",
                  eventDuration, eventMaxMagnitude, avgMagnitude, level);
    
    // Create event description with scientific analysis - use Richter-based classification
    float richter = calculateRichterMagnitude(eventMaxMagnitude);
    String eventType = getEventTypeFromRichter(richter);
    String description = getScientificEventDescription(eventMaxMagnitude, eventDuration);
    
    // Add traditional description for compatibility
    description += " | Traditional: Duration=" + String(eventDuration) + 
                   "ms, Max=" + String(eventMaxMagnitude, 4) + "g, Avg=" + 
                   String(avgMagnitude, 4) + "g";
    
    if (detailedLoggingEnabled) {
        // KRITISCH: Prüfe NTP-Zeit vor Event-Weiterleitung mit detailliertem Logging
        Serial.printf("=== EVENT VALIDATION ===\n");
        Serial.printf("Event Type: %s\n", eventType.c_str());
        Serial.printf("Max Magnitude: %.6f g\n", eventMaxMagnitude);
        Serial.printf("Avg Magnitude: %.6f g\n", avgMagnitude);
        Serial.printf("Duration: %lu ms\n", eventDuration);
        Serial.printf("Sample Count: %d\n", eventSampleCount);
        Serial.printf("Classification Level: %d\n", level);
    }
    
    // Detaillierte NTP-Zeit-Validierung
    bool ntpTimeValid = timeManager.isTimeValid();
    if (detailedLoggingEnabled) Serial.printf("NTP Time Validation: %s\n", ntpTimeValid ? "VALID" : "INVALID");
    
    if (!ntpTimeValid) {
        Serial.println(">>> EVENT REJECTED: NTP time not valid <<<");
        if (detailedLoggingEnabled) {
            Serial.println("Reasons for NTP time invalidity:");
            Serial.printf("  - System may not be synchronized with NTP server\n");
            Serial.printf("  - Network connectivity issues\n");
            Serial.printf("  - Time integrity requirements not met\n");
            Serial.printf("Event details: %s, Magnitude: %.6f g, Duration: %lu ms\n", 
                          eventType.c_str(), eventMaxMagnitude, eventDuration);
            Serial.printf("Boot time would be: %lu ms\n", millis());
            Serial.println("=== EVENT VALIDATION FAILED ===\n");
        }
        return; // Event wird nicht weitergeleitet ohne gültige NTP-Zeit
    }
    
    if (detailedLoggingEnabled) {
        // NTP-Zeit ist gültig - Event wird weitergeleitet
        Serial.printf(">>> EVENT ACCEPTED: NTP time is valid <<<\n");
        Serial.printf("Current NTP time: %s\n", timeManager.getFormattedDateTime().c_str());
        Serial.printf("Epoch timestamp: %lu\n", timeManager.getEpochTime());
    }
    
    // Create comprehensive seismic event with scientific data
    createSeismicEvent(eventMaxMagnitude, eventDuration, "seismograph_detection");
    
    // Send event to dual core manager for processing (nur mit gültiger NTP-Zeit)
    if (globalCoreManager != nullptr) {
        EventPacket eventPacket;
        eventPacket.eventType = eventType;
        eventPacket.magnitude = eventMaxMagnitude;
        eventPacket.level = level;
        // Verwende NTP-validierte Zeit statt Boot-Zeit
        eventPacket.timestamp = timeManager.getEpochTime() * 1000; // Convert to milliseconds
        globalCoreManager->sendEvent(eventPacket);
        
        if (detailedLoggingEnabled) {
            Serial.printf(">>> EVENT FORWARDED SUCCESSFULLY <<<\n");
            Serial.printf("Forwarded with NTP-validated timestamp: %s\n", 
                          timeManager.getFormattedDateTime().c_str());
            Serial.printf("Timestamp (ms): %lu\n", eventPacket.timestamp);
        }
    } else {
        Serial.println("WARNING: globalCoreManager is null - Event not forwarded");
    }
    if (detailedLoggingEnabled) Serial.println("=== EVENT VALIDATION COMPLETE ===\n");
}

int Seismograph::classifyEvent(float magnitude) {
    // Use Richter-scale based classification (6 levels)
    float richter = calculateRichterMagnitude(magnitude);
    
    if (richter >= 7.0f) {
        return 6; // Major
    } else if (richter >= 6.0f) {
        return 5; // Strong
    } else if (richter >= 5.0f) {
        return 4; // Moderate
    } else if (richter >= 4.0f) {
        return 3; // Light
    } else if (richter >= 2.0f) {
        return 2; // Minor
    } else {
        return 1; // Micro
    }
}

String Seismograph::getEventTypeFromRichter(float richter) {
    if (richter >= 7.0f) return "Major";
    if (richter >= 6.0f) return "Strong";
    if (richter >= 5.0f) return "Moderate";
    if (richter >= 4.0f) return "Light";
    if (richter >= 2.0f) return "Minor";
    return "Micro";
}

void Seismograph::updateAdaptiveThresholds() {
    // Skip adaptive threshold updates if disabled
    if (!adaptiveThresholdEnabled) {
        return;
    }
    
    unsigned long currentTime = millis();
    
    // Update adaptive thresholds every 30 seconds
    if (currentTime - lastAdaptiveUpdate < 30000) {
        return;
    }
    lastAdaptiveUpdate = currentTime;
    
    if (!ltaFull) return;
    
    // Calculate current background noise level
    float lta = ltaSum / LTA_WINDOW;
    
    // Validate LTA value to prevent NaN
    if (isnan(lta) || lta < 0.0001f) {
        lta = 0.001f; // Set minimum background noise
    }
    
    backgroundNoise = lta;
    
    // Adapt thresholds based on background noise with safety checks
    float adaptationFactor = 1.0f;
    if (THRESHOLD_MICRO > 0.0f && backgroundNoise > 0.0f) {
        adaptationFactor = 1.0f + (backgroundNoise / THRESHOLD_MICRO);
    }
    
    // Ensure adaptation factor is reasonable
    adaptationFactor = constrain(adaptationFactor, 0.5f, 3.0f);
    
    adaptiveThresholdMicro = THRESHOLD_MICRO * adaptationFactor;
    adaptiveThresholdLight = THRESHOLD_LIGHT * adaptationFactor;
    adaptiveThresholdStrong = THRESHOLD_STRONG * adaptationFactor;
    
    // Validate calculated thresholds
    if (isnan(adaptiveThresholdMicro) || adaptiveThresholdMicro <= 0.0f) {
        adaptiveThresholdMicro = THRESHOLD_MICRO;
    }
    if (isnan(adaptiveThresholdLight) || adaptiveThresholdLight <= 0.0f) {
        adaptiveThresholdLight = THRESHOLD_LIGHT;
    }
    if (isnan(adaptiveThresholdStrong) || adaptiveThresholdStrong <= 0.0f) {
        adaptiveThresholdStrong = THRESHOLD_STRONG;
    }
    
    // Limit adaptation to reasonable bounds
    adaptiveThresholdMicro = constrain(adaptiveThresholdMicro, THRESHOLD_MICRO * 0.5f, THRESHOLD_MICRO * 3.0f);
    adaptiveThresholdLight = constrain(adaptiveThresholdLight, THRESHOLD_LIGHT * 0.5f, THRESHOLD_LIGHT * 3.0f);
    adaptiveThresholdStrong = constrain(adaptiveThresholdStrong, THRESHOLD_STRONG * 0.5f, THRESHOLD_STRONG * 3.0f);
    
    if (detailedLoggingEnabled) {
        Serial.printf("Adaptive thresholds updated: Micro=%.4f, Light=%.4f, Strong=%.4f (Background=%.4f, Factor=%.2f)\n",
                      adaptiveThresholdMicro, adaptiveThresholdLight, adaptiveThresholdStrong, backgroundNoise, adaptationFactor);
    }
}

bool Seismograph::isSpikeFiltered(float magnitude) {
    if (!magnitudeBufferFull) {
        // Log why spike filtering is not active yet
        static unsigned long lastBufferLog = 0;
        if (detailedLoggingEnabled && (millis() - lastBufferLog > 10000)) { // Log every 10 seconds
            Serial.printf("Spike filter not active: Buffer not full yet (%d/5 samples)\n", magnitudeIndex);
            lastBufferLog = millis();
        }
        return false;
    }
    
    // Get median of last 5 readings
    float median = getMedianMagnitude();
    
    // Use appropriate threshold based on adaptive mode
    float thresholdMicro = adaptiveThresholdEnabled ? adaptiveThresholdMicro : THRESHOLD_MICRO;
    
    // Calculate spike detection criteria using defined constants
    float medianMultiplier = SPIKE_MEDIAN_MULTIPLIER;
    float thresholdMultiplier = SPIKE_THRESHOLD_MULTIPLIER;
    bool exceedsMedian = magnitude > median * medianMultiplier;
    bool exceedsThreshold = magnitude > thresholdMicro * thresholdMultiplier;
    
    // Detailed logging for spike analysis
    static unsigned long lastSpikeAnalysisLog = 0;
    bool shouldLogSpikeAnalysis = detailedLoggingEnabled && (millis() - lastSpikeAnalysisLog > 5000);
    
    if (shouldLogSpikeAnalysis) {
        Serial.printf("--- SPIKE FILTER ANALYSIS ---\n");
        Serial.printf("Current magnitude: %.6f g\n", magnitude);
        Serial.printf("Median of last 5: %.6f g\n", median);
        Serial.printf("Last 5 magnitudes: ");
        for (int i = 0; i < 5; i++) {
            Serial.printf("%.6f ", lastMagnitudes[i]);
        }
        Serial.println();
        Serial.printf("Spike criteria:\n");
        Serial.printf("  > %.1fx median (%.6f g): %s\n", medianMultiplier, median * medianMultiplier, exceedsMedian ? "YES" : "no");
        Serial.printf("  > %.1fx micro threshold (%.6f g): %s\n", thresholdMultiplier, thresholdMicro * thresholdMultiplier, exceedsThreshold ? "YES" : "no");
        lastSpikeAnalysisLog = millis();
    }
    
    // Check if current magnitude is a spike
    if (exceedsMedian && exceedsThreshold) {
        if (detailedLoggingEnabled) {
            Serial.printf(">>> SPIKE DETECTED AND FILTERED <<<\n");
            Serial.printf("Magnitude %.6f g > %.1fx median (%.6f g) AND > %.1fx threshold (%.6f g)\n", 
                          magnitude, medianMultiplier, median * medianMultiplier, 
                          thresholdMultiplier, thresholdMicro * thresholdMultiplier);
        }
        return true;
    }
    
    if (shouldLogSpikeAnalysis) {
        Serial.printf("Sample passes spike filter\n");
        Serial.printf("--- END SPIKE ANALYSIS ---\n");
    }
    
    return false;
}

float Seismograph::getMedianMagnitude() {
    // Optimized median calculation for 5 elements using partial sorting
    float sorted[5];
    for (int i = 0; i < 5; i++) {
        sorted[i] = lastMagnitudes[i];
    }
    
    // Find median using selection algorithm - only need to find 3rd smallest
    for (int i = 0; i < 3; i++) {
        int minIdx = i;
        for (int j = i + 1; j < 5; j++) {
            if (sorted[j] < sorted[minIdx]) {
                minIdx = j;
            }
        }
        if (minIdx != i) {
            float temp = sorted[i];
            sorted[i] = sorted[minIdx];
            sorted[minIdx] = temp;
        }
    }
    
    return sorted[2]; // Return median (3rd smallest)
}

void Seismograph::checkCalibrationDrift() {
    // Check calibration drift every 5 minutes
    const unsigned long driftCheckInterval = 300000; // 5 minutes
    
    if (millis() - lastDriftCheck < driftCheckInterval) {
        return;
    }
    
    lastDriftCheck = millis();
    
    // Only check if we have valid calibration and LTA is available
    if (!calibrationValid || !ltaFull || baselineLTA <= 0) {
        return;
    }
    
    float currentLTA = ltaSum / LTA_WINDOW;
    float driftPercent = ((currentLTA - baselineLTA) / baselineLTA) * 100.0f;
    float absDriftPercent = abs(driftPercent);
    
    // Define drift warning thresholds
    const float warningDriftPercent = 20.0f;  // 20% drift warning
    const float criticalDriftPercent = 50.0f; // 50% drift critical
    const float highBaselineThreshold = 0.1f; // High baseline warning
    
    if (detailedLoggingEnabled) {
        // Log drift status
        Serial.printf("=== CALIBRATION DRIFT CHECK ===\n");
        Serial.printf("Baseline LTA: %.6f g\n", baselineLTA);
        Serial.printf("Current LTA: %.6f g\n", currentLTA);
        Serial.printf("Drift: %.2f%%\n", driftPercent);
        Serial.printf("Calibration age: %lu minutes\n", (millis() - lastCalibrationTime) / 60000);
        
        // Check for high baseline
        if (currentLTA > highBaselineThreshold) {
            Serial.println(">>> WARNING: HIGH BASELINE DETECTED <<<");
            Serial.printf("Current baseline (%.6f g) exceeds threshold (%.6f g)\n", 
                          currentLTA, highBaselineThreshold);
            Serial.println("Possible causes:");
            Serial.println("  - Sensor mounting has shifted");
            Serial.println("  - Continuous vibrations present");
            Serial.println("  - Calibration needs to be refreshed");
            Serial.println("  - Environmental changes (temperature, etc.)");
        }
    }
    
    // Check drift levels
    if (absDriftPercent > criticalDriftPercent) {
        if (detailedLoggingEnabled) {
            Serial.println(">>> CRITICAL: SEVERE CALIBRATION DRIFT <<<");
            Serial.printf("Drift of %.2f%% exceeds critical threshold (%.1f%%)\n", 
                          driftPercent, criticalDriftPercent);
            Serial.println("IMMEDIATE ACTION REQUIRED:");
            Serial.println("  - Recalibration strongly recommended");
            Serial.println("  - Check sensor mounting stability");
            Serial.println("  - Verify environmental conditions");
        }
        calibrationValid = false; // Mark calibration as invalid
    } else if (absDriftPercent > warningDriftPercent) {
        if (detailedLoggingEnabled) {
            Serial.println(">>> WARNING: CALIBRATION DRIFT DETECTED <<<");
            Serial.printf("Drift of %.2f%% exceeds warning threshold (%.1f%%)\n", 
                          driftPercent, warningDriftPercent);
            Serial.println("Recommended actions:");
            Serial.println("  - Monitor drift trend");
            Serial.println("  - Consider recalibration if drift continues");
            Serial.println("  - Check for environmental changes");
        }
    } else {
        if (detailedLoggingEnabled) Serial.printf("✓ Calibration drift within acceptable range (%.2f%%)\n", driftPercent);
    }
    
    // Additional checks for calibration validity
    if (calibrationValid && detailedLoggingEnabled) {
        // Check if calibration is very old (more than 24 hours)
        unsigned long calibrationAge = millis() - lastCalibrationTime;
        const unsigned long maxCalibrationAge = 24 * 60 * 60 * 1000; // 24 hours
        
        if (calibrationAge > maxCalibrationAge) {
            Serial.println(">>> INFO: OLD CALIBRATION DETECTED <<<");
            Serial.printf("Calibration is %lu hours old\n", calibrationAge / (60 * 60 * 1000));
            Serial.println("Consider recalibration for optimal accuracy");
        }
        
        // Check for NaN or invalid LTA values
        if (isnan(currentLTA) || currentLTA < 0) {
            Serial.println(">>> ERROR: INVALID LTA VALUES <<<");
            Serial.println("Sensor readings may be corrupted");
            calibrationValid = false;
        }
    }
    
    if (detailedLoggingEnabled) Serial.println("=== DRIFT CHECK COMPLETE ===\n");
}

float Seismograph::calculateRichterMagnitude(float acceleration) {
    // Simplified Richter magnitude calculation based on peak ground acceleration
    // Formula: M = log10(PGA * 1000) + C
    // Where PGA is in g and C is a calibration constant
    
    if (acceleration <= 0.0f) return -10.0f; // Invalid reading
    
    // Convert acceleration to mm/s² (1g = 9806.65 mm/s²)
    float pga_mm_s2 = acceleration * 9806.65f;
    
    // Apply logarithmic scale with calibration offset
    float magnitude = log10(pga_mm_s2) - LOCAL_MAGNITUDE_OFFSET;
    
    // Constrain to reasonable Richter scale range (-2 to 10)
    magnitude = constrain(magnitude, -2.0f, 10.0f);
    
    return magnitude;
}

float Seismograph::calculateLocalMagnitude(float acceleration) {
    // Local magnitude (ML) calculation for regional seismic networks
    // This is a simplified approximation for single-station measurements
    
    if (acceleration <= 0.0f) return -10.0f;
    
    // Convert to velocity amplitude approximation (simplified)
    // Assuming typical frequency content around 1-10 Hz
    float velocity_approx = acceleration / (2.0f * PI * 5.0f); // Assume 5 Hz dominant frequency
    
    // Local magnitude formula: ML = log10(A) + distance_correction + station_correction
    // For single station, we use simplified version without distance correction
    float local_magnitude = log10(velocity_approx * 1000000.0f) - 2.0f; // Convert to micrometers
    
    // Apply local calibration offset
    local_magnitude -= LOCAL_MAGNITUDE_OFFSET;
    
    // Constrain to reasonable range
    local_magnitude = constrain(local_magnitude, -3.0f, 8.0f);
    
    return local_magnitude;
}

String Seismograph::getScientificEventDescription(float magnitude, unsigned long duration) {
    float richter = calculateRichterMagnitude(magnitude);
    float local_mag = calculateLocalMagnitude(magnitude);
    
    String description = "PGA=" + String(magnitude, 6) + "g, ";
    description += "Est.Richter=" + String(richter, 2) + ", ";
    description += "Local.Mag=" + String(local_mag, 2) + ", ";
    description += "Duration=" + String(duration) + "ms";
    
    // Add intensity classification based on Richter scale
    if (richter < 2.0f) {
        description += " (Micro-earthquake)";
    } else if (richter < 4.0f) {
        description += " (Minor earthquake)";
    } else if (richter < 5.0f) {
        description += " (Light earthquake)";
    } else if (richter < 6.0f) {
        description += " (Moderate earthquake)";
    } else if (richter < 7.0f) {
        description += " (Strong earthquake)";
    } else {
        description += " (Major earthquake)";
    }
    
    return description;
}

void Seismograph::printStats() {
    Serial.println("=== Seismograph Statistics ===");
    Serial.printf("Total samples: %lu\n", totalSamples);
    Serial.printf("Events detected: %lu\n", eventsDetected);
    Serial.printf("Spikes filtered: %lu\n", spikesFiltered);
    Serial.printf("Last magnitude: %.4f g\n", lastMagnitude);
    Serial.printf("Background noise: %.4f g\n", backgroundNoise);
    Serial.printf("Calibrated: %s\n", calibrated ? "Yes" : "No");
    Serial.printf("Calibration valid: %s\n", calibrationValid ? "Yes" : "No");
    Serial.printf("Event active: %s\n", eventActive ? "Yes" : "No");
    Serial.printf("Adaptive thresholds: %s\n", adaptiveThresholdEnabled ? "Enabled" : "Disabled");
    
    if (adaptiveThresholdEnabled) {
        Serial.printf("Adaptive values: Micro=%.4f, Light=%.4f, Strong=%.4f\n",
                      adaptiveThresholdMicro, adaptiveThresholdLight, adaptiveThresholdStrong);
    } else {
        Serial.printf("Fixed thresholds: Micro=%.4f, Light=%.4f, Strong=%.4f\n",
                      THRESHOLD_MICRO, THRESHOLD_LIGHT, THRESHOLD_STRONG);
    }
    
    if (staFull && ltaFull) {
        float sta = staSum / STA_WINDOW;
        float lta = ltaSum / LTA_WINDOW;
        float ratio = (lta > 0) ? sta / lta : 0;
        Serial.printf("STA/LTA ratio: %.2f (trigger at %.2f)\n", ratio, STA_LTA_RATIO);
        
        if (baselineLTA > 0) {
            float driftPercent = ((lta - baselineLTA) / baselineLTA) * 100.0f;
            Serial.printf("Calibration drift: %.2f%% (baseline: %.6f g)\n", driftPercent, baselineLTA);
        }
    }
    
    if (lastCalibrationTime > 0) {
        Serial.printf("Last calibration: %lu minutes ago\n", (millis() - lastCalibrationTime) / 60000);
    }
}

void Seismograph::createSeismicEvent(float magnitude, unsigned long duration, const String& source) {
    // Nur Events mit gültiger NTP-Zeit erstellen
    if (!timeManager.isTimeValid()) {
        if (detailedLoggingEnabled) {
            Serial.println("CRITICAL: Cannot create seismic event - NTP time not valid");
        }
        return;
    }
    
    // Erstelle vollständige SeismicEventData-Struktur
    SeismicEventData eventData;
    
    // Detection info
    eventData.timestamp = timeManager.getEpochTime();
    eventData.datetimeISO = timeManager.getFormattedDateTime();
    eventData.ntpValidated = true;
    eventData.bootTimeMs = millis();
    
    // Classification
    float richter = calculateRichterMagnitude(magnitude);
    eventData.eventType = getEventTypeFromRichter(richter);
    eventData.intensityLevel = getIntensityLevelFromRichter(richter);
    eventData.richterRange = getRichterRangeFromType(eventData.eventType);
    eventData.confidence = 0.95f; // High confidence for detected events
    
    // Measurements
    eventData.pgaG = magnitude;
    eventData.richterMagnitude = richter;
    eventData.localMagnitude = calculateLocalMagnitude(magnitude);
    eventData.durationMs = duration;
    eventData.peakFrequencyHz = calculatePeakFrequency(magnitude);
    eventData.energyJoules = calculateEnergyJoules(richter);
    
    // Sensor data (use last sensor reading or simulate)
    SensorData lastReading = readSensor();
    eventData.maxAccelX = abs(lastReading.accelX);
    eventData.maxAccelY = abs(lastReading.accelY);
    eventData.maxAccelZ = abs(lastReading.accelZ);
    eventData.vectorMagnitude = magnitude;
    eventData.calibrationValid = calibrationValid;
    eventData.calibrationAgeHours = getCalibrationAgeHours();
    
    // Algorithm data
    eventData.detectionMethod = "STA_LTA";
    if (staFull && ltaFull) {
        float sta = staSum / STA_WINDOW;
        float lta = ltaSum / LTA_WINDOW;
        eventData.triggerRatio = (lta > 0) ? sta / lta : 0.0f;
    } else {
        eventData.triggerRatio = 0.0f;
    }
    eventData.staWindowSamples = STA_WINDOW;
    eventData.ltaWindowSamples = LTA_WINDOW;
    eventData.backgroundNoise = backgroundNoise;
    
    // Metadata
    eventData.source = source;
    eventData.processingVersion = "v1.0";
    eventData.sampleRateHz = 100; // Typical sampling rate
    eventData.filterApplied = "bandpass_1-30hz";
    eventData.dataQuality = calibrationValid ? "excellent" : "good";
    
    // Send to DataLogger für permanente Speicherung
    if (globalDataLogger != nullptr) {
        bool success = globalDataLogger->logSeismicEvent(eventData);
        if (detailedLoggingEnabled) {
            Serial.printf("Seismic event logged: %s (Success: %s)\n", 
                          eventData.eventType.c_str(), success ? "YES" : "NO");
        }
    } else {
        if (detailedLoggingEnabled) {
            Serial.println("WARNING: globalDataLogger is null - Seismic event not logged");
        }
    }
}

int Seismograph::getIntensityLevelFromRichter(float richter) {
    if (richter >= 7.0f) return 6; // Major
    if (richter >= 6.0f) return 5; // Strong
    if (richter >= 5.0f) return 4; // Moderate
    if (richter >= 4.0f) return 3; // Light
    if (richter >= 2.0f) return 2; // Minor
    return 1; // Micro
}

String Seismograph::getRichterRangeFromType(const String& eventType) {
    if (eventType == "Major") return "≥7.0";
    if (eventType == "Strong") return "6.0-7.0";
    if (eventType == "Moderate") return "5.0-6.0";
    if (eventType == "Light") return "4.0-5.0";
    if (eventType == "Minor") return "2.0-4.0";
    return "<2.0"; // Micro
}

float Seismograph::calculateEnergyJoules(float richter) {
    // Gutenberg-Richter energy formula: log10(E) = 11.8 + 1.5 * M
    // Where E is energy in Joules and M is Richter magnitude
    if (richter < -2.0f) return 0.0f;
    
    float logEnergy = 11.8f + 1.5f * richter;
    float energy = pow(10.0f, logEnergy);
    
    // Constrain to reasonable values
    return constrain(energy, 1.0f, 1e20f);
}

float Seismograph::calculatePeakFrequency(float magnitude) {
    // Empirical relationship: higher magnitude events have lower dominant frequencies
    // Typical range: 1-30 Hz for local earthquakes
    float frequency = 30.0f - (magnitude * 50.0f);
    return constrain(frequency, 1.0f, 30.0f);
}

float Seismograph::getCalibrationAgeHours() {
    if (lastCalibrationTime == 0) return -1.0f; // No calibration
    return (millis() - lastCalibrationTime) / (60.0f * 60.0f * 1000.0f);
}

float Seismograph::calculatePGAFromRichter(float richter) {
    // Inverse of calculateRichterMagnitude function
    // Original: magnitude = log10(pga_mm_s2) - LOCAL_MAGNITUDE_OFFSET
    // Inverse: pga_mm_s2 = 10^(magnitude + LOCAL_MAGNITUDE_OFFSET)
    
    if (richter < -2.0f || richter > 10.0f) {
        Serial.printf("WARNING: Richter magnitude %.2f out of realistic range\n", richter);
        richter = constrain(richter, -2.0f, 10.0f);
    }
    
    // Calculate PGA in mm/s²
    float pga_mm_s2 = pow(10.0f, richter + LOCAL_MAGNITUDE_OFFSET);
    
    // Convert back to g units (1g = 9806.65 mm/s²)
    float pga_g = pga_mm_s2 / 9806.65f;
    
    // Ensure realistic PGA values
    pga_g = constrain(pga_g, 0.0001f, 10.0f); // 0.1mg to 10g
    
    return pga_g;
}

unsigned long Seismograph::calculateEventDuration(float richter) {
    // Empirical relationship: larger earthquakes typically last longer
    // Based on seismological observations
    
    unsigned long duration;
    
    if (richter < 2.0f) {
        // Micro events: 0.1-1 seconds
        duration = 100 + (richter * 200);
    } else if (richter < 4.0f) {
        // Minor events: 1-5 seconds  
        duration = 1000 + ((richter - 2.0f) * 2000);
    } else if (richter < 6.0f) {
        // Light to Moderate events: 5-30 seconds
        duration = 5000 + ((richter - 4.0f) * 12500);
    } else if (richter < 7.0f) {
        // Strong events: 30-120 seconds
        duration = 30000 + ((richter - 6.0f) * 90000);
    } else {
        // Major events: 120-300 seconds
        duration = 120000 + ((richter - 7.0f) * 180000);
    }
    
    // Constrain to reasonable limits
    duration = constrain(duration, 100, 300000); // 0.1s to 5 minutes
    
    return duration;
}
