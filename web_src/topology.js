fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) document.getElementById('site-ver').textContent = 'v' + d.version;
}).catch(function() {});

function esc(s) {
  if (!s) return '';
  return String(s).replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
}

// Vue simplifiée en attendant la cartographie graphique (roadmap v0.4.x) :
// regroupe la passerelle, les équipements identifiés comme routeur/point
// d'accès, puis le reste des équipements connus.
function renderTopology(devices) {
  var list  = document.getElementById('topology-list');
  var empty = document.getElementById('topology-empty');
  if (!devices || !devices.length) {
    list.innerHTML = '';
    empty.style.display = 'block';
    return;
  }
  empty.style.display = 'none';
  var gateways = devices.filter(function(d) { return d.category === 'Gateway' || d.category === 'Router'; });
  var others   = devices.filter(function(d) { return d.category !== 'Gateway' && d.category !== 'Router'; });

  function entry(d) {
    var name = d.alias || d.hostname || d.ip;
    return '<li class="hist-entry"><span class="note-text">' + esc(name) +
      ' — ' + esc(d.ip) + (d.manufacturer ? ' (' + esc(d.manufacturer) + ')' : '') + '</span></li>';
  }

  list.innerHTML =
    '<p class="card-meta">Passerelle(s) / routeur(s) détecté(s)</p>' +
    '<ul class="notes-list">' + (gateways.length ? gateways.map(entry).join('') : '<li class="note-empty">Aucun</li>') + '</ul>' +
    '<p class="card-meta" style="margin-top:1rem">Équipements rattachés (' + others.length + ')</p>' +
    '<ul class="notes-list">' + (others.length ? others.map(entry).join('') : '<li class="note-empty">Aucun</li>') + '</ul>';
}

fetch('/api/devices')
  .then(function(r) { return r.json(); })
  .then(function(data) {
    renderTopology(data.devices);
    document.getElementById('footer-ts').textContent = 'Actualisé : ' + new Date().toLocaleTimeString('fr-FR');
  })
  .catch(function() {});
