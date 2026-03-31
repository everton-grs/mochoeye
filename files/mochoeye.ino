#include "esp_camera.h"
#include <WiFi.h>
#include <WebServer.h>
#include <SD_MMC.h>
#include <FS.h>
#include <time.h>

#include "board_config.h"

const char* ssid = "rede wifi";
const char* password = "senha_redewifi";

WebServer server(80);

bool sdReady = false;
String lastCapturePath = "";

String urlDecode(const String &input) {
  String s = input;
  s.replace("%20", " ");
  s.replace("%23", "#");
  s.replace("%2F", "/");
  s.replace("%5B", "[");
  s.replace("%5D", "]");
  s.replace("%28", "(");
  s.replace("%29", ")");
  return s;
}

String contentType(const String &path) {
  if (path.endsWith(".jpg") || path.endsWith(".jpeg")) return "image/jpeg";
  if (path.endsWith(".png")) return "image/png";
  if (path.endsWith(".gif")) return "image/gif";
  if (path.endsWith(".bmp")) return "image/bmp";
  if (path.endsWith(".html")) return "text/html";
  if (path.endsWith(".css")) return "text/css";
  if (path.endsWith(".js")) return "application/javascript";
  if (path.endsWith(".json")) return "application/json";
  if (path.endsWith(".txt")) return "text/plain";
  return "application/octet-stream";
}

String makeTimestampName() {
  struct tm timeinfo;
  if (getLocalTime(&timeinfo, 2000)) {
    char buf[40];
    strftime(buf, sizeof(buf), "/photos/%Y%m%d/%H%M%S.jpg", &timeinfo);
    return String(buf);
  }

  uint32_t ms = millis();
  char buf[40];
  snprintf(buf, sizeof(buf), "/photos/boot/%lu.jpg", (unsigned long)ms);
  return String(buf);
}

bool ensureDir(const String &path) {
  if (path.isEmpty() || path == "/") return true;

  int start = 1;
  while (start < path.length()) {
    int slash = path.indexOf('/', start);
    String part = (slash == -1) ? path : path.substring(0, slash);
    if (part.length() > 0 && !SD_MMC.exists(part)) {
      if (!SD_MMC.mkdir(part)) return false;
    }
    if (slash == -1) break;
    start = slash + 1;
  }
  return true;
}

bool savePhotoToSD(String &savedPath, String &message) {
  if (!sdReady) {
    message = "Cartao microSD nao inicializado";
    return false;
  }

  camera_fb_t *fb = esp_camera_fb_get();
  if (!fb) {
    message = "Falha ao capturar imagem da camera";
    return false;
  }

  if (fb->format != PIXFORMAT_JPEG) {
    esp_camera_fb_return(fb);
    message = "Frame nao veio em JPEG";
    return false;
  }

  savedPath = makeTimestampName();
  int lastSlash = savedPath.lastIndexOf('/');
  String folder = savedPath.substring(0, lastSlash);

  if (!ensureDir(folder)) {
    esp_camera_fb_return(fb);
    message = "Nao foi possivel criar diretorios no microSD";
    return false;
  }

  File file = SD_MMC.open(savedPath, FILE_WRITE);
  if (!file) {
    esp_camera_fb_return(fb);
    message = "Nao foi possivel abrir arquivo no microSD";
    return false;
  }

  size_t fbLen = fb->len;
  size_t written = file.write(fb->buf, fbLen);
  file.close();
  esp_camera_fb_return(fb);

  if (written != fbLen) {
    message = "Escrita incompleta no microSD";
    return false;
  }

  lastCapturePath = savedPath;
  message = "Foto salva com sucesso";
  return true;
}

String jsonEscape(String s) {
  s.replace("\\", "\\\\");
  s.replace("\"", "\\\"");
  s.replace("\n", "\\n");
  s.replace("\r", "");
  return s;
}

void appendDirJson(fs::FS &fs, const String &dirname, String &json) {
  File root = fs.open(dirname);
  if (!root || !root.isDirectory()) {
    json = "{\"ok\":false,\"error\":\"Diretorio invalido\"}";
    return;
  }

  json = "{\"ok\":true,\"dir\":\"" + jsonEscape(dirname) + "\",\"items\":[";

  bool first = true;
  File file = root.openNextFile();

  while (file) {
    if (!first) json += ",";

    String fullPath = String(file.path());
    String name = fullPath;
    int lastSlash = name.lastIndexOf('/');
    if (lastSlash >= 0) {
      name = name.substring(lastSlash + 1);
    }

    bool isDir = file.isDirectory();

    json += "{";
    json += "\"name\":\"" + jsonEscape(name) + "\",";
    json += "\"path\":\"" + jsonEscape(fullPath) + "\",";
    json += "\"type\":\"" + String(isDir ? "dir" : "file") + "\",";
    json += "\"size\":" + String((unsigned long)file.size());
    json += "}";

    first = false;
    file = root.openNextFile();
  }

  json += "]}";
}

void handleRoot() {
  String html = R"rawliteral(
<!DOCTYPE html>
<html lang="pt-br">
<head>
  <meta charset="UTF-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>ESP32 CAM Galeria SD</title>
  <style>
    body { font-family: Arial, sans-serif; margin: 0; background: #f4f4f4; color: #222; }
    header { background: #1f2937; color: white; padding: 16px 20px; }
    main { padding: 20px; max-width: 1200px; margin: auto; }
    .card { background: white; border-radius: 12px; padding: 16px; margin-bottom: 16px; box-shadow: 0 2px 8px rgba(0,0,0,.08); }
    button { padding: 10px 14px; border: 0; border-radius: 8px; cursor: pointer; background: #2563eb; color: white; margin-right: 6px; }
    button:hover { background: #1d4ed8; }
    .btn-delete { background: #dc2626; }
    .btn-delete:hover { background: #b91c1c; }
    .muted { color: #666; font-size: 14px; }
    #status { margin-top: 10px; font-weight: bold; }
    #preview { max-width: 100%; border-radius: 10px; margin-top: 12px; }
    ul { list-style: none; padding-left: 0; }
    li { padding: 10px 0; border-bottom: 1px solid #eee; }
    a { color: #2563eb; text-decoration: none; }
    a:hover { text-decoration: underline; }
    .pathbar { font-family: monospace; background: #f3f4f6; padding: 8px 10px; border-radius: 8px; margin-bottom: 10px; word-break: break-all; }
    .row { display: flex; align-items: center; justify-content: space-between; gap: 10px; flex-wrap: wrap; }
    .left { display: flex; align-items: center; gap: 8px; min-width: 0; }
  </style>
</head>
<body>
  <header>
    <h2>ESP32 CAM com captura para microSD</h2>
    <div class="muted">Captura sob demanda e navegador de arquivos do cartão</div>
  </header>
  <main>
    <div class="card">
      <button onclick="capturePhoto()">Tirar foto e salvar no cartão</button>
      <div id="status">Pronto.</div>
      <img id="preview" style="display:none;" />
    </div>

    <div class="card">
      <h3>Arquivos do microSD</h3>
      <div class="pathbar" id="currentPath">/</div>
      <div style="margin-bottom:10px;">
        <button onclick="goUp()">Subir diretório</button>
        <button onclick="loadDir('/')">Raiz</button>
      </div>
      <ul id="fileList"></ul>
    </div>
  </main>

<script>
let currentDir = '/';

async function capturePhoto() {
  const status = document.getElementById('status');
  const preview = document.getElementById('preview');
  status.textContent = 'Capturando e salvando...';
  preview.style.display = 'none';

  try {
    const res = await fetch('/capture-save');
    const data = await res.json();

    if (data.ok) {
      status.textContent = data.message + ' -> ' + data.path;
      preview.src = '/view?path=' + encodeURIComponent(data.path) + '&t=' + Date.now();
      preview.style.display = 'block';
      loadDir(currentDir);
    } else {
      status.textContent = 'Erro: ' + data.message;
    }
  } catch (e) {
    status.textContent = 'Erro de comunicacao com o servidor';
  }
}

async function deleteFile(path) {
  const status = document.getElementById('status');

  if (!confirm('Deseja realmente excluir este arquivo?\n' + path)) {
    return;
  }

  status.textContent = 'Excluindo arquivo...';

  try {
    const res = await fetch('/delete?path=' + encodeURIComponent(path));
    const data = await res.json();

    if (data.ok) {
      status.textContent = data.message + ' -> ' + path;
      const preview = document.getElementById('preview');
      if (preview.src.includes(encodeURIComponent(path))) {
        preview.style.display = 'none';
        preview.src = '';
      }
      loadDir(currentDir);
    } else {
      status.textContent = 'Erro: ' + data.message;
    }
  } catch (e) {
    status.textContent = 'Erro de comunicacao com o servidor';
  }
}

async function loadDir(dir) {
  currentDir = dir;
  document.getElementById('currentPath').textContent = dir;
  const list = document.getElementById('fileList');
  list.innerHTML = '<li>Carregando...</li>';

  try {
    const res = await fetch('/list?dir=' + encodeURIComponent(dir));
    const data = await res.json();

    if (!data.ok) {
      list.innerHTML = '<li>Erro ao listar diretório</li>';
      return;
    }

    if (!data.items.length) {
      list.innerHTML = '<li>Diretório vazio</li>';
      return;
    }

    list.innerHTML = '';

    data.items.forEach(item => {
      const li = document.createElement('li');

      if (item.type === 'dir') {
        const safePath = item.path.replace(/\\/g, '\\\\').replace(/'/g, "\\'");
        li.innerHTML =
          '<div class="row">' +
            '<div class="left">📁 <a href="#" onclick="loadDir(\'' + safePath + '\'); return false;">' + item.name + '</a></div>' +
          '</div>';
      } else if (item.name.match(/\.(jpg|jpeg|png|gif|bmp)$/i)) {
        li.innerHTML =
          '<div class="row">' +
            '<div class="left">🖼️ <a href="/view?path=' + encodeURIComponent(item.path) + '" target="_blank">' + item.name + '</a> <span class="muted">(' + item.size + ' bytes)</span></div>' +
            '<div><button class="btn-delete" onclick="deleteFile(\'' + item.path.replace(/\\/g, '\\\\').replace(/'/g, "\\'") + '\')">Excluir</button></div>' +
          '</div>';
      } else {
        li.innerHTML =
          '<div class="row">' +
            '<div class="left">📄 <a href="/view?path=' + encodeURIComponent(item.path) + '" target="_blank">' + item.name + '</a> <span class="muted">(' + item.size + ' bytes)</span></div>' +
            '<div><button class="btn-delete" onclick="deleteFile(\'' + item.path.replace(/\\/g, '\\\\').replace(/'/g, "\\'") + '\')">Excluir</button></div>' +
          '</div>';
      }

      list.appendChild(li);
    });
  } catch (e) {
    list.innerHTML = '<li>Erro de comunicação com o servidor</li>';
  }
}

function goUp() {
  if (currentDir === '/' || currentDir === '') return;

  const clean = currentDir.endsWith('/') && currentDir.length > 1
    ? currentDir.slice(0, -1)
    : currentDir;

  const idx = clean.lastIndexOf('/');

  if (idx <= 0) {
    loadDir('/');
  } else {
    loadDir(clean.substring(0, idx));
  }
}

loadDir('/');
</script>
</body>
</html>
)rawliteral";

  server.send(200, "text/html", html);
}

void handleCaptureSave() {
  String path, message;
  bool ok = savePhotoToSD(path, message);

  String json = "{";
  json += String("\"ok\":") + (ok ? "true" : "false") + ",";
  json += "\"message\":\"" + jsonEscape(message) + "\",";
  json += "\"path\":\"" + jsonEscape(path) + "\"";
  json += "}";

  server.send(ok ? 200 : 500, "application/json", json);
}

void handleList() {
  if (!sdReady) {
    server.send(500, "application/json", "{\"ok\":false,\"error\":\"microSD indisponivel\"}");
    return;
  }

  String dir = server.hasArg("dir") ? urlDecode(server.arg("dir")) : "/";
  if (!dir.startsWith("/")) dir = "/" + dir;

  String json;
  appendDirJson(SD_MMC, dir, json);

  Serial.print("LIST JSON: ");
  Serial.println(json);

  server.send(200, "application/json", json);
}

void handleDelete() {
  if (!sdReady) {
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"microSD indisponivel\"}");
    return;
  }

  if (!server.hasArg("path")) {
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Parametro path ausente\"}");
    return;
  }

  String path = urlDecode(server.arg("path"));
  if (!path.startsWith("/")) path = "/" + path;

  File file = SD_MMC.open(path);
  if (!file) {
    server.send(404, "application/json", "{\"ok\":false,\"message\":\"Arquivo nao encontrado\"}");
    return;
  }

  if (file.isDirectory()) {
    file.close();
    server.send(400, "application/json", "{\"ok\":false,\"message\":\"Exclusao de diretorio nao permitida\"}");
    return;
  }

  file.close();

  bool removed = SD_MMC.remove(path);
  if (!removed) {
    server.send(500, "application/json", "{\"ok\":false,\"message\":\"Falha ao excluir arquivo\"}");
    return;
  }

  if (lastCapturePath == path) {
    lastCapturePath = "";
  }

  String json = "{";
  json += "\"ok\":true,";
  json += "\"message\":\"Arquivo excluido com sucesso\"";
  json += "}";

  server.send(200, "application/json", json);
}

void handleView() {
  if (!sdReady) {
    server.send(500, "text/plain", "microSD indisponivel");
    return;
  }

  if (!server.hasArg("path")) {
    server.send(400, "text/plain", "Parametro path ausente");
    return;
  }

  String path = urlDecode(server.arg("path"));
  if (!path.startsWith("/")) path = "/" + path;

  File file = SD_MMC.open(path);
  if (!file || file.isDirectory()) {
    server.send(404, "text/plain", "Arquivo nao encontrado");
    return;
  }

  server.streamFile(file, contentType(path));
  file.close();
}

void handleStatus() {
  String json = "{";
  json += String("\"wifi\":\"") + WiFi.localIP().toString() + "\",";
  json += String("\"sd\":") + (sdReady ? "true" : "false") + ",";
  json += "\"lastCapture\":\"" + jsonEscape(lastCapturePath) + "\"";
  json += "}";

  server.send(200, "application/json", json);
}

bool initCamera() {
  camera_config_t config;
  config.ledc_channel = LEDC_CHANNEL_0;
  config.ledc_timer = LEDC_TIMER_0;
  config.pin_d0 = Y2_GPIO_NUM;
  config.pin_d1 = Y3_GPIO_NUM;
  config.pin_d2 = Y4_GPIO_NUM;
  config.pin_d3 = Y5_GPIO_NUM;
  config.pin_d4 = Y6_GPIO_NUM;
  config.pin_d5 = Y7_GPIO_NUM;
  config.pin_d6 = Y8_GPIO_NUM;
  config.pin_d7 = Y9_GPIO_NUM;
  config.pin_xclk = XCLK_GPIO_NUM;
  config.pin_pclk = PCLK_GPIO_NUM;
  config.pin_vsync = VSYNC_GPIO_NUM;
  config.pin_href = HREF_GPIO_NUM;
  config.pin_sccb_sda = SIOD_GPIO_NUM;
  config.pin_sccb_scl = SIOC_GPIO_NUM;
  config.pin_pwdn = PWDN_GPIO_NUM;
  config.pin_reset = RESET_GPIO_NUM;
  config.xclk_freq_hz = 20000000;
  config.pixel_format = PIXFORMAT_JPEG;
  config.frame_size = psramFound() ? FRAMESIZE_SVGA : FRAMESIZE_VGA;
  config.jpeg_quality = psramFound() ? 10 : 12;
  config.fb_count = psramFound() ? 2 : 1;
  config.grab_mode = CAMERA_GRAB_LATEST;
  config.fb_location = CAMERA_FB_IN_PSRAM;

  esp_err_t err = esp_camera_init(&config);
  if (err != ESP_OK) {
    Serial.printf("Falha ao iniciar camera: 0x%x\n", err);
    return false;
  }

  sensor_t *s = esp_camera_sensor_get();
  if (s) {
    s->set_framesize(s, FRAMESIZE_VGA);
    s->set_brightness(s, 0);
    s->set_saturation(s, 0);
  }

  return true;
}

bool initSDCard() {
  if (!SD_MMC.begin("/sdcard", true)) {
    Serial.println("Falha ao montar o microSD em modo 1-bit");
    return false;
  }

  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("Nenhum cartao microSD detectado");
    return false;
  }

  ensureDir("/photos");
  return true;
}

void setup() {
  Serial.begin(115200);
  Serial.setDebugOutput(true);
  Serial.println();

  if (!initCamera()) {
    return;
  }

  WiFi.begin(ssid, password);
  WiFi.setSleep(false);
  Serial.print("Conectando ao Wi-Fi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println();
  Serial.print("IP: ");
  Serial.println(WiFi.localIP());

  configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov");

  sdReady = initSDCard();
  Serial.println(sdReady ? "microSD pronto" : "microSD indisponivel");

  server.on("/", HTTP_GET, handleRoot);
  server.on("/capture-save", HTTP_GET, handleCaptureSave);
  server.on("/list", HTTP_GET, handleList);
  server.on("/delete", HTTP_GET, handleDelete);
  server.on("/view", HTTP_GET, handleView);
  server.on("/status", HTTP_GET, handleStatus);

  server.begin();
  Serial.println("Servidor iniciado");
}

void loop() {
  server.handleClient();
}
