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

// Nom "humain" du materiel - deduit de model/manufacturer/category, pour donner
// un intitule comprehensible au-dessus du hostname brut (ex: "device-72" -> "Google Nest Hub")
function humanDeviceName(d) {
  if (d.model) return d.model;
  if (d.manufacturer) return d.type ? d.manufacturer + ' ' + d.type : d.manufacturer;
  if (d.category) return d.category;
  return '';
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
    'NetBIOS':      { cls: 'source-netbios',   label: 'NetBIOS',   title: 'Nom resolu via NetBIOS Node Status (UDP 137) - PC Windows / Samba' },
    'SNMP':         { cls: 'source-snmp',      label: 'SNMP',      title: 'sysDescr SNMP (UDP 161) — fabricant/modèle en texte clair' },
    'Cast':         { cls: 'source-cast',      label: 'Cast',      title: 'API Google Cast (/setup/eureka_info)' },
    'Sonos':        { cls: 'source-sonos',     label: 'Sonos',     title: 'API Sonos (/xml/device_description.xml)' },
    'Roku':         { cls: 'source-roku',      label: 'Roku',      title: 'API Roku (/query/device-info)' },
    'SamsungTV':    { cls: 'source-samsung',   label: 'Samsung',   title: 'API Samsung Smart TV (/api/v2/)' },
    'MQTT':         { cls: 'source-mqtt',      label: 'MQTT',      title: 'Broker MQTT (port 1883) — CONNECT + topics $SYS/broker/*' },
    'DHCP':         { cls: 'source-dhcp',      label: 'DHCP',      title: 'Nom resolu via fingerprinting DHCP passif (option 12, UDP 67)' }
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

var lastDevices = [];

function populateFilterOptions(devices) {
  var catSel = document.getElementById('filter-category');
  var mfrSel = document.getElementById('filter-manufacturer');
  var prevCat = catSel.value;
  var prevMfr = mfrSel.value;
  var cats = Array.from(new Set(devices.map(function(d) { return d.category; }).filter(Boolean))).sort();
  var mfrs = Array.from(new Set(devices.map(function(d) { return d.manufacturer; }).filter(Boolean))).sort();
  catSel.innerHTML = '<option value="">Tous</option>' +
    cats.map(function(c) { return '<option value="' + esc(c) + '">' + esc(c) + '</option>'; }).join('');
  mfrSel.innerHTML = '<option value="">Tous</option>' +
    mfrs.map(function(m) { return '<option value="' + esc(m) + '">' + esc(m) + '</option>'; }).join('');
  catSel.value = prevCat;
  mfrSel.value = prevMfr;
}

function applyFilters(devices) {
  var cat = document.getElementById('filter-category').value;
  var mfr = document.getElementById('filter-manufacturer').value;
  var favOnly    = document.getElementById('filter-favorite').checked;
  var onlineOnly = document.getElementById('filter-online').checked;
  return devices.filter(function(d) {
    if (cat && d.category !== cat) return false;
    if (mfr && d.manufacturer !== mfr) return false;
    if (favOnly && !d.favorite) return false;
    if (onlineOnly && !d.online) return false;
    return true;
  });
}

function renderDevices(allDevices) {
  lastDevices = allDevices || [];
  populateFilterOptions(lastDevices);
  var tbody = document.getElementById('devices-body');
  var meta  = document.getElementById('scan-meta');
  var devices = applyFilters(lastDevices);
  if (!lastDevices.length) {
    tbody.innerHTML = '<tr><td colspan="8" class="empty-msg">Aucun équipement détecté</td></tr>';
    meta.textContent = '0 équipement';
    return;
  }
  if (!devices.length) {
    tbody.innerHTML = '<tr><td colspan="8" class="empty-msg">Aucun équipement ne correspond aux filtres</td></tr>';
    meta.textContent = '0 équipement affiché sur ' + lastDevices.length;
    return;
  }
  var onlineCount = devices.filter(function(d) { return d.online; }).length;
  meta.textContent = onlineCount + ' équipement' + (onlineCount > 1 ? 's' : '') + ' en ligne' +
    (devices.length !== lastDevices.length ? ' (' + devices.length + '/' + lastDevices.length + ' affichés)' : '');
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
    var humanName = humanDeviceName(d);
    var humanHtml = (humanName && humanName !== displayName)
      ? '<div class="name-human">' + esc(humanName) + '</div>'
      : '';
    var nameHtml = displayName
      ? '<div class="name-cell" title="' + esc(displayName) + '">' + humanHtml +
          '<div class="name-raw">' + esc(displayName) +
            (d.alias ? '<span class="alias-tag" title="Alias personnalisé"> ★</span>' : sourceBadge(d.source)) +
          '</div>' +
          editBtn +
        '</div>'
      : humanHtml + '<span class="none">—</span>' + editBtn;
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
      ? '<span class="type-badge ' + categoryClass(d.category) + '">' + esc(d.category) + '</span>' +
        (d.type ? '<div class="subtype-tag">' + esc(d.type) + '</div>' : '') + confBadge(d)
      : '<span class="none">—</span>' + confBadge(d);
    var seenHtml = d.online
      ? fmtSeen(d.elapsedMs)
      : '<span class="none" title="Non vu lors du dernier scan">hors ligne</span>';
    if (d.seenCount > 0)
      seenHtml += '<div class="seen-count" title="Nombre de scans où cet équipement a été vu en ligne">vu ' + d.seenCount + 'x</div>';
    var rescanBtn = d.ip
      ? '<button class="rescan-btn" title="Scan rapide (2-5s) — confirme l\'identité et améliore la confiance" ' +
        'data-ip="' + esc(d.ip) + '" data-mode="quick" onclick="rescanDevice(this)">⟲</button>' +
        '<button class="rescan-btn rescan-btn-deep" title="Scan approfondi (15-60s) — récupère un maximum d\'informations" ' +
        'data-ip="' + esc(d.ip) + '" data-mode="deep" onclick="rescanDevice(this)">⟲⟲</button>'
      : '';
    var favKey = d.mac || d.ip;
    var favBtn = '<button class="fav-btn ' + (d.favorite ? 'fav-on' : '') + '" ' +
      'title="' + (d.favorite ? 'Retirer des favoris' : 'Marquer comme favori') + '" ' +
      'data-key="' + esc(favKey) + '" data-fav="' + (d.favorite ? '1' : '0') + '" ' +
      'onclick="toggleFavorite(this)">' + (d.favorite ? '★' : '☆') + '</button>';
    var noteCount = (d.notes && d.notes.length) || 0;
    var notesBtn = '<button class="notes-btn" title="Notes (' + noteCount + ')" ' +
      'data-key="' + esc(favKey) + '" onclick="toggleNotesRow(this)">📝' +
      (noteCount ? '<span class="notes-count">' + noteCount + '</span>' : '') + '</button>';
    var notesData = 'data-notes=\'' + esc(JSON.stringify(d.notes || [])) + '\'';
    return '<tr' + (d.online ? '' : ' class="row-offline"') + ' ' + notesData + '>' +
      '<td class="status-cell">' + statusHtml + '</td>' +
      '<td class="ip-cell">'     + esc(d.ip) + rescanBtn + '</td>' +
      '<td>'                     + nameHtml    + '</td>' +
      '<td>'                     + mfrHtml     + '</td>' +
      '<td>'                     + catHtml     + '</td>' +
      '<td class="mac-cell">'    + esc(d.mac)  + '</td>' +
      '<td class="seen-cell">'   + seenHtml    + '</td>' +
      '<td class="actions-cell">' + favBtn + notesBtn + '</td>' +
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
        // Matérialise la barre même si le scan a démarré ailleurs (ex: scan
        // automatique au boot) et non via un clic sur "Scanner" dans cette page.
        if (document.getElementById('progress-wrap').style.display !== 'block') startProgressAnim();
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

function toggleFavorite(btn) {
  var key = btn.getAttribute('data-key');
  var fav = btn.getAttribute('data-fav') === '1';
  var body = 'mac=' + encodeURIComponent(key) + '&favorite=' + (fav ? '0' : '1');
  fetch('/api/favorite', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
    .then(function() { fetchDevices(); });
}

var NOTES_ROW_ID = 'notes-row';

function fmtNoteDate(ts) {
  if (!ts) return '';
  return new Date(ts * 1000).toLocaleString('fr-FR');
}

function toggleNotesRow(btn) {
  var row = btn.closest('tr');
  var existing = document.getElementById(NOTES_ROW_ID);
  if (existing) {
    var wasForRow = existing.previousSibling === row;
    existing.remove();
    if (wasForRow) return;
  }
  var key = btn.getAttribute('data-key');
  var notes = [];
  try { notes = JSON.parse(row.getAttribute('data-notes') || '[]'); } catch (e) {}

  var newRow = document.createElement('tr');
  newRow.id = NOTES_ROW_ID;
  var nCols = row.children.length || 8;
  var listHtml = notes.length
    ? notes.map(function(n) {
        return '<li class="note-item">' +
          '<span class="note-date">' + esc(fmtNoteDate(n.ts)) + '</span>' +
          '<span class="note-text">' + esc(n.text) + '</span>' +
          '<button class="note-del" data-key="' + esc(key) + '" data-ts="' + n.ts + '" onclick="deleteNote(this)">✕</button>' +
        '</li>';
      }).join('')
    : '<li class="note-empty">Aucune note</li>';
  newRow.innerHTML =
    '<td colspan="' + nCols + '">' +
      '<div class="notes-panel">' +
        '<ul class="notes-list">' + listHtml + '</ul>' +
        '<div class="notes-add">' +
          '<input type="text" class="notes-input" placeholder="Ajouter une note (ex: cartouche changée le 12/05)" />' +
          '<button class="notes-add-btn" data-key="' + esc(key) + '" onclick="addNote(this)">Ajouter</button>' +
        '</div>' +
      '</div>' +
    '</td>';
  row.parentNode.insertBefore(newRow, row.nextSibling);
}

function addNote(btn) {
  var key   = btn.getAttribute('data-key');
  var input = btn.parentNode.querySelector('.notes-input');
  var text  = (input.value || '').trim();
  if (!text) return;
  var body = 'mac=' + encodeURIComponent(key) + '&text=' + encodeURIComponent(text);
  fetch('/api/notes', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
    .then(function() { fetchDevices(); });
}

function deleteNote(btn) {
  var key = btn.getAttribute('data-key');
  var ts  = btn.getAttribute('data-ts');
  var body = 'mac=' + encodeURIComponent(key) + '&ts=' + encodeURIComponent(ts);
  fetch('/api/notes', { method: 'DELETE', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
    .then(function() { fetchDevices(); });
}

var RESCAN_ROW_ID = 'rescan-progress-row';

function rescanModeLabel(mode) {
  return mode === 'deep' ? 'Analyse approfondie' : 'Passe rapide';
}

function showRescanRow(afterRow, ip, mode, step, percent) {
  var row = document.getElementById(RESCAN_ROW_ID);
  if (!row) {
    row = document.createElement('tr');
    row.id = RESCAN_ROW_ID;
    var nCols = afterRow.children.length || 7;
    row.innerHTML =
      '<td colspan="' + nCols + '">' +
        '<div class="rescan-row-banner">' +
          '<span class="rescan-row-text" id="rescan-row-text"></span>' +
          '<div class="rescan-row-bar-wrap"><div class="rescan-row-bar" id="rescan-row-bar"></div></div>' +
          '<span class="rescan-row-pct" id="rescan-row-pct"></span>' +
        '</div>' +
      '</td>';
  }
  if (afterRow.nextSibling !== row) {
    afterRow.parentNode.insertBefore(row, afterRow.nextSibling);
  }
  document.getElementById('rescan-row-text').textContent = rescanModeLabel(mode) + ' sur ' + ip + ' — ' + step;
  document.getElementById('rescan-row-bar').style.width = (percent || 0) + '%';
  document.getElementById('rescan-row-pct').textContent = (percent || 0) + '%';
}

function showRescanLog(afterRow, ip, mode, log) {
  var row = document.getElementById(RESCAN_ROW_ID);
  if (!row) return;
  document.getElementById('rescan-row-text').textContent = rescanModeLabel(mode) + ' sur ' + ip + ' — ' + (log || []).join(' · ');
  document.getElementById('rescan-row-bar').style.width = '100%';
  document.getElementById('rescan-row-pct').textContent = '100%';
}

function hideRescanRow() {
  var row = document.getElementById(RESCAN_ROW_ID);
  if (row) row.remove();
}

function pollRescanStatus(btn, prevHtml, ip, row, mode) {
  fetch('/api/devices/rescan/status').then(function(r) { return r.json(); }).then(function(s) {
    if (s.running) {
      var pct = s.percent || 0;
      btn.textContent = pct + '%';
      btn.title = rescanModeLabel(mode) + ' en cours — ' + (s.step || '…');
      showRescanRow(row, ip, mode, s.step || '…', pct);
      setTimeout(function() { pollRescanStatus(btn, prevHtml, ip, row, mode); }, 500);
    } else {
      btn.disabled = false;
      btn.textContent = prevHtml;
      btn.removeAttribute('title');
      showRescanLog(row, ip, mode, s.log);
      setTimeout(hideRescanRow, 2500);
      fetchDevices();
    }
  }).catch(function() {
    btn.disabled = false;
    btn.textContent = prevHtml;
    hideRescanRow();
  });
}

function rescanDevice(btn) {
  var ip   = btn.getAttribute('data-ip');
  var mode = btn.getAttribute('data-mode') || 'quick';
  var row = btn.closest('tr');
  btn.disabled = true;
  var prevHtml = btn.textContent;
  btn.textContent = '…';
  btn.title = rescanModeLabel(mode) + ' en cours — Démarrage';
  showRescanRow(row, ip, mode, 'Démarrage', 0);
  fetch('/api/devices/rescan', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: 'ip=' + encodeURIComponent(ip) + '&mode=' + encodeURIComponent(mode) })
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (d.error) {
        alert('Réinterrogation impossible : ' + d.error);
        btn.disabled = false;
        btn.textContent = prevHtml;
        hideRescanRow();
        return;
      }
      pollRescanStatus(btn, prevHtml, ip, row, mode);
    })
    .catch(function() {
      btn.disabled = false;
      btn.textContent = prevHtml;
      hideRescanRow();
    });
}

function triggerScan() {
  document.getElementById('scan-btn').disabled = true;
  document.getElementById('scan-btn').textContent = 'Démarrage…';
  startProgressAnim();
  fetch('/api/scan', { method: 'POST' }).then(function() { fetchDevices(); });
}

function toggleResetMenu() {
  document.getElementById('reset-menu-list').classList.toggle('open');
}

function toggleDataMenu() {
  document.getElementById('data-menu-list').classList.toggle('open');
}

document.addEventListener('click', function(e) {
  var menu = document.getElementById('reset-menu-list');
  if (menu && menu.classList.contains('open') && !e.target.closest('.reset-menu')) {
    menu.classList.remove('open');
  }
  var dataMenu = document.getElementById('data-menu-list');
  if (dataMenu && dataMenu.classList.contains('open') && !e.target.closest('.reset-menu')) {
    dataMenu.classList.remove('open');
  }
});

['filter-category', 'filter-manufacturer', 'filter-favorite', 'filter-online'].forEach(function(id) {
  document.getElementById(id).addEventListener('change', function() { renderDevices(lastDevices); });
});

function resetDevices(keepAlias, keepManufacturer) {
  document.getElementById('reset-menu-list').classList.remove('open');
  var msg = 'Effacer la liste des équipements';
  if (keepAlias && keepManufacturer) msg += ', en conservant ceux avec un alias ou un fabricant connu';
  else if (keepAlias) msg += ', en conservant ceux avec un alias';
  else if (keepManufacturer) msg += ', en conservant ceux avec un fabricant connu';
  msg += ' ?';
  if (!confirm(msg)) return;

  var body = 'keepAlias=' + (keepAlias ? '1' : '0') + '&keepManufacturer=' + (keepManufacturer ? '1' : '0');
  fetch('/api/devices/reset', { method: 'POST', headers: { 'Content-Type': 'application/x-www-form-urlencoded' }, body: body })
    .then(function() { fetchDevices(); });
}

fetchDevices();
setInterval(function() { if (!pollTimer) fetchDevices(); }, 60000);
