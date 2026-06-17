var pollTimer   = null;
var progressVal = 0;
var progTimer   = null;

// Récupère la version au chargement de la page
fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) document.getElementById('site-ver').textContent = 'v' + d.version;
}).catch(function() {});

function fmtSeen(ms) {
  var s = Math.floor(ms / 1000);
  if (s < 5)    return 'maintenant';
  if (s < 60)   return s + 's';
  if (s < 3600) return Math.floor(s / 60) + ' min';
  return Math.floor(s / 3600) + 'h';
}

function categoryClass(cat) {
  if (!cat) return '';
  return 'type-' + cat.toLowerCase()
    .replace(/[^a-z]/g, '')
    .replace('homeautomation', 'homeauto')
    .replace('robotvacuum',    'robot')
    .replace('smarthub',       'smarthub');
}

function svcClass(s) {
  var m = {
    'HTTP':'web','HTTPS':'web',
    'SSH':'ssh','SFTP':'ssh',
    'SMB':'share','AFP':'share','NFS':'share','FTP':'share',
    'AirPlay':'apple','HomeKit':'apple','iTunes':'apple','AppleInf':'apple',
    'Cast':'google',
    'Sonos':'music','Spotify':'music',
    'Hue':'iot','ESPHome':'iot','HA':'iot','MQTT':'iot',
    'IPP':'print','Print':'print'
  };
  return m[s] || 'other';
}

function confClass(score) {
  if (score >= 90) return 'conf-high';
  if (score >= 60) return 'conf-medium';
  return 'conf-low';
}

function confBadge(d) {
  if (d.confidence === undefined || d.confidence === null) return '';
  var title = 'Niveau de confiance : ' + d.confidence + '% (source : ' + esc(d.confidenceLabel) + ')';
  return '<span class="conf-badge ' + confClass(d.confidence) + '" title="' + title + '">' + d.confidence + '%</span>';
}

function portClass(p) {
  if (p === 'SSH')    return 'port-ssh';
  if (p === 'FTP' || p === 'Telnet') return 'port-legacy';
  if (p === 'SMB')    return 'port-smb';
  if (p === 'RDP')    return 'port-rdp';
  if (p === 'RTSP')   return 'port-rtsp';
  if (p === 'MQTT')   return 'port-mqtt';
  if (p === 'IPP')    return 'port-print';
  if (p === 'HA')     return 'port-ha';
  if (p.indexOf('HTTP') >= 0 || p.indexOf('HTTPS') >= 0) return 'port-http';
  return 'port-other';
}

function sourceBadge(source) {
  if (!source || source === 'MAC') return '';
  var cfg = {
    'mDNS':         { cls: 'source-mdns',     label: 'mDNS',      title: 'Nom résolu via mDNS — annonce .local de l\'appareil' },
    'PTR':          { cls: 'source-ptr',       label: 'DNS↩',     title: 'DNS inverse (PTR) — nom fourni par le routeur / box' },
    'Self':         { cls: 'source-self',      label: 'ESP32',     title: 'Cet appareil — Gateway Lab V1' },
    'SSDP':         { cls: 'source-ssdp',      label: 'UPnP',      title: 'Découverte UPnP/SSDP — descripteur XML du device' },
    'HueAPI':       { cls: 'source-hue',       label: 'Hue',       title: 'API Philips Hue Bridge (/api/config)' },
    'SynologyAPI':  { cls: 'source-synology',  label: 'DSM',       title: 'API Synology DSM (non authentifiée)' },
    'FreeboxAPI':   { cls: 'source-freebox',   label: 'Freebox',   title: 'API Freebox (/api_version — non authentifiée)' },
    'NetBIOS':      { cls: 'source-netbios',   label: 'NetBIOS',   title: 'Nom resolu via NetBIOS Node Status (UDP 137) - PC Windows / Samba' }
  };
  var c = cfg[source];
  if (!c) return '';
  return '<span class="source-badge ' + c.cls + '" title="' + c.title + '">' + c.label + '</span>';
}

function esc(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function updateStats(stats) {
  if (!stats) return;
  var sb = document.getElementById('stats-bar');
  sb.style.display = 'flex';
  document.getElementById('stat-known').textContent   = stats.known   + ' connu'   + (stats.known   > 1 ? 's' : '');
  document.getElementById('stat-online').textContent  = stats.online  + ' en ligne';
  document.getElementById('stat-offline').textContent = stats.offline + ' hors ligne';
}

function renderDevices(devices) {
  var tbody = document.getElementById('devices-body');
  var meta  = document.getElementById('scan-meta');
  if (!devices || devices.length === 0) {
    tbody.innerHTML = '<tr><td colspan="7" class="empty-msg">Aucun équipement détecté</td></tr>';
    meta.textContent = '0 équipement';
    return;
  }
  var onlineCount = devices.filter(function(d) { return d.online; }).length;
  meta.textContent = onlineCount + ' équipement' + (onlineCount > 1 ? 's' : '') + ' en ligne';
  // Tri : online d'abord, puis par IP
  devices.sort(function(a, b) {
    if (a.online !== b.online) return a.online ? -1 : 1;
    var pa = (a.ip || '').split('.').map(Number);
    var pb = (b.ip || '').split('.').map(Number);
    for (var i = 0; i < 4; i++) { if (pa[i] !== pb[i]) return pa[i] - pb[i]; }
    return 0;
  });
  tbody.innerHTML = devices.map(function(d) {
    var statusHtml = d.online
      ? '<span class="status-online"  title="En ligne — vu lors du dernier scan">●</span>'
      : '<span class="status-offline" title="Hors ligne — non vu lors du dernier scan">○</span>';
    var displayName = d.alias || d.hostname;
    var aliasKey = d.mac || d.ip;
    var editBtn = '<button class="alias-edit" title="Renommer cet équipement" ' +
      'data-key="' + esc(aliasKey) + '" data-alias="' + esc(d.alias || '') + '" ' +
      'onclick="editAlias(this)">✎</button>';
    var nameHtml = displayName
      ? '<div class="name-cell">' + esc(displayName) +
          (d.alias ? '<span class="alias-tag" title="Alias personnalisé"> ★</span>' : sourceBadge(d.source)) +
          editBtn +
        '</div>'
      : '<span class="none">—</span>' + editBtn;
    var svcHtml = (d.services && d.services.length)
      ? '<div class="svc-list">' + d.services.map(function(s) {
          return '<span class="svc-badge svc-' + svcClass(s) + '" title="Service DNS-SD : ' + esc(s) + '">' + esc(s) + '</span>';
        }).join('') + '</div>'
      : '';
    var portsHtml = (d.openPorts && d.openPorts.length)
      ? '<div class="port-list">' + d.openPorts.map(function(p) {
          return '<span class="port-badge ' + portClass(p) + '" title="Port TCP ouvert : ' + esc(p) + '">' + esc(p) + '</span>';
        }).join('') + '</div>'
      : '';
    var mfrHtml = d.manufacturer
      ? '<span class="vendor-badge">' + esc(d.manufacturer) + '</span>' +
        (d.model ? '<div class="mfr-model">' + esc(d.model) + '</div>' : '') +
        (d.os    ? '<div class="mfr-os">'    + esc(d.os)    + '</div>' : '') +
        svcHtml + portsHtml
      : (svcHtml + portsHtml || '<span class="none">—</span>');
    var catHtml = d.category
      ? '<span class="type-badge ' + categoryClass(d.category) + '">' + esc(d.category) + '</span>' + confBadge(d)
      : '<span class="none">—</span>' + confBadge(d);
    var seenHtml = d.online
      ? fmtSeen(d.elapsedMs)
      : '<span class="none" title="Non vu lors du dernier scan">hors ligne</span>';
    if (d.seenCount > 0)
      seenHtml += '<div class="seen-count" title="Nombre de scans où cet équipement a été vu en ligne">vu ' + d.seenCount + 'x</div>';
    return '<tr' + (d.online ? '' : ' class="row-offline"') + '>' +
      '<td class="status-cell">' + statusHtml + '</td>' +
      '<td class="ip-cell">'     + esc(d.ip)  + '</td>' +
      '<td>'                     + nameHtml    + '</td>' +
      '<td>'                     + mfrHtml     + '</td>' +
      '<td>'                     + catHtml     + '</td>' +
      '<td class="mac-cell">'    + esc(d.mac)  + '</td>' +
      '<td class="seen-cell">'   + seenHtml    + '</td>' +
      '</tr>';
  }).join('');
}

function animateProgress() {
  if (progressVal < 90) {
    progressVal += (progressVal < 50) ? 2 : 0.5;
    document.getElementById('progress-bar').style.width = progressVal + '%';
  }
}
function startProgressAnim() {
  progressVal = 0;
  document.getElementById('progress-wrap').style.display = 'block';
  document.getElementById('progress-bar').style.width = '0%';
  progTimer = setInterval(animateProgress, 200);
}
function stopProgressAnim() {
  clearInterval(progTimer);
  document.getElementById('progress-bar').style.width = '100%';
  setTimeout(function() { document.getElementById('progress-wrap').style.display = 'none'; }, 600);
}

function fetchDevices() {
  fetch('/api/devices')
    .then(function(r) { return r.json(); })
    .then(function(data) {
      updateStats(data.stats);
      renderDevices(data.devices);
      var btn  = document.getElementById('scan-btn');
      var info = document.getElementById('scan-info');
      if (data.scanning) {
        btn.disabled = true;
        btn.textContent = 'Scan en cours…';
        info.style.display = 'block';
        info.textContent = '⟳ Scan en cours : ARP → hostnames → UPnP/SSDP → DNS-SD → Ports TCP — résultats progressifs';
        if (!pollTimer) pollTimer = setInterval(fetchDevices, 2000);
      } else {
        btn.disabled = false;
        btn.textContent = 'Scanner';
        info.style.display = 'none';
        stopProgressAnim();
        if (pollTimer) { clearInterval(pollTimer); pollTimer = null; }
        var n = data.devices ? data.devices.length : 0;
        document.getElementById('footer-ts').textContent =
          (n ? n + ' équipement(s) — ' : '') + 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
      }
    });
}

function editAlias(btn) {
  var key      = btn.getAttribute('data-key');
  var current  = btn.getAttribute('data-alias') || '';
  var value = prompt('Alias pour cet équipement (laisser vide pour effacer) :', current);
  if (value === null) return;
  var body = 'mac=' + encodeURIComponent(key) + '&alias=' + encodeURIComponent(value);
  fetch('/api/alias', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
    .then(function() { fetchDevices(); });
}

function triggerScan() {
  document.getElementById('scan-btn').disabled = true;
  document.getElementById('scan-btn').textContent = 'Démarrage…';
  startProgressAnim();
  fetch('/api/scan', { method: 'POST' }).then(function() { fetchDevices(); });
}

fetchDevices();
setInterval(function() { if (!pollTimer) fetchDevices(); }, 60000);
