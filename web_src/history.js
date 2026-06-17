var EVENT_LABEL = {
  'new':     { icon: '🆕', text: 'Nouvel équipement', cls: 'hist-new' },
  'online':  { icon: '🟢', text: 'Reconnecté',         cls: 'hist-online' },
  'offline': { icon: '🔴', text: 'Hors ligne',         cls: 'hist-offline' },
  'changed': { icon: '✏️', text: 'Changement',         cls: 'hist-changed' }
};

var FIELD_LABEL = {
  'ip': 'Adresse IP', 'manufacturer': 'Fabricant', 'category': 'Catégorie',
  'hostname': 'Nom d\'hôte', 'openPorts': 'Ports ouverts'
};

function esc(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;').replace(/"/g,'&quot;');
}

function fmtDate(epoch) {
  if (!epoch) return 'Date inconnue';
  return new Date(epoch * 1000).toLocaleString('fr-FR');
}

function renderEntry(e) {
  var meta = EVENT_LABEL[e.event] || { icon: '•', text: e.event, cls: '' };
  var detail = '';
  if (e.event === 'changed') {
    var fieldName = FIELD_LABEL[e.field] || e.field;
    detail = '<div class="hist-detail">' + esc(fieldName) + ' : <span class="hist-old">' +
              esc(e.oldValue) + '</span> → <span class="hist-new-val">' + esc(e.newValue) + '</span></div>';
  }
  return '<div class="hist-entry ' + meta.cls + '">' +
           '<span class="hist-icon">' + meta.icon + '</span>' +
           '<div class="hist-body">' +
             '<div class="hist-title">' + esc(meta.text) + ' - ' + esc(e.label || e.ip) + '</div>' +
             detail +
             '<div class="hist-time">' + fmtDate(e.epoch) + ' · ' + esc(e.ip) + '</div>' +
           '</div>' +
         '</div>';
}

function loadHistory() {
  fetch('/api/history')
    .then(function(r) { return r.json(); })
    .then(function(list) {
      var container = document.getElementById('history-list');
      var empty     = document.getElementById('history-empty');
      if (!list || !list.length) {
        container.innerHTML = '';
        empty.style.display = 'block';
        return;
      }
      empty.style.display = 'none';
      container.innerHTML = list.map(renderEntry).join('');
      document.getElementById('footer-ts').textContent = 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
    })
    .catch(function() {});
}

fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) document.getElementById('site-ver').textContent = 'v' + d.version;
}).catch(function() {});

loadHistory();
setInterval(loadHistory, 30000);
