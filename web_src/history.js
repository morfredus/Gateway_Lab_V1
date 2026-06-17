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

var historyData = [];

function activeFilters() {
  var checked = {};
  document.querySelectorAll('.hist-filter:checked').forEach(function(cb) { checked[cb.value] = true; });
  return checked;
}

function renderHistory() {
  var filters   = activeFilters();
  var filtered  = historyData.filter(function(e) { return filters[e.event]; });
  var container = document.getElementById('history-list');
  var empty     = document.getElementById('history-empty');
  if (!filtered.length) {
    container.innerHTML = '';
    empty.style.display = 'block';
    empty.textContent = historyData.length ? 'Aucun événement ne correspond aux filtres sélectionnés.' : 'Aucun événement enregistré pour le moment.';
    return;
  }
  empty.style.display = 'none';
  container.innerHTML = filtered.map(renderEntry).join('');
}

document.querySelectorAll('.hist-filter').forEach(function(cb) {
  cb.addEventListener('change', renderHistory);
});

function loadHistory() {
  fetch('/api/history')
    .then(function(r) { return r.json(); })
    .then(function(list) {
      historyData = list || [];
      renderHistory();
      document.getElementById('footer-ts').textContent = 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
    })
    .catch(function() {});
}

function clearHistory() {
  if (!historyData.length) {
    if (!confirm('Le journal est déjà vide. Continuer ?')) return;
  } else if (!confirm('Vider le journal d\'historique ? Une sauvegarde sera téléchargée avant suppression.')) {
    return;
  }

  function doClear() {
    fetch('/api/history', { method: 'DELETE' }).then(function() { loadHistory(); });
  }

  if (!historyData.length) { doClear(); return; }

  var blob = new Blob([JSON.stringify(historyData, null, 2)], { type: 'application/json' });
  var url  = URL.createObjectURL(blob);
  var a    = document.createElement('a');
  a.href = url;
  a.download = 'gateway-lab-historique-' + new Date().toISOString().slice(0, 19).replace(/[:T]/g, '-') + '.json';
  document.body.appendChild(a);
  a.click();
  document.body.removeChild(a);
  URL.revokeObjectURL(url);

  doClear();
}

fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) document.getElementById('site-ver').textContent = 'v' + d.version;
}).catch(function() {});

loadHistory();
setInterval(loadHistory, 30000);
