// Récupère la version au chargement (commun à toutes les pages)
fetch('/api/status').then(function(r) { return r.json(); }).then(function(d) {
  if (d.version) {
    document.getElementById('site-ver').textContent = 'v' + d.version;
    document.getElementById('fw-version').textContent = 'v' + d.version;
  }
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

function refreshLedBrightness() {
  fetch('/api/led/brightness', { cache: 'no-store' }).then(function(r) { return r.json(); }).then(function(d) {
    document.getElementById('led-brightness').value = d.brightness;
    document.getElementById('led-brightness-value').textContent = d.brightness;
  }).catch(function() {});
}

var ledBrightnessInput = document.getElementById('led-brightness');
ledBrightnessInput.addEventListener('input', function() {
  document.getElementById('led-brightness-value').textContent = ledBrightnessInput.value;
});
ledBrightnessInput.addEventListener('change', function() {
  var fd = new FormData();
  fd.append('value', ledBrightnessInput.value);
  fetch('/api/led/brightness', { method: 'POST', body: fd }).catch(function() {});
});

function fmtBytes(b) {
  if (b === undefined || b === null) return '—';
  if (b < 1024) return b + ' o';
  return (b / 1024).toFixed(0) + ' Ko';
}

function refreshHealth() {
  fetch('/api/diagnostics', { cache: 'no-store' }).then(function(r) { return r.json(); }).then(function(d) {
    if (d.error) return;
    var badge = document.getElementById('health-badge');
    var msg   = document.getElementById('health-msg');
    if (d.degraded) {
      badge.textContent = 'Mode dégradé';
      badge.className = 'badge badge-warn';
      msg.textContent = 'Mémoire critique — nouveaux scans, rescans, notes et modifications de '
        + 'configuration désactivés. L\'inventaire déjà acquis reste consultable. '
        + (d.degradedReason || '') + '.';
    } else {
      badge.textContent = 'Normal';
      badge.className = 'badge badge-ok';
      msg.textContent = 'Mémoire disponible suffisante.';
    }
    document.getElementById('sys-heap').textContent    = fmtBytes(d.freeHeap);
    document.getElementById('sys-psram').textContent   = fmtBytes(d.freePsram);
    document.getElementById('sys-devices').textContent = (d.deviceCount !== undefined ? d.deviceCount : '—')
      + ' / ' + (d.maxDevices !== undefined ? d.maxDevices : '—');
    document.getElementById('sys-history').textContent = (d.historyCount !== undefined ? d.historyCount : '—')
      + ' événement' + (d.historyCount === 1 ? '' : 's');
  }).catch(function() {});
}

document.getElementById('restart-btn').addEventListener('click', function() {
  if (!confirm('Redémarrer Gateway Lab V1 maintenant ?')) return;
  fetch('/api/system/restart', { method: 'POST' }).catch(function() {});
  document.getElementById('health-msg').textContent = 'Redémarrage en cours…';
});

// ── Mise à jour du firmware (ex page OTA) ──────────────────────────────
document.getElementById('ota-form').addEventListener('submit', function(e) {
  e.preventDefault();
  var file = document.getElementById('ota-file').files[0];
  var msg  = document.getElementById('ota-msg');
  if (!file) {
    msg.textContent = 'Aucun fichier sélectionné.';
    return;
  }
  var fd = new FormData();
  fd.append('firmware', file);
  var xhr = new XMLHttpRequest();
  xhr.upload.onprogress = function(e) {
    document.getElementById('ota-progress').style.display = 'block';
    document.getElementById('ota-bar').style.width = (e.loaded / e.total * 100) + '%';
    msg.textContent = 'Transfert : ' + Math.round(e.loaded / e.total * 100) + '%';
  };
  xhr.onload = function() {
    if (xhr.status === 200) {
      msg.style.color = '#10b981';
      msg.textContent = 'Firmware transféré — redémarrage en cours…';
      document.getElementById('ota-form').style.display = 'none';
      waitForOtaReboot();
    } else {
      msg.style.color = '#ef4444';
      msg.textContent = 'Erreur : ' + xhr.responseText;
    }
  };
  xhr.open('POST', '/update');
  xhr.send(fd);
});

function waitForOtaReboot() {
  var msg  = document.getElementById('ota-msg');
  var dot  = 0;
  var dots = ['', '.', '..', '...'];
  setTimeout(function poll() {
    dot = (dot + 1) % 4;
    msg.textContent = 'Redémarrage en cours' + dots[dot];
    fetch('/api/status', { cache: 'no-store' })
      .then(function(r) {
        if (r.ok) {
          msg.textContent = 'Redémarrage terminé — redirection…';
          setTimeout(function() { window.location.href = '/wifi'; }, 800);
        } else {
          setTimeout(poll, 1000);
        }
      })
      .catch(function() { setTimeout(poll, 1000); });
  }, 3000);
}

refreshWifiStatus();
refreshLedBrightness();
refreshHealth();
setInterval(refreshWifiStatus, 10000);
setInterval(refreshHealth, 10000);
