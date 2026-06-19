// Tous les types d'evenements emis par DeviceHistory cote firmware (network_scanner.cpp).
// Depuis v1.0.0, la surveillance continue (_monitorTick) emet reconnected/disappeared/
// mobile_left/mobile_returned/identification_improved en plus de new/online/offline/changed
// utilises par un scan complet — chaque type doit avoir une entree ici pour s'afficher.
var EVENT_LABEL = {
  'new':                     { icon: '🆕', text: 'Nouvel équipement',        cls: 'hist-new' },
  'online':                  { icon: '🟢', text: 'Reconnecté',               cls: 'hist-online' },
  'reconnected':             { icon: '🟢', text: 'Reconnecté',               cls: 'hist-online' },
  'mobile_returned':         { icon: '📱', text: 'Mobile de retour',         cls: 'hist-online' },
  'offline':                 { icon: '🔴', text: 'Hors ligne',               cls: 'hist-offline' },
  'disappeared':             { icon: '🔴', text: 'Disparu',                  cls: 'hist-offline' },
  'mobile_left':             { icon: '📴', text: 'Mobile parti',             cls: 'hist-offline' },
  'changed':                 { icon: '✏️', text: 'Changement',               cls: 'hist-changed' },
  'identification_improved': { icon: '🔍', text: 'Identification améliorée', cls: 'hist-changed' }
};

// Categorie de filtre (case a cocher) associee a chaque type d'evenement reel —
// plusieurs types peuvent partager la meme case (ex: reconnected/mobile_returned
// sous "Reconnexions").
var EVENT_FILTER_CATEGORY = {
  'new':                     'new',
  'online':                  'online',
  'reconnected':             'online',
  'mobile_returned':         'online',
  'offline':                 'offline',
  'disappeared':             'offline',
  'mobile_left':             'offline',
  'changed':                 'changed',
  'identification_improved': 'changed'
};

var FIELD_LABEL = {
  'ip': 'Adresse IP', 'manufacturer': 'Fabricant', 'category': 'Catégorie', 'type': 'Type',
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
  if (e.event === 'changed' || e.event === 'identification_improved') {
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

var historyData  = [];
var favoriteMacs = {};

function activeFilters() {
  var checked = {};
  document.querySelectorAll('.hist-filter:checked').forEach(function(cb) { checked[cb.value] = true; });
  return checked;
}

function renderHistory() {
  var filters    = activeFilters();
  var favOnly    = document.getElementById('hist-filter-favorite').checked;
  var filtered   = historyData.filter(function(e) {
    var category = EVENT_FILTER_CATEGORY[e.event] || e.event;
    if (!filters[category]) return false;
    if (favOnly && !favoriteMacs[e.mac]) return false;
    return true;
  });
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
document.getElementById('hist-filter-favorite').addEventListener('change', renderHistory);

function loadFavorites() {
  fetch('/api/devices')
    .then(function(r) { return r.json(); })
    .then(function(list) {
      favoriteMacs = {};
      (list || []).forEach(function(d) { if (d.favorite) favoriteMacs[d.mac] = true; });
      renderHistory();
    })
    .catch(function() {});
}

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

loadFavorites();
loadHistory();
setInterval(loadHistory, 30000);
setInterval(loadFavorites, 30000);
