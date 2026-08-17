// Copyright 2015-2016 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.
#include "esp_http_server.h"
#include "esp_timer.h"
#include "esp_camera.h"
#include "img_converters.h"
#include "fb_gfx.h"
#include "esp32-hal-ledc.h"
#include "sdkconfig.h"
#include "camera_index.h"
#include "board_config.h"
#include "Arduino.h"
#include <WiFi.h>
#include <HardwareSerial.h>

#if defined(ARDUINO_ARCH_ESP32) && defined(CONFIG_ARDUHAL_ESP_LOG)
#include "esp32-hal-log.h"
#endif

// LED FLASH setup
#if defined(LED_GPIO_NUM)
#define CONFIG_LED_MAX_INTENSITY 255

int led_duty = 0;
bool isStreaming = false;

#endif

typedef struct {
  httpd_req_t *req;
  size_t len;
} jpg_chunking_t;

static uint8_t speed_limit = 200;
static bool drive_enabled = false;

// Helper UART (ESP32-CAM UART1 -> UNO SoftwareSerial on D10/D11)
#define HELPER_UART_BAUD 38400
#define HELPER_UART_TX_PIN 13
#define HELPER_UART_RX_PIN 14

// Helper status (from UNO)
String helper_bat12 = "--";
String helper_bat5 = "--";
String helper_ina12 = "--";
String helper_ina5 = "--";
String helper_mcp = "--";
String helper_i2c = "--";
String helper_sonar = "0,0,0,0,0";
String helper_cliff = "0,0,0,0";
String helper_gps = "--,--,NOFIX,--,--";
String helper_sound = "0,0";
extern String helper_last_line;
extern unsigned long helper_last_ms;

  void initHelperUart() {
    Serial1.begin(HELPER_UART_BAUD, SERIAL_8N1, HELPER_UART_RX_PIN, HELPER_UART_TX_PIN);
    Serial.println("Helper UART ready");
  }

void sendHelperCmd(const String& cmd);

// Motor control is delegated to UNO helper

#define PART_BOUNDARY "123456789000000000000987654321"
static const char *_STREAM_CONTENT_TYPE = "multipart/x-mixed-replace;boundary=" PART_BOUNDARY;
static const char *_STREAM_BOUNDARY = "\r\n--" PART_BOUNDARY "\r\n";
static const char *_STREAM_PART = "Content-Type: image/jpeg\r\nContent-Length: %u\r\nX-Timestamp: %d.%06d\r\n\r\n";

static const char INDEX_HTML[] PROGMEM = R"HTML(
<!doctype html>
<html>
<head>
  <meta name="viewport" content="width=device-width,initial-scale=1">
  <title>FPV Rover v2.5.0</title>
  <style>
    body{font-family:Verdana, Geneva, sans-serif;margin:0;padding:0;background:#101417;color:#e8eef2;}
    .row{padding:4px 6px;display:flex;gap:6px;flex-wrap:wrap;}
    .card{background:#1c242b;border-radius:8px;padding:6px;flex:1;min-width:150px;box-shadow:inset 0 0 0 1px #2c3a44;}
    .card.ok{background:#1f6a44;box-shadow:inset 0 0 0 1px #46d68c;}
    .card.warn{background:#7a5810;box-shadow:inset 0 0 0 1px #ffca3a;}
    .card.bad{background:#8f2631;box-shadow:inset 0 0 0 1px #ff7b8a;}
    .card.auto{flex:0 0 auto;min-width:unset;}
    .controls .card{padding:4px;}
    .controls .btn{padding:6px 10px;}
    .controls select{padding:4px 6px;}
    .label{font-size:12px;opacity:.8;margin-bottom:2px;letter-spacing:.2px;}
    .btn{padding:8px 12px;margin:0;border:0;border-radius:8px;background:#2b7bff;color:#fff;}
    .joy{width:120px;height:120px;background:rgba(15,26,34,0.85);border:1px solid #2c3a44;border-radius:12px;position:relative;touch-action:none;}
    .knob{width:40px;height:40px;border-radius:50%;background:#2b7bff;position:absolute;left:50%;top:50%;transform:translate(-50%,-50%);}
    .stream-wrap{position:relative;width:100%;max-width:800px;aspect-ratio:4/3;background:linear-gradient(135deg,#111b22,#0a0d10);overflow:hidden;border-radius:10px;margin:0 auto;}
    .stream-wrap img{width:100%;height:100%;object-fit:contain;display:block;}
    .stream-placeholder{position:absolute;inset:0;display:flex;align-items:center;justify-content:center;color:#90a4ae;font-size:18px;letter-spacing:.3px;}
    .sensor-label{position:absolute;z-index:4;color:#fff;font-weight:700;font-size:13px;text-shadow:-1px -1px 0 #000,1px -1px 0 #000,-1px 1px 0 #000,1px 1px 0 #000,0 0 6px #000;pointer-events:none;transform:translate(-50%,-50%);}
    .sensor-label.bad{color:#ffef6e;}
    .sensor-label.cliff{color:#8cf5ff;}
    .overlay-joy{position:absolute;right:8px;bottom:8px;z-index:5;}
    .joy-inline{display:none;justify-content:flex-end;}
    .joy-inline .card{display:flex;flex-direction:column;align-items:flex-end;}
    .slider-bar{align-items:center;padding-left:10px;padding-right:10px;}
    .slider-bar .mode-card{margin:0 auto;}
    .slider-bar .speed-card{margin-left:auto;}
    .overlay-joy .label{color:#cfd8dc;}
    .status-lights{display:flex;gap:10px;flex-wrap:wrap;align-items:center;}
    .status-item{display:flex;align-items:center;gap:6px;font-size:12px;color:#d7e2e9;}
    .status-light{width:13px;height:13px;border-radius:50%;background:#42515a;box-shadow:inset 0 0 0 1px #60717a;}
    .status-light.ok{background:#63f59f;box-shadow:0 0 10px rgba(99,245,159,.9), inset 0 0 0 1px #d6ffe5;}
    .status-light.bad{background:#ff7b8a;box-shadow:0 0 10px rgba(255,123,138,.9), inset 0 0 0 1px #ffd4db;}
  </style>
</head>
<body>
  <div class="row">
    <div class="card auto"><div class="label">WiFi</div><div id="wifi">--</div></div>
    <div class="card auto"><div class="label">Mode</div><div id="wifimode">--</div></div>
    <div class="card auto" id="rssiCard"><div class="label">RSSI</div><div id="rssi">--</div></div>
    <div class="card auto"><div class="label">IP</div><div id="ip">--</div></div>
    <div class="card auto" id="tempCard"><div class="label">ESP32 Temp</div><div id="temp">--</div></div>
    <div class="card auto"><div class="label">PSRAM</div><div id="psram">--</div></div>
    <div class="card auto"><div class="label">SD</div><div id="sd">Not Mounted</div></div>
    <div class="card auto" id="bat12Card"><div class="label">Battery (12V)</div><div id="bat12">--</div></div>
    <div class="card auto" id="bat5Card"><div class="label">Battery (5V)</div><div id="bat5">--</div></div>
    <div class="card auto"><div class="label">GPS</div><div id="gpsLine">--</div></div>
  </div>
  <div class="row controls">
    <div class="card auto">
      <button class="btn" id="streamBtn" onclick="toggleStream()">Start</button>
    </div>
    <div class="card auto">
      <select id="orient" onchange="setOrient()">
        <option value="normal">Normal</option>
        <option value="mirror">Mirror</option>
        <option value="flip">Flip</option>
        <option value="mirrorflip">Mirror+Flip</option>
      </select>
    </div>
    <div class="card auto">
      <button class="btn" id="fsBtn" onclick="toggleFullscreen()">Full</button>
    </div>
  </div>
  <div class="row">
    <div class="card">
      <div class="stream-wrap">
        <div class="stream-placeholder" id="streamPlaceholder">Stream stopped</div>
        <img id="streamImg" src="">
        <div class="sensor-label" id="sonarLs" style="left:8%;top:50%;">LS -- in</div>
        <div class="sensor-label" id="sonarFl" style="left:28%;top:19%;">FL -- in</div>
        <div class="sensor-label" id="sonarFc" style="left:50%;top:10%;">FC -- in</div>
        <div class="sensor-label" id="sonarFr" style="left:72%;top:19%;">FR -- in</div>
        <div class="sensor-label" id="sonarRs" style="left:92%;top:50%;">RS -- in</div>
        <div class="sensor-label cliff" id="cliffFl" style="left:38%;top:32%;">IR FL OK</div>
        <div class="sensor-label cliff" id="cliffFr" style="left:62%;top:32%;">IR FR OK</div>
        <div class="sensor-label cliff" id="cliffLs" style="left:20%;top:62%;">IR LS OK</div>
        <div class="sensor-label cliff" id="cliffRs" style="left:80%;top:62%;">IR RS OK</div>
        <div class="sensor-label" id="soundLabel" style="left:50%;top:88%;">Sound quiet</div>
        <div class="overlay-joy" id="joyOverlay">
          <div class="joy" id="joyDrive"><div class="knob" id="knobDrive"></div></div>
        </div>
      </div>
    </div>
  </div>
  <div class="row joy-inline" id="joyInline">
    <div class="card auto">
      <div class="joy" id="joyDriveInline"><div class="knob" id="knobDriveInline"></div></div>
    </div>
  </div>
  <div class="row slider-bar">
    <div class="card auto">
      <div class="label">Camera LED</div>
      <input type="range" min="0" max="255" value="0" id="led" oninput="setLed(this.value)">
      <div id="ledVal" class="label">0</div>
    </div>
    <div class="card auto mode-card">
      <div class="label">Mode</div>
      <label><input type="radio" name="mode" value="manual" checked onchange="setMode(this.value)"> Manual</label>
      <label style="margin-left:10px;"><input type="radio" name="mode" value="auto" onchange="setMode(this.value)"> Auto</label>
      <label style="margin-left:10px;"><input type="radio" name="mode" value="trail" onchange="setMode(this.value)"> Trail</label>
    </div>
    <div class="card auto output-card">
      <div class="label">Drive Enable</div>
      <label><input type="checkbox" id="outEnable" onchange="setOutputEnable(this.checked)"> Enabled</label>
    </div>
    <div class="card auto speed-card">
      <div class="label">Speed Limit</div>
      <input type="range" min="0" max="255" value="200" id="spd" oninput="setSpeed(this.value)">
      <div id="spdVal" class="label">200</div>
    </div>
  </div>
  <div class="row">
    <div class="card auto">
      <button class="btn" onclick="returnHome()">Return To Power-Up Coordinates</button>
    </div>
  </div>
  <script>
    let streaming=false;
    function toggleStream(){
      const img=document.getElementById('streamImg');
      const btn=document.getElementById('streamBtn');
      const overlay=document.getElementById('joyOverlay');
      const inline=document.getElementById('joyInline');
      const placeholder=document.getElementById('streamPlaceholder');
      if(!streaming){
        img.src='http://'+location.hostname+':81/stream';
        btn.innerText='Stop Stream';
        streaming=true;
        placeholder.style.display='none';
        overlay.style.display='block';
        inline.style.display='none';
      } else {
        img.src='';
        btn.innerText='Start Stream';
        streaming=false;
        placeholder.style.display='flex';
        overlay.style.display='none';
        inline.style.display='flex';
      }
    }
    function setOrient(){
      const v=document.getElementById('orient').value;
      fetch('/orient?o='+v).catch(()=>{});
    }
    function toggleFullscreen(){
      const el=document.documentElement;
      if (!document.fullscreenElement) {
        el.requestFullscreen().catch(()=>{});
      } else {
        document.exitFullscreen().catch(()=>{});
      }
    }
    function setMode(v){
      fetch('/mode?m='+v).catch(()=>{});
    }
    function setOutputEnable(v){
      fetch('/outputenable?e='+(v?1:0)).catch(()=>{});
    }
    function setSpeed(v){
      document.getElementById('spdVal').innerText = v;
      document.getElementById('spd').value = v;
      fetch('/speed?val='+v).catch(()=>{});
    }
    function setLed(v){
      document.getElementById('ledVal').innerText = v;
      fetch('/led?val='+v).catch(()=>{});
    }
    function returnHome(){
      fetch('/returnhome').catch(()=>{});
      document.querySelector('input[name="mode"][value="auto"]').checked=false;
    }
    function setSensorText(id, label, value, suffix, bad){
      const el=document.getElementById(id);
      el.innerText = label + ' ' + value + suffix;
      el.classList.toggle('bad', !!bad);
    }
    function updateSensors(){
      fetch('/sensors').then(r=>r.json()).then(d=>{
        const sonar=d.sonar || {};
        setSensorText('sonarLs','LS', sonar.ls ?? '--', ' in', sonar.ls > 0 && sonar.ls <= 6);
        setSensorText('sonarFl','FL', sonar.fl ?? '--', ' in', sonar.fl > 0 && sonar.fl <= 12);
        setSensorText('sonarFc','FC', sonar.fc ?? '--', ' in', sonar.fc > 0 && sonar.fc <= 12);
        setSensorText('sonarFr','FR', sonar.fr ?? '--', ' in', sonar.fr > 0 && sonar.fr <= 12);
        setSensorText('sonarRs','RS', sonar.rs ?? '--', ' in', sonar.rs > 0 && sonar.rs <= 6);
        const cliff=d.cliff || {};
        setSensorText('cliffFl','IR FL', cliff.fl ? 'CLIFF' : 'OK', '', cliff.fl);
        setSensorText('cliffFr','IR FR', cliff.fr ? 'CLIFF' : 'OK', '', cliff.fr);
        setSensorText('cliffLs','IR LS', cliff.ls ? 'CLIFF' : 'OK', '', cliff.ls);
        setSensorText('cliffRs','IR RS', cliff.rs ? 'CLIFF' : 'OK', '', cliff.rs);
        const soundText = d.sound && d.sound.hit ? 'Sound hit' : 'Sound quiet';
        setSensorText('soundLabel', soundText, d.sound ? d.sound.age_ms : '--', ' ms', d.sound && d.sound.hit);
        document.getElementById('gpsLine').innerText =
          d.gps && d.gps.fix ? (d.gps.lat + ', ' + d.gps.lon) : 'No fix';
      }).catch(()=>{});
    }
    function updateStatus(){
      fetch('/psram').then(r=>r.json()).then(d=>{
        document.getElementById('psram').innerText =
          d.ok ? ('OK | ' + d.free + ' / ' + d.total + ' bytes free') : 'FAIL';
      }).catch(()=>{});
      fetch('/wifistatus').then(r=>r.json()).then(d=>{
        document.getElementById('wifi').innerText = d.ssid;
        document.getElementById('wifimode').innerText = d.mode;
        document.getElementById('rssi').innerText = d.rssi + ' dBm';
        document.getElementById('ip').innerText = d.ip;
        document.getElementById('temp').innerText = d.temp_f;
        const tc = document.getElementById('tempCard');
        tc.classList.remove('ok','warn','bad');
        const tf = parseFloat(String(d.temp_f).replace(' F',''));
        if (!isNaN(tf)) {
          if (tf < 140) tc.classList.add('ok');
          else if (tf < 160) tc.classList.add('warn');
          else tc.classList.add('bad');
        }
        const rssi = parseInt(d.rssi, 10);
        const rc = document.getElementById('rssiCard');
        rc.classList.remove('ok','warn','bad');
        if (!isNaN(rssi)) {
          if (rssi >= -60) rc.classList.add('ok');
          else if (rssi >= -75) rc.classList.add('warn');
          else rc.classList.add('bad');
        }
      }).catch(()=>{});
      fetch('/battery').then(r=>r.json()).then(d=>{
        document.getElementById('bat12').innerText = d.v12;
        document.getElementById('bat5').innerText = d.v5;
        const v12 = parseFloat(d.v12);
        const v5 = parseFloat(d.v5);
        const b12 = document.getElementById('bat12Card');
        const b5 = document.getElementById('bat5Card');
        b12.classList.remove('ok','warn','bad');
        b5.classList.remove('ok','warn','bad');
        if (!isNaN(v12)) {
          if (v12 >= 12.0) b12.classList.add('ok');
          else if (v12 >= 10.0) b12.classList.add('warn');
          else b12.classList.add('bad');
        }
        if (!isNaN(v5)) {
          if (v5 >= 4.75) b5.classList.add('ok');
          else if (v5 >= 4.5) b5.classList.add('warn');
          else b5.classList.add('bad');
        }
      }).catch(()=>{});
      updateSensors();
      fetch('/uart').then(r=>r.json()).then(d=>{
        const ttl = document.getElementById('ttlLight');
        ttl.classList.remove('ok','bad');
        ttl.classList.add(d.status === 'OK' ? 'ok' : 'bad');
      }).catch(()=>{});
    }
    setInterval(updateStatus, 2000);
    updateStatus();

    function setupJoy(areaId, knobId, onMove, onEnd){
      const area=document.getElementById(areaId);
      const knob=document.getElementById(knobId);
      let active=false;
      function pos(e){
        const r=area.getBoundingClientRect();
        const x=(e.touches?e.touches[0].clientX:e.clientX)-r.left-r.width/2;
        const y=(e.touches?e.touches[0].clientY:e.clientY)-r.top-r.height/2;
        return {x:x, y:y, r:r};
      }
      function move(e){
        if(!active) return;
        const p=pos(e);
        const max=area.clientWidth/2-20;
        const dx=Math.max(-max,Math.min(max,p.x));
        const dy=Math.max(-max,Math.min(max,p.y));
        knob.style.left=(p.r.width/2+dx)+"px";
        knob.style.top=(p.r.height/2+dy)+"px";
        onMove(dx,dy,max);
      }
      function end(){ active=false; knob.style.left="50%"; knob.style.top="50%"; onEnd(); }
      area.addEventListener('mousedown',e=>{active=true; move(e);});
      area.addEventListener('mousemove',move);
      area.addEventListener('mouseup',end);
      area.addEventListener('mouseleave',end);
      area.addEventListener('touchstart',e=>{active=true; move(e);}, {passive:true});
      area.addEventListener('touchmove',move, {passive:true});
      area.addEventListener('touchend',end);
    }

    setupJoy('joyDrive','knobDrive', (dx,dy,max)=>{
      const ax=Math.abs(dx), ay=Math.abs(dy);
      let cmd='STOP';
      if (ay>ax){ if(dy<-10) cmd='FWD'; else if(dy>10) cmd='REV'; }
      else { if(dx<-10) cmd='LEFT'; else if(dx>10) cmd='RIGHT'; }
      fetch('/drive?c='+cmd).catch(()=>{});
    }, ()=>fetch('/drive?c=STOP').catch(()=>{}));

    setupJoy('joyDriveInline','knobDriveInline', (dx,dy,max)=>{
      const ax=Math.abs(dx), ay=Math.abs(dy);
      let cmd='STOP';
      if (ay>ax){ if(dy<-10) cmd='FWD'; else if(dy>10) cmd='REV'; }
      else { if(dx<-10) cmd='LEFT'; else if(dx>10) cmd='RIGHT'; }
      fetch('/drive?c='+cmd).catch(()=>{});
    }, ()=>fetch('/drive?c=STOP').catch(()=>{}));

    // Initial state: stream off
    document.getElementById('joyOverlay').style.display='none';
    document.getElementById('joyInline').style.display='flex';

    document.addEventListener('keydown',e=>{
      if(e.repeat) return;
      if(e.key==='w'||e.key==='W'){ e.preventDefault(); fetch('/drive?c=FWD').catch(()=>{}); }
      if(e.key==='s'||e.key==='S'){ e.preventDefault(); fetch('/drive?c=REV').catch(()=>{}); }
      if(e.key==='a'||e.key==='A'){ e.preventDefault(); fetch('/drive?c=LEFT').catch(()=>{}); }
      if(e.key==='d'||e.key==='D'){ e.preventDefault(); fetch('/drive?c=RIGHT').catch(()=>{}); }
    });
    document.addEventListener('keyup',e=>{
      if(e.key==='w'||e.key==='W'||e.key==='s'||e.key==='S'||e.key==='a'||e.key==='A'||e.key==='d'||e.key==='D'){
        e.preventDefault();
      }
      fetch('/drive?c=STOP').catch(()=>{});
    });
  </script>
</body>
</html>
)HTML";

httpd_handle_t stream_httpd = NULL;
httpd_handle_t camera_httpd = NULL;

typedef struct {
  size_t size;   //number of values used for filtering
  size_t index;  //current value index
  size_t count;  //value count
  int sum;
  int *values;  //array to be filled with values
} ra_filter_t;

static ra_filter_t ra_filter;

static ra_filter_t *ra_filter_init(ra_filter_t *filter, size_t sample_size) {
  memset(filter, 0, sizeof(ra_filter_t));

  filter->values = (int *)malloc(sample_size * sizeof(int));
  if (!filter->values) {
    return NULL;
  }
  memset(filter->values, 0, sample_size * sizeof(int));

  filter->size = sample_size;
  return filter;
}

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
static int ra_filter_run(ra_filter_t *filter, int value) {
  if (!filter->values) {
    return value;
  }
  filter->sum -= filter->values[filter->index];
  filter->values[filter->index] = value;
  filter->sum += filter->values[filter->index];
  filter->index++;
  filter->index = filter->index % filter->size;
  if (filter->count < filter->size) {
    filter->count++;
  }
  return filter->sum / filter->count;
}
#endif

#if defined(LED_GPIO_NUM)
void enable_led(bool en) {  // Turn LED On or Off
  int duty = en ? led_duty : 0;
  if (en && isStreaming && (led_duty > CONFIG_LED_MAX_INTENSITY)) {
    duty = CONFIG_LED_MAX_INTENSITY;
  }
  ledcWrite(LED_GPIO_NUM, duty);
  //ledc_set_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL, duty);
  //ledc_update_duty(CONFIG_LED_LEDC_SPEED_MODE, CONFIG_LED_LEDC_CHANNEL);
  log_i("Set LED intensity to %d", duty);
}
#endif

static esp_err_t bmp_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_start = esp_timer_get_time();
#endif
  fb = esp_camera_fb_get();
  if (!fb) {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/x-windows-bmp");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.bmp");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

  uint8_t *buf = NULL;
  size_t buf_len = 0;
  bool converted = frame2bmp(fb, &buf, &buf_len);
  esp_camera_fb_return(fb);
  if (!converted) {
    log_e("BMP Conversion failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }
  res = httpd_resp_send(req, (const char *)buf, buf_len);
  free(buf);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  uint64_t fr_end = esp_timer_get_time();
#endif
  log_i("BMP: %llums, %uB", (uint64_t)((fr_end - fr_start) / 1000), buf_len);
  return res;
}

static size_t jpg_encode_stream(void *arg, size_t index, const void *data, size_t len) {
  jpg_chunking_t *j = (jpg_chunking_t *)arg;
  if (!index) {
    j->len = 0;
  }
  if (httpd_resp_send_chunk(j->req, (const char *)data, len) != ESP_OK) {
    return 0;
  }
  j->len += len;
  return len;
}

static esp_err_t capture_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  esp_err_t res = ESP_OK;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_start = esp_timer_get_time();
#endif

#if defined(LED_GPIO_NUM)
  enable_led(true);
  vTaskDelay(150 / portTICK_PERIOD_MS);  // The LED needs to be turned on ~150ms before the call to esp_camera_fb_get()
  fb = esp_camera_fb_get();              // or it won't be visible in the frame. A better way to do this is needed.
  enable_led(false);
#else
  fb = esp_camera_fb_get();
#endif

  if (!fb) {
    log_e("Camera capture failed");
    httpd_resp_send_500(req);
    return ESP_FAIL;
  }

  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Content-Disposition", "inline; filename=capture.jpg");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");

  char ts[32];
  snprintf(ts, 32, "%lld.%06ld", fb->timestamp.tv_sec, fb->timestamp.tv_usec);
  httpd_resp_set_hdr(req, "X-Timestamp", (const char *)ts);

#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  size_t fb_len = 0;
#endif
  if (fb->format == PIXFORMAT_JPEG) {
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    fb_len = fb->len;
#endif
    res = httpd_resp_send(req, (const char *)fb->buf, fb->len);
  } else {
    jpg_chunking_t jchunk = {req, 0};
    res = frame2jpg_cb(fb, 80, jpg_encode_stream, &jchunk) ? ESP_OK : ESP_FAIL;
    httpd_resp_send_chunk(req, NULL, 0);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    fb_len = jchunk.len;
#endif
  }
  esp_camera_fb_return(fb);
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
  int64_t fr_end = esp_timer_get_time();
#endif
  log_i("JPG: %uB %ums", (uint32_t)(fb_len), (uint32_t)((fr_end - fr_start) / 1000));
  return res;
}

static esp_err_t stream_handler(httpd_req_t *req) {
  camera_fb_t *fb = NULL;
  struct timeval _timestamp;
  esp_err_t res = ESP_OK;
  size_t _jpg_buf_len = 0;
  uint8_t *_jpg_buf = NULL;
  char *part_buf[128];

  static int64_t last_frame = 0;
  if (!last_frame) {
    last_frame = esp_timer_get_time();
  }

  res = httpd_resp_set_type(req, _STREAM_CONTENT_TYPE);
  if (res != ESP_OK) {
    return res;
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  httpd_resp_set_hdr(req, "X-Framerate", "60");

#if defined(LED_GPIO_NUM)
  isStreaming = true;
  enable_led(true);
#endif

  while (true) {
    fb = esp_camera_fb_get();
    if (!fb) {
      log_e("Camera capture failed");
      res = ESP_FAIL;
    } else {
      _timestamp.tv_sec = fb->timestamp.tv_sec;
      _timestamp.tv_usec = fb->timestamp.tv_usec;
      if (fb->format != PIXFORMAT_JPEG) {
        bool jpeg_converted = frame2jpg(fb, 80, &_jpg_buf, &_jpg_buf_len);
        esp_camera_fb_return(fb);
        fb = NULL;
        if (!jpeg_converted) {
          log_e("JPEG compression failed");
          res = ESP_FAIL;
        }
      } else {
        _jpg_buf_len = fb->len;
        _jpg_buf = fb->buf;
      }
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, _STREAM_BOUNDARY, strlen(_STREAM_BOUNDARY));
    }
    if (res == ESP_OK) {
      size_t hlen = snprintf((char *)part_buf, 128, _STREAM_PART, _jpg_buf_len, _timestamp.tv_sec, _timestamp.tv_usec);
      res = httpd_resp_send_chunk(req, (const char *)part_buf, hlen);
    }
    if (res == ESP_OK) {
      res = httpd_resp_send_chunk(req, (const char *)_jpg_buf, _jpg_buf_len);
    }
    if (fb) {
      esp_camera_fb_return(fb);
      fb = NULL;
      _jpg_buf = NULL;
    } else if (_jpg_buf) {
      free(_jpg_buf);
      _jpg_buf = NULL;
    }
    if (res != ESP_OK) {
      log_e("Send frame failed");
      break;
    }
    int64_t fr_end = esp_timer_get_time();

    int64_t frame_time = fr_end - last_frame;
    last_frame = fr_end;

    frame_time /= 1000;
#if ARDUHAL_LOG_LEVEL >= ARDUHAL_LOG_LEVEL_INFO
    uint32_t avg_frame_time = ra_filter_run(&ra_filter, frame_time);
#endif
    log_i(
      "MJPG: %uB %ums (%.1ffps), AVG: %ums (%.1ffps)", (uint32_t)(_jpg_buf_len), (uint32_t)frame_time, 1000.0 / (uint32_t)frame_time, avg_frame_time,
      1000.0 / avg_frame_time
    );
  }

#if defined(LED_GPIO_NUM)
  isStreaming = false;
  enable_led(false);
#endif

  return res;
}

static esp_err_t parse_get(httpd_req_t *req, char **obuf) {
  char *buf = NULL;
  size_t buf_len = 0;

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
      httpd_resp_send_500(req);
      return ESP_FAIL;
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      *obuf = buf;
      return ESP_OK;
    }
    free(buf);
  }
  httpd_resp_send_404(req);
  return ESP_FAIL;
}

static esp_err_t cmd_handler(httpd_req_t *req) {
  char *buf = NULL;
  char variable[32];
  char value[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "var", variable, sizeof(variable)) != ESP_OK || httpd_query_key_value(buf, "val", value, sizeof(value)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int val = atoi(value);
  log_i("%s = %d", variable, val);
  sensor_t *s = esp_camera_sensor_get();
  int res = 0;

  if (!strcmp(variable, "framesize")) {
    if (s->pixformat == PIXFORMAT_JPEG) {
      res = s->set_framesize(s, (framesize_t)val);
    }
  } else if (!strcmp(variable, "quality")) {
    res = s->set_quality(s, val);
  } else if (!strcmp(variable, "contrast")) {
    res = s->set_contrast(s, val);
  } else if (!strcmp(variable, "brightness")) {
    res = s->set_brightness(s, val);
  } else if (!strcmp(variable, "saturation")) {
    res = s->set_saturation(s, val);
  } else if (!strcmp(variable, "gainceiling")) {
    res = s->set_gainceiling(s, (gainceiling_t)val);
  } else if (!strcmp(variable, "colorbar")) {
    res = s->set_colorbar(s, val);
  } else if (!strcmp(variable, "awb")) {
    res = s->set_whitebal(s, val);
  } else if (!strcmp(variable, "agc")) {
    res = s->set_gain_ctrl(s, val);
  } else if (!strcmp(variable, "aec")) {
    res = s->set_exposure_ctrl(s, val);
  } else if (!strcmp(variable, "hmirror")) {
    res = s->set_hmirror(s, val);
  } else if (!strcmp(variable, "vflip")) {
    res = s->set_vflip(s, val);
  } else if (!strcmp(variable, "awb_gain")) {
    res = s->set_awb_gain(s, val);
  } else if (!strcmp(variable, "agc_gain")) {
    res = s->set_agc_gain(s, val);
  } else if (!strcmp(variable, "aec_value")) {
    res = s->set_aec_value(s, val);
  } else if (!strcmp(variable, "aec2")) {
    res = s->set_aec2(s, val);
  } else if (!strcmp(variable, "dcw")) {
    res = s->set_dcw(s, val);
  } else if (!strcmp(variable, "bpc")) {
    res = s->set_bpc(s, val);
  } else if (!strcmp(variable, "wpc")) {
    res = s->set_wpc(s, val);
  } else if (!strcmp(variable, "raw_gma")) {
    res = s->set_raw_gma(s, val);
  } else if (!strcmp(variable, "lenc")) {
    res = s->set_lenc(s, val);
  } else if (!strcmp(variable, "special_effect")) {
    res = s->set_special_effect(s, val);
  } else if (!strcmp(variable, "wb_mode")) {
    res = s->set_wb_mode(s, val);
  } else if (!strcmp(variable, "ae_level")) {
    res = s->set_ae_level(s, val);
  }
#if defined(LED_GPIO_NUM)
  else if (!strcmp(variable, "led_intensity")) {
    led_duty = val;
    if (isStreaming) {
      enable_led(true);
    }
  }
#endif
  else {
    log_i("Unknown command: %s", variable);
    res = -1;
  }

  if (res < 0) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static int print_reg(char *p, sensor_t *s, uint16_t reg, uint32_t mask) {
  return sprintf(p, "\"0x%x\":%u,", reg, s->get_reg(s, reg, mask));
}

static esp_err_t psram_handler(httpd_req_t *req) {
  char json[128];
  bool ok = psramFound();
  uint32_t total = ok ? ESP.getPsramSize() : 0;
  uint32_t free = ok ? ESP.getFreePsram() : 0;
  int len = snprintf(json, sizeof(json),
                     "{\"ok\":%s,\"total\":%u,\"free\":%u}",
                     ok ? "true" : "false", total, free);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, len);
}

static esp_err_t wifi_status_handler(httpd_req_t *req) {
  char json[256];
  const char *mode = WiFi.isConnected() ? "Client" : "AP";
  String ssid = WiFi.isConnected() ? WiFi.SSID() : String("ESP32-AP");
  String ip = WiFi.isConnected() ? WiFi.localIP().toString() : WiFi.softAPIP().toString();
  int rssi = WiFi.isConnected() ? WiFi.RSSI() : -99;
  float c = temperatureRead();
  float f = (c * 9.0f / 5.0f) + 32.0f;
  char temp_f[16];
  if (isnan(c)) {
    strcpy(temp_f, "--");
  } else {
    snprintf(temp_f, sizeof(temp_f), "%.1f F", f);
  }
  int len = snprintf(json, sizeof(json),
                     "{\"mode\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"rssi\":%d,\"temp_f\":\"%s\"}",
                     mode, ssid.c_str(), ip.c_str(), rssi, temp_f);
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json, len);
}

static esp_err_t battery_handler(httpd_req_t *req) {
    String json = "{\"v12\":\"" + helper_bat12 +
                  "\",\"v5\":\"" + helper_bat5 +
                  "\",\"ina12\":\"" + helper_ina12 +
                  "\",\"ina5\":\"" + helper_ina5 +
                  "\",\"mcp\":\"" + helper_mcp +
                  "\",\"i2c\":\"" + helper_i2c +
                  "\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
    return httpd_resp_send(req, json.c_str(), json.length());
  }

static String csvField(const String &line, uint8_t index) {
  uint8_t field = 0;
  int start = 0;
  for (int i = 0; i <= line.length(); i++) {
    if (i == line.length() || line.charAt(i) == ',') {
      if (field == index) return line.substring(start, i);
      field++;
      start = i + 1;
    }
  }
  return "--";
}

static int csvInt(const String &line, uint8_t index) {
  String v = csvField(line, index);
  return (v == "--") ? 0 : v.toInt();
}

static esp_err_t sensors_handler(httpd_req_t *req) {
  String gpsLat = csvField(helper_gps, 0);
  String gpsLon = csvField(helper_gps, 1);
  String gpsFix = csvField(helper_gps, 2);
  String gpsHomeLat = csvField(helper_gps, 3);
  String gpsHomeLon = csvField(helper_gps, 4);
  String json = "{";
  json += "\"sonar\":{\"ls\":" + String(csvInt(helper_sonar, 0)) +
          ",\"fl\":" + String(csvInt(helper_sonar, 1)) +
          ",\"fc\":" + String(csvInt(helper_sonar, 2)) +
          ",\"fr\":" + String(csvInt(helper_sonar, 3)) +
          ",\"rs\":" + String(csvInt(helper_sonar, 4)) + "},";
  json += "\"cliff\":{\"fl\":" + String(csvInt(helper_cliff, 0) ? "true" : "false") +
          ",\"fr\":" + String(csvInt(helper_cliff, 1) ? "true" : "false") +
          ",\"ls\":" + String(csvInt(helper_cliff, 2) ? "true" : "false") +
          ",\"rs\":" + String(csvInt(helper_cliff, 3) ? "true" : "false") + "},";
  json += "\"gps\":{\"fix\":" + String(gpsFix == "FIX" ? "true" : "false") +
          ",\"lat\":\"" + gpsLat + "\",\"lon\":\"" + gpsLon +
          "\",\"home_lat\":\"" + gpsHomeLat + "\",\"home_lon\":\"" + gpsHomeLon + "\"},";
  json += "\"sound\":{\"hit\":" + String(csvInt(helper_sound, 0) ? "true" : "false") +
          ",\"age_ms\":" + String(csvInt(helper_sound, 1)) + "}";
  json += "}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t status_handler(httpd_req_t *req) {
  static char json_response[1024];

  sensor_t *s = esp_camera_sensor_get();
  char *p = json_response;
  *p++ = '{';

  if (s->id.PID == OV5640_PID || s->id.PID == OV3660_PID) {
    for (int reg = 0x3400; reg < 0x3406; reg += 2) {
      p += print_reg(p, s, reg, 0xFFF);  //12 bit
    }
    p += print_reg(p, s, 0x3406, 0xFF);

    p += print_reg(p, s, 0x3500, 0xFFFF0);  //16 bit
    p += print_reg(p, s, 0x3503, 0xFF);
    p += print_reg(p, s, 0x350a, 0x3FF);   //10 bit
    p += print_reg(p, s, 0x350c, 0xFFFF);  //16 bit

    for (int reg = 0x5480; reg <= 0x5490; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }

    for (int reg = 0x5380; reg <= 0x538b; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }

    for (int reg = 0x5580; reg < 0x558a; reg++) {
      p += print_reg(p, s, reg, 0xFF);
    }
    p += print_reg(p, s, 0x558a, 0x1FF);  //9 bit
  } else if (s->id.PID == OV2640_PID) {
    p += print_reg(p, s, 0xd3, 0xFF);
    p += print_reg(p, s, 0x111, 0xFF);
    p += print_reg(p, s, 0x132, 0xFF);
  }

  p += sprintf(p, "\"xclk\":%u,", s->xclk_freq_hz / 1000000);
  p += sprintf(p, "\"pixformat\":%u,", s->pixformat);
  p += sprintf(p, "\"framesize\":%u,", s->status.framesize);
  p += sprintf(p, "\"quality\":%u,", s->status.quality);
  p += sprintf(p, "\"brightness\":%d,", s->status.brightness);
  p += sprintf(p, "\"contrast\":%d,", s->status.contrast);
  p += sprintf(p, "\"saturation\":%d,", s->status.saturation);
  p += sprintf(p, "\"sharpness\":%d,", s->status.sharpness);
  p += sprintf(p, "\"special_effect\":%u,", s->status.special_effect);
  p += sprintf(p, "\"wb_mode\":%u,", s->status.wb_mode);
  p += sprintf(p, "\"awb\":%u,", s->status.awb);
  p += sprintf(p, "\"awb_gain\":%u,", s->status.awb_gain);
  p += sprintf(p, "\"aec\":%u,", s->status.aec);
  p += sprintf(p, "\"aec2\":%u,", s->status.aec2);
  p += sprintf(p, "\"ae_level\":%d,", s->status.ae_level);
  p += sprintf(p, "\"aec_value\":%u,", s->status.aec_value);
  p += sprintf(p, "\"agc\":%u,", s->status.agc);
  p += sprintf(p, "\"agc_gain\":%u,", s->status.agc_gain);
  p += sprintf(p, "\"gainceiling\":%u,", s->status.gainceiling);
  p += sprintf(p, "\"bpc\":%u,", s->status.bpc);
  p += sprintf(p, "\"wpc\":%u,", s->status.wpc);
  p += sprintf(p, "\"raw_gma\":%u,", s->status.raw_gma);
  p += sprintf(p, "\"lenc\":%u,", s->status.lenc);
  p += sprintf(p, "\"hmirror\":%u,", s->status.hmirror);
  p += sprintf(p, "\"vflip\":%u,", s->status.vflip);
  p += sprintf(p, "\"dcw\":%u,", s->status.dcw);
  p += sprintf(p, "\"colorbar\":%u", s->status.colorbar);
#if defined(LED_GPIO_NUM)
  p += sprintf(p, ",\"led_intensity\":%u", led_duty);
#else
  p += sprintf(p, ",\"led_intensity\":%d", -1);
#endif
  *p++ = '}';
  *p++ = 0;
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json_response, strlen(json_response));
}

static esp_err_t uart_handler(httpd_req_t *req) {
  const unsigned long now = millis();
  const unsigned long age = (helper_last_ms == 0) ? 0 : (now - helper_last_ms);
  const char *status = (helper_last_ms != 0 && age < 5000) ? "OK" : "STALE";
  String json = "{\"status\":\"" + String(status) + "\"}";
  httpd_resp_set_type(req, "application/json");
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, json.c_str(), json.length());
}

static esp_err_t orient_handler(httpd_req_t *req) {
  char *buf = NULL;
  size_t buf_len = 0;

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (!buf) {
      return httpd_resp_send_500(req);
    }
    if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
      char param[16];
      if (httpd_query_key_value(buf, "o", param, sizeof(param)) == ESP_OK) {
        sensor_t *s = esp_camera_sensor_get();
        if (s) {
          if (!strcmp(param, "normal")) {
            s->set_hmirror(s, 0);
            s->set_vflip(s, 0);
          } else if (!strcmp(param, "mirror")) {
            s->set_hmirror(s, 1);
            s->set_vflip(s, 0);
          } else if (!strcmp(param, "flip")) {
            s->set_hmirror(s, 0);
            s->set_vflip(s, 1);
          } else if (!strcmp(param, "mirrorflip")) {
            s->set_hmirror(s, 1);
            s->set_vflip(s, 1);
          }
        }
      }
    }
    free(buf);
  }
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

// Drive routing: commands are sent to the UNO helper, which drives both the
// L298N and the D51157 serial output.
static esp_err_t drive_handler(httpd_req_t *req) {
  char *buf = NULL;
  size_t buf_len = 0;
  char param[16] = {0};
  const char *cmd = "STOP";

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (buf) {
        if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
          if (httpd_query_key_value(buf, "c", param, sizeof(param)) == ESP_OK) {
            cmd = param;
          }
        }
      free(buf);
    }
  }

  // Remap drive commands to correct orientation
  const char *mapped = cmd;
  if (strcmp(cmd, "FWD") == 0) mapped = "RIGHT";
  else if (strcmp(cmd, "RIGHT") == 0) mapped = "FWD";
  else if (strcmp(cmd, "REV") == 0) mapped = "LEFT";
  else if (strcmp(cmd, "LEFT") == 0) mapped = "REV";

  Serial.printf("Drive cmd: %s -> %s\n", cmd, mapped);
  if (!drive_enabled) {
    sendHelperCmd("DRIVE:STOP");
  } else {
    sendHelperCmd(String("DRIVE:") + mapped);
  }
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

static esp_err_t speed_handler(httpd_req_t *req) {
  char *buf = NULL;
  size_t buf_len = 0;
  const char *val = "0";

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (buf) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(buf, "val", param, sizeof(param)) == ESP_OK) {
          val = param;
        }
      }
      free(buf);
    }
  }
  int v = atoi(val);
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  speed_limit = (uint8_t)v;
  Serial.printf("Speed limit: %u\n", speed_limit);
  sendHelperCmd(String("SPEED:") + speed_limit);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

static esp_err_t led_handler(httpd_req_t *req) {
  char *buf = NULL;
  size_t buf_len = 0;
  const char *val = "0";

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (buf) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(buf, "val", param, sizeof(param)) == ESP_OK) {
          val = param;
        }
      }
      free(buf);
    }
  }
#if defined(LED_GPIO_NUM)
  int v = atoi(val);
  if (v < 0) v = 0;
  if (v > 255) v = 255;
  led_duty = v;
  if (led_duty == 0) {
    enable_led(false);
  } else {
    enable_led(true);
  }
  Serial.printf("LED level: %d\n", led_duty);
#else
  Serial.printf("LED level (ignored, no LED GPIO): %s\n", val);
#endif
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

static esp_err_t mode_handler(httpd_req_t *req) {
  char *buf = NULL;
  size_t buf_len = 0;
  const char *mode = "manual";

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (buf) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        char param[16];
        if (httpd_query_key_value(buf, "m", param, sizeof(param)) == ESP_OK) {
          mode = param;
        }
      }
      free(buf);
    }
  }
  Serial.printf("Mode: %s\n", mode);
  sendHelperCmd(String("MODE:") + mode);
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

static esp_err_t return_home_handler(httpd_req_t *req) {
  drive_enabled = true;
  sendHelperCmd("ENABLE:1");
  sendHelperCmd("RETURNHOME");
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

static esp_err_t output_enable_handler(httpd_req_t *req) {
  char *buf = NULL;
  size_t buf_len = 0;
  const char *val = "0";

  buf_len = httpd_req_get_url_query_len(req) + 1;
  if (buf_len > 1) {
    buf = (char *)malloc(buf_len);
    if (buf) {
      if (httpd_req_get_url_query_str(req, buf, buf_len) == ESP_OK) {
        char param[8] = {0};
        if (httpd_query_key_value(buf, "e", param, sizeof(param)) == ESP_OK) {
          val = param;
        }
      }
      free(buf);
    }
  }

  drive_enabled = (atoi(val) != 0);
  Serial.printf("Drive output enable: %s\n", drive_enabled ? "ON" : "OFF");
  sendHelperCmd(String("ENABLE:") + (drive_enabled ? 1 : 0));
  httpd_resp_set_type(req, "text/plain");
  return httpd_resp_send(req, "OK", 2);
}

static esp_err_t xclk_handler(httpd_req_t *req) {
  char *buf = NULL;
  char _xclk[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "xclk", _xclk, sizeof(_xclk)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int xclk = atoi(_xclk);
  log_i("Set XCLK: %d MHz", xclk);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_xclk(s, LEDC_TIMER_0, xclk);
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t reg_handler(httpd_req_t *req) {
  char *buf = NULL;
  char _reg[32];
  char _mask[32];
  char _val[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK
      || httpd_query_key_value(buf, "val", _val, sizeof(_val)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  int val = atoi(_val);
  log_i("Set Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, val);

  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_reg(s, reg, mask, val);
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t greg_handler(httpd_req_t *req) {
  char *buf = NULL;
  char _reg[32];
  char _mask[32];

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }
  if (httpd_query_key_value(buf, "reg", _reg, sizeof(_reg)) != ESP_OK || httpd_query_key_value(buf, "mask", _mask, sizeof(_mask)) != ESP_OK) {
    free(buf);
    httpd_resp_send_404(req);
    return ESP_FAIL;
  }
  free(buf);

  int reg = atoi(_reg);
  int mask = atoi(_mask);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->get_reg(s, reg, mask);
  if (res < 0) {
    return httpd_resp_send_500(req);
  }
  log_i("Get Register: reg: 0x%02x, mask: 0x%02x, value: 0x%02x", reg, mask, res);

  char buffer[20];
  const char *val = itoa(res, buffer, 10);
  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, val, strlen(val));
}

static int parse_get_var(char *buf, const char *key, int def) {
  char _int[16];
  if (httpd_query_key_value(buf, key, _int, sizeof(_int)) != ESP_OK) {
    return def;
  }
  return atoi(_int);
}

static esp_err_t pll_handler(httpd_req_t *req) {
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }

  int bypass = parse_get_var(buf, "bypass", 0);
  int mul = parse_get_var(buf, "mul", 0);
  int sys = parse_get_var(buf, "sys", 0);
  int root = parse_get_var(buf, "root", 0);
  int pre = parse_get_var(buf, "pre", 0);
  int seld5 = parse_get_var(buf, "seld5", 0);
  int pclken = parse_get_var(buf, "pclken", 0);
  int pclk = parse_get_var(buf, "pclk", 0);
  free(buf);

  log_i("Set Pll: bypass: %d, mul: %d, sys: %d, root: %d, pre: %d, seld5: %d, pclken: %d, pclk: %d", bypass, mul, sys, root, pre, seld5, pclken, pclk);
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_pll(s, bypass, mul, sys, root, pre, seld5, pclken, pclk);
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t win_handler(httpd_req_t *req) {
  char *buf = NULL;

  if (parse_get(req, &buf) != ESP_OK) {
    return ESP_FAIL;
  }

  int startX = parse_get_var(buf, "sx", 0);
  int startY = parse_get_var(buf, "sy", 0);
  int endX = parse_get_var(buf, "ex", 0);
  int endY = parse_get_var(buf, "ey", 0);
  int offsetX = parse_get_var(buf, "offx", 0);
  int offsetY = parse_get_var(buf, "offy", 0);
  int totalX = parse_get_var(buf, "tx", 0);
  int totalY = parse_get_var(buf, "ty", 0);  // codespell:ignore totaly
  int outputX = parse_get_var(buf, "ox", 0);
  int outputY = parse_get_var(buf, "oy", 0);
  bool scale = parse_get_var(buf, "scale", 0) == 1;
  bool binning = parse_get_var(buf, "binning", 0) == 1;
  free(buf);

  log_i(
    "Set Window: Start: %d %d, End: %d %d, Offset: %d %d, Total: %d %d, Output: %d %d, Scale: %u, Binning: %u", startX, startY, endX, endY, offsetX, offsetY,
    totalX, totalY, outputX, outputY, scale, binning  // codespell:ignore totaly
  );
  sensor_t *s = esp_camera_sensor_get();
  int res = s->set_res_raw(s, startX, startY, endX, endY, offsetX, offsetY, totalX, totalY, outputX, outputY, scale, binning);  // codespell:ignore totaly
  if (res) {
    return httpd_resp_send_500(req);
  }

  httpd_resp_set_hdr(req, "Access-Control-Allow-Origin", "*");
  return httpd_resp_send(req, NULL, 0);
}

static esp_err_t index_handler(httpd_req_t *req) {
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, INDEX_HTML, HTTPD_RESP_USE_STRLEN);
}

void startCameraServer() {
  httpd_config_t config = HTTPD_DEFAULT_CONFIG();
  config.max_uri_handlers = 24;

  httpd_uri_t index_uri = {
    .uri = "/",
    .method = HTTP_GET,
    .handler = index_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t status_uri = {
    .uri = "/status",
    .method = HTTP_GET,
    .handler = status_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t psram_uri = {
    .uri = "/psram",
    .method = HTTP_GET,
    .handler = psram_handler,
    .user_ctx = NULL
  };

  httpd_uri_t wifi_uri = {
    .uri = "/wifistatus",
    .method = HTTP_GET,
    .handler = wifi_status_handler,
    .user_ctx = NULL
  };

  httpd_uri_t battery_uri = {
    .uri = "/battery",
    .method = HTTP_GET,
    .handler = battery_handler,
    .user_ctx = NULL
  };

  httpd_uri_t uart_uri = {
    .uri = "/uart",
    .method = HTTP_GET,
    .handler = uart_handler,
    .user_ctx = NULL
  };

  httpd_uri_t sensors_uri = {
    .uri = "/sensors",
    .method = HTTP_GET,
    .handler = sensors_handler,
    .user_ctx = NULL
  };

  httpd_uri_t orient_uri = {
    .uri = "/orient",
    .method = HTTP_GET,
    .handler = orient_handler,
    .user_ctx = NULL
  };

  httpd_uri_t drive_uri = {
    .uri = "/drive",
    .method = HTTP_GET,
    .handler = drive_handler,
    .user_ctx = NULL
  };

  httpd_uri_t speed_uri = {
    .uri = "/speed",
    .method = HTTP_GET,
    .handler = speed_handler,
    .user_ctx = NULL
  };

  httpd_uri_t led_uri = {
    .uri = "/led",
    .method = HTTP_GET,
    .handler = led_handler,
    .user_ctx = NULL
  };

  httpd_uri_t mode_uri = {
    .uri = "/mode",
    .method = HTTP_GET,
    .handler = mode_handler,
    .user_ctx = NULL
  };
  httpd_uri_t output_enable_uri = {
    .uri = "/outputenable",
    .method = HTTP_GET,
    .handler = output_enable_handler,
    .user_ctx = NULL
  };
  httpd_uri_t return_home_uri = {
    .uri = "/returnhome",
    .method = HTTP_GET,
    .handler = return_home_handler,
    .user_ctx = NULL
  };
  httpd_uri_t cmd_uri = {
    .uri = "/control",
    .method = HTTP_GET,
    .handler = cmd_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t capture_uri = {
    .uri = "/capture",
    .method = HTTP_GET,
    .handler = capture_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t stream_uri = {
    .uri = "/stream",
    .method = HTTP_GET,
    .handler = stream_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t bmp_uri = {
    .uri = "/bmp",
    .method = HTTP_GET,
    .handler = bmp_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t xclk_uri = {
    .uri = "/xclk",
    .method = HTTP_GET,
    .handler = xclk_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t reg_uri = {
    .uri = "/reg",
    .method = HTTP_GET,
    .handler = reg_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t greg_uri = {
    .uri = "/greg",
    .method = HTTP_GET,
    .handler = greg_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t pll_uri = {
    .uri = "/pll",
    .method = HTTP_GET,
    .handler = pll_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  httpd_uri_t win_uri = {
    .uri = "/resolution",
    .method = HTTP_GET,
    .handler = win_handler,
    .user_ctx = NULL
#ifdef CONFIG_HTTPD_WS_SUPPORT
    ,
    .is_websocket = true,
    .handle_ws_control_frames = false,
    .supported_subprotocol = NULL
#endif
  };

  ra_filter_init(&ra_filter, 20);

  log_i("Starting web server on port: '%d'", config.server_port);
  if (httpd_start(&camera_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(camera_httpd, &index_uri);
    httpd_register_uri_handler(camera_httpd, &cmd_uri);
    httpd_register_uri_handler(camera_httpd, &status_uri);
    httpd_register_uri_handler(camera_httpd, &psram_uri);
    httpd_register_uri_handler(camera_httpd, &wifi_uri);
    httpd_register_uri_handler(camera_httpd, &battery_uri);
    httpd_register_uri_handler(camera_httpd, &uart_uri);
    httpd_register_uri_handler(camera_httpd, &sensors_uri);
    httpd_register_uri_handler(camera_httpd, &orient_uri);
    httpd_register_uri_handler(camera_httpd, &drive_uri);
    httpd_register_uri_handler(camera_httpd, &speed_uri);
    httpd_register_uri_handler(camera_httpd, &led_uri);
    httpd_register_uri_handler(camera_httpd, &mode_uri);
    httpd_register_uri_handler(camera_httpd, &output_enable_uri);
    httpd_register_uri_handler(camera_httpd, &return_home_uri);
    httpd_register_uri_handler(camera_httpd, &capture_uri);
    httpd_register_uri_handler(camera_httpd, &bmp_uri);

    httpd_register_uri_handler(camera_httpd, &xclk_uri);
    httpd_register_uri_handler(camera_httpd, &reg_uri);
    httpd_register_uri_handler(camera_httpd, &greg_uri);
    httpd_register_uri_handler(camera_httpd, &pll_uri);
    httpd_register_uri_handler(camera_httpd, &win_uri);
  }

  config.server_port += 1;
  config.ctrl_port += 1;
  log_i("Starting stream server on port: '%d'", config.server_port);
  if (httpd_start(&stream_httpd, &config) == ESP_OK) {
    httpd_register_uri_handler(stream_httpd, &stream_uri);
  }
}

void setupLedFlash() {
#if defined(LED_GPIO_NUM)
  ledcAttach(LED_GPIO_NUM, 5000, 8);
#else
  log_i("LED flash is disabled -> LED_GPIO_NUM undefined");
#endif
}
