// Page temporaire de débogage — voir docs/DEVELOPMENT.md pour la retirer.

function esc(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

// esp_reset_reason_t : codes "anormaux" mis en evidence (voir _isCrashReason() dans boot_log.cpp)
var CRASH_REASON_CODES = [4, 5, 6, 7, 9]; // PANIC, INT_WDT, TASK_WDT, WDT, BROWNOUT

function renderEntry(e, index, total) {
  var cls = CRASH_REASON_CODES.indexOf(e.resetReasonCode) !== -1 ? 'hist-offline' : 'hist-online';
  var lines = (e.lines || []).map(function(l) { return esc(l); }).join('<br>');

  var meta = [];
  meta.push('Boot #' + esc(e.bootCount) + ' / crash #' + esc(e.crashCount));
  if (typeof e.temperature === 'number') meta.push(e.temperature.toFixed(1) + ' °C');
  if (e.lastTask) meta.push('Dernière tâche : ' + esc(e.lastTask));
  if (e.wifiIp) meta.push('WiFi ' + esc(e.wifiIp) + ' (RSSI ' + esc(e.wifiRssi) + ' dBm)');
  if (typeof e.uptimeAtResetMs === 'number') meta.push('Uptime avant reset : ' + Math.round(e.uptimeAtResetMs / 1000) + ' s');
  if (typeof e.freeHeapAtReset === 'number') meta.push('Heap libre avant reset : ' + e.freeHeapAtReset + ' o (bloc max ' + esc(e.largestBlockAtReset) + ' o)');

  var stats = e.lastStats;
  var statsLine = stats ?
    'Dernier instantané — uptime ' + Math.round(stats.uptime / 1000) + ' s, heap ' + stats.freeHeap +
    ' o, bloc max ' + stats.largestBlock + ' o, équipements ' + stats.devicesCount +
    ', pages servies ' + stats.pagesServed + ', appels API ' + stats.apiCalls
    : '';

  return '<div class="hist-entry ' + cls + '">' +
           '<span class="hist-icon">🔁</span>' +
           '<div class="hist-body">' +
             '<div class="hist-title">Boot #' + (total - index) + ' — ' + esc(e.resetReason) + '</div>' +
             '<div class="hist-detail">' + meta.join(' · ') + '</div>' +
             (statsLine ? '<div class="hist-detail">' + esc(statsLine) + '</div>' : '') +
             (lines ? '<div class="hist-detail">' + lines + '</div>' : '<div class="hist-detail">(aucun log capturé avant ce reset)</div>') +
           '</div>' +
         '</div>';
}

function loadBootLog() {
  fetch('/api/bootlog')
    .then(function(r) { return r.json(); })
    .then(function(list) {
      list = list || [];
      list.reverse(); // plus récent en premier
      var container = document.getElementById('bootlog-list');
      var empty     = document.getElementById('bootlog-empty');
      if (!list.length) {
        container.innerHTML = '';
        empty.style.display = 'block';
        return;
      }
      empty.style.display = 'none';
      var total = list.length;
      container.innerHTML = list.map(function(e, i) { return renderEntry(e, i, total); }).join('');
      document.getElementById('footer-ts').textContent = 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
    })
    .catch(function() {});
}

function clearBootLog() {
  if (!confirm('Vider le journal de redémarrage ?')) return;
  fetch('/api/bootlog', { method: 'DELETE' }).then(function() { loadBootLog(); });
}

fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) document.getElementById('site-ver').textContent = 'v' + d.version;
}).catch(function() {});

loadBootLog();
setInterval(loadBootLog, 30000);
