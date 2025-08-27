#include "janusconnector.h"

int JanusConnector::s_nextMountpointId = 1;

JanusConnector::JanusConnector(QObject *parent)
    : QObject(parent)
    , m_networkManager(new QNetworkAccessManager(this))
    , m_webView(new QWebEngineView())
    , m_webChannel(new QWebChannel(this))
    , m_keepAliveTimer(new QTimer(this))
    , m_janusUrl("http://10.10.205.65:8088/janus")
    , m_sessionId(0)
    , m_handleId(0)
    , m_state(Idle)
    , m_currentReply(nullptr)
    , m_mountpointId(s_nextMountpointId++)
{
    // Setup network manager
    m_networkManager->setTransferTimeout(10000);// 10 seconds

    // Setup keep-alive timer
    m_keepAliveTimer->setInterval(30000); // 30 seconds
    connect(m_keepAliveTimer, &QTimer::timeout, this, &JanusConnector::sendKeepAlive);

    setupWebEngineView();
}

JanusConnector::~JanusConnector()
{
    cleanup();
    delete m_webView;
}

void JanusConnector::setJanusUrl(const QString &url)
{
    if (m_state != Idle) {
        qWarning() << "Cannot change Janus URL while connected";
        return;
    }
    m_janusUrl = url;
}

QString JanusConnector::janusUrl() const
{
    return m_janusUrl;
}

void JanusConnector::connectToJanus(const CameraParams &params)
{
    if (!params.isValid()) {
        emit errorOccurred("Invalid camera parameters");
        return;
    }

    if (m_state != Idle) {
        qWarning() << "Already connecting/connected, current state:" << m_state;
        return;
    }

    m_currentParams = params;
    qDebug() << "Connecting to Janus for camera:" << params.cameraUUID;
    qDebug() << "RTSP URL:" << params.rtspUrl;
    qDebug() << "Using mountpoint ID:" << m_mountpointId;

    createJanusSession();
}

void JanusConnector::disconnect()
{
    cleanup();
    m_state = Idle;
    emit connectionStateChanged(false);
}

bool JanusConnector::isConnected() const
{
    return m_state == Ready || m_state == Streaming;
}

qint64 JanusConnector::sessionId() const
{
    return m_sessionId;
}

qint64 JanusConnector::handleId() const
{
    return m_handleId;
}

void JanusConnector::startStreaming()
{
    if (m_state != Ready) {
        qWarning() << "Cannot start streaming, not ready. Current state:" << m_state;
        return;
    }
    startWebRTCStreaming();
}

void JanusConnector::stopStreaming()
{
    if (m_state == Streaming) {
        m_webView->hide();
        m_state = Ready;
        emit streamingStopped();
    }
}

void JanusConnector::createJanusSession()
{
    m_state = CreatingSession;

    QJsonObject sessionRequest;
    sessionRequest["janus"] = "create";
    sessionRequest["transaction"] = "tx-create-session";

    QJsonDocument doc(sessionRequest);
    QNetworkRequest request((QUrl(m_janusUrl)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_currentReply = m_networkManager->post(request, doc.toJson());
    connect(m_currentReply, &QNetworkReply::finished,
            this, &JanusConnector::onSessionCreated);
    connect(m_currentReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &JanusConnector::onNetworkError);
}

void JanusConnector::onSessionCreated()
{
    if (!m_currentReply) return;

    QByteArray response = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    qDebug() << "Session Create Response:" << response;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("Failed to parse session response");
        m_state = Idle;
        return;
    }

    QJsonObject obj = doc.object();
    if (obj["janus"].toString() != "success") {
        emit errorOccurred("Failed to create Janus session");
        m_state = Idle;
        return;
    }

    m_sessionId = obj["data"].toObject()["id"].toVariant().toLongLong();
    qDebug() << "Session ID:" << m_sessionId;

    // Start keep-alive timer
    m_keepAliveTimer->start();

    // Proceed to attach plugin
    attachToStreamingPlugin();
}

void JanusConnector::attachToStreamingPlugin()
{
    m_state = AttachingPlugin;

    QJsonObject attachRequest;
    attachRequest["janus"] = "attach";
    attachRequest["plugin"] = "janus.plugin.streaming";
    attachRequest["transaction"] = "tx-attach-plugin";

    QJsonDocument doc(attachRequest);
    QString url = QString("%1/%2").arg(m_janusUrl).arg(m_sessionId);
    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_currentReply = m_networkManager->post(request, doc.toJson());
    connect(m_currentReply, &QNetworkReply::finished,
            this, &JanusConnector::onPluginAttached);
    connect(m_currentReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &JanusConnector::onNetworkError);
}

void JanusConnector::onPluginAttached()
{
    if (!m_currentReply) return;

    QByteArray response = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    qDebug() << "Plugin Attach Response:" << response;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("Failed to parse attach response");
        m_state = Idle;
        return;
    }

    QJsonObject obj = doc.object();
    if (obj["janus"].toString() != "success") {
        emit errorOccurred("Failed to attach to streaming plugin");
        m_state = Idle;
        return;
    }

    m_handleId = obj["data"].toObject()["id"].toVariant().toLongLong();
    qDebug() << "Handle ID:" << m_handleId;

    // Proceed to create mountpoint
    createRTSPMountpoint();
}

void JanusConnector::createRTSPMountpoint()
{
    m_state = CreatingMountpoint;

    QJsonObject body;
    body["request"] = "create";
    body["type"] = "rtsp";
    body["id"] = m_mountpointId;;
    body["name"] = m_currentParams.roomName;
    body["description"] = QString("%1 - %2 Live Stream")
                              .arg(m_currentParams.customerName, m_currentParams.applianceName);
    body["audio"] = true;
    body["video"] = true;
    body["permanent"] = false;
    body["url"] = m_currentParams.rtspUrl;
    body["metadata"] = QString("Camera: %1, Room: %2, School: %3")
                           .arg(m_currentParams.cameraId, m_currentParams.roomName,
                                m_currentParams.applianceName);

    // RTSP parameters
    body["rtsp_user"] = m_currentParams.rtspUser;
    body["rtsp_pwd"] = m_currentParams.rtspPassword;
    body["rtsp_reconnect_delay"] = 5;
    body["rtsp_session_timeout"] = 0;
    body["rtsp_timeout"] = 10;
    body["rtsp_conn_timeout"] = 5;

    QJsonObject mountpointRequest;
    mountpointRequest["janus"] = "message";
    mountpointRequest["transaction"] = "tx-create-mountpoint";
    mountpointRequest["session_id"] = m_sessionId;
    mountpointRequest["handle_id"] = m_handleId;
    mountpointRequest["body"] = body;

    QJsonDocument doc(mountpointRequest);
    QString url = QString("%1/%2/%3").arg(m_janusUrl).arg(m_sessionId).arg(m_handleId);
    QNetworkRequest request((QUrl(url)));
    request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

    m_currentReply = m_networkManager->post(request, doc.toJson());
    connect(m_currentReply, &QNetworkReply::finished,
            this, &JanusConnector::onMountpointCreated);
    connect(m_currentReply, QOverload<QNetworkReply::NetworkError>::of(&QNetworkReply::errorOccurred),
            this, &JanusConnector::onNetworkError);
}

void JanusConnector::onMountpointCreated()
{
    if (!m_currentReply) return;

    QByteArray response = m_currentReply->readAll();
    m_currentReply->deleteLater();
    m_currentReply = nullptr;

    qDebug() << "Mountpoint Create Response:" << response;

    QJsonParseError parseError;
    QJsonDocument doc = QJsonDocument::fromJson(response, &parseError);

    if (parseError.error != QJsonParseError::NoError) {
        emit errorOccurred("Failed to parse mountpoint response");
        m_state = Idle;
        return;
    }

    QJsonObject obj = doc.object();
    if (obj["janus"].toString() != "success") {
        emit errorOccurred("Failed to create RTSP mountpoint");
        m_state = Idle;
        return;
    }

    m_state = Ready;
    qDebug() << "RTSP mountpoint created successfully";

    emit sessionReady(m_sessionId, m_handleId);
    emit connectionStateChanged(true);

    // Auto-start streaming after a short delay
    QTimer::singleShot(1000, this, &JanusConnector::startStreaming);
}

// void JanusConnector::startWebRTCStreaming()
// {
//     if (m_state != Ready) {
//         qWarning() << "Cannot start WebRTC streaming, not ready";
//         return;
//     }

//     qDebug() << "Starting WebRTC streaming";

//     // Read janus.js from resources
//     QFile janusFile(":/scripts/janus.js");
//     QString janusJsContent;
//     if (janusFile.open(QIODevice::ReadOnly)) {
//         janusJsContent = QString::fromUtf8(janusFile.readAll());
//         janusFile.close();
//     } else {
//         qWarning() << "Failed to load janus.js from resources";
//         emit errorOccurred("Failed to load janus.js from resources");
//         return;
//     }

//     // Generate HTML content with embedded Janus.js
//     QString htmlContent = QString(R"(
// <!DOCTYPE html>
// <html>
// <head>
//     <title>%1 Live Stream</title>
//     <meta charset="utf-8">
//     <style>
//         * {
//             margin: 0;
//             padding: 0;
//             box-sizing: border-box;
//         }

//         body {
//             font-family: 'Segoe UI', Arial, sans-serif;
//             background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);
//             color: white;
//             height: 100vh;
//             overflow: hidden;
//         }

//         .header {
//             background: rgba(0,0,0,0.3);
//             padding: 15px;
//             text-align: center;
//             border-bottom: 2px solid rgba(255,255,255,0.2);
//         }

//         .header h2 {
//             margin: 0;
//             font-size: 24px;
//             text-shadow: 2px 2px 4px rgba(0,0,0,0.5);
//         }

//         .main-container {
//             display: flex;
//             height: calc(100vh - 80px);
//             gap: 0;
//         }

//         /* ✅ LEFT SIDE - Video */
//         .video-section {
//             flex: 0 0 65%;
//             background: rgba(0,0,0,0.2);
//             padding: 20px;
//             display: flex;
//             flex-direction: column;
//             justify-content: center;
//             align-items: center;
//         }

//         .video-container {
//             width: 100%;
//             max-width: 800px;
//             position: relative;
//         }

//         video {
//             width: 100%;
//             height: auto;
//             border: 3px solid rgba(255,255,255,0.3);
//             border-radius: 12px;
//             background: #000;
//             box-shadow: 0 8px 32px rgba(0,0,0,0.4);
//         }

//         /* ✅ RIGHT SIDE - All Output */
//         .output-section {
//             flex: 0 0 35%;
//             background: rgba(0,0,0,0.4);
//             border-left: 2px solid rgba(255,255,255,0.2);
//             display: flex;
//             flex-direction: column;
//             height: 100%;
//         }

//         .status-panel {
//             background: rgba(255,255,255,0.1);
//             padding: 15px;
//             border-bottom: 1px solid rgba(255,255,255,0.2);
//         }

//         .status {
//             background: rgba(255,255,255,0.2);
//             padding: 12px 15px;
//             border-radius: 8px;
//             font-weight: 500;
//             text-align: center;
//             font-size: 14px;
//         }

//         .info-panel {
//             padding: 15px;
//             border-bottom: 1px solid rgba(255,255,255,0.2);
//         }

//         .info-title {
//             font-size: 16px;
//             font-weight: 600;
//             margin-bottom: 10px;
//             color: #ffeb3b;
//         }

//         .info-grid {
//             display: grid;
//             grid-template-columns: 1fr 1fr;
//             gap: 8px;
//             margin-bottom: 10px;
//         }

//         .info-item {
//             background: rgba(255,255,255,0.1);
//             padding: 8px;
//             border-radius: 6px;
//             font-size: 12px;
//         }

//         .info-label {
//             color: #e0e0e0;
//             font-size: 10px;
//             text-transform: uppercase;
//             font-weight: 600;
//         }

//         .info-value {
//             color: #ffffff;
//             font-weight: 500;
//             margin-top: 2px;
//         }

//         .debug-panel {
//             flex: 1;
//             padding: 15px;
//             display: flex;
//             flex-direction: column;
//         }

//         .debug-title {
//             font-size: 16px;
//             font-weight: 600;
//             margin-bottom: 10px;
//             color: #4caf50;
//         }

//         .debug-log {
//             flex: 1;
//             background: rgba(0,0,0,0.6);
//             border: 1px solid rgba(255,255,255,0.2);
//             border-radius: 8px;
//             padding: 10px;
//             font-family: 'Consolas', 'Monaco', monospace;
//             font-size: 11px;
//             line-height: 1.4;
//             overflow-y: auto;
//             color: #e0e0e0;
//         }

//         .log-entry {
//             margin: 2px 0;
//             padding: 2px 4px;
//             border-radius: 3px;
//         }

//         .log-success {
//             color: #4caf50;
//         }

//         .log-error {
//             color: #f44336;
//         }

//         .log-warning {
//             color: #ff9800;
//         }

//         .log-info {
//             color: #2196f3;
//         }

//         /* ✅ Responsive design */
//         @media (max-width: 1200px) {
//             .main-container {
//                 flex-direction: column;
//             }

//             .video-section {
//                 flex: 0 0 60%;
//             }

//             .output-section {
//                 flex: 0 0 40%;
//                 border-left: none;
//                 border-top: 2px solid rgba(255,255,255,0.2);
//             }
//         }

//         @media (max-width: 768px) {
//             .info-grid {
//                 grid-template-columns: 1fr;
//             }

//             .header h2 {
//                 font-size: 18px;
//             }
//         }
//     </style>
// </head>
// <body>
//     <!-- Header -->
//     <div class="header">
//         <h2>🎥 %2 - %3</h2>
//     </div>

//     <!-- Main Container -->
//     <div class="main-container">
//         <!-- ✅ LEFT SIDE - Video -->
//         <div class="video-section">
//             <div class="video-container">
//                 <video id="remotevideo" autoplay playsinline muted controls></video>
//             </div>
//         </div>

//         <!-- ✅ RIGHT SIDE - All Output -->
//         <div class="output-section">
//             <!-- Status Panel -->
//             <div class="status-panel">
//                 <div id="status" class="status">🔄 Connecting to live stream...</div>
//             </div>

//             <!-- Connection Info Panel -->
//             <div class="info-panel">
//                 <div class="info-title">📊 Connection Info</div>
//                 <div class="info-grid">
//                     <div class="info-item">
//                         <div class="info-label">Server</div>
//                         <div class="info-value">%5</div>
//                     </div>
//                     <div class="info-item">
//                         <div class="info-label">Session</div>
//                         <div class="info-value" id="session-id">Connecting...</div>
//                     </div>
//                     <div class="info-item">
//                         <div class="info-label">Handle</div>
//                         <div class="info-value" id="handle-id">Connecting...</div>
//                     </div>
//                     <div class="info-item">
//                         <div class="info-label">Stream ID</div>
//                         <div class="info-value">%6</div>
//                     </div>
//                 </div>
//                 <div class="info-grid">
//                     <div class="info-item">
//                         <div class="info-label">Video State</div>
//                         <div class="info-value" id="video-state">Initializing</div>
//                     </div>
//                     <div class="info-item">
//                         <div class="info-label">WebRTC</div>
//                         <div class="info-value" id="webrtc-state">Disconnected</div>
//                     </div>
//                 </div>
//             </div>

//             <!-- Debug Log Panel -->
//             <div class="debug-panel">
//                 <div class="debug-title">🔍 Debug Log</div>
//                 <div id="debug-log" class="debug-log">
//                     <div class="log-entry">System initializing...</div>
//                 </div>
//             </div>
//         </div>
//     </div>

//     <!-- Scripts -->
//     <script src="https://webrtc.github.io/adapter/adapter-latest.js"></script>
//     <script>%4</script>

//     <script>
//         // Global variables
//         let janus = null;
//         let streaming = null;
//         let videoElement = document.getElementById('remotevideo');
//         let id = %6;
//         let logCount = 0;

//         function debugLog(message, type = 'info') {
//             logCount++;
//             const timestamp = new Date().toLocaleTimeString();
//             console.log('[' + timestamp + '] ' + message);

//             const debugDiv = document.getElementById('debug-log');
//             const logEntry = document.createElement('div');
//             logEntry.className = 'log-entry log-' + type;
//             logEntry.innerHTML = '<strong>' + timestamp + ':</strong> ' + message;
//             debugDiv.appendChild(logEntry);
//             debugDiv.scrollTop = debugDiv.scrollHeight;

//             // Keep only last 100 entries
//             while (debugDiv.children.length > 100) {
//                 debugDiv.removeChild(debugDiv.firstChild);
//             }
//         }

//         function updateStatus(message) {
//             document.getElementById('status').innerHTML = message;
//             debugLog('STATUS: ' + message.replace(/🔄|✅|❌|🎥|🎉/g, '').trim());
//         }

//         function updateInfo(field, value) {
//             const element = document.getElementById(field);
//             if (element) {
//                 element.textContent = value;
//             }
//         }

//         // Initial checks
//         debugLog('🚀 Starting streaming application...', 'success');
//         debugLog('Browser: ' + navigator.userAgent.split(' ').pop());
//         debugLog('WebRTC support: ' + (typeof RTCPeerConnection !== 'undefined' ? 'Yes' : 'No'));

//         if (typeof adapter !== 'undefined') {
//             debugLog('✅ WebRTC Adapter loaded: ' + adapter.browserDetails.browser, 'success');
//         } else {
//             debugLog('⚠️ WebRTC Adapter not loaded', 'warning');
//         }

//         if (typeof Janus !== 'undefined') {
//             debugLog('✅ Janus library loaded successfully', 'success');
//         } else {
//             debugLog('❌ Janus library not loaded!', 'error');
//             updateStatus('❌ Janus library missing');
//         }

//         if (videoElement) {
//             debugLog('✅ Video element ready', 'success');
//             updateInfo('video-state', 'Ready');
//         } else {
//             debugLog('❌ Video element not found!', 'error');
//             updateInfo('video-state', 'Error');
//         }

//         // Start Janus initialization
//         updateStatus('🔄 Initializing Janus...');
//         debugLog('📡 Starting Janus.init()...', 'info');

//         Janus.init({
//             debug: 'all',
//             callback: function() {
//                 debugLog('✅ Janus.init() success!', 'success');
//                 updateStatus('🔄 Connecting to Janus server...');

//                 janus = new Janus({
//                     server: '%5',
//                     success: function() {
//                         debugLog('✅ Connected to Janus server!', 'success');
//                         updateStatus('🔄 Attaching to streaming plugin...');
//                         updateInfo('session-id', 'Connected');

//                         janus.attach({
//                             plugin: 'janus.plugin.streaming',
//                             success: function(pluginHandle) {
//                                 debugLog('✅ Attached to streaming plugin!', 'success');
//                                 updateStatus('🔄 Requesting video stream...');
//                                 updateInfo('handle-id', 'Attached');
//                                 streaming = pluginHandle;
//                                 streaming.send({ message: { request: 'watch', id: id } });
//                             },
//                             onmessage: function(msg, jsep) {
//                                 debugLog('📨 Message: ' + JSON.stringify(msg), 'info');

//                                 if (msg.error_code) {
//                                     debugLog('❌ Janus error ' + msg.error_code + ': ' + msg.error, 'error');
//                                     updateStatus('❌ Stream error: ' + msg.error);
//                                     return;
//                                 }

//                                 if (jsep) {
//                                     debugLog('🤝 Received WebRTC offer, creating answer...', 'success');
//                                     updateStatus('🔄 Processing video offer...');
//                                     updateInfo('webrtc-state', 'Negotiating');

//                                     streaming.createAnswer({
//                                         jsep: jsep,
//                                         media: { audioSend: false, videoSend: false },
//                                         success: function(jsep) {
//                                             debugLog('✅ Created WebRTC answer successfully!', 'success');
//                                             updateStatus('🔄 Starting video playback...');
//                                             streaming.send({ message: { request: 'start' }, jsep: jsep });
//                                         },
//                                         error: function(err) {
//                                             debugLog('❌ WebRTC answer error: ' + JSON.stringify(err), 'error');
//                                             updateStatus('❌ WebRTC connection failed');
//                                             updateInfo('webrtc-state', 'Failed');
//                                         }
//                                     });
//                                 }
//                             },
//                             onremotetrack: function(track, mid, on) {
//                                 debugLog('🎬 Remote track: ' + track.kind + ', enabled=' + on, on ? 'success' : 'warning');
//                                 if (!on) {
//                                     debugLog('⏹️ Track disabled', 'warning');
//                                     return;
//                                 }

//                                 debugLog('✅ Adding ' + track.kind + ' track to video element', 'success');
//                                 updateInfo('video-state', 'Receiving ' + track.kind);

//                                 if (!videoElement.srcObject) {
//                                     videoElement.srcObject = new MediaStream();
//                                     debugLog('📺 Created new MediaStream', 'info');
//                                 }

//                                 videoElement.srcObject.addTrack(track);
//                                 debugLog('✅ Track added successfully!', 'success');
//                                 updateStatus('🎥 Receiving ' + track.kind + ' stream!');
//                             },
//                             webrtcState: function(state) {
//                                 debugLog('🔗 WebRTC state: ' + state, state ? 'success' : 'warning');
//                                 if (state === true) {
//                                     updateStatus('✅ WebRTC Connected!');
//                                     updateInfo('webrtc-state', 'Connected');
//                                 } else if (state === false) {
//                                     updateStatus('❌ WebRTC Disconnected');
//                                     updateInfo('webrtc-state', 'Disconnected');
//                                 }
//                             },
//                             error: function(error) {
//                                 debugLog('❌ Plugin error: ' + error, 'error');
//                                 updateStatus('❌ Streaming plugin error');
//                             }
//                         });
//                     },
//                     error: function(error) {
//                         debugLog('❌ Janus server connection failed: ' + error, 'error');
//                         updateStatus('❌ Cannot connect to Janus server');
//                         updateInfo('session-id', 'Failed');
//                     }
//                 });
//             },
//             error: function(error) {
//                 debugLog('❌ Janus initialization failed: ' + error, 'error');
//                 updateStatus('❌ Janus initialization failed');
//             }
//         });

//         // Video element event handlers
//         videoElement.addEventListener('playing', function() {
//             debugLog('🎉 Video is now playing!', 'success');
//             updateStatus('🎉 Live video is playing!');
//             updateInfo('video-state', 'Playing');
//         });

//         videoElement.addEventListener('error', function(e) {
//             debugLog('📹 Video error: ' + (e.message || 'Unknown error'), 'error');
//             updateStatus('❌ Video playback error');
//             updateInfo('video-state', 'Error');
//         });

//         videoElement.addEventListener('loadstart', function() {
//             debugLog('📹 Video loading started', 'info');
//             updateInfo('video-state', 'Loading');
//         });

//         videoElement.addEventListener('canplay', function() {
//             debugLog('📹 Video ready to play', 'info');
//             updateInfo('video-state', 'Ready to play');
//         });

//         debugLog('🎯 JavaScript initialization complete', 'success');
//     </script>
// </body>
// </html>
//     )").arg(m_currentParams.roomName)   // %1
//                               .arg(m_currentParams.customerName) // %2
//                               .arg(m_currentParams.applianceName) // %3
//                               .arg(janusJsContent)               // %4
//                               .arg(m_janusUrl)                   // %5
//                               .arg(m_mountpointId);              // %6

//     m_webView->setHtml(htmlContent);
//     m_webView->show();

//     m_state = Streaming;
//     emit streamingStarted();
// }

void JanusConnector::startWebRTCStreaming()
{
    if (m_state != Ready) {
        qWarning() << "Cannot start WebRTC streaming, not ready";
        return;
    }

    qDebug() << "Starting WebRTC streaming";

    // **Load janus.js from resources**
    QFile janusFile(":/scripts/janus.js");
    QString janusJsContent;
    if (janusFile.open(QIODevice::ReadOnly)) {
        janusJsContent = QString::fromUtf8(janusFile.readAll());
        janusFile.close();
    } else {
        qWarning() << "Failed to load janus.js from resources";
        emit errorOccurred("Failed to load janus.js from resources");
        return;
    }

    // **Use template loader to generate HTML content**
    QString htmlContent = templateloader::loadStreamTemplate(
        m_currentParams,
        m_janusUrl,
        m_mountpointId,
        janusJsContent
        );

    if (htmlContent.isEmpty()) {
        emit errorOccurred("Failed to load stream template");
        return;
    }

    m_webView->setHtml(htmlContent);
    m_webView->show();

    m_state = Streaming;
    emit streamingStarted();
}

void JanusConnector::setupWebEngineView()
{
    m_webView->resize(1280, 720);
    m_webView->setWindowTitle("Janus WebRTC Stream");

    QWebEngineSettings *settings = m_webView->page()->settings();
    settings->setAttribute(QWebEngineSettings::JavascriptEnabled, true);
    settings->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
    settings->setAttribute(QWebEngineSettings::AllowRunningInsecureContent, true);

    m_webView->setContextMenuPolicy(Qt::DefaultContextMenu);

    m_webView->page()->setWebChannel(m_webChannel);
    m_webChannel->registerObject("qtConnector", this);
}

void JanusConnector::sendKeepAlive()
{
    if (m_sessionId > 0) {
        QJsonObject keepAlive;
        keepAlive["janus"] = "keepalive";
        keepAlive["session_id"] = m_sessionId;
        keepAlive["transaction"] = QString("tx-keepalive-%1")
                                       .arg(QDateTime::currentMSecsSinceEpoch());

        QJsonDocument doc(keepAlive);
        QNetworkRequest request(QUrl(QString("%1/%2").arg(m_janusUrl).arg(m_sessionId)));
        request.setHeader(QNetworkRequest::ContentTypeHeader, "application/json");

        QNetworkReply *reply = m_networkManager->post(request, doc.toJson());
        connect(reply, &QNetworkReply::finished, reply, &QNetworkReply::deleteLater);
    }
}

void JanusConnector::cleanup()
{
    m_keepAliveTimer->stop();

    if (m_currentReply) {
        m_currentReply->abort();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    if (m_webView) {
        m_webView->hide();
    }

    m_sessionId = 0;
    m_handleId = 0;
}

void JanusConnector::onNetworkError(QNetworkReply::NetworkError error)
{
    qDebug() << "Network error:" << error;

    if (m_currentReply) {
        qDebug() << "Error string:" << m_currentReply->errorString();
        m_currentReply->deleteLater();
        m_currentReply = nullptr;
    }

    emit errorOccurred(QString("Network error: %1").arg(static_cast<int>(error)));
    m_state = Idle;
    emit connectionStateChanged(false);
}
