let janus = null;
let streaming = null;
let videoElement = document.getElementById('remotevideo');
let statusElement = document.getElementById('status');
let fsBtn = document.getElementById('fsBtn');
let id = parseInt('{{MOUNTPOINT_ID}}');

function updateStatus(message) {
    statusElement.textContent = message;
}

updateStatus('Initializing...');

fsBtn.addEventListener('click', () => {
    if (videoElement.requestFullscreen) {
        videoElement.requestFullscreen();
    } else if (videoElement.webkitRequestFullscreen) {
        videoElement.webkitRequestFullscreen();
    } else if (videoElement.msRequestFullscreen) {
        videoElement.msRequestFullscreen();
    }
});

Janus.init({
    debug: false,
    callback: function() {
        updateStatus('Connecting to server...');
        janus = new Janus({
            server: '{{JANUS_URL}}',
            success: function() {
                updateStatus('Connecting to stream...');
                janus.attach({
                    plugin: 'janus.plugin.streaming',
                    success: function(pluginHandle) {
                        streaming = pluginHandle;
                        streaming.send({ message: { request: 'watch', id: id } });
                    },
                    onmessage: function(msg, jsep) {
                        if (msg.error_code) {
                            updateStatus('Stream error: ' + msg.error);
                            return;
                        }
                        if (jsep) {
                            streaming.createAnswer({
                                jsep: jsep,
                                media: { audioSend: false, videoSend: false },
                                success: function(jsep) {
                                    streaming.send({ message: { request: 'start' }, jsep: jsep });
                                },
                                error: function(err) {
                                    updateStatus('Connection failed');
                                }
                            });
                        }
                    },
                    onremotetrack: function(track, mid, on) {
                        if (!on) return;
                        if (!videoElement.srcObject) {
                            videoElement.srcObject = new MediaStream();
                        }
                        videoElement.srcObject.addTrack(track);
                        updateStatus('Stream connected');
                    },
                    webrtcState: function(state) {
                        if (state === true) updateStatus('Stream active');
                        else if (state === false) updateStatus('Stream disconnected');
                    },
                    error: function(error) {
                        updateStatus('Plugin error');
                    }
                });
            },
            error: function(error) {
                updateStatus('Cannot connect to server');
            }
        });
    },
    error: function(error) {
        updateStatus('Failed to initialize');
    }
});

videoElement.addEventListener('playing', function() {
    updateStatus('Playing live stream');
});
