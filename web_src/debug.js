// Page temporaire de débogage — voir docs/DEVELOPMENT.md pour la retirer.

function esc(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

var REASON_CLASS = {
  0: 'hist-changed', 1: 'hist-online', 2: 'hist-online',
  3: 'hist-offline', 4: 'hist-offline', 5: 'hist-offline', 6: 'hist-offline',
  7: 'hist-changed', 8: 'hist-offline', 9: 'hist-changed'
};

function renderEntry(e, index, total) {
  var cls = REASON_CLASS[e.reasonCode] || '';
  var lines = (e.lines || []).map(function(l) { return esc(l); }).join('<br>');
  return '<div class="hist-entry ' + cls + '">' +
           '<span class="hist-icon">🔁</span>' +
           '<div class="hist-body">' +
             '<div class="hist-title">Boot #' + (total - index) + ' — ' + esc(e.reasonText) + '</div>' +
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
