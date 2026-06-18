function fmtUptime(ms) {
  var s = Math.floor(ms / 1000);
  var m = Math.floor(s / 60); s %= 60;
  var h = Math.floor(m / 60); m %= 60;
  var d = Math.floor(h / 24); h %= 24;
  return (d > 0 ? d + 'j ' : '') + h + 'h ' + m + 'm ' + s + 's';
}
function rssiQuality(r) {
  if (r >= -50) return 'Excellent';
  if (r >= -60) return 'Bon';
  if (r >= -70) return 'Moyen';
  return 'Faible';
}
function refresh() {
  fetch('/api/status')
    .then(function(r) { return r.json(); })
    .then(function(d) {
      document.getElementById('ssid').textContent     = d.ssid || '—';
      document.getElementById('ip').textContent       = d.ip   || '—';
      document.getElementById('rssi').textContent     = d.rssi + ' dBm (' + rssiQuality(d.rssi) + ')';
      document.getElementById('hostname').textContent = d.hostname ? d.hostname + '.local' : '—';
      document.getElementById('uptime').textContent   = fmtUptime(d.uptime || 0);
      if (d.version) {
        document.getElementById('site-ver').textContent   = 'v' + d.version;
        document.getElementById('footer-mdns').textContent = d.hostname ? d.hostname + '.local' : '';
      }
      document.getElementById('status-badge').className   = 'badge badge-ok';
      document.getElementById('status-badge').textContent = 'Connecté';
      document.getElementById('footer-ts').textContent    = 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
    })
    .catch(function() {
      document.getElementById('status-badge').className   = 'badge badge-warn';
      document.getElementById('status-badge').textContent = 'Erreur';
      document.getElementById('footer-ts').textContent    = 'Erreur de connexion';
    });
}
function fmtMs(ms) {
  if (!ms) return '—';
  if (ms < 1000) return ms + ' ms';
  return (ms / 1000).toFixed(1) + ' s';
}

function fmtBytes(b) {
  if (b === undefined || b === null) return '—';
  if (b < 1024) return b + ' o';
  return (b / 1024).toFixed(0) + ' Ko';
}

function fetchDiagnostics() {
  fetch('/api/diagnostics')
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (d.error) return;
      var bar = document.getElementById('diag-bar');
      if (!bar) return;
      bar.style.display = 'flex';
      document.getElementById('diag-heap').textContent  = 'Heap libre : '  + fmtBytes(d.freeHeap);
      document.getElementById('diag-psram').textContent = 'PSRAM libre : ' + fmtBytes(d.freePsram);
      document.getElementById('diag-fs').textContent     = 'LittleFS : '   + fmtBytes(d.fsUsedBytes) + ' / ' + fmtBytes(d.fsTotalBytes);
      document.getElementById('diag-scan').textContent   = 'Scan moyen : ' + fmtMs(d.avgScanMs);
      document.getElementById('diag-rescan').textContent = 'Passe précise moyenne : ' + fmtMs(d.avgRescanMs);
    })
    .catch(function() {});
}

refresh();
fetchDiagnostics();
setInterval(refresh, 10000);
setInterval(fetchDiagnostics, 30000);
