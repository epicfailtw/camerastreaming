// Global variables
let janus = null;
let streaming = null;
let videoElement = document.getElementById('remotevideo');
let id = parseInt('{{MOUNTPOINT_ID}}');
let logCount = 0;

function debugLog(message, type = 'info') {
    logCount++;
    const timestamp = new Date().toLocaleTimeString();
    console.log('[' + timestamp + '] ' + message);

    const debugDiv = document.getElementById('debug-log');
    const logEntry = document.createElement('div');
    logEntry.className = 'log-entry log-' + type;
    logEntry.innerHTML = '<strong>' + timestamp + ':</strong> ' + message;
    debugDiv.appendChild(logEntry);
    debugDiv.scrollTop = debugDiv.scrollHeight;

    // Keep only last 100 entries
    while (debugDiv.children.length > 100) {
        debugDiv.removeChild(debugDiv.firstChild);
    }
}

function updateStatus(message) {
    document.getElementById('status').innerHTML = message;
    debugLog('STATUS: ' + message.replace(/🔥|✅|❌|🎥|🎉/g, '').trim());
}

function updateInfo(field, value) {
    const element = document.getElementById(field);
    if (element) {
        element.textContent = value;
    }
}

// Initial checks
debugLog('🚀 Starting streaming application...', 'success');
debugLog('Browser: ' + navigator.userAgent.split(' ').pop());
debugLog('WebRTC support: ' + (typeof RTCPeerConnection !== 'undefined' ? 'Yes' : 'No'));

if (typeof adapter !== 'undefined') {
    debugLog('✅ WebRTC Adapter loaded: ' + adapter.browserDetails.browser, 'success');
} else {
    debugLog('⚠️ WebRTC Adapter not loaded', 'warning');
}

if (typeof Janus !== 'undefined') {
    debugLog('✅ Janus library loaded successfully', 'success');
} else {
    debugLog('❌ Janus library not loaded!', 'error');
    updateStatus('❌ Janus library missing');
}

if (videoElement) {
    debugLog('✅ Video element ready', 'success');
    updateInfo('video-state', 'Ready');
} else {
    debugLog('❌ Video element not found!', 'error');
    updateInfo('video-state', 'Error');
}

// Start Janus initialization
updateStatus('🔥 Initializing Janus...');
debugLog('📡 Starting Janus.init()...', 'info');

Janus.init({
    debug: 'all',
    callback: function() {
        debugLog('✅ Janus.init() success!', 'success');
        updateStatus('🔥 Connecting to Janus server...');

        janus = new Janus({
            server: '{{JANUS_URL}}',
            success: function() {
                debugLog('✅ Connected to Janus server!', 'success');
                updateStatus('🔥 Attaching to streaming plugin...');
                updateInfo('session-id', 'Connected');

                janus.attach({
                    plugin: 'janus.plugin.streaming',
                    success: function(pluginHandle) {
                        debugLog('✅ Attached to streaming plugin!', 'success');
                        updateStatus('🔥 Requesting video stream...');
                        updateInfo('handle-id', 'Attached');
                        streaming = pluginHandle;
                        streaming.send({ message: { request: 'watch', id: id } });
                    },
                    onmessage: function(msg, jsep) {
                        debugLog('📨 Message: ' + JSON.stringify(msg), 'info');

                        if (msg.error_code) {
                            debugLog('❌ Janus error ' + msg.error_code + ': ' + msg.error, 'error');
                            updateStatus('❌ Stream error: ' + msg.error);
                            return;
                        }

                        if (jsep) {
                            debugLog('🤝 Received WebRTC offer, creating answer...', 'success');
                            updateStatus('🔥 Processing video offer...');
                            updateInfo('webrtc-state', 'Negotiating');

                            streaming.createAnswer({
                                jsep: jsep,
                                media: { audioSend: false, videoSend: false },
                                success: function(jsep) {
                                    debugLog('✅ Created WebRTC answer successfully!', 'success');
                                    updateStatus('🔥 Starting video playback...');
                                    streaming.send({ message: { request: 'start' }, jsep: jsep });
                                },
                                error: function(err) {
                                    debugLog('❌ WebRTC answer error: ' + JSON.stringify(err), 'error');
                                    updateStatus('❌ WebRTC connection failed');
                                    updateInfo('webrtc-state', 'Failed');
                                }
                            });
                        }
                    },
                    onremotetrack: function(track, mid, on) {
                        debugLog('🎬 Remote track: ' + track.kind + ', enabled=' + on, on ? 'success' : 'warning');
                        if (!on) {
                            debugLog('ℹ️ Track disabled', 'warning');
                            return;
                        }

                        debugLog('✅ Adding ' + track.kind + ' track to video element', 'success');
                        updateInfo('video-state', 'Receiving ' + track.kind);

                        if (!videoElement.srcObject) {
                            videoElement.srcObject = new MediaStream();
                            debugLog('📺 Created new MediaStream', 'info');
                        }

                        videoElement.srcObject.addTrack(track);
                        debugLog('✅ Track added successfully!', 'success');
                        updateStatus('🎥 Receiving ' + track.kind + ' stream!');
                    },
                    webrtcState: function(state) {
                        debugLog('🔗 WebRTC state: ' + state, state ? 'success' : 'warning');
                        if (state === true) {
                            updateStatus('✅ WebRTC Connected!');
                            updateInfo('webrtc-state', 'Connected');
                        } else if (state === false) {
                            updateStatus('❌ WebRTC Disconnected');
                            updateInfo('webrtc-state', 'Disconnected');
                        }
                    },
                    error: function(error) {
                        debugLog('❌ Plugin error: ' + error, 'error');
                        updateStatus('❌ Streaming plugin error');
                    }
                });
            },
            error: function(error) {
                debugLog('❌ Janus server connection failed: ' + error, 'error');
                updateStatus('❌ Cannot connect to Janus server');
                updateInfo('session-id', 'Failed');
            }
        });
    },
    error: function(error) {
        debugLog('❌ Janus initialization failed: ' + error, 'error');
        updateStatus('❌ Janus initialization failed');
    }
});

// Video element event handlers
videoElement.addEventListener('playing', function() {
    debugLog('🎉 Video is now playing!', 'success');
    updateStatus('🎉 Live video is playing!');
    updateInfo('video-state', 'Playing');
});

videoElement.addEventListener('error', function(e) {
    debugLog('📹 Video error: ' + (e.message || 'Unknown error'), 'error');
    updateStatus('❌ Video playback error');
    updateInfo('video-state', 'Error');
});

videoElement.addEventListener('loadstart', function() {
    debugLog('📹 Video loading started', 'info');
    updateInfo('video-state', 'Loading');
});

videoElement.addEventListener('canplay', function() {
    debugLog('📹 Video ready to play', 'info');
    updateInfo('video-state', 'Ready to play');
});

debugLog('🎯 JavaScript initialization complete', 'success');
