/**
 * WiFiManager — Implémentation
 *
 * Bibliothèques utilisées :
 *   WiFiMulti    — gestion de plusieurs réseaux, connexion au meilleur signal
 *   ESPmDNS      — résolution de noms sur le réseau local (gateway-lab-v1.local)
 *   Preferences  — persistance NVS des réseaux enregistrés
 *   DNSServer +
 *   WebServer    — portail captif de configuration (mode point d'accès)
 */

#include "wifi_manager.h"
#include <algorithm>
#include <WiFi.h>
#include <WiFiMulti.h>
#include <ESPmDNS.h>
#include <Preferences.h>
#include <DNSServer.h>
#include <WebServer.h>
#include <ArduinoJson.h>
#include "app_config.h"    // WIFI_CONNECT_TIMEOUT, MDNS_HOSTNAME, ENABLE_MDNS
#include "../utils/logger.h"

// secrets.h est optionnel : seul DEFAULT_WIFI_SSID/PASSWORD y est défini,
// utilisé uniquement en développement quand aucun réseau n'est en NVS.
#if __has_include("secrets.h")
#include "secrets.h"
#endif

static const char* TAG            = "WiFi";
static const char* NVS_NAMESPACE  = "wifi";
static const char* NVS_KEY        = "networks";
static const char* AP_SSID        = "GatewayLab-Setup";
static const IPAddress AP_IP(192, 168, 4, 1);

// Instance interne de WiFiMulti — non exposée hors de ce fichier
static WiFiMulti _multi;

// Délai minimum entre deux tentatives de reconnexion automatique
static constexpr unsigned long RECONNECT_DEBOUNCE_MS = 30000;
static unsigned long _lastReconnectAttempt = 0;

// Callback conservé pour le rappeler lors d'une reconnexion automatique
static WiFiManager::Callback _storedCb;

// État précédent : permet de détecter la transition déconnecté → connecté
static bool _wasConnected = false;

// true tant que le portail de configuration (point d'accès) est actif
static bool _apMode = false;

static DNSServer _dns;
static WebServer _portal(80);

// Instance globale exportée
WiFiManager wifiMgr;

// ---------------------------------------------------------------------------
// Persistance NVS — liste des réseaux enregistrés, sérialisée en JSON
// ---------------------------------------------------------------------------
static std::vector<WifiCredential> _loadNetworks() {
    std::vector<WifiCredential> list;

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, true);   // lecture seule
    String json = prefs.getString(NVS_KEY, "[]");
    prefs.end();

    JsonDocument doc;
    if (deserializeJson(doc, json) == DeserializationError::Ok) {
        for (JsonObject o : doc.as<JsonArray>()) {
            WifiCredential c;
            c.ssid     = o["ssid"].as<String>();
            c.password = o["password"].as<String>();
            if (!c.ssid.isEmpty()) list.push_back(c);
        }
    }
    return list;
}

static bool _saveNetworks(const std::vector<WifiCredential>& list) {
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();
    for (const auto& c : list) {
        JsonObject o = arr.add<JsonObject>();
        o["ssid"]     = c.ssid;
        o["password"] = c.password;
    }
    String json;
    serializeJson(doc, json);

    Preferences prefs;
    prefs.begin(NVS_NAMESPACE, false);  // lecture/écriture
    size_t written = prefs.putString(NVS_KEY, json);
    prefs.end();
    return written == (size_t)json.length();
}

std::vector<WifiCredential> WiFiManager::savedNetworks() const {
    return _loadNetworks();
}

bool WiFiManager::addNetwork(const String& ssid, const String& password) {
    if (ssid.isEmpty()) return false;

    auto list = _loadNetworks();
    for (auto& c : list) {
        if (c.ssid == ssid) {
            c.password = password;   // réseau déjà connu : mise à jour du mot de passe
            return _saveNetworks(list);
        }
    }
    list.push_back({ssid, password});
    return _saveNetworks(list);
}

bool WiFiManager::removeNetwork(const String& ssid) {
    auto list = _loadNetworks();
    size_t before = list.size();
    list.erase(std::remove_if(list.begin(), list.end(),
        [&](const WifiCredential& c) { return c.ssid == ssid; }), list.end());
    if (list.size() == before) return false;   // SSID introuvable
    return _saveNetworks(list);
}

// ---------------------------------------------------------------------------
// Portail de configuration — page HTML servie en mode point d'accès
// Page volontairement autonome (CSS inline) : indépendante du pipeline
// minify_web.py, car servie avant toute connexion réseau.
// ---------------------------------------------------------------------------
static const char PORTAL_PAGE[] PROGMEM = R"HTML(
<!DOCTYPE html><html lang="fr"><head><meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Gateway Lab V1 - Configuration WiFi</title>
<style>
body{font-family:system-ui,sans-serif;background:#0f172a;color:#e2e8f0;display:flex;
  flex-direction:column;align-items:center;padding:2rem 1rem;margin:0}
.card{background:#1e293b;border-radius:12px;padding:1.5rem;max-width:380px;width:100%}
h1{font-size:1.05rem;margin:0 0 0.4rem}
p.hint{font-size:0.8rem;color:#94a3b8;margin-bottom:1rem}
label{display:block;margin:0.8rem 0 0.3rem;font-size:0.85rem;color:#94a3b8}
select,input{width:100%;padding:0.6rem;border-radius:6px;border:1px solid #334155;
  background:#0f172a;color:#e2e8f0;box-sizing:border-box;font-size:0.9rem}
button{width:100%;margin-top:1.2rem;padding:0.7rem;border:none;border-radius:6px;
  background:#3b82f6;color:#fff;font-weight:600;cursor:pointer;font-size:0.95rem}
button:hover{background:#2563eb}
#msg{margin-top:1rem;font-size:0.85rem;text-align:center;min-height:1.1rem}
</style></head><body>
<div class="card">
  <h1>Configuration WiFi - Gateway Lab V1</h1>
  <p class="hint">Choisissez votre réseau WiFi domestique. L'ESP32 redémarrera et s'y connectera automatiquement.</p>
  <form id="f">
    <label>Réseau WiFi détecté</label>
    <select id="ssidSel"><option value="">Recherche en cours...</option></select>
    <label>Ou saisissez le SSID manuellement</label>
    <input id="ssidManual" placeholder="Nom du réseau (SSID)">
    <label>Mot de passe</label>
    <input id="password" type="password" placeholder="Mot de passe WiFi">
    <button type="submit">Enregistrer et connecter</button>
  </form>
  <p id="msg"></p>
</div>
<script>
fetch('/scan').then(function(r){return r.json();}).then(function(list){
  var sel=document.getElementById('ssidSel');
  sel.innerHTML='<option value="">-- Choisir un réseau --</option>';
  list.forEach(function(n){
    var o=document.createElement('option');
    o.value=n.ssid; o.textContent=n.ssid+' ('+n.rssi+' dBm)';
    sel.appendChild(o);
  });
}).catch(function(){
  document.getElementById('ssidSel').innerHTML='<option value="">Aucun réseau détecté</option>';
});
document.getElementById('f').addEventListener('submit',function(e){
  e.preventDefault();
  var ssid=document.getElementById('ssidManual').value || document.getElementById('ssidSel').value;
  var password=document.getElementById('password').value;
  var msg=document.getElementById('msg');
  if(!ssid){ msg.textContent='Veuillez choisir ou saisir un réseau.'; return; }
  msg.textContent='Enregistrement...';
  var fd=new FormData(); fd.append('ssid',ssid); fd.append('password',password);
  fetch('/save',{method:'POST',body:fd}).then(function(r){return r.json();}).then(function(d){
    msg.textContent = d.status==='ok'
      ? 'Enregistré - redémarrage de l’ESP32...'
      : 'Erreur : '+(d.error||'inconnue');
  }).catch(function(){ msg.textContent='Erreur réseau.'; });
});
</script></body></html>
)HTML";

static void _portalHandleRoot() {
    _portal.send_P(200, "text/html", PORTAL_PAGE);
}

// Scan des réseaux visibles — utilisé par la page de configuration pour
// proposer une liste déroulante plutôt qu'une saisie manuelle uniquement
static void _portalHandleScan() {
    int n = WiFi.scanNetworks();
    String json = "[";
    for (int i = 0; i < n; i++) {
        if (i) json += ",";
        json += "{\"ssid\":\"" + WiFi.SSID(i) + "\",\"rssi\":" + String(WiFi.RSSI(i)) + "}";
    }
    json += "]";
    _portal.send(200, "application/json", json);
}

// Enregistrement des identifiants saisis puis redémarrage — au prochain
// boot, begin() trouvera le réseau en NVS (priorité 1) et s'y connectera
static void _portalHandleSave() {
    String ssid     = _portal.arg("ssid");
    String password = _portal.arg("password");
    if (ssid.isEmpty()) {
        _portal.send(400, "application/json", "{\"error\":\"ssid requis\"}");
        return;
    }
    wifiMgr.addNetwork(ssid, password);
    _portal.send(200, "application/json", "{\"status\":\"ok\"}");
    Log::i(TAG, "Réseau \"%s\" enregistré via le portail — redémarrage", ssid.c_str());
    delay(500);
    ESP.restart();
}

// Portail captif : toute requête vers un domaine inconnu est redirigée
// vers la page de configuration (déclenche le popup "Se connecter au réseau")
static void _portalHandleNotFound() {
    _portal.sendHeader("Location", String("http://") + AP_IP.toString(), true);
    _portal.send(302, "text/plain", "");
}

static void _startPortal() {
    _apMode = true;
    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID);
    delay(100);
    WiFi.softAPConfig(AP_IP, AP_IP, IPAddress(255, 255, 255, 0));

    _dns.start(53, "*", AP_IP);   // résout tous les domaines vers le portail

    _portal.on("/",     HTTP_GET,  _portalHandleRoot);
    _portal.on("/scan", HTTP_GET,  _portalHandleScan);
    _portal.on("/save", HTTP_POST, _portalHandleSave);
    _portal.onNotFound(_portalHandleNotFound);
    _portal.begin();

    Log::i(TAG, "Portail de configuration actif — SSID \"%s\" — http://%s",
           AP_SSID, AP_IP.toString().c_str());
}

// ---------------------------------------------------------------------------
// mDNS
// ---------------------------------------------------------------------------
static void _startMdns() {
#ifdef ENABLE_MDNS
    if (MDNS.begin(MDNS_HOSTNAME)) {
        Log::i(TAG, "mDNS actif : http://%s.local", MDNS_HOSTNAME);
    }
#endif
}

// ---------------------------------------------------------------------------
// Connexion STA — enregistre tous les réseaux fournis et attend jusqu'au
// timeout configuré ; WiFiMulti choisit celui au signal le plus fort
// ---------------------------------------------------------------------------
static bool _tryConnect(const std::vector<WifiCredential>& networks) {
    WiFi.mode(WIFI_STA);
    for (const auto& c : networks) {
        _multi.addAP(c.ssid.c_str(), c.password.c_str());
    }

    Log::i(TAG, "Connexion en cours...");
    unsigned long start = millis();
    while (_multi.run() != WL_CONNECTED &&
           millis() - start < WIFI_CONNECT_TIMEOUT) {
        delay(500);
        Serial.print('.');
    }
    Serial.println();
    return WiFi.isConnected();
}

void WiFiManager::begin(Callback cb) {
    _storedCb = cb;

    // Priorité 1 : réseaux enregistrés en NVS (configurés via le portail web)
    std::vector<WifiCredential> networks = _loadNetworks();

    // Priorité 2 : valeurs de développement (include/secrets.h), seulement
    // si aucun réseau n'a encore été configuré par l'utilisateur
#ifdef DEFAULT_WIFI_SSID
    if (networks.empty()) {
        networks.push_back({DEFAULT_WIFI_SSID, DEFAULT_WIFI_PASSWORD});
        Log::i(TAG, "Aucun réseau en NVS — utilisation de DEFAULT_WIFI_SSID (secrets.h)");
    }
#endif

    if (!networks.empty() && _tryConnect(networks)) {
        Log::i(TAG, "Connecté à \"%s\" — IP %s — RSSI %d dBm",
               WiFi.SSID().c_str(), WiFi.localIP().toString().c_str(), WiFi.RSSI());
        _startMdns();
        _wasConnected = true;
        if (cb) cb(true);
        return;
    }

    // Priorité 3 : aucun réseau disponible — portail de configuration
    Log::w(TAG, "Aucun réseau disponible — démarrage du portail de configuration");
    if (cb) cb(false);
    _startPortal();
}

void WiFiManager::loop() {
    if (_apMode) {
        _dns.processNextRequest();
        _portal.handleClient();
        return;
    }

    bool connected = WiFi.isConnected();

    if (!connected) {
        unsigned long now = millis();
        // Tentative de reconnexion seulement si le délai de debounce est écoulé
        if (now - _lastReconnectAttempt >= RECONNECT_DEBOUNCE_MS) {
            _lastReconnectAttempt = now;
            Log::w(TAG, "WiFi perdu — tentative de reconnexion...");
            _multi.run();   // Retourne immédiatement, la connexion est async
        }
    } else if (!_wasConnected) {
        // Transition déconnecté → connecté : relancer mDNS et tous les services
        Log::i(TAG, "WiFi rétabli sur \"%s\" — IP %s",
               WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());
        _startMdns();
        if (_storedCb) _storedCb(true);
    }

    _wasConnected = connected;
}

bool   WiFiManager::isConnected() const { return !_apMode && WiFi.isConnected(); }
bool   WiFiManager::isApMode()    const { return _apMode; }
String WiFiManager::ssid()        const { return WiFi.SSID(); }
String WiFiManager::localIP()     const {
    return _apMode ? WiFi.softAPIP().toString() : WiFi.localIP().toString();
}
int8_t WiFiManager::rssi()        const { return (int8_t)WiFi.RSSI(); }
String WiFiManager::hostname()    const { return MDNS_HOSTNAME; }
