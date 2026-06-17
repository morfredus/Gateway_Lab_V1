// Récupère la version au chargement (commun à toutes les pages)
fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) document.getElementById('site-ver').textContent = 'v' + d.version;
}).catch(function() {});

function refreshWifiStatus() {
  fetch('/api/wifi', { cache: 'no-store' }).then(function(r) { return r.json(); }).then(function(d) {
    var badge = document.getElementById('status-badge');
    badge.textContent = d.connected ? 'Connecté' : 'Déconnecté';
    badge.className = 'badge ' + (d.connected ? 'badge-ok' : 'badge-warn');

    document.getElementById('wifi-ssid').textContent = d.ssid || '—';
    document.getElementById('wifi-ip').textContent   = d.ip   || '—';
    document.getElementById('wifi-rssi').textContent = d.rssi ? (d.rssi + ' dBm') : '—';
    document.getElementById('footer-ts').textContent =
      'Actualisé : ' + new Date().toLocaleTimeString();

    renderNetworks(d.networks || []);
  }).catch(function() {});
}

function renderNetworks(networks) {
  var list  = document.getElementById('wifi-list');
  var empty = document.getElementById('wifi-empty');

  if (!networks.length) {
    list.innerHTML = '';
    empty.style.display = 'block';
    return;
  }
  empty.style.display = 'none';

  list.innerHTML = networks.map(function(n) {
    return '<div class="info-row">' +
      '<span class="info-label">' + escapeHtml(n.ssid) + '</span>' +
      '<button class="alias-edit" data-ssid="' + escapeHtml(n.ssid) + '" title="Supprimer">✕ Supprimer</button>' +
      '</div>';
  }).join('');

  list.querySelectorAll('.alias-edit').forEach(function(btn) {
    btn.addEventListener('click', function() {
      if (!confirm('Supprimer le réseau "' + btn.dataset.ssid + '" ?')) return;
      fetch('/api/wifi?ssid=' + encodeURIComponent(btn.dataset.ssid), { method: 'DELETE' })
        .then(function(r) { return r.json(); })
        .then(function() { refreshWifiStatus(); })
        .catch(function() {});
    });
  });
}

function escapeHtml(s) {
  return String(s).replace(/[&<>"']/g, function(c) {
    return { '&': '&amp;', '<': '&lt;', '>': '&gt;', '"': '&quot;', "'": '&#39;' }[c];
  });
}

document.getElementById('wifi-form').addEventListener('submit', function(e) {
  e.preventDefault();
  var ssid     = document.getElementById('wifi-new-ssid').value.trim();
  var password = document.getElementById('wifi-new-password').value;
  var msg = document.getElementById('wifi-msg');

  if (!ssid) {
    msg.textContent = 'Le SSID est requis.';
    return;
  }

  var fd = new FormData();
  fd.append('ssid', ssid);
  fd.append('password', password);

  fetch('/api/wifi', { method: 'POST', body: fd })
    .then(function(r) { return r.json(); })
    .then(function(d) {
      if (d.status === 'ok') {
        msg.style.color = '#10b981';
        msg.textContent = 'Réseau enregistré.';
        document.getElementById('wifi-form').reset();
        refreshWifiStatus();
      } else {
        msg.style.color = '#ef4444';
        msg.textContent = 'Erreur : ' + (d.error || 'inconnue');
      }
    })
    .catch(function() {
      msg.style.color = '#ef4444';
      msg.textContent = 'Erreur réseau.';
    });
});

refreshWifiStatus();
setInterval(refreshWifiStatus, 10000);
