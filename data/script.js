let chart;
const maxDataPoints = 50;
let sensorDataHistory = [];
let websocket = null;
let reconnectInterval = null;
let connectionTimeout = null;
let isConnected = false;

// Unified reconnect management
function startReconnect() {
    // Cleanup existing reconnect
    if (reconnectInterval) {
        clearInterval(reconnectInterval);
        reconnectInterval = null;
    }
    
    // Clear connection timeout
    if (connectionTimeout) {
        clearTimeout(connectionTimeout);
        connectionTimeout = null;
    }
    
    console.log('Starting reconnect attempt in 5 seconds...');
    reconnectInterval = setInterval(connectWebSocket, 5000);
}

// WebSocket connection management
function connectWebSocket() {
    // Close existing connection properly
    if (websocket) {
        websocket.onclose = null; // Prevent reconnect loop
        websocket.onerror = null;
        websocket.onopen = null;
        websocket.onmessage = null;
        
        if (websocket.readyState === WebSocket.OPEN || 
            websocket.readyState === WebSocket.CONNECTING) {
            websocket.close();
        }
        websocket = null;
    }
    
    // Clear any existing connection timeout
    if (connectionTimeout) {
        clearTimeout(connectionTimeout);
        connectionTimeout = null;
    }
    
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    const wsUrl = `${protocol}//${window.location.host}/ws`;
    
    console.log('Connecting to WebSocket:', wsUrl);
    
    try {
        websocket = new WebSocket(wsUrl);
        
        // Set connection timeout (10 seconds)
        connectionTimeout = setTimeout(() => {
            if (websocket && websocket.readyState === WebSocket.CONNECTING) {
                console.log('WebSocket connection timeout - forcing reconnect');
                
                // Clear handlers to prevent conflicts
                websocket.onclose = null;
                websocket.onerror = null;
                websocket.onopen = null;
                websocket.onmessage = null;
                
                // Force close and cleanup
                try {
                    websocket.close();
                } catch (e) {
                    console.log('Error closing websocket:', e);
                }
                
                websocket = null;
                isConnected = false;
                updateConnectionStatus(false);
                
                // Direct reconnect without waiting for onclose
                startReconnect();
            }
        }, 10000);
        
        websocket.onopen = function(event) {
            console.log('WebSocket connected successfully');
            isConnected = true;
            updateConnectionStatus(true);
            
            // Clear connection timeout
            if (connectionTimeout) {
                clearTimeout(connectionTimeout);
                connectionTimeout = null;
            }
            
            // Clear reconnect interval if it exists
            if (reconnectInterval) {
                clearInterval(reconnectInterval);
                reconnectInterval = null;
            }
            
            // Request initial status
            sendWebSocketMessage({command: 'get_status'});
            
            // Start real-time streaming
            sendWebSocketMessage({command: 'start_streaming'});
        };
        
        websocket.onmessage = function(event) {
            try {
                const data = JSON.parse(event.data);
                handleWebSocketMessage(data);
            } catch (error) {
                console.error('Error parsing WebSocket message:', error);
            }
        };
        
        websocket.onclose = function(event) {
            console.log('WebSocket disconnected, code:', event.code, 'reason:', event.reason);
            isConnected = false;
            updateConnectionStatus(false);
            
            // Clear connection timeout
            if (connectionTimeout) {
                clearTimeout(connectionTimeout);
                connectionTimeout = null;
            }
            
            // Start reconnect attempt
            startReconnect();
        };
        
        websocket.onerror = function(error) {
            console.error('WebSocket error:', error);
            isConnected = false;
            updateConnectionStatus(false);
            
            // Clear connection timeout
            if (connectionTimeout) {
                clearTimeout(connectionTimeout);
                connectionTimeout = null;
            }
            
            // Start reconnect attempt
            startReconnect();
        };
        
    } catch (error) {
        console.error('Failed to create WebSocket:', error);
        isConnected = false;
        updateConnectionStatus(false);
        startReconnect();
    }
}

function sendWebSocketMessage(message) {
    if (websocket && websocket.readyState === WebSocket.OPEN) {
        websocket.send(JSON.stringify(message));
    }
}

function handleWebSocketMessage(data) {
    switch(data.type) {
        case 'sensor_data':
            updateChart(data);
            updateSensorDisplay(data);
            break;
            
        case 'status':
            updateStatusDisplay(data);
            break;
            
        case 'seismic_event':
            handleSeismicEvent(data);
            break;
            
        case 'event':
            console.log('System event:', data.event_type, data.data);
            break;
            
        case 'response':
            console.log('WebSocket response:', data.message);
            break;
            
        case 'error':
            console.error('WebSocket error:', data.message);
            break;
            
        default:
            console.log('Unknown WebSocket message type:', data.type);
    }
}

function updateConnectionStatus(connected) {
    const statusElement = document.getElementById('connectionStatus');
    if (statusElement) {
        statusElement.textContent = connected ? '🟢 Connected' : '🔴 Disconnected';
        statusElement.className = connected ? 'connected' : 'disconnected';
    }
}

function initChart() {
    const ctx = document.getElementById('sensorChart').getContext('2d');
    chart = new Chart(ctx, {
        type: 'line',
        data: {
            labels: [],
            datasets: [
                {
                    label: 'Magnitude (g)',
                    data: [],
                    borderColor: 'rgb(255,99,132)',
                    tension: 0.1
                },
                {
                    label: 'Accel X',
                    data: [],
                    borderColor: 'rgb(54,162,235)',
                    tension: 0.1
                },
                {
                    label: 'Accel Y',
                    data: [],
                    borderColor: 'rgb(75,192,192)',
                    tension: 0.1
                },
                {
                    label: 'Accel Z',
                    data: [],
                    borderColor: 'rgb(255,205,86)',
                    tension: 0.1
                }
            ]
        },
        options: {
            responsive: true,
            scales: {
                y: {
                    beginAtZero: true
                }
            }
        }
    });
}

function updateChart(data) {
    const time = new Date().toLocaleTimeString();
    chart.data.labels.push(time);
    chart.data.datasets[0].data.push(data.magnitude || 0);
    chart.data.datasets[1].data.push(data.accel_x || 0);
    chart.data.datasets[2].data.push(data.accel_y || 0);
    chart.data.datasets[3].data.push(data.accel_z || 0);
    
    if (chart.data.labels.length > maxDataPoints) {
        chart.data.labels.shift();
        chart.data.datasets.forEach(dataset => dataset.data.shift());
    }
    
    chart.update('none');
}

function updateSensorDisplay(data) {
    document.getElementById('sensorData').innerHTML = 
        'X: ' + (data.accel_x || 0).toFixed(4) + 'g | ' +
        'Y: ' + (data.accel_y || 0).toFixed(4) + 'g | ' +
        'Z: ' + (data.accel_z || 0).toFixed(4) + 'g | ' +
        'Magnitude: ' + (data.magnitude || 0).toFixed(4) + 'g<br>' +
        'Events Detected: ' + (data.events_detected || 0) + '<br>' +
        'Calibrated: ' + (data.calibrated ? '✅ Yes' : '❌ No');
}

function updateStatusDisplay(data) {
    const localTime = data.ntp_timestamp ? 
        new Date(data.ntp_timestamp * 1000).toLocaleString() : 'N/A';
    const timeStatus = data.time_valid ? '✅ NTP Synced' : '⚠️ No NTP';
    
    document.getElementById('status').innerHTML = 
        '⏱️ Uptime: ' + Math.floor(data.uptime || 0) + 's<br>' +
        '💾 Free Heap: ' + (data.free_heap || 0) + ' bytes<br>' +
        '📶 WiFi RSSI: ' + (data.wifi_rssi || 0) + ' dBm<br>' +
        '🌐 MQTT: ' + (data.mqtt_connected ? '✅ Connected' : '❌ Disconnected') + '<br>' +
        '🎯 Sensor: ' + (data.sensor_calibrated ? '✅ Calibrated' : '❌ Not Calibrated') + '<br>' +
        '📅 Time: ' + localTime + ' ' + timeStatus + '<br>' +
        '🔗 WebSocket: ' + (isConnected ? '✅ Connected' : '❌ Disconnected') + '<br>' +
        '👥 Clients: ' + (data.connected_clients || 0) + '<br>' +
        '📡 Streaming: ' + (data.streaming_enabled ? '✅ Enabled' : '❌ Disabled');
}

function handleSeismicEvent(data) {
    console.log('Seismic event detected:', data);
    
    // Show notification
    const notification = document.createElement('div');
    notification.className = 'seismic-notification';
    notification.innerHTML = `
        <strong>🌍 Seismic Event Detected!</strong><br>
        Type: ${data.event_type}<br>
        Magnitude: ${data.magnitude.toFixed(4)}g<br>
        Level: ${data.level}
    `;
    
    document.body.appendChild(notification);
    
    // Auto-remove notification after 5 seconds
    setTimeout(() => {
        if (notification.parentNode) {
            notification.parentNode.removeChild(notification);
        }
    }, 5000);
    
    // Reload events list to show new event
    setTimeout(loadSeismicEvents, 1000);
}

// Fallback function for when WebSocket is not available
function updateData() {
    if (isConnected) {
        // WebSocket is handling updates, no need for polling
        return;
    }
    
    // Fallback to HTTP polling when WebSocket is not available
    fetch('/api/status')
        .then(response => response.json())
        .then(data => {
            const localTime = data.time_valid && data.timestamp ? 
                new Date(data.timestamp * 1000).toLocaleString() : 'N/A';
            const timeStatus = data.time_valid ? '✅ NTP Synced' : '⚠️ No NTP';
            const otaStatus = data.ota_enabled ? 
                '✅ Ready (' + data.ota_hostname + ':' + data.ota_port + ')' : '❌ Disabled';
            
            document.getElementById('status').innerHTML = 
                '⏱️ Uptime: ' + data.uptime + 's<br>' +
                '💾 Free Heap: ' + data.free_heap + ' bytes<br>' +
                '📶 WiFi RSSI: ' + data.wifi_rssi + ' dBm<br>' +
                '🌐 MQTT: ' + (data.mqtt_connected ? '✅ Connected' : '❌ Disconnected') + '<br>' +
                '🎯 Sensor: ' + (data.sensor_calibrated ? '✅ Calibrated' : '❌ Not Calibrated') + '<br>' +
                '📅 Time: ' + localTime + ' ' + timeStatus + '<br>' +
                '🔄 OTA: ' + otaStatus + '<br>' +
                '🔗 WebSocket: ❌ Disconnected (using HTTP polling)';
        })
        .catch(error => console.error('Status Error:', error));

    fetch('/api/data')
        .then(response => response.json())
        .then(data => {
            updateChart(data);
            updateSensorDisplay(data);
        })
        .catch(error => console.error('Data Error:', error));
}

function formatTimestampToLocal(timestamp, ntpValid) {
    if (!timestamp) return 'N/A';
    
    let date;
    if (ntpValid && timestamp > 1577836800) {
        date = new Date(timestamp * 1000);
        return date.toLocaleString();
    } else if (timestamp > 1577836800000) {
        date = new Date(timestamp);
        return date.toLocaleString();
    } else if (timestamp > 1577836800) {
        date = new Date(timestamp * 1000);
        return date.toLocaleString();
    } else {
        const bootSeconds = Math.floor(timestamp / 1000);
        const hours = Math.floor(bootSeconds / 3600);
        const minutes = Math.floor((bootSeconds % 3600) / 60);
        const seconds = bootSeconds % 60;
        return 'Boot+' + String(hours).padStart(2, '0') + ':' + 
               String(minutes).padStart(2, '0') + ':' + String(seconds).padStart(2, '0');
    }
}


function simulateEvent(type) {
    const magnitudes = {
        micro: 0.003,
        light: 0.02,
        strong: 0.08
    };
    const magnitude = magnitudes[type] || 0.01;
    
    fetch('/api/simulate?type=' + type + '&magnitude=' + magnitude, {
        method: 'POST'
    })
    .then(response => response.text())
    .then(data => {
        alert('🧪 ' + data);
        setTimeout(updateData, 1000);
    })
    .catch(error => alert('❌ Simulation Error: ' + error));
}


function restart() {
    if (confirm('Are you sure you want to restart the system?')) {
        fetch('/api/restart', {
            method: 'POST'
        })
        .then(() => alert('🔄 System restarting...'))
        .catch(error => alert('❌ Error: ' + error));
    }
}

let allEvents = [];
let filteredEvents = [];

function loadSeismicEvents() {
    fetch('/api/seismic-events')
        .then(response => response.json())
        .then(data => {
            allEvents = data.events || [];
            allEvents.sort((a, b) => (b.detection?.timestamp || 0) - (a.detection?.timestamp || 0));
            updateEventStats(data.statistics);
            filterEvents();
        })
        .catch(error => {
            console.error('Events Error:', error);
            document.getElementById('eventsList').innerHTML = 
                '<div class="no-events">Error loading events</div>';
        });
}

function updateEventStats(stats) {
    if (!stats || !stats.by_type) return;
    
    const total = Object.values(stats.by_type).reduce((a, b) => a + b, 0);
    document.getElementById('eventStats').innerHTML = 'Total: ' + total + ' events';
}

function filterEvents() {
    const filter = document.getElementById('eventFilter').value;
    
    if (filter === 'all') {
        filteredEvents = allEvents;
    } else {
        filteredEvents = allEvents.filter(event => 
            event.classification && event.classification.type === filter
        );
    }
    
    displayEvents();
}

function displayEvents() {
    const container = document.getElementById('eventsList');
    
    if (filteredEvents.length === 0) {
        container.innerHTML = '<div class="no-events">No events found</div>';
        return;
    }
    
    let html = '';
    filteredEvents.slice(0, 25).forEach(event => {
        const eventType = event.classification?.type || 'Unknown';
        const richter = event.measurements?.richter_magnitude || 0;
        const pga = event.measurements?.pga_g || 0;
        const duration = event.measurements?.duration_ms || 0;
        const timestamp = event.detection?.timestamp || 0;
        const ntpValid = event.detection?.ntp_validated || false;
        const timeStr = formatTimestampToLocal(timestamp, ntpValid);
        const typeClass = getRichterClass(richter);
        const typeIcon = getEventIcon(eventType);
        
        html += '<div class="event-item">';
        html += '<div class="event-header">';
        html += '<span class="event-type ' + typeClass + '">' + typeIcon + ' ' + eventType + ' Event</span>';
        html += '<span class="event-time">' + timeStr + '</span>';
        html += '</div>';
        html += '<div class="event-data">';
        html += '<div class="event-metric"><span class="event-metric-label">Richter Scale</span><span class="event-metric-value ' + typeClass + '">' + richter.toFixed(2) + '</span></div>';
        html += '<div class="event-metric"><span class="event-metric-label">PGA</span><span class="event-metric-value">' + pga.toFixed(4) + 'g</span></div>';
        html += '<div class="event-metric"><span class="event-metric-label">Duration</span><span class="event-metric-value">' + (duration / 1000).toFixed(1) + 's</span></div>';
        html += '</div>';
        html += '</div>';
    });
    
    container.innerHTML = html;
}

function getRichterClass(richter) {
    if (richter < 3.0) return 'richter-micro';
    if (richter < 4.0) return 'richter-minor';
    if (richter < 5.0) return 'richter-light';
    if (richter < 6.0) return 'richter-moderate';
    if (richter < 7.0) return 'richter-strong';
    return 'richter-major';
}

function getEventIcon(type) {
    const icons = {
        Micro: '🟢',
        Minor: '🟡',
        Light: '🟠',
        Moderate: '🔴',
        Strong: '🟣',
        Major: '🔵'
    };
    return icons[type] || '🌍';
}

window.onload = function() {
    initChart();
    
    // Start WebSocket connection
    connectWebSocket();
    
    // Fallback HTTP polling (reduced frequency since WebSocket handles real-time updates)
    setInterval(updateData, 10000); // Reduced from 5s to 10s
    setInterval(loadSeismicEvents, 30000); // Reduced from 10s to 30s
    
    // Initial data load
    updateData();
    loadSeismicEvents();
};
